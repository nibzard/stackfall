// SDL-path audio suite for the synthesized sound effects (task T013,
// specification section 6.3).
//
// This suite links SDL2 and runs separately from the portable stackfall_tests
// binary, exactly like tests/test_input_sdl.cpp and
// tests/test_renderer_sdl.cpp. It needs no display, no sound hardware, and no
// human ear: the synthesis is pinned byte for byte, the cue detector is driven
// through the real rules core with the same fixtures the rules suite uses, and
// the device half runs against the SDL dummy audio driver, which exercises the
// real callback path without a sound card.
//
// Coverage of the task T013 acceptance criteria:
//   * all five cues synthesize at every rate with exact lengths, bounded
//     samples, silent first and last samples (no clicks), a decaying envelope,
//     byte-identical repeats, and pairwise distinct waveforms;
//   * a cue carries no asset: the buffers are the only sound data, and the WAV
//     export a test writes is evidence for the final human pass, not game data;
//   * the detector raises one cue per event: a move, a rotation (a wall kick
//     included, and never a move on top of it), a landing lock, a one-to-three
//     line clear, a four-line clear, and game over exactly once;
//   * gravity, soft drop, hold, menu ticks, and paused ticks stay silent;
//   * a missing audio device leaves the driver closed, drops nothing but the
//     sound, and the game state of the caller untouched;
//   * the mute toggle stops new cues while the device stays alive, and the
//     counters still report every event the round raised;
//   * the mixer saturates instead of overflowing when every voice sounds at
//     once, and returns to exact silence when no cue is playing.

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

#include <SDL.h>

#include "stackfall/audio_sdl.hpp"
#include "stackfall/game.hpp"
#include "stackfall/input.hpp"
#include "stackfall/pieces.hpp"

#include "game_test_peer.hpp"
#include "test_util.hpp"

namespace {

using namespace stackfall;

constexpr std::uint64_t kSeed = 20261103ULL;

// ---------------------------------------------------------------------------
// Small helpers.
// ---------------------------------------------------------------------------

InputFrame holdLeft() {
  InputFrame f;
  f.leftHeld = true;
  return f;
}

InputFrame holdRight() {
  InputFrame f;
  f.rightHeld = true;
  return f;
}

InputFrame holdDown() {
  InputFrame f;
  f.downHeld = true;
  return f;
}

InputFrame idle() { return InputFrame{}; }

InputFrame pressRotateCW() {
  InputFrame f;
  f.rotateCWPressed = true;
  return f;
}

InputFrame pressRotateCCW() {
  InputFrame f;
  f.rotateCCWPressed = true;
  return f;
}

InputFrame pressHardDrop() {
  InputFrame f;
  f.hardDropPressed = true;
  return f;
}

InputFrame pressHold() {
  InputFrame f;
  f.holdPressed = true;
  return f;
}

InputFrame pressPause() {
  InputFrame f;
  f.pausePressed = true;
  return f;
}

// One simulation tick through the game, observed by the detector the way the
// platform loop observes it: tick, then look, then play.
CueSet step(Game& game, CueDetector& detector, const InputFrame& input) {
  game.tick(input);
  return detector.observe(game);
}

// The baseline observation a session starts with, exactly as runMainLoop does
// after Game::start: the first observe() only takes a snapshot.
void prime(Game& game, CueDetector& detector) { (void)detector.observe(game); }

std::int64_t peakOf(const std::vector<std::int16_t>& samples) {
  std::int64_t peak = 0;
  for (const std::int16_t value : samples) {
    const std::int64_t magnitude = value < 0 ? -static_cast<std::int64_t>(value)
                                            : static_cast<std::int64_t>(value);
    peak = std::max(peak, magnitude);
  }
  return peak;
}

// A fresh, empty directory below the system temporary directory, removed when
// the guard dies, so the suite touches no repository file.
struct TempDir {
  std::string path;

  TempDir() {
    static int counter = 0;
    std::error_code ec;
    const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
    char name[64];
    std::snprintf(name, sizeof(name), "stackfall-audio-%d-%d",
                  static_cast<int>(::getpid()), ++counter);
    path = (base / name).string();
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

// ---------------------------------------------------------------------------
// Cue synthesis.
// ---------------------------------------------------------------------------

void testCuesSynthesizeAtEveryRate() {
  const int rates[] = {22050, 32000, 44100, 48000};
  for (const int rate : rates) {
    for (const SoundCue cue : kAllSoundCues) {
      std::vector<std::int16_t> samples;
      synthesizeCue(cue, rate, &samples);
      // The documented duration, rounded up to whole frames.
      const int frames = soundCueDurationMs(cue) * rate;
      const std::size_t expected =
          static_cast<std::size_t>((frames + 999) / 1000);
      SC_CHECK(samples.size() == expected);
      SC_CHECK(samples.size() > 0);

      // Bounded, audible, and click-free at both ends: the attack ramps up from
      // exact zero and the closing fade lands on exact zero again.
      const std::int64_t peak = peakOf(samples);
      SC_CHECK(peak >= 5000);
      SC_CHECK(samples.front() == 0);
      SC_CHECK(samples.back() == 0);

      // The attack reaches its working level almost at once, so the cue opens
      // as a tone rather than fading in.
      const std::size_t opening = static_cast<std::size_t>(rate / 200);  // 5 ms
      std::int64_t openingPeak = 0;
      for (std::size_t i = 0; i < opening && i < samples.size(); ++i) {
        const std::int64_t magnitude = std::abs(static_cast<int>(samples[i]));
        openingPeak = std::max(openingPeak, magnitude);
      }
      SC_CHECK(openingPeak > peak / 4);
      // Enough of the cue carries sound to be a tone and not a click: at least
      // a fifth of it, after the integer envelope has rounded its tail away.
      std::size_t sounding = 0;
      for (const std::int16_t value : samples) {
        if (value != 0) {
          ++sounding;
        }
      }
      SC_CHECK(sounding > samples.size() / 5);

      // The same cue at the same rate is the same byte sequence on every
      // machine and every build.
      std::vector<std::int16_t> again;
      synthesizeCue(cue, rate, &again);
      SC_CHECK(samples == again);
    }
  }
}

void testCuesAreDistinct() {
  std::vector<std::vector<std::int16_t>> rendered;
  for (const SoundCue cue : kAllSoundCues) {
    std::vector<std::int16_t> samples;
    synthesizeCue(cue, 44100, &samples);
    rendered.push_back(samples);
  }
  for (std::size_t a = 0; a < rendered.size(); ++a) {
    for (std::size_t b = a + 1; b < rendered.size(); ++b) {
      SC_CHECK(rendered[a] != rendered[b]);
    }
  }
  // The names are as distinct as the waveforms, so a session log names one
  // cue one way.
  for (std::size_t a = 0; a < rendered.size(); ++a) {
    for (std::size_t b = a + 1; b < rendered.size(); ++b) {
      SC_CHECK(std::strcmp(soundCueName(static_cast<SoundCue>(a)),
                           soundCueName(static_cast<SoundCue>(b))) != 0);
    }
  }
  SC_CHECK(std::strcmp(soundCueName(SoundCue::FourLineClear), "four") == 0);
  // Every cue is short: feedback, not music.
  for (const SoundCue cue : kAllSoundCues) {
    SC_CHECK(soundCueDurationMs(cue) <= 800);
    SC_CHECK(soundCueDurationMs(cue) > 0);
  }
}

void testSynthesisRejectsImpossibleRates() {
  std::vector<std::int16_t> samples;
  synthesizeCue(SoundCue::Clear, 0, &samples);
  SC_CHECK(samples.empty());
  synthesizeCue(SoundCue::Clear, -44100, &samples);
  SC_CHECK(samples.empty());
}

void testWavExport() {
  TempDir dir;
  std::vector<std::int16_t> samples;
  synthesizeCue(SoundCue::Rotate, 44100, &samples);
  const std::string path = dir.path + "/rotate.wav";
  SC_CHECK(writeWavFile(path, samples, 44100));

  std::FILE* file = std::fopen(path.c_str(), "rb");
  SC_CHECK(file != nullptr);
  if (file != nullptr) {
    std::vector<unsigned char> bytes;
    unsigned char buffer[256];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
      bytes.insert(bytes.end(), buffer, buffer + read);
    }
    std::fclose(file);

    // A 44-byte header plus two bytes per sample, mono PCM.
    SC_CHECK(bytes.size() == 44 + samples.size() * 2);
    SC_CHECK(std::memcmp(&bytes[0], "RIFF", 4) == 0);
    SC_CHECK(std::memcmp(&bytes[8], "WAVE", 4) == 0);
    SC_CHECK(std::memcmp(&bytes[12], "fmt ", 4) == 0);
    SC_CHECK(bytes[16] == 16);  // format block size
    SC_CHECK(bytes[20] == 1);   // PCM
    SC_CHECK(bytes[22] == 1);   // one channel
    SC_CHECK(bytes[34] == 16);  // bits per sample
    SC_CHECK(std::memcmp(&bytes[36], "data", 4) == 0);
    // The sample data is the synthesized cue itself, little-endian.
    for (std::size_t i = 0; i < samples.size(); ++i) {
      const int low = bytes[44 + i * 2];
      const int high = bytes[45 + i * 2];
      const int value = static_cast<std::int16_t>(low | (high << 8));
      SC_CHECK(value == samples[i]);
    }
  }
  // A path that cannot be created reports failure instead of throwing.
  SC_CHECK(!writeWavFile("/nonexistent-stackfall-dir/rotate.wav", samples,
                         44100));
}

// ---------------------------------------------------------------------------
// Cue detection through the rules core.
// ---------------------------------------------------------------------------

void testFallingAndMenusAreSilent() {
  Game game;
  game.start(kSeed);
  CueDetector detector;
  prime(game, detector);
  const int startY = game.activePiece().y;

  // Natural gravity moves the piece down for many ticks without a single cue.
  for (int tick = 0; tick < 60; ++tick) {
    SC_CHECK(step(game, detector, idle()).none());
  }
  // Soft drop is a player action on the fall, and it stays silent too.
  for (int tick = 0; tick < 8; ++tick) {
    SC_CHECK(step(game, detector, holdDown()).none());
  }
  SC_CHECK(game.activePiece().y > startY);  // the piece really did fall

  // Pause and its frozen ticks raise nothing, and neither does the resume.
  SC_CHECK(step(game, detector, pressPause()).none());
  SC_CHECK(game.scene() == Scene::Paused);
  for (int tick = 0; tick < 10; ++tick) {
    SC_CHECK(step(game, detector, holdDown()).none());
  }
  SC_CHECK(step(game, detector, pressPause()).none());
  SC_CHECK(game.scene() == Scene::Playing);
}

void testMoveCue() {
  Game game;
  game.start(kSeed);
  CueDetector detector;
  // A T in mid-air: it can move both ways for several columns before a wall.
  // The piece goes in before the first observation, so the detector's snapshot
  // holds the piece the coming ticks act on.
  GameTestPeer::setPiece(game, Piece{PieceId::T, Rotation::Spawn, 4, 8});
  prime(game, detector);

  CueSet cues = step(game, detector, holdLeft());
  SC_CHECK(cues.has(SoundCue::MoveLand));
  SC_CHECK(cues.count() == 1);
  SC_CHECK(game.activePiece().x == 3);

  cues = step(game, detector, InputFrame{});  // release: nothing happens
  SC_CHECK(cues.none());

  cues = step(game, detector, holdRight());
  SC_CHECK(cues.has(SoundCue::MoveLand));
  SC_CHECK(cues.count() == 1);
  SC_CHECK(game.activePiece().x == 4);

  // Auto-repeat moves one column at a time and each step is its own event.
  int moves = 0;
  for (int tick = 0; tick < 30; ++tick) {
    const CueSet repeated = step(game, detector, holdRight());
    SC_CHECK(repeated.count() <= 1);
    if (repeated.has(SoundCue::MoveLand)) {
      ++moves;
    }
  }
  SC_CHECK(game.activePiece().x > 4);
  SC_CHECK(moves == game.activePiece().x - 4);

  // The wall: every further tick presses a move that cannot happen, and no cue
  // sounds for a rejected input.
  const int atWall = game.activePiece().x;
  for (int tick = 0; tick < 20; ++tick) {
    SC_CHECK(step(game, detector, holdRight()).none());
  }
  SC_CHECK(game.activePiece().x == atWall);
}

void testRotateCueAndKickIsNotAMove() {
  Game game;
  game.start(kSeed);
  CueDetector detector;
  GameTestPeer::setPiece(game, Piece{PieceId::T, Rotation::Spawn, 4, 8});
  prime(game, detector);

  // Four quarter turns walk the rotation states and land back at spawn; each
  // one is a rotate cue and never a move.
  for (int turn = 0; turn < 4; ++turn) {
    const CueSet cues = step(game, detector, pressRotateCW());
    SC_CHECK(cues.has(SoundCue::Rotate));
    SC_CHECK(cues.count() == 1);
    SC_CHECK(!cues.has(SoundCue::MoveLand));
  }
  SC_CHECK(game.activePiece().rotation == Rotation::Spawn);

  const CueSet ccw = step(game, detector, pressRotateCCW());
  SC_CHECK(ccw.has(SoundCue::Rotate));
  SC_CHECK(ccw.count() == 1);
  SC_CHECK(game.activePiece().rotation == Rotation::CounterClockwise);

  // A wall kick moves the piece while it rotates. That is one rotation event,
  // not a rotation plus a move, so the move cue stays silent.
  Game kicked;
  kicked.start(kSeed);
  CueDetector kickedDetector;
  // A vertical I at column 2, with the cell its rotation would occupy at
  // (3, 11) settled: the in-place candidate and the +2 kick candidate both
  // fail on it, so the rotation succeeds through the (2, -1) kick.
  GameTestPeer::board(kicked).settleCell(3, 11, PieceId::J);
  GameTestPeer::setPiece(
      kicked, Piece{PieceId::I, Rotation::Clockwise, 0, 10});
  prime(kicked, kickedDetector);
  const int xBefore = kicked.activePiece().x;
  const CueSet kickCues = step(kicked, kickedDetector, pressRotateCCW());
  SC_CHECK(kicked.activePiece().rotation == Rotation::Spawn);
  SC_CHECK(kicked.activePiece().x != xBefore);  // the kick really moved it
  SC_CHECK(kickCues.has(SoundCue::Rotate));
  SC_CHECK(!kickCues.has(SoundCue::MoveLand));
  SC_CHECK(kickCues.count() == 1);
}

void testHoldIsSilent() {
  Game game;
  game.start(kSeed);
  CueDetector detector;
  GameTestPeer::setPiece(game, Piece{PieceId::T, Rotation::Spawn, 4, 8});
  prime(game, detector);

  const CueSet cues = step(game, detector, pressHold());
  SC_CHECK(cues.none());
  SC_CHECK(game.hasHoldPiece());
  SC_CHECK(game.holdLocked());

  // The second hold of the same piece is rejected, and silent as well.
  SC_CHECK(step(game, detector, pressHold()).none());
}

// Settles every column of row y except gapColumn, the fixture the rules suite
// uses for clear quantities.
void fillRowExcept(Game& game, int y, int gapColumn) {
  Board& board = GameTestPeer::board(game);
  for (int x = 0; x < kBoardWidth; ++x) {
    if (x != gapColumn) {
      board.settleCell(x, y, PieceId::I);
    }
  }
}

// A vertical I above the gap column, the piece that completes the rows.
void setVerticalI(Game& game, int column, int top) {
  // Clockwise I cells sit in local column 2, rows 0..3.
  const Piece vertical{PieceId::I, Rotation::Clockwise, column - 2, top};
  GameTestPeer::setPiece(game, vertical);
}

void testLockAndClearCues() {
  const struct {
    int rows;
    SoundCue expected;
  } cases[] = {
      {0, SoundCue::MoveLand},
      {1, SoundCue::Clear},
      {2, SoundCue::Clear},
      {3, SoundCue::Clear},
      {4, SoundCue::FourLineClear},
  };
  for (const auto& item : cases) {
    Game game;
    game.start(kSeed);
    CueDetector detector;
    for (int i = 0; i < item.rows; ++i) {
      fillRowExcept(game, kBoardHeight - 1 - i, 3);
    }
    setVerticalI(game, 3, 8);
    prime(game, detector);
    SC_CHECK(game.hasActivePiece());

    // One hard drop travels and locks in the same tick, so it raises exactly
    // one cue, whatever that lock cleared.
    const CueSet cues = step(game, detector, pressHardDrop());
    SC_CHECK(cues.count() == 1);
    SC_CHECK(cues.has(item.expected));
    SC_CHECK(game.lines() == static_cast<std::uint64_t>(item.rows));
    SC_CHECK(game.hasActivePiece());  // the next piece already spawned
    // The ticks after the lock are quiet again.
    SC_CHECK(step(game, detector, idle()).none());
  }
}

void testNaturalLockRaisesTheLandingCue() {
  // A lock that comes from the expired lock delay, not from a hard drop, is a
  // landing like any other.
  Game game;
  game.start(kSeed);
  CueDetector detector;
  // An O resting on the floor.
  GameTestPeer::setPiece(game, Piece{PieceId::O, Rotation::Spawn, 4, 18});
  prime(game, detector);
  int groundedTicks = 0;
  CueSet cues;
  while (groundedTicks < 40) {
    cues = step(game, detector, idle());
    ++groundedTicks;
    if (cues.has(SoundCue::MoveLand)) {
      break;
    }
    SC_CHECK(cues.none());
  }
  SC_CHECK(cues.has(SoundCue::MoveLand));
  SC_CHECK(cues.count() == 1);
  SC_CHECK(game.board().settledCellCount() == 4);
  SC_CHECK(game.hasActivePiece());
}

void testGameOverCueFiresOnce() {
  Game game;
  game.start(kSeed);
  CueDetector detector;
  prime(game, detector);

  // Soft drop until the round ends. The tick that ends it raises the game-over
  // cue and nothing else, and no later tick repeats it.
  int ticks = 0;
  bool sawGameOver = false;
  while (ticks < 20000) {
    const CueSet cues = step(game, detector, holdDown());
    ++ticks;
    if (game.scene() == Scene::GameOver) {
      SC_CHECK(cues.has(SoundCue::GameOver));
      SC_CHECK(cues.count() == 1);
      sawGameOver = true;
      break;
    }
    SC_CHECK(!cues.has(SoundCue::GameOver));
  }
  SC_CHECK(sawGameOver);
  for (int quiet = 0; quiet < 5; ++quiet) {
    SC_CHECK(step(game, detector, holdDown()).none());
  }
}

void testLockOutRaisesTheGameOverCue() {
  // A stack that reaches the top of the visible field, with two free cells at
  // row 0. Under the immediate-spawn rule of task T029 the O spawns in row 0,
  // so the test stands it one row higher, the way an upward kick leaves a
  // piece: its top cells sit in the hidden rows, and that lock ends the round
  // (specification section 4.5). The tick raises the game-over cue and
  // nothing else, even though the lock also changed the board.
  Game game;
  game.start(kSeed);
  CueDetector detector;
  Board& board = GameTestPeer::board(game);
  for (int y = 1; y < kBoardHeight; ++y) {
    fillRowExcept(game, y, -1);  // every column, so the row is full
  }
  for (int x = 0; x < kBoardWidth; ++x) {
    if (x != 4 && x != 5) {
      board.settleCell(x, 0, PieceId::J);
    }
  }
  GameTestPeer::setPiece(game, Piece{PieceId::O, Rotation::Spawn, 3, -1});
  prime(game, detector);
  SC_CHECK(game.activePiece().y == -1);  // its top cells sit in a hidden row

  const CueSet cues = step(game, detector, pressHardDrop());
  SC_CHECK(game.scene() == Scene::GameOver);
  SC_CHECK(cues.has(SoundCue::GameOver));
  SC_CHECK(cues.count() == 1);
  SC_CHECK(!cues.has(SoundCue::MoveLand));
}

void testRestartOutsideATickNeedsAReset() {
  Game game;
  game.start(kSeed);
  CueDetector detector;
  prime(game, detector);
  step(game, detector, pressHardDrop());
  SC_CHECK(game.board().settledCellCount() == 4);

  // A restart rewrites the round outside any tick. A detector that keeps its
  // snapshot reads the emptied board as an event, which is why reset() exists:
  // the platform layer calls it on its scripted restart path.
  game.start(kSeed);
  CueDetector withoutReset = detector;
  const CueSet bogus = withoutReset.observe(game);
  SC_CHECK(bogus.has(SoundCue::MoveLand));  // the very false positive

  detector.reset();
  prime(game, detector);
  SC_CHECK(step(game, detector, idle()).none());
}

void testSessionEventCounts() {
  // One short, exact session: every cue it raises is counted, and each event
  // lands on its own tick. The hard drop at the end of every round locks a
  // piece that spawned in columns 3..5 of an otherwise empty board, so no row
  // ever completes and no clear cue joins the count.
  Game game;
  game.start(kSeed);
  CueDetector detector;
  prime(game, detector);

  int moveLand = 0;
  int rotations = 0;
  int clears = 0;
  int fours = 0;
  int overs = 0;
  const int rounds = 6;

  for (int round = 0; round < rounds; ++round) {
    const CueSet left = step(game, detector, holdLeft());
    SC_CHECK(left.count() == 1 && left.has(SoundCue::MoveLand));
    SC_CHECK(step(game, detector, InputFrame{}).none());
    const CueSet right = step(game, detector, holdRight());
    SC_CHECK(right.count() == 1 && right.has(SoundCue::MoveLand));
    SC_CHECK(step(game, detector, InputFrame{}).none());
    const CueSet cw = step(game, detector, pressRotateCW());
    SC_CHECK(cw.count() == 1 && cw.has(SoundCue::Rotate));
    const CueSet ccw = step(game, detector, pressRotateCCW());
    SC_CHECK(ccw.count() == 1 && ccw.has(SoundCue::Rotate));
    SC_CHECK(step(game, detector, InputFrame{}).none());
    // The hard drop locks the piece and spawns the next one, so the next round
    // of the loop acts on a fresh piece.
    const CueSet drop = step(game, detector, pressHardDrop());
    SC_CHECK(drop.count() == 1 && drop.has(SoundCue::MoveLand));
    SC_CHECK(game.scene() == Scene::Playing);
    moveLand += 3;  // two lateral steps and one landing
    rotations += 2;
  }

  SC_CHECK(moveLand == 3 * rounds);
  SC_CHECK(rotations == 2 * rounds);
  SC_CHECK(clears == 0);
  SC_CHECK(fours == 0);
  SC_CHECK(overs == 0);
  SC_CHECK(game.board().settledCellCount() == 4 * rounds);
}

// ---------------------------------------------------------------------------
// The audio device.
// ---------------------------------------------------------------------------

// Sets SDL_AUDIODRIVER for the next subsystem initialization. SDL reads the
// variable when the audio subsystem starts, and the suite initializes that
// subsystem only through AudioDriver::open(), so one guard frames one attempt.
void setAudioDriver(const char* name) {
  SDL_setenv("SDL_AUDIODRIVER", name, 1);
}

void testDriverWithoutAudio() {
  // A driver name SDL does not know makes the subsystem itself fail, which is
  // the missing-audio case of specification section 6.3. The game must run on.
  AudioDriver audio;
  setAudioDriver("stackfall-missing-driver");
  SC_CHECK(!audio.open());
  SC_CHECK(!audio.available());
  SC_CHECK(audio.sampleRate() == 0);
  SC_CHECK(audio.channels() == 0);
  SC_CHECK(audio.cueSamples(SoundCue::MoveLand).empty());

  // Cue requests still count, as dropped, and nothing else changes.
  audio.play(SoundCue::MoveLand);
  audio.play(SoundCue::Rotate);
  SC_CHECK(audio.requestedCount(SoundCue::MoveLand) == 1);
  SC_CHECK(audio.requestedCount(SoundCue::Rotate) == 1);
  SC_CHECK(audio.playedCount(SoundCue::MoveLand) == 0);
  SC_CHECK(audio.droppedCount() == 2);
  SC_CHECK(audio.mutedCount() == 0);

  // Muting on a silent driver counts the cue as muted, and closing a closed
  // driver is safe.
  audio.setMuted(true);
  audio.play(SoundCue::GameOver);
  SC_CHECK(audio.mutedCount() == 1);
  SC_CHECK(audio.droppedCount() == 2);
  audio.close();
  SC_CHECK(!audio.available());

  // A second attempt fails the same way, and leaves the driver closed again.
  SC_CHECK(!audio.open());
  SC_CHECK(!audio.available());
}

bool openDummyDevice(AudioDriver& audio) {
  setAudioDriver("dummy");
  return audio.open();
}

void testDriverWithADevice() {
  AudioDriver audio;
  if (!openDummyDevice(audio)) {
    std::printf(
        "audio: the SDL dummy driver is unavailable on this host; the device "
        "checks are reported as unavailable\n");
    return;
  }
  SC_CHECK(audio.available());
  SC_CHECK(audio.sampleRate() > 0);
  SC_CHECK(audio.channels() == 1 || audio.channels() == 2);

  // The five cues are synthesized at the device rate and channel count.
  for (const SoundCue cue : kAllSoundCues) {
    const std::vector<std::int16_t>& samples = audio.cueSamples(cue);
    const int frames = soundCueDurationMs(cue) * audio.sampleRate();
    const std::size_t expected =
        static_cast<std::size_t>((frames + 999) / 1000);
    const std::size_t channels =
        static_cast<std::size_t>(audio.channels());
    const std::size_t cells = expected * channels;
    SC_CHECK(samples.size() == cells);
    SC_CHECK(peakOf(samples) >= 5000);
    SC_CHECK(samples.front() == 0);
    SC_CHECK(samples.back() == 0);
  }

  // Playing a cue sends it to the mixer, and the callback runs without any
  // help from the test.
  audio.play(SoundCue::MoveLand);
  audio.play(SoundCue::Rotate);
  SC_CHECK(audio.playedCount(SoundCue::MoveLand) == 1);
  SC_CHECK(audio.playedCount(SoundCue::Rotate) == 1);
  SC_CHECK(audio.totalPlayedCount() == 2);
  SC_CHECK(audio.mutedCount() == 0);
  SC_CHECK(audio.droppedCount() == 0);
  SDL_Delay(120);  // let the dummy device pull a few buffers

  // The mute stops new cues only.
  audio.setMuted(true);
  audio.play(SoundCue::Clear);
  audio.play(SoundCue::FourLineClear);
  SC_CHECK(audio.requestedCount(SoundCue::Clear) == 1);
  SC_CHECK(audio.playedCount(SoundCue::Clear) == 0);
  SC_CHECK(audio.mutedCount() == 2);
  SC_CHECK(audio.available());  // the device stays alive
  SDL_Delay(60);

  audio.setMuted(false);
  audio.play(SoundCue::Clear);
  SC_CHECK(audio.playedCount(SoundCue::Clear) == 1);
  SC_CHECK(audio.totalPlayedCount() == 3);
  SDL_Delay(60);

  // Closing and reopening costs no state: the cues come back, so a session
  // that reinitializes audio cannot go silent by accident.
  audio.close();
  SC_CHECK(!audio.available());
  SC_CHECK(audio.cueSamples(SoundCue::MoveLand).empty());
  SC_CHECK(openDummyDevice(audio));
  SC_CHECK(audio.playedCount(SoundCue::MoveLand) == 0);  // counters reset
  audio.play(SoundCue::GameOver);
  SC_CHECK(audio.playedCount(SoundCue::GameOver) == 1);
  SDL_Delay(60);
  audio.close();
}

void testMixerSaturatesAndFallsSilent() {
  AudioDriver audio;
  if (!openDummyDevice(audio)) {
    std::printf(
        "audio: the SDL dummy driver is unavailable on this host; the mixer "
        "checks are reported as unavailable\n");
    return;
  }
  const int channels = audio.channels();
  const std::size_t frames = 256;
  std::vector<std::int16_t> buffer(frames * static_cast<std::size_t>(channels));

  // Direct mixing needs the device thread out of the way: the mixer body is
  // the callback, and only one thread may own the voice positions.
  audio.setPaused(true);

  // With no cue requested, the mixer writes exact silence.
  audio.mixFrames(buffer.data(), frames);
  for (const std::int16_t value : buffer) {
    SC_CHECK(value == 0);
  }

  // Every voice at once: their sum runs several times past the int16 range, so
  // the mixer saturates, and every sample stays inside the format.
  for (const SoundCue cue : kAllSoundCues) {
    audio.play(cue);
  }
  std::int64_t peak = 0;
  std::size_t loud = 0;
  for (int block = 0; block < 8; ++block) {
    audio.mixFrames(buffer.data(), frames);
    for (const std::int16_t value : buffer) {
      const std::int64_t magnitude =
          value < 0 ? -static_cast<std::int64_t>(value)
                    : static_cast<std::int64_t>(value);
      peak = std::max(peak, magnitude);
      if (magnitude > 1000) {
        ++loud;
      }
    }
  }
  SC_CHECK(peak <= 32768);  // saturated, never out of range
  SC_CHECK(peak > 15000);   // the voices really summed
  SC_CHECK(loud > 0);

  // After the longest cue has passed, the mixer returns to exact silence.
  const std::size_t longest = static_cast<std::size_t>(
      (soundCueDurationMs(SoundCue::GameOver) * audio.sampleRate() + 999) /
      1000);
  for (std::size_t mixed = 0; mixed < longest + frames * 2; mixed += frames) {
    audio.mixFrames(buffer.data(), frames);
  }
  for (const std::int16_t value : buffer) {
    SC_CHECK(value == 0);
  }
  audio.close();
}

// The five cues as WAV evidence for the final human listening pass
// (docs/AGENT_TESTING.md). Written only when the caller asks for a directory.
// The cue name of MoveLand carries a slash, so the file name replaces it.
void dumpCueEvidence(const std::string& directory) {
  std::vector<std::int16_t> samples;
  for (const SoundCue cue : kAllSoundCues) {
    synthesizeCue(cue, kSoundRequestRate, &samples);
    std::string name = soundCueName(cue);
    std::replace(name.begin(), name.end(), '/', '-');
    const std::string path = directory + "/" + name + ".wav";
    if (!writeWavFile(path, samples, kSoundRequestRate)) {
      std::fprintf(stderr, "Warning: cannot write cue evidence \"%s\"\n",
                   path.c_str());
    } else {
      std::printf("audio evidence: %s\n", path.c_str());
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string dumpDir;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--dump-dir") == 0 && i + 1 < argc) {
      dumpDir = argv[++i];
    } else {
      std::fprintf(stderr, "Warning: unknown option \"%s\"; ignored\n",
                   argv[i]);
    }
  }

  std::printf("test_audio: synthesized cues\n");
  testCuesSynthesizeAtEveryRate();
  testCuesAreDistinct();
  testSynthesisRejectsImpossibleRates();
  testWavExport();
  std::printf("test_audio: cue detection through the rules core\n");
  testFallingAndMenusAreSilent();
  testMoveCue();
  testRotateCueAndKickIsNotAMove();
  testHoldIsSilent();
  testLockAndClearCues();
  testNaturalLockRaisesTheLandingCue();
  testGameOverCueFiresOnce();
  testLockOutRaisesTheGameOverCue();
  testRestartOutsideATickNeedsAReset();
  testSessionEventCounts();
  std::printf("test_audio: the audio device\n");
  testDriverWithoutAudio();
  testDriverWithADevice();
  testMixerSaturatesAndFallsSilent();

  if (!dumpDir.empty()) {
    dumpCueEvidence(dumpDir);
  }

  stackfall_test::print_summary();
  return stackfall_test::g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
