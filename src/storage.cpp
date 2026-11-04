// Atomic high-score persistence, task T012 (specification sections 9 and
// 10.3), and the settings file of the player preferences, tasks T013, T039,
// and T044 (specification section 6.3). See include/stackfall/storage.hpp for
// the file formats, the directory resolution, and the atomic write sequence
// the files share. Task T046 gives the two loads one shared bounded-read
// procedure, readSaveFile below, while each store keeps its own parser, its
// own default values, and its own recovery boundary
// (notes/refactor-review.md).
//
// Everything here reports failure through SaveStatus instead of throwing: a
// missing, invalid, or partial record is a high score of 0, never a crash, and
// a failed save leaves the previous record untouched (specification section 9,
// section 11 item 20). A damaged settings file costs only the preferences,
// which fall back to their defaults.

#include "stackfall/storage.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

#include <filesystem>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace stackfall {
namespace {

// Joins `directory` and `name` with exactly one separator, so a trailing slash
// in an environment value cannot produce a doubled one.
std::string joinPath(const std::string& directory, const char* name) {
  if (!directory.empty() && directory.back() == '/') {
    return directory + name;
  }
  return directory + "/" + name;
}

std::string environmentValue(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr ? std::string(value) : std::string();
}

bool isAbsolutePath(const std::string& path) {
  return !path.empty() && path[0] == '/';
}

// The decimal process id, as a unique tag for the temporary file name, so two
// STACKFALL processes sharing one save directory cannot overwrite each other's
// temporary file.
std::string processTag() {
#if defined(_WIN32)
  const int pid = _getpid();
#else
  const int pid = static_cast<int>(::getpid());
#endif
  char text[24];
  std::snprintf(text, sizeof(text), "%d", pid);
  return std::string(text);
}

// The temporary file name inside the save directory. It is hidden (the leading
// dot keeps a directory listing clean) and it lives in the same directory as
// the file it will become, so the rename that publishes it cannot cross a
// filesystem.
std::string tempFileName(const char* fileName) {
  return "." + std::string(fileName) + ".tmp." + processTag();
}

// Reads the whole file, capped at kMaxSaveFileBytes plus one byte, so a file
// at or beyond the cap is rejected by size instead of being read. Returns
// false when the file cannot be opened or read.
bool readSmallFile(const std::string& path, std::string* bytes) {
  std::FILE* stream = std::fopen(path.c_str(), "rb");
  if (stream == nullptr) {
    return false;
  }
  bytes->assign(kMaxSaveFileBytes + 1, '\0');
  const std::size_t read =
      std::fread(&(*bytes)[0], 1, kMaxSaveFileBytes + 1, stream);
  const bool ok = std::ferror(stream) == 0;
  std::fclose(stream);
  if (!ok) {
    return false;
  }
  bytes->resize(read);
  return true;
}

// The atomic write both files share (specification section 9): write `bytes`
// to a temporary file inside `directory`, flush it, push it to the device,
// close it, and rename it over `directory/fileName`. A crash or power loss can
// only leave the previous complete file or no file at all.
SaveStatus atomicWriteTextFile(const std::string& directory,
                              const char* fileName, const std::string& bytes) {
  if (directory.empty()) {
    return SaveStatus::NoSaveDirectory;
  }
  const std::string target = joinPath(directory, fileName);
  const std::string temporary =
      joinPath(directory, tempFileName(fileName).c_str());
  try {
    const std::filesystem::path directoryPath(directory);
    std::error_code ec;
    std::filesystem::create_directories(directoryPath, ec);
    if (ec) {
      std::error_code probe;
      if (!std::filesystem::is_directory(directoryPath, probe)) {
        return SaveStatus::WriteFailed;
      }
    }

    std::FILE* stream = std::fopen(temporary.c_str(), "wb");
    if (stream == nullptr) {
      return SaveStatus::WriteFailed;
    }
    bool ok =
        std::fwrite(bytes.data(), 1, bytes.size(), stream) == bytes.size();
    if (ok) {
      ok = std::fflush(stream) == 0;
    }
#if defined(__unix__) || defined(__APPLE__)
    // Push the bytes to the device before the rename publishes them, so a
    // power loss cannot publish an empty file on the handheld. On a platform
    // without fsync the flush and close above remain the whole guarantee.
    if (ok) {
      ok = ::fsync(::fileno(stream)) == 0;
    }
#endif
    if (std::fclose(stream) != 0) {
      ok = false;
    }
    if (!ok) {
      std::error_code removeEc;
      std::filesystem::remove(std::filesystem::path(temporary), removeEc);
      return SaveStatus::WriteFailed;
    }

    std::error_code renameEc;
    std::filesystem::rename(std::filesystem::path(temporary),
                            std::filesystem::path(target), renameEc);
    if (renameEc) {
      // The previous file, if any, is still the file: remove the orphaned
      // temporary file and report the failure.
      std::error_code removeEc;
      std::filesystem::remove(std::filesystem::path(temporary), removeEc);
      return SaveStatus::WriteFailed;
    }
    return SaveStatus::Ok;
  } catch (...) {
    // Never let a storage error escape into the game loop. The temporary file
    // may be left behind here; the next successful save reuses its name.
    return SaveStatus::WriteFailed;
  }
}

// The bytes of one save file, or the status that stopped the read before any
// byte was usable. `bytes` is empty unless the status is Ok.
struct SaveFileBytes {
  SaveStatus status = SaveStatus::Ok;
  std::string bytes;
};

// The bounded read both loads share (task T046): resolve `directory` and
// `fileName`, check that something exists there, accept only a regular file,
// and read it through the size cap of readSmallFile. One place maps an absent
// directory, a missing file, a name that is not a regular file, and a file
// that exists but cannot be read to their statuses. The two stores keep their
// own parsers, their own default values, and their own recovery boundary;
// nothing a parser throws escapes the store that called it.
SaveFileBytes readSaveFile(const std::string& directory, const char* fileName) {
  SaveFileBytes file;
  if (directory.empty()) {
    file.status = SaveStatus::NoSaveDirectory;
    return file;
  }
  try {
    const std::string path = joinPath(directory, fileName);
    const std::filesystem::file_status status =
        std::filesystem::status(std::filesystem::path(path));
    if (!std::filesystem::exists(status)) {
      file.status = SaveStatus::NotFound;
      return file;
    }
    // A directory or device that happens to carry the file's name is not that
    // file; treat it as invalid rather than reading it.
    if (!std::filesystem::is_regular_file(status)) {
      file.status = SaveStatus::Invalid;
      return file;
    }
    if (!readSmallFile(path, &file.bytes)) {
      file.status = SaveStatus::Invalid;  // exists, but cannot be read
      return file;
    }
    return file;
  } catch (...) {
    // A storage error is never a game error: the caller recovers to its own
    // defaults, exactly as it does for a file that failed to parse.
    file.bytes.clear();
    file.status = SaveStatus::Invalid;
    return file;
  }
}

// The settings file as the sound-only era of task T013 wrote it. The parser
// still accepts both legacy renderings, with the preferences the file does
// name at their saved values and every later preference at its default.
std::string legacySettingsFile(bool soundOn) {
  return std::string("version=") + std::to_string(kSaveFormatVersion) +
         "\nsound=" + (soundOn ? "on" : "off") + "\n";
}

// The settings file as the sound-and-ghost era of task T039 wrote it, before
// the grid, hold, next, and grayscale lines of T044 joined.
std::string legacySettingsFile(bool soundOn, bool ghostOn) {
  return std::string("version=") + std::to_string(kSaveFormatVersion) +
         "\nsound=" + (soundOn ? "on" : "off") +
         "\nghost=" + (ghostOn ? "on" : "off") + "\n";
}

}  // namespace

const char* saveStatusName(SaveStatus status) {
  switch (status) {
    case SaveStatus::Ok:
      return "ok";
    case SaveStatus::NoSaveDirectory:
      return "no save directory";
    case SaveStatus::NotFound:
      return "not found";
    case SaveStatus::Invalid:
      return "invalid";
    case SaveStatus::WriteFailed:
      return "write failed";
  }
  return "?";
}

std::string formatSaveFile(std::uint64_t highScore) {
  std::string bytes = "version=";
  bytes += std::to_string(kSaveFormatVersion);
  bytes += "\nhigh_score=";
  bytes += std::to_string(highScore);
  bytes += "\n";
  return bytes;
}

bool parseSaveFile(const std::string& bytes, std::uint64_t* highScore) {
  if (highScore != nullptr) {
    *highScore = 0;
  }
  if (bytes.size() > kMaxSaveFileBytes) {
    return false;
  }

  // The version line must match the format we write, so a record from a future
  // or hand-edited version is not a readable record.
  const std::string versionLine =
      "version=" + std::to_string(kSaveFormatVersion) + "\n";
  if (bytes.size() < versionLine.size() ||
      bytes.compare(0, versionLine.size(), versionLine) != 0) {
    return false;
  }

  // The second line must carry the score key and end the file, so a file that
  // lost its final newline, or that keeps going after the score, is partial or
  // edited rather than valid.
  const std::string scoreKey = "high_score=";
  const std::size_t valueStart = versionLine.size() + scoreKey.size();
  if (bytes.size() <= valueStart ||
      bytes.compare(versionLine.size(), scoreKey.size(), scoreKey) != 0 ||
      bytes.back() != '\n') {
    return false;
  }

  // Decimal parse with an overflow check; every other character, including a
  // sign, a space, or a carriage return, fails here.
  const std::size_t digitsEnd = bytes.size() - 1;  // one past the last digit
  std::uint64_t value = 0;
  for (std::size_t i = valueStart; i < digitsEnd; ++i) {
    const char digit = bytes[i];
    if (digit < '0' || digit > '9') {
      return false;
    }
    const std::uint64_t add = static_cast<std::uint64_t>(digit - '0');
    if (value > ((~std::uint64_t{0}) - add) / 10) {
      return false;  // above the 64-bit limit
    }
    value = value * 10 + add;
  }

  // Exact-format check: the file must equal the canonical rendering of the
  // value it carried, so leading zeros, a trailing space, a carriage return,
  // or a whole second record cannot pass.
  if (bytes != formatSaveFile(value)) {
    return false;
  }
  if (highScore != nullptr) {
    *highScore = value;
  }
  return true;
}

std::string saveDirectoryFromEnvironment() {
  // The launcher's explicit directory wins, because it names the game's own
  // conf/ directory without depending on a desktop convention.
  const std::string explicitDir = environmentValue(kSaveDirEnv);
  if (!explicitDir.empty()) {
    return explicitDir;
  }
  // XDG_DATA_HOME names a data home, so the record goes directly into it: the
  // launcher sets it to $GAMEDIR/conf for exactly this purpose. A relative
  // value is not a data home per the XDG specification and is ignored.
  const std::string xdg = environmentValue("XDG_DATA_HOME");
  if (isAbsolutePath(xdg)) {
    return xdg;
  }
  // The default data directory of the user plus the application directory, so
  // a desktop build without a launcher stays inside its own folder.
  const std::string home = environmentValue("HOME");
  if (!home.empty()) {
    return joinPath(home, ".local/share/stackfall");
  }
  // No hint at all: persistence turns off rather than guessing a location.
  return std::string();
}

std::string logFilePath(const std::string& saveDirectory) {
  // The explicit file wins and is used exactly as given, so a test run, a
  // desktop launch, and the future launcher can each place log.txt
  // deliberately (task T014, specification section 9).
  const std::string explicitPath = environmentValue(kLogFileEnv);
  if (!explicitPath.empty()) {
    return explicitPath;
  }
  // Without an explicit file, the log lands beside the high-score record in
  // the save directory, whichever way that directory was named: the option of
  // the running process or the environment. The specification's own log.txt in
  // the game directory comes from the launcher's stdout redirection (section
  // 10.3), so this default keeps the two sinks from ever writing the same
  // file.
  if (saveDirectory.empty()) {
    return std::string();
  }
  return joinPath(saveDirectory, kLogFileName);
}

std::string logFilePathFromEnvironment() {
  return logFilePath(saveDirectoryFromEnvironment());
}

HighScoreStore::HighScoreStore(std::string directory)
    : directory_(std::move(directory)) {
  // An empty value from the environment means "off"; a bare "/" stays a valid
  // directory, and joinPath already keeps its single separator.
}

HighScoreStore HighScoreStore::fromEnvironment() {
  return HighScoreStore(saveDirectoryFromEnvironment());
}

std::string HighScoreStore::filePath() const {
  if (directory_.empty()) {
    return std::string();
  }
  return joinPath(directory_, kSaveFileName);
}

LoadResult HighScoreStore::load() const {
  LoadResult result;  // high score 0, NotFound
  const SaveFileBytes file = readSaveFile(directory_, kSaveFileName);
  if (file.status != SaveStatus::Ok) {
    result.status = file.status;
    return result;
  }
  try {
    std::uint64_t value = 0;
    if (!parseSaveFile(file.bytes, &value)) {
      result.status = SaveStatus::Invalid;
      return result;
    }
    result.highScore = value;
    result.status = SaveStatus::Ok;
    return result;
  } catch (...) {
    // A storage error is never a game error: recover to 0 and carry on.
    result.highScore = 0;
    result.status = SaveStatus::Invalid;
    return result;
  }
}

SaveStatus HighScoreStore::save(std::uint64_t highScore) const {
  // The store holds no cached value, so the caller owns the record rule of
  // section 4.7: the platform layer saves when a round sets a new record, and
  // writes the value it was given.
  return atomicWriteTextFile(directory_, kSaveFileName,
                             formatSaveFile(highScore));
}

// ---------------------------------------------------------------------------
// The settings file of the player preferences (tasks T013, T039, and T044,
// specification section 6.3). Same directory, same write sequence, one line
// per preference.
// ---------------------------------------------------------------------------

std::string formatSettingsFile(const SettingsValues& values) {
  std::string bytes = "version=";
  bytes += std::to_string(kSaveFormatVersion);
  bytes += "\nsound=";
  bytes += values.soundOn ? "on" : "off";
  bytes += "\nghost=";
  bytes += values.ghostOn ? "on" : "off";
  bytes += "\ngrid=";
  bytes += values.gridOn ? "on" : "off";
  bytes += "\nhold=";
  bytes += values.holdOn ? "on" : "off";
  bytes += "\nnext=";
  bytes += std::to_string(values.nextPreviews);
  bytes += "\ngrayscale=";
  bytes += values.grayscaleOn ? "on" : "off";
  bytes += "\n";
  return bytes;
}

bool parseSettingsFile(const std::string& bytes, SettingsValues* values) {
  if (bytes.size() > kMaxSaveFileBytes) {
    return false;
  }
  // Exact-format check against every readable rendering, so only whole,
  // canonical files continue and everything else, including one that lost its
  // final newline, is a damaged preference rather than a setting.
  SettingsValues parsed;
  bool readable = false;
  // The four boolean preferences pair with the grayscale flag and the next
  // count: 2^5 combinations times the five counts are the 160 canonical
  // renderings of the current format.
  for (int booleans = 0; booleans < 32; ++booleans) {
    for (int next = kSettingsNextMin; next <= kSettingsNextMax; ++next) {
      const SettingsValues candidate{(booleans & 1) != 0, (booleans & 2) != 0,
                                     (booleans & 4) != 0, (booleans & 8) != 0,
                                     next, (booleans & 16) != 0};
      if (bytes == formatSettingsFile(candidate)) {
        parsed = candidate;
        readable = true;
      }
    }
  }
  // The legacy three-line file of the sound-and-ghost era parses with the
  // four later preferences at their defaults, so an upgrade never loses a
  // saved sound or ghost choice.
  for (int soundValue = 0; soundValue <= 1; ++soundValue) {
    for (int ghostValue = 0; ghostValue <= 1; ++ghostValue) {
      if (bytes == legacySettingsFile(soundValue == 1, ghostValue == 1)) {
        parsed = SettingsValues{soundValue == 1, ghostValue == 1, true, true,
                                kSettingsNextMax, false};
        readable = true;
      }
    }
  }
  // The legacy two-line file of the sound-only era parses with the ghost and
  // the four later preferences at their defaults.
  for (int soundValue = 0; soundValue <= 1; ++soundValue) {
    if (bytes == legacySettingsFile(soundValue == 1)) {
      parsed = SettingsValues{soundValue == 1, true, true, true,
                              kSettingsNextMax, false};
      readable = true;
    }
  }
  if (!readable) {
    return false;
  }
  if (values != nullptr) {
    *values = parsed;
  }
  return true;
}

SettingsStore::SettingsStore(std::string directory)
    : directory_(std::move(directory)) {}

SettingsStore SettingsStore::fromEnvironment() {
  return SettingsStore(saveDirectoryFromEnvironment());
}

std::string SettingsStore::filePath() const {
  if (directory_.empty()) {
    return std::string();
  }
  return joinPath(directory_, kSettingsFileName);
}

SettingsResult SettingsStore::load() const {
  SettingsResult result;  // every preference at its default, NotFound
  const SaveFileBytes file = readSaveFile(directory_, kSettingsFileName);
  if (file.status != SaveStatus::Ok) {
    result.status = file.status;
    return result;
  }
  try {
    SettingsValues values;
    if (!parseSettingsFile(file.bytes, &values)) {
      result.status = SaveStatus::Invalid;
      return result;
    }
    result.soundOn = values.soundOn;
    result.ghostOn = values.ghostOn;
    result.gridOn = values.gridOn;
    result.holdOn = values.holdOn;
    result.nextPreviews = values.nextPreviews;
    result.grayscaleOn = values.grayscaleOn;
    result.status = SaveStatus::Ok;
    return result;
  } catch (...) {
    // A damaged preference never costs more than the preferences: every one
    // stays at its default and the game carries on.
    result.soundOn = true;
    result.ghostOn = true;
    result.gridOn = true;
    result.holdOn = true;
    result.nextPreviews = kSettingsNextMax;
    result.grayscaleOn = false;
    result.status = SaveStatus::Invalid;
    return result;
  }
}

SaveStatus SettingsStore::save(const SettingsValues& values) const {
  return atomicWriteTextFile(directory_, kSettingsFileName,
                             formatSettingsFile(values));
}

}  // namespace stackfall
