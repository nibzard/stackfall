// Headless regression suite for the deterministic replay hash (task T008,
// milestone M1). The acceptance criteria are specification section 7 and
// section 11, item 18, desktop half:
//
//   Game::start(seed) resets all fields; equal seed plus identical
//   tick-indexed inputs must produce identical game state and score across
//   builds using the same rules version.
//
// Every round here is a replay: a compact script that expands to one
// InputFrame per tick, so a pinned scenario stays reviewable data instead of
// a page of builder calls. stateHash() folds the whole round, public and
// private fields alike, into one 64-bit FNV-1a value.
//
// The pinned values came from this suite itself. A rules change that moves a
// piece, a counter, or a point total breaks a pin on purpose; re-record the
// value deliberately through `stackfall_tests --seed <seed>` after reviewing
// the change, never by copying a new run into the table unread.
//
// The developer CLI accepts --seed <integer> (specification section 4.3).
// With it, the suite prints the refresh values of every pinned scenario and
// replays each scenario at that seed too, requiring a second identical pass.
// Two runs of `stackfall_tests --seed N` with equal output therefore show the
// reproducibility of section 7 end to end.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "game_test_peer.hpp"
#include "stackfall/board.hpp"
#include "stackfall/game.hpp"
#include "stackfall/input.hpp"
#include "stackfall/pieces.hpp"
#include "stackfall/randomizer.hpp"
#include "stackfall/scoring.hpp"
#include "stackfall/types.hpp"
#include "stackfall/version.hpp"
#include "test_util.hpp"

namespace {

using stackfall::Game;
using stackfall::GameTestPeer;
using stackfall::InputFrame;
using stackfall::Piece;
using stackfall::PieceId;
using stackfall::Scene;

// ---------------------------------------------------------------------------
// Replay scripts.
//
// A script is whitespace-separated tokens of the form "[count*]flags". The
// count repeats the frame and defaults to one. The flags of one token combine
// into a single tick's InputFrame:
//
//   .   nothing held or pressed        l r d  left, right, down held
//   c   rotate clockwise pressed       z      rotate counter-clockwise
//   x   hard drop pressed              h      hold pressed
//   p   pause pressed
//
// For example "40*. 3*l c x" is 40 idle ticks, three ticks holding left, one
// clockwise rotation, and one hard drop. A pause press wins its whole tick,
// exactly as it does in Game::tick (specification section 5).
// ---------------------------------------------------------------------------

// The largest repeat one token may carry, and the largest replay the parser
// may build. A script is data, so a typo must fail the parse instead of
// allocating an unbounded replay.
constexpr std::uint64_t kMaxTokenRepeat = 100000;
constexpr std::uint64_t kMaxFrames = 1000000;

bool applyFlag(char flag, InputFrame* frame) {
  switch (flag) {
    case '.':
      return true;  // an explicit "nothing this tick"
    case 'l':
      frame->leftHeld = true;
      return true;
    case 'r':
      frame->rightHeld = true;
      return true;
    case 'd':
      frame->downHeld = true;
      return true;
    case 'c':
      frame->rotateCWPressed = true;
      return true;
    case 'z':
      frame->rotateCCWPressed = true;
      return true;
    case 'x':
      frame->hardDropPressed = true;
      return true;
    case 'h':
      frame->holdPressed = true;
      return true;
    case 'p':
      frame->pausePressed = true;
      return true;
    default:
      return false;
  }
}

bool parseScript(const char* script, std::vector<InputFrame>* frames,
                 std::string* error) {
  frames->clear();
  error->clear();
  const char* p = script;
  while (*p != '\0') {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
      ++p;
    }
    if (*p == '\0') {
      break;
    }

    std::uint64_t repeat = 1;  // one frame when the token carries no count
    const bool hasRepeat = *p >= '0' && *p <= '9';
    if (hasRepeat) {
      repeat = 0;  // the count starts from zero, not from the default of one
      while (*p >= '0' && *p <= '9') {
        repeat = repeat * 10 + static_cast<std::uint64_t>(*p - '0');
        if (repeat > kMaxTokenRepeat) {
          *error = "a token repeats more times than the limit allows";
          return false;
        }
        ++p;
      }
      if (*p != '*') {
        *error = "a count needs '*' before its flags";
        return false;
      }
      ++p;
      if (repeat == 0) {
        *error = "a token must repeat at least once";
        return false;
      }
    }

    InputFrame frame;
    int flagCount = 0;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
      if (!applyFlag(*p, &frame)) {
        *error = std::string("unknown replay flag '") + *p + "'";
        return false;
      }
      ++flagCount;
      ++p;
    }
    if (flagCount == 0) {
      *error = "a token carries no flags";
      return false;
    }
    if (static_cast<std::uint64_t>(frames->size()) + repeat > kMaxFrames) {
      *error = "the replay grows past the frame limit";
      return false;
    }
    for (std::uint64_t i = 0; i < repeat; ++i) {
      frames->push_back(frame);
    }
  }
  if (frames->empty()) {
    *error = "a replay needs at least one tick";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// State hash, specification section 7.
//
// FNV-1a 64-bit, one byte at a time. Mixing bytes instead of whole words
// keeps the value free of machine endianness and struct layout, so the
// desktop and the ARM64 device builds hash one round to the same number
// (section 11, item 18).
// ---------------------------------------------------------------------------

constexpr std::uint64_t kHashOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kHashPrime = 1099511628211ULL;

void mixByte(std::uint64_t& hash, std::uint64_t value) {
  hash ^= value & 0xFFu;
  hash *= kHashPrime;
}

void mixValue(std::uint64_t& hash, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    mixByte(hash, value >> shift);
  }
}

void mixSigned(std::uint64_t& hash, int value) {
  // A negative coordinate converts to its two's-complement bits, so distinct
  // rows and columns never fold into one another.
  mixValue(hash, static_cast<std::uint64_t>(value));
}

// Folds every field of the round into one value: the board arrays, the active
// piece, the previews, the hold slot, the piece source, the score, line, and
// level counters, the scene, and the private tick counters of section 4.5.
// The rules version leads, so a pin breaks loudly when the rules change
// instead of surviving into a build that plays a different game.
std::uint64_t stateHash(const Game& game) {
  std::uint64_t hash = kHashOffsetBasis;
  for (const char* c = stackfall::kVersionString; *c != '\0'; ++c) {
    mixByte(hash, static_cast<std::uint64_t>(*c));
  }

  mixValue(hash, game.seed());
  mixValue(hash, static_cast<std::uint64_t>(game.scene()));
  mixValue(hash, game.tickCount());
  mixValue(hash, game.score());
  mixValue(hash, game.lines());
  mixValue(hash, static_cast<std::uint64_t>(game.level()));

  for (std::uint16_t row : game.board().occupancy()) {
    mixValue(hash, row);
  }
  for (const auto& row : game.board().cellTypes()) {
    for (std::uint8_t type : row) {
      mixByte(hash, type);
    }
  }

  mixValue(hash, game.hasActivePiece() ? 1 : 0);
  if (game.hasActivePiece()) {
    const Piece& piece = game.activePiece();
    mixValue(hash, static_cast<std::uint64_t>(piece.id));
    mixValue(hash, static_cast<std::uint64_t>(piece.rotation));
    mixSigned(hash, piece.x);
    mixSigned(hash, piece.y);
  }

  for (PieceId id : game.queue()) {
    mixValue(hash, static_cast<std::uint64_t>(id));
  }
  mixValue(hash, game.hasHoldPiece() ? 1 : 0);
  mixValue(hash, static_cast<std::uint64_t>(game.holdPieceId()));
  mixValue(hash, game.holdLocked() ? 1 : 0);

  // The piece source: the queue above plus how many pieces it has drawn. The
  // generator itself is pinned against the reference PCG32 outputs in
  // test_randomizer.cpp, so this hash only has to carry the round.
  mixValue(hash, GameTestPeer::randomizer(game).drawCount());

  mixSigned(hash, GameTestPeer::gravityTimer(game));
  mixSigned(hash, GameTestPeer::groundedTicks(game));
  mixSigned(hash, GameTestPeer::lockResets(game));
  mixSigned(hash, GameTestPeer::shiftDir(game));
  mixSigned(hash, GameTestPeer::shiftTicks(game));
  return hash;
}

// ---------------------------------------------------------------------------
// Pinned scenarios.
// ---------------------------------------------------------------------------

struct Scenario {
  const char* name;
  std::uint64_t seed;
  const char* script;
  std::uint64_t hash;   // pinned state hash
  std::uint64_t score;  // pinned score
  std::uint64_t lines;  // pinned cleared lines
};

// Five rounds that each exercise a different slice of the rules: natural
// gravity with lock delay, soft drop with movement and rotation, hold and
// pause, a stack built for line clears, and a block-out.
//
// The "stack-and-clear" script plays seed 30 on purpose: that round deals
// I, J, L, then S, O, T, Z, so three flat pieces tile the bottom row and the
// hold swap turns the S sideways to fill the one remaining column of the
// second row. Every placement is a single-tick press, because one held tick
// moves one column and only a ten-tick hold charges the DAS.
//
// The pins below are the T029 refresh (immediate spawn, 2026-11-03),
// re-pinned for the 1.0.0 version of the first release (2026-11-04): the
// version string leads the state hash, so the bump alone moved every value;
// the scores, lines, and tick counts are unchanged. Every
// piece starts one row lower, so each hard drop travels one cell less and
// scores 2 points less per drop - soft-drop-moves holds five drops (108 ->
// 98), hold-and-pause four (107 -> 99), stack-and-clear ten (559 -> 539,
// the same two cleared lines) - and the block-out round tops out one row
// earlier, after one piece less (224 -> 202, still by block-out).
constexpr Scenario kScenarios[] = {
    {"gravity-lock", 20261102ULL, "1200*.", 0xcbc3d0ac48e709d5ULL, 0, 0},
    {"soft-drop-moves", 1ULL,
     "3*l c 8*d z 2*r 10*d x 4*r c 6*d x 2*l z 12*d x 20*. 5*l 9*d x "
     "3*r c 7*d z x 25*.",
     0x73c9f9b3446d866fULL, 98, 0},
    {"hold-and-pause", 424242ULL,
     "h 6*d x 30*. p 40*. p h 8*d x 15*. 4*r c 5*d x h x 60*.",
     0x3fe169192382b9d1ULL, 99, 0},
    {"stack-and-clear", 30ULL,
     "l . l . l . x r . x r . r . r . r . x h r . r . r . x l . l . l . x "
     "r . x h c l . l . x l . l . c d d x d d d x 25*. d d x 40*.",
     0xeab46107fc034bc1ULL, 539, 2},
    {"block-out", 999ULL, "120*x 300*.", 0x108537ace8574743ULL, 202, 0},
};

constexpr int kScenarioCount =
    static_cast<int>(sizeof(kScenarios) / sizeof(kScenarios[0]));

// The scenario that is still mid-round; sensitivity checks truncate or extend
// it, which only means something while the round is alive.
constexpr int kLivingScenario = 0;

struct Outcome {
  std::uint64_t hash;
  std::uint64_t score;
  std::uint64_t lines;
  int level;
  std::uint64_t ticks;
  Scene scene;
};

bool sameOutcome(const Outcome& a, const Outcome& b) {
  return a.hash == b.hash && a.score == b.score && a.lines == b.lines &&
         a.level == b.level && a.ticks == b.ticks && a.scene == b.scene;
}

const char* sceneName(Scene scene) {
  switch (scene) {
    case Scene::Boot:
      return "boot";
    case Scene::Title:
      return "title";
    case Scene::Controls:
      return "controls";
    case Scene::Options:
      return "options";
    case Scene::Playing:
      return "playing";
    case Scene::Paused:
      return "paused";
    case Scene::PauseControls:
      return "pause-controls";
    case Scene::MusicOptions:
      return "music-options";
    case Scene::GameOver:
      return "game-over";
  }
  return "unknown";
}

Outcome playScenario(std::uint64_t seed, const std::vector<InputFrame>& frames) {
  Game game;
  game.start(seed);
  for (const InputFrame& frame : frames) {
    game.tick(frame);
  }
  return Outcome{stateHash(game), game.score(), game.lines(), game.level(),
                 game.tickCount(), game.scene()};
}

void printOutcome(const char* name, std::uint64_t seed, const Outcome& outcome) {
  std::printf("  %-17s seed %-10llu hash 0x%016llx  score %-8llu lines %-3llu "
              "level %-2d ticks %-6llu %s\n",
              name, static_cast<unsigned long long>(seed),
              static_cast<unsigned long long>(outcome.hash),
              static_cast<unsigned long long>(outcome.score),
              static_cast<unsigned long long>(outcome.lines), outcome.level,
              static_cast<unsigned long long>(outcome.ticks),
              sceneName(outcome.scene));
}

// ---------------------------------------------------------------------------
// Checks.
// ---------------------------------------------------------------------------

void check_script_parser() {
  std::vector<InputFrame> frames;
  std::string error;

  SC_CHECK(parseScript(".", &frames, &error));
  SC_CHECK(frames.size() == 1);
  SC_CHECK(frames[0].leftHeld == false && frames[0].pausePressed == false);

  SC_CHECK(parseScript("3*l d", &frames, &error));
  SC_CHECK(frames.size() == 4);
  SC_CHECK(frames[0].leftHeld && frames[2].leftHeld);
  SC_CHECK(!frames[3].leftHeld && frames[3].downHeld);

  // One token may combine a held direction with a press.
  SC_CHECK(parseScript("lc", &frames, &error));
  SC_CHECK(frames.size() == 1);
  SC_CHECK(frames[0].leftHeld && frames[0].rotateCWPressed);

  // Every action flag maps to its own field.
  SC_CHECK(parseScript("r z x h p", &frames, &error));
  SC_CHECK(frames.size() == 5);
  SC_CHECK(frames[0].rightHeld);
  SC_CHECK(frames[1].rotateCCWPressed);
  SC_CHECK(frames[2].hardDropPressed);
  SC_CHECK(frames[3].holdPressed);
  SC_CHECK(frames[4].pausePressed);

  // Whitespace around tokens is free.
  SC_CHECK(parseScript("  2*x   3*.  ", &frames, &error));
  SC_CHECK(frames.size() == 5);

  // A malformed script reports a reason and builds nothing.
  SC_CHECK(!parseScript("", &frames, &error));
  SC_CHECK(!error.empty());
  SC_CHECK(!parseScript("3l", &frames, &error));
  SC_CHECK(!parseScript("0*c", &frames, &error));
  SC_CHECK(!parseScript("12*", &frames, &error));
  SC_CHECK(!parseScript("q", &frames, &error));
  SC_CHECK(!parseScript("2*c 4", &frames, &error));
  SC_CHECK(!parseScript("100001*x", &frames, &error));
  SC_CHECK(frames.empty());
}

void check_pinned_replays_hold(
    const std::vector<std::vector<InputFrame>>& scripts) {
  for (int i = 0; i < kScenarioCount; ++i) {
    const Scenario& scenario = kScenarios[i];
    const Outcome outcome = playScenario(scenario.seed, scripts[i]);

    SC_CHECK(outcome.hash == scenario.hash);
    SC_CHECK(outcome.score == scenario.score);
    SC_CHECK(outcome.lines == scenario.lines);
    if (outcome.hash != scenario.hash || outcome.score != scenario.score ||
        outcome.lines != scenario.lines) {
      std::fprintf(stderr,
                   "  scenario \"%s\" drifted; refresh values:\n", scenario.name);
      printOutcome(scenario.name, scenario.seed, outcome);
    }
  }
}

void check_repeated_runs_agree(
    const std::vector<std::vector<InputFrame>>& scripts) {
  for (int i = 0; i < kScenarioCount; ++i) {
    const Scenario& scenario = kScenarios[i];
    const Outcome first = playScenario(scenario.seed, scripts[i]);
    const Outcome second = playScenario(scenario.seed, scripts[i]);
    SC_CHECK(sameOutcome(first, second));

    // A restart returns every field to its opening value, so a game that ran
    // another round first still reproduces this one (specification section
    // 4.3 and section 11, item 9).
    Game game;
    game.start(scenario.seed + 1);
    for (const InputFrame& frame : scripts[i]) {
      game.tick(frame);
    }
    game.start(scenario.seed);
    for (const InputFrame& frame : scripts[i]) {
      game.tick(frame);
    }
    const Outcome restarted = Outcome{stateHash(game),      game.score(),
                                      game.lines(),         game.level(),
                                      game.tickCount(),     game.scene()};
    SC_CHECK(sameOutcome(first, restarted));
  }
}

void check_hash_separates_rounds(
    const std::vector<std::vector<InputFrame>>& scripts) {
  const Scenario& scenario = kScenarios[kLivingScenario];
  const std::vector<InputFrame>& frames = scripts[kLivingScenario];

  const Outcome round = playScenario(scenario.seed, frames);
  SC_CHECK(round.scene == Scene::Playing);

  // A different seed deals a different piece sequence.
  const Outcome otherSeed = playScenario(scenario.seed + 1, frames);
  SC_CHECK(otherSeed.hash != round.hash);

  // A shorter or longer input stream is a different round.
  const std::vector<InputFrame> shorter(frames.begin(),
                                        frames.end() - 30);
  const Outcome truncated = playScenario(scenario.seed, shorter);
  SC_CHECK(truncated.hash != round.hash);

  std::vector<InputFrame> longer(frames);
  longer.push_back(InputFrame::idle());
  const Outcome extended = playScenario(scenario.seed, longer);
  SC_CHECK(extended.hash != round.hash);
}

void check_hash_covers_the_state(
    const std::vector<std::vector<InputFrame>>& scripts) {
  const Scenario& scenario = kScenarios[kLivingScenario];
  const std::vector<InputFrame>& frames = scripts[kLivingScenario];

  Game base;
  base.start(scenario.seed);
  for (const InputFrame& frame : frames) {
    base.tick(frame);
  }
  const std::uint64_t baseline = stateHash(base);
  // The last mutation below plays one more tick, so the round must be alive.
  SC_CHECK(base.scene() == Scene::Playing);

  // One settled cell more on the board.
  {
    Game mutated = base;
    bool settled = false;
    for (int y = stackfall::kBoardHeight - 1; y >= 0 && !settled; --y) {
      for (int x = 0; x < stackfall::kBoardWidth && !settled; ++x) {
        if (!mutated.board().isSettled(x, y)) {
          SC_CHECK(GameTestPeer::board(mutated).settleCell(x, y, PieceId::T));
          settled = true;
        }
      }
    }
    SC_CHECK(settled);
    SC_CHECK(stateHash(mutated) != baseline);
  }

  // One cleared line more, with the level it implies.
  {
    Game mutated = base;
    GameTestPeer::setLines(mutated, base.lines() + 1);
    SC_CHECK(stateHash(mutated) != baseline);
  }

  // The hold slot and its once-per-piece lock.
  {
    Game mutated = base;
    GameTestPeer::setHoldSlot(mutated, PieceId::L);
    SC_CHECK(stateHash(mutated) != baseline);
    Game locked = base;
    GameTestPeer::setHoldLocked(locked, !base.holdLocked());
    SC_CHECK(stateHash(locked) != baseline);
  }

  // The active piece, moved one column.
  {
    Game mutated = base;
    if (base.hasActivePiece()) {
      const Piece moved{base.activePiece().id, base.activePiece().rotation,
                        base.activePiece().x - 1, base.activePiece().y};
      GameTestPeer::setPiece(mutated, moved);
    } else {
      GameTestPeer::setPiece(mutated,
                             stackfall::makeSpawnPiece(PieceId::O));
    }
    SC_CHECK(stateHash(mutated) != baseline);
  }

  // The private tick counters of section 4.5.
  {
    Game mutated = base;
    GameTestPeer::setTickCounters(mutated, GameTestPeer::gravityTimer(base) + 1,
                                  GameTestPeer::groundedTicks(base) + 1,
                                  GameTestPeer::lockResets(base) + 1,
                                  GameTestPeer::shiftDir(base) + 1,
                                  GameTestPeer::shiftTicks(base) + 1);
    SC_CHECK(stateHash(mutated) != baseline);
  }

  // The previews and the piece source draw count.
  {
    Game mutated = base;
    mutated.tick(InputFrame::idle());
    SC_CHECK(stateHash(mutated) != baseline);
  }
}

void run_custom_seed_pass(std::uint64_t seed,
                          const std::vector<std::vector<InputFrame>>& scripts) {
  std::printf("test_regression: --seed %llu reproducibility pass\n",
              static_cast<unsigned long long>(seed));
  for (int i = 0; i < kScenarioCount; ++i) {
    const Outcome first = playScenario(seed, scripts[i]);
    const Outcome second = playScenario(seed, scripts[i]);
    SC_CHECK(sameOutcome(first, second));
    printOutcome(kScenarios[i].name, seed, first);
  }
}

void print_refresh_values(const std::vector<std::vector<InputFrame>>& scripts) {
  std::printf("test_regression: pinned scenarios, refresh values\n");
  for (int i = 0; i < kScenarioCount; ++i) {
    printOutcome(kScenarios[i].name, kScenarios[i].seed,
                 playScenario(kScenarios[i].seed, scripts[i]));
  }
}

}  // namespace

void run_regression_tests(bool hasCustomSeed, std::uint64_t customSeed) {
  std::printf("test_regression: state hash after pinned replays\n");

  std::vector<std::vector<InputFrame>> scripts;
  for (const Scenario& scenario : kScenarios) {
    std::vector<InputFrame> frames;
    std::string error;
    if (!parseScript(scenario.script, &frames, &error)) {
      std::fprintf(stderr, "FAIL scenario \"%s\" script: %s\n", scenario.name,
                   error.c_str());
      SC_CHECK(false);
      return;
    }
    scripts.push_back(std::move(frames));
  }

  check_script_parser();
  check_pinned_replays_hold(scripts);
  check_repeated_runs_agree(scripts);
  check_hash_separates_rounds(scripts);
  check_hash_covers_the_state(scripts);

  if (hasCustomSeed) {
    print_refresh_values(scripts);
    run_custom_seed_pass(customSeed, scripts);
  }
}
