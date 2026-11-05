#pragma once

// Synthesized sound effects for STACKFALL (specification section 6.3, task
// T013). Everything you can hear in the game is generated here from
// mathematical waveforms at startup: integer phase accumulators drive square,
// triangle, and sine oscillators, integer envelopes shape each tone, and the
// result is 16-bit PCM in memory. No .wav, .ogg, or other audio asset exists
// anywhere in the project, and nothing here reads an audio file at runtime.
//
// The module has three parts, kept in one place because they share the cue
// table:
//
//   * synthesizeCue() renders one cue into a mono PCM buffer. It uses no SDL
//     type and no clock, so the headless test suite pins its output: the same
//     cue at the same rate always renders the same bytes on every machine.
//   * CueDetector turns the observable state of a round into cues. It reads
//     the game through const queries only and never writes it, exactly like
//     the renderer (specification section 7), and it owns no randomness, so
//     the gameplay PCG32 stream stays untouched (specification section 7).
//   * AudioDriver owns the SDL audio device. open() may fail for any reason -
//     no subsystem, no device, no usable format - and the failure only costs
//     the sound: every other method stays safe to call and the game runs on.
//
// The five cues are exactly the five of section 6.3. One cue, MoveLand, serves
// both a lateral move and a landing lock, as that section lists them together.
// A four-line clear has its own cue and replaces the ordinary clear cue; the
// game-over cue replaces every other cue of its tick.
//
// The mute toggle is platform-layer state (specification section 6.3): the
// keyboard M key and the controller Select button switch it, the settings file
// of stackfall/storage.hpp supplies its starting value, and it never reaches
// the rules core. Since the soundtrack of MUSIC.md joined the device, the
// toggle is the master switch: it fades both buses - the effect cues and the
// music engine below - while the music transport keeps running, and the
// separate music mode of the settings file can still stop the music alone.
//
// The device carries two buses now (MUSIC.md, mixing and gain): the five cue
// voices sum into one 32-bit accumulator and the MusicEngine of
// stackfall/music.hpp renders the music bus beside them; each bus takes its
// own volume step, the master gain applies to both, and one final safety
// clamp produces the 16-bit stream. A clear, a four-line clear, or a game
// over ducks the music by about 4 decibels for the cue's length plus a short
// release, so the feedback stays audible over the soundtrack without
// touching the transport.

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>

#include "stackfall/game.hpp"
#include "stackfall/music.hpp"

namespace stackfall {

// The five cues of specification section 6.3. The numeric values index the
// cue table and the per-cue counters, so they must stay dense and ordered.
enum class SoundCue : std::uint8_t {
  MoveLand = 0,     // one lateral step, or a lock that clears nothing
  Rotate = 1,       // a successful rotation, wall kick included
  Clear = 2,        // a lock that clears one to three lines
  FourLineClear = 3,  // a lock that clears four lines
  GameOver = 4,     // the round ended by block-out or lock-out
};

inline constexpr int kSoundCueCount = 5;

inline constexpr std::array<SoundCue, kSoundCueCount> kAllSoundCues = {
    {SoundCue::MoveLand, SoundCue::Rotate, SoundCue::Clear,
     SoundCue::FourLineClear, SoundCue::GameOver}};

// One lowercase name per cue, for logs, tests, and evidence files.
const char* soundCueName(SoundCue cue);

// A set of cues, one bit per cue. One simulation tick raises at most a couple
// of cues, and a set keeps the reporting honest about that: a rotation and a
// move can share a tick, while a lock raises exactly one of MoveLand, Clear,
// and FourLineClear.
struct CueSet {
  std::uint8_t bits = 0;

  bool has(SoundCue cue) const {
    return (bits & (1u << static_cast<int>(cue))) != 0;
  }
  void add(SoundCue cue) { bits |= 1u << static_cast<int>(cue); }
  bool none() const { return bits == 0; }
  int count() const {
    int total = 0;
    for (const SoundCue cue : kAllSoundCues) {
      if (has(cue)) {
        ++total;
      }
    }
    return total;
  }
};

// ---------------------------------------------------------------------------
// Cue synthesis (specification section 6.3).
// ---------------------------------------------------------------------------

// The sample rate the game requests from the device. The device may deliver
// another rate; the cues are synthesized at whatever rate it reports, so the
// tones keep their pitch and duration either way.
inline constexpr int kSoundRequestRate = 44100;

// Renders `cue` at `sampleRate` into `samples` as mono 16-bit PCM, replacing
// any previous contents. The tone is built from the cue's segment table: every
// segment sweeps its own frequency, mixes the three waveforms with its own
// weights, and scales its own amplitude; one phase accumulator runs across the
// whole cue so a segment change never steps the phase, and a short attack ramp
// plus a closing fade keep both ends silent, so a cue cannot click.
//
// Pure integer arithmetic throughout: the same cue at the same rate renders
// byte-identical samples on every machine and every build.
void synthesizeCue(SoundCue cue, int sampleRate,
                   std::vector<std::int16_t>* samples);

// The documented duration of one cue in milliseconds, so tests can check the
// rendered length against the table that defines the sound.
int soundCueDurationMs(SoundCue cue);

// Writes `samples` as a 16-bit mono PCM WAV file. This exists only so an
// unattended run can keep audible evidence of the five cues for the final
// human pass (docs/AGENT_TESTING.md); the game itself never reads a WAV file,
// and no WAV file belongs to the game data.
bool writeWavFile(const std::string& path,
                  const std::vector<std::int16_t>& samples, int sampleRate);

// ---------------------------------------------------------------------------
// Cue detection: observable round state to cues (specification sections 6.3
// and 7).
// ---------------------------------------------------------------------------

// Turns what a simulation tick did into cues by observing the round before and
// after it. The detector holds one private snapshot and answers from the
// difference, which keeps the rules core free of any audio notion:
//
//   * a changed board means a lock: the size of the line jump picks MoveLand,
//     Clear, or FourLineClear;
//   * a changed rotation state means a rotation, and the position change that
//     travels with it is the wall kick, not a move;
//   * a changed column with an unchanged rotation means a lateral move;
//   * a hold swap replaces the piece without locking anything, so it is
//     silent, as are gravity and soft drop;
//   * the tick that enters GameOver reports GameOver and nothing else;
//   * ticks outside Playing, and every paused tick, report nothing.
//
// Call observe() once per simulation tick, after Game::tick. Nothing else may
// change the game between two observations; use reset() after a change that
// happens outside a tick, such as a scripted restart.
class CueDetector {
 public:
  CueSet observe(const Game& game);

  // Forgets the snapshot, so the next observe() cannot compare against a state
  // that changed outside a tick.
  void reset() { hasSnapshot_ = false; }

 private:
  struct Snapshot {
    Scene scene = Scene::Boot;
    bool hasActive = false;
    Piece piece{PieceId::I, Rotation::Spawn, 0, 0};
    bool hasHold = false;
    PieceId holdId = PieceId::I;
    bool holdLocked = false;
    std::uint64_t lines = 0;
    std::uint64_t board = 0;  // occupancy signature, see take()
  };

  static Snapshot take(const Game& game);

  bool hasSnapshot_ = false;
  Snapshot previous_;
};

// ---------------------------------------------------------------------------
// The audio device.
// ---------------------------------------------------------------------------

// Owns the SDL audio device and the five synthesized cues.
//
// open() initializes the audio subsystem and opens the default output device
// with a 16-bit callback spec, then synthesizes the cues at the rate and
// channel count the device really reported and starts playback. Every step can
// fail; a failure is logged, leaves the driver closed, and returns false, and
// the caller carries on with a silent game (specification section 6.3).
//
// Playback is a small mixer inside the SDL audio callback: each cue is one
// voice, a play request restarts that voice, and the mixer sums the sounding
// voices with saturation. The main thread talks to the callback through
// request counters alone, and the cue buffers never change after open(), so
// the two threads share no unprotected state.
class AudioDriver {
 public:
  // A closed driver: no device, sound on, no counters.
  AudioDriver() = default;

  // Closes the device if one is open.
  ~AudioDriver();

  AudioDriver(const AudioDriver&) = delete;
  AudioDriver& operator=(const AudioDriver&) = delete;

  // See the class comment. Safe to call again after any outcome. A successful
  // open also clears the cue counters, so a reinitialized driver starts a
  // fresh account of what the session played.
  bool open();

  // Stops playback, closes the device, and quits the audio subsystem when this
  // driver initialized it. Safe to call when already closed.
  void close();

  // Suspends or resumes the device callback without closing anything. The
  // platform layer never needs this in normal play; it exists so a caller can
  // own the mixer for a while, as the test suite does.
  void setPaused(bool paused);

  bool available() const { return device_ != 0; }
  int sampleRate() const { return obtainedRate_; }
  int channels() const { return obtainedChannels_; }
  // The device's own callback size in frames, the unit MUSIC.md's callback
  // budget is measured against.
  int bufferFrames() const { return obtainedBufferFrames_; }

  // The mute toggle of specification section 6.3, the master switch of all
  // audio since MUSIC.md. Muting stops new cues and fades both buses to
  // silence; it never touches the device or the music timeline, so unmuting
  // needs no reinitialization.
  void setMuted(bool muted);
  bool muted() const { return muted_; }
  void toggleMute() { setMuted(!muted_); }

  // The two volume steps of MUSIC.md's player controls, 0 through 10. Each
  // publishes its bus gain as a latest-value atomic, so queue pressure can
  // never lose one, and 0 is exactly silent. The master mute gates both
  // buses on top of these.
  void setMusicVolume(int step);
  int musicVolume() const { return musicVolume_; }
  void setEffectsVolume(int step);
  int effectsVolume() const { return effectsVolume_; }

  // The music engine this device renders. The MusicDirector of
  // stackfall/music_director.hpp pushes commands and reads snapshots through
  // here; with no device the engine simply never renders, and the director's
  // requests cost nothing.
  MusicEngine& musicEngine() { return musicEngine_; }

  // Plays every cue of `set`. A muted or closed driver counts the cue and
  // does nothing else, so a session log can report exactly what a working
  // device would have played.
  void play(CueSet set);
  void play(SoundCue cue);

  // The synthesized PCM of one cue at the device rate, interleaved for the
  // device channel count. Empty before a successful open(). Tests and evidence
  // dumps read this; the callback is its only consumer at runtime.
  const std::vector<std::int16_t>& cueSamples(SoundCue cue) const {
    return samples_[static_cast<std::size_t>(cue)];
  }

  // How many times each cue was requested, how many of those reached a device,
  // and how many the mute or a missing device dropped. The event counts of an
  // unattended session come from here.
  unsigned requestedCount(SoundCue cue) const {
    return requested_[static_cast<std::size_t>(cue)];
  }
  unsigned playedCount(SoundCue cue) const {
    return played_[static_cast<std::size_t>(cue)];
  }
  unsigned mutedCount() const { return mutedCount_; }
  unsigned droppedCount() const { return droppedCount_; }
  unsigned totalPlayedCount() const;

  // Mixes `frames` frames into `out`, which must hold frames * channels()
  // int16 values. This is the whole body of the SDL callback, exposed so the
  // test suite can drive the mixer without a device.
  void mixFrames(std::int16_t* out, std::size_t frames);

  // The callback accounting of the performance evidence (task T065): how
  // many callbacks ran and how many frames they mixed. Two relaxed atomics
  // the callback increments, read once at shutdown, so the counters cost
  // nothing a timed sample can see.
  unsigned long callbackCount() const {
    return callbackCount_.load(std::memory_order_relaxed);
  }
  unsigned long callbackFrames() const {
    return callbackFrames_.load(std::memory_order_relaxed);
  }
  // The callback work total and maximum, in performance-counter counts.
  // Zero unless enableCallbackTiming() ran, so the two clock reads per
  // callback stay out of every sample that did not ask for them.
  bool callbackTimingEnabled() const { return callbackTiming_; }
  void enableCallbackTiming();
  std::uint64_t callbackWorkCounts() const {
    return callbackWorkCounts_.load(std::memory_order_relaxed);
  }
  std::uint64_t callbackWorkMaxCounts() const {
    return callbackWorkMaxCounts_.load(std::memory_order_relaxed);
  }

 private:
  // The synthesized cues and the counters above are main-thread state; the
  // voice positions and the seen counters below belong to the callback. The
  // request counters are the only shared cells, and they are atomic.
  static void SDLCALL audioCallback(void* userdata, Uint8* stream, int len);

  SDL_AudioDeviceID device_ = 0;
  bool subsystemInitialized_ = false;
  bool muted_ = false;
  int obtainedRate_ = 0;
  int obtainedChannels_ = 0;
  int obtainedBufferFrames_ = 0;

  std::array<std::vector<std::int16_t>, kSoundCueCount> samples_;
  std::array<unsigned, kSoundCueCount> requested_{};
  std::array<unsigned, kSoundCueCount> played_{};
  unsigned mutedCount_ = 0;
  unsigned droppedCount_ = 0;

  std::array<std::atomic<unsigned>, kSoundCueCount> voiceRequest_{};
  std::array<std::atomic<unsigned>, kSoundCueCount> voiceSeen_{};
  std::array<std::size_t, kSoundCueCount> voicePos_{};

  // The music bus (MUSIC.md, native audio design): the engine owns the
  // transport and the voices inside the callback, and this scratch buffer -
  // sized once at open() - receives its mono frames. No allocation, no log,
  // no lock inside the callback.
  MusicEngine musicEngine_;
  std::vector<std::int32_t> musicScratch_;
  std::vector<std::int32_t> cueScratch_;
  int musicVolume_ = 10;
  int effectsVolume_ = 10;
  // The duck of MUSIC.md's mixing rules: frames the music stays lowered
  // after a clear or game-over cue, and the duck gain the callback ramps.
  int duckHoldFrames_ = 0;
  std::uint32_t duckGainQ16_ = 65536;
  // The master mute fades both buses: the engine ramps its own copy, and this
  // member ramps the cue bus in the callback at the same 15 millisecond rate,
  // so neither bus clicks when the master switch moves.
  std::uint32_t cueMasterGainQ16_ = 65536;
  std::uint32_t cueMasterStep_ = 1;

  // The callback accounting of the accessors above. `callbackTiming_` is
  // main-thread state set once before playback starts; the two work cells
  // are callback-thread state behind relaxed atomics.
  std::atomic<std::uint64_t> callbackCount_{0};
  std::atomic<std::uint64_t> callbackFrames_{0};
  bool callbackTiming_ = false;
  std::atomic<std::uint64_t> callbackWorkCounts_{0};
  std::atomic<std::uint64_t> callbackWorkMaxCounts_{0};

  void publishMusicGain();
  void updateDuck(std::size_t frames);
};

}  // namespace stackfall
