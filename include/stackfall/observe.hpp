#pragma once

// Structured gameplay observations for the unattended test harness
// (docs/AGENT_TESTING.md, T023). One checkpoint record carries every field an
// automated session asserts on: the scene, the tick, the score, lines, and
// level counters, the active piece and its ghost, the hold slot, the preview
// queue, the menu cursor, and the full settled board as occupancy masks and
// color codes.
//
// The formatter lives in stackfall_core, next to the Game it reads, so the
// game binary and the headless reference replay emit byte-identical records
// from the same state. The harness compares the two streams field by field:
// the reference proves what the rules core must do with a script's inputs,
// the game proves the real SDL event path did exactly that, and a mismatch
// names the field that diverged.
//
// Record shape, one line per checkpoint, fields separated by single spaces:
//
//   check=playing-1 scene=playing tick=41 score=0 lines=0 level=1 high=0
//     record=0 active=I,0,3,-2 ghost=I,0,3,18 hold=none queue=O,T,S,Z,J
//     menu=none:-1 quit=0 settled=0 board=0000.0000.colors=0000...
//
// `board=` prints 22 groups of four lowercase hex digits, one per stored row
// from y=-2 down to y=19; `colors=` prints one hex digit per cell in the same
// order. The harness's independent expectations pin whole records or single
// fields, never a whole file, so a new checkpoint cannot break old pins.

#include <string>

#include "stackfall/game.hpp"

namespace stackfall {

// One checkpoint record of `game`, named `checkName`. Read-only.
std::string observationLine(const std::string& checkName, const Game& game);

// The `scene=` field of a record for `scene`.
const char* observationSceneName(Scene scene);

}  // namespace stackfall
