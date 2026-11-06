// Developer automation of one session (task T048, on the harness of task
// T023, docs/AGENT_TESTING.md): the script driver, the virtual gamepad, the
// checkpoint capture, and the evidence streams behind the developer options
// --input-script, --virtual-controller, --input-log, --observations,
// --screenshot-dir, --screenshot-budget, --fixture-dir, and --frame-times.
//
// Everything here used to live in src/main.cpp, which the refactoring review
// of notes/refactor-review.md held at 1,810 lines. The move is the whole
// change: no command, no format string, no ordering moved with it, so a
// recorded session keeps the bytes it always had. See the header for the
// boundary the review set - timing, simulation, rendering, and startup stay
// in the loop - and for the script language table.
//
// The headless reference replay of tests/harness keeps its own parser and its
// own mapping tables on purpose; only the bounded count rule of
// stackfall/script.cpp is shared (see the header).

#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#include "stackfall/developer_session.hpp"

#include "stackfall/audio_sdl.hpp"
#include "stackfall/fixture.hpp"
#include "stackfall/input_sdl.hpp"
#include "stackfall/observe.hpp"
#include "stackfall/pieces.hpp"
#include "stackfall/renderer_sdl.hpp"
#include "stackfall/script.hpp"

namespace stackfall {

namespace {

// Writes one line to `stream` and the session log, the same rule
// src/main.cpp applies to its own lines. A va_list cannot be walked twice,
// so the file reads a copy.
void logToBoth(std::FILE* stream, std::FILE* sessionLog, const char* format,
               std::va_list args) {
  std::va_list copy;
  va_copy(copy, args);
  std::vfprintf(stream, format, args);
  if (sessionLog != nullptr) {
    std::vfprintf(sessionLog, format, copy);
    std::fflush(sessionLog);  // a terminated run has still written it all
  }
  va_end(copy);
}

// ---------------------------------------------------------------------------
// The checkpoint BMP writer (task T054). SDL_SaveBMP wrote one 24-bit BMP
// per checkpoint, 1.5 MB at the 960x544 canvas, and nothing capped how many
// a session writes: the 313-piece play session of papercut
// pc_01M6JJR4BAFHC9KZM6GQ1EKK04 left 314 of them, about 490 MB, on a host
// with 1.2 GB free. The writer below emits the palettized form these frames
// were made for: the procedural renderer paints a few dozen colors per
// frame, so one 8-bit palette plus BI_RLE8 rows hold a whole checkpoint in
// roughly 30 to 80 KB. Two escapes keep every image lossless: a frame with
// more than 256 colors takes the 24-bit uncompressed form SDL_SaveBMP used
// to write, and a frame whose rows RLE cannot shrink takes the 8-bit
// uncompressed form. tests/harness/check_harness.py decodes all of these
// with no image library, and PIL reads RLE8 BMPs natively, so the visual
// reviews of docs/AGENT_TESTING.md keep working.
// ---------------------------------------------------------------------------

// Appends one little-endian word of the BMP headers, spelled out byte by
// byte so the file never depends on the host's byte order.
void appendLe16(std::vector<std::uint8_t>* out, std::uint16_t value) {
  out->push_back(static_cast<std::uint8_t>(value & 0xFF));
  out->push_back(static_cast<std::uint8_t>(value >> 8));
}

void appendLe32(std::vector<std::uint8_t>* out, std::uint32_t value) {
  appendLe16(out, static_cast<std::uint16_t>(value & 0xFFFF));
  appendLe16(out, static_cast<std::uint16_t>(value >> 16));
}

// Appends the 14-byte file header and the 40-byte BITMAPINFOHEADER, with the
// palette following in the same buffer. `height` is positive, so the rows
// run bottom-up, and `compression` is 1 for BI_RLE8 and 0 for BI_RGB.
void appendBmpHeader(std::vector<std::uint8_t>* out, int width, int height,
                     int bitsPerPixel, std::uint32_t compression,
                     std::uint32_t imageSize, size_t paletteEntries) {
  const size_t pixelOffset = 14 + 40 + paletteEntries * 4;
  out->reserve(pixelOffset + imageSize);
  out->push_back('B');
  out->push_back('M');
  appendLe32(out, static_cast<std::uint32_t>(pixelOffset + imageSize));
  appendLe32(out, 0);  // reserved
  appendLe32(out, static_cast<std::uint32_t>(pixelOffset));
  appendLe32(out, 40);  // the BITMAPINFOHEADER size itself
  appendLe32(out, static_cast<std::uint32_t>(width));
  appendLe32(out, static_cast<std::uint32_t>(height));
  appendLe16(out, 1);  // one color plane
  appendLe16(out, static_cast<std::uint16_t>(bitsPerPixel));
  appendLe32(out, compression);
  appendLe32(out, imageSize);
  appendLe32(out, 0);  // pixels per meter, unset
  appendLe32(out, 0);
  appendLe32(out, static_cast<std::uint32_t>(paletteEntries));
  appendLe32(out, 0);  // every palette entry matters
}

// Appends the BI_RLE8 encoding of one row of palette indexes. Runs of three
// or more equal indexes travel as two-byte pairs; shorter groups collect
// into one absolute run, padded to a 16-bit boundary as the format requires.
// An absolute run must name three or more bytes - the two sizes below that
// are the end-of-bitmap and the delta escape - so a flush of one or two
// bytes travels as plain encoded pairs instead.
void appendRleRow(std::vector<std::uint8_t>* out, const std::uint8_t* row,
                  int width, std::vector<std::uint8_t>* literal) {
  auto flushLiteral = [out, literal]() {
    if (literal->empty()) {
      return;
    }
    if (literal->size() < 3) {
      for (const std::uint8_t index : *literal) {
        out->push_back(1);
        out->push_back(index);
      }
    } else {
      out->push_back(0);
      out->push_back(static_cast<std::uint8_t>(literal->size()));
      out->insert(out->end(), literal->begin(), literal->end());
      if (literal->size() % 2 != 0) {
        out->push_back(0);  // the absolute run's word-alignment pad
      }
    }
    literal->clear();
  };
  int x = 0;
  while (x < width) {
    int run = 1;
    while (run < 255 && x + run < width && row[x + run] == row[x]) {
      ++run;
    }
    if (run >= 3) {
      flushLiteral();
      out->push_back(static_cast<std::uint8_t>(run));
      out->push_back(row[x]);
    } else {
      const int room = 255 - static_cast<int>(literal->size());
      if (run > room) {
        flushLiteral();
      }
      literal->insert(literal->end(), row + x, row + x + run);
    }
    x += run;
  }
  flushLiteral();
}

// One whole-file write of the encoded image; the caller owns the error line.
bool writeBytes(const std::string& path,
                const std::vector<std::uint8_t>& bytes) {
  errno = 0;
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    return false;
  }
  const bool ok = bytes.empty() ||
                  std::fwrite(bytes.data(), 1, bytes.size(), file) ==
                      bytes.size();
  std::fclose(file);
  return ok;
}

// Encodes the RGB888 frame readOutputPixels filled into the bytes of one BMP
// file, palettized whenever the frame's own colors allow it. Returns false
// only for a surface this writer does not understand, so a failed screenshot
// stays a real failure.
bool encodeScreenshotBmp(const SDL_Surface& surface,
                         std::vector<std::uint8_t>* out) {
  if (surface.w <= 0 || surface.h <= 0 || surface.format == nullptr ||
      SDL_BYTESPERPIXEL(surface.format->format) != 4 ||
      surface.pixels == nullptr) {
    return false;
  }
  const int width = surface.w;
  const int height = surface.h;
  const size_t pixelCount = static_cast<size_t>(width) * height;
  const auto* rows = static_cast<const std::uint8_t*>(surface.pixels);

  // The frame's palette and each pixel's index into it. A pixel of the color
  // the row already carries skips the map, which is nearly every pixel of
  // the large flat areas the procedural renderer draws.
  std::vector<std::uint32_t> palette;
  std::unordered_map<std::uint32_t, int> indexOf;
  std::vector<std::uint8_t> indexes(pixelCount, 0);
  bool tooManyColors = false;
  std::uint32_t lastColor = 0;
  int lastIndex = 0;
  bool hasLast = false;
  for (int y = 0; y < height && !tooManyColors; ++y) {
    const std::uint8_t* row = rows + static_cast<size_t>(y) * surface.pitch;
    std::uint8_t* indexRow = indexes.data() + static_cast<size_t>(y) * width;
    for (int x = 0; x < width; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel, row + x * 4, 4);
      std::uint8_t rgb[3] = {0, 0, 0};
      SDL_GetRGB(pixel, surface.format, &rgb[0], &rgb[1], &rgb[2]);
      const std::uint32_t color = static_cast<std::uint32_t>(rgb[0]) << 16 |
                                  static_cast<std::uint32_t>(rgb[1]) << 8 |
                                  rgb[2];
      if (!hasLast || color != lastColor) {
        const auto found = indexOf.find(color);
        if (found != indexOf.end()) {
          lastIndex = found->second;
        } else {
          if (palette.size() >= 256) {
            tooManyColors = true;  // the frame outruns an 8-bit palette
            break;
          }
          lastIndex = static_cast<int>(palette.size());
          indexOf.emplace(color, lastIndex);
          palette.push_back(color);
        }
        lastColor = color;
        hasLast = true;
      }
      indexRow[x] = static_cast<std::uint8_t>(lastIndex);
    }
  }

  if (tooManyColors) {
    // The 24-bit uncompressed escape: the exact form SDL_SaveBMP wrote, so a
    // pathological frame loses nothing. Rows run bottom-up, each padded to a
    // four-byte stride.
    const size_t stride = (static_cast<size_t>(width) * 3 + 3) / 4 * 4;
    appendBmpHeader(out, width, height, 24, 0,
                    static_cast<std::uint32_t>(stride * height), 0);
    for (int y = height - 1; y >= 0; --y) {
      const std::uint8_t* row = rows + static_cast<size_t>(y) * surface.pitch;
      for (int x = 0; x < width; ++x) {
        std::uint32_t pixel = 0;
        std::memcpy(&pixel, row + x * 4, 4);
        std::uint8_t rgb[3] = {0, 0, 0};
        SDL_GetRGB(pixel, surface.format, &rgb[0], &rgb[1], &rgb[2]);
        out->push_back(rgb[2]);
        out->push_back(rgb[1]);
        out->push_back(rgb[0]);
      }
      for (size_t pad = static_cast<size_t>(width) * 3; pad < stride; ++pad) {
        out->push_back(0);
      }
    }
    return true;
  }

  // The 8-bit body, RLE first: bottom-up rows, each closed by the end-of-line
  // escape, the whole image closed by the end-of-bitmap escape.
  std::vector<std::uint8_t> body;
  std::vector<std::uint8_t> literal;
  const size_t rawStride = (static_cast<size_t>(width) + 3) / 4 * 4;
  body.reserve(pixelCount / 4);
  for (int y = height - 1; y >= 0; --y) {
    appendRleRow(&body, indexes.data() + static_cast<size_t>(y) * width,
                 width, &literal);
    body.push_back(0);
    body.push_back(0);  // end of line
  }
  body.push_back(0);
  body.push_back(1);  // end of bitmap
  std::uint32_t compression = 1;  // BI_RLE8
  if (body.size() >= rawStride * height) {
    // RLE would grow the data: the uncompressed 8-bit escape, the same rows
    // padded to the four-byte stride of the format.
    compression = 0;
    body.clear();
    body.reserve(rawStride * height);
    for (int y = height - 1; y >= 0; --y) {
      const std::uint8_t* indexRow =
          indexes.data() + static_cast<size_t>(y) * width;
      body.insert(body.end(), indexRow, indexRow + width);
      for (size_t pad = static_cast<size_t>(width); pad < rawStride; ++pad) {
        body.push_back(0);
      }
    }
  }

  appendBmpHeader(out, width, height, 8, compression,
                  static_cast<std::uint32_t>(body.size()), palette.size());
  for (const std::uint32_t color : palette) {
    out->push_back(static_cast<std::uint8_t>(color & 0xFF));         // blue
    out->push_back(static_cast<std::uint8_t>(color >> 8 & 0xFF));    // green
    out->push_back(static_cast<std::uint8_t>(color >> 16 & 0xFF));   // red
    out->push_back(0);
  }
  out->insert(out->end(), body.begin(), body.end());
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Logging and the evidence streams.
// ---------------------------------------------------------------------------

void DeveloperSession::logf(const char* format, ...) const {
  std::va_list args;
  va_start(args, format);
  logToBoth(stdout, sessionLog_, format, args);
  va_end(args);
}

void DeveloperSession::logErrorf(const char* format, ...) const {
  std::va_list args;
  va_start(args, format);
  logToBoth(stderr, sessionLog_, format, args);
  va_end(args);
}

void DeveloperSession::openInputLog(const char* path) {
  inputLog_.open(path);
  if (path != nullptr && inputLog_.get() == nullptr) {
    logErrorf("Warning: cannot open %s \"%s\"\n", "input log", path);
  }
}

void DeveloperSession::openObservations(const char* path) {
  observations_.open(path);
  if (path != nullptr && observations_.get() == nullptr) {
    logErrorf("Warning: cannot open %s \"%s\"\n", "observations", path);
  }
}

void DeveloperSession::openFrameTimes(const char* path) {
  frameTimes_.open(path);
  if (path != nullptr && frameTimes_.get() == nullptr) {
    logErrorf("Warning: cannot open %s \"%s\"\n", "frame-time record", path);
  }
}

void DeveloperSession::setScreenshotDir(const char* dir) {
  screenshotDir_ = dir;
  if (screenshotDir_ != nullptr) {
    logf("Checkpoint screenshots into %s\n", screenshotDir_);
  }
}

void DeveloperSession::setScreenshotBudget(std::uint64_t bytes) {
  screenshotBudget_ = bytes;
  if (bytes == 0) {
    logf("Checkpoint screenshot budget unlimited\n");
  } else {
    logf("Checkpoint screenshot budget %llu bytes\n",
         static_cast<unsigned long long>(bytes));
  }
}

void DeveloperSession::setFixtureDir(const char* dir) { fixtureDir_ = dir; }

// ---------------------------------------------------------------------------
// The script driver.
// ---------------------------------------------------------------------------

bool DeveloperSession::loadScript(const char* path, Game* game,
                                  CueDetector* cues) {
  game_ = game;
  cues_ = cues;
  std::FILE* file = std::fopen(path, "r");
  if (file == nullptr) {
    logErrorf("Cannot open input script \"%s\"\n", path);
    return false;
  }
  std::string line;
  char buffer[256];
  bool ok = true;
  while (std::fgets(buffer, sizeof(buffer), file) != nullptr) {
    line = buffer;
    if (!parseLine(line) && ok) {
      ok = false;  // report the first bad line, keep reading for the report
    }
  }
  std::fclose(file);
  loaded_ = ok;
  return ok;
}

void DeveloperSession::pumpScript(std::uint64_t tickCount) {
  while (index_ < commands_.size()) {
    const Command& command = commands_[index_];
    if (command.kind == Command::Kind::Wait) {
      if (!waiting_) {
        waitUntil_ = tickCount + static_cast<std::uint64_t>(command.value);
        waiting_ = true;
      }
      if (tickCount < waitUntil_) {
        return;
      }
      waiting_ = false;
      ++index_;
      continue;
    }
    run(command);
    ++index_;
  }
}

const DeveloperSession::KeyName* DeveloperSession::findKey(const char* name) {
  // One table for every rebindable key of the customization screen (task
  // T066): the canonical names of stackfall/bindings.hpp resolve through the
  // same vocabulary the controls file writes, so a script can name any key a
  // capture can. M stays in its own entry because it is the fixed sound
  // shortcut the platform layer reads, not a control; Q stays out for the
  // same reason, as it quits.
  static const KeyName soundToggle = {"m", SDL_SCANCODE_M};
  static const KeyName unknown = {nullptr, SDL_SCANCODE_UNKNOWN};
  if (std::strcmp(soundToggle.name, name) == 0) {
    return &soundToggle;
  }
  const int control = keyboardControlFromName(name);
  if (control != kNoControl) {
    static KeyName resolved[kKeyboardControlCount];
    const SDL_Scancode scancode = scancodeOfKeyboardControl(control);
    resolved[control].name = keyboardControlName(control);
    resolved[control].scancode = scancode;
    return &resolved[control];
  }
  return &unknown;
}

void DeveloperSession::pushKeyEvent(SDL_Scancode scancode, bool pressed) {
  SDL_Event event;
  SDL_zero(event);
  event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
  event.key.repeat = 0;
  event.key.keysym.scancode = scancode;
  event.key.keysym.sym = SDL_GetKeyFromScancode(scancode);
  SDL_PushEvent(&event);
}

void DeveloperSession::marker(const char* text) {
  logf("script: %s\n", text);
  if (inputLog_.get() != nullptr) {
    std::fprintf(inputLog_.get(), "script %s\n", text);
    std::fflush(inputLog_.get());
  }
}

void DeveloperSession::run(const Command& command) {
  switch (command.kind) {
    case Command::Kind::KeyDown:
    case Command::Kind::KeyUp: {
      const bool pressed = command.kind == Command::Kind::KeyDown;
      pushKeyEvent(static_cast<SDL_Scancode>(command.value), pressed);
      break;
    }
    case Command::Kind::PadDown:
    case Command::Kind::PadUp: {
      if (!pad_.attached()) {
        marker("pad command without a virtual controller; ignored");
        break;
      }
      const bool pressed = command.kind == Command::Kind::PadDown;
      SDL_JoystickSetVirtualButton(pad_.joystick(), command.value,
                                   pressed ? 1 : 0);
      // Virtual buttons set state; SDL emits the event only on the next
      // update. Apply it now, so a tap (down and up in one frame) still
      // produces both events, exactly like a quick real press.
      SDL_JoystickUpdate();
      break;
    }
    case Command::Kind::Stick: {
      if (!pad_.attached()) {
        marker("stick command without a virtual controller; ignored");
        break;
      }
      SDL_JoystickSetVirtualAxis(pad_.joystick(), command.value,
                                 static_cast<Sint16>(command.axisValue));
      // The same update rule as a virtual button: the event belongs to this
      // frame, not to whichever frame SDL next polls on its own.
      SDL_JoystickUpdate();
      break;
    }
    case Command::Kind::Wait:
      break;  // handled in pumpScript()
    case Command::Kind::Stall:
      logf("script: stalling %d ms for a catch-up frame\n", command.value);
      SDL_Delay(command.value);
      break;
    case Command::Kind::Detach:
      // The owner releases the pad, so the exit path that follows cannot
      // double close it: reset() forgets both handles on the spot.
      pad_.reset();
      break;
    case Command::Kind::Attach:
      // The replug half of task T066's disconnect coverage: the same virtual
      // pad comes back with a fresh SDL instance id, so discovery opens it as
      // a new source and the binding tables apply to it unchanged - the
      // tables name a device kind, never a handle.
      pad_.attach();
      break;
    case Command::Kind::Restart:
      if (game_ != nullptr) {
        game_->start(game_->seed());
        // The round changed outside a simulation tick, so the cue detector
        // must forget its snapshot: the emptied board is a restart, not a
        // clear.
        if (cues_ != nullptr) {
          cues_->reset();
        }
        marker("restart");
      }
      break;
    case Command::Kind::Fixture: {
      if (game_ == nullptr) {
        break;
      }
      if (fixtureDir_ == nullptr) {
        marker("fixture command without --fixture-dir; ignored");
        break;
      }
      const std::string path =
          std::string(fixtureDir_) + "/" + command.text + ".board.txt";
      BoardFixture fixture;
      std::string error;
      if (!loadBoardFixture(path, &fixture, &error)) {
        marker(("fixture " + command.text + " unavailable: " + error).c_str());
        break;
      }
      if (!game_->applyBoardFixture(fixture)) {
        marker(("fixture " + command.text + " rejected: the round is not "
                "playing or the board is inconsistent").c_str());
        break;
      }
      if (cues_ != nullptr) {
        // A replaced board is not a line clear, exactly like a restart.
        cues_->reset();
      }
      const std::string activeText =
          fixture.hasActive ? std::string(1, pieceLetter(fixture.active))
                            : std::string("kept");
      // The fixture line is evidence of a labeled board state, not a session
      // event: stdout and the input log below carry it beside the tick
      // stream, and log.txt stays a log.
      std::printf(
          "Fixture \"%s\" applied from %s (provenance: %s; active %s)\n",
          fixture.name.c_str(), path.c_str(),
          fixture.provenance.empty() ? "unlabeled"
                                     : fixture.provenance.c_str(),
          activeText.c_str());
      if (inputLog_.get() != nullptr) {
        std::fprintf(inputLog_.get(), "fixture %s path=%s\n",
                     command.text.c_str(), path.c_str());
        std::fflush(inputLog_.get());
      }
      break;
    }
    case Command::Kind::Check:
      pendingChecks_.push_back(command.text);
      break;
    case Command::Kind::Shot:
      pendingShots_.push_back(command.text);
      break;
    case Command::Kind::Quit: {
      // Through the queue, like a window close: the loop leaves through
      // its ordinary exit path.
      SDL_Event event;
      SDL_zero(event);
      event.type = SDL_QUIT;
      SDL_PushEvent(&event);
      marker("quit");
      break;
    }
    case Command::Kind::Echo:
      marker(command.text.c_str());
      break;
  }
}

// Parses one script line into commands_. Returns false and reports the
// line on a syntax error, because a recorded session must not run halfway
// by accident.
bool DeveloperSession::parseLine(const std::string& raw) {
  const size_t hash = raw.find('#');
  std::string line = raw.substr(0, hash);
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r' ||
                           line.back() == ' ' || line.back() == '\t')) {
    line.pop_back();
  }
  if (line.empty()) {
    return true;
  }
  const size_t verbEnd = line.find_first_of(" \t");
  const std::string verb = line.substr(0, verbEnd);
  const std::string rest =
      verbEnd == std::string::npos ? "" : line.substr(verbEnd + 1);
  const size_t nameEnd = rest.find_first_of(" \t");
  const std::string name =
      rest.empty() || nameEnd == 0 ? "" : rest.substr(0, nameEnd);
  const std::string tail =
      nameEnd == std::string::npos ? "" : rest.substr(nameEnd + 1);
  const size_t tailEnd = tail.find_first_of(" \t");
  const std::string state = tail.substr(0, tailEnd);

  Command command;
  if (verb == "key" || verb == "pad") {
    // "key <name> down|up" and "pad <name> down|up".
    const bool down = state == "down";
    const bool up = state == "up";
    if (!down && !up) {
      return reportSyntax(raw, "key/pad needs down or up");
    }
    if (verb == "key") {
      const KeyName* key = findKey(name.c_str());
      if (key == nullptr || key->name == nullptr) {
        return reportSyntax(raw, "unknown key name");
      }
      command.kind = down ? Command::Kind::KeyDown : Command::Kind::KeyUp;
      command.value = key->scancode;
    } else {
      const int button = virtualPadButtonFromName(name.c_str());
      if (button < 0) {
        return reportSyntax(raw, "unknown pad button name");
      }
      command.kind = down ? Command::Kind::PadDown : Command::Kind::PadUp;
      command.value = button;
    }
  } else if (verb == "stick") {
    // "stick <x|y> <value>", the analog thumbstick of task T86: the value is
    // the reading the virtual pad reports, so a script can name the deadzone
    // boundary and the far ends of an axis exactly as SDL reports them.
    const int axis = virtualPadAxisFromName(name.c_str());
    if (axis < 0) {
      return reportSyntax(raw, "unknown stick axis name");
    }
    if (!parseScriptAxisValue(state.c_str(), &command.axisValue)) {
      return reportSyntax(raw, "stick needs a value from -32768 to 32767");
    }
    command.kind = Command::Kind::Stick;
    command.value = axis;
  } else if (verb == "wait" || verb == "stall") {
    // The count runs through the shared bounded rule of the core, never a
    // narrowing cast: a fuzzed count above the 32-bit range once became a
    // negative SDL_Delay argument that hung the session (T025 fuzz pass).
    if (!parseScriptCount(name.c_str(), &command.value)) {
      return reportSyntax(raw, "wait/stall needs a number from 0 to "
                               "3600000");
    }
    command.kind =
        verb == "wait" ? Command::Kind::Wait : Command::Kind::Stall;
  } else if (verb == "detach") {
    command.kind = Command::Kind::Detach;
  } else if (verb == "attach") {
    command.kind = Command::Kind::Attach;
  } else if (verb == "restart") {
    command.kind = Command::Kind::Restart;
  } else if (verb == "quit") {
    command.kind = Command::Kind::Quit;
  } else if (verb == "fixture" || verb == "check" || verb == "shot") {
    if (name.empty() || nameEnd != std::string::npos) {
      return reportSyntax(raw, "fixture/check/shot needs one name");
    }
    command.text = name;
    command.kind = verb == "fixture" ? Command::Kind::Fixture
                    : verb == "check" ? Command::Kind::Check
                                      : Command::Kind::Shot;
  } else if (verb == "echo") {
    command.kind = Command::Kind::Echo;
    command.text = rest;
  } else {
    return reportSyntax(raw, "unknown command");
  }
  commands_.push_back(std::move(command));
  return true;
}

bool DeveloperSession::reportSyntax(const std::string& raw, const char* why) {
  // The line keeps its newline in the log file too, so one report cannot
  // run into the next entry.
  std::string line = raw;
  if (line.empty() || line.back() != '\n') {
    line += '\n';
  }
  logErrorf("Input script error (%s): %s", why, line.c_str());
  return false;
}

// ---------------------------------------------------------------------------
// Checkpoint capture (docs/AGENT_TESTING.md, T023). A checkpoint saves the
// frame the loop just rendered, exactly as presented: readOutputPixels copies
// the whole renderer output - the letterbox bars of a non-native window
// included (task T032) - into a fresh surface and the BMP writer above
// encodes it, both from core SDL2 and this module alone, so no image library
// and no asset path is involved. The screenshot names the checkpoint, never
// a frame number, so evidence stays comparable across runs. The budget of
// task T054 bounds the bytes a whole session may write: the next image that
// would pass the budget is skipped with one line, and the checkpoint record
// answers `shot=none` exactly as it does when a save fails, so the
// observation stream never depends on the images.
// ---------------------------------------------------------------------------

bool DeveloperSession::saveScreenshot(SDL_Renderer* sdlRenderer,
                                      const std::string& path) {
  int width = 0;
  int height = 0;
  if (SDL_GetRendererOutputSize(sdlRenderer, &width, &height) != 0) {
    logf("Screenshot %s failed: %s\n", path.c_str(), SDL_GetError());
    return false;
  }
  SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
      0, width, height, 32, SDL_PIXELFORMAT_RGB888);
  if (surface == nullptr) {
    logf("Screenshot %s failed: %s\n", path.c_str(), SDL_GetError());
    return false;
  }
  bool ok = true;
  if (!readOutputPixels(sdlRenderer, surface)) {
    logf("Screenshot %s failed to read pixels: %s\n", path.c_str(),
         SDL_GetError());
    ok = false;
  } else {
    std::vector<std::uint8_t> bytes;
    if (!encodeScreenshotBmp(*surface, &bytes)) {
      logf("Screenshot %s failed to encode: not a 32-bit frame\n",
           path.c_str());
      ok = false;
    } else if (screenshotBudget_ != 0 &&
               screenshotBytes_ + bytes.size() > screenshotBudget_) {
      logf(
          "Screenshot %s skipped: %llu bytes would pass the %llu-byte "
          "budget (%llu bytes written)\n",
          path.c_str(), static_cast<unsigned long long>(bytes.size()),
          static_cast<unsigned long long>(screenshotBudget_),
          static_cast<unsigned long long>(screenshotBytes_));
      ok = false;
    } else if (!writeBytes(path, bytes)) {
      logf("Screenshot %s failed to save: %s\n", path.c_str(),
           std::strerror(errno));
      ok = false;
    } else {
      screenshotBytes_ += bytes.size();
    }
  }
  SDL_FreeSurface(surface);
  return ok;
}

void DeveloperSession::captureCheckpoints(const Game& game,
                                          SDL_Renderer* sdlRenderer) {
  for (const std::string& name : pendingShots_) {
    if (screenshotDir_ == nullptr) {
      logf("Checkpoint %s has no --screenshot-dir; no image\n", name.c_str());
      continue;
    }
    const std::string path =
        std::string(screenshotDir_) + "/" + name + ".bmp";
    if (saveScreenshot(sdlRenderer, path)) {
      logf("Checkpoint shot %s -> %s\n", name.c_str(), path.c_str());
    }
  }
  pendingShots_.clear();
  for (const std::string& name : pendingChecks_) {
    std::string shotPath = "none";
    if (screenshotDir_ != nullptr) {
      const std::string path =
          std::string(screenshotDir_) + "/" + name + ".bmp";
      if (saveScreenshot(sdlRenderer, path)) {
        shotPath = path;
        logf("Checkpoint %s -> %s\n", name.c_str(), path.c_str());
      }
    } else {
      logf("Checkpoint %s has no --screenshot-dir; no image\n", name.c_str());
    }
    const std::string record = observationLine(name, game) + " shot=" + shotPath;
    // The record line is structured data, not a session event: stdout and the
    // observations file carry it, and log.txt stays a log.
    std::printf("%s\n", record.c_str());
    if (observations_.get() != nullptr) {
      std::fprintf(observations_.get(), "%s\n", record.c_str());
      std::fflush(observations_.get());
    }
  }
  pendingChecks_.clear();
}

}  // namespace stackfall
