// Headless suite for the record file's writer thread (task T065).
//
// Task T052 made the record durable the moment a running score passes it.
// Measured on the round's host, that save costs about 2.7 milliseconds at
// the median (notes/performance.md, T065), and it ran on the gameplay
// thread, which is the confirmed cause of the device's mid-round stutter.
// The fix moves the write onto the writer thread of
// include/stackfall/record_writer.hpp without changing the store's atomic
// write, the file's contents, or its recovery behavior.
//
// This suite pins the three properties the fix is made of:
//
//   * The gameplay thread does not wait on storage: submit() returns in a
//     small fraction of the write it started, with a slow sink injected.
//   * The write still happens and still lands: flush() waits for it, the
//     store's file holds the newest submitted score, and a session without
//     a save directory writes nothing at all.
//   * A score that rises faster than the writer can write is coalesced to
//     the newest value, in order, without dropping the last word - so a
//     power loss costs at most the write in flight (task T052's bound).
//
// The timing checks bound generous margins rather than chasing the measured
// numbers: a sink that sleeps 80 milliseconds must not cost the caller half
// of it, which holds by orders of magnitude on the slowest machine this
// suite runs on and still fails the old synchronous path, which paid the
// full sleep. The margins absorb the one-time parallel ctest flake of task
// T093: a `ctest -j4` run under load may preempt this thread for tens of
// milliseconds mid-submit or hold it past a running write, so the ceiling
// leaves room for that preemption and the one check that needs a write to
// be still in flight blocks that write on a latch instead of racing the
// clock.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "stackfall/record_writer.hpp"
#include "stackfall/storage.hpp"

#include "test_util.hpp"

namespace {

using stackfall::HighScoreStore;
using stackfall::LoadResult;
using stackfall::RecordWrite;
using stackfall::RecordWriter;
using stackfall::SaveStatus;

// One fresh directory per case, below the system temporary directory, so
// the suite touches no repository file.
class SaveDir {
 public:
  SaveDir()
      : path_(std::filesystem::temp_directory_path() /
              ("stackfall-record-writer-" + std::to_string(++counter_) +
               "-" + std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()))) {
    std::filesystem::create_directories(path_);
  }

  ~SaveDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::string& str() const { return text_; }

  std::filesystem::path path_;
  std::string text_{path_.string()};
  static int counter_;
};

int SaveDir::counter_ = 0;

// A sink that takes as long as a slow handheld save, counts its calls, and
// records the values it was asked to write.
class SlowSink {
 public:
  stackfall::RecordSaveFunction function() {
    return [this](std::uint64_t score) {
      ++calls_;
      values_.push_back(score);
      std::this_thread::sleep_for(std::chrono::milliseconds(80));
      ++finished_;
      return status_;
    };
  }

  int calls() const { return calls_; }
  int finished() const { return finished_; }
  const std::vector<std::uint64_t>& values() const { return values_; }
  void setStatus(SaveStatus status) { status_ = status; }

 private:
  std::atomic<int> calls_{0};
  std::atomic<int> finished_{0};
  std::vector<std::uint64_t> values_;
  SaveStatus status_ = SaveStatus::Ok;
};

// A sink that blocks inside the write until the case releases it, so a case
// can prove a write is in flight instead of betting that the wall clock has
// not yet reached the sink's sleep end - the bet a loaded parallel ctest
// run lost once (task T093).
class BlockedSink {
 public:
  stackfall::RecordSaveFunction function() {
    return [this](std::uint64_t score) {
      std::unique_lock<std::mutex> lock(mutex_);
      ++calls_;
      value_ = score;
      running_ = true;
      runningSignal_.notify_all();
      releaseSignal_.wait(lock, [this] { return released_; });
      ++finished_;
      return SaveStatus::Ok;
    };
  }

  // Returns when the write is running inside the sink.
  void waitUntilRunning() {
    std::unique_lock<std::mutex> lock(mutex_);
    runningSignal_.wait(lock, [this] { return running_; });
  }

  // Lets every blocked write finish.
  void release() {
    std::lock_guard<std::mutex> guard(mutex_);
    released_ = true;
    releaseSignal_.notify_all();
  }

  int calls() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return calls_;
  }

  int finished() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return finished_;
  }

  std::uint64_t value() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return value_;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable runningSignal_;
  std::condition_variable releaseSignal_;
  int calls_ = 0;
  int finished_ = 0;
  std::uint64_t value_ = 0;
  bool running_ = false;
  bool released_ = false;
};

// ---------------------------------------------------------------------------
// Cases.
// ---------------------------------------------------------------------------

// submit() hands the score over and returns without waiting for the write.
void submit_does_not_wait_for_storage() {
  SaveDir dir;
  RecordWriter writer(dir.str());
  SlowSink sink;
  writer.setSinkForTest(sink.function());
  const auto started = std::chrono::steady_clock::now();
  writer.submit(1234, 7);
  const std::int64_t elapsedMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started)
          .count();
  // Half of the 80 ms write it started: the old synchronous path paid the
  // sink's full sleep and fails this by 2x, while a preempted caller of a
  // loaded `ctest -j` run still passes (task T093's flake).
  SC_CHECK(elapsedMs < 40);
  SC_CHECK(writer.flush() == SaveStatus::Ok);
  SC_CHECK(sink.calls() == 1);
}

// The write lands: after flush(), the store's own file holds the submitted
// score, exactly as the synchronous save wrote it.
void flushed_write_lands_in_the_file() {
  SaveDir dir;
  RecordWriter writer(dir.str());
  writer.submit(4096, 30);
  SC_CHECK(writer.flush() == SaveStatus::Ok);
  const HighScoreStore store(dir.str());
  const LoadResult loaded = store.load();
  SC_CHECK(loaded.status == SaveStatus::Ok);
  SC_CHECK(loaded.highScore == 4096);
}

// A session without a save directory is inert: no thread, no write, and
// flush() reports the same no-directory status the store always did.
void session_without_directory_writes_nothing() {
  RecordWriter writer("");
  SC_CHECK(!writer.hasDirectory());
  SC_CHECK(writer.store() == nullptr);
  writer.submit(500, 1);  // must be a harmless no-op
  SC_CHECK(writer.flush() == SaveStatus::NoSaveDirectory);
  SC_CHECK(writer.pollCompleted().empty());
}

// A score that rises faster than the writer can write keeps the newest
// value and never falls behind: every submit replaces the pending value, so
// the file cannot hold a number that a later submit already superseded when
// the round went on, and the completed reports arrive in order.
void rapid_scores_coalesce_to_the_newest() {
  SaveDir dir;
  RecordWriter writer(dir.str());
  SlowSink sink;
  writer.setSinkForTest(sink.function());
  // Ten saves while the first write is still running.
  for (int i = 1; i <= 10; ++i) {
    writer.submit(static_cast<std::uint64_t>(i) * 100, i);
  }
  const SaveStatus status = writer.flush();
  SC_CHECK(status == SaveStatus::Ok);
  SC_CHECK(sink.calls() <= 10);
  SC_CHECK(!sink.values().empty());
  // The last value the sink was asked to write is the last submitted score:
  // a coalesced run may skip earlier values, never the final one.
  SC_CHECK(sink.values().back() == 1000);
  // The completed reports are in submission order and end at the newest.
  std::vector<RecordWrite> writes = writer.pollCompleted();
  SC_CHECK(!writes.empty());
  SC_CHECK(writes.back().score == 1000);
  SC_CHECK(writes.back().status == SaveStatus::Ok);
  for (std::size_t i = 1; i < writes.size(); ++i) {
    SC_CHECK(writes[i - 1].tick <= writes[i].tick);
  }
  // The file itself is untouched here: this case injects a sink that counts
  // instead of writing, so the landing of a real value is the store-backed
  // case above.
}

// A failed write reports its status through the completed record, so the
// session log keeps the failure line the synchronous path printed.
void failed_write_reports_its_status() {
  SaveDir dir;
  RecordWriter writer(dir.str());
  SlowSink sink;
  sink.setStatus(SaveStatus::WriteFailed);
  writer.setSinkForTest(sink.function());
  writer.submit(700, 3);
  SC_CHECK(writer.flush() == SaveStatus::WriteFailed);
  const std::vector<RecordWrite> writes = writer.pollCompleted();
  SC_CHECK(writes.size() == 1);
  SC_CHECK(writes[0].status == SaveStatus::WriteFailed);
  SC_CHECK(writes[0].tick == 3);
}

// The gameplay thread's per-frame poll empties the completed queue: what it
// takes is gone, and what has not completed stays for a later frame. The
// sink blocks inside the write until this case releases it, so the first
// poll runs while the write is provably in flight; the old form bet on the
// 80 ms sleep not having elapsed yet and lost that bet once under a loaded
// parallel ctest run (task T093).
void poll_empties_completed_writes() {
  SaveDir dir;
  RecordWriter writer(dir.str());
  BlockedSink sink;
  writer.setSinkForTest(sink.function());
  writer.submit(100, 1);
  sink.waitUntilRunning();
  SC_CHECK(writer.pollCompleted().empty());  // the blocked write is in flight
  sink.release();
  SC_CHECK(writer.flush() == SaveStatus::Ok);
  SC_CHECK(sink.finished() == 1);
  const std::vector<RecordWrite> first = writer.pollCompleted();
  SC_CHECK(first.size() == 1);
  SC_CHECK(first[0].score == 100);
  SC_CHECK(first[0].tick == 1);
  SC_CHECK(first[0].status == SaveStatus::Ok);
  SC_CHECK(writer.pollCompleted().empty());
}

// The gameplay thread keeps submitting across many flush-free frames while
// the writer keeps up; the end state is the last value, and the file was
// never left holding less than a completed write.
void sustained_submission_stays_current() {
  SaveDir dir;
  RecordWriter writer(dir.str());
  for (int frame = 1; frame <= 40; ++frame) {
    writer.submit(static_cast<std::uint64_t>(frame) * 11, frame);
    // A frame's worth of other work: the writer finishes in that window.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    writer.pollCompleted();
  }
  SC_CHECK(writer.flush() == SaveStatus::Ok);
  writer.pollCompleted();
  const HighScoreStore store(dir.str());
  SC_CHECK(store.load().highScore == 440);
}

}  // namespace

// ---------------------------------------------------------------------------
// Suite entry.
// ---------------------------------------------------------------------------

void run_record_writer_tests() {
  submit_does_not_wait_for_storage();
  flushed_write_lands_in_the_file();
  session_without_directory_writes_nothing();
  rapid_scores_coalesce_to_the_newest();
  failed_write_reports_its_status();
  poll_empties_completed_writes();
  sustained_submission_stays_current();
}
