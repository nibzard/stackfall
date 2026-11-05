// The record file's writer thread (task T065). See
// include/stackfall/record_writer.hpp for the behavior contract and the
// durability argument; this file holds the thread mechanics alone.

#include "stackfall/record_writer.hpp"

#include <chrono>
#include <utility>

namespace stackfall {

RecordWriter::RecordWriter(std::string saveDirectory)
    : directory_(saveDirectory.empty()
                     ? nullptr
                     : std::make_unique<HighScoreStore>(
                           std::move(saveDirectory))) {
  if (directory_ != nullptr) {
    sink_ = [store = directory_.get()](std::uint64_t score) {
      return store->save(score);
    };
  }
}

RecordWriter::~RecordWriter() {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    stopping_ = true;
  }
  submitted_.notify_all();
  if (threadStarted_) {
    thread_.join();
  }
}

void RecordWriter::submit(std::uint64_t score, std::uint64_t tick) {
  if (directory_ == nullptr) {
    return;  // a session without a save directory persists nothing
  }
  startThread();
  {
    std::lock_guard<std::mutex> guard(mutex_);
    pendingScore_ = score;
    pendingTick_ = tick;
    hasPending_ = true;
  }
  submitted_.notify_one();
}

std::vector<RecordWrite> RecordWriter::pollCompleted() {
  std::vector<RecordWrite> writes;
  std::lock_guard<std::mutex> guard(mutex_);
  writes.assign(completed_.begin(), completed_.end());
  completed_.clear();
  return writes;
}

SaveStatus RecordWriter::flush() {
  std::unique_lock<std::mutex> lock(mutex_);
  // Wait until nothing is waiting and nothing is in flight. The writer
  // signals after every completed write, so this wakes at the latest when
  // the write that carries the newest submitted score lands.
  settled_.wait(lock, [this] { return !hasPending_ && !writing_; });
  return lastStatus_;
}

void RecordWriter::setSinkForTest(RecordSaveFunction sink) {
  std::lock_guard<std::mutex> guard(mutex_);
  sink_ = std::move(sink);
}

void RecordWriter::startThread() {
  std::lock_guard<std::mutex> guard(mutex_);
  if (threadStarted_) {
    return;
  }
  thread_ = std::thread(&RecordWriter::writerLoop, this);
  threadStarted_ = true;
}

void RecordWriter::writerLoop() {
  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    submitted_.wait(lock, [this] { return hasPending_ || stopping_; });
    if (!hasPending_) {
      return;  // stopping_ with nothing to write
    }
    const std::uint64_t score = pendingScore_;
    const std::uint64_t tick = pendingTick_;
    hasPending_ = false;
    writing_ = true;
    RecordSaveFunction sink = sink_;
    lock.unlock();
    // The store's atomic write, off the gameplay thread. Everything the
    // specification requires of a record save happens here unchanged:
    // the temporary file, the write, the flush, the fsync, the rename
    // (SPEC section 9), and the crash safety that follows from them.
    const SaveStatus status = sink(score);
    lock.lock();
    writing_ = false;
    lastStatus_ = status;
    completed_.push_back(RecordWrite{score, tick, status});
    settled_.notify_one();
  }
}

}  // namespace stackfall
