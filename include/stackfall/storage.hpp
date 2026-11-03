#pragma once

// Atomic high-score persistence (specification sections 9 and 10.3, task T012).
//
// The record is one small versioned UTF-8 ASCII file in the port's writable
// conf/ directory, exactly two lines:
//
//   version=1\nhigh_score=12345\n
//
// The PortMaster launcher creates that directory and hands it to the game
// through the environment: it exports XDG_DATA_HOME="$GAMEDIR/conf", or passes
// the explicit STACKFALL_SAVE_DIR to remove all ambiguity. saveDirectoryFrom
// Environment() resolves the directory in that order, and a machine with no
// hint at all falls back to the XDG default data directory of the user plus
// the application directory, so a desktop build also persists:
//
//   1. STACKFALL_SAVE_DIR, used exactly as given;
//   2. XDG_DATA_HOME when it holds an absolute path, used exactly as given,
//      because the launcher points it at the game's own conf/ directory;
//   3. $HOME/.local/share/stackfall;
//   4. empty, which disables persistence instead of guessing a write location.
//
// A save writes a temporary file in the same directory, flushes it, closes it,
// and renames it over the record, so a crash or power loss can only leave the
// previous complete record or no record at all, never a half-written one.
//
// A load that finds no file, a file it cannot read, or a file that is not an
// exact version 1 record reports high score 0 and a status; no missing,
// invalid, or partial file throws, crashes, or stops the game (specification
// section 9, section 11 item 20).
//
// The whole module is plain std::filesystem and std::FILE code: no SDL, no
// clock, and no game state. It sits in stackfall_core next to the rules so the
// persistence suite of tests/test_storage.cpp runs headless on every machine
// that builds the rules tests (specification section 8).

#include <cstdint>
#include <string>

namespace stackfall {

// The record's name inside the save directory (specification section 10.3).
// The settings file of the sound toggle sits beside it; see kSettingsFileName.
inline constexpr char kSaveFileName[] = "highscore.dat";

// The format version of the first line. A file with any other version is not a
// readable record and recovers to high score 0.
inline constexpr int kSaveFormatVersion = 1;

// A record larger than this is not the two-line format; loading stops instead
// of reading an unbounded file into memory.
inline constexpr std::size_t kMaxSaveFileBytes = 4096;

// The explicit save-directory environment variable of the launcher
// (specification section 9).
inline constexpr char kSaveDirEnv[] = "STACKFALL_SAVE_DIR";

enum class SaveStatus : std::uint8_t {
  Ok = 0,          // the record was read or written
  NoSaveDirectory, // no environment hint and no home directory: nothing to do
  NotFound,        // no record exists yet, as on a fresh installation
  Invalid,         // the record exists but is not an exact version 1 file
  WriteFailed,     // the temporary write, flush, or rename failed
};

// One lowercase word per status, for the startup log of section 9.
const char* saveStatusName(SaveStatus status);

// The outcome of a load: the high score to use and how it was obtained. Every
// status except Ok carries high score 0.
struct LoadResult {
  std::uint64_t highScore = 0;
  SaveStatus status = SaveStatus::NotFound;

  bool ok() const { return status == SaveStatus::Ok; }
};

// Reads `bytes` as the two-line record. Accepts only the exact canonical
// rendering of one decimal value: no reordered or missing lines, no carriage
// returns, no spaces, no signs, no leading zeros, no second record, and no
// value above the 64-bit limit. A partial file, for example one that lost its
// final newline, is rejected. Returns false and leaves *highScore at 0.
bool parseSaveFile(const std::string& bytes, std::uint64_t* highScore);

// Renders the canonical record of `highScore`.
std::string formatSaveFile(std::uint64_t highScore);

// Resolves the save directory from the environment, as described in the header
// comment. Returns an empty string when nothing names a directory.
std::string saveDirectoryFromEnvironment();

// ---------------------------------------------------------------------------
// The session log's location (task T014, specification section 9). The
// platform layer writes every line to stdout and, whenever a location
// resolves, to log.txt as well, so the required entries survive a run whose
// stdout went nowhere:
//
//   1. STACKFALL_LOG_FILE, used exactly as given, so a test run, a desktop
//      launch, and the future launcher can each place the file deliberately;
//   2. log.txt inside the session's save directory, beside the record and the
//      settings file, because the specification's own log.txt in the game
//      directory comes from the launcher's stdout redirection (section 10.3)
//      and must never meet this sink in one file. That directory is the value
//      of the --save-dir option when a harness passed one (task T033), and
//      saveDirectoryFromEnvironment() otherwise;
//   3. empty, which logs to stdout alone instead of guessing a location.
// ---------------------------------------------------------------------------

// The session log's file name (specification section 9: log.txt).
inline constexpr char kLogFileName[] = "log.txt";

// The explicit log-file environment variable (task T014).
inline constexpr char kLogFileEnv[] = "STACKFALL_LOG_FILE";

// Resolves the session log's path in the order above against
// `saveDirectory`, the one directory the record and the settings file also
// live in: STACKFALL_LOG_FILE wins, the log otherwise lands in
// `saveDirectory`, and an empty `saveDirectory` - no directory at all, exactly
// as for the two stores - returns the empty string.
std::string logFilePath(const std::string& saveDirectory);

// Resolves the session log's path for a session that passed no --save-dir, so
// the directory comes from the environment alone. Returns an empty string when
// neither step resolves, and the session logs to stdout alone.
std::string logFilePathFromEnvironment();

// One save location. Every member reports failure instead of throwing, so the
// platform layer can log and carry on (specification section 9).
class HighScoreStore {
 public:
  // A store with no directory: load reports NoSaveDirectory and save does
  // nothing. This is also the state of fromEnvironment() on a machine with no
  // environment hint and no home directory.
  HighScoreStore() = default;

  // A store rooted at `directory`, which need not exist yet; save creates it.
  explicit HighScoreStore(std::string directory);

  // A store rooted at saveDirectoryFromEnvironment().
  static HighScoreStore fromEnvironment();

  // True while this store names a directory.
  bool hasDirectory() const { return !directory_.empty(); }

  // The directory this store writes to, or "" when persistence is off.
  const std::string& directory() const { return directory_; }

  // The record's full path, or "" when persistence is off.
  std::string filePath() const;

  // Reads the record. A missing, unreadable, or invalid file reports high
  // score 0 with its status; nothing here can throw or crash.
  LoadResult load() const;

  // Writes `highScore` through the temporary file, flush, close, and rename of
  // the header comment. The store holds no cached value, so the caller owns
  // the record rule of section 4.7: the platform layer saves when a round sets
  // a new record, and writes the value it was given.
  SaveStatus save(std::uint64_t highScore) const;

 private:
  std::string directory_;  // empty: persistence is off
};

// ---------------------------------------------------------------------------
// The settings file (task T013, specification section 6.3). The mute state of
// the sound toggle persists beside the high score, in the same save directory
// and through the same atomic write, as one more versioned ASCII file:
//
//   version=1\nsound=on\n
//
// `sound=off` starts the session muted; every other outcome, including a
// missing, unreadable, or damaged file, starts it with sound on, so a broken
// settings file can only cost the preference and never the game. The format
// rules follow the high-score record: the exact canonical rendering of the
// value, nothing more and nothing less.
// ---------------------------------------------------------------------------

// The settings file's name inside the save directory.
inline constexpr char kSettingsFileName[] = "settings.dat";

// Reads `bytes` as the two-line settings file. Accepts only the exact
// canonical rendering of `sound=on` or `sound=off`; returns false and leaves
// *soundOn untouched on anything else.
bool parseSettingsFile(const std::string& bytes, bool* soundOn);

// Renders the canonical settings file of `soundOn`.
std::string formatSettingsFile(bool soundOn);

// The outcome of a settings load: the sound state to use and how it was
// obtained. Every status except Ok leaves soundOn at its default of true.
struct SettingsResult {
  bool soundOn = true;
  SaveStatus status = SaveStatus::NotFound;

  bool ok() const { return status == SaveStatus::Ok; }
};

// One settings location, with the same shape and the same failure rules as
// HighScoreStore so the platform layer treats the two files alike.
class SettingsStore {
 public:
  SettingsStore() = default;
  explicit SettingsStore(std::string directory);
  static SettingsStore fromEnvironment();

  bool hasDirectory() const { return !directory_.empty(); }
  const std::string& directory() const { return directory_; }
  std::string filePath() const;

  // Reads the sound state. Sound stays on unless a valid file turns it off.
  SettingsResult load() const;

  // Writes the sound state through the same atomic sequence as the record.
  SaveStatus save(bool soundOn) const;

 private:
  std::string directory_;  // empty: persistence is off
};

}  // namespace stackfall
