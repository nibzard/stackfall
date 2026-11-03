// Structured gameplay observations (docs/AGENT_TESTING.md, T023). Every field
// comes from a const query of the Game, so writing a record changes nothing
// about the round, exactly like the renderer
// (specification section 7).

#include "stackfall/observe.hpp"

#include <cstdio>

#include "stackfall/pieces.hpp"

namespace stackfall {
namespace {

// One `id,rotation,x,y` piece field, or `none`.
void appendPiece(std::string* text, const Piece* piece) {
  if (piece == nullptr) {
    *text += "none";
    return;
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%c,%d,%d,%d", pieceLetter(piece->id),
                static_cast<int>(piece->rotation), piece->x, piece->y);
  *text += buffer;
}

const char* hexDigits = "0123456789abcdef";

}  // namespace

const char* observationSceneName(Scene scene) {
  switch (scene) {
    case Scene::Boot:
      return "boot";
    case Scene::Title:
      return "title";
    case Scene::Controls:
      return "controls";
    case Scene::Playing:
      return "playing";
    case Scene::Paused:
      return "paused";
    case Scene::GameOver:
      return "gameover";
  }
  return "?";
}

std::string observationLine(const std::string& checkName, const Game& game) {
  std::string text = "check=";
  text += checkName;
  text += " scene=";
  text += observationSceneName(game.scene());
  char buffer[96];
  std::snprintf(buffer, sizeof(buffer), " tick=%llu", static_cast<unsigned long long>(game.tickCount()));
  text += buffer;
  std::snprintf(buffer, sizeof(buffer), " score=%llu",
                static_cast<unsigned long long>(game.score()));
  text += buffer;
  std::snprintf(buffer, sizeof(buffer), " lines=%llu",
                static_cast<unsigned long long>(game.lines()));
  text += buffer;
  std::snprintf(buffer, sizeof(buffer), " level=%d", game.level());
  text += buffer;
  std::snprintf(buffer, sizeof(buffer), " high=%llu",
                static_cast<unsigned long long>(game.highScore()));
  text += buffer;
  std::snprintf(buffer, sizeof(buffer), " record=%d",
                game.newRecord() ? 1 : 0);
  text += buffer;

  text += " active=";
  if (game.hasActivePiece()) {
    const Piece& active = game.activePiece();
    appendPiece(&text, &active);
  } else {
    appendPiece(&text, nullptr);
  }
  text += " ghost=";
  if (game.hasActivePiece()) {
    const Piece ghost = game.ghostPiece();
    appendPiece(&text, &ghost);
  } else {
    appendPiece(&text, nullptr);
  }

  text += " hold=";
  if (game.hasHoldPiece()) {
    text += pieceLetter(game.holdPieceId());
    text += game.holdLocked() ? ",locked" : ",free";
  } else {
    text += "none";
  }

  text += " queue=";
  const auto& queue = game.queue();
  for (std::size_t i = 0; i < queue.size(); ++i) {
    if (i != 0) {
      text += ',';
    }
    text += pieceLetter(queue[i]);
  }

  // The cursor of the menu in charge, or `none:-1` while a round runs.
  text += " menu=";
  switch (game.scene()) {
    case Scene::Title:
      text += "title:";
      text += std::to_string(game.titleSelection());
      break;
    case Scene::Paused:
      text += "pause:";
      text += std::to_string(game.pauseSelection());
      break;
    case Scene::GameOver:
      text += "over:";
      text += std::to_string(game.overSelection());
      break;
    default:
      text += "none:-1";
      break;
  }
  std::snprintf(buffer, sizeof(buffer), " quit=%d",
                game.quitRequested() ? 1 : 0);
  text += buffer;

  const Board& board = game.board();
  std::snprintf(buffer, sizeof(buffer), " settled=%d",
                board.settledCellCount());
  text += buffer;

  text += " board=";
  const auto& occupancy = board.occupancy();
  for (int row = 0; row < kBoardRows; ++row) {
    const std::uint16_t mask = occupancy[row];
    if (row != 0) {
      text += '.';
    }
    text += hexDigits[(mask >> 12) & 0xF];
    text += hexDigits[(mask >> 8) & 0xF];
    text += hexDigits[(mask >> 4) & 0xF];
    text += hexDigits[mask & 0xF];
  }

  text += " colors=";
  const auto& cellTypes = board.cellTypes();
  for (int row = 0; row < kBoardRows; ++row) {
    for (int x = 0; x < kBoardWidth; ++x) {
      text += hexDigits[cellTypes[row][x] & 0xF];
    }
  }
  return text;
}

}  // namespace stackfall
