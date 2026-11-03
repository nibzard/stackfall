// Headless suite for atomic high-score persistence (task T012, specification
// sections 9 and 10.3, section 11 item 20).
//
// The suite proves the two halves of the acceptance: a good record round-trips
// through a store that reads it back, as a relaunch does, and a missing,
// invalid, partial, oversized, unreadable, or unwritable save never crashes the
// game and never lowers a good record. The atomic write is checked through its
// observable behavior: a successful save leaves exactly one entry in the save
// directory (the published record), a failed save leaves the previous record
// untouched with no temporary file behind, and a read-only record is still
// replaced because the rename replaces a directory entry, not the file's bytes.
//
// Every case writes below std::filesystem::temp_directory_path(), so the suite
// touches no repository file and needs no display, no SDL, and no controller.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "stackfall/storage.hpp"

#include "test_util.hpp"

namespace {

using stackfall::HighScoreStore;
using stackfall::LoadResult;
using stackfall::SaveStatus;
using stackfall::formatSaveFile;
using stackfall::parseSaveFile;
using stackfall::saveDirectoryFromEnvironment;
using stackfall::kSaveFileName;

// ---------------------------------------------------------------------------
// Filesystem and environment helpers.
// ---------------------------------------------------------------------------

// Sets one environment variable for the lifetime of the guard and restores the
// previous value afterwards, because the resolution rules read the live
// environment of the process that the whole suite shares. An empty string
// clears the variable, because an empty value means "unset" to the resolution
// rules.
class EnvGuard {
 public:
  EnvGuard(const std::string& name, const std::string& value) : name_(name) {
    const char* previous = std::getenv(name.c_str());
    hadPrevious_ = previous != nullptr;
    previous_ = hadPrevious_ ? previous : "";
    apply(value.empty() ? nullptr : value.c_str());
  }

  ~EnvGuard() { apply(hadPrevious_ ? previous_.c_str() : nullptr); }

 private:
  void apply(const char* value) {
#if defined(_WIN32)
    if (value == nullptr) {
      _putenv_s(name_.c_str(), "");
    } else {
      _putenv_s(name_.c_str(), value);
    }
#else
    if (value == nullptr) {
      ::unsetenv(name_.c_str());
    } else {
      ::setenv(name_.c_str(), value, 1);
    }
#endif
  }

  std::string name_;
  std::string previous_;
  bool hadPrevious_ = false;
};

// A private directory below the system temporary directory, removed with
// everything written into it when the test scope ends.
class TempDir {
 public:
  TempDir() {
    std::error_code ec;
    std::filesystem::path base = std::filesystem::temp_directory_path(ec);
    if (ec) {
      base = std::filesystem::path("/tmp");
    }
    const int pid =
#if defined(_WIN32)
        _getpid();
#else
        static_cast<int>(::getpid());
#endif
    char name[64];
    std::snprintf(name, sizeof(name), "stackfall-storage-%d-%d", pid,
                  ++counter);
    path_ = (base / name).string();
    std::filesystem::remove_all(path_, ec);
    std::filesystem::create_directories(path_, ec);
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  const std::string& path() const { return path_; }

 private:
  static int counter;
  std::string path_;
};

int TempDir::counter = 0;

// True when the suite runs with privileges that ignore permission bits, in
// which case the read-only record case cannot prove anything.
bool runningPrivileged() {
#if defined(_WIN32)
  return false;
#else
  return ::geteuid() == 0;
#endif
}

void writeRawFile(const std::string& path, const std::string& bytes) {
  std::FILE* stream = std::fopen(path.c_str(), "wb");
  SC_CHECK(stream != nullptr);
  if (stream == nullptr) {
    return;
  }
  SC_CHECK(std::fwrite(bytes.data(), 1, bytes.size(), stream) == bytes.size());
  SC_CHECK(std::fclose(stream) == 0);
}

std::string readRawFile(const std::string& path) {
  std::FILE* stream = std::fopen(path.c_str(), "rb");
  SC_CHECK(stream != nullptr);
  if (stream == nullptr) {
    return std::string();
  }
  std::string bytes;
  bytes.resize(stackfall::kMaxSaveFileBytes + 1);
  const std::size_t read = std::fread(&bytes[0], 1, bytes.size(), stream);
  std::fclose(stream);
  bytes.resize(read);
  return bytes;
}

// The sorted entry names of `directory`, so a test can prove that a save left
// no temporary file behind.
std::vector<std::string> entryNames(const std::string& directory) {
  std::vector<std::string> names;
  try {
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory)) {
      names.push_back(entry.path().filename().string());
    }
  } catch (...) {
    // A listing that fails reports fewer names; the checks below still compare
    // what was listed.
  }
  std::sort(names.begin(), names.end());
  return names;
}

// ---------------------------------------------------------------------------
// The record format.
// ---------------------------------------------------------------------------

void testFormatAndParse() {
  // The canonical rendering of specification section 9.
  SC_CHECK(formatSaveFile(12345) == "version=1\nhigh_score=12345\n");
  SC_CHECK(formatSaveFile(0) == "version=1\nhigh_score=0\n");
  SC_CHECK(formatSaveFile(18446744073709551615ULL) ==
           "version=1\nhigh_score=18446744073709551615\n");

  const std::string valid[] = {
      "version=1\nhigh_score=0\n",
      "version=1\nhigh_score=1\n",
      "version=1\nhigh_score=12345\n",
      "version=1\nhigh_score=18446744073709551615\n",  // the 64-bit limit
  };
  const std::uint64_t expected[] = {0, 1, 12345, 18446744073709551615ULL};
  for (std::size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
    std::uint64_t value = 12345;  // a sentinel the parser must overwrite
    SC_CHECK(parseSaveFile(valid[i], &value));
    SC_CHECK(value == expected[i]);
    SC_CHECK(formatSaveFile(value) == valid[i]);  // render and parse agree
  }

  // Every damaged, edited, foreign, or partial form of the record is rejected
  // and reports high score 0.
  const std::string invalid[] = {
      "",                                   // empty file
      "\n",
      "version=1\n",                        // the score line is gone
      "version=1\nhigh_score=",             // the value is gone
      "version=1\nhigh_score=\n",           // an empty value
      "version=1\nhigh_score=12345",        // the final newline is gone
      "version=1\r\nhigh_score=12345\r\n",  // a Windows text editor
      "high_score=12345\nversion=1\n",      // the lines were swapped
      "version=2\nhigh_score=1\n",          // a future format version
      "version=01\nhigh_score=1\n",         // a padded version
      "version=1.0\nhigh_score=1\n",
      "version=1\nhigh_score=-5\n",
      "version=1\nhigh_score=+5\n",
      "version=1\nhigh_score=abc\n",
      "version=1\nhigh_score=12x45\n",
      "version=1\nhigh_score=012345\n",  // a padded score
      "version=1\nhigh_score=1 2\n",
      "version=1\nhigh_score=12345\nextra=1\n",          // an extra line
      "version=1\nhigh_score=12345\n\n",                 // a blank line
      "version=1\nhigh_score=12345 \n",                  // a trailing space
      "version=1\nhigh_score=12345\nhigh_score=6\n",     // a second record
      "version=1\nhigh_score=999999999999999999999999\n",  // far over 64 bits
      "version=1\nhigh_score=18446744073709551616\n",      // one past the limit
      " version=1\nhigh_score=1\n",
      "version=1\nhigh_score =1\n",
      "VERSION=1\nHIGH_SCORE=1\n",
      "version=1;high_score=1\n",
      std::string("version=1\nhigh_score=1\0\n", 24),  // a NUL inside the value
      std::string("\0\1\2garbage", 10),               // binary garbage
  };
  for (const std::string& bytes : invalid) {
    std::uint64_t value = 99;  // a sentinel the parser must clear
    SC_CHECK(!parseSaveFile(bytes, &value));
    SC_CHECK(value == 0);
  }

  // A record above the size cap is rejected without reading further.
  std::string oversized = "version=1\nhigh_score=1\n";
  oversized.append(stackfall::kMaxSaveFileBytes, 'x');
  std::uint64_t value = 7;
  SC_CHECK(!parseSaveFile(oversized, &value));
  SC_CHECK(value == 0);

  // A null out-pointer is allowed; the caller may only want the verdict.
  SC_CHECK(parseSaveFile("version=1\nhigh_score=5\n", nullptr));
  SC_CHECK(!parseSaveFile("junk", nullptr));
}

// ---------------------------------------------------------------------------
// Directory resolution from the environment.
// ---------------------------------------------------------------------------

void testEnvironmentResolution() {
  // The launcher's explicit directory wins over every desktop convention.
  {
    const EnvGuard explicitDir(stackfall::kSaveDirEnv, "/tmp/stackfall-save");
    const EnvGuard xdg("XDG_DATA_HOME", "/tmp/xdg-data");
    const EnvGuard home("HOME", "/home/tester");
    SC_CHECK(saveDirectoryFromEnvironment() == "/tmp/stackfall-save");
  }
  // A trailing separator is normalized, not doubled.
  {
    const EnvGuard explicitDir(stackfall::kSaveDirEnv, "/tmp/stackfall-save/");
    const EnvGuard xdg("XDG_DATA_HOME", "/tmp/xdg-data");
    const HighScoreStore store = HighScoreStore::fromEnvironment();
    SC_CHECK(store.directory() == "/tmp/stackfall-save/");
    SC_CHECK(store.filePath() == "/tmp/stackfall-save/highscore.dat");
  }
  // XDG_DATA_HOME carries the record when no explicit directory exists.
  {
    const EnvGuard explicitDir(stackfall::kSaveDirEnv, "");
    const EnvGuard xdg("XDG_DATA_HOME", "/tmp/xdg-data");
    const EnvGuard home("HOME", "/home/tester");
    SC_CHECK(saveDirectoryFromEnvironment() == "/tmp/xdg-data");
    const HighScoreStore store = HighScoreStore::fromEnvironment();
    SC_CHECK(store.filePath() == "/tmp/xdg-data/highscore.dat");
  }
  // A relative XDG_DATA_HOME is not a data home, so the home fallback applies.
  {
    const EnvGuard explicitDir(stackfall::kSaveDirEnv, "");
    const EnvGuard xdg("XDG_DATA_HOME", "relative/path");
    const EnvGuard home("HOME", "/home/tester");
    SC_CHECK(saveDirectoryFromEnvironment() ==
             "/home/tester/.local/share/stackfall");
  }
  // With no hint at all, the default data directory of the user plus the
  // application directory is used.
  {
    const EnvGuard explicitDir(stackfall::kSaveDirEnv, "");
    const EnvGuard xdg("XDG_DATA_HOME", "");
    const EnvGuard home("HOME", "/home/tester");
    SC_CHECK(saveDirectoryFromEnvironment() ==
             "/home/tester/.local/share/stackfall");
  }
  // Without a home directory, persistence turns off instead of guessing.
  {
    const EnvGuard explicitDir(stackfall::kSaveDirEnv, "");
    const EnvGuard xdg("XDG_DATA_HOME", "");
    const EnvGuard home("HOME", "");
    SC_CHECK(saveDirectoryFromEnvironment().empty());
    const HighScoreStore store = HighScoreStore::fromEnvironment();
    SC_CHECK(!store.hasDirectory());
    SC_CHECK(store.filePath().empty());
    SC_CHECK(store.load().status == SaveStatus::NoSaveDirectory);
    SC_CHECK(store.load().highScore == 0);
    SC_CHECK(store.save(10) == SaveStatus::NoSaveDirectory);
  }
  // A default-constructed store behaves the same way.
  {
    const HighScoreStore store;
    SC_CHECK(!store.hasDirectory());
    SC_CHECK(store.load().status == SaveStatus::NoSaveDirectory);
    SC_CHECK(store.save(10) == SaveStatus::NoSaveDirectory);
  }
}

// ---------------------------------------------------------------------------
// Load and save behavior.
// ---------------------------------------------------------------------------

// The session log's path resolves beside the record when nothing names it
// (task T014, specification section 9), and follows the --save-dir directory
// a session passed (task T033).
void testLogPathResolution() {
  // The explicit log file wins over every convention and is used exactly as
  // given, so a test run or the launcher can place log.txt deliberately.
  {
    const EnvGuard logFile(stackfall::kLogFileEnv, "/tmp/stackfall-run.log");
    const EnvGuard saveDir(stackfall::kSaveDirEnv, "/tmp/stackfall-save");
    const EnvGuard home("HOME", "/home/tester");
    SC_CHECK(stackfall::logFilePathFromEnvironment() ==
             "/tmp/stackfall-run.log");
  }
  // A relative explicit path is honored as given, exactly like the save dir.
  {
    const EnvGuard logFile(stackfall::kLogFileEnv, "run/log.txt");
    SC_CHECK(stackfall::logFilePathFromEnvironment() == "run/log.txt");
  }
  // Without an explicit file, the log lands beside the record.
  {
    const EnvGuard logFile(stackfall::kLogFileEnv, "");
    const EnvGuard saveDir(stackfall::kSaveDirEnv, "/tmp/stackfall-save");
    const EnvGuard home("HOME", "/home/tester");
    SC_CHECK(stackfall::logFilePathFromEnvironment() ==
             "/tmp/stackfall-save/log.txt");
  }
  // A trailing separator in the save directory is normalized, not doubled.
  {
    const EnvGuard logFile(stackfall::kLogFileEnv, "");
    const EnvGuard saveDir(stackfall::kSaveDirEnv, "/tmp/stackfall-save/");
    SC_CHECK(stackfall::logFilePathFromEnvironment() ==
             "/tmp/stackfall-save/log.txt");
  }
  // XDG_DATA_HOME carries the log when it carries the record.
  {
    const EnvGuard logFile(stackfall::kLogFileEnv, "");
    const EnvGuard saveDir(stackfall::kSaveDirEnv, "");
    const EnvGuard xdg("XDG_DATA_HOME", "/tmp/xdg-data");
    const EnvGuard home("HOME", "/home/tester");
    SC_CHECK(stackfall::logFilePathFromEnvironment() ==
             "/tmp/xdg-data/log.txt");
  }
  // An explicit log file survives a machine that has no save directory, so a
  // session can still keep its log.
  {
    const EnvGuard logFile(stackfall::kLogFileEnv, "/tmp/stackfall-run.log");
    const EnvGuard saveDir(stackfall::kSaveDirEnv, "");
    const EnvGuard xdg("XDG_DATA_HOME", "");
    const EnvGuard home("HOME", "");
    SC_CHECK(stackfall::logFilePathFromEnvironment() ==
             "/tmp/stackfall-run.log");
  }
  // With no location at all, the log stays on stdout.
  {
    const EnvGuard logFile(stackfall::kLogFileEnv, "");
    const EnvGuard saveDir(stackfall::kSaveDirEnv, "");
    const EnvGuard xdg("XDG_DATA_HOME", "");
    const EnvGuard home("HOME", "");
    SC_CHECK(stackfall::logFilePathFromEnvironment().empty());
  }
  // The --save-dir directory of the harness carries the log beside the record,
  // ahead of every environment hint, so a scripted session writes no log into
  // the player's directory (task T033).
  {
    const EnvGuard logFile(stackfall::kLogFileEnv, "");
    const EnvGuard saveDir(stackfall::kSaveDirEnv, "/tmp/stackfall-save");
    const EnvGuard xdg("XDG_DATA_HOME", "/tmp/xdg-data");
    const EnvGuard home("HOME", "/home/tester");
    SC_CHECK(stackfall::logFilePath("/tmp/harness-save") ==
             "/tmp/harness-save/log.txt");
  }
  // A trailing separator in that directory is normalized, not doubled.
  {
    const EnvGuard logFile(stackfall::kLogFileEnv, "");
    SC_CHECK(stackfall::logFilePath("/tmp/harness-save/") ==
             "/tmp/harness-save/log.txt");
  }
  // The explicit log file still wins over the explicit directory, so a session
  // can place log.txt deliberately while its record stays in --save-dir.
  {
    const EnvGuard logFile(stackfall::kLogFileEnv, "/tmp/stackfall-run.log");
    SC_CHECK(stackfall::logFilePath("/tmp/harness-save") ==
             "/tmp/stackfall-run.log");
  }
  // An empty explicit directory is "off", exactly as it is for the two stores,
  // so the session logs to stdout alone instead of falling back to the
  // environment.
  {
    const EnvGuard logFile(stackfall::kLogFileEnv, "");
    const EnvGuard saveDir(stackfall::kSaveDirEnv, "/tmp/stackfall-save");
    const EnvGuard home("HOME", "/home/tester");
    SC_CHECK(stackfall::logFilePath("").empty());
  }
}

void testMissingFileLoadsZero() {
  TempDir dir;
  const HighScoreStore store(dir.path());
  SC_CHECK(store.filePath() == dir.path() + "/" + kSaveFileName);

  const LoadResult result = store.load();
  SC_CHECK(result.status == SaveStatus::NotFound);
  SC_CHECK(result.highScore == 0);
  SC_CHECK(!result.ok());
  // Nothing was created by looking.
  SC_CHECK(entryNames(dir.path()).empty());
}

void testRoundTrip() {
  TempDir dir;
  const HighScoreStore store(dir.path());

  SC_CHECK(store.save(12345) == SaveStatus::Ok);

  // The published record is exactly the two-line format.
  SC_CHECK(readRawFile(store.filePath()) == "version=1\nhigh_score=12345\n");
  // A successful save leaves the temporary file behind nowhere.
  const std::vector<std::string> names = entryNames(dir.path());
  SC_CHECK(names.size() == 1 && names[0] == kSaveFileName);

  // A store that reads the record back, as a relaunched process builds it.
  const HighScoreStore relaunched(dir.path());
  const LoadResult result = relaunched.load();
  SC_CHECK(result.ok());
  SC_CHECK(result.status == SaveStatus::Ok);
  SC_CHECK(result.highScore == 12345);

  // The same through the environment the launcher exports.
  {
    const EnvGuard explicitDir(stackfall::kSaveDirEnv, dir.path());
    const HighScoreStore fromEnv = HighScoreStore::fromEnvironment();
    const LoadResult envResult = fromEnv.load();
    SC_CHECK(envResult.ok());
    SC_CHECK(envResult.highScore == 12345);
  }

  // A new record replaces the old one completely.
  SC_CHECK(store.save(99999) == SaveStatus::Ok);
  SC_CHECK(relaunched.load().highScore == 99999);
  SC_CHECK(readRawFile(store.filePath()) == "version=1\nhigh_score=99999\n");
  SC_CHECK(entryNames(dir.path()).size() == 1);

  // Boundary values round-trip too.
  SC_CHECK(store.save(0) == SaveStatus::Ok);
  SC_CHECK(relaunched.load().highScore == 0);
  SC_CHECK(relaunched.load().ok());
  SC_CHECK(readRawFile(store.filePath()) == "version=1\nhigh_score=0\n");
  SC_CHECK(store.save(18446744073709551615ULL) == SaveStatus::Ok);
  const LoadResult limit = relaunched.load();
  SC_CHECK(limit.ok());
  SC_CHECK(limit.highScore == 18446744073709551615ULL);
}

void testCorruptAndPartialFiles() {
  // Every damaged record on disk loads as high score 0 without a crash and
  // reports itself as invalid.
  const std::string damaged[] = {
      "",
      "version=1\n",
      "version=1\nhigh_score=12345",
      "version=2\nhigh_score=1\n",
      "version=1\nhigh_score=banana\n",
      "version=1\nhigh_score=12345\nhigh_score=999\n",
      "version=1\r\nhigh_score=1\r\n",
      std::string("version=1\nhigh_score=1\0\n", 24),
      std::string(64, '\x7f'),
      std::string(2048, 'A'),
  };
  for (const std::string& bytes : damaged) {
    TempDir dir;
    const HighScoreStore store(dir.path());
    writeRawFile(store.filePath(), bytes);
    const LoadResult result = store.load();
    SC_CHECK(result.status == SaveStatus::Invalid);
    SC_CHECK(result.highScore == 0);
    SC_CHECK(!result.ok());
  }

  // A record above the size cap is invalid, not a reason to read a large file.
  {
    TempDir dir;
    const HighScoreStore store(dir.path());
    writeRawFile(store.filePath(),
                 std::string(stackfall::kMaxSaveFileBytes + 1, 'x'));
    const LoadResult result = store.load();
    SC_CHECK(result.status == SaveStatus::Invalid);
    SC_CHECK(result.highScore == 0);
  }

  // A directory that carries the record's name is not a record.
  {
    TempDir dir;
    const HighScoreStore store(dir.path());
    std::error_code ec;
    std::filesystem::create_directories(store.filePath(), ec);
    SC_CHECK(!ec);
    const LoadResult result = store.load();
    SC_CHECK(result.status == SaveStatus::Invalid);
    SC_CHECK(result.highScore == 0);
  }

  // A leftover temporary file is never mistaken for the record.
  {
    TempDir dir;
    const HighScoreStore store(dir.path());
    SC_CHECK(store.save(321) == SaveStatus::Ok);
    writeRawFile(dir.path() + "/.highscore.dat.tmp.999999", "version=9\n");
    const LoadResult result = store.load();
    SC_CHECK(result.ok());
    SC_CHECK(result.highScore == 321);
  }

  // An invalid record is repaired by the next save: the caller writes the new
  // best score over the damaged file (specification section 9).
  {
    TempDir dir;
    const HighScoreStore store(dir.path());
    writeRawFile(store.filePath(), "version=1\nhigh_score=7");
    SC_CHECK(store.load().status == SaveStatus::Invalid);
    SC_CHECK(store.save(8) == SaveStatus::Ok);
    const LoadResult repaired = HighScoreStore(dir.path()).load();
    SC_CHECK(repaired.ok());
    SC_CHECK(repaired.highScore == 8);
  }
}

void testDirectoryCreation() {
  TempDir dir;
  // The launcher normally creates conf/; a missing directory is created on
  // the first save rather than losing the record.
  const std::string nested = dir.path() + "/conf/nested";
  const HighScoreStore store(nested);
  SC_CHECK(store.save(500) == SaveStatus::Ok);
  const LoadResult result = HighScoreStore(nested).load();
  SC_CHECK(result.ok());
  SC_CHECK(result.highScore == 500);
  SC_CHECK(std::filesystem::is_regular_file(store.filePath()));
}

void testFailedWrites() {
  // A regular file in the place of a parent directory blocks the write
  // regardless of privileges, so the save fails without a crash.
  {
    TempDir dir;
    const std::string blocker = dir.path() + "/blocker";
    writeRawFile(blocker, "not a directory");
    const HighScoreStore store(blocker + "/conf");
    SC_CHECK(store.save(5) == SaveStatus::WriteFailed);
    SC_CHECK(std::filesystem::is_regular_file(blocker));  // nothing was removed
    const std::vector<std::string> names = entryNames(dir.path());
    SC_CHECK(names.size() == 1 && names[0] == "blocker");
  }

  // A failed rename leaves the previous record as the record and leaves no
  // temporary file behind. A directory at the record's path makes the rename
  // fail on every platform and for every user.
  {
    TempDir dir;
    const HighScoreStore store(dir.path());
    std::error_code ec;
    std::filesystem::create_directories(store.filePath(), ec);
    SC_CHECK(store.save(7) == SaveStatus::WriteFailed);
    const std::vector<std::string> names = entryNames(dir.path());
    SC_CHECK(names.size() == 1);
    SC_CHECK(names[0] == kSaveFileName);
    SC_CHECK(std::filesystem::is_directory(store.filePath()));
    // The record stays unusable, and the game still runs.
    const LoadResult result = store.load();
    SC_CHECK(result.status == SaveStatus::Invalid);
    SC_CHECK(result.highScore == 0);
  }

  // A failed save never damages a good record: write a good one, then fail the
  // next save, and the good record still loads.
  {
    TempDir dir;
    const HighScoreStore store(dir.path());
    SC_CHECK(store.save(600) == SaveStatus::Ok);
    std::error_code ec;
    std::filesystem::permissions(store.filePath(),
                                 std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::remove, ec);
    SC_CHECK(!ec);
    if (runningPrivileged()) {
      std::printf("storage: skipping the read-only record case (root)\n");
    } else {
      // The rename replaces the directory entry, so a read-only record is
      // still published; a direct write to the record could not do that.
      SC_CHECK(store.save(700) == SaveStatus::Ok);
      const LoadResult result = store.load();
      SC_CHECK(result.ok());
      SC_CHECK(result.highScore == 700);
    }
    std::filesystem::permissions(store.filePath(),
                                 std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add, ec);
    SC_CHECK(!ec);
    SC_CHECK(entryNames(dir.path()).size() == 1);
  }
}

// ---------------------------------------------------------------------------
// The settings file of the sound toggle (task T013, specification section
// 6.3). A damaged settings file may cost the preference, never the game: every
// outcome except a valid `sound=off` leaves the sound on.
// ---------------------------------------------------------------------------

void testSettingsFormatAndParse() {
  using stackfall::formatSettingsFile;
  using stackfall::parseSettingsFile;

  SC_CHECK(formatSettingsFile(true) == "version=1\nsound=on\n");
  SC_CHECK(formatSettingsFile(false) == "version=1\nsound=off\n");

  bool soundOn = false;
  SC_CHECK(parseSettingsFile("version=1\nsound=on\n", &soundOn));
  SC_CHECK(soundOn);
  SC_CHECK(parseSettingsFile("version=1\nsound=off\n", &soundOn));
  SC_CHECK(!soundOn);
  // A parse that fails leaves the caller's value alone.
  soundOn = true;
  SC_CHECK(!parseSettingsFile("version=1\nsound=maybe\n", &soundOn));
  SC_CHECK(soundOn);

  // Everything that is not one of the two canonical renderings is a damaged
  // preference: a lost final newline, a padded or edited value, a wrong key, a
  // future version, a second record, a carriage return, a NUL byte, or an
  // empty file.
  const char* damaged[] = {
      "",
      "version=1\nsound=on",
      "version=1\nsound=on\n\n",
      "version=1\nsound=off\n\n",
      "version=1\nsound=On\n",
      "version=1\nsound=ON\n",
      "version=1\nsound=1\n",
      "version=1\nsound=true\n",
      "version=1\nsound= off\n",
      "version=1\nsound=off \n",
      "version=1\nsound=\n",
      "version=1\nhigh_score=5\n",
      "version=2\nsound=off\n",
      "sound=off\n",
      "version=1\n",
      "version=1\nsound=off\nsound=on\n",
      "version=1\r\nsound=off\r\n",
      "version=1\nsound=of",
  };
  for (const char* bytes : damaged) {
    SC_CHECK(!parseSettingsFile(bytes, &soundOn));
  }
  SC_CHECK(!parseSettingsFile(std::string("version=1\nsound=on\n\0", 20),
                              &soundOn));
  // The size cap of the record applies here too.
  SC_CHECK(!parseSettingsFile(std::string(stackfall::kMaxSaveFileBytes + 1, 'a'),
                              &soundOn));
}

void testSettingsStores() {
  using stackfall::SettingsResult;
  using stackfall::SettingsStore;

  // A default-constructed store has nothing to load, and the sound stays on.
  {
    const SettingsStore store;
    SC_CHECK(!store.hasDirectory());
    SC_CHECK(store.filePath().empty());
    const SettingsResult result = store.load();
    SC_CHECK(result.status == SaveStatus::NoSaveDirectory);
    SC_CHECK(result.soundOn);
    SC_CHECK(store.save(false) == SaveStatus::NoSaveDirectory);
  }

  // A missing file is a fresh installation: sound on, status not found.
  {
    TempDir dir;
    const SettingsStore store(dir.path());
    SC_CHECK(store.filePath() == dir.path() + "/" +
                                     std::string(stackfall::kSettingsFileName));
    const SettingsResult result = store.load();
    SC_CHECK(result.status == SaveStatus::NotFound);
    SC_CHECK(result.soundOn);
  }

  // Both states round-trip through a second store, as a relaunch does.
  {
    TempDir dir;
    const SettingsStore store(dir.path());
    SC_CHECK(store.save(false) == SaveStatus::Ok);
    SettingsResult result = SettingsStore(dir.path()).load();
    SC_CHECK(result.ok());
    SC_CHECK(!result.soundOn);
    SC_CHECK(store.save(true) == SaveStatus::Ok);
    result = SettingsStore(dir.path()).load();
    SC_CHECK(result.ok());
    SC_CHECK(result.soundOn);
    SC_CHECK(entryNames(dir.path()).size() == 1);
  }

  // The record and the settings live side by side in one save directory.
  {
    TempDir dir;
    const HighScoreStore record(dir.path());
    const SettingsStore settings(dir.path());
    SC_CHECK(record.save(1234) == SaveStatus::Ok);
    SC_CHECK(settings.save(false) == SaveStatus::Ok);
    const std::vector<std::string> names = entryNames(dir.path());
    SC_CHECK(names.size() == 2);
    SC_CHECK(std::find(names.begin(), names.end(), kSaveFileName) != names.end());
    SC_CHECK(std::find(names.begin(), names.end(),
                       stackfall::kSettingsFileName) != names.end());
    SC_CHECK(record.load().ok());
    SC_CHECK(!SettingsStore(dir.path()).load().soundOn);
  }

  // fromEnvironment() resolves the same directory the record store uses, so
  // one STACKFALL_SAVE_DIR carries both files.
  {
    TempDir dir;
    const EnvGuard saveDir(stackfall::kSaveDirEnv, dir.path());
    const SettingsStore store = SettingsStore::fromEnvironment();
    SC_CHECK(store.hasDirectory());
    SC_CHECK(store.save(false) == SaveStatus::Ok);
    const SettingsResult result = SettingsStore::fromEnvironment().load();
    SC_CHECK(result.ok());
    SC_CHECK(!result.soundOn);
  }

  // Damage of every kind keeps the sound on and never throws: text garbage, a
  // foreign version, a partial file, a NUL byte, a binary blob, an oversized
  // file, and a directory at the file's path.
  {
    TempDir dir;
    const SettingsStore store(dir.path());
    const std::string damaged[] = {
        "",
        "hello",
        "version=2\nsound=off\n",
        "version=1\nsound=of",
        std::string("version=1\nsound=off\n\0extra", 23),
        std::string(64, '\x7f'),
        std::string(stackfall::kMaxSaveFileBytes + 1, 'x'),
    };
    for (const std::string& bytes : damaged) {
      writeRawFile(store.filePath(), bytes);
      const SettingsResult result = store.load();
      SC_CHECK(result.status == SaveStatus::Invalid);
      SC_CHECK(result.soundOn);
    }
    std::error_code ec;
    std::filesystem::create_directories(store.filePath(), ec);
    const SettingsResult result = store.load();
    SC_CHECK(result.status == SaveStatus::Invalid);
    SC_CHECK(result.soundOn);
    // The next save repairs the file.
    SC_CHECK(store.save(true) == SaveStatus::Ok);
    SC_CHECK(SettingsStore(dir.path()).load().ok());
  }
}

}  // namespace

void run_storage_tests() {
  std::printf("test_storage: atomic high-score persistence\n");
  testFormatAndParse();
  testEnvironmentResolution();
  testLogPathResolution();
  testMissingFileLoadsZero();
  testRoundTrip();
  testCorruptAndPartialFiles();
  testDirectoryCreation();
  testFailedWrites();
  std::printf("test_storage: settings file of the sound toggle\n");
  testSettingsFormatAndParse();
  testSettingsStores();
}
