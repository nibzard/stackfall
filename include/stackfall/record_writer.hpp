#pragma once

// The record file's writer thread (task T065, the device-feedback round).
//
// Task T052 made the record durable the moment a running round passes it,
// which the specification's own write makes safe to interrupt anywhere:
// a temporary file, a write, a flush, an fsync, and a rename (SPEC section
// 9). Measured on the round's host, that write costs about 2.7 milliseconds
// at the median and several times that in the tail, and the owner's device
// report says the game sometimes stutters after initially smooth play -
// exactly the moment a round's score first passes the stored record, after
// which every further point the round scores pays the same write on the
// gameplay thread (notes/performance.md, T065).
//
// This owner keeps the write and moves it off the gameplay thread. The
// gameplay thread hands the newest score to a writer thread and carries on;
// the thread performs the same atomic write the store always performed, so
// the file's contents, its atomicity, and its recovery behavior are
// unchanged. Because only the newest score matters - the file holds one
// number and every later number contains every earlier one - a value that
// arrives while a write is still running replaces the next one, so the file
// never lags by more than one write and the writer can never fall behind
// the round, however fast the score rises.
//
// Durability (task T052, specification sections 4.7 and 9) is preserved:
//
//   * An abandoned round, a window close, or a quit flushes the writer at
//     the round boundary, so the record is complete on disk before the
//     title screen shows it.
//   * A power loss costs at most the write in flight: the atomic write
//     leaves the previous complete file or the new one, never a partial
//     one, and a new value is handed over within one write of the score
//     that produced it.
//
// The class is SDL-free and lives in stackfall_core beside the store it
// wraps, so the headless suite pins it exactly (task T023's contract).
//
// The gameplay thread never waits here: submit() takes a lock the writer
// holds only while it moves data, never while it writes, and every other
// public call either reads completed results or waits only at a round
// boundary or shutdown.

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "stackfall/storage.hpp"

namespace stackfall {

// One write the writer completed, reported back in submission order.
struct RecordWrite {
  std::uint64_t score = 0;
  std::uint64_t tick = 0;  // the simulation tick the save was requested on
  SaveStatus status = SaveStatus::NoSaveDirectory;
};

// The signature of the store's save, injectable so the headless suite can
// hand the writer a slow or failing sink and time it deterministically.
using RecordSaveFunction = std::function<SaveStatus(std::uint64_t)>;

class RecordWriter {
 public:
  // Owns the record file of `saveDirectory`. An empty directory means no
  // persistence, exactly as in HighScoreStore: submit() then does nothing
  // and no thread ever starts.
  explicit RecordWriter(std::string saveDirectory);

  // Flushes and joins. A writer thread that never started needs no join.
  ~RecordWriter();

  RecordWriter(const RecordWriter&) = delete;
  RecordWriter& operator=(const RecordWriter&) = delete;

  // Whether any record can persist this session.
  bool hasDirectory() const { return directory_ != nullptr; }

  // The store this writer owns, for the log lines that name its file. Null
  // when the session has no save directory.
  const HighScoreStore* store() const { return directory_.get(); }

  // Hands the newest record to the writer. Never blocks on storage: the
  // lock is held to move two integers, and a value already waiting is
  // replaced, because the file holds one number and the newer one wins.
  void submit(std::uint64_t score, std::uint64_t tick);

  // Takes the writes that completed, in submission order. The gameplay
  // thread calls this every frame, so the returned set stays small and the
  // session log and input log keep their per-save lines.
  std::vector<RecordWrite> pollCompleted();

  // Waits until every submitted score is on disk, and returns the status of
  // the newest write (NoSaveDirectory when nothing was ever submitted).
  // Round boundaries and shutdown call this; the gameplay loop never does.
  SaveStatus flush();

  // Replaces the sink the writer calls. Test support only: a production
  // writer always saves through its own store.
  void setSinkForTest(RecordSaveFunction sink);

 private:
  void startThread();
  void writerLoop();

  // The store itself. A session without a save directory keeps it null and
  // the writer inert.
  std::unique_ptr<HighScoreStore> directory_;

  RecordSaveFunction sink_;  // guarded by mutex_ once the thread runs

  mutable std::mutex mutex_;
  std::condition_variable submitted_;
  std::condition_variable settled_;
  std::thread thread_;
  bool threadStarted_ = false;
  bool stopping_ = false;
  bool writing_ = false;
  bool hasPending_ = false;
  std::uint64_t pendingScore_ = 0;
  std::uint64_t pendingTick_ = 0;
  SaveStatus lastStatus_ = SaveStatus::NoSaveDirectory;
  std::deque<RecordWrite> completed_;
};

}  // namespace stackfall
