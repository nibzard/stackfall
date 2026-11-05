// save_probe: measure the cost of one durable record save (task T065).
//
// A developer tool of the performance evidence, in the shape of
// tools/music/render_previews.cpp: not a test, not shipped, and built from
// the same storage code the game runs, so a number it prints on the desktop
// and a number it prints on the RG503 measure the same work.
//
//   stackfall_save_probe DIRECTORY COUNT
//
// Two measurements over COUNT rounds:
//
//   store   HighScoreStore::save(), the exact path task T052 runs on the
//           gameplay thread: temporary file, write, flush, fsync, close,
//           rename.
//   plain   the same write without the fsync, as the diagnostic that names
//           the expensive step. The game never takes this path; the number
//           exists to show how much of `store` the durability costs.
//
// Output is one line per measurement plus a summary the harness parses:
//
//   save-probe store calls=200 p50=...us p95=...us p99=...us max=...us
//   save-probe plain calls=200 p50=...us ...
//   save-probe summary store_p50_us=... plain_p50_us=...

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "stackfall/storage.hpp"

namespace {

double percentile(std::vector<double>& samples, double fraction) {
  if (samples.empty()) {
    return 0.0;
  }
  const std::size_t index = static_cast<std::size_t>(
      fraction * static_cast<double>(samples.size() - 1));
  return samples[index];
}

void report(const char* name, std::vector<double>* samples) {
  std::sort(samples->begin(), samples->end());
  std::printf(
      "save-probe %s calls=%zu p50=%.1fus p95=%.1fus p99=%.1fus max=%.1fus\n",
      name, samples->size(), percentile(*samples, 0.50),
      percentile(*samples, 0.95), percentile(*samples, 0.99),
      samples->empty() ? 0.0 : samples->back());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: save_probe DIRECTORY COUNT\n");
    return 2;
  }
  const std::string directory = argv[1];
  const int count = std::atoi(argv[2]);
  if (count <= 0 || count > 100000) {
    std::fprintf(stderr, "save_probe: COUNT must be 1..100000\n");
    return 2;
  }
  const int rounds = count;

  // The shipped path, through the store the game uses.
  stackfall::HighScoreStore store(directory);
  std::vector<double> storeSamples;
  storeSamples.reserve(static_cast<std::size_t>(rounds));
  for (int i = 0; i < rounds; ++i) {
    const auto start = std::chrono::steady_clock::now();
    store.save(static_cast<std::uint64_t>(i) * 7 + 1);
    const auto end = std::chrono::steady_clock::now();
    storeSamples.push_back(
        std::chrono::duration<double, std::micro>(end - start).count());
  }

  // The diagnostic path: the same bytes, no fsync.
  const std::string target = directory + "/probe-plain.dat";
  const std::string temporary = directory + "/.probe-plain.dat.tmp";
  std::vector<double> plainSamples;
  plainSamples.reserve(static_cast<std::size_t>(rounds));
  for (int i = 0; i < rounds; ++i) {
    char text[64];
    const int length = std::snprintf(text, sizeof(text),
                                     "version=1\nhigh_score=%d\n", i * 7 + 1);
    const auto start = std::chrono::steady_clock::now();
    if (std::FILE* file = std::fopen(temporary.c_str(), "wb")) {
      std::fwrite(text, 1, static_cast<std::size_t>(length), file);
      std::fflush(file);
      std::fclose(file);
      std::rename(temporary.c_str(), target.c_str());
    }
    const auto end = std::chrono::steady_clock::now();
    plainSamples.push_back(
        std::chrono::duration<double, std::micro>(end - start).count());
  }
  std::remove(target.c_str());
  std::remove(temporary.c_str());

  report("store", &storeSamples);
  report("plain", &plainSamples);
  std::printf("save-probe summary store_p50_us=%.1f plain_p50_us=%.1f\n",
              percentile(storeSamples, 0.50), percentile(plainSamples, 0.50));
  return 0;
}
