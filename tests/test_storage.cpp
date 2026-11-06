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
// The settings file of the player preferences (tasks T013, T039, T044, and
// T062, specification section 6.3). A damaged settings file may cost the
// preferences, never the game: every outcome except a valid value leaves each
// preference at its default (sound, ghost, grid, and hold on, five previews,
// grayscale off, landscape, the Auto music pool, music volume 6, effects
// volume 8).
// ---------------------------------------------------------------------------

// True when `values` holds exactly the expected preference states. The
// orientation defaults to landscape and the music settings to the MUSIC.md
// defaults, the recovery values, so the callers that never set them keep
// reading as before (tasks T055 and T062); the background pair defaults to
// the Auto wallpaper over design 1 of task T084 and the piece designs to off
// (task T095) the same way.
bool valuesAre(const stackfall::SettingsValues& values, bool soundOn,
               bool ghostOn, bool gridOn, bool holdOn, int nextPreviews,
               bool grayscaleOn,
               stackfall::Orientation orientation =
                   stackfall::Orientation::Landscape,
               stackfall::MusicMode musicMode = stackfall::MusicMode::Auto,
               int musicVolume = stackfall::kSettingsMusicVolumeDefault,
               int effectsVolume = stackfall::kSettingsEffectsVolumeDefault,
               stackfall::backgrounds::Mode backgroundMode =
                   stackfall::backgrounds::Mode::Auto,
               int backgroundId = stackfall::kSettingsBackgroundFirst,
               bool pieceDesignsOn = false) {
  return values.soundOn == soundOn && values.ghostOn == ghostOn &&
         values.gridOn == gridOn && values.holdOn == holdOn &&
         values.nextPreviews == nextPreviews &&
         values.grayscaleOn == grayscaleOn &&
         values.orientation == orientation &&
         values.musicMode == musicMode && values.musicVolume == musicVolume &&
         values.effectsVolume == effectsVolume &&
         values.backgroundMode == backgroundMode &&
         values.backgroundId == backgroundId &&
         values.pieceDesignsOn == pieceDesignsOn;
}

// ---------------------------------------------------------------------------
// The one shared bounded read of the two loads (task T046,
// notes/refactor-review.md item 1). Whatever stops the read, both files
// report the same status and recover to their own values: high score 0 for
// the record, every preference at its default for the settings. Each parser
// stays in its store, so a valid record and a valid settings file - legacy
// settings included - still load through the shared path.
// ---------------------------------------------------------------------------

// Both stores rooted at `directory` report `expected` and keep their own
// recovery values.
void checkSharedLoadStatus(const std::string& directory,
                           SaveStatus expected) {
  const LoadResult record = HighScoreStore(directory).load();
  SC_CHECK(record.status == expected);
  SC_CHECK(record.highScore == 0);
  SC_CHECK(!record.ok());
  const stackfall::SettingsResult settings =
      stackfall::SettingsStore(directory).load();
  SC_CHECK(settings.status == expected);
  SC_CHECK(valuesAre(settings, true, true, true, true, 5, false));
}

void testSharedFileLoading() {
  // A store without a directory is off, alike for both files.
  checkSharedLoadStatus(std::string(), SaveStatus::NoSaveDirectory);

  // A fresh installation: no file yet, and nothing is created by looking.
  {
    TempDir dir;
    checkSharedLoadStatus(dir.path(), SaveStatus::NotFound);
    SC_CHECK(entryNames(dir.path()).empty());
  }

  // A directory that carries a file's name is not that file.
  {
    TempDir dir;
    std::error_code ec;
    std::filesystem::create_directories(
        dir.path() + "/" + std::string(stackfall::kSettingsFileName), ec);
    SC_CHECK(!ec);
    // The settings name is a directory and the record name is missing, so
    // each file reports its own condition from the one shared read.
    const stackfall::SettingsResult settings =
        stackfall::SettingsStore(dir.path()).load();
    SC_CHECK(settings.status == SaveStatus::Invalid);
    SC_CHECK(valuesAre(settings, true, true, true, true, 5, false));
    SC_CHECK(HighScoreStore(dir.path()).load().status == SaveStatus::NotFound);
    std::filesystem::create_directories(dir.path() + "/" + kSaveFileName, ec);
    SC_CHECK(!ec);
    checkSharedLoadStatus(dir.path(), SaveStatus::Invalid);
  }

  // A file the process cannot read exists but is invalid, not missing, and
  // the read failure changes nothing on disk.
  if (runningPrivileged()) {
    std::printf("storage: skipping the unreadable file case (root)\n");
  } else {
    TempDir dir;
    const std::string recordPath = dir.path() + "/" + kSaveFileName;
    const std::string settingsPath =
        dir.path() + "/" + std::string(stackfall::kSettingsFileName);
    writeRawFile(recordPath, formatSaveFile(4242));
    writeRawFile(settingsPath,
                 stackfall::formatSettingsFile(stackfall::SettingsValues{}));
    std::error_code ec;
    std::filesystem::permissions(recordPath, std::filesystem::perms::owner_read,
                                 std::filesystem::perm_options::remove, ec);
    SC_CHECK(!ec);
    std::filesystem::permissions(settingsPath,
                                 std::filesystem::perms::owner_read,
                                 std::filesystem::perm_options::remove, ec);
    SC_CHECK(!ec);
    checkSharedLoadStatus(dir.path(), SaveStatus::Invalid);
    // Read access back, and both files load again through the same path.
    std::filesystem::permissions(recordPath, std::filesystem::perms::owner_read,
                                 std::filesystem::perm_options::add, ec);
    SC_CHECK(!ec);
    std::filesystem::permissions(settingsPath,
                                 std::filesystem::perms::owner_read,
                                 std::filesystem::perm_options::add, ec);
    SC_CHECK(!ec);
    const LoadResult record = HighScoreStore(dir.path()).load();
    SC_CHECK(record.ok());
    SC_CHECK(record.highScore == 4242);
    const stackfall::SettingsResult settings =
        stackfall::SettingsStore(dir.path()).load();
    SC_CHECK(settings.ok());
    SC_CHECK(valuesAre(settings, true, true, true, true, 5, false));
  }

  // A file above the size cap is invalid for both, without reading past it.
  {
    TempDir dir;
    const std::string oversized(stackfall::kMaxSaveFileBytes + 1, 'x');
    writeRawFile(dir.path() + "/" + kSaveFileName, oversized);
    writeRawFile(dir.path() + "/" + std::string(stackfall::kSettingsFileName),
                 oversized);
    checkSharedLoadStatus(dir.path(), SaveStatus::Invalid);
  }

  // A good record, a good settings file, and the legacy settings files all
  // load through the shared read, so sharing it changed no valid path.
  {
    TempDir dir;
    const HighScoreStore recordStore(dir.path());
    const stackfall::SettingsStore settingsStore(dir.path());
    SC_CHECK(recordStore.save(777) == SaveStatus::Ok);
    stackfall::SettingsValues values;
    values.soundOn = false;
    values.nextPreviews = 2;
    SC_CHECK(settingsStore.save(values) == SaveStatus::Ok);
    const LoadResult record = recordStore.load();
    SC_CHECK(record.ok());
    SC_CHECK(record.highScore == 777);
    const stackfall::SettingsResult settings = settingsStore.load();
    SC_CHECK(settings.ok());
    SC_CHECK(valuesAre(settings, false, true, true, true, 2, false));

    writeRawFile(settingsStore.filePath(), "version=1\nsound=off\n");
    const stackfall::SettingsResult legacy =
        stackfall::SettingsStore(dir.path()).load();
    SC_CHECK(legacy.ok());
    SC_CHECK(valuesAre(legacy, false, true, true, true, 5, false));
    // The record beside the legacy settings is untouched by that read.
    SC_CHECK(HighScoreStore(dir.path()).load().highScore == 777);
  }
}

void testSettingsFormatAndParse() {
  using stackfall::SettingsValues;
  using stackfall::formatSettingsFile;
  using stackfall::kSettingsNextMax;
  using stackfall::kSettingsNextMin;
  using stackfall::parseSettingsFile;

  SC_CHECK(kSettingsNextMin == 1);
  SC_CHECK(kSettingsNextMax == 5);

  // The canonical rendering of specification sections 6.3 and 9: one line per
  // preference, grid, hold, next, and grayscale after the sound and ghost of
  // the earlier eras (task T044), orientation next (task T055), the music
  // mode and both volume lines after that (task T062, MUSIC.md persistence),
  // the background mode and preferred design after those (task T084), and the
  // piece designs last (task T095).
  SC_CHECK(formatSettingsFile(SettingsValues{}) ==
           "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
           "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
           "effectsvolume=8\nbackground=auto\nbackgroundid=1\npiecedesigns=off\n");
  {
    SettingsValues all;
    all.soundOn = false;
    all.ghostOn = false;
    all.gridOn = false;
    all.holdOn = false;
    all.nextPreviews = 1;
    all.grayscaleOn = true;
    all.orientation = stackfall::Orientation::Portrait;
    all.musicMode = stackfall::MusicMode::Focus;
    all.musicVolume = 0;
    all.effectsVolume = 10;
    all.backgroundMode = stackfall::backgrounds::Mode::Fixed;
    all.backgroundId = 20;
    all.pieceDesignsOn = true;
    SC_CHECK(formatSettingsFile(all) ==
             "version=1\nsound=off\nghost=off\ngrid=off\nhold=off\nnext=1\n"
             "grayscale=on\norientation=portrait\nmusic=focus\nmusicvolume=0\n"
             "effectsvolume=10\nbackground=fixed\nbackgroundid=20\npiecedesigns=on\n");
  }

  // Every canonical combination parses with the values it carries and
  // renders back to exactly the same bytes: the six toggles (sound, ghost,
  // grid, hold, grayscale, orientation) cross every next count 1..5, and the
  // music settings cross every mode and both volume steps.
  for (int booleans = 0; booleans < 64; ++booleans) {
    for (int next = kSettingsNextMin; next <= kSettingsNextMax; ++next) {
      const SettingsValues written{
          (booleans & 1) != 0,  (booleans & 2) != 0, (booleans & 4) != 0,
          (booleans & 8) != 0,  next,                (booleans & 16) != 0,
          (booleans & 32) != 0 ? stackfall::Orientation::Portrait
                               : stackfall::Orientation::Landscape};
      const std::string bytes = formatSettingsFile(written);
      SettingsValues read{false, true, false, true, 3, true,
                          stackfall::Orientation::Portrait};  // sentinels
      SC_CHECK(parseSettingsFile(bytes, &read));
      SC_CHECK(valuesAre(read, written.soundOn, written.ghostOn,
                         written.gridOn, written.holdOn, written.nextPreviews,
                         written.grayscaleOn, written.orientation));
      SC_CHECK(formatSettingsFile(read) == bytes);
    }
  }
  for (int mode = 0; mode <= static_cast<int>(stackfall::MusicMode::Off);
       ++mode) {
    for (int music = stackfall::kSettingsVolumeMin;
         music <= stackfall::kSettingsVolumeMax; ++music) {
      for (int effects = stackfall::kSettingsVolumeMin;
           effects <= stackfall::kSettingsVolumeMax; ++effects) {
        SettingsValues written;
        written.musicMode = static_cast<stackfall::MusicMode>(mode);
        written.musicVolume = music;
        written.effectsVolume = effects;
        const std::string bytes = formatSettingsFile(written);
        SettingsValues read;  // sentinels
        read.musicMode = stackfall::MusicMode::Groove;
        read.musicVolume = 1;
        read.effectsVolume = 2;
        SC_CHECK(parseSettingsFile(bytes, &read));
        SC_CHECK(valuesAre(read, true, true, true, true, 5, false,
                           stackfall::Orientation::Landscape, written.musicMode,
                           music, effects));
        SC_CHECK(formatSettingsFile(read) == bytes);
      }
    }
  }

  // The background pair of task T084 crosses every mode with every stable
  // design ID 1..20: each combination parses with its values, renders back
  // to the same bytes, and never touches the ten preferences before it.
  for (const stackfall::backgrounds::Mode mode :
       {stackfall::backgrounds::Mode::Auto, stackfall::backgrounds::Mode::Off,
        stackfall::backgrounds::Mode::Fixed}) {
    for (int design = stackfall::kSettingsBackgroundFirst;
         design <= stackfall::kSettingsBackgroundLast; ++design) {
      SettingsValues written;
      written.backgroundMode = mode;
      written.backgroundId = design;
      const std::string bytes = formatSettingsFile(written);
      SettingsValues read;  // sentinels
      read.backgroundMode = stackfall::backgrounds::Mode::Off;
      read.backgroundId = stackfall::kSettingsBackgroundLast;
      SC_CHECK(parseSettingsFile(bytes, &read));
      SC_CHECK(valuesAre(read, true, true, true, true, 5, false,
                         stackfall::Orientation::Landscape,
                         stackfall::MusicMode::Auto,
                         stackfall::kSettingsMusicVolumeDefault,
                         stackfall::kSettingsEffectsVolumeDefault, mode,
                         design));
      SC_CHECK(formatSettingsFile(read) == bytes);
    }
  }

  // The piece designs line of task T095 crosses both values: each parses
  // with its value, renders back to the same bytes, and never touches the
  // twelve preferences before it.
  for (const bool on : {false, true}) {
    SettingsValues written;
    written.pieceDesignsOn = on;
    const std::string bytes = formatSettingsFile(written);
    SC_CHECK(bytes.find("piecedesigns=") ==
             bytes.rfind("piecedesigns="));  // exactly one line, at the end
    SettingsValues read;  // sentinels
    read.pieceDesignsOn = !on;
    read.nextPreviews = 2;
    SC_CHECK(parseSettingsFile(bytes, &read));
    SC_CHECK(valuesAre(read, true, true, true, true, 5, false,
                       stackfall::Orientation::Landscape,
                       stackfall::MusicMode::Auto,
                       stackfall::kSettingsMusicVolumeDefault,
                       stackfall::kSettingsEffectsVolumeDefault,
                       stackfall::backgrounds::Mode::Auto,
                       stackfall::kSettingsBackgroundFirst, on));
    SC_CHECK(formatSettingsFile(read) == bytes);
  }

  // The legacy thirteen-line file of the background era (task T084, the form
  // before T095) keeps every saved value and receives the piece designs off,
  // which is the upgrade every player with a saved settings file makes: off
  // is the recovery value, so the cells stay plain until the player asks.
  {
    SettingsValues read;  // sentinels
    read.pieceDesignsOn = true;
    read.nextPreviews = 4;
    SC_CHECK(parseSettingsFile(
        "version=1\nsound=on\nghost=off\ngrid=on\nhold=off\nnext=4\n"
        "grayscale=on\norientation=portrait\nmusic=arcade\nmusicvolume=1\n"
        "effectsvolume=7\nbackground=fixed\nbackgroundid=9\n",
        &read));
    SC_CHECK(valuesAre(read, true, false, true, false, 4, true,
                       stackfall::Orientation::Portrait,
                       stackfall::MusicMode::Arcade, 1, 7,
                       stackfall::backgrounds::Mode::Fixed, 9, false));
    // The same file with the new line on keeps every value and the designs.
    SettingsValues withLine = read;
    withLine.pieceDesignsOn = true;
    SC_CHECK(parseSettingsFile(formatSettingsFile(withLine), &read));
    SC_CHECK(valuesAre(read, true, false, true, false, 4, true,
                       stackfall::Orientation::Portrait,
                       stackfall::MusicMode::Arcade, 1, 7,
                       stackfall::backgrounds::Mode::Fixed, 9, true));
  }

  // The legacy eleven-line file of the music era (task T062, the form before
  // T084) keeps every saved value and receives the Auto wallpaper over
  // design 1, which is the upgrade every player with a saved settings file
  // makes.
  {
    SettingsValues read;  // sentinels
    read.backgroundMode = stackfall::backgrounds::Mode::Fixed;
    read.backgroundId = 12;
    SC_CHECK(parseSettingsFile(
        "version=1\nsound=off\nghost=on\ngrid=on\nhold=on\nnext=3\n"
        "grayscale=on\norientation=portrait\nmusic=focus\nmusicvolume=2\n"
        "effectsvolume=9\n",
        &read));
    SC_CHECK(valuesAre(read, false, true, true, true, 3, true,
                       stackfall::Orientation::Portrait,
                       stackfall::MusicMode::Focus, 2, 9));
  }

  // The legacy six-line file of the grayscale era (task T044) keeps every
  // saved value and receives the landscape default (task T055).
  {
    SettingsValues read{false, false, false, false, 1, true,
                        stackfall::Orientation::Portrait};  // sentinels
    SC_CHECK(parseSettingsFile("version=1\nsound=off\nghost=on\ngrid=on\n"
                               "hold=on\nnext=3\ngrayscale=on\n",
                               &read));
    SC_CHECK(valuesAre(read, false, true, true, true, 3, true));
  }

  // The legacy eight-line file of the orientation era (task T055, the form
  // before T062) keeps every saved value and receives the MUSIC.md defaults,
  // which is the upgrade every player with a saved settings file makes.
  {
    SettingsValues read;  // sentinels
    read.musicMode = stackfall::MusicMode::Groove;
    read.musicVolume = 1;
    read.effectsVolume = 2;
    SC_CHECK(parseSettingsFile("version=1\nsound=off\nghost=on\ngrid=on\n"
                               "hold=on\nnext=3\ngrayscale=on\n"
                               "orientation=portrait\n",
                               &read));
    SC_CHECK(valuesAre(read, false, true, true, true, 3, true,
                       stackfall::Orientation::Portrait));
  }

  // The four legacy three-line files of the sound-and-ghost era (T039) keep
  // their saved values and receive the four new defaults (task T044).
  {
    SettingsValues read{false, false, false, false, 1, true};  // sentinels
    SC_CHECK(parseSettingsFile("version=1\nsound=on\nghost=on\n", &read));
    SC_CHECK(valuesAre(read, true, true, true, true, kSettingsNextMax, false));
    SC_CHECK(parseSettingsFile("version=1\nsound=on\nghost=off\n", &read));
    SC_CHECK(valuesAre(read, true, false, true, true, kSettingsNextMax, false));
    SC_CHECK(parseSettingsFile("version=1\nsound=off\nghost=on\n", &read));
    SC_CHECK(valuesAre(read, false, true, true, true, kSettingsNextMax, false));
    SC_CHECK(parseSettingsFile("version=1\nsound=off\nghost=off\n", &read));
    SC_CHECK(valuesAre(read, false, false, true, true, kSettingsNextMax,
                       false));
  }

  // The two legacy two-line files of the sound-only era (T013) still parse,
  // with the ghost on, so an upgrade never loses a saved sound choice.
  {
    SettingsValues read{false, false, false, false, 1, true};  // sentinels
    SC_CHECK(parseSettingsFile("version=1\nsound=on\n", &read));
    SC_CHECK(valuesAre(read, true, true, true, true, kSettingsNextMax, false));
    SC_CHECK(parseSettingsFile("version=1\nsound=off\n", &read));
    SC_CHECK(valuesAre(read, false, true, true, true, kSettingsNextMax, false));
  }

  // A parse that fails leaves the caller values untouched.
  {
    SettingsValues read{true, false, true, false, 2, true};  // sentinels
    SC_CHECK(!parseSettingsFile("version=1\nsound=maybe\n", &read));
    SC_CHECK(valuesAre(read, true, false, true, false, 2, true));
    SC_CHECK(!parseSettingsFile(
        "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=9\n"
        "grayscale=off\n",
        &read));
    SC_CHECK(valuesAre(read, true, false, true, false, 2, true));
  }

  // Everything that is not one of the readable renderings is a damaged
  // preference: a lost final newline, a padded or edited value, a wrong or
  // missing key, a future version, a doubled line, a carriage return, a NUL
  // byte, or an empty file. The sound-era rejections stay, the ghost-era
  // ones stay, and the four new lines carry their own.
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
      // The ghost line's own damage, beside the sound rejections above.
      "version=1\nsound=on\nghost=on",
      "version=1\nsound=on\nghost=off",
      "version=1\nsound=on\nghost=maybe\n",
      "version=1\nsound=on\nghost=On\n",
      "version=1\nsound=on\nghost=ON\n",
      "version=1\nsound=on\nghost=1\n",
      "version=1\nsound=on\nghost=true\n",
      "version=1\nsound=on\nghost=off \n",
      "version=1\nsound=on\nghost=\n",
      "version=1\nsound=on\nghost=on\n\n",
      "version=1\nsound=on\nghost=on\nghost=on\n",
      "version=1\nsound=on\nsound=off\nghost=on\n",
      "version=1\nghost=on\n",
      "version=1\nghost=off\n",
      "ghost=on\n",
      "version=1\nsound=on\nhigh_score=5\n",
      "version=1\r\nsound=on\r\nghost=on\r\n",
      "version=2\nsound=on\nghost=off\n",
      // The grid, hold, next, and grayscale lines of task T044: the same
      // damage shapes the two earlier lines already reject, plus the count
      // range of the next preference.
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\ngrayscale=off",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\ngrayscale=off"
      "\n\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\ngrayscale=off"
      "\ngrayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=maybe\nhold=on\nnext=5\n"
      "grayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=On\nhold=on\nnext=5\n"
      "grayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=1\nhold=on\nnext=5\n"
      "grayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=\nhold=on\nnext=5\n"
      "grayscale=off\n",
      "version=1\nsound=on\nghost=on\nhold=maybe\nnext=5\ngrayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=Off\nnext=5\n"
      "grayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=true\nnext=5\n"
      "grayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nnext=maybe\ngrayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=0\n"
      "grayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=6\n"
      "grayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=05\n"
      "grayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5x\n"
      "grayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=\n"
      "grayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=maybe\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=On\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=1\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=\n",
      // Reordered, missing, and extra lines of the new form.
      "version=1\ngrid=on\nsound=on\nghost=on\nhold=on\nnext=5\n"
      "grayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\ngrayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nnext=5\ngrayscale=off\n",
      "version=1\nsound=on\ngrid=on\nhold=on\nnext=5\ngrayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\nhigh_score=5\n",
      "version=1\r\nsound=on\r\nghost=on\r\ngrid=on\r\nhold=on\r\nnext=5\r\n"
      "grayscale=off\r\n",
      "version=2\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\n",
      // The orientation line of task T055: the same damage shapes, plus the
      // two values the line accepts and nothing else.
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=maybe\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=Portrait\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=up\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\norientation=portrait\n",
      "version=1\norientation=portrait\nsound=on\nghost=on\ngrid=on\n"
      "hold=on\nnext=5\ngrayscale=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nextra=1\n",
      "version=2\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=portrait\n",
      // The music lines of task T062: the same damage shapes, plus mode
      // names the enum never had and volumes outside 0..10. A file that
      // stops between the new lines never shipped, so it is damaged too.
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=maybe\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=Auto\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto \n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=jazz\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=11\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=-1\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=06\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=1x\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=11\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusicvolume=6\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nmusic=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusicvolume=6\nmusic=auto\n"
      "musicvolume=6\neffectsvolume=8\n",
      "version=2\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=off\nmusicvolume=1\n"
      "effectsvolume=2\n",
      "version=1\r\nsound=on\r\nghost=on\r\ngrid=on\r\nhold=on\r\nnext=5\r\n"
      "grayscale=off\r\norientation=landscape\r\nmusic=auto\r\n"
      "musicvolume=6\r\neffectsvolume=8\r\n",
      // The background lines of task T084: the same damage shapes, plus mode
      // words the wallpaper never had and design IDs outside the catalog's
      // 1..20. The pair shipped together, so a file that stops between the
      // two lines never existed and is damaged like every other deviation.
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=maybe\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=Auto\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=on\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=auto \n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=auto\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackgroundid=1\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=fixed\nbackgroundid=0\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=fixed\nbackgroundid=21\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=fixed\nbackgroundid=-1\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=fixed\nbackgroundid=07\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=fixed\nbackgroundid=1x\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=fixed\nbackgroundid=\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=off\nbackgroundid=1\nextra=1\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackgroundid=1\nbackground=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=off\nbackground=auto\nbackgroundid=1\n",
      "version=2\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=off\nbackgroundid=4\n",
      "version=1\r\nsound=on\r\nghost=on\r\ngrid=on\r\nhold=on\r\nnext=5\r\n"
      "grayscale=off\r\norientation=landscape\r\nmusic=auto\r\n"
      "musicvolume=6\r\neffectsvolume=8\r\nbackground=off\r\n"
      "backgroundid=4\r\n",
      // The piece designs line of task T095: the same damage shapes every
      // earlier toggle rejects, plus a fifteenth line no era ever wrote and
      // the full form that stops one line early.
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=auto\nbackgroundid=1\npiecedesigns=maybe\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=auto\nbackgroundid=1\npiecedesigns=On\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=auto\nbackgroundid=1\npiecedesigns=1\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=auto\nbackgroundid=1\npiecedesigns=off \n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=auto\nbackgroundid=1\npiecedesigns=\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=auto\nbackgroundid=1\npiecedesigns=off",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=auto\nbackgroundid=1\npiecedesigns=off\n"
      "piecedesigns=off\n",
      "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
      "grayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\n"
      "effectsvolume=8\nbackground=auto\nbackgroundid=1\npiecedesigns=off\n"
      "extra=1\n",
  };
  {
    SettingsValues read;  // any state; a rejection must not touch it
    for (const char* bytes : damaged) {
      SC_CHECK(!parseSettingsFile(bytes, &read));
    }
  }
  SC_CHECK(!parseSettingsFile(
      std::string("version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
                  "grayscale=off\n\0",
                  66),
      nullptr));
  SC_CHECK(!parseSettingsFile(std::string("version=1\nsound=on\n\0", 20),
                              nullptr));
  SC_CHECK(!parseSettingsFile(
      std::string("version=1\nsound=on\nghost=on\n\0", 29), nullptr));
  // The size cap of the record applies here too.
  SC_CHECK(!parseSettingsFile(
      std::string(stackfall::kMaxSaveFileBytes + 1, 'a'), nullptr));
  // A null out-pointer is allowed; the caller may only want the verdict.
  SC_CHECK(parseSettingsFile(
      "version=1\nsound=off\nghost=off\ngrid=off\nhold=off\nnext=1\n"
      "grayscale=on\n",
      nullptr));
  SC_CHECK(!parseSettingsFile(
      "version=1\nsound=off\nghost=off\ngrid=off\nhold=off\nnext=1\n"
      "grayscale=on",
      nullptr));
}

void testSettingsStores() {
  using stackfall::SettingsResult;
  using stackfall::SettingsStore;
  using stackfall::SettingsValues;

  // A default-constructed store has nothing to load, and every preference
  // stays at its default.
  {
    const SettingsStore store;
    SC_CHECK(!store.hasDirectory());
    SC_CHECK(store.filePath().empty());
    const SettingsResult result = store.load();
    SC_CHECK(result.status == SaveStatus::NoSaveDirectory);
    SC_CHECK(valuesAre(result, true, true, true, true, 5, false));
    SC_CHECK(store.save(SettingsValues{}) == SaveStatus::NoSaveDirectory);
  }

  // A missing file is a fresh installation: every preference at its default,
  // status not found.
  {
    TempDir dir;
    const SettingsStore store(dir.path());
    SC_CHECK(store.filePath() == dir.path() + "/" +
                                     std::string(stackfall::kSettingsFileName));
    const SettingsResult result = store.load();
    SC_CHECK(result.status == SaveStatus::NotFound);
    SC_CHECK(valuesAre(result, true, true, true, true, 5, false));
  }

  // The preferences round-trip through a second store, as a relaunch does,
  // and the file on disk is exactly the canonical form: one value changed at
  // a time, then the whole set at once.
  {
    TempDir dir;
    const SettingsStore store(dir.path());
    SettingsValues values;
    values.soundOn = false;
    SC_CHECK(store.save(values) == SaveStatus::Ok);
    SC_CHECK(readRawFile(store.filePath()) ==
             "version=1\nsound=off\nghost=on\ngrid=on\nhold=on\nnext=5\n"
             "grayscale=off\norientation=landscape\nmusic=auto\n"
             "musicvolume=6\neffectsvolume=8\nbackground=auto\n"
             "backgroundid=1\npiecedesigns=off\n");
    SettingsResult result = SettingsStore(dir.path()).load();
    SC_CHECK(result.ok());
    SC_CHECK(valuesAre(result, false, true, true, true, 5, false));

    values.ghostOn = false;
    SC_CHECK(store.save(values) == SaveStatus::Ok);
    values.gridOn = false;
    SC_CHECK(store.save(values) == SaveStatus::Ok);
    values.holdOn = false;
    SC_CHECK(store.save(values) == SaveStatus::Ok);
    values.nextPreviews = 2;
    SC_CHECK(store.save(values) == SaveStatus::Ok);
    values.grayscaleOn = true;
    SC_CHECK(store.save(values) == SaveStatus::Ok);
    result = SettingsStore(dir.path()).load();
    SC_CHECK(result.ok());
    SC_CHECK(valuesAre(result, false, false, false, false, 2, true));
    SC_CHECK(readRawFile(store.filePath()) ==
             "version=1\nsound=off\nghost=off\ngrid=off\nhold=off\nnext=2\n"
             "grayscale=on\norientation=landscape\nmusic=auto\n"
             "musicvolume=6\neffectsvolume=8\nbackground=auto\n"
             "backgroundid=1\npiecedesigns=off\n");

    values = SettingsValues{};
    values.nextPreviews = 1;
    SC_CHECK(store.save(values) == SaveStatus::Ok);
    result = SettingsStore(dir.path()).load();
    SC_CHECK(result.ok());
    SC_CHECK(valuesAre(result, true, true, true, true, 1, false));
    SC_CHECK(entryNames(dir.path()).size() == 1);
  }

  // The full thirteen-preference set round-trips through the store, exactly
  // as a relaunch reads what a session saved (tasks T062, T084, and T095):
  // every later preference - the music pool, both volumes, the background
  // mode, the preferred design, and the piece designs - comes back with the
  // value the file carries, so a load never costs the player a saved choice.
  {
    TempDir dir;
    const SettingsStore store(dir.path());
    SettingsValues values;
    values.soundOn = false;
    values.ghostOn = true;
    values.gridOn = true;
    values.holdOn = false;
    values.nextPreviews = 2;
    values.grayscaleOn = true;
    values.orientation = stackfall::Orientation::Portrait;
    values.musicMode = stackfall::MusicMode::Groove;
    values.musicVolume = 3;
    values.effectsVolume = 1;
    values.backgroundMode = stackfall::backgrounds::Mode::Fixed;
    values.backgroundId = 17;
    values.pieceDesignsOn = true;
    SC_CHECK(store.save(values) == SaveStatus::Ok);
    const SettingsResult result = SettingsStore(dir.path()).load();
    SC_CHECK(result.ok());
    SC_CHECK(valuesAre(result, false, true, true, false, 2, true,
                       stackfall::Orientation::Portrait,
                       stackfall::MusicMode::Groove, 3, 1,
                       stackfall::backgrounds::Mode::Fixed, 17, true));
    SC_CHECK(readRawFile(store.filePath()) ==
             "version=1\nsound=off\nghost=on\ngrid=on\nhold=off\nnext=2\n"
             "grayscale=on\norientation=portrait\nmusic=groove\n"
             "musicvolume=3\neffectsvolume=1\nbackground=fixed\n"
             "backgroundid=17\npiecedesigns=on\n");
    // A damaged file recovers to the whole default set, background pair and
    // piece designs included, and the read rewrites nothing on disk.
    writeRawFile(store.filePath(),
                 "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
                 "grayscale=off\norientation=landscape\nmusic=auto\n"
                 "musicvolume=6\neffectsvolume=8\nbackground=fixed\n"
                 "backgroundid=99\n");
    const SettingsResult broken = SettingsStore(dir.path()).load();
    SC_CHECK(broken.status == SaveStatus::Invalid);
    SC_CHECK(valuesAre(broken, true, true, true, true, 5, false));
    SC_CHECK(valuesAre(broken, true, true, true, true, 5, false,
                       stackfall::Orientation::Landscape,
                       stackfall::MusicMode::Auto,
                       stackfall::kSettingsMusicVolumeDefault,
                       stackfall::kSettingsEffectsVolumeDefault,
                       stackfall::backgrounds::Mode::Auto,
                       stackfall::kSettingsBackgroundFirst, false));
    SC_CHECK(readRawFile(store.filePath()).find("backgroundid=99\n") !=
             std::string::npos);
  }

  // A legacy three-line file loads with its saved sound and ghost choices and
  // the four new defaults (the acceptance case of task T044:
  // version=1/sound=off/ghost=off), and the next save upgrades it to the
  // current canonical form.
  {
    TempDir dir;
    const SettingsStore store(dir.path());
    writeRawFile(store.filePath(), "version=1\nsound=off\nghost=off\n");
    SettingsResult result = store.load();
    SC_CHECK(result.ok());
    SC_CHECK(valuesAre(result, false, false, true, true, 5, false));
    SC_CHECK(store.save(SettingsValues{}) == SaveStatus::Ok);
    SC_CHECK(readRawFile(store.filePath()) ==
             "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
             "grayscale=off\norientation=landscape\nmusic=auto\n"
             "musicvolume=6\neffectsvolume=8\nbackground=auto\n"
             "backgroundid=1\npiecedesigns=off\n");
    result = SettingsStore(dir.path()).load();
    SC_CHECK(result.ok());
    SC_CHECK(valuesAre(result, true, true, true, true, 5, false));

    writeRawFile(store.filePath(), "version=1\nsound=on\nghost=off\n");
    result = store.load();
    SC_CHECK(result.ok());
    SC_CHECK(valuesAre(result, true, false, true, true, 5, false));
  }

  // A legacy two-line file loads with its saved sound choice and the ghost on
  // plus the four new defaults, and the next save upgrades it too.
  {
    TempDir dir;
    const SettingsStore store(dir.path());
    writeRawFile(store.filePath(), "version=1\nsound=off\n");
    SettingsResult result = store.load();
    SC_CHECK(result.ok());
    SC_CHECK(valuesAre(result, false, true, true, true, 5, false));
    SettingsValues kept = result;
    kept.grayscaleOn = true;
    SC_CHECK(store.save(kept) == SaveStatus::Ok);
    SC_CHECK(readRawFile(store.filePath()) ==
             "version=1\nsound=off\nghost=on\ngrid=on\nhold=on\nnext=5\n"
             "grayscale=on\norientation=landscape\nmusic=auto\n"
             "musicvolume=6\neffectsvolume=8\nbackground=auto\n"
             "backgroundid=1\npiecedesigns=off\n");
    result = SettingsStore(dir.path()).load();
    SC_CHECK(result.ok());
    SC_CHECK(valuesAre(result, false, true, true, true, 5, true));

    writeRawFile(store.filePath(), "version=1\nsound=on\n");
    result = store.load();
    SC_CHECK(result.ok());
    SC_CHECK(valuesAre(result, true, true, true, true, 5, false));
  }

  // The record and the settings live side by side in one save directory.
  {
    TempDir dir;
    const HighScoreStore record(dir.path());
    const SettingsStore settings(dir.path());
    SC_CHECK(record.save(1234) == SaveStatus::Ok);
    SettingsValues values;
    values.soundOn = false;
    SC_CHECK(settings.save(values) == SaveStatus::Ok);
    const std::vector<std::string> names = entryNames(dir.path());
    SC_CHECK(names.size() == 2);
    SC_CHECK(std::find(names.begin(), names.end(), kSaveFileName) != names.end());
    SC_CHECK(std::find(names.begin(), names.end(),
                       stackfall::kSettingsFileName) != names.end());
    SC_CHECK(record.load().ok());
    const SettingsResult both = SettingsStore(dir.path()).load();
    SC_CHECK(valuesAre(both, false, true, true, true, 5, false));
  }

  // fromEnvironment() resolves the same directory the record store uses, so
  // one STACKFALL_SAVE_DIR carries both files.
  {
    TempDir dir;
    const EnvGuard saveDir(stackfall::kSaveDirEnv, dir.path());
    const SettingsStore store = SettingsStore::fromEnvironment();
    SC_CHECK(store.hasDirectory());
    SettingsValues values;
    values.soundOn = false;
    values.ghostOn = false;
    SC_CHECK(store.save(values) == SaveStatus::Ok);
    const SettingsResult result = SettingsStore::fromEnvironment().load();
    SC_CHECK(result.ok());
    SC_CHECK(valuesAre(result, false, false, true, true, 5, false));
  }

  // Damage of every kind keeps every preference at its default and never
  // throws: text garbage, a foreign version, a partial file, a damaged
  // preference value, a NUL byte, a binary blob, an oversized file, and a
  // directory at the file's path.
  {
    TempDir dir;
    const SettingsStore store(dir.path());
    const std::string damaged[] = {
        "",
        "hello",
        "version=2\nsound=off\n",
        "version=1\nsound=of",
        "version=1\nsound=on\nghost=maybe\n",
        "version=1\nsound=on\nghost=on\nghost=on\n",
        "version=1\nghost=off\n",
        "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n",
        "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=9\n"
        "grayscale=off\n",
        "version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\n"
        "grayscale=off\ngrayscale=off\n",
        std::string("version=1\nsound=off\n\0extra", 23),
        std::string(64, '\x7f'),
        std::string(stackfall::kMaxSaveFileBytes + 1, 'x'),
    };
    for (const std::string& bytes : damaged) {
      writeRawFile(store.filePath(), bytes);
      const SettingsResult result = store.load();
      SC_CHECK(result.status == SaveStatus::Invalid);
      SC_CHECK(valuesAre(result, true, true, true, true, 5, false));
    }
    std::error_code ec;
    std::filesystem::create_directories(store.filePath(), ec);
    const SettingsResult result = store.load();
    SC_CHECK(result.status == SaveStatus::Invalid);
    SC_CHECK(valuesAre(result, true, true, true, true, 5, false));
    // The next save repairs the file.
    SC_CHECK(store.save(SettingsValues{}) == SaveStatus::Ok);
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
  std::printf("test_storage: settings file of the player preferences\n");
  testSettingsFormatAndParse();
  testSettingsStores();
  std::printf("test_storage: shared bounded file loading\n");
  testSharedFileLoading();
}
