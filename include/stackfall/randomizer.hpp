#pragma once

// The piece source of the STACKFALL rules core (specification section 4.3):
// an explicitly coded PCG32 generator, a Fisher-Yates shuffle of the seven
// piece ids, and a next-piece queue that always holds the five previews.
//
// Everything here is fixed-width integer arithmetic. This header includes no
// <random> header and no platform-dependent distribution code, so one seed
// produces the same piece sequence on every build machine and on the device
// (specification sections 4.3 and 7).

#include <array>
#include <cstdint>

#include "stackfall/types.hpp"

namespace stackfall {

// The queue always holds this many upcoming pieces, because the preview panel
// shows the next five pieces (specification sections 2 and 4.3).
inline constexpr int kPreviewCount = 5;

// Pieces per bag. Every bag holds each of the seven piece ids exactly once
// (specification section 4.3).
inline constexpr int kBagSize = kPieceCount;

// ---------------------------------------------------------------------------
// Pcg32: the generator behind the bag shuffle.
//
// This is the PCG-XSH-RR 64/32 generator of the reference pcg_basic.c
// (https://www.pcg-random.org), written out instead of taken from a library:
// a 64-bit linear congruential state advanced by the constant multiplier
// 6364136223846793005, then permuted for output by a xorshift of the high
// bits and a rotation picked by the top bits of the old state. The test suite
// pins the generator against the documented output of the reference
// implementation, so a mistyped constant fails the suite.
// ---------------------------------------------------------------------------

// The stream the game uses for piece generation. PCG32 takes a stream
// selector next to the seed: two generators on different streams never
// produce a matching sequence, so the game can hand another stream to
// cosmetic randomness without touching the pieces (specification section 7).
// The value itself carries no meaning.
inline constexpr std::uint64_t kGameplayStream = 54;

class Pcg32 {
 public:
  // Seeds the generator for `seed` on `stream` the way the reference
  // pcg32_srandom_r does: start from state zero, advance, add the seed, and
  // advance again.
  explicit Pcg32(std::uint64_t seed,
                 std::uint64_t stream = kGameplayStream);

  // Returns the generator to the state a fresh construction with the same
  // arguments produces.
  void seed(std::uint64_t seed, std::uint64_t stream = kGameplayStream);

  // Returns the next 32-bit output.
  std::uint32_t next();

  // Returns a uniform value in [0, bound) without modulo bias, by discarding
  // the outputs below the threshold (2^32 - bound) mod bound, as the
  // reference pcg32_boundedrand_r does. `bound` must be nonzero; zero returns
  // 0 after an assertion in debug builds. The call consumes at least one
  // output.
  std::uint32_t below(std::uint32_t bound);

  // The current LCG state and increment, for tests and state hashing.
  std::uint64_t state() const;
  std::uint64_t increment() const;

 private:
  std::uint64_t state_;
  std::uint64_t increment_;
};

// ---------------------------------------------------------------------------
// Randomizer: the seven-bag source with a five-piece preview queue.
//
// One generator per game drives the shuffles. Fisher-Yates orders the seven
// ids into a bag, the bag empties completely before the next shuffle, and the
// queue holds the next five pieces at all times, including across a bag
// boundary (specification section 4.3).
// ---------------------------------------------------------------------------

class Randomizer {
 public:
  // Seeds the generator with `seed` and fills the queue.
  explicit Randomizer(std::uint64_t seed);

  // Restarts the round: the following pieces match a fresh Randomizer built
  // with the same seed.
  void reset(std::uint64_t seed);

  // Hands out the next piece, moves the queue up, and refills the tail from
  // the bag, so the queue keeps showing five pieces.
  PieceId nextPiece();

  // The five pieces after the drawn piece. queue()[0] is the piece the next
  // nextPiece() call returns.
  const std::array<PieceId, kPreviewCount>& queue() const;

  // How many pieces the source handed out since construction or reset.
  std::uint64_t drawCount() const;

 private:
  // Empties the queue, restarts the bag bookkeeping, and draws five fresh
  // pieces from a new bag.
  void restartQueue();

  // Returns the next piece out of the open bag, shuffling a new bag first
  // when the open one is empty.
  PieceId drawFromBag();

  // Refills bag_ with a Fisher-Yates shuffle of the seven ids.
  void refillBag();

  Pcg32 generator_;
  std::array<PieceId, kBagSize> bag_;  // the open bag; bag_[bagIndex_] is next
  int bagIndex_;                       // 0..kBagSize; kBagSize means empty
  std::array<PieceId, kPreviewCount> queue_;
  std::uint64_t drawCount_;
};

}  // namespace stackfall
