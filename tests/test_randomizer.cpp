// Headless suite for the PCG32 generator and the seven-bag queue (task T006,
// milestone M1). The acceptance criterion is specification section 11,
// item 13:
//
//   13. Each seven-draw bag is a permutation of I/O/T/S/Z/J/L; seeded
//       sequences match fixtures.
//
// The generator fixtures are not values this implementation picked for
// itself: the six outputs for seed 42 on stream 54 are the documented sample
// round of the reference pcg_basic.c
// (https://www.pcg-random.org/using-pcg-c-basic.html), and the remaining raw
// and bounded vectors come from that reference compiled and run next to this
// suite. The suite therefore fails if the generator is not the real
// PCG-XSH-RR 64/32. The piece fixtures and the shuffle statistics were
// produced after that check passed, and they hold on every machine because
// the core contains no platform-dependent randomness.

#include <array>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "stackfall/pieces.hpp"
#include "stackfall/randomizer.hpp"
#include "stackfall/types.hpp"
#include "test_util.hpp"

namespace {

using stackfall::Pcg32;
using stackfall::PieceId;
using stackfall::Randomizer;

constexpr int kBag = stackfall::kBagSize;
constexpr int kQueue = stackfall::kPreviewCount;

// ---------------------------------------------------------------------------
// Generator checks.
// ---------------------------------------------------------------------------

// The documented sample round of the reference implementation: seed 42,
// stream 54, six outputs.
constexpr std::uint32_t kSampleRound[6] = {0xa15c02b7u, 0x7b47f409u,
                                           0xba1d3330u, 0x83d2f293u,
                                           0xbfa4784bu, 0xcbed606eu};

// Seed 1 on stream 1, from the reference pcg32_random_r.
constexpr std::uint32_t kSeedOneRound[6] = {0xc9828f91u, 0x1592e274u,
                                            0xc0262657u, 0xa5c2b6d3u,
                                            0xaf811256u, 0x6c1c2879u};

// Seed 0 on stream 0: the zero seed is a legal input like any other.
constexpr std::uint32_t kZeroRound[6] = {0xe4c14788u, 0x379c6516u,
                                         0x5c4ab3bbu, 0x601d23e0u,
                                         0x1c382b8cu, 0xd1faab16u};

// The reference pcg32_boundedrand_r on bound 7, seeded 42 on stream 54.
constexpr std::uint32_t kBoundedSeven[24] = {4, 4, 6, 0, 3, 2, 5, 1, 0, 0, 4,
                                             6, 0, 5, 5, 4, 0, 2, 4, 2, 0, 2,
                                             5, 5};

// The reference bounded generator on its worst-case bound 2^31 + 1, seeded 42
// on stream 54. That bound rejects about half of the raw outputs, so these
// values pin the rejection loop itself: a plain modulo answers other values
// here, while on bound 7 it would agree almost every time.
constexpr std::uint32_t kBoundedWorstCase[12] = {
    559678134u,  974992175u,  64156306u,   1067743306u, 1273847917u,
    1069982636u, 19922796u,   1713320025u, 2033732495u, 1836607525u,
    573805929u,  81421794u};

void checkGeneratorMatchesTheReference() {
  Pcg32 sample(42, 54);
  for (std::uint32_t expected : kSampleRound) {
    SC_CHECK(sample.next() == expected);
  }

  Pcg32 one(1, 1);
  for (std::uint32_t expected : kSeedOneRound) {
    SC_CHECK(one.next() == expected);
  }

  Pcg32 zero(0, 0);
  for (std::uint32_t expected : kZeroRound) {
    SC_CHECK(zero.next() == expected);
  }

  Pcg32 bounded(42, 54);
  for (std::uint32_t expected : kBoundedSeven) {
    SC_CHECK(bounded.below(7) == expected);
  }

  Pcg32 worstCase(42, 54);
  for (std::uint32_t expected : kBoundedWorstCase) {
    SC_CHECK(worstCase.below(0x80000001u) == expected);
  }
}

// The first `count` outputs of one generator state, for comparisons.
std::vector<std::uint32_t> rawOutputs(std::uint64_t seed, std::uint64_t stream,
                                      int count) {
  Pcg32 rng(seed, stream);
  std::vector<std::uint32_t> values;
  for (int i = 0; i < count; ++i) {
    values.push_back(rng.next());
  }
  return values;
}

void checkGeneratorReproducesAndSeparates() {
  // Equal seed and stream produce equal outputs and equal visible state.
  const std::vector<std::uint32_t> run = rawOutputs(0xB0A25EED, 7, 64);
  SC_CHECK(run == rawOutputs(0xB0A25EED, 7, 64));

  Pcg32 a(0xB0A25EED, 7);
  Pcg32 b(0xB0A25EED, 7);
  bool same = true;
  for (int i = 0; i < 64; ++i) {
    same = same && a.next() == b.next() && a.state() == b.state();
  }
  SC_CHECK(same);

  // Re-seeding an existing generator returns it to the state of a fresh one.
  a.seed(0xB0A25EED, 7);
  bool replayed = true;
  for (std::uint32_t value : run) {
    replayed = replayed && a.next() == value;
  }
  SC_CHECK(replayed);

  // Another seed on the same stream, and the same seed on another stream,
  // both give another sequence. That separation is what lets the game keep
  // cosmetic randomness off the piece stream (specification section 7).
  SC_CHECK(rawOutputs(0xB0A25EED, 8, 64) != run);
  SC_CHECK(rawOutputs(0xB0A25EEE, 7, 64) != run);
  SC_CHECK(Pcg32(5, 7).increment() != Pcg32(5, 8).increment());
  // The increment stays odd, so the generator keeps its full 2^64 period on
  // every stream.
  SC_CHECK(Pcg32(5, 7).increment() % 2 == 1);
  SC_CHECK(Pcg32(0, 0).increment() % 2 == 1);
}

void checkBoundedValuesStayInsideTheirBound() {
  // Bound 7 over a long run: every value stays inside the range and each of
  // the seven values appears about as often as the others.
  Pcg32 rng(0x0FF5EED1);
  std::array<int, 7> counts = {};
  for (int i = 0; i < 200000; ++i) {
    const std::uint32_t value = rng.below(7);
    SC_CHECK(value < 7);
    counts[value] += 1;
  }
  // Expected count 28571 with standard deviation 157, so the range covers
  // several standard deviations and still fails a generator that favors part
  // of the range.
  for (int value = 0; value < 7; ++value) {
    if (counts[value] < 27500 || counts[value] > 29700) {
      std::fprintf(stderr, "  context: value %d appeared %d times\n", value,
                   counts[value]);
    }
    SC_CHECK(counts[value] >= 27500);
    SC_CHECK(counts[value] <= 29700);
  }

  // Bound 1 has a single answer, and a bound near 2^31 keeps its range.
  Pcg32 tiny(0x11E5EED);
  for (int i = 0; i < 64; ++i) {
    SC_CHECK(tiny.below(1) == 0);
  }
  Pcg32 large(0x0FF5EED2);
  for (int i = 0; i < 20000; ++i) {
    SC_CHECK(large.below(0x80000000u) < 0x80000000u);
  }
}

// ---------------------------------------------------------------------------
// Bag and queue checks.
// ---------------------------------------------------------------------------

// The first `draws` pieces of a fresh randomizer, as one letter string.
std::string pieceString(std::uint64_t seed, int draws) {
  Randomizer randomizer(seed);
  std::string text;
  for (int i = 0; i < draws; ++i) {
    text += stackfall::pieceLetter(randomizer.nextPiece());
  }
  return text;
}

// True when the seven ids are exactly the seven piece ids, each exactly once.
bool isBagPermutation(const std::array<PieceId, kBag>& window) {
  unsigned seen = 0;
  for (PieceId id : window) {
    seen |= 1u << static_cast<unsigned>(id);
  }
  // Bits 1..7, one per piece code of PieceId.
  return seen == 0xFEu;
}

void checkQueueAlwaysShowsFivePieces() {
  const std::uint64_t seeds[] = {0, 1, 42, 0xDEADBEEF, 0xFFFFFFFFFFFFFFFFull};
  for (std::uint64_t seed : seeds) {
    Randomizer randomizer(seed);
    SC_CHECK(randomizer.drawCount() == 0);
    for (int draw = 0; draw < 200; ++draw) {
      SC_CHECK(static_cast<int>(randomizer.queue().size()) == kQueue);
      for (PieceId id : randomizer.queue()) {
        SC_CHECK(static_cast<int>(id) >= 1);
        SC_CHECK(static_cast<int>(id) <= stackfall::kPieceCount);
      }
      // The front of the queue is the piece the next draw hands out, and the
      // queue fills up again behind it.
      const PieceId expected = randomizer.queue()[0];
      const PieceId drawn = randomizer.nextPiece();
      SC_CHECK(drawn == expected);
      SC_CHECK(randomizer.drawCount() ==
               static_cast<std::uint64_t>(draw) + 1);
      SC_CHECK(static_cast<int>(randomizer.queue().size()) == kQueue);
    }
  }
}

// The five previewed pieces are exactly the next five pieces drawn, in order,
// even when the preview and the draws straddle a bag boundary.
void checkQueuePredictsTheComingFiveDraws() {
  Randomizer randomizer(42);
  for (int round = 0; round < 400; ++round) {
    const std::array<PieceId, kQueue> preview = randomizer.queue();
    bool matches = true;
    for (int i = 0; i < kQueue; ++i) {
      matches = matches && randomizer.nextPiece() == preview[i];
    }
    if (!matches) {
      std::fprintf(stderr, "  context: round %d\n", round);
    }
    SC_CHECK(matches);
  }
}

// Every window of seven aligned draws is one shuffled bag, so no piece
// repeats inside a bag and no piece is missing from it (specification section
// 4.3, section 11 item 13).
void checkEveryBagIsAPermutation() {
  const std::uint64_t seeds[] = {1, 42, 0xC0FFEE};
  for (std::uint64_t seed : seeds) {
    Randomizer randomizer(seed);
    const int kBags = 3000;
    for (int bag = 0; bag < kBags; ++bag) {
      std::array<PieceId, kBag> window = {};
      for (int i = 0; i < kBag; ++i) {
        window[i] = randomizer.nextPiece();
      }
      if (!isBagPermutation(window)) {
        std::fprintf(stderr, "  context: seed %llu, bag %d\n",
                     static_cast<unsigned long long>(seed), bag);
      }
      SC_CHECK(isBagPermutation(window));
    }
  }
}

// Fixed seeds reproduce the piece sequence exactly, through bag boundaries
// and across independent instances (section 11, item 13).
void checkSeededSequencesMatchFixtures() {
  struct Fixture {
    std::uint64_t seed;
    const char* queue;
    const char* draws;
  };
  const Fixture fixtures[] = {
      {1, "STLIJ", "STLIJZOSITZLOJJISTOZL"},
      {42, "TIOJL", "TIOJLSZTOLIZSJTJLZSOI"},
      {0xC0FFEE, "TOIJS", "TOIJSZLILZTJSOZJOILST"},
  };

  for (const Fixture& fixture : fixtures) {
    Randomizer randomizer(fixture.seed);
    std::string queue;
    for (PieceId id : randomizer.queue()) {
      queue += stackfall::pieceLetter(id);
    }
    if (queue != fixture.queue) {
      std::fprintf(stderr, "  context: seed %llu queue %s, fixture %s\n",
                   static_cast<unsigned long long>(fixture.seed), queue.c_str(),
                   fixture.queue);
    }
    SC_CHECK(queue == fixture.queue);

    const std::string draws = pieceString(fixture.seed, 21);
    if (draws != fixture.draws) {
      std::fprintf(stderr, "  context: seed %llu draws %s, fixture %s\n",
                   static_cast<unsigned long long>(fixture.seed),
                   draws.c_str(), fixture.draws);
    }
    SC_CHECK(draws == fixture.draws);
  }

  // Ten whole bags in one string.
  SC_CHECK(pieceString(42, 70) ==
           "TIOJLSZTOLIZSJTJLZSOILTOSJIZTLSIJOZLSIJTOZJZOTILSLTJSOZIOTZISJLOZL"
           "TJSI");

  // A second instance replays the same string: the sequence comes from the
  // seed alone, with no hidden shared state.
  SC_CHECK(pieceString(42, 70) == pieceString(42, 70));

  // Two instances on the same seed agree draw by draw, even though they are
  // used in turn.
  Randomizer first(77);
  Randomizer second(77);
  bool agree = true;
  for (int i = 0; i < 100; ++i) {
    agree = agree && first.nextPiece() == second.nextPiece();
  }
  SC_CHECK(agree);
}

void checkDistinctSeedsGiveDistinctSequences() {
  std::vector<std::string> bags;
  for (std::uint64_t seed = 1; seed <= 16; ++seed) {
    bags.push_back(pieceString(seed, kBag));
  }
  bool allDistinct = true;
  for (std::size_t i = 0; i < bags.size(); ++i) {
    for (std::size_t j = i + 1; j < bags.size(); ++j) {
      allDistinct = allDistinct && bags[i] != bags[j];
    }
  }
  SC_CHECK(allDistinct);
}

// The shuffle spreads the pieces and the orders evenly. A biased shuffle
// that draws its swap partner from the whole array instead of the remaining
// part puts the same piece first in about four of ten bags and reaches fewer
// of the 7! orders, so both fixed ranges below fail it.
void checkShuffleSpreadsThePieces() {
  Randomizer randomizer(42);
  const int kBags = 10000;
  std::array<int, 8> firstCount = {};
  std::set<std::string> orders;

  for (int bag = 0; bag < kBags; ++bag) {
    std::array<PieceId, kBag> window = {};
    std::string text;
    for (int i = 0; i < kBag; ++i) {
      window[i] = randomizer.nextPiece();
      text += stackfall::pieceLetter(window[i]);
    }
    firstCount[static_cast<std::size_t>(window[0])] += 1;
    orders.insert(text);
  }

  // Each piece leads a bag about one time in seven: expected 1429 of 10000.
  for (int id = 1; id <= stackfall::kPieceCount; ++id) {
    if (firstCount[id] < 1250 || firstCount[id] > 1600) {
      std::fprintf(stderr, "  context: piece %d led %d bags\n", id,
                   firstCount[id]);
    }
    SC_CHECK(firstCount[id] >= 1250);
    SC_CHECK(firstCount[id] <= 1600);
  }

  // The orders vary: 10000 uniform draws from 5040 orders reach about 4350
  // distinct ones.
  if (orders.size() < 4050 || orders.size() > 4700) {
    std::fprintf(stderr, "  context: %zu distinct orders\n", orders.size());
  }
  SC_CHECK(orders.size() >= 4050);
  SC_CHECK(orders.size() <= 4700);
}

// A restart replays the sequence of the seed it was given.
void checkResetReplaysTheSamePieces() {
  Randomizer randomizer(0x5EED0042);
  std::string firstRun;
  for (int i = 0; i < 60; ++i) {
    firstRun += stackfall::pieceLetter(randomizer.nextPiece());
  }
  SC_CHECK(randomizer.drawCount() == 60);

  randomizer.reset(0x5EED0042);
  SC_CHECK(randomizer.drawCount() == 0);
  std::string secondRun;
  for (int i = 0; i < 60; ++i) {
    secondRun += stackfall::pieceLetter(randomizer.nextPiece());
  }
  if (secondRun != firstRun) {
    std::fprintf(stderr, "  context: reset replayed %s instead of %s\n",
                 secondRun.c_str(), firstRun.c_str());
  }
  SC_CHECK(secondRun == firstRun);

  // A reset to another seed starts that seed's fixture instead.
  randomizer.reset(1);
  std::string otherRun;
  for (int i = 0; i < 21; ++i) {
    otherRun += stackfall::pieceLetter(randomizer.nextPiece());
  }
  SC_CHECK(otherRun == std::string("STLIJZOSITZLOJJISTOZL"));
}

}  // namespace

void run_randomizer_tests() {
  checkGeneratorMatchesTheReference();
  checkGeneratorReproducesAndSeparates();
  checkBoundedValuesStayInsideTheirBound();
  checkQueueAlwaysShowsFivePieces();
  checkQueuePredictsTheComingFiveDraws();
  checkEveryBagIsAPermutation();
  checkSeededSequencesMatchFixtures();
  checkDistinctSeedsGiveDistinctSequences();
  checkShuffleSpreadsThePieces();
  checkResetReplaysTheSamePieces();
}
