#pragma once

// Developer board fixtures for the unattended test harness
// (docs/AGENT_TESTING.md, T023). A fixture is one small text file that names a
// settled board no ordinary piece sequence reaches quickly: a stack one
// vertical I away from a four-line clear, a board whose spawn cells are
// already occupied, and so on. The harness loads the file through the game
// binary's developer fixture hook and through the headless reference replay,
// so both run against byte-identical input.
//
// Format, one fixture per file named `<name>.board.txt`:
//
//   # stackfall board fixture v1
//   name: four-line-ready
//   description: four rows each missing column 5
//   provenance: hand-built for T023; independent of any piece sequence
//   # 22 rows from the hidden row y=-2 down to y=19; '.' is empty and a
//   # letter I,O,T,S,Z,J,L settles that cell in its piece color.
//   ..........
//   ... 18 more rows ...
//   SSSSS.SSSS
//   active: I
//
// Every line before the rows may carry the fields `name:`, `description:`,
// and `provenance:`, which the harness copies into its evidence, and the
// optional `active:` letter after the rows forces the piece the fixture
// leaves in play at its spawn state. Without it the active piece keeps its
// identity. The parser is strict: exactly 22 rows of exactly 10 cells, no
// unknown field, nothing after the last field, so a typo cannot quietly load
// half a board.
//
// Fixtures are developer test hooks and nothing else: no normal startup path
// reads them, and every session that uses one labels it in its evidence, as
// docs/AGENT_TESTING.md requires. Normal games from an empty board must stay
// part of the harness beside them.

#include <string>

#include "stackfall/board.hpp"
#include "stackfall/types.hpp"

namespace stackfall {

// One parsed fixture file.
struct BoardFixture {
  std::string name;         // the `name:` field, or the file's base name
  std::string description;  // the `description:` field, may be empty
  std::string provenance;   // the `provenance:` field, may be empty
  Board board;              // the settled board, consistent by construction
  bool hasActive = false;   // the `active:` field was present
  PieceId active = PieceId::I;
};

// Parses `text` as one fixture file. Returns false, leaves `*out` untouched,
// and writes the reason into `*error` on any malformed input.
bool parseBoardFixture(const std::string& text, BoardFixture* out,
                       std::string* error);

// Loads the fixture file at `path` and parses it. Same failure contract as
// parseBoardFixture().
bool loadBoardFixture(const std::string& path, BoardFixture* out,
                      std::string* error);

}  // namespace stackfall
