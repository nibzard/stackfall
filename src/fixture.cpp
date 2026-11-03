// Developer board fixture loading (docs/AGENT_TESTING.md, T023). Plain
// std::string and std::FILE code, no SDL and no clock, so the parser sits in
// stackfall_core and both the game binary and the headless reference replay
// read the same files through it.

#include "stackfall/fixture.hpp"

#include <cstdio>

#include "stackfall/pieces.hpp"

namespace stackfall {
namespace {

// Strips one trailing carriage return, so a fixture written on Windows reads
// the same as one written here.
std::string trimCarriageReturn(const std::string& line) {
  if (!line.empty() && line.back() == '\r') {
    return line.substr(0, line.size() - 1);
  }
  return line;
}

// The value of a `field: value` line: everything after the first colon, with
// surrounding spaces and tabs removed.
std::string fieldValue(const std::string& line, std::size_t colon) {
  std::size_t start = colon + 1;
  std::size_t end = line.size();
  while (start < end && (line[start] == ' ' || line[start] == '\t')) {
    ++start;
  }
  while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t')) {
    --end;
  }
  return line.substr(start, end - start);
}

}  // namespace

bool parseBoardFixture(const std::string& text, BoardFixture* out,
                       std::string* error) {
  std::string name;
  std::string description;
  std::string provenance;
  bool hasActive = false;
  PieceId active = PieceId::I;
  int rowCount = 0;
  std::uint16_t rowMasks[kBoardRows] = {};
  std::uint8_t rowCodes[kBoardRows][kBoardWidth] = {};

  std::size_t start = 0;
  int lineNumber = 0;
  while (start <= text.size()) {
    const std::size_t end = text.find('\n', start);
    const std::string raw =
        end == std::string::npos ? text.substr(start) : text.substr(start, end - start);
    start = end == std::string::npos ? text.size() + 1 : end + 1;
    ++lineNumber;
    const std::string line = trimCarriageReturn(raw);
    if (!line.empty() && line[0] == '#') {
      continue;  // comment
    }
    if (line.empty()) {
      continue;
    }
    const std::size_t colon = line.find(':');
    if (colon != std::string::npos && rowCount == kBoardRows) {
      // A field after the rows: only `active:` may appear there.
      const std::string field = line.substr(0, colon);
      const std::string value = fieldValue(line, colon);
      if (field == "active") {
        if (value.size() != 1 || !pieceIdFromLetter(value[0], &active)) {
          *error = "line " + std::to_string(lineNumber) +
                   ": active needs one piece letter I O T S Z J L";
          return false;
        }
        hasActive = true;
        continue;
      }
      *error = "line " + std::to_string(lineNumber) + ": unknown field \"" +
               field + "\" after the board rows";
      return false;
    }
    if (colon != std::string::npos && rowCount < kBoardRows) {
      const std::string field = line.substr(0, colon);
      const std::string value = fieldValue(line, colon);
      if (field == "name") {
        name = value;
        continue;
      }
      if (field == "description") {
        description = value;
        continue;
      }
      if (field == "provenance") {
        provenance = value;
        continue;
      }
      *error = "line " + std::to_string(lineNumber) + ": unknown field \"" +
               field + "\"";
      return false;
    }
    if (rowCount >= kBoardRows) {
      *error = "line " + std::to_string(lineNumber) +
               ": more than " + std::to_string(kBoardRows) + " board rows";
      return false;
    }
    if (line.size() != static_cast<std::size_t>(kBoardWidth)) {
      *error = "line " + std::to_string(lineNumber) + ": a board row needs " +
               std::to_string(kBoardWidth) + " cells, got " +
               std::to_string(line.size());
      return false;
    }
    for (int x = 0; x < kBoardWidth; ++x) {
      const char cell = line[x];
      if (cell == '.') {
        continue;
      }
      PieceId id = PieceId::I;
      if (!pieceIdFromLetter(cell, &id)) {
        *error = "line " + std::to_string(lineNumber) + ": cell \"" + cell +
                 "\" is neither '.' nor a piece letter";
        return false;
      }
      rowMasks[rowCount] = static_cast<std::uint16_t>(
          rowMasks[rowCount] | (1u << x));
      rowCodes[rowCount][x] = static_cast<std::uint8_t>(id);
    }
    ++rowCount;
  }

  if (rowCount != kBoardRows) {
    *error = "the fixture needs " + std::to_string(kBoardRows) +
             " board rows, found " + std::to_string(rowCount);
    return false;
  }

  // Build the board through the public writer, which checks every cell's
  // bounds and refuses double-settled cells, then prove the two arrays agree.
  Board board;
  for (int row = 0; row < kBoardRows; ++row) {
    for (int x = 0; x < kBoardWidth; ++x) {
      if ((rowMasks[row] & (1u << x)) == 0) {
        continue;
      }
      const PieceId id = static_cast<PieceId>(rowCodes[row][x]);
      const int y = row - kHiddenRows;
      if (!board.settleCell(x, y, id)) {
        *error = "cell " + std::to_string(x) + "," + std::to_string(y) +
                 " cannot be settled";
        return false;
      }
    }
  }
  if (!board.isConsistent()) {
    *error = "the parsed board is not consistent";
    return false;
  }

  *out = BoardFixture{};
  out->name = name;
  out->description = description;
  out->provenance = provenance;
  out->board = board;
  out->hasActive = hasActive;
  out->active = active;
  return true;
}

bool loadBoardFixture(const std::string& path, BoardFixture* out,
                      std::string* error) {
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    *error = "cannot open \"" + path + "\"";
    return false;
  }
  std::string text;
  char buffer[512];
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    text.append(buffer, read);
    if (text.size() > 8192) {
      std::fclose(file);
      *error = "\"" + path + "\" is larger than any fixture";
      return false;
    }
  }
  const bool failed = std::ferror(file) != 0;
  std::fclose(file);
  if (failed) {
    *error = "cannot read \"" + path + "\"";
    return false;
  }
  return parseBoardFixture(text, out, error);
}

}  // namespace stackfall
