// PCG32 and the seven-bag piece source for the STACKFALL rules core
// (specification section 4.3).
//
// The generator is transcribed from the reference pcg_basic.c
// (https://www.pcg-random.org) with its constants and formulas verbatim, so
// a seed fixes the piece sequence on every machine. The test suite pins the
// documented output values of that reference implementation, so a deviation
// from the real PCG-XSH-RR 64/32 fails the suite.

#include "stackfall/randomizer.hpp"

#include <cassert>
#include <utility>

namespace stackfall {

namespace {

// The LCG step of PCG32: state <- state * multiplier + increment.
constexpr std::uint64_t kMultiplier = 6364136223846793005ULL;

}  // namespace

// ---------------------------------------------------------------------------
// Pcg32.
// ---------------------------------------------------------------------------

Pcg32::Pcg32(std::uint64_t seed, std::uint64_t stream) {
  Pcg32::seed(seed, stream);
}

void Pcg32::seed(std::uint64_t seed, std::uint64_t stream) {
  // The reference seeding: state zero on an odd increment, one advance, add
  // the seed, one more advance. The odd increment keeps the LCG full period
  // whatever stream was chosen.
  state_ = 0;
  increment_ = (stream << 1) | 1;
  next();
  state_ += seed;
  next();
}

std::uint32_t Pcg32::next() {
  // Advance the state first, but permute the old state for output: xorshift
  // the high bits, then rotate by the top five bits of that old state.
  const std::uint64_t oldState = state_;
  state_ = oldState * kMultiplier + increment_;
  const std::uint32_t xorshifted =
      static_cast<std::uint32_t>(((oldState >> 18) ^ oldState) >> 27);
  const std::uint32_t rotation = static_cast<std::uint32_t>(oldState >> 59);
  return (xorshifted >> rotation) | (xorshifted << ((32 - rotation) & 31));
}

std::uint32_t Pcg32::below(std::uint32_t bound) {
  assert(bound != 0 && "below needs a positive bound");
  if (bound == 0) {
    return 0;
  }
  // Outputs below the threshold would make the remainder of a modulo biased.
  // The threshold is 2^32 mod bound, computed as (2^32 - bound) mod bound so
  // the arithmetic stays inside 32 bits, exactly like the reference
  // pcg32_boundedrand_r.
  const std::uint32_t threshold =
      static_cast<std::uint32_t>(0u - bound) % bound;
  for (;;) {
    const std::uint32_t value = next();
    if (value >= threshold) {
      return value % bound;
    }
  }
}

std::uint64_t Pcg32::state() const {
  return state_;
}

std::uint64_t Pcg32::increment() const {
  return increment_;
}

// ---------------------------------------------------------------------------
// Randomizer.
// ---------------------------------------------------------------------------

Randomizer::Randomizer(std::uint64_t seed) : generator_(seed) {
  restartQueue();
}

void Randomizer::reset(std::uint64_t seed) {
  generator_.seed(seed);
  restartQueue();
}

void Randomizer::restartQueue() {
  // The assignment keeps the whole object reproducible byte for byte, which
  // the state hashing of the regression runner relies on. bagIndex_ at
  // kBagSize marks the bag as spent, so the first draw shuffles a fresh one.
  bag_ = kAllPieceIds;
  bagIndex_ = kBagSize;
  drawCount_ = 0;
  for (int i = 0; i < kPreviewCount; ++i) {
    queue_[i] = drawFromBag();
  }
}

PieceId Randomizer::nextPiece() {
  const PieceId drawn = queue_[0];
  for (int i = 1; i < kPreviewCount; ++i) {
    queue_[i - 1] = queue_[i];
  }
  queue_[kPreviewCount - 1] = drawFromBag();
  ++drawCount_;
  return drawn;
}

PieceId Randomizer::drawFromBag() {
  // The open bag empties completely before the next shuffle, so a piece
  // cannot repeat inside one bag of seven draws (specification section 4.3).
  if (bagIndex_ == kBagSize) {
    refillBag();
  }
  return bag_[static_cast<std::size_t>(bagIndex_++)];
}

void Randomizer::refillBag() {
  // Fisher-Yates from the last slot down: swap each slot with a uniformly
  // chosen slot at or before it, and each of the 7! orders of the seven ids
  // comes out equally often.
  bag_ = kAllPieceIds;
  for (int i = kBagSize - 1; i > 0; --i) {
    const int j = static_cast<int>(
        generator_.below(static_cast<std::uint32_t>(i) + 1u));
    std::swap(bag_[static_cast<std::size_t>(i)],
              bag_[static_cast<std::size_t>(j)]);
  }
  bagIndex_ = 0;
}

const std::array<PieceId, kPreviewCount>& Randomizer::queue() const {
  return queue_;
}

std::uint64_t Randomizer::drawCount() const {
  return drawCount_;
}

}  // namespace stackfall
