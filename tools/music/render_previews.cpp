// The offline music preview tool of MUSIC.md phase M2 (task T062): renders
// catalogue tracks through the same MusicEngine the audio callback runs and
// writes one WAV per track, intensity, and sample rate into an ignored
// build directory. The files are review artifacts for the listening pass
// of docs/music/; the game never reads one.
//
// Example:
//
//   build/stackfall_music_preview --out build/music-previews
//
// The tool also prints the level report of every render - peak, RMS, clip
// count, direct-current offset, voice high-water - and refuses to write a
// track whose score fails validation.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "stackfall/music.hpp"
#include "stackfall/music_director.hpp"

namespace {

using stackfall::MusicIntensity;
using stackfall::MusicOfflineRenderer;
using stackfall::MusicValidationReport;
using stackfall::musicCatalogue;
using stackfall::musicTrackIndex;
using stackfall::validateMusicCatalogue;

int parseUnsigned(const char* text, long long* out) {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  long long value = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      return false;
    }
    value = value * 10 + (*p - '0');
  }
  *out = value;
  return true;
}

// One full render and its level report.
struct RenderReport {
  std::int64_t peak = 0;
  double sumSquares = 0.0;
  double sum = 0.0;
  std::size_t clipped = 0;
  std::size_t silent = 0;
  std::uint32_t voiceHighWater = 0;
};

RenderReport renderTrack(int trackIndex, MusicIntensity intensity,
                         int sampleRate, int seconds,
                         std::vector<std::int16_t>* samples) {
  const auto& track = musicCatalogue()[static_cast<std::size_t>(trackIndex)];
  // The full arrangement plus a tail that covers the last releases.
  const double barSeconds = 240.0 / track.bpm;
  const double totalSeconds = track.bars * barSeconds;
  const double renderSeconds =
      seconds > 0 ? static_cast<double>(seconds)
                  : totalSeconds + 1.0;
  const std::size_t frames = static_cast<std::size_t>(renderSeconds *
                                                      sampleRate);
  samples->assign(frames, 0);

  MusicOfflineRenderer renderer(sampleRate);
  renderer.play(trackIndex, intensity);
  const std::size_t chunk = static_cast<std::size_t>(sampleRate) / 10;
  for (std::size_t at = 0; at < frames; at += chunk) {
    const std::size_t count = std::min(chunk, frames - at);
    renderer.render(samples->data() + at, count);
  }

  RenderReport report;
  for (const std::int16_t value : *samples) {
    const std::int64_t magnitude =
        value < 0 ? -static_cast<std::int64_t>(value)
                  : static_cast<std::int64_t>(value);
    report.peak = std::max(report.peak, magnitude);
    if (magnitude >= 32767) {
      ++report.clipped;
    }
    if (magnitude < 8) {
      ++report.silent;
    }
    report.sumSquares +=
        static_cast<double>(value) * static_cast<double>(value);
    report.sum += value;
  }
  report.voiceHighWater = renderer.engine().voiceHighWater();
  return report;
}

void printUsage() {
  std::printf(
      "Usage: stackfall_music_preview [--track ID|all] [--intensity "
      "low|medium|high|all]\n"
      "                           [--seconds N] [--rate N] [--out DIR]\n"
      "  --track ID       one catalogue track (see below) or all (default)\n"
      "  --intensity      low, medium, high, or all (default medium)\n"
      "  --seconds N      render N seconds; 0 renders the whole arrangement\n"
      "                   plus a one-second release tail (default 0)\n"
      "  --rate N         sample rate (default 44100)\n"
      "  --out DIR        output directory (default build/music-previews)\n");
  std::printf("Tracks:");
  for (const auto& track : musicCatalogue()) {
    std::printf(" %s", track.id);
  }
  std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
  const char* trackId = "all";
  const char* intensityName = "medium";
  const char* outDir = "build/music-previews";
  long long seconds = 0;
  long long rate = 44100;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    const bool needsValue = std::strcmp(arg, "--track") == 0 ||
                            std::strcmp(arg, "--intensity") == 0 ||
                            std::strcmp(arg, "--seconds") == 0 ||
                            std::strcmp(arg, "--rate") == 0 ||
                            std::strcmp(arg, "--out") == 0;
    if (needsValue && i + 1 < argc) {
      ++i;
      if (std::strcmp(arg, "--track") == 0) {
        trackId = argv[i];
      } else if (std::strcmp(arg, "--intensity") == 0) {
        intensityName = argv[i];
      } else if (std::strcmp(arg, "--out") == 0) {
        outDir = argv[i];
      } else if (std::strcmp(arg, "--rate") == 0) {
        rate = 0;
        parseUnsigned(argv[i], &rate);
      } else {
        seconds = 0;
        parseUnsigned(argv[i], &seconds);
      }
    } else if (std::strcmp(arg, "--help") == 0) {
      printUsage();
      return 0;
    } else {
      std::fprintf(stderr, "Warning: unknown option \"%s\"; ignored\n", arg);
    }
  }
  if (rate < 8000 || rate > 192000) {
    std::fprintf(stderr, "Error: --rate must be 8000..192000\n");
    return 1;
  }

  const MusicValidationReport catalogue = validateMusicCatalogue();
  if (!catalogue.ok) {
    std::fprintf(stderr, "Error: track %s failed validation: %s\n",
                 catalogue.trackId, catalogue.error);
    return 1;
  }

  const std::vector<stackfall::MusicTrackData>& tracks = musicCatalogue();
  std::vector<int> selected;
  if (std::strcmp(trackId, "all") == 0) {
    for (std::size_t i = 0; i < tracks.size(); ++i) {
      selected.push_back(static_cast<int>(i));
    }
  } else {
    const int index = musicTrackIndex(trackId);
    if (index < 0) {
      std::fprintf(stderr, "Error: unknown track \"%s\"\n", trackId);
      return 1;
    }
    selected.push_back(index);
  }

  const bool allIntensities = std::strcmp(intensityName, "all") == 0;
  const int intensityCount =
      allIntensities ? stackfall::kMusicIntensityCount : 1;
  const int firstIntensity =
      allIntensities ? 0 : static_cast<int>(MusicIntensity::Medium);

  int failures = 0;
  for (const int index : selected) {
    const auto& track = tracks[static_cast<std::size_t>(index)];
    for (int step = 0; step < intensityCount; ++step) {
      const auto intensity =
          static_cast<MusicIntensity>(firstIntensity + step);
      std::vector<std::int16_t> samples;
      const RenderReport report =
          renderTrack(index, intensity, static_cast<int>(rate),
                      static_cast<int>(seconds), &samples);
      const double rmsDb = 10.0 * std::log10(
          report.sumSquares / std::max<std::size_t>(1, samples.size()) +
          1e-12);
      const double dc = report.sum / std::max<std::size_t>(1, samples.size());
      const char* intensityText =
          step == 0 ? "low" : (step == 1 ? "medium" : "high");
      if (!allIntensities) {
        intensityText = intensityName;
      }
      std::string name = std::string(outDir) + "/" + track.id + "-" +
                         intensityText + "-" + std::to_string(rate) + ".wav";
      if (!stackfall::writeMusicWavFile(name, samples, static_cast<int>(rate))) {
        std::fprintf(stderr, "Error: cannot write \"%s\"\n", name.c_str());
        ++failures;
        continue;
      }
      std::printf(
          "%s: peak %lld (%.1f dBFS), RMS %.1f dB, DC %+.2f, clipped %zu, "
          "quiet %zu, max voices %u\n",
          name.c_str(), static_cast<long long>(report.peak),
          20.0 * std::log10(static_cast<double>(report.peak) / 32768.0 + 1e-12),
          rmsDb, dc, report.clipped, report.silent, report.voiceHighWater);
      if (report.peak < 3000) {
        std::fprintf(stderr, "Warning: %s is nearly silent\n", name.c_str());
        ++failures;
      }
      if (report.clipped > 0) {
        std::fprintf(stderr, "Warning: %s clipped %zu samples\n",
                     name.c_str(), report.clipped);
        ++failures;
      }
      if (std::fabs(dc) > 150.0) {
        std::fprintf(stderr, "Warning: %s carries a DC offset of %+.2f\n",
                     name.c_str(), dc);
        ++failures;
      }
      if (report.voiceHighWater > stackfall::kMusicMaxVoices) {
        std::fprintf(stderr, "Warning: %s used %u simultaneous voices\n",
                     name.c_str(), report.voiceHighWater);
        ++failures;
      }
    }
  }
  return failures == 0 ? 0 : 1;
}
