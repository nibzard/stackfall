#pragma once

// Developer automation for one STACKFALL session: the virtual gamepad, the
// scripted input driver, and the checkpoint capture of docs/AGENT_TESTING.md
// (tasks T023 and T048). Everything here is inert until a harness asks for
// it: a normal startup attaches no pad, loads no script, opens no evidence
// file, and captures nothing, so the module changes nothing about ordinary
// play.
//
// The module owns the resources only developer automation needs - the virtual
// pad, the parsed command list, the three evidence streams, and the
// screenshot directory - and exposes them as a small interface of two halves:
//
//   * input injection: attachPad() and pumpScript() queue real SDL events,
//     exactly as a keyboard and a controller do, so the adapter, the loop,
//     and the round cannot tell a scripted press from a human one;
//   * capture: captureCheckpoints() saves the frame the loop just rendered
//     as one BMP per marked checkpoint and appends one structured
//     observation record per mark. The image is 8-bit BI_RLE8 of the frame's
//     own palette, and the session stops writing images once their bytes
//     pass the budget (task T054); records never depend on an image.
//
// What stays outside, in src/main.cpp, on purpose: the fixed-step timing, the
// simulation ticks, the rendering, and the startup order. The loop calls
// pumpScript() before its event poll and captureCheckpoints() after its
// render, so a checkpoint names the state the frame drew; the module never
// drives the loop itself. The round pointers below are the only application
// state the module holds, because the `restart` and `fixture` commands change
// the round outside a simulation tick.
//
// The script language (docs/AGENT_TESTING.md, and the same table the headless
// reference replay of tests/harness implements independently):
//
//   key <name> down|up    push a keyboard event into the SDL queue
//   pad <name> down|up    press a virtual pad button
//   wait <ticks>          hold the script until that many ticks ran
//   stall <ms>            delay the frame, forcing a catch-up batch
//   detach                detach the virtual controller
//   restart               restart the round with the same seed
//   fixture <name>        load a board fixture from the fixture directory
//   check <name>          record one observation and one screenshot
//   shot <name>           record one screenshot, no observation
//   quit                  leave the main loop cleanly
//   echo <text>           log a marker line
//
// The reference replay keeps its own parser, its own key table, and its own
// pad-button table on purpose: they must detect a defect in this module's
// tables rather than share it. Only the bounded count rule of
// stackfall/script.hpp is shared, because a fuzzed count must fail the same
// way on both sides. Every line this module prints - script markers, syntax
// reports, checkpoint names, screenshot failures - reaches stdout or stderr
// and the session log main opened, so log.txt keeps the same bytes it always
// had.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <SDL.h>

#include "stackfall/game.hpp"
#include "stackfall/input_sdl.hpp"

namespace stackfall {

// The default --screenshot-budget: 256 MiB of checkpoint images per session
// (task T054). The option's 0 means no budget at all.
constexpr std::uint64_t kDefaultScreenshotBudget = 256ull * 1024 * 1024;

class CueDetector;  // audio_sdl.hpp; held as a pointer the script resets

// Owns the developer virtual gamepad (docs/AGENT_TESTING.md): the attached
// virtual device and the joystick handle opened on it. A script's `detach`
// command releases the pad mid-session, so reset() is idempotent and the
// destructor is a no-op after it; no exit path can close the pad twice.
class VirtualController {
 public:
  VirtualController() = default;

  ~VirtualController() { reset(); }

  VirtualController(const VirtualController&) = delete;
  VirtualController& operator=(const VirtualController&) = delete;

  // Attaches the pad and opens its joystick, so `pad` script commands and
  // controller discovery have a device. Returns false when SDL refused the
  // virtual device or its mapping; the session then runs without one, exactly
  // as when no option asked for it. The attach helpers of src/input_sdl.cpp
  // print their own lines.
  bool attach() {
    if (joystick_ != nullptr) {
      return true;
    }
    const int device = attachVirtualController();
    if (device < 0) {
      return false;
    }
    joystick_ = SDL_JoystickOpen(device);
    if (joystick_ == nullptr) {
      // The device exists but its handle does not. Detach it again, so no
      // attached pad outlives the handle that failed to open.
      detachVirtualController(device);
      return false;
    }
    device_ = device;
    return true;
  }

  // Releases the pad: closes the joystick handle, then detaches the virtual
  // device. Safe to call again after any outcome.
  void reset() {
    if (joystick_ != nullptr) {
      SDL_JoystickClose(joystick_);
      joystick_ = nullptr;
    }
    if (device_ >= 0) {
      detachVirtualController(device_);
      device_ = -1;
    }
  }

  // Whether pad commands still have a pad to press. The handle is the truth
  // here, not the device index, because only an opened handle can carry one.
  bool attached() const { return joystick_ != nullptr; }
  SDL_Joystick* joystick() const { return joystick_; }

 private:
  SDL_Joystick* joystick_ = nullptr;
  int device_ = -1;
};

// Owns one stdio stream the harness asked for: the per-tick input log, the
// checkpoint observations, or the frame-time record. The caller names the
// path; a missing evidence file costs the run's record, never the session, so
// open() leaves a null stream and the caller carries on exactly as before.
class EvidenceFile {
 public:
  EvidenceFile() = default;

  ~EvidenceFile() { close(); }

  EvidenceFile(const EvidenceFile&) = delete;
  EvidenceFile& operator=(const EvidenceFile&) = delete;

  // Opens `path` for writing, or does nothing when the option named no file
  // (`path` is null). Returns false only when a named path cannot open, so
  // the owner can print one warning for it.
  bool open(const char* path) {
    close();
    if (path == nullptr) {
      return true;
    }
    file_ = std::fopen(path, "w");
    return file_ != nullptr;
  }

  // The stream, or nullptr when the session runs without this file. Callers
  // test the pointer, as they always did.
  std::FILE* get() const { return file_; }

  void close() {
    if (file_ != nullptr) {
      std::fclose(file_);
      file_ = nullptr;
    }
  }

 private:
  std::FILE* file_ = nullptr;
};

// One session's developer automation. See the file comment for the boundary
// against src/main.cpp.
class DeveloperSession {
 public:
  // `sessionLog` is the log file main opened (SessionLog::file()), or null
  // while the session logs to stdout alone. The module writes its lines to
  // both sinks and never closes the file; its owner does.
  explicit DeveloperSession(std::FILE* sessionLog) : sessionLog_(sessionLog) {}

  // Releases everything the session asked for: the evidence streams close in
  // the reverse order they opened - the frame-time record first, the
  // observations, then the input log - and the virtual pad goes last, so a
  // script that detached it mid-session and the exit path both release it
  // exactly once. Nothing here logs, so main's log owner can outlive it.
  ~DeveloperSession() = default;

  DeveloperSession(const DeveloperSession&) = delete;
  DeveloperSession& operator=(const DeveloperSession&) = delete;

  // -------------------------------------------------------------------------
  // Input injection.
  // -------------------------------------------------------------------------

  // Attaches the developer virtual gamepad: the form the --virtual-controller
  // option and every `pad` script command need. Returns false when SDL
  // refused the device or its mapping, and the session runs without one.
  bool attachPad() { return pad_.attach(); }

  // Parses the script at `path` into this session's command list. `game` and
  // `cues` name the round the `restart` and `fixture` commands act on; the
  // detector must forget its snapshot after either, because both change the
  // round outside a simulation tick. Both may be null, which leaves the two
  // commands as logged no-ops. Returns false after reporting the first
  // malformed line, so a recorded session cannot run halfway by accident.
  bool loadScript(const char* path, Game* game, CueDetector* cues);

  bool hasScript() const { return loaded_; }

  // Runs the commands due before this frame's event poll. Safe with no
  // script loaded: nothing is due.
  void pumpScript(std::uint64_t tickCount);

  // -------------------------------------------------------------------------
  // Checkpoint capture.
  // -------------------------------------------------------------------------

  // Flushes the checkpoints a `check` or `shot` command marked this frame:
  // one BMP and one structured observation record per `check`, one BMP per
  // `shot`, in the order the script named them. Call after the frame's
  // simulation ticks and its render, so the record and the image describe the
  // state that was drawn - the same state the headless reference replay
  // records after its tick, which is what makes the two streams comparable.
  // A checkpoint without a screenshot directory records `shot=none` instead
  // of failing, because a rules-only session still deserves its records; an
  // image the budget refuses records `shot=none` the same way, so the record
  // stream never depends on the images (task T054).
  void captureCheckpoints(const Game& game, SDL_Renderer* renderer);

  // -------------------------------------------------------------------------
  // The evidence streams. Each open call matches one developer option; open
  // them in the order main always did, because a script the parser rejects
  // must leave no later stream's file behind.
  // -------------------------------------------------------------------------

  // --input-log FILE: one line per simulation tick, plus the session's
  // markers. The loop and startup write it through inputLog().
  void openInputLog(const char* path);

  // --observations FILE: one structured record per checkpoint command.
  void openObservations(const char* path);

  // --frame-times FILE: one microsecond frame duration per frame of a
  // real-time paced run. The loop writes it through frameTimes().
  void openFrameTimes(const char* path);

  // --screenshot-dir DIR: where checkpoint BMPs land.
  void setScreenshotDir(const char* dir);

  // --screenshot-budget BYTES: the cap on the checkpoint images this session
  // writes, 0 for none. Once the next image would pass the cap, that image
  // and every later one is skipped with one log line while the observation
  // records keep coming, so a long session cannot fill the host's disk with
  // evidence (task T054; papercut pc_01M6JJR4BAFHC9KZM6GQ1EKK04).
  void setScreenshotBudget(std::uint64_t bytes);

  // --fixture-dir DIR: where `fixture` script commands resolve.
  void setFixtureDir(const char* dir);

  std::FILE* inputLog() const { return inputLog_.get(); }
  std::FILE* frameTimes() const { return frameTimes_.get(); }

 private:
  // One scripted command, parsed once at load time.
  struct Command {
    enum class Kind : std::uint8_t {
      KeyDown,
      KeyUp,
      PadDown,
      PadUp,
      Wait,
      Stall,
      Detach,
      Restart,
      Fixture,
      Check,
      Shot,
      Quit,
      Echo
    };
    Kind kind;
    int value = 0;     // scancode, button, ticks, or milliseconds
    std::string text;  // echo marker, checkpoint, or fixture name
  };

  // The script key table: names for the mapped scancodes of the
  // specification's section 5 keyboard layout.
  struct KeyName {
    const char* name;
    SDL_Scancode scancode;
  };
  static const KeyName* findKey(const char* name);

  static void pushKeyEvent(SDL_Scancode scancode, bool pressed);

  // One line to stdout (logf) or stderr (logErrorf) and the session log, the
  // same dual sink main uses, so an unattended run keeps every scripted line
  // in log.txt beside the session's own.
  void logf(const char* format, ...) const;
  void logErrorf(const char* format, ...) const;

  void marker(const char* text);
  void run(const Command& command);
  bool parseLine(const std::string& raw);
  bool reportSyntax(const std::string& raw, const char* why);
  bool saveScreenshot(SDL_Renderer* renderer, const std::string& path);

  // The log file main opened; never closed here.
  std::FILE* sessionLog_ = nullptr;

  // Declaration order is the release order of ~DeveloperSession, reversed:
  // the frame-time record closes first, then the observations, then the input
  // log, and the virtual pad releases last - the order the exit path always
  // used when these were four owners in main.
  VirtualController pad_;
  EvidenceFile inputLog_;
  EvidenceFile observations_;
  EvidenceFile frameTimes_;
  const char* screenshotDir_ = nullptr;  // null: shots log as unavailable
  // The image budget of T054 and how many bytes the session wrote so far.
  std::uint64_t screenshotBudget_ = kDefaultScreenshotBudget;
  std::uint64_t screenshotBytes_ = 0;
  const char* fixtureDir_ = nullptr;     // null: `fixture` cannot resolve
  Game* game_ = nullptr;                 // restart and fixture act on it
  CueDetector* cues_ = nullptr;          // a restart resets the snapshot
  // Checkpoints the loop flushes after the current frame renders.
  std::vector<std::string> pendingShots_;
  std::vector<std::string> pendingChecks_;

  // The parsed command list and how far it has run.
  std::vector<Command> commands_;
  size_t index_ = 0;
  bool waiting_ = false;
  std::uint64_t waitUntil_ = 0;
  bool loaded_ = false;
};

}  // namespace stackfall
