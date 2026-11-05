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

#include "stackfall/bindings.hpp"
#include "stackfall/music.hpp"
#include "stackfall/types.hpp"

namespace stackfall {

// The record's name inside the save directory (specification section 10.3).
// The settings file of the player preferences sits beside it; see
// kSettingsFileName.
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
  // the record rule of sections 4.7 and 9: the platform layer saves the
  // running score the moment it passes the stored record and saves again
  // when a game-over round sets a new record, and writes the value it was
  // given.
  SaveStatus save(std::uint64_t highScore) const;

 private:
  std::string directory_;  // empty: persistence is off
};

// ---------------------------------------------------------------------------
// The settings file (tasks T013, T039, T044, and T055, specification section
// 6.3). The preferences of the player persist beside the high score, in the
// same save directory and through the same atomic write, as one more
// versioned ASCII file with one line per preference:
//
//   version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\ngrayscale=off\n
//   orientation=landscape\n
//
// `sound=off` starts the session muted, `ghost=off` hides the ghost landing
// preview, `grid=off` removes the faint board grid, `hold=off` ignores every
// hold input and hides the Hold preview, `next=1..5` draws that many Next
// previews in queue order, `grayscale=on` draws the play scene without color,
// and `orientation=portrait` turns the scene and the controls a quarter for
// the sideways grip of task T055 - at startup here and again on the options
// row that selects it (task T068). Every other
// outcome, including a missing, unreadable, or damaged file, starts every
// preference at its default (sound, ghost, grid, and hold on, five previews,
// grayscale off, landscape), so a broken settings file can only cost the
// preferences and never the game. The format rules follow the high-score
// record: the exact canonical rendering of the values, nothing more and
// nothing less.
//
// The grid, hold, next, and grayscale lines are new (T044), the orientation
// line newer still (T055), and the music mode and both volume lines newest
// (T062); a settings file written before them carries only some of the lines.
// Every format change so far only appended whole lines, so the parser accepts
// any prefix of the canonical key order and fills the preferences a legacy
// file does not name with the defaults above - an upgrade never loses a
// saved sound, ghost, or orientation choice - and the next save rewrites the
// file in the current ten-preference form. That is why the format version
// stays 1, exactly as the high-score record's does: no version 1 file has
// ever carried a different meaning for a line.
// ---------------------------------------------------------------------------

// The settings file's name inside the save directory.
inline constexpr char kSettingsFileName[] = "settings.dat";

// The next-preference bounds: the option cycles 1..5, and 5 is the default
// composition of the five queue previews (task T044).
inline constexpr int kSettingsNextMin = 1;
inline constexpr int kSettingsNextMax = 5;

// The volume-preference bounds and defaults (MUSIC.md, player controls): the
// eleven-step table of MusicEngine, with music a step under effects so the
// cues stay readable over the soundtrack.
inline constexpr int kSettingsVolumeMin = 0;
inline constexpr int kSettingsVolumeMax = 10;
inline constexpr int kSettingsMusicVolumeDefault = 6;
inline constexpr int kSettingsEffectsVolumeDefault = 8;

// The ten player preferences of one session, in settings-file line order.
// The defaults are the recovery values of a damaged file: every toggle on,
// five Next previews, color on, landscape, the Auto music pool, and the two
// volume defaults above.
struct SettingsValues {
  bool soundOn = true;
  bool ghostOn = true;
  bool gridOn = true;
  bool holdOn = true;
  int nextPreviews = kSettingsNextMax;
  bool grayscaleOn = false;
  Orientation orientation = Orientation::Landscape;
  MusicMode musicMode = MusicMode::Auto;
  int musicVolume = kSettingsMusicVolumeDefault;
  int effectsVolume = kSettingsEffectsVolumeDefault;
};

// Reads `bytes` as the settings file. Accepts exactly the canonical
// renderings: every strict prefix of the ten preference lines, from the
// two-line sound-only era to the full form, each line exactly as
// formatSettingsFile writes it; a prefix leaves every preference it does not
// name at its default. Rejects unsupported mode names and volumes outside
// 0..10 along with every other deviation (MUSIC.md, persistence and
// compatibility). Returns false and leaves *values untouched on anything
// else.
bool parseSettingsFile(const std::string& bytes, SettingsValues* values);

// Renders the canonical settings file of the ten preferences.
std::string formatSettingsFile(const SettingsValues& values);

// The outcome of a settings load: the preference states to use and how they
// were obtained. Every status except Ok leaves every preference at its
// default.
struct SettingsResult : SettingsValues {
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

  // Reads the seven preferences. Each stays at its default unless a valid
  // file changes it.
  SettingsResult load() const;

  // Writes all seven preferences through the same atomic sequence as the
  // record. A save always writes the current canonical form, so a legacy file
  // upgrades on its next save.
  SaveStatus save(const SettingsValues& values) const;

 private:
  std::string directory_;  // empty: persistence is off
};

// ---------------------------------------------------------------------------
// The controls file (task T066, specification section 5). The custom bindings
// of the controls customization screen persist beside the record and the
// settings, in the same save directory and through the same atomic write, as
// one more versioned ASCII file with one line per bound action:
//
//   version=1\nlandscape.pad.left=left\n... landscape.key.pause=enter+esc\n
//   portrait.pad.left=down\n... portrait.key.pause=enter+esc\n
//
// Four blocks of eight lines, one block per orientation and device, in the
// fixed order landscape pad, landscape keyboard, portrait pad, portrait
// keyboard, each action of stackfall/bindings.hpp order on its own line. The
// value is the canonical control name, or two of them joined by '+' for the
// pairs section 5 names - `y+l1`, `enter+esc`. The canonical names are the
// words of stackfall/bindings.hpp, which the script language and the
// reference replay use too.
//
// The file is new in task T066, so there is no legacy form: a session that
// finds no file plays the section 5 defaults, and a session that finds a file
// it cannot read as the exact canonical rendering of all thirty-three lines -
// a truncated line, an unknown control, a control serving two actions, a
// padded value - recovers every table to those defaults and carries on, under
// the same recovery rule as the record and the settings: a damaged file costs
// the customizations and never the game. The next save rewrites the whole
// file in the current form.
// ---------------------------------------------------------------------------

// The controls file's name inside the save directory.
inline constexpr char kControlsFileName[] = "controls.dat";

// Reads `bytes` as the controls file. Accepts exactly the canonical
// thirty-three-line rendering described above; a file that stops between
// whole lines is damaged, because no partial form ever shipped. Returns false
// and leaves *bindings untouched on anything else.
bool parseControlsFile(const std::string& bytes, ControlBindings* bindings);

// Renders the canonical controls file of the four tables.
std::string formatControlsFile(const ControlBindings& bindings);

// The outcome of a controls load: the binding tables to use and how they were
// obtained. Every status except Ok leaves the tables at the section 5
// defaults.
struct ControlsResult : ControlBindings {
  SaveStatus status = SaveStatus::NotFound;

  bool ok() const { return status == SaveStatus::Ok; }
};

// One controls location, with the same shape and the same failure rules as
// HighScoreStore and SettingsStore so the platform layer treats the three
// files alike.
class ControlsStore {
 public:
  ControlsStore() = default;
  explicit ControlsStore(std::string directory);
  static ControlsStore fromEnvironment();

  bool hasDirectory() const { return !directory_.empty(); }
  const std::string& directory() const { return directory_; }
  std::string filePath() const;

  // Reads the four binding tables. Each stays at its default unless a valid
  // file changes it.
  ControlsResult load() const;

  // Writes the four tables through the same atomic sequence as the record.
  SaveStatus save(const ControlBindings& bindings) const;

 private:
  std::string directory_;  // empty: persistence is off
};

}  // namespace stackfall
