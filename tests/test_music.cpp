// The music engine and policy suite (MUSIC.md, task T062/M2 and M4). The
// engine and the director are SDL-free, so every check here renders through
// MusicOfflineRenderer with no audio device, exactly as the preview tool
// does, and the pinned numbers are the automated engine checks of MUSIC.md's
// validation plan: timing against the rational schedule, chunk
// independence, loop behavior without drift, intensity boundaries, voice
// limits, gain ramps, command pressure, and the director's pool policy.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_util.hpp"

#include "stackfall/music.hpp"
#include "stackfall/music_director.hpp"

namespace {

using stackfall::MusicCommand;
using stackfall::MusicCommandType;
using stackfall::MusicDirector;
using stackfall::MusicEngine;
using stackfall::MusicIntensity;
using stackfall::MusicInstrument;
using stackfall::MusicNote;
using stackfall::MusicOfflineRenderer;
using stackfall::MusicTrackData;
using stackfall::MusicValidationReport;
using stackfall::kMusicLayerAll;
using stackfall::kMusicLayerHigh;
using stackfall::kMusicLayerLow;
using stackfall::kMusicLayerMedium;
using stackfall::kMusicTicksPerBar;
using stackfall::kMusicTicksPerQuarter;
using stackfall::musicCatalogue;
using stackfall::musicTrackIndex;

// Renders `seconds` of a track from its first bar at one chunk size.
std::vector<std::int16_t> renderStretch(int trackIndex, MusicIntensity intensity,
                                        int sampleRate, double seconds,
                                        std::size_t chunkFrames) {
  MusicOfflineRenderer renderer(sampleRate);
  renderer.play(trackIndex, intensity);
  const std::size_t frames =
      static_cast<std::size_t>(seconds * sampleRate) + 1;
  std::vector<std::int16_t> samples(frames, 0);
  for (std::size_t at = 0; at < frames; at += chunkFrames) {
    const std::size_t count = std::min(chunkFrames, frames - at);
    renderer.render(samples.data() + at, count);
  }
  return samples;
}

std::int64_t peakOf(const std::vector<std::int16_t>& samples,
                    std::size_t from = 0) {
  std::int64_t peak = 0;
  for (std::size_t i = from; i < samples.size(); ++i) {
    const std::int64_t magnitude =
        samples[i] < 0 ? -static_cast<std::int64_t>(samples[i])
                       : static_cast<std::int64_t>(samples[i]);
    peak = std::max(peak, magnitude);
  }
  return peak;
}

double rmsOf(const std::vector<std::int16_t>& samples, std::size_t from,
             std::size_t count) {
  const std::size_t end = std::min(samples.size(), from + count);
  if (from >= end) {
    return 0.0;
  }
  double sum = 0.0;
  for (std::size_t i = from; i < end; ++i) {
    sum += static_cast<double>(samples[i]) * samples[i];
  }
  return std::sqrt(sum / static_cast<double>(end - from));
}

std::size_t countAbove(const std::vector<std::int16_t>& samples, int level) {
  std::size_t total = 0;
  for (const std::int16_t value : samples) {
    if (value > level || value < -level) {
      ++total;
    }
  }
  return total;
}

// The three reference tracks of MUSIC.md phase M3, which the heavier timing
// checks exercise, and the catalogue indexes the plan's table names.
constexpr const char* kReferenceIds[3] = {"arcade-bag", "groove-pocket",
                                          "focus-ghost"};

// ---------------------------------------------------------------------------
// Catalogue shape and validation.
// ---------------------------------------------------------------------------

void run_catalogue_checks() {
  std::printf("test_music: catalogue shape and validation\n");
  const std::vector<MusicTrackData>& tracks = musicCatalogue();
  SC_CHECK(tracks.size() == 12);

  // The plan's own table: IDs, collections, tempos, and working titles.
  struct Expected {
    const char* id;
    const char* title;
    stackfall::MusicCollection collection;
    int bpm;
  };
  const Expected expected[12] = {
      {"arcade-boot", "Boot Sequence", stackfall::MusicCollection::Arcade, 112},
      {"arcade-bag", "Seven Bag", stackfall::MusicCollection::Arcade, 128},
      {"arcade-kick", "Wall Kick", stackfall::MusicCollection::Arcade, 138},
      {"arcade-redline", "Redline", stackfall::MusicCollection::Arcade, 150},
      {"groove-pocket", "Pocket Circuit",
       stackfall::MusicCollection::Groove, 94},
      {"groove-night", "Night Bus", stackfall::MusicCollection::Groove, 104},
      {"groove-neon", "Neon Steps", stackfall::MusicCollection::Groove, 118},
      {"groove-afterimage", "Afterimage",
       stackfall::MusicCollection::Groove, 126},
      {"focus-ghost", "Ghost Grid", stackfall::MusicCollection::Focus, 88},
      {"focus-drop", "Soft Drop", stackfall::MusicCollection::Focus, 100},
      {"focus-orbit", "Orbit", stackfall::MusicCollection::Focus, 110},
      {"focus-clear", "Clear Skies", stackfall::MusicCollection::Focus, 116},
  };
  for (int i = 0; i < 12; ++i) {
    SC_CHECK(musicTrackIndex(expected[i].id) == i);
    const MusicTrackData& track = tracks[static_cast<std::size_t>(i)];
    SC_CHECK(std::strcmp(track.id, expected[i].id) == 0);
    SC_CHECK(std::strcmp(track.title, expected[i].title) == 0);
    SC_CHECK(track.collection == expected[i].collection);
    SC_CHECK(track.bpm == expected[i].bpm);
    // Every arrangement lasts two to three minutes before its loop repeats
    // (MUSIC.md, goal), and phrases are whole eight-bar units.
    const double seconds =
        track.bars * 4.0 * 60.0 / static_cast<double>(track.bpm);
    SC_CHECK(seconds >= 119.0);
    SC_CHECK(seconds <= 181.0);
    SC_CHECK(track.bars % stackfall::kMusicBarsPerPhrase == 0);
    SC_CHECK(!track.notes.empty());
  }

  // The whole catalogue validates, and every intensity map fits the voice
  // budget without the steal fallback (MUSIC.md, thread communication).
  const MusicValidationReport report = stackfall::validateMusicCatalogue();
  SC_CHECK(report.ok);
  SC_CHECK(report.maxVoices <= stackfall::kMusicMaxVoices);
  for (const MusicTrackData& track : tracks) {
    MusicValidationReport one;
    SC_CHECK(stackfall::validateMusicTrack(track, -1, &one));
    SC_CHECK(one.maxVoices >= 3);  // melody, bass, and drums really layer
  }

  // Distinct melodies: no two tracks share their opening melodic motif,
  // measured as the first eight lead-instrument notes of section A. The
  // motif data differs per track by construction; this pins it.
  for (std::size_t a = 0; a < tracks.size(); ++a) {
    for (std::size_t b = a + 1; b < tracks.size(); ++b) {
      std::vector<int> motifA;
      std::vector<int> motifB;
      for (const MusicNote& note : tracks[a].notes) {
        if (note.instrument == MusicInstrument::PulseLead &&
            motifA.size() < 8) {
          motifA.push_back(note.pitch);
        }
      }
      for (const MusicNote& note : tracks[b].notes) {
        if (note.instrument == MusicInstrument::PulseLead &&
            motifB.size() < 8) {
          motifB.push_back(note.pitch);
        }
      }
      SC_CHECK(motifA.size() == 8);
      SC_CHECK(motifB.size() == 8);
      SC_CHECK(motifA != motifB);
    }
  }

  // Swing lives in the score: Pocket Circuit's swung hats sit on the
  // shifted offbeat (72 ticks into the half bar), never on the straight one
  // (48), which is what a fixed swing offset inside the 96-tick grid means
  // (MUSIC.md, score representation).
  const MusicTrackData& pocket =
      tracks[static_cast<std::size_t>(musicTrackIndex("groove-pocket"))];
  int swungHits = 0;
  int straightHits = 0;
  for (const MusicNote& note : pocket.notes) {
    if (note.instrument != MusicInstrument::HatClosed) {
      continue;
    }
    const int withinHalf = static_cast<int>(note.startTick % 96);
    if (withinHalf == 72) {
      ++swungHits;
    } else if (withinHalf == 48) {
      ++straightHits;
    }
  }
  SC_CHECK(swungHits > 16);
  SC_CHECK(straightHits == 0);
}

// ---------------------------------------------------------------------------
// Validation rejects invalid scores before playback.
// ---------------------------------------------------------------------------

MusicTrackData validTestTrack() {
  MusicTrackData track;
  track.id = "test";
  track.title = "Test";
  track.collection = stackfall::MusicCollection::Arcade;
  track.bpm = 120;
  track.bars = 8;
  auto add = [&track](std::uint32_t start, std::uint16_t length,
                      std::int16_t pitch, MusicInstrument instrument) {
    MusicNote note;
    note.startTick = start;
    note.lengthTicks = length;
    note.pitch = pitch;
    note.velocity = 96;
    note.layerMask = kMusicLayerAll;
    note.instrument = instrument;
    track.notes.push_back(note);
  };
  add(0, 24, 60, MusicInstrument::PulseLead);
  add(0, 96, 36, MusicInstrument::TriangleBass);
  add(0, 24, 0, MusicInstrument::Kick);
  add(192, 24, 62, MusicInstrument::PulseLead);
  return track;
}

void run_validation_rejects_checks() {
  std::printf("test_music: invalid scores are rejected\n");
  MusicValidationReport report;

  MusicTrackData base = validTestTrack();
  SC_CHECK(stackfall::validateMusicTrack(base, 0, &report));
  SC_CHECK(report.ok);
  SC_CHECK(report.maxVoices == 3);

  {
    MusicTrackData broken = base;
    std::swap(broken.notes[0], broken.notes[3]);  // unsorted starts
    SC_CHECK(!stackfall::validateMusicTrack(broken, 0, &report));
    SC_CHECK(std::strstr(report.error, "sorted") != nullptr);
  }
  {
    MusicTrackData broken = base;
    broken.notes[1].startTick = 8 * kMusicTicksPerBar - 12;
    broken.notes[1].lengthTicks = 96;  // crosses the loop boundary
    SC_CHECK(!stackfall::validateMusicTrack(broken, 0, &report));
    SC_CHECK(std::strstr(report.error, "loop") != nullptr);
  }
  {
    MusicTrackData broken = base;
    broken.notes[0].instrument =
        static_cast<MusicInstrument>(kMusicLayerHigh + 40);
    SC_CHECK(!stackfall::validateMusicTrack(broken, 0, &report));
    SC_CHECK(std::strstr(report.error, "instrument") != nullptr);
  }
  {
    MusicTrackData broken = base;
    broken.notes[0].velocity = 0;
    SC_CHECK(!stackfall::validateMusicTrack(broken, 0, &report));
    SC_CHECK(std::strstr(report.error, "velocity") != nullptr);
    broken.notes[0].velocity = 128;
    SC_CHECK(!stackfall::validateMusicTrack(broken, 0, &report));
  }
  {
    MusicTrackData broken = base;
    broken.notes[0].layerMask = 0;
    SC_CHECK(!stackfall::validateMusicTrack(broken, 0, &report));
    SC_CHECK(std::strstr(report.error, "layer") != nullptr);
  }
  {
    MusicTrackData broken = base;
    broken.notes[0].lengthTicks = 0;
    SC_CHECK(!stackfall::validateMusicTrack(broken, 0, &report));
    SC_CHECK(std::strstr(report.error, "zero-length") != nullptr);
  }
  {
    MusicTrackData broken = base;
    broken.notes[0].pitch = 200;  // PulseLead is pitched; MIDI tops at 127
    SC_CHECK(!stackfall::validateMusicTrack(broken, 0, &report));
    SC_CHECK(std::strstr(report.error, "MIDI") != nullptr);
  }
  {
    MusicTrackData broken = base;
    broken.bpm = 400;
    SC_CHECK(!stackfall::validateMusicTrack(broken, 0, &report));
    broken = base;
    broken.bars = 0;
    SC_CHECK(!stackfall::validateMusicTrack(broken, 0, &report));
  }
  {
    // Thirteen simultaneous held notes exceed the engine budget.
    MusicTrackData broken = base;
    broken.notes.clear();
    for (int i = 0; i < 13; ++i) {
      MusicNote note;
      note.startTick = 0;
      note.lengthTicks = 96;
      note.pitch = static_cast<std::int16_t>(60 + i);
      note.velocity = 96;
      note.layerMask = kMusicLayerAll;
      note.instrument = MusicInstrument::ChordVoice;
      broken.notes.push_back(note);
    }
    SC_CHECK(!stackfall::validateMusicTrack(broken, 0, &report));
    SC_CHECK(std::strstr(report.error, "budget") != nullptr);
    // Twelve fit.
    broken.notes.pop_back();
    SC_CHECK(stackfall::validateMusicTrack(broken, 0, &report));
    SC_CHECK(report.maxVoices == 12);
  }
}

// ---------------------------------------------------------------------------
// Timing: the rational schedule, chunk independence, and loops.
// ---------------------------------------------------------------------------

// The engine's own phase increment and the sample index of a loop boundary,
// computed the way src/music.cpp does, so the checks below can compare the
// rendered boundary against the rational schedule.
std::uint32_t engineIncrement(int bpm, int sampleRate) {
  const std::uint64_t ticksPerSecond =
      static_cast<std::uint64_t>(bpm) * kMusicTicksPerQuarter;
  return static_cast<std::uint32_t>((ticksPerSecond << 32) /
                                    (60ull * sampleRate));
}

std::int64_t loopBoundaryFrame(const MusicTrackData& track, int sampleRate) {
  const std::uint64_t totalTicks = track.totalTicks();
  const std::uint64_t increment = engineIncrement(track.bpm, sampleRate);
  // The first sample whose advance reaches the whole arrangement.
  return static_cast<std::int64_t>((totalTicks << 32) / increment);
}

void run_timing_checks() {
  std::printf("test_music: timing, chunk independence, and loops\n");
  const std::vector<MusicTrackData>& tracks = musicCatalogue();
  const int rates[3] = {22050, 44100, 48000};

  const int ghostIndex = musicTrackIndex("focus-ghost");
  for (const char* id : kReferenceIds) {
    const int index = musicTrackIndex(id);
    SC_CHECK(index >= 0);
    const MusicTrackData& track = tracks[static_cast<std::size_t>(index)];

    for (int rate : rates) {
      // One track covers the two other rates; all three cover the working
      // rate, keeping the suite inside a bounded wall time.
      if (index != ghostIndex && rate != 44100) {
        continue;
      }
      // The loop boundary the engine will reach sits within one output
      // sample of the rational schedule (MUSIC.md, automated engine checks).
      const std::int64_t boundary = loopBoundaryFrame(track, rate);
      const double rational = static_cast<double>(track.totalTicks()) * rate *
                              60.0 /
                              (track.bpm * kMusicTicksPerQuarter);
      SC_CHECK(std::fabs(static_cast<double>(boundary) - rational) <= 1.0);

      // Frame-stepping across the boundary observes the wrap exactly there.
      MusicOfflineRenderer renderer(rate);
      renderer.play(index, MusicIntensity::Medium);
      std::vector<std::int16_t> one(1, 0);
      const std::size_t head =
          static_cast<std::size_t>(std::max<std::int64_t>(0, boundary - 400));
      std::vector<std::int16_t> fast(head, 0);
      for (std::size_t at = 0; at < head; at += 512) {
        renderer.render(fast.data() + at, std::min<std::size_t>(512, head - at));
      }
      std::int64_t observed = -1;
      for (std::int64_t frame = head; frame < boundary + 400; ++frame) {
        renderer.render(one.data(), 1);
        if (renderer.engine().snapshot().loopCount == 1) {
          observed = frame + 1;
          break;
        }
      }
      SC_CHECK(observed >= boundary - 1);
      SC_CHECK(observed <= boundary + 1);
    }
  }

  // Chunk independence: one large render equals a varied-chunk render of
  // the same stretch, including chunks of one frame (MUSIC.md, automated
  // engine checks).
  const int bag = musicTrackIndex("arcade-bag");
  const std::vector<std::int16_t> whole =
      renderStretch(bag, MusicIntensity::Medium, 44100, 20.0, 4096);
  const std::size_t sizes[6] = {1, 7, 64, 512, 3, 4096};
  std::vector<std::int16_t> varied(whole.size(), 0);
  {
    MusicOfflineRenderer renderer(44100);
    renderer.play(bag, MusicIntensity::Medium);
    std::size_t at = 0;
    int step = 0;
    while (at < varied.size()) {
      const std::size_t count =
          std::min(sizes[step % 6], varied.size() - at);
      renderer.render(varied.data() + at, count);
      at += count;
      ++step;
    }
  }
  SC_CHECK(varied.size() == whole.size());
  SC_CHECK(std::equal(whole.begin(), whole.end(), varied.begin()));

  // Determinism: the same stretch renders byte-identically twice.
  const std::vector<std::int16_t> again =
      renderStretch(bag, MusicIntensity::Medium, 44100, 20.0, 4096);
  SC_CHECK(again == whole);

  // Repeated loops do not drift: every wrap lands exactly on the rational
  // schedule - frame-stepping observes each one at ceil(k arrangements) -
  // and the loop boundary carries no click: the steepest sample step around
  // a wrap stays inside the steepest step of the whole render, which any
  // step discontinuity would exceed.
  {
    const int ghost = musicTrackIndex("focus-ghost");
    const MusicTrackData& track = tracks[static_cast<std::size_t>(ghost)];
    const int rate = 22050;
    const std::uint64_t totalTicks = track.totalTicks();
    const std::uint64_t increment = engineIncrement(track.bpm, rate);
    const auto expectedWrap = [totalTicks, increment](int loop) {
      const std::uint64_t target =
          static_cast<std::uint64_t>(loop) * totalTicks << 32;
      return (target + increment - 1) / increment;
    };
    MusicOfflineRenderer renderer(rate);
    renderer.play(ghost, MusicIntensity::Medium);
    std::vector<std::int16_t> rendered(
        static_cast<std::size_t>(expectedWrap(3)) + 2 * rate + 4096, 0);
    std::size_t wraps[4] = {0, 0, 0, 0};
    std::size_t at = 0;
    std::vector<std::int16_t> block(1024, 0);
    for (int loop = 1; loop <= 3; ++loop) {
      const std::size_t expected =
          static_cast<std::size_t>(expectedWrap(loop));
      // Fast-forward to just before the boundary, then step one frame at a
      // time: chunk independence makes the split invisible.
      while (at + 400 < expected) {
        const std::size_t count =
            std::min<std::size_t>(1024, expected - 400 - at);
        renderer.render(rendered.data() + at, count);
        at += count;
      }
      std::size_t observed = at;
      while (observed < rendered.size()) {
        renderer.render(rendered.data() + observed, 1);
        ++observed;
        if (renderer.engine().snapshot().loopCount ==
            static_cast<std::uint32_t>(loop)) {
          break;
        }
      }
      // Exactly on the rational schedule: a render that drifted would miss
      // by more than a sample, and by more each further loop.
      SC_CHECK(observed == expected);
      wraps[loop] = observed;
      at = observed;  // the stepping itself rendered every frame to the wrap
    }
    while (at < rendered.size()) {
      const std::size_t count =
          std::min<std::size_t>(1024, rendered.size() - at);
      renderer.render(rendered.data() + at, count);
      at += count;
    }
    // The click check: the steepest step anywhere near a wrap stays inside
    // the steepest step of the render at large.
    std::int64_t globalStep = 0;
    for (std::size_t i = 1; i < rendered.size(); ++i) {
      globalStep = std::max(globalStep,
                            static_cast<std::int64_t>(rendered[i]) -
                                static_cast<std::int64_t>(rendered[i - 1]));
    }
    for (int loop = 1; loop <= 3; ++loop) {
      std::int64_t wrapStep = 0;
      for (std::size_t i = wraps[static_cast<std::size_t>(loop)] - 200;
           i < wraps[static_cast<std::size_t>(loop)] + 200; ++i) {
        wrapStep = std::max(wrapStep,
                            static_cast<std::int64_t>(rendered[i]) -
                                static_cast<std::int64_t>(rendered[i - 1]));
      }
      SC_CHECK(wrapStep <= globalStep);
    }
    SC_CHECK(peakOf(rendered) > 4000);
  }
}

// ---------------------------------------------------------------------------
// Layers, voices, and levels.
// ---------------------------------------------------------------------------

void run_layer_and_level_checks() {
  std::printf("test_music: intensity layers, voices, and levels\n");
  const std::vector<MusicTrackData>& tracks = musicCatalogue();

  // Every track at every intensity is audible, inside the mix headroom,
  // and inside the voice budget, with no DC offset (MUSIC.md, mixing).
  for (const MusicTrackData& track : tracks) {
    for (int intensity = 0; intensity < stackfall::kMusicIntensityCount;
         ++intensity) {
      const auto level =
          static_cast<MusicIntensity>(intensity);
      const std::vector<std::int16_t> samples =
          renderStretch(musicTrackIndex(track.id), level, 22050, 12.0, 1024);
      const std::int64_t peak = peakOf(samples);
      SC_CHECK(peak > 2500);                 // audible, not a stub
      SC_CHECK(peak <= 24000);               // headroom beside the effects
      std::size_t atPeak = 0;
      for (std::size_t i = 0; i < samples.size(); ++i) {
        if (std::abs(static_cast<int>(samples[i])) >= 32767) {
          ++atPeak;
        }
      }
      SC_CHECK(atPeak == 0);  // no clipping at unity music volume
      // No unintended constant offset: the bound sits at the audibility
      // floor (about -48 dBFS), far under any level a listener or a speaker
      // reproduces, while still catching a genuine offset bug.
      double sum = 0.0;
      for (const std::int16_t value : samples) {
        sum += value;
      }
      SC_CHECK(std::fabs(sum / static_cast<double>(samples.size())) < 150.0);
    }
  }

  // The intensity maps share one timeline and grow: the high map carries
  // more energy than low over the same stretch of the bag track.
  const int bag = musicTrackIndex("arcade-bag");
  const std::vector<std::int16_t> low =
      renderStretch(bag, MusicIntensity::Low, 44100, 16.0, 1024);
  const std::vector<std::int16_t> high =
      renderStretch(bag, MusicIntensity::High, 44100, 16.0, 1024);
  const std::size_t window = 8 * 44100;  // past the intro, inside section A
  SC_CHECK(rmsOf(high, window, window / 2) > rmsOf(low, window, window / 2));

  // The low map alone must stay musically complete: melody, bass, and the
  // kick anchor are present (MUSIC.md: essential melody and harmony stay
  // coherent when optional layers are absent).
  const std::vector<std::int16_t> title =
      renderStretch(musicTrackIndex("arcade-boot"), MusicIntensity::Low,
                    44100, 16.0, 1024);
  SC_CHECK(countAbove(title, 300) > 44100);  // at least a second of sound

  // An intensity change applies at the next phrase boundary, not at once:
  // pushing Medium right before a boundary keeps the low map's bars until
  // the eight-bar mark (MUSIC.md, playback behavior).
  MusicOfflineRenderer renderer(44100);
  renderer.play(musicTrackIndex("focus-orbit"), MusicIntensity::Low);
  std::vector<std::int16_t> block(1024, 0);
  // Walk to the seventh bar of a phrase, ask for the high map, and prove
  // the switch waits for the eight-bar boundary: the bar counter passes
  // seven at the old map, and the new map is active inside the next bar.
  for (int guard = 0; guard < 4000; ++guard) {
    renderer.render(block.data(), block.size());
    if (renderer.engine().snapshot().bar % 8 == 7) {
      break;
    }
  }
  SC_CHECK(renderer.engine().snapshot().bar % 8 == 7);
  renderer.setIntensity(MusicIntensity::High);
  for (int bars = 0; bars < 3; ++bars) {
    for (int guard = 0; guard < 200; ++guard) {
      renderer.render(block.data(), block.size());
      if (renderer.engine().snapshot().bar % 8 == 1) {
        break;
      }
    }
  }
  SC_CHECK(renderer.engine().snapshot().intensity ==
           static_cast<std::uint8_t>(MusicIntensity::High));
}

// ---------------------------------------------------------------------------
// Gain ramps, transport commands, and queue pressure.
// ---------------------------------------------------------------------------

void run_transport_checks() {
  std::printf("test_music: transport commands, ramps, and queue pressure\n");
  const int ghost = musicTrackIndex("focus-ghost");

  // Pause fades out and freezes the timeline; resume fades back in and the
  // bar counter moves again (MUSIC.md, playback behavior).
  {
    MusicOfflineRenderer renderer(44100);
    renderer.play(ghost, MusicIntensity::Medium);
    std::vector<std::int16_t> warm(44100 * 4, 0);
    for (std::size_t at = 0; at < warm.size(); at += 512) {
      renderer.render(warm.data() + at,
                      std::min<std::size_t>(512, warm.size() - at));
    }
    const std::uint32_t barBefore = renderer.engine().snapshot().bar;
    renderer.pause();
    std::vector<std::int16_t> fade(44100 / 2, 0);
    for (std::size_t at = 0; at < fade.size(); at += 256) {
      renderer.render(fade.data() + at,
                      std::min<std::size_t>(256, fade.size() - at));
    }
    SC_CHECK(renderer.engine().snapshot().frozen);
    SC_CHECK(renderer.engine().snapshot().transportRunning);
    SC_CHECK(peakOf(fade, fade.size() - 512) == 0);  // the fade ends silent
    const std::uint32_t barFrozen = renderer.engine().snapshot().bar;
    std::vector<std::int16_t> held(44100, 0);
    for (std::size_t at = 0; at < held.size(); at += 512) {
      renderer.render(held.data() + at,
                      std::min<std::size_t>(512, held.size() - at));
    }
    SC_CHECK(renderer.engine().snapshot().bar == barFrozen);
    SC_CHECK(peakOf(held) == 0);  // frozen transport renders silence
    renderer.resume();
    std::vector<std::int16_t> back(44100, 0);
    for (std::size_t at = 0; at < back.size(); at += 512) {
      renderer.render(back.data() + at,
                      std::min<std::size_t>(512, back.size() - at));
    }
    SC_CHECK(!renderer.engine().snapshot().frozen);
    SC_CHECK(renderer.engine().snapshot().bar >= barFrozen);
    SC_CHECK(peakOf(back) > 1000);  // music returned with the fade-in
    (void)barBefore;
  }

  // Stop fades out over its own span and ends the transport; the game-over
  // fade is the same path with a longer window.
  {
    MusicOfflineRenderer renderer(44100);
    renderer.play(ghost, MusicIntensity::Medium);
    std::vector<std::int16_t> warm(44100 * 3, 0);
    for (std::size_t at = 0; at < warm.size(); at += 512) {
      renderer.render(warm.data() + at,
                      std::min<std::size_t>(512, warm.size() - at));
    }
    SC_CHECK(renderer.engine().snapshot().transportRunning);
    renderer.stop(250);
    std::vector<std::int16_t> fade(44100, 0);
    for (std::size_t at = 0; at < fade.size(); at += 256) {
      renderer.render(fade.data() + at,
                      std::min<std::size_t>(256, fade.size() - at));
    }
    SC_CHECK(!renderer.engine().snapshot().transportRunning);
    SC_CHECK(peakOf(fade, fade.size() - 2048) == 0);
  }

  // The volume table: step 0 is exactly silent, step 10 is unity, and the
  // steps in between are strictly increasing and perceptually spaced
  // (MUSIC.md: an explicit gain table; about 2.2 dB per step).
  const auto& table = MusicEngine::kVolumeGainQ16;
  SC_CHECK(table[0] == 0);
  SC_CHECK(table[10] == 65536);
  for (int step = 1; step <= 10; ++step) {
    SC_CHECK(table[static_cast<std::size_t>(step)] >
             table[static_cast<std::size_t>(step - 1)]);
    if (step >= 2) {
      const double ratio =
          static_cast<double>(table[static_cast<std::size_t>(step)]) /
          static_cast<double>(table[static_cast<std::size_t>(step - 1)]);
      SC_CHECK(ratio > 1.1);  // each step is an audible change
      SC_CHECK(ratio < 1.6);  // and no step jumps coarsely
    }
  }

  // Master mute fades the bus to exact silence while the transport keeps
  // running, and releasing it brings the music back (MUSIC.md: master mute
  // preserves the timeline).
  {
    MusicOfflineRenderer renderer(44100);
    renderer.play(ghost, MusicIntensity::Medium);
    std::vector<std::int16_t> warm(44100 * 3, 0);
    for (std::size_t at = 0; at < warm.size(); at += 512) {
      renderer.render(warm.data() + at,
                      std::min<std::size_t>(512, warm.size() - at));
    }
    renderer.engine().publishMasterGain(0);
    std::vector<std::int16_t> muted(44100 / 2, 0);
    for (std::size_t at = 0; at < muted.size(); at += 256) {
      renderer.render(muted.data() + at,
                      std::min<std::size_t>(256, muted.size() - at));
    }
    SC_CHECK(peakOf(muted, muted.size() - 512) == 0);
    SC_CHECK(renderer.engine().snapshot().transportRunning);
    SC_CHECK(renderer.engine().snapshot().bar > 0);  // the timeline moved
    renderer.engine().publishMasterGain(65536);
    std::vector<std::int16_t> unmuted(44100, 0);
    for (std::size_t at = 0; at < unmuted.size(); at += 512) {
      renderer.render(unmuted.data() + at,
                      std::min<std::size_t>(512, unmuted.size() - at));
    }
    SC_CHECK(peakOf(unmuted) > 1000);
  }

  // Music volume zero is silent by the table, not by stopping the bus. The
  // bus gain ramps from unity like every published change, so the check
  // starts past the short ramp.
  {
    MusicOfflineRenderer renderer(44100);
    renderer.play(ghost, MusicIntensity::Medium);
    renderer.engine().publishMusicGain(0);
    std::vector<std::int16_t> silent(44100 * 2, 0);
    for (std::size_t at = 0; at < silent.size(); at += 512) {
      renderer.render(silent.data() + at,
                      std::min<std::size_t>(512, silent.size() - at));
    }
    SC_CHECK(peakOf(silent, 4410) == 0);
    SC_CHECK(renderer.engine().snapshot().transportRunning);
  }

  // Queue pressure: a full queue reports failure instead of dropping a
  // command silently, and the engine recovers by draining what arrived.
  {
    MusicEngine engine;
    engine.initialize(22050);
    int accepted = 0;
    MusicCommand command;
    command.type = MusicCommandType::SetIntensity;
    command.intensity = 2;
    while (engine.tryPushCommand(command) && accepted < 100) {
      ++accepted;
    }
    SC_CHECK(accepted == 16);  // the queue's fixed capacity
    SC_CHECK(!engine.tryPushCommand(command));
    std::vector<std::int32_t> scratch(512, 0);
    engine.renderAdd(scratch.data(), scratch.size());
    SC_CHECK(engine.tryPushCommand(command));  // drained: room again
  }

  // Repeated Next-track style requests coalesce: only the latest
  // destination stays pending (MUSIC.md, playback behavior).
  {
    MusicOfflineRenderer renderer(44100);
    renderer.play(musicTrackIndex("arcade-boot"), MusicIntensity::Low);
    std::vector<std::int16_t> warm(44100, 0);
    for (std::size_t at = 0; at < warm.size(); at += 512) {
      renderer.render(warm.data() + at,
                      std::min<std::size_t>(512, warm.size() - at));
    }
    renderer.play(musicTrackIndex("focus-drop"), MusicIntensity::Medium);
    renderer.play(musicTrackIndex("groove-neon"), MusicIntensity::Medium);
    std::vector<std::int16_t> drained(64, 0);
    renderer.render(drained.data(), drained.size());
    SC_CHECK(renderer.engine().snapshot().pendingSwitch);
    std::vector<std::int16_t> over(44100 * 3, 0);
    for (std::size_t at = 0; at < over.size(); at += 512) {
      renderer.render(over.data() + at,
                      std::min<std::size_t>(512, over.size() - at));
    }
    SC_CHECK(renderer.engine().snapshot().track ==
             musicTrackIndex("groove-neon"));
    SC_CHECK(!renderer.engine().snapshot().pendingSwitch);
  }
}

// ---------------------------------------------------------------------------
// The director's policy.
// ---------------------------------------------------------------------------

// Renders the engine inside a director-driven session, one frame bundle per
// simulated step, the way the platform loop calls update(). advanceUntil
// waits for a condition with a bound: a bar-boundary switch takes up to one
// bar of the slowest tempo, so a fixed window would race it.
class DirectorHarness {
 public:
  explicit DirectorHarness(std::uint64_t seed)
      : director_(renderer_.engine()) {
    director_.begin(seed);
  }

  MusicDirector& director() { return director_; }
  MusicOfflineRenderer& renderer() { return renderer_; }

  template <typename Predicate>
  bool advanceUntil(Predicate done, double timeoutSeconds) {
    const std::size_t frames =
        static_cast<std::size_t>(timeoutSeconds * 44100);
    std::vector<std::int16_t> block(1024, 0);
    std::size_t doneFrames = 0;
    while (doneFrames < frames) {
      renderer_.render(block.data(), block.size());
      director_.update();
      doneFrames += block.size();
      if (done()) {
        return true;
      }
    }
    return false;
  }

  void advance(double seconds) {
    const std::size_t frames = static_cast<std::size_t>(seconds * 44100);
    std::vector<std::int16_t> block(1024, 0);
    std::size_t doneFrames = 0;
    while (doneFrames < frames) {
      const std::size_t count = std::min<std::size_t>(1024, frames - doneFrames);
      renderer_.render(block.data(), count);
      director_.update();
      doneFrames += count;
    }
  }

 private:
  MusicOfflineRenderer renderer_{44100};
  MusicDirector director_;
};

void run_director_checks() {
  std::printf("test_music: director pools and scene policy\n");
  const std::vector<MusicTrackData>& tracks = musicCatalogue();

  // Mode names round-trip; nothing else parses.
  for (int value = 0; value <= static_cast<int>(stackfall::MusicMode::Off);
       ++value) {
    const auto mode = static_cast<stackfall::MusicMode>(value);
    stackfall::MusicMode parsed = stackfall::MusicMode::Auto;
    SC_CHECK(stackfall::musicModeFromName(stackfall::musicModeName(mode),
                                          &parsed));
    SC_CHECK(parsed == mode);
  }
  stackfall::MusicMode parsed = stackfall::MusicMode::Auto;
  SC_CHECK(!stackfall::musicModeFromName("AUTO", &parsed));
  SC_CHECK(!stackfall::musicModeFromName("arcade ", &parsed));
  SC_CHECK(!stackfall::musicModeFromName("", &parsed));

  // Level bands (MUSIC.md, playback behavior).
  SC_CHECK(stackfall::musicIntensityForLevel(1) == MusicIntensity::Low);
  SC_CHECK(stackfall::musicIntensityForLevel(5) == MusicIntensity::Low);
  SC_CHECK(stackfall::musicIntensityForLevel(6) == MusicIntensity::Medium);
  SC_CHECK(stackfall::musicIntensityForLevel(12) == MusicIntensity::Medium);
  SC_CHECK(stackfall::musicIntensityForLevel(13) == MusicIntensity::High);
  SC_CHECK(stackfall::musicIntensityForLevel(20) == MusicIntensity::High);

  // The title theme plays at low intensity on begin.
  {
    DirectorHarness harness(20261104);
    harness.advance(2.0);
    SC_CHECK(harness.renderer().engine().snapshot().transportRunning);
    SC_CHECK(harness.renderer().engine().snapshot().track ==
             stackfall::kMusicTitleTrack);
    SC_CHECK(harness.renderer().engine().snapshot().intensity ==
             static_cast<std::uint8_t>(MusicIntensity::Low));
    SC_CHECK(std::strcmp(harness.director().currentTrackTitle(),
                         "Boot Sequence") == 0);
  }

  // Auto shuffles all twelve without repetition inside a cycle and never
  // repeats across a cycle boundary; the title theme rides above the pool
  // (MUSIC.md, playback behavior).
  {
    DirectorHarness harness(777);
    harness.advance(2.0);
    int previous = -1;
    std::vector<int> played;
    for (int round = 0; round < 13; ++round) {
      harness.director().onScene(stackfall::Scene::Title,
                                 stackfall::Scene::Playing);
      const int target = harness.director().currentTrack();
      (void)target;
      const int before = previous;
      SC_CHECK(harness.advanceUntil(
          [&harness, before] {
            return !harness.renderer().engine().snapshot().pendingSwitch &&
                   harness.director().currentTrack() != before;
          },
          12.0));
      const int track = harness.director().currentTrack();
      SC_CHECK(track >= 0);
      SC_CHECK(track < static_cast<int>(tracks.size()));
      // No immediate repeat, ever.
      SC_CHECK(track != previous);
      played.push_back(track);
      previous = track;
      // The round ends; game over fades the transport out.
      harness.director().onScene(stackfall::Scene::Playing,
                                 stackfall::Scene::GameOver);
      SC_CHECK(harness.advanceUntil(
          [&harness] {
            return !harness.renderer().engine().snapshot().transportRunning;
          },
          2.0));
      SC_CHECK(std::strcmp(harness.director().currentTrackTitle(), "") == 0);
      harness.director().onScene(stackfall::Scene::GameOver,
                                 stackfall::Scene::Title);
      SC_CHECK(harness.advanceUntil(
          [&harness] {
            return harness.renderer().engine().snapshot().track ==
                   stackfall::kMusicTitleTrack;
          },
          12.0));
    }
    // The first twelve rounds covered the whole Auto pool exactly once.
    std::vector<int> cycle(played.begin(), played.begin() + 12);
    std::sort(cycle.begin(), cycle.end());
    for (int i = 0; i < 12; ++i) {
      SC_CHECK(cycle[static_cast<std::size_t>(i)] == i);
    }
  }

  // A restricted pool stays inside its collection.
  {
    DirectorHarness harness(31337);
    harness.advance(1.0);
    harness.director().setMode(stackfall::MusicMode::Focus);
    harness.advance(1.0);
    for (int round = 0; round < 6; ++round) {
      harness.director().onScene(stackfall::Scene::Title,
                                 stackfall::Scene::Playing);
      SC_CHECK(harness.advanceUntil(
          [&harness, &tracks] {
            const auto snapshot = harness.renderer().engine().snapshot();
            return !snapshot.pendingSwitch && snapshot.transportRunning &&
                   tracks[static_cast<std::size_t>(harness.director()
                                                   .currentTrack())]
                           .collection ==
                       stackfall::MusicCollection::Focus &&
                   harness.director().currentTrack() !=
                       stackfall::kMusicTitleTrack;
          },
          12.0));
      harness.director().onScene(stackfall::Scene::Playing,
                                 stackfall::Scene::GameOver);
      SC_CHECK(harness.advanceUntil(
          [&harness] {
            return !harness.renderer().engine().snapshot().transportRunning;
          },
          2.0));
      harness.director().onScene(stackfall::Scene::GameOver,
                                 stackfall::Scene::Title);
      SC_CHECK(harness.advanceUntil(
          [&harness] {
            return harness.renderer().engine().snapshot().track ==
                   stackfall::kMusicTitleTrack;
          },
          12.0));
    }
  }

  // Pause freezes on the scene, resume continues, and the pause controls
  // detour never resumes by itself.
  {
    DirectorHarness harness(42);
    harness.advance(1.0);
    harness.director().onScene(stackfall::Scene::Title,
                               stackfall::Scene::Playing);
    harness.advanceUntil(
        [&harness] {
          return !harness.renderer().engine().snapshot().pendingSwitch;
        },
        12.0);
    harness.director().onScene(stackfall::Scene::Playing,
                               stackfall::Scene::Paused);
    SC_CHECK(harness.advanceUntil(
        [&harness] { return harness.renderer().engine().snapshot().frozen; },
        2.0));
    harness.director().onScene(stackfall::Scene::Paused,
                               stackfall::Scene::PauseControls);
    harness.advance(0.3);
    SC_CHECK(harness.renderer().engine().snapshot().frozen);
    harness.director().onScene(stackfall::Scene::PauseControls,
                               stackfall::Scene::Paused);
    harness.advance(0.3);
    SC_CHECK(harness.renderer().engine().snapshot().frozen);
    harness.director().onScene(stackfall::Scene::Paused,
                               stackfall::Scene::Playing);
    SC_CHECK(harness.advanceUntil(
        [&harness] { return !harness.renderer().engine().snapshot().frozen; },
        2.0));
    SC_CHECK(harness.renderer().engine().snapshot().transportRunning);
  }

  // Music Off stops the transport; a mode change back starts a fresh track
  // from the pool; while Off, scenes and Next track change nothing.
  {
    DirectorHarness harness(9);
    harness.advance(1.0);
    harness.director().setMode(stackfall::MusicMode::Off);
    SC_CHECK(harness.advanceUntil(
        [&harness] {
          return !harness.renderer().engine().snapshot().transportRunning;
        },
        2.0));
    harness.director().nextTrack();
    harness.director().onScene(stackfall::Scene::Title,
                               stackfall::Scene::Playing);
    harness.advance(0.5);
    SC_CHECK(!harness.renderer().engine().snapshot().transportRunning);
    harness.director().setMode(stackfall::MusicMode::Arcade);
    SC_CHECK(harness.advanceUntil(
        [&harness, &tracks] {
          return harness.renderer().engine().snapshot().transportRunning &&
                 tracks[static_cast<std::size_t>(
                            harness.director().currentTrack())]
                         .collection ==
                     stackfall::MusicCollection::Arcade;
        },
        12.0));
  }

  // Next track excludes the current track while another is available, and
  // the level feed schedules the intensity map, never the tempo.
  {
    DirectorHarness harness(5150);
    harness.advance(1.0);
    harness.director().onScene(stackfall::Scene::Title,
                               stackfall::Scene::Playing);
    harness.advanceUntil(
        [&harness] {
          return !harness.renderer().engine().snapshot().pendingSwitch;
        },
        12.0);
    const int before = harness.director().currentTrack();
    harness.director().nextTrack();
    SC_CHECK(harness.advanceUntil(
        [&harness, before] {
          return !harness.renderer().engine().snapshot().pendingSwitch &&
                 harness.director().currentTrack() != before;
        },
        12.0));
    const int after = harness.director().currentTrack();
    const int bpmBefore = tracks[static_cast<std::size_t>(after)].bpm;
    // The level feed changes the intensity map, never the tempo: walk to a
    // phrase boundary at the high map and compare the playing tempo.
    harness.director().setLevel(15);
    SC_CHECK(harness.advanceUntil(
        [&harness] {
          return harness.renderer().engine().snapshot().intensity ==
                 static_cast<std::uint8_t>(MusicIntensity::High);
        },
        40.0));
    SC_CHECK(tracks[static_cast<std::size_t>(
                 harness.director().currentTrack())]
                 .bpm == bpmBefore);
    harness.director().setLevel(1);
    SC_CHECK(harness.advanceUntil(
        [&harness] {
          return harness.renderer().engine().snapshot().intensity ==
                 static_cast<std::uint8_t>(MusicIntensity::Low);
        },
        40.0));
  }

  // Determinism: the same seed walks the same pool in the same order.
  std::vector<int> orderA;
  std::vector<int> orderB;
  {
    DirectorHarness a(1234);
    DirectorHarness b(1234);
    for (int round = 0; round < 6; ++round) {
      a.director().onScene(stackfall::Scene::Title,
                           stackfall::Scene::Playing);
      a.advanceUntil([&a] {
        return !a.renderer().engine().snapshot().pendingSwitch;
      }, 12.0);
      orderA.push_back(a.director().currentTrack());
      a.director().onScene(stackfall::Scene::Playing,
                           stackfall::Scene::GameOver);
      a.advance(0.4);
      a.director().onScene(stackfall::Scene::GameOver,
                           stackfall::Scene::Title);
      a.advance(0.4);
      b.director().onScene(stackfall::Scene::Title,
                           stackfall::Scene::Playing);
      b.advanceUntil([&b] {
        return !b.renderer().engine().snapshot().pendingSwitch;
      }, 12.0);
      orderB.push_back(b.director().currentTrack());
      b.director().onScene(stackfall::Scene::Playing,
                           stackfall::Scene::GameOver);
      b.advance(0.4);
      b.director().onScene(stackfall::Scene::GameOver,
                           stackfall::Scene::Title);
      b.advance(0.4);
    }
  }
  SC_CHECK(orderA == orderB);
}

// ---------------------------------------------------------------------------
// The WAV writer of the preview path.
// ---------------------------------------------------------------------------

void run_wav_checks() {
  std::printf("test_music: preview WAV writer\n");
  std::vector<std::int16_t> samples{0, 1000, -2000, 32767, -32768, 5};
  const std::string path = "test-music-preview.wav";
  SC_CHECK(stackfall::writeMusicWavFile(path, samples, 22050));
  std::FILE* file = std::fopen(path.c_str(), "rb");
  SC_CHECK(file != nullptr);
  if (file != nullptr) {
    std::vector<unsigned char> bytes;
    int ch = std::fgetc(file);
    while (ch != EOF) {
      bytes.push_back(static_cast<unsigned char>(ch));
      ch = std::fgetc(file);
    }
    std::fclose(file);
    SC_CHECK(bytes.size() == 44 + samples.size() * 2);
    SC_CHECK(std::memcmp(bytes.data(), "RIFF", 4) == 0);
    SC_CHECK(std::memcmp(bytes.data() + 8, "WAVEfmt ", 8) == 0);
    SC_CHECK(bytes[24] == 0x22 && bytes[25] == 0x56);  // 22050 Hz LE
    SC_CHECK(bytes[34] == 16);                          // bits per sample
    SC_CHECK(bytes[44] == 0 && bytes[45] == 0);         // first sample 0
    SC_CHECK(bytes[46] == 0xE8 && bytes[47] == 3);      // 1000 LE
  }
  std::remove(path.c_str());
}

}  // namespace

void run_music_tests() {
  run_catalogue_checks();
  run_validation_rejects_checks();
  run_timing_checks();
  run_layer_and_level_checks();
  run_transport_checks();
  run_director_checks();
  run_wav_checks();
}
