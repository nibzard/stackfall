// Headless suite for the developer fixture loader and the checkpoint record
// formatter of the unattended harness (task T023, docs/AGENT_TESTING.md).
// Both live in stackfall_core, so this suite stays in stackfall_tests: no
// SDL, no display, and no script driver. The suite pins the strict parse
// rules - exactly 22 rows of exactly 10 cells, letters only, no unknown
// field - because a fixture that loads half a board would quietly pass every
// session that uses it. It also pins the record format, because the game
// binary and the headless reference replay must emit byte-identical lines
// for the harness comparison to mean anything.

#include <string>

#include "test_util.hpp"

#include "stackfall/fixture.hpp"
#include "stackfall/game.hpp"
#include "stackfall/observe.hpp"
#include "stackfall/pieces.hpp"

namespace {

// The 22 board rows of the sample fixture, from y=-2 down to y=19.
const char* kSampleRows[22] = {
    "..........", "..........", "..........", "..........", "..........",
    "..........", "..........", "..........", "..........", "..........",
    "..........", "..........", "..........", "..........", "..........",
    "..........", "..........", "..........", "..........", "..........",
    "JJ........", "......Z...",
};

// A well-formed fixture with every field.
std::string sampleFixture(const char* active = "active: S\n") {
  std::string text = "# comment\n"
                     "name: sample\n"
                     "description: two cells in the bottom row\n"
                     "provenance: test fixture\n"
                     "\n";
  for (const char* row : kSampleRows) {
    text += row;
    text += '\n';
  }
  text += active;
  return text;
}

// A fixture text that parses, for mutation in the negative cases.
std::string fixtureWithoutRow(int index) {
  std::string text = sampleFixture("");
  for (int i = 21; i >= 0; --i) {
    if (i == index) {
      continue;
    }
    text = text.replace(text.rfind(kSampleRows[i]), 11, "");
  }
  return text;
}

bool parses(const std::string& text, std::string* error = nullptr) {
  stackfall::BoardFixture fixture;
  std::string message;
  const bool ok = stackfall::parseBoardFixture(text, &fixture, &message);
  if (error != nullptr) {
    *error = message;
  }
  return ok;
}

void run_parse_positive_tests() {
  stackfall::BoardFixture fixture;
  std::string error;
  const bool ok = stackfall::parseBoardFixture(sampleFixture(), &fixture, &error);
  SC_CHECK(ok);
  if (!ok) {
    std::printf("parse error: %s\n", error.c_str());
    return;
  }
  SC_CHECK(fixture.name == "sample");
  SC_CHECK(fixture.description == "two cells in the bottom row");
  SC_CHECK(fixture.provenance == "test fixture");
  SC_CHECK(fixture.hasActive);
  SC_CHECK(fixture.active == stackfall::PieceId::S);
  SC_CHECK(fixture.board.isConsistent());
  SC_CHECK(fixture.board.settledCellCount() == 3);
  // Storage follows section 4.1: J at columns 0,1 of y=18 and Z at column 6
  // of y=19.
  SC_CHECK(fixture.board.rowMask(18) == 0x0003);
  SC_CHECK(fixture.board.rowMask(19) == 0x0040);
  SC_CHECK(fixture.board.cellCodeAt(0, 18) ==
           static_cast<std::uint8_t>(stackfall::PieceId::J));
  SC_CHECK(fixture.board.cellCodeAt(6, 19) ==
           static_cast<std::uint8_t>(stackfall::PieceId::Z));
  SC_CHECK(fixture.board.cellCodeAt(5, 19) == 0);
  SC_CHECK(fixture.board.cellCodeAt(1, 18) ==
           static_cast<std::uint8_t>(stackfall::PieceId::J));

  // Without an `active:` line the fixture keeps whatever piece is in play.
  stackfall::BoardFixture bare;
  SC_CHECK(stackfall::parseBoardFixture(sampleFixture(""), &bare, &error));
  SC_CHECK(!bare.hasActive);
}

void run_parse_negative_tests() {
  std::string error;
  // Any missing row fails, whichever one it is.
  for (int row = 0; row < 22; ++row) {
    SC_CHECK(!parses(fixtureWithoutRow(row), &error));
    SC_CHECK(!error.empty());
  }
  // A digit where a letter belongs.
  {
    std::string text = sampleFixture();
    text.replace(text.find("......Z..."), 10, "......1...");
    SC_CHECK(!parses(text, &error));
  }
  // A lowercase letter.
  {
    std::string text = sampleFixture();
    text.replace(text.find("......Z..."), 10, "......z...");
    SC_CHECK(!parses(text, &error));
  }
  // A row with nine cells.
  {
    std::string text = sampleFixture();
    text.replace(text.find("......Z..."), 10, ".....Z...");
    SC_CHECK(!parses(text, &error));
  }
  // A row with eleven cells.
  {
    std::string text = sampleFixture();
    text.replace(text.find("......Z..."), 10, "......Z....");
    SC_CHECK(!parses(text, &error));
  }
  // An unknown field before the rows.
  {
    SC_CHECK(!parses("seed: 5\n" + sampleFixture(), &error));
  }
  // A field after the rows that is not `active:`.
  {
    SC_CHECK(!parses(sampleFixture() + "note: extra\n", &error));
  }
  // An `active:` value that is not one piece letter.
  {
    SC_CHECK(!parses(sampleFixture("active: Q\n"), &error));
    SC_CHECK(!parses(sampleFixture("active: SS\n"), &error));
    SC_CHECK(!parses(sampleFixture("active:\n"), &error));
  }
  // Twenty-three rows.
  {
    SC_CHECK(!parses(std::string("..........\n") + sampleFixture(), &error));
  }
  // No rows at all.
  {
    SC_CHECK(!parses("name: empty\n", &error));
  }
  // Field values tolerate surrounding spaces.
  {
    stackfall::BoardFixture fixture;
    std::string text = "name:   spaced  \n";
    for (const char* row : kSampleRows) {
      text += row;
      text += '\n';
    }
    text += "active:  I \n";
    SC_CHECK(stackfall::parseBoardFixture(text, &fixture, &error));
    SC_CHECK(fixture.name == "spaced");
    SC_CHECK(fixture.hasActive);
    SC_CHECK(fixture.active == stackfall::PieceId::I);
  }
  // Windows line endings parse the same.
  {
    std::string source = sampleFixture();
    std::string text;
    for (const char c : source) {
      text += c;
      if (c == '\n') {
        text.back() = '\r';
        text += '\n';
      }
    }
    stackfall::BoardFixture fixture;
    SC_CHECK(stackfall::parseBoardFixture(text, &fixture, &error));
    SC_CHECK(fixture.hasActive);
    SC_CHECK(fixture.board.settledCellCount() == 3);
  }
  // A full board of one letter is consistent and settles 220 cells.
  {
    std::string text = "name: full\n";
    for (int row = 0; row < 22; ++row) {
      text += "IIIIIIIIII\n";
    }
    stackfall::BoardFixture fixture;
    SC_CHECK(stackfall::parseBoardFixture(text, &fixture, &error));
    SC_CHECK(fixture.board.settledCellCount() == 220);
    SC_CHECK(fixture.board.rowMask(19) == 0x03FF);
  }
}

// The record format is the contract between the game binary and the headless
// reference replay: one line, the same fields in the same order, the same
// rendering of every value.
void run_observation_tests() {
  stackfall::Game game;
  game.openTitle(20261103);
  std::string record = stackfall::observationLine("title", game);
  SC_CHECK(record.rfind("check=title scene=title tick=0 score=0 lines=0 "
                        "level=1 high=0 record=0 active=none ghost=none "
                        "hold=none", 0) == 0);
  SC_CHECK(record.find("menu=title:0 quit=0 settled=0 board=") !=
           std::string::npos);
  // An empty board prints 22 zero groups separated by dots, then 220 zero
  // color digits.
  std::string emptyBoard;
  for (int row = 0; row < 22; ++row) {
    if (row != 0) {
      emptyBoard += '.';
    }
    emptyBoard += "0000";
  }
  SC_CHECK(record.find("board=" + emptyBoard + " colors=" +
                       std::string(220, '0')) != std::string::npos);
  // A settled cell shows in both the mask and the color digits: J at
  // columns 0,1 of y=18 sets bits 0 and 1 of group 18 and colors 2.
  {
    stackfall::BoardFixture fixture;
    std::string error;
    SC_CHECK(stackfall::parseBoardFixture(sampleFixture(), &fixture, &error));
    stackfall::Game loaded;
    loaded.openTitle(1);
    loaded.start(1);
    SC_CHECK(loaded.applyBoardFixture(fixture));
    record = stackfall::observationLine("fixture", loaded);
    // y=18 is storage group 20 of the mask string, and y=19 is group 21.
    SC_CHECK(record.find("board=" + emptyBoard.substr(0, 20 * 5) +
                         "0003.0040") != std::string::npos);
    // The color digits carry the same cells: J (code 6) twice at row y=18
    // and Z (code 5) once at y=19, zeros everywhere else.
    const std::string colors = std::string(200, '0') + "6600000000" +
                               "0000005000";
    SC_CHECK(record.find("colors=" + colors) != std::string::npos);
    // The forced S piece replaced the natural first piece at spawn.
    SC_CHECK(record.find("active=S,0,3,-1") != std::string::npos);
  }

  // A started round names its pieces with the letters the fixtures and the
  // scripts use.
  game.start(424242);
  record = stackfall::observationLine("play", game);
  SC_CHECK(record.find("scene=playing") != std::string::npos);
  const std::size_t active = record.find("active=");
  SC_CHECK(active != std::string::npos);
  if (active != std::string::npos) {
    const char letter = record[active + 7];
    bool known = false;
    for (const stackfall::PieceId id : stackfall::kAllPieceIds) {
      known = known || stackfall::pieceLetter(id) == letter;
    }
    SC_CHECK(known);
  }
  // The queue field carries five letters separated by commas.
  const std::size_t queue = record.find("queue=");
  SC_CHECK(queue != std::string::npos);
  if (queue != std::string::npos) {
    for (int i = 0; i < 5; ++i) {
      const char letter = record[queue + 6 + i * 2];
      bool known = false;
      for (const stackfall::PieceId id : stackfall::kAllPieceIds) {
        known = known || stackfall::pieceLetter(id) == letter;
      }
      SC_CHECK(known);
    }
    SC_CHECK(record[queue + 6 + 1] == ',');
    SC_CHECK(record[queue + 6 + 7] == ',');
  }
  // Every scene has a name.
  SC_CHECK(std::string(
               stackfall::observationSceneName(stackfall::Scene::Boot)) == "boot");
  SC_CHECK(std::string(
               stackfall::observationSceneName(stackfall::Scene::Controls)) ==
           "controls");
  SC_CHECK(std::string(
               stackfall::observationSceneName(stackfall::Scene::GameOver)) ==
           "gameover");
  SC_CHECK(std::string(
               stackfall::observationSceneName(stackfall::Scene::Paused)) ==
           "paused");
}

}  // namespace

void run_fixture_tests() {
  run_parse_positive_tests();
  run_parse_negative_tests();
  run_observation_tests();
}
