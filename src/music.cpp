// The music sequencer and voice engine of MUSIC.md (tasks T062/M2). See
// include/stackfall/music.hpp for the module contract.
//
// All transport math is integer fixed point. `phase_` counts musical ticks
// in 32.32, so one arrangement at one sample rate advances through the same
// sample counts in one long render and in any split of callback chunks, and
// the loop boundary lands within one output sample of the rational schedule.
// Wave, noise, envelope, and pitch tables are built once by initialize() on
// the main thread; the render path below only reads them.

#include "stackfall/music.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace stackfall {
namespace {

// The instrument parameters of MUSIC.md. Percussion entries carry their own
// sweep and mixing numbers; pitched entries name a wave table.
struct InstrumentParams {
  int table;             // wave table index, -1 for bespoke percussion
  int attackMs;
  int decayMs;           // decay to the sustain level (or to zero)
  int releaseMs;
  int sustainPpm;        // sustain level in parts per thousand
  int gainQ16;           // instrument gain
  double kickStartHz;    // kick sweep
  double kickEndHz;
  int kickSweepMs;
  double toneHz;         // snare tone / accent ring
  int toneWeightQ16;
  int noiseWeightQ16;
};

// Index order matches MusicInstrument. Gains are tuned so a full
// twelve-voice bus peaks below the mix headroom at unity volume.
constexpr InstrumentParams kInstruments[kMusicInstrumentCount] = {
    /* PulseLead */ {0, 4, 220, 140, 620, 27500, 0, 0, 0, 0, 0, 0},
    /* PulseWide */ {1, 6, 260, 160, 560, 22000, 0, 0, 0, 0, 0, 0},
    /* TriangleBass */ {2, 2, 320, 110, 700, 32000, 0, 0, 0, 0, 0, 0},
    /* ChordVoice */ {3, 8, 300, 220, 550, 15000, 0, 0, 0, 0, 0, 0},
    /* ArpVoice */ {4, 2, 160, 120, 500, 17000, 0, 0, 0, 0, 0, 0},
    /* Kick */ {-1, 0, 0, 10, 0, 30000, 165.0, 44.0, 48, 0, 0, 0},
    /* Snare */ {-1, 0, 0, 12, 0, 23000, 0, 0, 0, 185.0, 18000, 30000},
    /* HatClosed */ {-1, 0, 0, 8, 0, 12000, 0, 0, 0, 0, 0, 0},
    /* HatOpen */ {-1, 0, 0, 8, 0, 10000, 0, 0, 0, 0, 0, 0},
    /* Accent */ {-1, 0, 0, 10, 0, 13000, 0, 0, 0, 1740.0, 0, 0},
};

// Percussion decay lengths in milliseconds: the note length of the score
// holds the voice, and these shape the body.
constexpr int kPercussionDecayMs[kMusicInstrumentCount] = {
    0, 0, 0, 0, 0, 170, 130, 45, 260, 120};

// One fixed trim on the summed music bus, Q16. The instrument gains and
// velocities leave the loudest twelve-voice bars of the catalogue peaking
// just past 29000; this trim lands them under 24000 at unity volume, which
// leaves the shared final clamp clear for the effect bus during normal
// play (MUSIC.md, mixing and gain).
constexpr std::uint32_t kMusicBusTrimQ16 = 50000;

std::int16_t clamp16(std::int32_t value) {
  if (value > 32767) {
    return 32767;
  }
  if (value < -32768) {
    return -32768;
  }
  return static_cast<std::int16_t>(value);
}

int millisecondsToSamples(int ms, int sampleRate) {
  return static_cast<int>((static_cast<std::int64_t>(ms) * sampleRate + 999) /
                          1000);
}

// Both WAV header writers report a failed byte so the export can say so.
bool wavWrite16(std::FILE* file, std::uint16_t value) {
  const unsigned char bytes[2] = {
      static_cast<unsigned char>(value & 0xFFu),
      static_cast<unsigned char>((value >> 8) & 0xFFu)};
  return std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

bool wavWrite32(std::FILE* file, std::uint32_t value) {
  const unsigned char bytes[4] = {
      static_cast<unsigned char>(value & 0xFFu),
      static_cast<unsigned char>((value >> 8) & 0xFFu),
      static_cast<unsigned char>((value >> 16) & 0xFFu),
      static_cast<unsigned char>((value >> 24) & 0xFFu)};
  return std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

}  // namespace

const char* musicModeName(MusicMode mode) {
  switch (mode) {
    case MusicMode::Auto:
      return "auto";
    case MusicMode::Arcade:
      return "arcade";
    case MusicMode::Groove:
      return "groove";
    case MusicMode::Focus:
      return "focus";
    case MusicMode::Off:
      return "off";
  }
  return "?";
}

bool musicModeFromName(const char* name, MusicMode* mode) {
  if (name == nullptr || mode == nullptr) {
    return false;
  }
  for (int value = 0; value <= static_cast<int>(MusicMode::Off); ++value) {
    const MusicMode candidate = static_cast<MusicMode>(value);
    if (std::strcmp(name, musicModeName(candidate)) == 0) {
      *mode = candidate;
      return true;
    }
  }
  return false;
}

const char* musicCollectionName(MusicCollection collection) {
  switch (collection) {
    case MusicCollection::Arcade:
      return "Arcade";
    case MusicCollection::Groove:
      return "Groove";
    case MusicCollection::Focus:
      return "Focus";
  }
  return "?";
}

const char* musicInstrumentName(MusicInstrument instrument) {
  switch (instrument) {
    case MusicInstrument::PulseLead:
      return "pulse-lead";
    case MusicInstrument::PulseWide:
      return "pulse-wide";
    case MusicInstrument::TriangleBass:
      return "triangle-bass";
    case MusicInstrument::ChordVoice:
      return "chord";
    case MusicInstrument::ArpVoice:
      return "arp";
    case MusicInstrument::Kick:
      return "kick";
    case MusicInstrument::Snare:
      return "snare";
    case MusicInstrument::HatClosed:
      return "hat-closed";
    case MusicInstrument::HatOpen:
      return "hat-open";
    case MusicInstrument::Accent:
      return "accent";
  }
  return "?";
}

int musicTrackIndex(const char* id) {
  if (id == nullptr) {
    return -1;
  }
  const std::vector<MusicTrackData>& tracks = musicCatalogue();
  for (std::size_t i = 0; i < tracks.size(); ++i) {
    if (std::strcmp(tracks[i].id, id) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int musicTrackCount() {
  return static_cast<int>(musicCatalogue().size());
}

const char* musicTrackId(int index) {
  const std::vector<MusicTrackData>& tracks = musicCatalogue();
  if (index < 0 || static_cast<std::size_t>(index) >= tracks.size()) {
    return "";
  }
  return tracks[static_cast<std::size_t>(index)].id;
}

// ---------------------------------------------------------------------------
// Validation (MUSIC.md: reject invalid scores before playback).
// ---------------------------------------------------------------------------

bool validateMusicTrack(const MusicTrackData& track, int trackIndex,
                        MusicValidationReport* report) {
  MusicValidationReport out;
  out.ok = false;
  out.trackIndex = trackIndex;
  out.trackId = track.id;

  const std::uint32_t totalTicks = track.totalTicks();
  if (track.bpm < 40 || track.bpm > 220) {
    out.error = "tempo outside 40..220 beats per minute";
    if (report != nullptr) {
      *report = out;
    }
    return false;
  }
  if (track.bars <= 0 || totalTicks == 0) {
    out.error = "empty arrangement";
    if (report != nullptr) {
      *report = out;
    }
    return false;
  }
  if (track.notes.empty()) {
    out.error = "no notes";
    if (report != nullptr) {
      *report = out;
    }
    return false;
  }

  // Held-note bookkeeping for the voice maximum: one entry per note start
  // and one per note end, swept in time order.
  struct Edge {
    std::uint32_t tick;
    int change;
    int layerMask;
  };
  std::vector<Edge> edges;
  edges.reserve(track.notes.size() * 2);
  std::uint32_t previousStart = 0;
  for (std::size_t i = 0; i < track.notes.size(); ++i) {
    const MusicNote& note = track.notes[i];
    if (note.startTick < previousStart) {
      out.error = "notes not sorted by start tick";
      out.tick = note.startTick;
      out.note = static_cast<int>(i);
      if (report != nullptr) {
        *report = out;
      }
      return false;
    }
    previousStart = note.startTick;
    if (note.layerMask == 0) {
      out.error = "note belongs to no intensity layer";
      out.tick = note.startTick;
      out.note = static_cast<int>(i);
      if (report != nullptr) {
        *report = out;
      }
      return false;
    }
    if (note.velocity == 0 || note.velocity > 127) {
      out.error = "velocity outside 1..127";
      out.tick = note.startTick;
      out.note = static_cast<int>(i);
      if (report != nullptr) {
        *report = out;
      }
      return false;
    }
    if (static_cast<int>(note.instrument) >= kMusicInstrumentCount) {
      out.error = "unknown instrument id";
      out.tick = note.startTick;
      out.note = static_cast<int>(i);
      if (report != nullptr) {
        *report = out;
      }
      return false;
    }
    if (note.lengthTicks == 0) {
      out.error = "zero-length note";
      out.tick = note.startTick;
      out.note = static_cast<int>(i);
      if (report != nullptr) {
        *report = out;
      }
      return false;
    }
    if (note.startTick + note.lengthTicks > totalTicks) {
      out.error = "note crosses the loop boundary";
      out.tick = note.startTick;
      out.note = static_cast<int>(i);
      if (report != nullptr) {
        *report = out;
      }
      return false;
    }
    const bool pitched =
        kInstruments[static_cast<int>(note.instrument)].table >= 0;
    if (pitched && (note.pitch < 21 || note.pitch > 108)) {
      out.error = "pitched note outside MIDI 21..108";
      out.tick = note.startTick;
      out.note = static_cast<int>(i);
      if (report != nullptr) {
        *report = out;
      }
      return false;
    }
    edges.push_back(Edge{note.startTick, 1, note.layerMask});
    edges.push_back(
        Edge{note.startTick + note.lengthTicks, -1, note.layerMask});
  }
  std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
    if (a.tick != b.tick) {
      return a.tick < b.tick;
    }
    return a.change < b.change;  // ends leave before starts enter
  });
  for (int intensity = 0; intensity < kMusicIntensityCount; ++intensity) {
    const int bit = 1 << intensity;
    int held = 0;
    int peak = 0;
    for (const Edge& edge : edges) {
      if ((edge.layerMask & bit) == 0) {
        continue;
      }
      held += edge.change;
      peak = std::max(peak, held);
    }
    out.maxVoices = std::max(out.maxVoices, peak);
  }
  if (out.maxVoices > kMusicMaxVoices) {
    out.error = "held voices exceed the engine budget";
    if (report != nullptr) {
      *report = out;
    }
    return false;
  }
  out.ok = true;
  if (report != nullptr) {
    *report = out;
  }
  return true;
}

// ---------------------------------------------------------------------------
// MusicEngine.
// ---------------------------------------------------------------------------

MusicEngine::MusicEngine() {
  // The queue and gain cells are the only shared state, and every one of
  // them must stay lock-free: the audio callback touches them on a real-time
  // thread (MUSIC.md, thread communication).
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                "the music gain and queue cells must be lock-free");
}

void MusicEngine::initialize(int sampleRate) {
  sampleRate_ = sampleRate;
  initialized_ = sampleRate > 0;

  // Band-limited wave tables (MUSIC.md, instruments): additive sums whose
  // harmonic counts keep the top partial of each instrument's register under
  // the Nyquist frequency, so pulse voices stay bright without harsh folds.
  const double kPi = 3.14159265358979323846;
  struct TableSpec {
    double duty;   // pulse duty cycle; negative selects a triangle
    int harmonics;
  };
  const TableSpec specs[5] = {
      {0.25, 12},   // PulseLead
      {0.50, 8},    // PulseWide
      {-1.0, 16},   // TriangleBass
      {-1.0, 6},    // ChordVoice (dark)
      {0.125, 14},  // ArpVoice
  };
  double raw[6][2048];
  double peaks[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  for (int table = 0; table < 5; ++table) {
    for (int i = 0; i < 2048; ++i) {
      const double t = static_cast<double>(i) / 2048.0;
      double sum = 0.0;
      if (specs[table].duty < 0.0) {
        for (int n = 1; n <= specs[table].harmonics; n += 2) {
          sum += std::sin(2.0 * kPi * n * t) / (n * n);
        }
        // The odd-harmonic series of the triangle peaks near pi/2; scale it
        // back to full scale through its own closed-form peak.
        raw[table][i] = (8.0 / (kPi * kPi)) * sum / 0.9994;
      } else {
        for (int n = 1; n <= specs[table].harmonics; ++n) {
          sum += std::sin(kPi * n * specs[table].duty) *
                 std::cos(2.0 * kPi * n * t) / n;
        }
        raw[table][i] = sum;
      }
      peaks[table] = std::max(peaks[table], std::fabs(raw[table][i]));
    }
  }
  for (int i = 0; i < 2048; ++i) {
    // The sine table: percussion tones and ring carriers read it.
    raw[5][i] = std::sin(2.0 * kPi * static_cast<double>(i) / 2048.0);
  }
  for (int table = 0; table < 6; ++table) {
    // Pulses normalize to full scale against their own partial-sum peak;
    // the triangle keeps its closed-form amplitude (about 0.74 of scale),
    // which is the darker level the bass and chord voices are tuned for.
    double scale = 1.0;
    if (specs[table < 5 ? table : 0].duty >= 0.0 && table < 5) {
      scale = peaks[table] > 0.0 ? 1.0 / peaks[table] : 1.0;
    }
    for (int i = 0; i < 2048; ++i) {
      waveTables_[table][i] =
          clamp16(static_cast<std::int32_t>(32767.0 * raw[table][i] * scale));
    }
  }

  // The noise table: one deterministic 8192-sample sequence from a 16-bit
  // Galois LFSR (taps 0xB400, a maximal-length sequence), so percussion
  // renders identically on every machine and every run and the sequence
  // itself carries no direct-current bias.
  std::uint16_t lfsr = 0x729A;  // fixed seed: the soundtrack has no random
  for (int i = 0; i < 8192; ++i) {
    const unsigned bit = lfsr & 1u;
    lfsr >>= 1;
    if (bit != 0u) {
      lfsr ^= 0xB400u;
    }
    noiseTable_[i] = static_cast<std::int16_t>(lfsr);
  }

  // Pitch increments in 32.32 per MIDI note, and the velocity curve (square
  // law, so the quiet end of the range is musically usable).
  for (int midi = 0; midi < 128; ++midi) {
    const double hz = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    const double increment = hz * 4294967296.0 / sampleRate;
    pitchIncrement_[midi] = static_cast<std::int32_t>(
        std::min(increment, 1073741824.0));
  }
  for (int velocity = 0; velocity < 128; ++velocity) {
    const double fraction = static_cast<double>(velocity) / 127.0;
    velocityGainQ16_[velocity] =
        static_cast<std::int32_t>(fraction * fraction * 65536.0);
  }

  gainRampStep_ = static_cast<std::uint32_t>(
      65536 / std::max(1, millisecondsToSamples(15, sampleRate)) + 1);

  // A fresh engine rests silent with no transport.
  transportRunning_ = false;
  frozen_ = false;
  trackIndex_ = 0;
  intensity_ = 0;
  phase_ = 0;
  noteCursor_ = 0;
  pendingPlay_ = false;
  hasPendingIntensity_ = false;
  preBoundaryFadeStarted_ = false;
  sceneGain_ = 0;
  sceneGainTarget_ = 0;
  sceneGainStep_ = 1;
  stopWhenSilent_ = false;
  freezeWhenSilent_ = false;
  lastTick_ = 0;
  for (Voice& voice : voices_) {
    voice.active = false;
  }
  publishedLoopCount_.store(0, std::memory_order_relaxed);
  publishedVoiceHighWater_.store(0, std::memory_order_relaxed);
}

bool MusicEngine::tryPushCommand(const MusicCommand& command) {
  const std::uint32_t tail = queueTail_.load(std::memory_order_relaxed);
  const std::uint32_t head = queueHead_.load(std::memory_order_acquire);
  if (tail - head >= kQueueSlots) {
    commandsRejected_.fetch_add(1, std::memory_order_relaxed);
    return false;  // the caller retries; scheduled commands coalesce anyway
  }
  queue_[tail & (kQueueSlots - 1)] = command;
  queueTail_.store(tail + 1, std::memory_order_release);
  commandsPushed_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

void MusicEngine::publishMusicGain(std::uint32_t musicGainQ16) {
  musicGainTarget_.store(musicGainQ16, std::memory_order_relaxed);
}

void MusicEngine::publishMasterGain(std::uint32_t masterGainQ16) {
  masterGainTarget_.store(masterGainQ16, std::memory_order_relaxed);
}

void MusicEngine::drainCommands() {
  const std::uint32_t tail = queueTail_.load(std::memory_order_acquire);
  while (queueHead_.load(std::memory_order_relaxed) != tail) {
    const MusicCommand command =
        queue_[queueHead_.load(std::memory_order_relaxed) & (kQueueSlots - 1)];
    queueHead_.store(queueHead_.load(std::memory_order_relaxed) + 1,
                     std::memory_order_release);
    commandsDrained_.fetch_add(1, std::memory_order_relaxed);
    applyImmediate(command);
  }
}

void MusicEngine::applyImmediate(const MusicCommand& command) {
  switch (command.type) {
    case MusicCommandType::Play:
      // The latest destination wins (MUSIC.md: coalesce repeated requests).
      pendingPlay_ = true;
      pendingPlayCommand_ = command;
      preBoundaryFadeStarted_ = false;
      if (!transportRunning_) {
        switchTrack(command.track, command.intensity, command.fadeMs);
      }
      break;
    case MusicCommandType::Stop:
      pendingPlay_ = false;
      freezeWhenSilent_ = false;
      stopWhenSilent_ = true;
      sceneGainTarget_ = 0;
      sceneGainStep_ = std::max<std::uint32_t>(
          1, 65536 / std::max(1, millisecondsToSamples(command.fadeMs,
                                                       sampleRate_)));
      break;
    case MusicCommandType::Pause:
      if (frozen_) {
        break;
      }
      stopWhenSilent_ = false;
      freezeWhenSilent_ = true;
      sceneGainTarget_ = 0;
      sceneGainStep_ = std::max<std::uint32_t>(
          1, 65536 / std::max(1, millisecondsToSamples(command.fadeMs,
                                                       sampleRate_)));
      break;
    case MusicCommandType::Resume:
      if (!transportRunning_) {
        break;  // nothing to resume: the scene policy owns the next Play
      }
      frozen_ = false;
      freezeWhenSilent_ = false;
      stopWhenSilent_ = false;
      sceneGainTarget_ = 65536;
      sceneGainStep_ = std::max<std::uint32_t>(
          1, 65536 / std::max(1, millisecondsToSamples(command.fadeMs,
                                                       sampleRate_)));
      break;
    case MusicCommandType::SetIntensity:
      hasPendingIntensity_ = true;
      pendingIntensityValue_ = command.intensity;
      break;
  }
}

void MusicEngine::switchTrack(int trackIndex, int intensity,
                              std::uint16_t fadeMs) {
  const std::vector<MusicTrackData>& tracks = musicCatalogue();
  if (trackIndex < 0 || static_cast<std::size_t>(trackIndex) >= tracks.size()) {
    return;
  }
  // Retire the old track's voices with a fast release, never a cut.
  for (Voice& voice : voices_) {
    if (voice.active) {
      releaseVoice(&voice, true);
    }
  }
  trackIndex_ = trackIndex;
  intensity_ = static_cast<std::uint8_t>(intensity);
  const MusicTrackData& track = tracks[static_cast<std::size_t>(trackIndex)];
  const std::uint64_t ticksPerSecond =
      static_cast<std::uint64_t>(track.bpm) * kMusicTicksPerQuarter;
  phaseIncrement_ = static_cast<std::uint32_t>((ticksPerSecond << 32) /
                                               (60ull * sampleRate_));
  samplesPerTickQ8_ = static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(sampleRate_) << 8) * 60 /
      ticksPerSecond);
  phase_ &= 0xFFFFFFFFull;  // keep the fractional part: no phase jump
  noteCursor_ = 0;
  lastTick_ = 0xFFFFFFFFu;  // the first advance reports tick 0 itself
  transportRunning_ = true;
  frozen_ = false;
  stopWhenSilent_ = false;
  freezeWhenSilent_ = false;
  pendingPlay_ = false;
  hasPendingIntensity_ = false;
  preBoundaryFadeStarted_ = false;
  publishedLoopCount_.store(0, std::memory_order_relaxed);
  publishedBar_.store(0, std::memory_order_relaxed);
  publishedTrack_.store(static_cast<std::uint8_t>(trackIndex),
                        std::memory_order_relaxed);
  publishedIntensity_.store(intensity_, std::memory_order_relaxed);
  publishedRunning_.store(true, std::memory_order_relaxed);
  publishedFrozen_.store(false, std::memory_order_relaxed);
  publishedPending_.store(false, std::memory_order_relaxed);
  sceneGainTarget_ = 65536;
  sceneGainStep_ = std::max<std::uint32_t>(
      1, 65536 / std::max(1, millisecondsToSamples(fadeMs, sampleRate_)));
}

void MusicEngine::releaseVoice(Voice* voice, bool fast) {
  voice->releasing = true;
  const int ms = fast ? 5 : kInstruments[static_cast<int>(voice->instrument)]
                            .releaseMs;
  voice->releaseStep = std::max<std::int32_t>(
      1, 32768 / std::max(1, millisecondsToSamples(ms, sampleRate_)));
}

void MusicEngine::releaseLayer(std::uint8_t intensityBit) {
  for (Voice& voice : voices_) {
    if (voice.active && (voice.layerMask & intensityBit) == 0) {
      releaseVoice(&voice, false);
    }
  }
}

MusicEngine::Voice* MusicEngine::acquireVoice(std::uint8_t layerMask) {
  Voice* stolen = nullptr;
  std::int32_t stolenLevel = 0;
  Voice* optional = nullptr;
  std::int32_t optionalLevel = 0;
  for (Voice& voice : voices_) {
    if (!voice.active) {
      return &voice;
    }
    const bool optionalVoice = (voice.layerMask & kMusicLayerLow) == 0;
    Voice** slot = optionalVoice ? &optional : &stolen;
    std::int32_t* level = optionalVoice ? &optionalLevel : &stolenLevel;
    if (*slot == nullptr || voice.envelope < *level) {
      *slot = &voice;
      *level = voice.envelope;
    }
  }
  // The quietest optional voice goes first; only a budget with no optional
  // voice left steals a low-layer one (MUSIC.md, thread communication).
  if (optional != nullptr) {
    releaseVoice(optional, true);
    return optional;
  }
  if (stolen != nullptr) {
    releaseVoice(stolen, true);
    return stolen;
  }
  (void)layerMask;
  return nullptr;
}

void MusicEngine::startNote(const MusicNote& note) {
  Voice* voice = acquireVoice(note.layerMask);
  if (voice == nullptr) {
    return;
  }
  const InstrumentParams& params =
      kInstruments[static_cast<int>(note.instrument)];
  voice->instrument = note.instrument;
  voice->layerMask = note.layerMask;
  voice->pitch = note.pitch;
  voice->releasing = false;
  voice->attacking = true;
  voice->envelope = 0;
  voice->noiseCursor = static_cast<std::uint16_t>(note.startTick & 0x7FFF);
  voice->noisePrevious = 0;

  const std::int32_t velocityGain =
      velocityGainQ16_[std::min<int>(127, note.velocity)];
  voice->gainQ16 =
      (static_cast<std::int32_t>(velocityGain) * params.gainQ16) >> 16;

  const int attackSamples =
      std::max(1, millisecondsToSamples(params.attackMs, sampleRate_));
  voice->attackStep = 32768 / attackSamples + 1;
  const int decaySamples = std::max(
      1, millisecondsToSamples(
             params.table >= 0 ? params.decayMs
                               : kPercussionDecayMs[static_cast<int>(
                                     note.instrument)],
             sampleRate_));
  voice->decayStep =
      std::max<std::int32_t>(1, (32768 - params.sustainPpm * 32) /
                                    decaySamples);
  voice->sustainLevel = params.sustainPpm * 32;  // ppm to 0..32768
  voice->releaseStep = std::max<std::int32_t>(
      1, 32768 / std::max(1, millisecondsToSamples(params.releaseMs,
                                                   sampleRate_)));
  voice->heldSamples = 0;

  const std::vector<MusicTrackData>& tracks = musicCatalogue();
  const MusicTrackData& track =
      tracks[static_cast<std::size_t>(trackIndex_)];
  voice->holdSamples = static_cast<std::uint32_t>(
      static_cast<std::uint64_t>(note.lengthTicks) * sampleRate_ * 60 /
      (static_cast<std::uint64_t>(track.bpm) * kMusicTicksPerQuarter));

  voice->phase = 0;
  voice->sweepRemaining = 0;
  voice->sweepStep = 0;
  voice->phaseIncrement = 0;
  if (note.instrument == MusicInstrument::Kick) {
    const double start = params.kickStartHz * 4294967296.0 / sampleRate_;
    const double end = params.kickEndHz * 4294967296.0 / sampleRate_;
    const int sweepSamples =
        std::max(1, millisecondsToSamples(params.kickSweepMs, sampleRate_));
    voice->phaseIncrement = static_cast<std::int32_t>(start);
    voice->sweepStep =
        static_cast<std::int32_t>((end - start) / sweepSamples);
    voice->sweepRemaining = static_cast<std::int32_t>(sweepSamples);
  } else if (params.table >= 0) {
    const int midi = std::min(108, std::max(21, static_cast<int>(note.pitch)));
    voice->phaseIncrement = pitchIncrement_[midi];
  } else {
    const double hz = params.toneHz * 4294967296.0 / sampleRate_;
    voice->phaseIncrement = static_cast<std::int32_t>(hz);
  }
  voice->active = true;
}

std::int32_t MusicEngine::tableSample(std::uint32_t phase, int table) const {
  const std::array<std::int16_t, 2048>& samples = waveTables_[table];
  const std::uint32_t index = phase >> 21;          // top 11 bits
  const std::uint32_t fraction = (phase >> 5) & 0xFFFF;
  // Signed 64-bit math: the table holds signed samples and the fraction is
  // unsigned, so a 32-bit mix would wrap every negative half-cycle into a
  // large positive number.
  const std::int64_t a = samples[index & 2047];
  const std::int64_t b = samples[(index + 1) & 2047];
  return static_cast<std::int32_t>(
      (a * (65536 - fraction) + b * fraction) >> 16);
}

std::int32_t MusicEngine::voiceSample(Voice* voice) {
  const InstrumentParams& params =
      kInstruments[static_cast<int>(voice->instrument)];
  std::int32_t sample = 0;

  if (params.table >= 0) {
    voice->phase += static_cast<std::uint32_t>(voice->phaseIncrement);
    sample = tableSample(voice->phase, params.table);
  } else if (voice->instrument == MusicInstrument::Kick) {
    if (voice->sweepRemaining > 0) {
      voice->phaseIncrement += voice->sweepStep;
      --voice->sweepRemaining;
    }
    voice->phase += static_cast<std::uint32_t>(voice->phaseIncrement);
    sample = tableSample(voice->phase, 5);
  } else if (voice->instrument == MusicInstrument::Snare) {
    voice->phase += static_cast<std::uint32_t>(voice->phaseIncrement);
    const std::int32_t tone = tableSample(voice->phase, 5);
    const std::int16_t noise =
        noiseTable_[voice->noiseCursor & 8191u];
    ++voice->noiseCursor;
    const std::int32_t high =
        (static_cast<std::int32_t>(noise) - voice->noisePrevious) >> 1;
    voice->noisePrevious = noise;
    sample = (tone * params.toneWeightQ16 +
              high * params.noiseWeightQ16) >> 16;
  } else if (voice->instrument == MusicInstrument::Accent) {
    voice->phase += static_cast<std::uint32_t>(voice->phaseIncrement);
    const std::int32_t ring = tableSample(voice->phase, 5);
    const std::int16_t noise = noiseTable_[voice->noiseCursor & 8191u];
    ++voice->noiseCursor;
    const std::int32_t high =
        (static_cast<std::int32_t>(noise) - voice->noisePrevious) >> 1;
    voice->noisePrevious = noise;
    sample = (high * ring) >> 15;
  } else {  // hats
    const std::int16_t noise = noiseTable_[voice->noiseCursor & 8191u];
    ++voice->noiseCursor;
    const std::int32_t high =
        (static_cast<std::int32_t>(noise) - voice->noisePrevious) >> 1;
    voice->noisePrevious = noise;
    sample = high;
  }

  // The envelope: a short attack, a decay to the sustain level, and a
  // release that starts when the note's length is spent, when its layer
  // leaves the active map, or when the voice is stolen. A percussion voice
  // whose sustain is zero ends itself when its body has decayed, so a long
  // score note cannot pin a voice slot silent.
  ++voice->heldSamples;
  if (!voice->releasing && voice->heldSamples >= voice->holdSamples) {
    voice->releasing = true;
  }
  if (voice->releasing) {
    voice->envelope -= voice->releaseStep;
    if (voice->envelope <= 0) {
      voice->envelope = 0;
      voice->active = false;
      return 0;
    }
  } else if (voice->attacking) {
    voice->envelope += voice->attackStep;
    if (voice->envelope >= 32768) {
      voice->envelope = 32768;
      voice->attacking = false;
    }
  } else {
    voice->envelope -= voice->decayStep;
    if (voice->envelope <= voice->sustainLevel) {
      voice->envelope = voice->sustainLevel;
      if (voice->sustainLevel == 0) {
        voice->active = false;
        return 0;
      }
    }
  }
  const std::int32_t shaped = (sample * voice->envelope) >> 15;
  return (static_cast<std::int64_t>(shaped) * voice->gainQ16) >> 16;
}

void MusicEngine::onTickBoundary(std::uint32_t tick) {
  const std::vector<MusicTrackData>& tracks = musicCatalogue();
  const MusicTrackData& track = tracks[static_cast<std::size_t>(trackIndex_)];
  const std::uint32_t totalTicks = track.totalTicks();

  if (tick >= totalTicks) {
    // The loop boundary: wrap the phase, keep the fraction, and count the
    // completed arrangement. Sounding voices ring into the next loop, which
    // is what keeps the beat continuous (MUSIC.md, arrangement contract).
    tick -= totalTicks;
    phase_ -= static_cast<std::uint64_t>(totalTicks) << 32;
    noteCursor_ = 0;
    publishedLoopCount_.fetch_add(1, std::memory_order_relaxed);
  }

  const bool barBoundary = tick % kMusicTicksPerBar == 0;
  if (pendingPlay_ && barBoundary) {
    switchTrack(pendingPlayCommand_.track, pendingPlayCommand_.intensity,
                pendingPlayCommand_.fadeMs);
    return;
  }
  if (hasPendingIntensity_ && tick % kMusicTicksPerPhrase == 0) {
    intensity_ = pendingIntensityValue_;
    hasPendingIntensity_ = false;
    publishedIntensity_.store(intensity_, std::memory_order_relaxed);
    releaseLayer(static_cast<std::uint8_t>(1u << intensity_));
  }

  const std::uint8_t layerBit = static_cast<std::uint8_t>(1u << intensity_);
  const std::vector<MusicNote>& notes = track.notes;
  while (noteCursor_ < notes.size() &&
         notes[noteCursor_].startTick <= tick) {
    if (notes[noteCursor_].startTick == tick &&
        (notes[noteCursor_].layerMask & layerBit) != 0) {
      startNote(notes[noteCursor_]);
    }
    ++noteCursor_;
  }
  publishedBar_.store(tick / kMusicTicksPerBar, std::memory_order_relaxed);

  if (pendingPlay_ && !preBoundaryFadeStarted_) {
    std::uint32_t ticksToBoundary = kMusicTicksPerBar - tick % kMusicTicksPerBar;
    const std::uint32_t fadeSamples = static_cast<std::uint32_t>(
        millisecondsToSamples(pendingPlayCommand_.fadeMs, sampleRate_));
    const std::uint32_t samplesToBoundary =
        (ticksToBoundary * samplesPerTickQ8_) >> 8;
    if (samplesToBoundary <= fadeSamples) {
      preBoundaryFadeStarted_ = true;
      sceneGainTarget_ = 0;
      sceneGainStep_ = std::max<std::uint32_t>(
          1, 65536 / std::max<std::size_t>(1, fadeSamples));
    }
  }
}

void MusicEngine::renderAdd(std::int32_t* dst, std::size_t frames) {
  if (!initialized_ || dst == nullptr) {
    return;
  }
  drainCommands();

  const std::uint32_t musicTarget =
      musicGainTarget_.load(std::memory_order_relaxed);
  const std::uint32_t masterTarget =
      masterGainTarget_.load(std::memory_order_relaxed);
  const std::vector<MusicTrackData>& tracks = musicCatalogue();

  std::uint32_t activeHighWater =
      publishedVoiceHighWater_.load(std::memory_order_relaxed);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    // Gains ramp toward their targets, one step per sample, so a published
    // change lands as a short fade on both buses (MUSIC.md, mixing). The
    // falls clamp at the target: an unsigned step below zero would wrap to
    // a huge gain instead of silence.
    if (musicGain_ < musicTarget) {
      musicGain_ = std::min<std::uint32_t>(musicTarget,
                                           musicGain_ + gainRampStep_);
    } else if (musicGain_ > musicTarget) {
      musicGain_ = musicGain_ > gainRampStep_
                       ? std::max(musicTarget, musicGain_ - gainRampStep_)
                       : musicTarget;
    }
    if (masterGain_ < masterTarget) {
      masterGain_ = std::min<std::uint32_t>(masterTarget,
                                            masterGain_ + gainRampStep_);
    } else if (masterGain_ > masterTarget) {
      masterGain_ = masterGain_ > gainRampStep_
                        ? std::max(masterTarget, masterGain_ - gainRampStep_)
                        : masterTarget;
    }
    if (sceneGain_ < sceneGainTarget_) {
      sceneGain_ = std::min(sceneGainTarget_, sceneGain_ + sceneGainStep_);
    } else if (sceneGain_ > sceneGainTarget_) {
      sceneGain_ = sceneGain_ > sceneGainStep_
                       ? std::max(sceneGainTarget_, sceneGain_ - sceneGainStep_)
                       : sceneGainTarget_;
    }
    if (sceneGain_ == 0) {
      if (stopWhenSilent_ && transportRunning_) {
        transportRunning_ = false;
        stopWhenSilent_ = false;
        publishedRunning_.store(false, std::memory_order_relaxed);
        for (Voice& voice : voices_) {
          if (voice.active) {
            releaseVoice(&voice, true);
          }
        }
      } else if (freezeWhenSilent_ && transportRunning_ && !frozen_) {
        frozen_ = true;
        freezeWhenSilent_ = false;
      }
    }

    if (transportRunning_ && !frozen_ &&
        static_cast<std::size_t>(trackIndex_) < tracks.size()) {
      phase_ += phaseIncrement_;
      const std::uint32_t tick = static_cast<std::uint32_t>(phase_ >> 32);
      if (tick != lastTick_) {
        lastTick_ = tick;
        onTickBoundary(tick);
      }
    }

    std::int32_t mix = 0;
    std::uint32_t active = 0;
    for (Voice& voice : voices_) {
      if (voice.active) {
        mix += voiceSample(&voice);
        ++active;
      }
    }
    if (active > activeHighWater) {
      activeHighWater = active;
    }
    std::int64_t bus =
        (static_cast<std::int64_t>(mix) * kMusicBusTrimQ16) >> 16;
    bus = (bus * sceneGain_) >> 16;
    bus = (bus * musicGain_) >> 16;
    bus = (bus * masterGain_) >> 16;
    dst[frame] += static_cast<std::int32_t>(bus);
  }
  publishedActive_.store(static_cast<std::uint8_t>(activeHighWater),
                         std::memory_order_relaxed);
  publishedVoiceHighWater_.store(activeHighWater,
                                 std::memory_order_relaxed);
  publishedFrozen_.store(frozen_, std::memory_order_relaxed);
  publishedPending_.store(pendingPlay_, std::memory_order_relaxed);
}

MusicSnapshot MusicEngine::snapshot() const {
  MusicSnapshot out;
  out.track = publishedTrack_.load(std::memory_order_relaxed);
  out.intensity = publishedIntensity_.load(std::memory_order_relaxed);
  out.transportRunning = publishedRunning_.load(std::memory_order_relaxed);
  out.frozen = publishedFrozen_.load(std::memory_order_relaxed);
  out.pendingSwitch = publishedPending_.load(std::memory_order_relaxed);
  out.bar = publishedBar_.load(std::memory_order_relaxed);
  out.loopCount = publishedLoopCount_.load(std::memory_order_relaxed);
  const std::vector<MusicTrackData>& tracks = musicCatalogue();
  if (static_cast<std::size_t>(out.track) < tracks.size()) {
    out.totalBars = static_cast<std::uint32_t>(
        tracks[static_cast<std::size_t>(out.track)].bars);
  }
  out.activeVoices = publishedActive_.load(std::memory_order_relaxed);
  return out;
}

std::uint32_t MusicEngine::voiceHighWater() const {
  return publishedVoiceHighWater_.load(std::memory_order_relaxed);
}

void MusicEngine::resetVoiceStats() {
  publishedVoiceHighWater_.store(0, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// The offline renderer and the WAV writer.
// ---------------------------------------------------------------------------

MusicOfflineRenderer::MusicOfflineRenderer(int sampleRate) {
  engine_.initialize(sampleRate);
  engine_.publishMusicGain(65536);
  engine_.publishMasterGain(65536);
}

void MusicOfflineRenderer::play(int trackIndex, MusicIntensity intensity) {
  MusicCommand command;
  command.type = MusicCommandType::Play;
  command.track = static_cast<std::uint8_t>(trackIndex);
  command.intensity = static_cast<std::uint8_t>(intensity);
  engine_.tryPushCommand(command);
}

void MusicOfflineRenderer::stop(std::uint16_t fadeMs) {
  MusicCommand command;
  command.type = MusicCommandType::Stop;
  command.fadeMs = fadeMs;
  engine_.tryPushCommand(command);
}

void MusicOfflineRenderer::pause() {
  MusicCommand command;
  command.type = MusicCommandType::Pause;
  engine_.tryPushCommand(command);
}

void MusicOfflineRenderer::resume() {
  MusicCommand command;
  command.type = MusicCommandType::Resume;
  engine_.tryPushCommand(command);
}

void MusicOfflineRenderer::setIntensity(MusicIntensity intensity) {
  MusicCommand command;
  command.type = MusicCommandType::SetIntensity;
  command.intensity = static_cast<std::uint8_t>(intensity);
  engine_.tryPushCommand(command);
}

void MusicOfflineRenderer::render(std::int16_t* out, std::size_t frames) {
  if (out == nullptr) {
    return;
  }
  scratch_.assign(frames, 0);
  engine_.renderAdd(scratch_.data(), frames);
  for (std::size_t i = 0; i < frames; ++i) {
    out[i] = clamp16(scratch_[i]);
  }
}

bool writeMusicWavFile(const std::string& path,
                       const std::vector<std::int16_t>& samples,
                       int sampleRate) {
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    return false;
  }
  const std::uint32_t dataBytes =
      static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
  const std::uint32_t rate = static_cast<std::uint32_t>(sampleRate);
  const auto writeBytes = [file](const void* data, std::size_t size) {
    return std::fwrite(data, 1, size, file) == size;
  };
  bool ok = writeBytes("RIFF", 4) &&
            wavWrite32(file, 36u + dataBytes) &&
            writeBytes("WAVEfmt ", 8) &&
            wavWrite32(file, 16u) &&   // size of the format block
            wavWrite16(file, 1u) &&    // PCM
            wavWrite16(file, 1u) &&    // one channel
            wavWrite32(file, rate) &&
            wavWrite32(file, rate * 2u) &&  // byte rate
            wavWrite16(file, 2u) &&    // block align
            wavWrite16(file, 16u) &&   // bits per sample
            writeBytes("data", 4) &&
            wavWrite32(file, dataBytes);
  if (ok && !samples.empty()) {
    ok = writeBytes(samples.data(), samples.size() * sizeof(std::int16_t));
  }
  if (std::fflush(file) != 0) {
    ok = false;
  }
  if (std::fclose(file) != 0) {
    ok = false;
  }
  return ok;
}

}  // namespace stackfall
