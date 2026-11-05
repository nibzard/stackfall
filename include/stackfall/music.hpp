#pragma once

// The generated soundtrack of STACKFALL (MUSIC.md, tasks T062/M1-M6). The
// module has three parts, and none of them touches SDL, a clock, or a file
// the game reads at runtime:
//
//   * The score data: every track is typed note data over musical ticks. The
//     catalogue in src/music_tracks.cpp builds it once, deterministically,
//     from fixed pattern data - no randomness and no assets - and the engine
//     treats it as immutable (MUSIC.md, score representation).
//   * MusicEngine: the sequencer and the voices. It advances musical time
//     from the sample count alone, at whatever sample rate it was given, so
//     playback does not drift with the 60 Hz game tick or the wall clock
//     (MUSIC.md, native audio design). The audio callback of
//     src/audio_sdl.cpp owns one engine; the offline renderer of this module
//     owns another, which is how previews and tests render without a device.
//   * MusicOfflineRenderer: the developer renderer of MUSIC.md phase M2. It
//     drives an engine into 16-bit mono PCM and writes standard WAV files
//     that are review artifacts, never game assets.
//
// Thread ownership follows MUSIC.md: the main thread pushes commands through
// a fixed-capacity single-producer, single-consumer queue and publishes gain
// targets through latest-value atomics, so queue pressure can never lose a
// volume change; the callback owns the transport, the voices, and the gain
// ramps and never logs, allocates, reads a file, or takes a lock.

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace stackfall {

// ---------------------------------------------------------------------------
// Musical time and structure (MUSIC.md, arrangement contract).
// ---------------------------------------------------------------------------

// 96 musical ticks per quarter note: straight sixteenths (24 ticks), triplet
// eighths (32 ticks), and fixed swing offsets all stay whole numbers.
inline constexpr int kMusicTicksPerQuarter = 96;
inline constexpr int kMusicTicksPerSixteenth = kMusicTicksPerQuarter / 4;
inline constexpr int kMusicBeatsPerBar = 4;
inline constexpr int kMusicTicksPerBar = kMusicTicksPerQuarter * kMusicBeatsPerBar;
// Intensity changes apply at eight-bar phrase boundaries (MUSIC.md).
inline constexpr int kMusicBarsPerPhrase = 8;
inline constexpr int kMusicTicksPerPhrase = kMusicTicksPerBar * kMusicBarsPerPhrase;

// The three collections of the catalogue and the intensity maps every track
// carries (MUSIC.md, track catalogue and arrangement contract).
enum class MusicCollection : std::uint8_t { Arcade = 0, Groove = 1, Focus = 2 };
const char* musicCollectionName(MusicCollection collection);

enum class MusicIntensity : std::uint8_t { Low = 0, Medium = 1, High = 2 };
inline constexpr int kMusicIntensityCount = 3;

// The MUSIC row values of MUSIC.md's player controls (task T062/M5): the
// track pool the director draws from, with Off as the music-only stop. The
// settings file stores the names below, and the master switch stays the
// separate sound preference.
enum class MusicMode : std::uint8_t {
  Auto = 0,
  Arcade,
  Groove,
  Focus,
  Off,
};

// One lowercase name per mode, the settings file's own rendering.
const char* musicModeName(MusicMode mode);

// The settings file parser's half of the name pair: only the five canonical
// names parse, and nothing else does.
bool musicModeFromName(const char* name, MusicMode* mode);

// The initial engine voice budget (MUSIC.md, thread communication): twelve
// simultaneous music voices beside the five effect voices of the cue mixer.
inline constexpr int kMusicMaxVoices = 12;

// A note plays at an intensity when its layer mask holds that intensity's
// bit, so the three intensity maps share one timeline instead of three
// arrangements. Layers are cumulative: the low map carries the essential
// voices and plays at every intensity, a medium-layer voice joins from the
// medium map up, and a high-layer voice belongs to the high map alone.
enum : std::uint8_t {
  kMusicLayerLow = 1 << 0,
  kMusicLayerMedium = 1 << 1,
  kMusicLayerHigh = 1 << 2,
  kMusicLayerAll = kMusicLayerLow | kMusicLayerMedium | kMusicLayerHigh,
};

// The cumulative masks a composer uses: an essential voice (melody, bass,
// the kick anchor) plays everywhere; an optional voice states where it
// joins. The engine's layer-release logic reads the same bits.
inline constexpr std::uint8_t kMusicLayersEssential = kMusicLayerAll;
inline constexpr std::uint8_t kMusicLayersFromMedium =
    kMusicLayerMedium | kMusicLayerHigh;
inline constexpr std::uint8_t kMusicLayersHighOnly = kMusicLayerHigh;

// The compact shared instrument bank (MUSIC.md, instruments). Numeric values
// index the parameter table in src/music.cpp, so they must stay dense.
enum class MusicInstrument : std::uint8_t {
  PulseLead = 0,   // 25 percent pulse, the melodic lead
  PulseWide = 1,   // 50 percent pulse, a rounder counter voice
  TriangleBass = 2,  // triangle bass with a short attack
  ChordVoice = 3,  // soft, dark chord and pad voice
  ArpVoice = 4,    // thin 12.5 percent pulse for arpeggios
  Kick = 5,        // short pitch-swept sine kick
  Snare = 6,       // filtered noise plus a short tone
  HatClosed = 7,   // short noise hat
  HatOpen = 8,     // open noise hat
  Accent = 9,      // quiet metallic accent for fills
};
inline constexpr int kMusicInstrumentCount = 10;

const char* musicInstrumentName(MusicInstrument instrument);

// One note of one arrangement. `startTick` is absolute within the track and
// `lengthTicks` counts from it; validation rejects a note that crosses the
// loop boundary. Percussion ignores `pitch`. `velocity` runs 1..127 and
// `layerMask` carries the intensity maps.
struct MusicNote {
  std::uint32_t startTick = 0;
  std::uint16_t lengthTicks = 0;
  std::int16_t pitch = 0;  // MIDI note number; percussion reads none
  std::uint8_t velocity = 96;
  std::uint8_t layerMask = kMusicLayerAll;
  MusicInstrument instrument = MusicInstrument::PulseLead;
};

// One complete track: fixed patterns expanded into one sorted note list
// (MUSIC.md: expand and validate arrangement data before playback, outside
// the audio callback). Track IDs are stable slugs after release.
struct MusicTrackData {
  const char* id = "";
  const char* title = "";
  MusicCollection collection = MusicCollection::Arcade;
  int bpm = 120;
  int bars = 0;
  std::vector<MusicNote> notes;  // sorted by startTick, equal starts allowed

  std::uint32_t totalTicks() const {
    return static_cast<std::uint32_t>(bars) * kMusicTicksPerBar;
  }
};

// The whole catalogue in fixed order. Index 0 is the title theme
// (arcade-boot); musicTrackIndex() resolves a stable ID to that order.
const std::vector<MusicTrackData>& musicCatalogue();
int musicTrackCount();
int musicTrackIndex(const char* id);  // -1 when the ID is unknown
const char* musicTrackId(int index);  // "" when out of range

// ---------------------------------------------------------------------------
// Score validation (MUSIC.md: reject invalid scores before playback).
// ---------------------------------------------------------------------------

struct MusicValidationReport {
  bool ok = false;
  int trackIndex = -1;
  const char* trackId = "";
  const char* error = "";    // one static phrase naming the first failure
  std::uint32_t tick = 0;    // the offending note's start tick
  int note = -1;             // its index in the note list
  int maxVoices = 0;         // held-voice maximum, over every intensity map
};

// Checks one track: notes sorted by start, inside the loop, with known
// instruments, live velocities, and a layer mask; and the held-voice maximum
// of every intensity map inside kMusicMaxVoices. `trackIndex` only labels the
// report; the function validates the data it was given.
bool validateMusicTrack(const MusicTrackData& track, int trackIndex,
                        MusicValidationReport* report);

// Validates the whole catalogue and returns the first failure. Building the
// catalogue runs this once; a failure is a composition bug, not a runtime
// condition, and the caller refuses to ship the broken track.
MusicValidationReport validateMusicCatalogue();

// ---------------------------------------------------------------------------
// The sequencer and voice engine.
// ---------------------------------------------------------------------------

// What the main thread asks the callback to do. Play and SetIntensity are
// scheduled: the engine applies them at the next bar and phrase boundary
// with a 100 millisecond fade out before the switch and a fade in on the
// downbeat (MUSIC.md, playback behavior). Stop, Pause, and Resume act at
// once with their own fade. Repeated commands coalesce: only the latest
// scheduled destination stays pending.
enum class MusicCommandType : std::uint8_t {
  Play = 0,
  Stop,
  Pause,
  Resume,
  SetIntensity,
};

struct MusicCommand {
  MusicCommandType type = MusicCommandType::Stop;
  std::uint8_t track = 0;      // catalogue index, for Play
  std::uint8_t intensity = 0;  // MusicIntensity value, for Play/SetIntensity
  std::uint16_t fadeMs = 100;
};

// What the main thread may read back. Published through lock-free atomics;
// a torn read costs one stale frame of UI, never a wrong transport decision
// that a later frame does not correct.
struct MusicSnapshot {
  std::uint8_t track = 0;        // catalogue index
  std::uint8_t intensity = 0;
  bool transportRunning = false;
  bool frozen = false;           // paused by the scene policy
  bool pendingSwitch = false;    // a Play command is scheduled
  std::uint32_t bar = 0;         // current bar inside the arrangement
  std::uint32_t loopCount = 0;   // completed arrangements since Play
  std::uint32_t totalBars = 0;
  std::uint8_t activeVoices = 0;
};

class MusicEngine {
 public:
  MusicEngine();

  // Builds the wave, noise, envelope, and pitch tables for `sampleRate`.
  // Main thread only: it allocates. Re-initializing for a new rate resets the
  // transport, exactly as a fresh device would.
  void initialize(int sampleRate);
  int sampleRate() const { return sampleRate_; }

  // Main-thread command intake. False means the queue is full; the caller
  // keeps its request and pushes it again, which the coalescing below turns
  // into the same outcome.
  bool tryPushCommand(const MusicCommand& command);

  // Latest-value gain publication, both in Q16 (65536 is unity). The music
  // gain carries the volume setting and the effect duck; the master gain
  // carries the master mute of both buses. The callback ramps toward the
  // published targets, so a change lands as a short fade, never a step.
  void publishMusicGain(std::uint32_t musicGainQ16);
  void publishMasterGain(std::uint32_t masterGainQ16);

  // Renders `frames` mono frames and adds them into `dst`. This is the whole
  // callback-side entry point: it drains commands, advances the transport,
  // and mixes the voices at the current gains. `dst` is not cleared.
  void renderAdd(std::int32_t* dst, std::size_t frames);

  MusicSnapshot snapshot() const;

  // The highest simultaneous voice count since initialize(), for the
  // performance evidence of MUSIC.md; resetVoiceStats() clears it.
  std::uint32_t voiceHighWater() const;
  void resetVoiceStats();

  // The command traffic of the performance evidence (task T065): how many
  // commands the main thread pushed and how many the queue rejected, both
  // read once at shutdown. The director retries a rejected command, so the
  // rejected count is the queue-pressure signal, not a lost request.
  unsigned long commandsPushed() const {
    return commandsPushed_.load(std::memory_order_relaxed);
  }
  unsigned long commandsRejected() const {
    return commandsRejected_.load(std::memory_order_relaxed);
  }
  unsigned long commandsDrained() const {
    return commandsDrained_.load(std::memory_order_relaxed);
  }

  // The explicit perceptual volume table of MUSIC.md: eleven steps, about
  // 2.2 decibels apart, with step 0 exactly silent and step 10 at unity.
  static constexpr std::array<std::uint32_t, 11> kVolumeGainQ16 = {
      0,     6233,  8095,  10513, 13653, 17731,
      23028, 29906, 38839, 50440, 65536};

 private:
  // Command capacity: one per scene-policy decision plus the retried ones.
  // Overflow is reported, never dropped silently, and scheduled commands
  // coalesce to one pending slot each.
  static constexpr std::size_t kQueueSlots = 16;

  struct Voice {
    MusicInstrument instrument = MusicInstrument::Kick;
    std::int16_t pitch = 0;
    std::uint32_t phase = 0;       // wave phase, 32.32 fixed point
    std::int32_t phaseIncrement = 0;
    std::int32_t sweepStep = 0;    // kick: phase increment change per sample
    std::int32_t sweepRemaining = 0;
    std::int32_t envelope = 0;     // 0..32768
    std::int32_t attackStep = 0;   // envelope steps per sample
    std::int32_t decayStep = 0;
    std::int32_t releaseStep = 0;
    std::int32_t sustainLevel = 0;
    std::uint32_t gainQ16 = 0;     // velocity times instrument gain
    std::uint32_t heldSamples = 0;
    std::uint32_t holdSamples = 0;
    std::uint8_t layerMask = kMusicLayerAll;
    bool attacking = true;
    bool releasing = false;
    bool active = false;
    std::uint16_t noiseCursor = 0;
    std::int16_t noisePrevious = 0;
  };

  void drainCommands();
  void applyImmediate(const MusicCommand& command);
  void onTickBoundary(std::uint32_t tick);
  void switchTrack(int trackIndex, int intensity, std::uint16_t fadeMs);
  void startNote(const MusicNote& note);
  void releaseLayer(std::uint8_t intensityBit);
  void releaseVoice(Voice* voice, bool fast);
  Voice* acquireVoice(std::uint8_t layerMask);
  std::int32_t voiceSample(Voice* voice);
  std::int32_t tableSample(std::uint32_t phase, int table) const;

  int sampleRate_ = 0;
  bool initialized_ = false;

  // Immutable tables, built by initialize().
  std::array<std::array<std::int16_t, 2048>, 6> waveTables_{};  // 5 is sine
  std::array<std::int16_t, 8192> noiseTable_{};
  std::array<std::int32_t, 128> pitchIncrement_{};   // 32.32 per MIDI note
  std::array<std::int32_t, 128> velocityGainQ16_{};

  // The command queue. Head is the consumer's, tail the producer's; one of
  // each, so the loads and stores need no lock.
  std::array<MusicCommand, kQueueSlots> queue_{};
  std::atomic<std::uint32_t> queueTail_{0};  // producer writes
  std::atomic<std::uint32_t> queueHead_{0};  // consumer writes

  // The traffic counters of the accessors above, one relaxed cell each side
  // of the queue (task T065).
  std::atomic<std::uint64_t> commandsPushed_{0};
  std::atomic<std::uint64_t> commandsRejected_{0};
  std::atomic<std::uint64_t> commandsDrained_{0};

  // Published gain targets (Q16) and the callback-side ramping copies.
  std::atomic<std::uint32_t> musicGainTarget_{65536};
  std::atomic<std::uint32_t> masterGainTarget_{65536};
  std::uint32_t musicGain_ = 65536;
  std::uint32_t masterGain_ = 65536;
  std::uint32_t gainRampStep_ = 1;  // Q16 per sample toward a target

  // Transport state. `phase` counts musical ticks in 32.32 fixed point, so
  // fractional timing survives every callback boundary without drift.
  bool transportRunning_ = false;
  bool frozen_ = false;
  int trackIndex_ = 0;
  std::uint8_t intensity_ = 0;
  std::uint32_t ticksPerSecondNum_ = 0;  // bpm * 96, over 60 seconds
  std::uint32_t phaseIncrement_ = 0;     // 32.32 ticks per sample
  std::uint64_t phase_ = 0;              // ticks << 32
  std::uint32_t noteCursor_ = 0;
  std::uint32_t samplesPerTickQ8_ = 0;   // 24.8 fixed, for fade scheduling

  // Scheduled (coalesced) requests and their pre-boundary fade.
  bool pendingPlay_ = false;
  MusicCommand pendingPlayCommand_{};
  bool hasPendingIntensity_ = false;
  std::uint8_t pendingIntensityValue_ = 0;
  bool preBoundaryFadeStarted_ = false;

  // Scene fade: gain ramp toward 0 or unity over the fade length, and the
  // actions that wait for silence (a Stop ends the transport; a Pause
  // freezes it, MUSIC.md playback behavior).
  std::uint32_t sceneGain_ = 0;
  std::uint32_t sceneGainTarget_ = 0;
  std::uint32_t sceneGainStep_ = 1;
  bool stopWhenSilent_ = false;
  bool freezeWhenSilent_ = false;
  std::uint32_t lastTick_ = 0;

  std::array<Voice, kMusicMaxVoices> voices_{};

  // Snapshot publication (lock-free; single writer, the callback).
  std::atomic<std::uint32_t> publishedBar_{0};
  std::atomic<std::uint32_t> publishedLoopCount_{0};
  std::atomic<std::uint32_t> publishedVoiceHighWater_{0};
  std::atomic<std::uint8_t> publishedActive_{0};
  std::atomic<std::uint8_t> publishedTrack_{0};
  std::atomic<std::uint8_t> publishedIntensity_{0};
  std::atomic<bool> publishedRunning_{false};
  std::atomic<bool> publishedFrozen_{false};
  std::atomic<bool> publishedPending_{false};
};

// ---------------------------------------------------------------------------
// The offline developer renderer (MUSIC.md phase M2): an engine, its gain
// publication, and a mono 16-bit output path. Tests and the preview tool use
// it to render any track, duration, rate, and intensity without a device.
// ---------------------------------------------------------------------------

class MusicOfflineRenderer {
 public:
  explicit MusicOfflineRenderer(int sampleRate);

  MusicEngine& engine() { return engine_; }

  // Convenience wrappers in the shape of the policy calls the game makes.
  void play(int trackIndex, MusicIntensity intensity);
  void stop(std::uint16_t fadeMs);
  void pause();
  void resume();
  void setIntensity(MusicIntensity intensity);

  // Renders `frames` mono frames into `out` as signed 16-bit PCM, cleared
  // first, with one final safety clamp (MUSIC.md, mixing and gain).
  void render(std::int16_t* out, std::size_t frames);

 private:
  MusicEngine engine_;
  std::vector<std::int32_t> scratch_;
};

// Writes `samples` as a 16-bit mono PCM WAV file at `sampleRate`. Review
// artifact only: the game itself never reads a WAV file, and none ships.
bool writeMusicWavFile(const std::string& path,
                       const std::vector<std::int16_t>& samples, int sampleRate);

}  // namespace stackfall
