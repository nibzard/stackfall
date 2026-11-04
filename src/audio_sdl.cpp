// Synthesized sound effects, task T013 (specification section 6.3). See
// include/stackfall/audio_sdl.hpp for the module contract.
//
// Everything audible comes from the cue table below: each cue is a short list
// of segments, and each segment sweeps one frequency, mixes the three
// waveforms, and decays on its own. The renderer runs on integer phase
// accumulators and integer envelopes only, so one cue at one sample rate is
// the same byte sequence on every machine and every build, which is what the
// headless suite pins.
//
// Failure handling follows section 6.3: audio is secondary. open() may find no
// subsystem or no device, and the only cost is silence - every other call on a
// closed driver counts and returns, so a session without audio behaves exactly
// like a muted one apart from the log line.

#include "stackfall/audio_sdl.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace stackfall {
namespace {

// One 64-entry quarter-wave sine table, rounded from 32767 * sin(pi/2 * i/64).
// The table is source-defined data like the glyph table of section 6.2, not an
// asset: it is compiled in, and nothing reads it from disk.
constexpr std::int16_t kSineQuarter[64] = {
    0,     804,   1608,  2410,  3212,  4011,  4808,  5602,
    6393,  7179,  7962,  8739,  9512,  10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757};

// One straight section of one cue. Frequencies sit in hundredths of a hertz so
// the sweep stays exact in integers, and the three weights form the timbre:
// their weighted mean is the segment's wave. `amplitude` is the segment's peak
// and `decayTenThousand` its per-sample decay factor in ten-thousandths, so
// 9980 decays to 0.998 of the level each sample.
struct Segment {
  int durationMs;
  int startHz100;
  int endHz100;
  int weightSquare;
  int weightTriangle;
  int weightSine;
  int amplitude;
  int decayTenThousand;
};

// The five cues of section 6.3. Durations stay short: a cue is feedback, not
// music, and MoveLand can fire on every auto-repeat step.
//
//   MoveLand       a soft low tick-thud that falls as it decays
//   Rotate         a short upward blip in two steps
//   Clear          a two-note rise
//   FourLineClear  a four-note fanfare, the longest gameplay cue
//   GameOver       a long descending slide
//
// The amplitudes sit well above half scale because the waveform mix is a
// weighted mean: the square and the sine peak in opposite half-cycles, so a
// mix that weights both lands around half of the sum of the parts. The mixer
// saturates, so a loud cue cannot damage anything but its own headroom.
constexpr int kMaxSegments = 4;

struct CueSpec {
  int durationMs;
  int segmentCount;
  Segment segments[kMaxSegments];
};

constexpr CueSpec kCueSpecs[kSoundCueCount] = {
    // MoveLand: 55 ms.
    {55, 1,
     {{55, 30000, 10500, 2, 3, 0, 16000, 9985}}},
    // Rotate: 45 ms in two rising steps.
    {45, 2,
     {{22, 48000, 62000, 0, 1, 3, 9000, 9960},
      {23, 62000, 78000, 0, 1, 3, 7400, 9965}}},
    // Clear: 200 ms, two notes.
    {200, 2,
     {{85, 66000, 66000, 1, 0, 3, 19000, 9992},
      {115, 88000, 88000, 1, 0, 3, 17000, 9992}}},
    // FourLineClear: 380 ms, four rising notes.
    {380, 4,
     {{75, 52300, 52300, 2, 0, 3, 21000, 9993},
      {75, 65900, 65900, 2, 0, 3, 20000, 9993},
      {75, 78400, 78400, 2, 0, 3, 20000, 9993},
      {155, 104700, 104700, 3, 0, 3, 22000, 9993}}},
    // GameOver: 800 ms, one long slide down. The slowest decay of the set,
    // because the whole slide stays audible instead of dying away.
    {800, 3,
     {{200, 39200, 32000, 2, 3, 0, 15000, 9998},
      {250, 32000, 19600, 2, 3, 0, 14000, 9998},
      {350, 19600, 6500, 1, 4, 0, 13500, 9998}}},
};

// Attack and closing fade of every cue, in milliseconds. Both ends of a cue
// reach exact silence, so a cue cannot click when it starts or ends.
constexpr int kCueAttackMs = 1;
constexpr int kCueFadeMs = 4;

int millisecondsToFrames(int ms, int sampleRate) {
  return (ms * sampleRate + 999) / 1000;
}

// The three waveforms of one phase, each at full scale in int16 range.
int squareOf(std::uint32_t phase) {
  return (phase & 0x80000000u) != 0u ? 32767 : -32768;
}

int triangleOf(std::uint32_t phase) {
  const int p = static_cast<int>(phase >> 16);  // 0..65535 over one cycle
  if (p < 16384) {
    return p * 2;
  }
  if (p < 49152) {
    return 65536 - p * 2;
  }
  return p * 2 - 131072;
}

int sineOf(std::uint32_t phase) {
  const int index = static_cast<int>((phase >> 24) & 0x3Fu);
  switch (phase >> 30) {
    case 0u:
      return kSineQuarter[index];
    case 1u:
      return kSineQuarter[63 - index];
    case 2u:
      return -kSineQuarter[index];
    default:
      return -kSineQuarter[63 - index];
  }
}

std::int16_t clampSample(std::int32_t value) {
  if (value > 32767) {
    return 32767;
  }
  if (value < -32768) {
    return -32768;
  }
  return static_cast<std::int16_t>(value);
}

// Both header writers report a failed byte so the WAV export can say so.
bool writeLittleEndian16(std::FILE* file, std::uint16_t value) {
  const unsigned char bytes[2] = {
      static_cast<unsigned char>(value & 0xFFu),
      static_cast<unsigned char>((value >> 8) & 0xFFu)};
  return std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

bool writeLittleEndian32(std::FILE* file, std::uint32_t value) {
  const unsigned char bytes[4] = {
      static_cast<unsigned char>(value & 0xFFu),
      static_cast<unsigned char>((value >> 8) & 0xFFu),
      static_cast<unsigned char>((value >> 16) & 0xFFu),
      static_cast<unsigned char>((value >> 24) & 0xFFu)};
  return std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

}  // namespace

const char* soundCueName(SoundCue cue) {
  switch (cue) {
    case SoundCue::MoveLand:
      return "move/land";
    case SoundCue::Rotate:
      return "rotate";
    case SoundCue::Clear:
      return "clear";
    case SoundCue::FourLineClear:
      return "four";
    case SoundCue::GameOver:
      return "gameover";
  }
  return "?";
}

int soundCueDurationMs(SoundCue cue) {
  return kCueSpecs[static_cast<std::size_t>(cue)].durationMs;
}

void synthesizeCue(SoundCue cue, int sampleRate,
                   std::vector<std::int16_t>* samples) {
  samples->clear();
  if (sampleRate <= 0) {
    return;
  }
  const CueSpec& spec = kCueSpecs[static_cast<std::size_t>(cue)];
  const int totalFrames = millisecondsToFrames(spec.durationMs, sampleRate);
  const int fadeFrames =
      std::max(1, millisecondsToFrames(kCueFadeMs, sampleRate));
  samples->assign(static_cast<std::size_t>(totalFrames), 0);

  // One phase accumulator runs across the whole cue, so a segment boundary
  // changes the pitch without stepping the wave.
  std::uint32_t phase = 0;
  std::size_t frame = 0;
  for (int index = 0; index < spec.segmentCount; ++index) {
    const Segment& segment = spec.segments[index];
    const int segmentFrames =
        millisecondsToFrames(segment.durationMs, sampleRate);
    const int attackFrames =
        std::max(1, millisecondsToFrames(kCueAttackMs, sampleRate));
    const int weightSum =
        segment.weightSquare + segment.weightTriangle + segment.weightSine;
    if (weightSum <= 0) {
      continue;  // a table entry with no waveform stays silent
    }
    std::int32_t envelope = segment.amplitude;
    for (int i = 0; i < segmentFrames && frame < samples->size(); ++i) {
      // The frequency sweeps linearly across the segment.
      const std::int64_t span = segment.endHz100 - segment.startHz100;
      const std::int64_t hz100 =
          segment.startHz100 + span * i / segmentFrames;
      const std::uint64_t increment =
          ((static_cast<std::uint64_t>(hz100) << 32) +
           50u * static_cast<std::uint64_t>(sampleRate)) /
          (100u * static_cast<std::uint64_t>(sampleRate));
      phase += static_cast<std::uint32_t>(increment);

      const std::int64_t square =
          static_cast<std::int64_t>(squareOf(phase)) * segment.weightSquare;
      const std::int64_t triangle =
          static_cast<std::int64_t>(triangleOf(phase)) *
          segment.weightTriangle;
      const std::int64_t sine =
          static_cast<std::int64_t>(sineOf(phase)) * segment.weightSine;
      const std::int64_t wave = (square + triangle + sine) / weightSum;

      // The envelope decays every sample, the attack of the segment ramps the
      // first millisecond up from zero, and the closing fade of the cue lands
      // on exact zero whatever the decay left.
      const std::int64_t decayed =
          static_cast<std::int64_t>(envelope) * segment.decayTenThousand;
      envelope = static_cast<std::int32_t>(decayed / 10000);
      std::int32_t scale = 32768;
      if (i < attackFrames) {
        scale = static_cast<std::int32_t>(32768LL * i / attackFrames);
      }
      const std::size_t framesLeft = samples->size() - frame;
      if (framesLeft <= static_cast<std::size_t>(fadeFrames)) {
        const std::int64_t remaining =
            32768LL * static_cast<std::int64_t>(framesLeft - 1) / fadeFrames;
        scale = static_cast<std::int32_t>(remaining);
      }
      const std::int64_t shaped =
          wave * envelope * scale / (32768LL * 32768LL);
      (*samples)[frame] = clampSample(static_cast<std::int32_t>(shaped));
      ++frame;
    }
  }
}

bool writeWavFile(const std::string& path,
                  const std::vector<std::int16_t>& samples, int sampleRate) {
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
            writeLittleEndian32(file, 36u + dataBytes) &&
            writeBytes("WAVEfmt ", 8) &&
            writeLittleEndian32(file, 16u) &&   // size of the format block
            writeLittleEndian16(file, 1u) &&    // PCM
            writeLittleEndian16(file, 1u) &&    // one channel
            writeLittleEndian32(file, rate) &&
            writeLittleEndian32(file, rate * 2u) &&  // byte rate
            writeLittleEndian16(file, 2u) &&    // block align
            writeLittleEndian16(file, 16u) &&   // bits per sample
            writeBytes("data", 4) &&
            writeLittleEndian32(file, dataBytes);
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

// ---------------------------------------------------------------------------
// Cue detection.
// ---------------------------------------------------------------------------

CueDetector::Snapshot CueDetector::take(const Game& game) {
  Snapshot snapshot;
  snapshot.scene = game.scene();
  snapshot.hasActive = game.hasActivePiece();
  snapshot.piece = game.activePiece();
  snapshot.hasHold = game.hasHoldPiece();
  snapshot.holdId = game.holdPieceId();
  snapshot.holdLocked = game.holdLocked();
  snapshot.lines = game.lines();
  // A signature over the occupancy rows: every lock writes four cells, so a
  // changed signature means a lock, and a clear changes it again. FNV-1a over
  // the masks keeps the check cheap and exact.
  std::uint64_t hash = 1469598103934665603ULL;
  for (const std::uint16_t mask : game.board().occupancy()) {
    hash = (hash ^ mask) * 1099511628211ULL;
  }
  snapshot.board = hash;
  return snapshot;
}

CueSet CueDetector::observe(const Game& game) {
  CueSet cues;
  const Snapshot now = take(game);
  if (!hasSnapshot_) {
    hasSnapshot_ = true;
    previous_ = now;
    return cues;
  }

  if (now.scene == Scene::GameOver && previous_.scene != Scene::GameOver) {
    cues.add(SoundCue::GameOver);
  }

  if (previous_.scene == Scene::Playing && now.scene == Scene::Playing) {
    if (now.board != previous_.board) {
      // A lock wrote the board. The line total says what kind of lock it was.
      const std::uint64_t cleared = now.lines - previous_.lines;
      if (cleared >= 4) {
        cues.add(SoundCue::FourLineClear);
      } else if (cleared > 0) {
        cues.add(SoundCue::Clear);
      } else {
        cues.add(SoundCue::MoveLand);
      }
    } else if (previous_.hasActive && now.hasActive &&
               previous_.piece.id == now.piece.id) {
      // A hold swap changes the active piece through the hold state rather
      // than the board, so it stays silent.
      const bool holdChanged = now.hasHold != previous_.hasHold ||
                               now.holdId != previous_.holdId ||
                               now.holdLocked != previous_.holdLocked;
      if (!holdChanged) {
        if (now.piece.rotation != previous_.piece.rotation) {
          // A rotation; any position change that travels with it is the wall
          // kick, which belongs to this cue and not to a move.
          cues.add(SoundCue::Rotate);
        } else if (now.piece.x != previous_.piece.x) {
          cues.add(SoundCue::MoveLand);
        }
      }
    }
  }

  previous_ = now;
  return cues;
}

// ---------------------------------------------------------------------------
// The audio device.
// ---------------------------------------------------------------------------

AudioDriver::~AudioDriver() { close(); }

bool AudioDriver::open() {
  close();
  requested_.fill(0);
  played_.fill(0);
  mutedCount_ = 0;
  droppedCount_ = 0;

  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    std::printf("Sound: audio subsystem unavailable: %s\n", SDL_GetError());
    return false;
  }
  subsystemInitialized_ = true;

  // Ask for signed 16-bit stereo at a small buffer, allow the device its own
  // rate and buffer size, and fall back to mono before giving up. The cues are
  // synthesized after the device answers, so its real rate and channel count
  // are the ones the tones are built for.
  SDL_AudioSpec desired;
  SDL_zero(desired);
  desired.freq = kSoundRequestRate;
  desired.format = AUDIO_S16SYS;
  desired.samples = 512;
  desired.callback = &AudioDriver::audioCallback;
  desired.userdata = this;
  SDL_AudioSpec obtained;
  SDL_zero(obtained);
  const int channelAttempts[] = {2, 1};
  for (const int channels : channelAttempts) {
    desired.channels = static_cast<Uint8>(channels);
    device_ = SDL_OpenAudioDevice(
        nullptr, 0, &desired, &obtained,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (device_ != 0) {
      break;
    }
    std::printf("Sound: opening a %d-channel device failed: %s\n", channels,
                SDL_GetError());
  }
  if (device_ == 0) {
    std::printf("Sound: no audio device; the game continues silently\n");
    close();
    return false;
  }

  obtainedRate_ = obtained.freq;
  obtainedChannels_ = obtained.channels;
  const std::size_t channels = static_cast<std::size_t>(obtainedChannels_);
  // The music engine joins the device at its real rate (MUSIC.md, native
  // audio design). Building the catalogue and the wave tables here, on the
  // main thread, keeps the callback free of allocation and file access; the
  // two scratch buffers take the device's own maximum callback size, so the
  // callback never allocates either.
  musicCatalogue();
  musicEngine_.initialize(obtainedRate_);
  obtainedBufferFrames_ = static_cast<int>(obtained.samples);
  musicScratch_.assign(static_cast<std::size_t>(obtained.samples) + 16, 0);
  cueScratch_.assign(
      (static_cast<std::size_t>(obtained.samples) + 16) * channels, 0);
  publishMusicGain();
  musicEngine_.publishMasterGain(65536);
  cueMasterGainQ16_ = 65536;
  cueMasterStep_ = static_cast<std::uint32_t>(
      65536 / (obtainedRate_ / 1000 * 15) + 1);
  for (const SoundCue cue : kAllSoundCues) {
    const std::size_t index = static_cast<std::size_t>(cue);
    std::vector<std::int16_t> mono;
    synthesizeCue(cue, obtainedRate_, &mono);
    std::vector<std::int16_t>& interleaved = samples_[index];
    interleaved.clear();
    interleaved.reserve(mono.size() * channels);
    for (const std::int16_t value : mono) {
      for (std::size_t channel = 0; channel < channels; ++channel) {
        interleaved.push_back(value);
      }
    }
    voicePos_[index] = interleaved.size();  // finished until a play restarts it
    voiceSeen_[index].store(0, std::memory_order_relaxed);
    voiceRequest_[index].store(0, std::memory_order_relaxed);
  }
  const int bufferFrames = static_cast<int>(obtained.samples);
  std::printf("Sound: device opened at %d Hz, %d channels, %d-frame buffer\n",
              obtainedRate_, obtainedChannels_, bufferFrames);
  SDL_PauseAudioDevice(device_, 0);
  return true;
}

void AudioDriver::close() {
  if (device_ != 0) {
    SDL_PauseAudioDevice(device_, 1);
    SDL_CloseAudioDevice(device_);
    device_ = 0;
  }
  obtainedRate_ = 0;
  obtainedChannels_ = 0;
  for (std::vector<std::int16_t>& cue : samples_) {
    cue.clear();
    cue.shrink_to_fit();
  }
  musicScratch_.clear();
  musicScratch_.shrink_to_fit();
  cueScratch_.clear();
  cueScratch_.shrink_to_fit();
  if (subsystemInitialized_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    subsystemInitialized_ = false;
  }
}

void AudioDriver::setMuted(bool muted) {
  if (muted_ == muted) {
    return;
  }
  muted_ = muted;
  // The master switch fades both buses (MUSIC.md: master mute preserves the
  // music timeline). The engine ramps toward the published target; the cue
  // side reads muted_ directly below, as it always has.
  musicEngine_.publishMasterGain(muted_ ? 0u : 65536u);
  std::printf("Sound %s\n", muted_ ? "muted" : "unmuted");
}

void AudioDriver::setMusicVolume(int step) {
  musicVolume_ = std::max(0, std::min(10, step));
  publishMusicGain();
}

void AudioDriver::setEffectsVolume(int step) {
  effectsVolume_ = std::max(0, std::min(10, step));
}

void AudioDriver::publishMusicGain() {
  // The music bus gain is the volume step times the duck, published as one
  // latest-value atomic (MUSIC.md, thread communication).
  const std::uint32_t volume =
      MusicEngine::kVolumeGainQ16[static_cast<std::size_t>(musicVolume_)];
  musicEngine_.publishMusicGain(
      static_cast<std::uint32_t>((static_cast<std::uint64_t>(volume) *
                                  duckGainQ16_) >> 16));
}

void AudioDriver::updateDuck(std::size_t frames) {
  // The duck lowers the music about 4 decibels for the cue that asked plus
  // a release near 200 milliseconds; the attack is as fast as one callback.
  if (duckHoldFrames_ > 0) {
    duckHoldFrames_ -= static_cast<int>(
        std::min<std::size_t>(frames, static_cast<std::size_t>(duckHoldFrames_)));
  }
  const std::uint32_t target = duckHoldFrames_ > 0 ? 41352u : 65536u;
  const std::size_t releaseSamples =
      static_cast<std::size_t>(obtainedRate_) / 5;  // 200 ms
  const std::uint32_t step = std::max<std::uint32_t>(
      1, 65536 / std::max<std::size_t>(1, releaseSamples));
  if (duckGainQ16_ < target) {
    duckGainQ16_ = std::min(target, duckGainQ16_ + step * 4);  // fast attack
  } else if (duckGainQ16_ > target) {
    duckGainQ16_ = std::max(target, duckGainQ16_ - step);
  }
}

void AudioDriver::setPaused(bool paused) {
  if (device_ == 0) {
    return;
  }
  SDL_PauseAudioDevice(device_, paused ? 1 : 0);
}

void AudioDriver::play(SoundCue cue) {
  const std::size_t index = static_cast<std::size_t>(cue);
  ++requested_[index];
  if (muted_) {
    ++mutedCount_;
    return;
  }
  if (device_ == 0) {
    ++droppedCount_;
    return;
  }
  // The cues that matter most duck the music for their length plus a short
  // release (MUSIC.md, mixing and gain). A move or a rotation never does.
  if (obtainedRate_ > 0) {
    int holdMs = 0;
    switch (cue) {
      case SoundCue::Clear:
        holdMs = 320;
        break;
      case SoundCue::FourLineClear:
        holdMs = 470;
        break;
      case SoundCue::GameOver:
        holdMs = 820;
        break;
      default:
        break;
    }
    if (holdMs > 0) {
      duckHoldFrames_ =
          std::max(duckHoldFrames_,
                   static_cast<int>(static_cast<std::size_t>(holdMs) *
                                    obtainedRate_ / 1000));
    }
  }
  ++played_[index];
  voiceRequest_[index].store(
      voiceRequest_[index].load(std::memory_order_relaxed) + 1,
      std::memory_order_relaxed);
}

void AudioDriver::play(CueSet set) {
  for (const SoundCue cue : kAllSoundCues) {
    if (set.has(cue)) {
      play(cue);
    }
  }
}

unsigned AudioDriver::totalPlayedCount() const {
  unsigned total = 0;
  for (const SoundCue cue : kAllSoundCues) {
    total += played_[static_cast<std::size_t>(cue)];
  }
  return total;
}

void AudioDriver::mixFrames(std::int16_t* out, std::size_t frames) {
  if (out == nullptr || obtainedChannels_ <= 0) {
    return;
  }
  const std::size_t channels = static_cast<std::size_t>(obtainedChannels_);
  const std::size_t cells = frames * channels;
  std::fill(out, out + cells, static_cast<std::int16_t>(0));
  if (device_ == 0 || cells == 0) {
    return;
  }

  // The effect bus: every sounding cue sums into one 32-bit accumulator
  // per cell, unclamped, so the per-voice saturation of the cue-only mixer
  // is gone and one safety clamp at the end covers both buses (MUSIC.md,
  // mixing and gain).
  if (cells > cueScratch_.size()) {
    return;  // sized at open(); a larger callback would be a device bug
  }
  std::fill(cueScratch_.begin(), cueScratch_.begin() + cells, 0);
  for (const SoundCue cue : kAllSoundCues) {
    const std::size_t index = static_cast<std::size_t>(cue);
    // A play request that arrived since the last callback restarts the voice.
    // Counting requests instead of holding a queue means one cue asked for
    // twice between two callbacks still sounds once, while the played counter
    // above keeps the exact event count for the session log.
    const unsigned requested =
        voiceRequest_[index].load(std::memory_order_relaxed);
    if (requested != voiceSeen_[index].load(std::memory_order_relaxed)) {
      voiceSeen_[index].store(requested, std::memory_order_relaxed);
      voicePos_[index] = 0;
    }

    const std::vector<std::int16_t>& cueSamples = samples_[index];
    std::size_t& position = voicePos_[index];
    if (position >= cueSamples.size()) {
      continue;
    }
    const std::size_t mixable =
        std::min(cells, cueSamples.size() - position);
    for (std::size_t i = 0; i < mixable; ++i) {
      cueScratch_[i] += cueSamples[position + i];
    }
    position += mixable;
  }

  // The music bus beside it, then both volume steps and one final clamp.
  updateDuck(frames);
  publishMusicGain();
  if (frames > 0 && frames <= musicScratch_.size()) {
    std::fill(musicScratch_.begin(), musicScratch_.begin() + frames, 0);
    musicEngine_.renderAdd(musicScratch_.data(), frames);
  } else if (frames > 0) {
    std::fill(musicScratch_.begin(), musicScratch_.end(), 0);
  }
  const std::uint32_t effectsGain =
      MusicEngine::kVolumeGainQ16[static_cast<std::size_t>(effectsVolume_)];
  const std::uint32_t masterTarget = muted_ ? 0u : 65536u;
  for (std::size_t frame = 0; frame < frames; ++frame) {
    // The cue bus ramps its own master copy, one step per frame at the same
    // 15 millisecond rate as the engine, so a mute move fades instead of
    // stepping (MUSIC.md, master mute).
    cueMasterGainQ16_ =
        cueMasterGainQ16_ < masterTarget
            ? std::min<std::uint32_t>(masterTarget,
                                      cueMasterGainQ16_ + cueMasterStep_)
            : (cueMasterGainQ16_ > cueMasterStep_
                   ? std::max<std::uint32_t>(
                         masterTarget, cueMasterGainQ16_ - cueMasterStep_)
                   : masterTarget);
    const std::int32_t music = musicScratch_[frame];
    for (std::size_t channel = 0; channel < channels; ++channel) {
      const std::size_t cell = frame * channels + channel;
      const std::int64_t effects =
          (static_cast<std::int64_t>(cueScratch_[cell]) * effectsGain *
           cueMasterGainQ16_) >>
          32;
      out[cell] = clampSample(static_cast<std::int32_t>(effects + music));
    }
  }
}

void SDLCALL AudioDriver::audioCallback(void* userdata, Uint8* stream,
                                        int len) {
  auto* driver = static_cast<AudioDriver*>(userdata);
  const int bytesPerFrame = 2 * driver->obtainedChannels_;
  const std::size_t frames =
      bytesPerFrame > 0 ? static_cast<std::size_t>(len) / bytesPerFrame : 0;
  auto* out = static_cast<std::int16_t*>(static_cast<void*>(stream));
  driver->mixFrames(out, frames);
}

}  // namespace stackfall
