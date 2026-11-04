// SDL-path pixel suite for the procedural renderer (task T010,
// specification sections 6.1, 6.2, and 7).
//
// This suite links SDL2 and runs separately from the portable stackfall_tests
// binary, exactly like tests/test_input_sdl.cpp (specification section 8).
// It draws real frames through Renderer::draw into an offscreen software
// renderer over a plain surface: no window, no video subsystem, no asset
// file, and no human eye. Every assertion reads exact pixels and compares
// them with the palette, the layout contract, and an independently driven
// second Game instance, so a shifted box, a wrong color, or a drawn cell that
// the rules do not place fails the suite.
//
// Coverage of the task T010 acceptance criteria:
//   * the board, frame, and optional faint grid appear at the section 6.1
//     geometry, and the grid toggle really toggles;
//   * the 27 px field sits at (345, 2) inside its one-pixel frame at
//     x=344..615 and y=1..542, the top and bottom visible rows carry whole
//     cells, and nothing clips on the 960x544 canvas (task T038);
//   * settled cells draw in their stored colors and only where the board has
//     them;
//   * the ghost draws below the active piece as a hollow outline in the
//     piece's own color and not at all when the piece is grounded, and every
//     one of the seven outlines holds a 3:1 luminance contrast against the
//     well while staying dimmer than the live piece;
//   * hold and the five next previews draw at the preview cell size, and an
//     unavailable hold reads dimmer than an available one;
//   * every glyph draws exactly the pixels of the 5x7 table, at scale;
//   * the focused HUD of task T037 holds the approved composition: two 128 px
//     columns 24 px from the board frame, statistics left (Hold, score,
//     lines, level), the five Next previews right with the first at full
//     color and the rest quieter, the longest supported values inside the
//     column, no permanent control legend, and no decorative panel outlines;
//   * draw() changes no byte of the game state (specification section 7);
//   * the same logical frame scales and letterboxes onto larger canvases;
//   * a window that is not the canvas letterboxes with black slack bars that
//     split evenly and a capture that reads the whole output, while the
//     native canvas keeps its exact pixels (task T032).
//
// Coverage of the task T011 screens (section 4.7):
//   * the title page draws its logo, preview strip, four-item menu with an
//     accent marker on the selected item, and the high score;
//   * the controls page draws its three columns and follows the layout
//     constants, with the footer of whichever route opened it;
//   * the options page of tasks T040 and T044 draws its heading, footer, and
//     the six setting rows with every value combination, and the ghost
//     setting really gates the ghost outline of the play scene;
//   * the grid setting of task T044 gates the grid lines without touching a
//     piece pixel, the hold setting hides the Hold preview, the next setting
//     draws exactly its count of previews in queue order at the unchanged
//     composition, and the grayscale setting recolors the pieces of the play
//     scene while it holds the ghost contrast floor and restores the normal
//     palette exactly on off;
//   * the pause overlay veils the frozen round, draws its four-item panel
//     between the HUD columns, and navigation under it leaves the round
//     byte-identical; its Controls item opens the mappings page and returns
//     to the same frozen round (task T037);
//   * the game-over overlay draws the final values, the accent record note
//     exactly when the round set one, and its two-item menu.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL.h>

#include "stackfall/font.hpp"
#include "stackfall/game.hpp"
#include "stackfall/pieces.hpp"
#include "stackfall/renderer_sdl.hpp"

#include "game_test_peer.hpp"
#include "test_util.hpp"

namespace {

using namespace stackfall;

// ---------------------------------------------------------------------------
// An offscreen canvas: a surface plus the software renderer that draws into
// it. SDL_CreateSoftwareRenderer needs no window and no video subsystem, so
// this suite runs on a build machine without a display.
// ---------------------------------------------------------------------------
struct Canvas {
  SDL_Surface* surface = nullptr;
  SDL_Renderer* renderer = nullptr;
  bool ok = false;

  ~Canvas() {
    if (renderer != nullptr) {
      SDL_DestroyRenderer(renderer);
    }
    if (surface != nullptr) {
      SDL_FreeSurface(surface);
    }
  }
};

Canvas makeCanvas(int width, int height) {
  Canvas canvas;
  canvas.surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32,
                                                  SDL_PIXELFORMAT_RGBA32);
  if (canvas.surface == nullptr) {
    std::printf("surface creation failed: %s\n", SDL_GetError());
    return canvas;
  }
  canvas.renderer = SDL_CreateSoftwareRenderer(canvas.surface);
  if (canvas.renderer == nullptr) {
    std::printf("software renderer creation failed: %s\n", SDL_GetError());
    return canvas;
  }
  if (SDL_RenderSetLogicalSize(canvas.renderer, kCanvasWidth, kCanvasHeight) !=
      0) {
    std::printf("logical size failed: %s\n", SDL_GetError());
    return canvas;
  }
  canvas.ok = true;
  return canvas;
}

SDL_Color pixelAt(const Canvas& canvas, int x, int y) {
  SDL_Color color{0, 0, 0, 0};
  if (!canvas.ok || x < 0 || y < 0 || x >= canvas.surface->w ||
      y >= canvas.surface->h) {
    return color;
  }
  const Uint32* pixels = static_cast<const Uint32*>(canvas.surface->pixels);
  const Uint32 value =
      pixels[(y * canvas.surface->pitch / 4) + static_cast<std::size_t>(x)];
  SDL_GetRGBA(value, canvas.surface->format, &color.r, &color.g, &color.b,
              &color.a);
  return color;
}

bool sameColor(const SDL_Color& a, const SDL_Color& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

// Fills the whole canvas with one color, the known backdrop of a text check.
void clearTo(Canvas& canvas, SDL_Color color) {
  SDL_SetRenderDrawColor(canvas.renderer, color.r, color.g, color.b,
                         SDL_ALPHA_OPAQUE);
  SDL_RenderClear(canvas.renderer);
}

int countColor(const Canvas& canvas, const SDL_Rect& area, SDL_Color color) {
  int found = 0;
  for (int y = area.y; y < area.y + area.h; ++y) {
    for (int x = area.x; x < area.x + area.w; ++x) {
      if (sameColor(pixelAt(canvas, x, y), color)) {
        ++found;
      }
    }
  }
  return found;
}

// True when `area` holds `color` and nothing else.
bool areaIsOnly(const Canvas& canvas, const SDL_Rect& area, SDL_Color color) {
  return countColor(canvas, area, color) == area.w * area.h;
}

// The heights of the text lines in `band`: one entry per contiguous run of
// rows that hold a bright or dim text pixel. This is the measure
// tools/design-review/check_fixes.py takes on a captured packet, restated
// here so a 7 px hint row fails the suite before any packet records it.
std::vector<int> textRowRuns(const Canvas& canvas, const SDL_Rect& band) {
  std::vector<int> runs;
  int run = 0;
  for (int y = band.y; y < band.y + band.h; ++y) {
    bool hasText = false;
    for (int x = band.x; x < band.x + band.w && !hasText; ++x) {
      const SDL_Color color = pixelAt(canvas, x, y);
      hasText = sameColor(color, kText) || sameColor(color, kTextDim);
    }
    if (hasText) {
      ++run;
    } else if (run > 0) {
      runs.push_back(run);
      run = 0;
    }
  }
  if (run > 0) {
    runs.push_back(run);
  }
  return runs;
}

// The relative luminance of WCAG 2, the formula
// tools/design-review/check_fixes.py measures on a captured packet, restated
// here so the suite holds a color to the same floor before any screenshot
// records it.
double channelLuminance(int value) {
  const double c = static_cast<double>(value) / 255.0;
  return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminanceOf(const SDL_Color& color) {
  return 0.2126 * channelLuminance(color.r) +
         0.7152 * channelLuminance(color.g) +
         0.0722 * channelLuminance(color.b);
}

double contrastRatio(const SDL_Color& a, const SDL_Color& b) {
  double lighter = luminanceOf(a);
  double darker = luminanceOf(b);
  if (lighter < darker) {
    const double swap = lighter;
    lighter = darker;
    darker = swap;
  }
  return (lighter + 0.05) / (darker + 0.05);
}

// The ghost blend, one quarter of the fill toward the well, restated here on
// purpose with its own arithmetic (kHintScale above does the same for the
// hint scale): if the renderer drifts back toward the well, these expected
// colors fail the suite before any packet measures 2.44:1 again.
constexpr int ghostChannel(int fill, int well) { return fill + (well - fill) / 4; }

// The number of lit font pixels of `text`, so a text row's bright or dim
// pixel count can be pinned to the exact glyphs of the 5x7 table: a row that
// drew one wrong glyph, ON for OFF say, fails even when every other measure
// matches.
int glyphPixelCount(const char* text) {
  int count = 0;
  for (int index = 0; text[index] != '\0'; ++index) {
    const Glyph& glyph = glyphFor(text[index]);
    for (int row = 0; row < kGlyphRows; ++row) {
      for (int column = 0; column < kGlyphColumns; ++column) {
        if (glyphPixel(glyph, column, row)) {
          ++count;
        }
      }
    }
  }
  return count;
}

constexpr SDL_Color expectedGhostColor(PieceId id) {
  return SDL_Color{
      static_cast<Uint8>(ghostChannel(pieceFill(id).r, kBoardWell.r)),
      static_cast<Uint8>(ghostChannel(pieceFill(id).g, kBoardWell.g)),
      static_cast<Uint8>(ghostChannel(pieceFill(id).b, kBoardWell.b)), 255};
}

// The canvas pixel of the center and of the top-left corner of board cell
// (x, y).
constexpr int cellCenterX(int x) {
  return kBoardOriginX + x * kBoardCellSize + kBoardCellSize / 2;
}
constexpr int cellCenterY(int y) {
  return kBoardOriginY + y * kBoardCellSize + kBoardCellSize / 2;
}
constexpr int cellCornerX(int x) { return kBoardOriginX + x * kBoardCellSize; }
constexpr int cellCornerY(int y) { return kBoardOriginY + y * kBoardCellSize; }

// The center of one preview cell, from the section 6.1 rule restated here on
// purpose: the piece keeps its spawn shape cell coordinates at the preview
// cell size, and its bounding box centers inside `box`. If the renderer's
// centering drifts, this independent copy of the rule fails the suite.
SDL_Point previewCellCenter(PieceId id, const SDL_Rect& box, const Cell& cell) {
  const std::array<Cell, kCellsPerPiece>& cells =
      pieceCells(id, Rotation::Spawn);
  int minX = 4;
  int maxX = 0;
  int minY = 4;
  int maxY = 0;
  for (const Cell& shape : cells) {
    minX = shape.x < minX ? shape.x : minX;
    maxX = shape.x > maxX ? shape.x : maxX;
    minY = shape.y < minY ? shape.y : minY;
    maxY = shape.y > maxY ? shape.y : maxY;
  }
  const int originX = box.x + (box.w - (maxX - minX + 1) * kPreviewCellSize) /
                                  2 -
                      minX * kPreviewCellSize;
  const int originY = box.y + (box.h - (maxY - minY + 1) * kPreviewCellSize) /
                                  2 -
                      minY * kPreviewCellSize;
  return SDL_Point{originX + cell.x * kPreviewCellSize + kPreviewCellSize / 2,
                   originY + cell.y * kPreviewCellSize + kPreviewCellSize / 2};
}

// Settles `rounds` pieces by hard drop, one press per piece, stopping early
// if the round ends.
void playRounds(Game& game, int rounds) {
  for (int i = 0; i < rounds; ++i) {
    InputFrame drop = InputFrame::idle();
    drop.hardDropPressed = true;
    game.tick(drop);
    game.tick(InputFrame::idle());
    if (game.scene() != Scene::Playing) {
      return;
    }
  }
}

// Screen text scales and the controls row count, restated here on purpose
// like previewCellCenter above: the suite asserts the layout contract with
// its own copy of the numbers the renderer draws with.
constexpr int kLabelScale = 2;
constexpr int kValueScale = 2;
constexpr int kHintScale = 2;
constexpr int kControlRowCount = 10;

// How many cells of the active piece lie inside the visible field.
int visibleActiveCells(const Game& game) {
  if (!game.hasActivePiece()) {
    return 0;
  }
  int visible = 0;
  for (const Cell& cell : boardCells(game.activePiece())) {
    if (cell.y >= 0) {
      ++visible;
    }
  }
  return visible;
}

// Everything a draw() call must leave untouched (specification section 7):
// the board arrays, the queue, the counters, and the piece states.
std::vector<std::uint8_t> snapshotOf(const Game& game) {
  std::vector<std::uint8_t> bytes;
  auto append = [&bytes](const void* data, std::size_t size) {
    const auto* first = static_cast<const std::uint8_t*>(data);
    bytes.insert(bytes.end(), first, first + size);
  };
  append(game.board().occupancy().data(),
         game.board().occupancy().size() * sizeof(std::uint16_t));
  for (const auto& row : game.board().cellTypes()) {
    append(row.data(), row.size());
  }
  append(game.queue().data(), game.queue().size());
  const std::uint64_t score = game.score();
  const std::uint64_t lines = game.lines();
  const std::uint64_t ticks = game.tickCount();
  append(&score, sizeof(score));
  append(&lines, sizeof(lines));
  append(&ticks, sizeof(ticks));
  const std::uint8_t flags[6] = {
      static_cast<std::uint8_t>(game.hasActivePiece() ? 1 : 0),
      static_cast<std::uint8_t>(game.hasHoldPiece() ? 1 : 0),
      static_cast<std::uint8_t>(game.holdLocked() ? 1 : 0),
      static_cast<std::uint8_t>(game.holdPieceId()),
      static_cast<std::uint8_t>(game.activePiece().id),
      static_cast<std::uint8_t>(game.activePiece().rotation)};
  append(flags, sizeof(flags));
  const int ints[4] = {game.activePiece().x, game.activePiece().y,
                       game.level(), static_cast<int>(game.scene())};
  append(ints, sizeof(ints));
  return bytes;
}

// ---------------------------------------------------------------------------
// Checks.
// ---------------------------------------------------------------------------

void run_board_and_grid_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Game game;
  game.start(20261102);
  Renderer renderer(canvas.renderer);

  renderer.draw(game);

  // Background outside the panels and above the board. The board frame sits
  // one pixel above the field, so the canvas row above it is the first free
  // row: kBoardOriginY - 2 == 0.
  SC_CHECK(sameColor(pixelAt(canvas, 5, 5), kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, kCanvasWidth - 5, kCanvasHeight - 5),
                     kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX + 40, kBoardOriginY - 2),
                     kBackground));

  // Board well interior, frame outline just outside it.
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX + 3, kBoardOriginY + 3),
                     kBoardWell));
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX + kBoardPixelWidth - 4,
                             kBoardOriginY + kBoardPixelHeight - 4),
                     kBoardWell));
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX - 1, 271), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX + kBoardPixelWidth, 271),
                     kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, 480, kBoardOriginY - 1), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, 480, kBoardOriginY + kBoardPixelHeight),
                     kFrame));

  // The faint grid sits on every interior cell boundary, and the setting
  // really toggles it.
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX + kBoardCellSize,
                             kBoardOriginY + 7),
                     kGridLine));
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX + 9 * kBoardCellSize, 400),
                     kGridLine));
  SC_CHECK(sameColor(
      pixelAt(canvas, kBoardOriginX + 7, kBoardOriginY + 3 * kBoardCellSize),
      kGridLine));

  // A settled piece rides through the toggle unchanged: grid off removes the
  // lines and the cell keeps every pixel of its color, and grid on restores
  // the lines without touching the piece (task T044).
  SC_CHECK(stackfall::GameTestPeer::board(game).settleCell(4, 10, PieceId::T));
  renderer.draw(game);
  const SDL_Rect cellRect{cellCornerX(4), cellCornerY(10), kBoardCellSize,
                          kBoardCellSize};
  const int colorPixelsBefore =
      countColor(canvas, cellRect, pieceFill(PieceId::T));
  SC_CHECK(colorPixelsBefore > 0);
  renderer.setShowGrid(false);
  SC_CHECK(renderer.showGrid() == false);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX + kBoardCellSize,
                             kBoardOriginY + 7),
                     kBoardWell));
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX + 9 * kBoardCellSize, 400),
                     kBoardWell));
  SC_CHECK(countColor(canvas, cellRect, pieceFill(PieceId::T)) ==
           colorPixelsBefore);
  renderer.setShowGrid(true);
  SC_CHECK(renderer.showGrid() == true);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX + kBoardCellSize,
                             kBoardOriginY + 7),
                     kGridLine));
  SC_CHECK(countColor(canvas, cellRect, pieceFill(PieceId::T)) ==
           colorPixelsBefore);
}

// The board-cells-27px geometry of specification section 6.1 (task T038),
// asserted at the edges of the canvas: 27 px cells, a 270x540 field at
// (345, 2) with occupied bounds x=345..614 and y=2..541, one frame pixel at
// x=344..615 and y=1..542, and nothing clipped on the 960x544 canvas. The
// numbers are restated here on purpose, like the layout contract above, so a
// constant that drifts from the specification fails before any capture
// records it.
void run_board_boundary_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);

  SC_CHECK(kBoardCellSize == 27);
  SC_CHECK(kBoardOriginX == 345);
  SC_CHECK(kBoardOriginY == 2);
  SC_CHECK(kBoardPixelWidth == 270);
  SC_CHECK(kBoardPixelHeight == 540);
  SC_CHECK(kBoardOriginX + kBoardPixelWidth - 1 == 614);
  SC_CHECK(kBoardOriginY + kBoardPixelHeight - 1 == 541);
  const SDL_Rect frame = boardFrame();
  SC_CHECK(frame.x == 344 && frame.y == 1);
  SC_CHECK(frame.w == 272 && frame.h == 542);
  SC_CHECK(frame.x + frame.w - 1 == 615);
  SC_CHECK(frame.y + frame.h - 1 == 542);
  // The field is centered on the canvas and the frame leaves it a free row
  // or column on every side, so no board pixel can clip.
  SC_CHECK(frame.x == kCanvasWidth - (frame.x + frame.w));
  SC_CHECK(kCanvasHeight - (frame.y + frame.h) == 1);

  // An empty field: the well fill covers exactly x=345..614 and y=2..541,
  // the frame wraps it, and the rows and columns outside stay background.
  Game game;
  game.start(20261103);
  renderer.draw(game);
  const int column = 400;  // inside the field, on no grid line
  SC_CHECK(sameColor(pixelAt(canvas, 343, 300), kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, 344, 300), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, 345, 300), kBoardWell));
  SC_CHECK(sameColor(pixelAt(canvas, 614, 300), kBoardWell));
  SC_CHECK(sameColor(pixelAt(canvas, 615, 300), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, 616, 300), kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, column, 0), kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, column, 1), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, column, 2), kBoardWell));
  SC_CHECK(sameColor(pixelAt(canvas, column, 541), kBoardWell));
  SC_CHECK(sameColor(pixelAt(canvas, column, 542), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, column, 543), kBackground));

  // The grid stays inside the field: the first lines at x=372 and y=29, the
  // last at x=588 and y=515, and none reaches the frame.
  SC_CHECK(kBoardOriginX + kBoardCellSize == 372);
  SC_CHECK(kBoardOriginX + 9 * kBoardCellSize == 588);
  SC_CHECK(kBoardOriginY + kBoardCellSize == 29);
  SC_CHECK(kBoardOriginY + 19 * kBoardCellSize == 515);
  SC_CHECK(sameColor(pixelAt(canvas, 372, 300), kGridLine));
  SC_CHECK(sameColor(pixelAt(canvas, 588, 300), kGridLine));
  SC_CHECK(sameColor(pixelAt(canvas, column, 29), kGridLine));
  SC_CHECK(sameColor(pixelAt(canvas, column, 515), kGridLine));
  SC_CHECK(sameColor(pixelAt(canvas, 344, 29), kFrame));

  // Whole cells on the top and the bottom visible row: settle the four
  // corners of the field through the test peer, then draw the same round
  // again.
  Board& board = stackfall::GameTestPeer::board(game);
  SC_CHECK(board.settleCell(0, 0, PieceId::I));
  SC_CHECK(board.settleCell(kBoardWidth - 1, 0, PieceId::O));
  SC_CHECK(board.settleCell(0, kBoardHeight - 1, PieceId::J));
  SC_CHECK(board.settleCell(kBoardWidth - 1, kBoardHeight - 1, PieceId::L));
  renderer.draw(game);

  // The top row: the canvas row above the frame is background, the frame at
  // y=1, the cell's dark edge at y=2, its fill from y=3.
  SC_CHECK(cellCornerY(0) == 2);
  SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(0), 0), kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(0), 1), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(0), 2), kCellEdge));
  SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(0), 3),
                     cellPalette(PieceId::I).light));
  SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(0), cellCenterY(0)),
                     pieceFill(PieceId::I)));

  // The bottom row: the cell's last pixel row is y=541, the frame at y=542,
  // and the last canvas row below it stays background, so the lowest cell on
  // the field cannot clip.
  SC_CHECK(cellCornerY(kBoardHeight - 1) + kBoardCellSize - 1 == 541);
  SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(0),
                             cellCenterY(kBoardHeight - 1)),
                     pieceFill(PieceId::J)));
  SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(0), 541), kCellEdge));
  SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(0), 542), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(0), 543), kBackground));

  // The left and the right edge: the cell edge at x=345 and x=614, the frame
  // outside them, and background again one column past the frame.
  SC_CHECK(cellCornerX(0) == 345);
  SC_CHECK(sameColor(pixelAt(canvas, 343, cellCenterY(0)), kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, 344, cellCenterY(0)), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, 345, cellCenterY(0)), kCellEdge));
  SC_CHECK(cellCornerX(kBoardWidth - 1) + kBoardCellSize - 1 == 614);
  SC_CHECK(sameColor(pixelAt(canvas, 614, cellCenterY(kBoardHeight - 1)),
                     kCellEdge));
  SC_CHECK(sameColor(pixelAt(canvas, 615, cellCenterY(kBoardHeight - 1)),
                     kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, 616, cellCenterY(kBoardHeight - 1)),
                     kBackground));
}

void run_cells_and_ghost_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);

  // The reference game drives the same ticks but is never drawn, so a pixel
  // mismatch points at the renderer and not at the rules.
  Game subject;
  Game reference;
  subject.start(30);
  reference.start(30);
  playRounds(subject, 8);
  playRounds(reference, 8);
  SC_CHECK(subject.scene() == Scene::Playing);
  SC_CHECK(subject.hasActivePiece());

  // A freshly spawned piece draws at once under the immediate-spawn rule of
  // task T029: its top cells sit in visible row 0, so no soft drop is needed
  // to bring it into view. Both games stay in step because neither ticks.
  SC_CHECK(visibleActiveCells(subject) ==
           static_cast<int>(stackfall::kCellsPerPiece));

  renderer.draw(subject);

  // Settled cells in their stored colors, and only where the board has them.
  int settledSeen = 0;
  for (int y = 0; y < kBoardHeight; ++y) {
    for (int x = 0; x < kBoardWidth; ++x) {
      const std::uint8_t code =
          reference.board().cellTypes()[boardRowIndex(y)][x];
      bool covered = false;
      for (const Cell& cell : boardCells(subject.activePiece())) {
        covered = covered || (cell.x == x && cell.y == y);
      }
      for (const Cell& cell : boardCells(subject.ghostPiece())) {
        covered = covered || (cell.x == x && cell.y == y);
      }
      if (covered) {
        continue;
      }
      const SDL_Color expected =
          code == 0 ? kBoardWell : pieceFill(static_cast<PieceId>(code));
      const SDL_Color actual = pixelAt(canvas, cellCenterX(x), cellCenterY(y));
      if (!sameColor(actual, expected)) {
        SC_CHECK(false);
      }
      if (code != 0) {
        ++settledSeen;
      }
    }
  }
  // Eight settled pieces of four cells, less any cells a line clear removed.
  SC_CHECK(settledSeen >= 12);

  // The active piece draws in its own color at its own cells, and its cells
  // in the hidden rows stay invisible.
  const PieceId activeId = subject.activePiece().id;
  int activeSeen = 0;
  for (const Cell& cell : boardCells(subject.activePiece())) {
    if (cell.y < 0) {
      // A cell above the field never draws. When the top visible row of its
      // column is empty on the board, that row must hold no piece color.
      if (reference.board().cellTypes()[boardRowIndex(0)][cell.x] == 0) {
        for (const SDL_Color& fill : kPieceFills) {
          SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(cell.x), cellCenterY(0)),
                             fill) == false);
        }
      }
      continue;
    }
    SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(cell.x), cellCenterY(cell.y)),
                       pieceFill(activeId)));
    ++activeSeen;
  }
  SC_CHECK(activeSeen > 0);

  // The ghost is a hollow outline in the piece color, strictly below.
  const Piece ghost = subject.ghostPiece();
  SC_CHECK(ghost.y > subject.activePiece().y);
  for (const Cell& cell : boardCells(ghost)) {
    if (cell.y < 0) {
      continue;
    }
    SC_CHECK(sameColor(pixelAt(canvas, cellCornerX(cell.x), cellCornerY(cell.y)),
                       ghostColor(activeId)));
    // One pixel of outline: the pixel just inside the corner is already the
    // well again, so the ghost stays hollow and cannot read as a settled or
    // live cell.
    SC_CHECK(sameColor(pixelAt(canvas, cellCornerX(cell.x) + 1,
                               cellCornerY(cell.y) + 1),
                       kBoardWell));
    SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(cell.x), cellCenterY(cell.y)),
                       kBoardWell));
  }

  // Every one of the seven outlines holds the 3:1 contrast floor against the
  // well, the acceptance criterion of the design review finding
  // ghost-outline-contrast, and each stays clearly dimmer than the live
  // piece it previews. At the reviewed blend of three fifths toward the well
  // the same loop measures 1.76:1 to 2.82:1 and fails.
  for (int index = 0; index < kPieceCount; ++index) {
    const PieceId id = kAllPieceIds[index];
    SC_CHECK(sameColor(ghostColor(id), expectedGhostColor(id)));
    SC_CHECK(contrastRatio(ghostColor(id), kBoardWell) >= 3.0);
    SC_CHECK(contrastRatio(pieceFill(id), ghostColor(id)) >= 1.5);
  }

  // A grounded piece shows no ghost: soft-drop the live piece until it rests
  // and its cells must hold the piece color, not a hollow outline.
  Game grounded;
  grounded.start(30);
  playRounds(grounded, 8);
  InputFrame soft = InputFrame::idle();
  soft.downHeld = true;
  for (int i = 0; i < kBoardHeight + 4; ++i) {
    grounded.tick(soft);
    if (grounded.board().maxDrop(grounded.activePiece()) == 0) {
      break;
    }
  }
  SC_CHECK(grounded.board().maxDrop(grounded.activePiece()) == 0);
  renderer.draw(grounded);
  const PieceId groundedId = grounded.activePiece().id;
  for (const Cell& cell : boardCells(grounded.activePiece())) {
    if (cell.y < 0) {
      continue;
    }
    SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(cell.x), cellCenterY(cell.y)),
                       pieceFill(groundedId)));
  }

  // The ghost setting of the options screen really gates the outline (task
  // T040): with the preview off (and the grid off, so the well reads plain),
  // every pixel of each landing cell that no settled or live cell covers is
  // the well color where the ghost drew its outline, while the active piece
  // above still draws in full color.
  renderer.setShowGrid(false);
  renderer.setShowGhost(false);
  SC_CHECK(renderer.showGhost() == false);
  renderer.draw(subject);
  const Piece ghostOff = subject.ghostPiece();
  SC_CHECK(ghostOff.y > subject.activePiece().y);
  for (const Cell& cell : boardCells(ghostOff)) {
    if (cell.y < 0) {
      continue;  // hidden rows never draw
    }
    bool covered = false;
    for (const Cell& live : boardCells(subject.activePiece())) {
      covered = covered || (live.x == cell.x && live.y == cell.y);
    }
    covered = covered ||
              reference.board().cellTypes()[boardRowIndex(cell.y)][cell.x] != 0;
    if (covered) {
      continue;
    }
    const SDL_Rect cellRect{cellCornerX(cell.x), cellCornerY(cell.y),
                            kBoardCellSize, kBoardCellSize};
    SC_CHECK(areaIsOnly(canvas, cellRect, kBoardWell));
  }
  for (const Cell& cell : boardCells(subject.activePiece())) {
    if (cell.y < 0) {
      continue;
    }
    SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(cell.x), cellCenterY(cell.y)),
                       pieceFill(activeId)));
  }
  renderer.setShowGrid(true);
  renderer.setShowGhost(true);
  SC_CHECK(renderer.showGhost() == true);
}

// ---------------------------------------------------------------------------
// The preference gates of task T044: the Next preview count draws exactly N
// previews in queue order at the unchanged composition, and hold off hides
// the whole Hold group.
// ---------------------------------------------------------------------------

// True when no pixel of `area` holds any piece color, full or dimmed, color
// or gray: an empty preview slot.
bool areaHoldsNoPieceColor(const Canvas& canvas, const SDL_Rect& area) {
  for (int index = 0; index < kPieceCount; ++index) {
    const PieceId id = kAllPieceIds[index];
    if (countColor(canvas, area, pieceFill(id)) != 0 ||
        countColor(canvas, area, dimCellPalette(id).fill) != 0 ||
        countColor(canvas, area, grayscaleFill(id)) != 0 ||
        countColor(canvas, area, grayscaleDimPalette(id).fill) != 0) {
      return false;
    }
  }
  return true;
}

void run_next_and_hold_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);

  Game game;
  game.start(4242);
  const auto& queue = game.queue();

  // Exactly N previews draw, in queue order, for every N = 1..5: the drawn
  // slots carry their queued piece at the unchanged contrast hierarchy (the
  // first at full color, the rest dim), and every slot past N stays empty.
  // The queue itself is the renderer's input, so the count changes nothing
  // about the randomizer (task T044).
  for (int count = 1; count <= kPreviewCount; ++count) {
    renderer.setNextPreviews(count);
    SC_CHECK(renderer.nextPreviews() == count);
    renderer.draw(game);
    for (int index = 0; index < kPreviewCount; ++index) {
      const SDL_Rect slot = nextSlotBox(index);
      if (index < count) {
        const CellPalette& palette =
            index == 0 ? cellPalette(queue[index])
                       : dimCellPalette(queue[index]);
        for (const Cell& cell :
             pieceCells(queue[index], Rotation::Spawn)) {
          const SDL_Point center =
              previewCellCenter(queue[index], slot, cell);
          SC_CHECK(sameColor(pixelAt(canvas, center.x, center.y),
                             palette.fill));
        }
      } else {
        SC_CHECK(areaHoldsNoPieceColor(canvas, slot));
      }
    }
    // The panel padding below the last slot stays background at every count.
    const SDL_Rect panelPadding{nextPanelBox().x,
                                nextPanelBox().y + nextPanelBox().h - 5,
                                nextPanelBox().w, 5};
    SC_CHECK(areaIsOnly(canvas, panelPadding, kBackground));
  }

  // A stray count clamps into the preference range instead of reading past
  // the queue.
  renderer.setNextPreviews(0);
  SC_CHECK(renderer.nextPreviews() == 1);
  renderer.setNextPreviews(9);
  SC_CHECK(renderer.nextPreviews() == kPreviewCount);
  renderer.setNextPreviews(kPreviewCount);

  // The default count of 5 renders exactly the T037 composition: five
  // identifiable previews, the first at full color and the four behind it
  // dim, anchored at the unchanged slots.
  renderer.draw(game);
  for (int index = 0; index < kPreviewCount; ++index) {
    const SDL_Rect slot = nextSlotBox(index);
    const CellPalette& palette =
        index == 0 ? cellPalette(queue[index]) : dimCellPalette(queue[index]);
    for (const Cell& cell : pieceCells(queue[index], Rotation::Spawn)) {
      const SDL_Point center = previewCellCenter(queue[index], slot, cell);
      SC_CHECK(sameColor(pixelAt(canvas, center.x, center.y), palette.fill));
    }
  }

  // Hold off hides the whole Hold group, label and piece alike, even while a
  // piece sits in the slot: the band above the statistics carries no text and
  // no piece color, while the statistics below keep drawing (task T044).
  stackfall::GameTestPeer::setHoldSlot(game, PieceId::T);
  stackfall::GameTestPeer::setHoldLocked(game, false);
  SC_CHECK(game.hasHoldPiece());
  renderer.setShowHold(false);
  SC_CHECK(renderer.showHold() == false);
  renderer.draw(game);
  const SDL_Rect holdBand{kLeftPanelX, kPanelHeadingY, kLeftPanelWidth,
                          kHoldBoxHeight + kPanelBoxY - kPanelHeadingY};
  SC_CHECK(areaHoldsNoPieceColor(canvas, holdBand));
  SC_CHECK(countColor(canvas, holdBand, kTextDim) == 0);  // no HOLD label
  const SDL_Rect statsBand{kLeftPanelX, kStatsFirstLabelY, kLeftPanelWidth,
                           2 * kStatsFieldStride};
  SC_CHECK(countColor(canvas, statsBand, kTextDim) > 50);
  SC_CHECK(countColor(canvas, statsBand, kText) > 50);

  // Hold on restores the group exactly: the label returns and the stored
  // piece draws at full color.
  renderer.setShowHold(true);
  SC_CHECK(renderer.showHold() == true);
  renderer.draw(game);
  SC_CHECK(countColor(canvas, holdBand, kTextDim) > 20);
  for (const Cell& cell : pieceCells(PieceId::T, Rotation::Spawn)) {
    const SDL_Point center =
        previewCellCenter(PieceId::T, holdBox(), cell);
    SC_CHECK(sameColor(pixelAt(canvas, center.x, center.y),
                       pieceFill(PieceId::T)));
  }
}

// ---------------------------------------------------------------------------
// The grayscale play scene of task T044: on swaps the piece colors for seven
// grays while the text, the outlines, and the board stay as they are, the
// ghost keeps the 3:1 contrast floor, and off restores the normal palette
// byte for byte.
// ---------------------------------------------------------------------------

void run_grayscale_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);

  Game subject;
  Game reference;
  subject.start(30);
  reference.start(30);
  playRounds(subject, 8);
  playRounds(reference, 8);
  SC_CHECK(subject.scene() == Scene::Playing);
  SC_CHECK(subject.hasActivePiece());
  SC_CHECK(reference.board().settledCellCount() > 0);
  // A stored, available hold piece, so the Hold preview is part of every
  // frame this check draws.
  stackfall::GameTestPeer::setHoldSlot(subject, subject.activePiece().id);
  stackfall::GameTestPeer::setHoldLocked(subject, false);
  SC_CHECK(subject.hasHoldPiece());

  // The palette contract: every fill is achromatic, the seven shades are
  // distinct, and the ghost blend of one quarter toward the well holds the
  // 3:1 outline contrast floor of the color mode while every live shade
  // stays clearly brighter than its ghost.
  for (int index = 0; index < kPieceCount; ++index) {
    const PieceId id = kAllPieceIds[index];
    const SDL_Color fill = grayscaleFill(id);
    SC_CHECK(fill.r == fill.g);
    SC_CHECK(fill.g == fill.b);
    for (int other = index + 1; other < kPieceCount; ++other) {
      SC_CHECK(sameColor(fill, grayscaleFill(kAllPieceIds[other])) == false);
    }
    SC_CHECK(contrastRatio(grayscaleGhostColor(id), kBoardWell) >= 3.0);
    SC_CHECK(contrastRatio(grayscaleFill(id), grayscaleGhostColor(id)) >=
             1.5);
    SC_CHECK(sameColor(grayscaleFill(id), grayscaleGhostColor(id)) == false);
  }

  // The full color frame, kept byte for byte: grayscale off after an on pass
  // must restore it exactly.
  renderer.draw(subject);
  std::vector<std::uint8_t> colorFrame(
      static_cast<const std::uint8_t*>(canvas.surface->pixels),
      static_cast<const std::uint8_t*>(canvas.surface->pixels) +
          static_cast<std::size_t>(canvas.surface->pitch) * canvas.surface->h);

  renderer.setGrayscale(true);
  SC_CHECK(renderer.grayscale() == true);
  renderer.draw(subject);

  // Settled cells draw in their grayscale fill, and only where the board has
  // them; the active piece and the ghost keep their own shades.
  const PieceId activeId = subject.activePiece().id;
  const Piece ghost = subject.ghostPiece();
  SC_CHECK(ghost.y > subject.activePiece().y);
  for (int y = 0; y < kBoardHeight; ++y) {
    for (int x = 0; x < kBoardWidth; ++x) {
      const std::uint8_t code =
          reference.board().cellTypes()[boardRowIndex(y)][x];
      bool covered = false;
      for (const Cell& cell : boardCells(subject.activePiece())) {
        covered = covered || (cell.x == x && cell.y == y);
      }
      for (const Cell& cell : boardCells(ghost)) {
        covered = covered || (cell.x == x && cell.y == y);
      }
      if (covered) {
        continue;
      }
      const SDL_Color expected =
          code == 0 ? kBoardWell : grayscaleFill(static_cast<PieceId>(code));
      SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(x), cellCenterY(y)),
                         expected));
    }
  }
  for (const Cell& cell : boardCells(subject.activePiece())) {
    if (cell.y < 0) {
      continue;
    }
    SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(cell.x), cellCenterY(cell.y)),
                       grayscaleFill(activeId)));
  }
  for (const Cell& cell : boardCells(ghost)) {
    if (cell.y < 0) {
      continue;
    }
    SC_CHECK(sameColor(pixelAt(canvas, cellCornerX(cell.x), cellCornerY(cell.y)),
                       grayscaleGhostColor(activeId)));
    SC_CHECK(sameColor(pixelAt(canvas, cellCornerX(cell.x) + 1,
                               cellCornerY(cell.y) + 1),
                       kBoardWell));
  }

  // The Hold and Next previews draw in their grayscale palettes, full for the
  // available hold and the first Next, dim behind it.
  renderer.draw(subject);
  for (const Cell& cell :
       pieceCells(subject.holdPieceId(), Rotation::Spawn)) {
    const SDL_Point center =
        previewCellCenter(subject.holdPieceId(), holdBox(), cell);
    SC_CHECK(sameColor(pixelAt(canvas, center.x, center.y),
                       grayscaleFill(subject.holdPieceId())));
  }
  const auto& queue = subject.queue();
  for (int index = 0; index < kPreviewCount; ++index) {
    const CellPalette& palette = index == 0 ? grayscalePalette(queue[index])
                                            : grayscaleDimPalette(queue[index]);
    for (const Cell& cell : pieceCells(queue[index], Rotation::Spawn)) {
      const SDL_Point center = previewCellCenter(queue[index],
                                                 nextSlotBox(index), cell);
      SC_CHECK(sameColor(pixelAt(canvas, center.x, center.y), palette.fill));
    }
  }

  // The text, the outlines, and the board keep their colors: the statistics
  // labels stay dim text, the frame stays the frame color, and the well
  // stays the well.
  const SDL_Rect statsBand{kLeftPanelX, kStatsFirstLabelY, kLeftPanelWidth,
                           2 * kStatsFieldStride};
  SC_CHECK(countColor(canvas, statsBand, kTextDim) > 50);
  SC_CHECK(countColor(canvas, statsBand, kText) > 50);
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX - 1, 271), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, cellCenterX(9), cellCenterY(0)),
                     kBoardWell));

  // Off restores the normal palette exactly: the very bytes of the color
  // frame come back, settled pieces, active piece, ghost, and previews
  // together.
  renderer.setGrayscale(false);
  SC_CHECK(renderer.grayscale() == false);
  renderer.draw(subject);
  const std::uint8_t* pixels =
      static_cast<const std::uint8_t*>(canvas.surface->pixels);
  const std::vector<std::uint8_t> restored(
      pixels, pixels + static_cast<std::size_t>(canvas.surface->pitch) *
                           canvas.surface->h);
  SC_CHECK(restored.size() == colorFrame.size());
  SC_CHECK(restored == colorFrame);
}

void run_preview_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);

  Game game;
  game.start(4242);
  renderer.draw(game);

  // An unused hold slot shows no piece color at all: the empty hold of the
  // focused HUD is one quiet label and empty space (task T037).
  SC_CHECK(game.hasHoldPiece() == false);
  const SDL_Rect hold = holdBox();
  for (const SDL_Color& fill : kPieceFills) {
    SC_CHECK(countColor(canvas, hold, fill) == 0);
  }
  for (int index = 0; index < kPieceCount; ++index) {
    SC_CHECK(countColor(canvas, hold, dimCellPalette(kAllPieceIds[index]).fill) ==
             0);
  }

  // The five next previews draw the queued pieces in order at the preview
  // cell size. The first draws at full color and the rest dimmer, so the
  // piece that lands next stays readable while the far queue reads quieter
  // (task T037 contrast hierarchy), and the gap between the panel padding
  // stays empty background.
  const auto& queue = game.queue();
  for (int index = 0; index < kPreviewCount; ++index) {
    const SDL_Rect slot = nextSlotBox(index);
    const CellPalette& palette =
        index == 0 ? cellPalette(queue[index]) : dimCellPalette(queue[index]);
    for (const Cell& cell : pieceCells(queue[index], Rotation::Spawn)) {
      const SDL_Point center = previewCellCenter(queue[index], slot, cell);
      SC_CHECK(sameColor(pixelAt(canvas, center.x, center.y), palette.fill));
    }
  }
  const SDL_Rect panelPadding{nextPanelBox().x,
                              nextPanelBox().y + nextPanelBox().h - 5,
                              nextPanelBox().w, 5};
  SC_CHECK(areaIsOnly(canvas, panelPadding, kBackground));

  // Every hold shape draws in its own color, available and locked: the test
  // peer places each of the seven pieces in the slot and the box must show
  // exactly that palette, so one missing or wrong hold shape fails (task
  // T037: all Hold shapes, empty Hold above).
  for (int index = 0; index < kPieceCount; ++index) {
    const PieceId id = kAllPieceIds[index];
    stackfall::GameTestPeer::setHoldSlot(game, id);
    stackfall::GameTestPeer::setHoldLocked(game, false);
    renderer.draw(game);
    for (const Cell& cell : pieceCells(id, Rotation::Spawn)) {
      const SDL_Point center = previewCellCenter(id, hold, cell);
      SC_CHECK(sameColor(pixelAt(canvas, center.x, center.y), pieceFill(id)));
    }
    stackfall::GameTestPeer::setHoldLocked(game, true);
    renderer.draw(game);
    for (const Cell& cell : pieceCells(id, Rotation::Spawn)) {
      const SDL_Point center = previewCellCenter(id, hold, cell);
      SC_CHECK(sameColor(pixelAt(canvas, center.x, center.y),
                         dimCellPalette(id).fill));
    }
  }

  // Every next shape draws somewhere in the queue: across these seeds each of
  // the seven pieces appears in some slot, full color in the first slot and
  // dim behind it.
  bool seenFull[kPieceCount] = {false, false, false, false, false, false, false};
  bool seenDim[kPieceCount] = {false, false, false, false, false, false, false};
  for (int seed = 1; seed < 60; ++seed) {
    Game scan;
    scan.start(static_cast<std::uint64_t>(seed));
    renderer.draw(scan);
    const auto& scanQueue = scan.queue();
    for (int index = 0; index < kPreviewCount; ++index) {
      const SDL_Rect slot = nextSlotBox(index);
      const CellPalette& palette = index == 0 ? cellPalette(scanQueue[index])
                                              : dimCellPalette(scanQueue[index]);
      const Cell& first = pieceCells(scanQueue[index], Rotation::Spawn)[0];
      const SDL_Point center = previewCellCenter(scanQueue[index], slot, first);
      const int which = pieceIndex(scanQueue[index]);
      if (sameColor(pixelAt(canvas, center.x, center.y), palette.fill)) {
        if (index == 0) {
          seenFull[which] = true;
        } else {
          seenDim[which] = true;
        }
      }
    }
  }
  for (int index = 0; index < kPieceCount; ++index) {
    SC_CHECK(seenFull[index]);
    SC_CHECK(seenDim[index]);
  }

  // One hold stores the piece; the slot is spent, so it draws dimmed.
  Game holdGame;
  holdGame.start(4242);
  InputFrame holdPress = InputFrame::idle();
  holdPress.holdPressed = true;
  holdGame.tick(holdPress);
  SC_CHECK(holdGame.hasHoldPiece());
  SC_CHECK(holdGame.holdLocked());
  renderer.draw(holdGame);
  for (const Cell& cell : pieceCells(holdGame.holdPieceId(), Rotation::Spawn)) {
    const SDL_Point center =
        previewCellCenter(holdGame.holdPieceId(), hold, cell);
    SC_CHECK(sameColor(pixelAt(canvas, center.x, center.y),
                       dimCellPalette(holdGame.holdPieceId()).fill));
  }

  // A lock re-enables hold, and the stored piece draws at full color.
  InputFrame drop = InputFrame::idle();
  drop.hardDropPressed = true;
  holdGame.tick(drop);
  SC_CHECK(holdGame.holdLocked() == false);
  renderer.draw(holdGame);
  for (const Cell& cell : pieceCells(holdGame.holdPieceId(), Rotation::Spawn)) {
    const SDL_Point center =
        previewCellCenter(holdGame.holdPieceId(), hold, cell);
    SC_CHECK(sameColor(pixelAt(canvas, center.x, center.y),
                       pieceFill(holdGame.holdPieceId())));
  }
}

void run_text_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);

  // Every glyph pixel lands exactly where the table says, and nothing else
  // in the cell area does. Scale 3 turns one font pixel into a 3x3 square,
  // so every step inside the square is checked too.
  const char text[] = "A5? bz";
  const int scale = 3;
  const int x0 = 40;
  const int y0 = 60;
  const SDL_Color ink{255, 40, 60, 255};
  clearTo(canvas, kBackground);
  renderer.drawText(x0, y0, scale, ink, text);
  for (int index = 0; text[index] != '\0'; ++index) {
    const Glyph& glyph = glyphFor(text[index]);
    for (int row = 0; row < kGlyphRows; ++row) {
      for (int column = 0; column < kGlyphColumns; ++column) {
        const bool on = glyphPixel(glyph, column, row);
        for (int step = 0; step < scale; ++step) {
          const int x = x0 + (index * kGlyphAdvance + column) * scale + step;
          const int y = y0 + row * scale + step;
          SC_CHECK(sameColor(pixelAt(canvas, x, y), on ? ink : kBackground));
        }
      }
    }
    // The spacing column between two glyphs stays empty.
    for (int row = 0; row < kGlyphRows; ++row) {
      SC_CHECK(sameColor(pixelAt(canvas,
                                 x0 + (index * kGlyphAdvance + kGlyphColumns) *
                                           scale,
                                 y0 + row * scale),
                         kBackground));
    }
  }

  // A whole play frame carries the statistics of the left column: the HOLD
  // label and the three label-over-value fields, every row at the full label
  // glyph height, and no second text line anywhere else in the column - the
  // permanent control legend is gone from ordinary gameplay (task T037).
  Game game;
  game.start(4242);
  playRounds(game, 4);
  renderer.draw(game);
  const SDL_Rect leftColumn{kLeftPanelX, 0, kLeftPanelWidth, kCanvasHeight};
  const std::vector<int> leftRuns = textRowRuns(canvas, leftColumn);
  SC_CHECK(leftRuns.size() == 7);  // HOLD, SCORE, LINES, LEVEL, three values
  for (const int height : leftRuns) {
    SC_CHECK(height == kGlyphRows * kLabelScale);
  }
  SC_CHECK(countColor(canvas, leftColumn, kText) > 50);     // the values
  SC_CHECK(countColor(canvas, leftColumn, kTextDim) > 50);  // the labels

  // The right column carries the NEXT heading alone: the score fields moved
  // to the left group, so no bright value text remains on that side.
  const SDL_Rect rightColumn{kRightPanelX, 0, kRightPanelWidth, kCanvasHeight};
  const std::vector<int> rightRuns = textRowRuns(canvas, rightColumn);
  SC_CHECK(rightRuns.size() == 1);  // NEXT
  SC_CHECK(countColor(canvas, rightColumn, kText) == 0);
}

// ---------------------------------------------------------------------------
// The focused gameplay HUD of the approved composition (task T037, design
// review finding focused-gameplay-hud). The numbers are restated here on
// purpose like the board geometry above, so a constant that drifts from the
// specification fails before any capture records it:
//   * two 128 px columns at x=192..319 and x=640..767, each 24 px of quiet
//     gap from the board frame at x=344..615;
//   * statistics left (Hold, score, lines, level), five Next previews right;
//   * nothing the HUD draws reaches the board, crosses a gap, or leaves the
//     canvas - including the longest score and lines values the saturating
//     counters can hold;
//   * no permanent control legend below the statistics.
// ---------------------------------------------------------------------------
void run_focused_hud_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);

  // The geometry contract of the two groups.
  SC_CHECK(kHudGap == 24);
  SC_CHECK(kHudColumnWidth == 128);
  SC_CHECK(kLeftPanelX == 192);
  SC_CHECK(kLeftPanelWidth == 128);
  SC_CHECK(kLeftPanelX + kLeftPanelWidth - 1 == 319);
  SC_CHECK(kRightPanelX == 640);
  SC_CHECK(kRightPanelWidth == 128);
  SC_CHECK(kRightPanelX + kRightPanelWidth - 1 == 767);
  SC_CHECK(boardFrame().x - (kLeftPanelX + kLeftPanelWidth) == kHudGap);
  SC_CHECK(kRightPanelX - (boardFrame().x + boardFrame().w) == kHudGap);
  // The two columns sit symmetric about the board center.
  SC_CHECK(kLeftPanelX + kLeftPanelWidth / 2 == kCanvasWidth / 2 - 224);
  SC_CHECK(kRightPanelX + kRightPanelWidth / 2 == kCanvasWidth / 2 + 224);

  Game game;
  game.start(4242);
  playRounds(game, 6);
  SC_CHECK(game.scene() == Scene::Playing);
  SC_CHECK(game.score() > 0);
  renderer.draw(game);

  // The quiet gaps beside the frame hold nothing but background, full height:
  // no HUD pixel reaches the board from either group.
  const SDL_Rect leftGap{kLeftPanelX + kLeftPanelWidth, 0,
                         boardFrame().x - (kLeftPanelX + kLeftPanelWidth),
                         kCanvasHeight};
  const SDL_Rect rightGap{boardFrame().x + boardFrame().w, 0,
                          kRightPanelX - (boardFrame().x + boardFrame().w),
                          kCanvasHeight};
  SC_CHECK(leftGap.w == kHudGap);
  SC_CHECK(rightGap.w == kHudGap);
  SC_CHECK(areaIsOnly(canvas, leftGap, kBackground));
  SC_CHECK(areaIsOnly(canvas, rightGap, kBackground));

  // The outer regions of the canvas stay quiet as well.
  const SDL_Rect outerLeft{0, 0, kLeftPanelX, kCanvasHeight};
  const SDL_Rect outerRight{kRightPanelX + kRightPanelWidth, 0,
                            kCanvasWidth - (kRightPanelX + kRightPanelWidth),
                            kCanvasHeight};
  SC_CHECK(areaIsOnly(canvas, outerLeft, kBackground));
  SC_CHECK(areaIsOnly(canvas, outerRight, kBackground));

  // The decorative panel outlines are gone: the column bands hold no frame
  // or panel line color anywhere, so the only outline of the play scene is
  // the board frame itself.
  const SDL_Rect leftBand{kLeftPanelX, 0, kLeftPanelWidth, kCanvasHeight};
  const SDL_Rect rightBand{kRightPanelX, 0, kRightPanelWidth, kCanvasHeight};
  SC_CHECK(countColor(canvas, leftBand, kFrame) == 0);
  SC_CHECK(countColor(canvas, rightBand, kFrame) == 0);

  // The statistics group ends at the LEVEL value; below it the column holds
  // no text pixel at all, which is the removed permanent control legend.
  const int belowStats =
      kStatsFirstLabelY + 2 * kStatsFieldStride + kGlyphRows * kLabelScale + 4 +
      kGlyphRows * kValueScale + 8;
  const SDL_Rect legendBand{kLeftPanelX, belowStats, kLeftPanelWidth,
                            kCanvasHeight - belowStats};
  SC_CHECK(countColor(canvas, legendBand, kText) == 0);
  SC_CHECK(countColor(canvas, legendBand, kTextDim) == 0);

  // The longest values the counters can hold: the peer sets the saturating
  // maximum of score and lines, and both values must stay inside the column.
  // At twenty digits a value drops one scale step (239 px at the value scale
  // would cross into the board; 119 px at scale 1 fits with room), so every
  // supported number renders whole.
  stackfall::GameTestPeer::setScore(game, ~std::uint64_t{0});
  stackfall::GameTestPeer::setLines(game, ~std::uint64_t{0});
  SC_CHECK(game.level() == 20);
  renderer.draw(game);
  char text[24];
  std::snprintf(text, sizeof(text), "%llu",
                static_cast<unsigned long long>(~std::uint64_t{0}));
  SC_CHECK(textWidth(kValueScale, text) > kLeftPanelWidth);
  SC_CHECK(textWidth(1, text) <= kLeftPanelWidth);
  SC_CHECK(renderer.statValueScale(text) == 1);
  SC_CHECK(renderer.statValueScale("123456") == kValueScale);
  // Text pixels of the long values stay inside the column: the gaps and the
  // outer regions still hold pure background, and the column keeps text.
  SC_CHECK(areaIsOnly(canvas, leftGap, kBackground));
  SC_CHECK(areaIsOnly(canvas, outerLeft, kBackground));
  const SDL_Rect statsColumn{kLeftPanelX, kStatsFirstLabelY, kLeftPanelWidth,
                             3 * kStatsFieldStride};
  SC_CHECK(countColor(canvas, statsColumn, kText) > 0);
}

void run_read_only_and_scaling_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);

  Game game;
  game.start(987654321);
  playRounds(game, 3);
  const std::vector<std::uint8_t> before = snapshotOf(game);
  renderer.draw(game);
  renderer.draw(game);  // a second frame changes nothing either
  const std::vector<std::uint8_t> after = snapshotOf(game);
  SC_CHECK(before == after);

  // An un-started game draws the title page: no board well behind it and no
  // play-scene hold piece in the hold box. Its own canvas, so the play frame
  // under the checks below stays on `canvas`.
  Canvas bootCanvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(bootCanvas.ok);
  Renderer bootRenderer(bootCanvas.renderer);
  Game boot;
  bootRenderer.draw(boot);
  SC_CHECK(boot.scene() == Scene::Boot);
  SC_CHECK(sameColor(pixelAt(bootCanvas, kBoardOriginX + 3, kBoardOriginY + 3),
                     kBackground));
  for (const SDL_Color& fill : kPieceFills) {
    SC_CHECK(countColor(bootCanvas, holdBox(), fill) == 0);
  }

  // The same logical frame scales up 1.5x on a 1440x816 canvas, which keeps
  // the 960x544 aspect exactly: the frame edges move from logical x = 344 and
  // x = 615 to canvas x = 516 and x = 922, and the well fills 517..921
  // between them (a device pixel starts at floor(logical x * 1.5)).
  Canvas scaled = makeCanvas(1440, 816);
  SC_CHECK(scaled.ok);
  Renderer scaledRenderer(scaled.renderer);
  scaledRenderer.draw(game);
  SC_CHECK(sameColor(pixelAt(scaled, 515, 500), kBackground));
  SC_CHECK(sameColor(pixelAt(scaled, 516, 500), kFrame));
  SC_CHECK(sameColor(pixelAt(scaled, 517, 500), kBoardWell));
  SC_CHECK(sameColor(pixelAt(scaled, 921, 500), kBoardWell));
  SC_CHECK(sameColor(pixelAt(scaled, 922, 500), kFrame));
  SC_CHECK(sameColor(pixelAt(scaled, 923, 500), kBackground));

  // A taller 960x1088 canvas letterboxes the frame with a 272 pixel offset
  // and no scaling, so a logical pixel keeps its x and shifts its y.
  Canvas letterboxed = makeCanvas(960, 1088);
  SC_CHECK(letterboxed.ok);
  Renderer letterboxedRenderer(letterboxed.renderer);
  letterboxedRenderer.draw(game);
  SC_CHECK(sameColor(pixelAt(letterboxed, 5, 5 + 272), kBackground));
  SC_CHECK(sameColor(pixelAt(letterboxed, kBoardOriginX - 1, 271 + 272),
                     kFrame));
  SC_CHECK(sameColor(pixelAt(letterboxed, cellCenterX(0), cellCenterY(19) + 272),
                     pixelAt(canvas, cellCenterX(0), cellCenterY(19))));
}

// ---------------------------------------------------------------------------
// The letterbox canvas of a window that is not the canvas (task T032,
// section 6.1, design review finding letterbox-scene-centered). The reviewed
// 640x480 window anchored the scene at its top: 0 px of bar above, 118 px
// below. Every check here measures the drawn pixels of one frame through the
// same path the game draws, so the slack bars exist, split evenly, and hold
// black, not the stale pixels the output buffers carried before the resize.
// ---------------------------------------------------------------------------

// The letterbox black of beginFrame(), the one color of the frame that no
// scene element uses: the palette's darkest ground is the well at (10,12,19).
constexpr SDL_Color kLetterboxBlack{0, 0, 0, 255};

// One row of an arbitrary surface, read the way pixelAt reads a Canvas.
SDL_Color surfacePixel(const SDL_Surface* surface, int x, int y) {
  SDL_Color color{0, 0, 0, 0};
  if (surface == nullptr || x < 0 || y < 0 || x >= surface->w ||
      y >= surface->h) {
    return color;
  }
  const Uint32* pixels = static_cast<const Uint32*>(surface->pixels);
  SDL_GetRGBA(pixels[(y * surface->pitch / 4) + static_cast<std::size_t>(x)],
              surface->format, &color.r, &color.g, &color.b, &color.a);
  return color;
}

// The leading and the trailing run of pure black rows in a surface.
int leadingBlackRows(const SDL_Surface* surface) {
  int rows = 0;
  while (rows < surface->h) {
    bool black = true;
    for (int x = 0; x < surface->w && black; ++x) {
      black = sameColor(surfacePixel(surface, x, rows), kLetterboxBlack);
    }
    if (!black) {
      break;
    }
    ++rows;
  }
  return rows;
}

int trailingBlackRows(const SDL_Surface* surface) {
  int rows = 0;
  while (rows < surface->h) {
    const int y = surface->h - 1 - rows;
    bool black = true;
    for (int x = 0; x < surface->w && black; ++x) {
      black = sameColor(surfacePixel(surface, x, y), kLetterboxBlack);
    }
    if (!black) {
      break;
    }
    ++rows;
  }
  return rows;
}

// Draws one frame of `game` onto a stale-filled surface of `width` x
// `height`, exactly as the game's renderFrame does: the letterbox base first,
// then the scene. The stale fill stands for the pixels a resized window's
// buffers still hold, which is what a frame that paints no bars leaves in
// them.
Canvas drawLetterboxedFrame(const Game& game, int width, int height) {
  Canvas canvas = makeCanvas(width, height);
  if (canvas.ok) {
    SDL_FillRect(canvas.surface, nullptr,
                 SDL_MapRGB(canvas.surface->format, 200, 200, 200));
    beginFrame(canvas.renderer);
    Renderer renderer(canvas.renderer);
    renderer.draw(game);
  }
  return canvas;
}

void run_letterbox_canvas_checks() {
  Game game;
  game.start(31337);

  // The reviewed case: a 640x480 window. The canvas scales by two thirds, so
  // the scene spans 362 or 363 of the 480 rows and the slack splits between
  // the top and the bottom. Both bars must exist and stay within 2 px of each
  // other - the criterion tools/design-review/check_fixes.py measures on a
  // captured packet, restated here from the drawn pixels.
  Canvas window = drawLetterboxedFrame(game, 640, 480);
  SC_CHECK(window.ok);
  const int top = leadingBlackRows(window.surface);
  const int bottom = trailingBlackRows(window.surface);
  SC_CHECK(top > 0);
  SC_CHECK(bottom > 0);
  SC_CHECK(top - bottom <= 2 && bottom - top <= 2);
  const int sceneRows = window.surface->h - top - bottom;
  SC_CHECK(sceneRows - 362 <= 2 && 362 - sceneRows <= 2);

  // The stale pixels are gone: no row keeps the bright fill the frame began
  // on, and the scene really drew between the bars, scaled, with the board
  // well at its center.
  const SDL_Rect all{0, 0, window.surface->w, window.surface->h};
  SC_CHECK(countColor(window, all, SDL_Color{200, 200, 200, 255}) == 0);
  SC_CHECK(countColor(window, all, kBoardWell) > 1000);
  SC_CHECK(sameColor(pixelAt(window, 5, top + 3), kBackground));
  SC_CHECK(sameColor(pixelAt(window, 5, window.surface->h - bottom - 4),
                     kBackground));
  SC_CHECK(sameColor(pixelAt(window, cellCenterX(4) * 2 / 3,
                             top + cellCenterY(9) * 2 / 3),
                     kBoardWell));

  // A wider 720x480 window scales by three quarters, so its slack splits into
  // two equal 36 px bars. A second size holds the centering against a
  // scale that rounds differently.
  Canvas wide = drawLetterboxedFrame(game, 720, 480);
  SC_CHECK(wide.ok);
  const int wideTop = leadingBlackRows(wide.surface);
  const int wideBottom = trailingBlackRows(wide.surface);
  SC_CHECK(wideTop == 36);
  SC_CHECK(wideBottom == 36);

  // The capture of the frame reads the whole output in device pixels. Reading
  // in the logical space instead returns only the scene, anchored at the top
  // of the image, which is how the reviewed frame recorded 0 px above and
  // 118 px below.
  SDL_Surface* shot =
      SDL_CreateRGBSurfaceWithFormat(0, window.surface->w, window.surface->h,
                                     32, SDL_PIXELFORMAT_RGBA32);
  SC_CHECK(shot != nullptr);
  if (shot != nullptr) {
    SC_CHECK(readOutputPixels(window.renderer, shot));
    SC_CHECK(leadingBlackRows(shot) == top);
    SC_CHECK(trailingBlackRows(shot) == bottom);
    SC_CHECK(sameColor(surfacePixel(shot, 5, 2), kLetterboxBlack));
    SC_CHECK(sameColor(surfacePixel(shot, 5, top + 3), kBackground));
    // The canvas transform is back in place after the read: the next frame
    // draws scaled and centered again.
    Canvas again = drawLetterboxedFrame(game, 640, 480);
    SC_CHECK(again.ok);
    SC_CHECK(leadingBlackRows(again.surface) == top);
    SDL_FreeSurface(shot);
  }

  // At the native canvas size the frame path leaves the scene's own output
  // alone: no base, no bars, the same bytes the scene draws by itself. The
  // device runs fullscreen at this size.
  Canvas native = makeCanvas(kCanvasWidth, kCanvasHeight);
  Canvas plain = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(native.ok && plain.ok);
  beginFrame(native.renderer);
  Renderer(native.renderer).draw(game);
  Renderer(plain.renderer).draw(game);
  SC_CHECK(native.surface->pitch == plain.surface->pitch);
  SC_CHECK(std::memcmp(native.surface->pixels, plain.surface->pixels,
                       static_cast<std::size_t>(plain.surface->pitch) *
                           plain.surface->h) == 0);
}

// ---------------------------------------------------------------------------
// The section 4.7 screens of task T011.
// ---------------------------------------------------------------------------

// Moves the menu cursor one item down through the real session tick.
void menuDown(Game& game) {
  TickInput tick;
  tick.menu.downPressed = true;
  game.tick(tick);
}

// The canvas rectangle of one menu row: the item's text line plus its marker.
SDL_Rect menuRow(int y, int scale) {
  return SDL_Rect{0, y, kCanvasWidth, kGlyphRows * scale};
}

// Ends the round deterministically (see tests/test_screens.cpp): fill the
// spawn area except the active piece's own cells and column 9, then hard-drop.
// The spawn cells sit in rows 0 and 1 under the immediate-spawn rule of task
// T029, so the band covers both.
void forceGameOver(Game& game) {
  Board& board = stackfall::GameTestPeer::board(game);
  const std::array<Cell, kCellsPerPiece> cells =
      boardCells(game.activePiece());
  for (int y = -kHiddenRows; y <= 1; ++y) {
    for (int x = 0; x < kBoardWidth - 1; ++x) {
      bool pieceCell = false;
      for (const Cell& cell : cells) {
        pieceCell = pieceCell || (cell.x == x && cell.y == y);
      }
      if (!pieceCell) {
        board.settleCell(x, y, PieceId::J);
      }
    }
  }
  InputFrame drop = InputFrame::idle();
  drop.hardDropPressed = true;
  game.tick(drop);
  SC_CHECK(game.scene() == Scene::GameOver);
}

void run_title_screen_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);
  Game game;
  game.openTitle(20261103);
  renderer.draw(game);

  // Plain background: no board well, no grid, no panels.
  SC_CHECK(sameColor(pixelAt(canvas, 5, 5), kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX + 3, kBoardOriginY + 3),
                     kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, 150, 300), kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, 480, 470), kBackground));

  // The logo band carries bright glyphs.
  const SDL_Rect logo{0, kTitleLogoY, kCanvasWidth, kGlyphRows * kTitleLogoScale};
  SC_CHECK(countColor(canvas, logo, kText) > 1000);

  // The seven pieces draw in their order along the preview strip, one piece
  // per box, none dimmed.
  for (int index = 0; index < kPieceCount; ++index) {
    const SDL_Rect box = titlePreviewBox(index);
    SC_CHECK(countColor(canvas, box, pieceFill(kAllPieceIds[index])) > 100);
  }

  // The menu: the selected item is bright with an accent marker left of its
  // text; the others are dim with no marker. Marker and text positions come
  // from the same constants the renderer uses.
  const char* items[] = {"START GAME", "CONTROLS", "OPTIONS", "QUIT"};
  for (int index = 0; index < kTitleItemCount; ++index) {
    const int y = kTitleMenuY + index * kMenuItemHeight;
    const SDL_Rect row = menuRow(y, kMenuScale);
    if (index == game.titleSelection()) {
      SC_CHECK(countColor(canvas, row, kText) > 100);
      const int textLeft =
          kCanvasWidth / 2 - textWidth(kMenuScale, items[index]) / 2;
      const int markerX = textLeft - kMenuMarkerGap - kMenuMarkerSize;
      const int markerY = y + (kGlyphRows * kMenuScale - kMenuMarkerSize) / 2;
      SC_CHECK(areaIsOnly(canvas,
                          SDL_Rect{markerX, markerY, kMenuMarkerSize,
                                   kMenuMarkerSize},
                          kAccent));
    } else {
      SC_CHECK(countColor(canvas, row, kTextDim) > 100);
      SC_CHECK(countColor(canvas, row, kText) == 0);
      SC_CHECK(countColor(canvas, row, kAccent) == 0);
    }
  }

  // The high score reads out: label dim, value bright, and a loaded best
  // shows far more value pixels than the single glyph of zero.
  const SDL_Rect label = menuRow(kTitleHighScoreLabelY, kLabelScale);
  SC_CHECK(countColor(canvas, label, kTextDim) > 100);
  const SDL_Rect value = menuRow(kTitleHighScoreValueY, kValueScale);
  const int zeroPixels = countColor(canvas, value, kText);
  SC_CHECK(zeroPixels > 60);
  game.setHighScore(20261103);
  renderer.draw(game);
  SC_CHECK(countColor(canvas, value, kText) > zeroPixels + 350);

  // Navigation moves the marker: item one becomes the selected one.
  menuDown(game);
  SC_CHECK(game.titleSelection() == 1);
  renderer.draw(game);
  SC_CHECK(countColor(canvas, menuRow(kTitleMenuY, kMenuScale), kText) == 0);
  SC_CHECK(countColor(canvas, menuRow(kTitleMenuY + kMenuItemHeight, kMenuScale),
                      kText) > 100);

  // The footer hint line draws at the hint scale: one text line of the full
  // label height, and nothing else in its band.
  const SDL_Rect hintBand{0, kTitleHintY - 6, kCanvasWidth,
                          kGlyphRows * kHintScale + 12};
  const std::vector<int> hintRuns = textRowRuns(canvas, hintBand);
  SC_CHECK(hintRuns.size() == 1);
  SC_CHECK(hintRuns[0] == kGlyphRows * kHintScale);
}

void run_controls_screen_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);
  Game game;
  game.openTitle(1);
  menuDown(game);
  TickInput select;
  select.menu.selectPressed = true;
  game.tick(select);
  SC_CHECK(game.scene() == Scene::Controls);
  renderer.draw(game);

  // Plain background and a bright heading.
  SC_CHECK(sameColor(pixelAt(canvas, 5, 5), kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX + 3, kBoardOriginY + 3),
                     kBackground));
  const SDL_Rect heading{0, kControlsHeadingY, kCanvasWidth,
                         kGlyphRows * kControlsHeadingScale};
  SC_CHECK(countColor(canvas, heading, kText) > 500);

  // Three column headers and ten rows of bright text in three columns.
  const SDL_Rect header{kControlsGamepadX, kControlsHeaderY,
                        kCanvasWidth - kControlsGamepadX, kGlyphRows * kLabelScale};
  SC_CHECK(countColor(canvas, header, kTextDim) > 100);
  const SDL_Rect rows{kControlsGamepadX, kControlsFirstRowY,
                      kCanvasWidth - kControlsGamepadX,
                      kControlRowCount * kControlsRowHeight};
  SC_CHECK(countColor(canvas, rows, kText) > 2000);
  const SDL_Rect gamepadColumn{
      kControlsGamepadX, kControlsFirstRowY, kControlsKeyboardX - kControlsGamepadX,
      kControlRowCount * kControlsRowHeight};
  const SDL_Rect keyboardColumn{
      kControlsKeyboardX, kControlsFirstRowY,
      kControlsActionX - kControlsKeyboardX, kControlRowCount * kControlsRowHeight};
  const SDL_Rect actionColumn{
      kControlsActionX, kControlsFirstRowY, kCanvasWidth - kControlsActionX,
      kControlRowCount * kControlsRowHeight};
  SC_CHECK(countColor(canvas, gamepadColumn, kText) > 500);
  SC_CHECK(countColor(canvas, keyboardColumn, kText) > 500);
  SC_CHECK(countColor(canvas, actionColumn, kText) > 500);

  // The footer invites the way back, and B takes it.
  const SDL_Rect footer = menuRow(kControlsFooterY, kLabelScale);
  SC_CHECK(countColor(canvas, footer, kText) > 100);
  TickInput back;
  back.menu.backPressed = true;
  game.tick(back);
  SC_CHECK(game.scene() == Scene::Title);
}

// The options page of tasks T040 and T044: the heading, the six setting rows
// with every value combination, the accent marker on the highlighted row, and
// the footer band. The core moves the cursor alone; the values come from the
// renderer settings, so all 160 combinations of the five toggles and the next
// count are drawn, and the exact glyph count of the 5x7 table pins each row's
// string - ON cannot stand in for OFF, and NEXT PIECES 3 cannot stand in for
// NEXT PIECES 5.
void run_options_screen_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);
  Game game;
  game.openTitle(20261103);

  // The defaults mirror a session that loaded no settings file: every toggle
  // on, five Next previews, grayscale off, landscape, the Auto music pool,
  // and both MUSIC.md volume defaults (tasks T044, T055, and T062).
  SC_CHECK(kOptionsItemCount == 8);
  SC_CHECK(kOptionsMusicRow == 7);
  SC_CHECK(renderer.soundOn() == true);
  SC_CHECK(renderer.showGhost() == true);
  SC_CHECK(renderer.showGrid() == true);
  SC_CHECK(renderer.showHold() == true);
  SC_CHECK(renderer.nextPreviews() == kPreviewCount);
  SC_CHECK(renderer.grayscale() == false);
  SC_CHECK(renderer.orientation() == Orientation::Landscape);
  SC_CHECK(renderer.orientationPreference() == Orientation::Landscape);
  SC_CHECK(renderer.musicMode() == MusicMode::Auto);
  SC_CHECK(renderer.musicVolume() == 6);
  SC_CHECK(renderer.effectsVolume() == 8);
  SC_CHECK(std::string(renderer.musicTrackTitle()).empty());

  // Two downs from Start Game reach OPTIONS, and A opens the screen.
  menuDown(game);
  menuDown(game);
  SC_CHECK(game.titleSelection() == 2);
  TickInput select;
  select.menu.selectPressed = true;
  game.tick(select);
  SC_CHECK(game.scene() == Scene::Options);
  SC_CHECK(game.optionsSelection() == 0);  // SOUND is the opening row

  // Plain background and a bright heading at the options constants.
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 5, 5), kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX + 3, kBoardOriginY + 3),
                     kBackground));
  const SDL_Rect heading{0, kOptionsHeadingY, kCanvasWidth,
                         kGlyphRows * kOptionsHeadingScale};
  SC_CHECK(countColor(canvas, heading, kText) > 400);

  // Every value combination draws: each row reads its label plus the value of
  // its renderer setting - ON or OFF for the five toggles, the digit for
  // NEXT PIECES, the frame for ORIENTATION - and nothing else in a row band
  // is bright or dim, so the exact glyph count pins the string.
  for (int booleans = 0; booleans < 64; ++booleans) {
    for (int next = 1; next <= kPreviewCount; ++next) {
      renderer.setSoundOn((booleans & 1) != 0);
      renderer.setShowGhost((booleans & 2) != 0);
      renderer.setShowGrid((booleans & 4) != 0);
      renderer.setShowHold((booleans & 8) != 0);
      renderer.setGrayscale((booleans & 16) != 0);
      renderer.setOrientationPreference((booleans & 32) != 0
                                             ? Orientation::Portrait
                                             : Orientation::Landscape);
      renderer.setNextPreviews(next);
      renderer.draw(game);
      char nextRow[24];
      std::snprintf(nextRow, sizeof(nextRow), "NEXT PIECES %d", next);
      const char* rows[kOptionsItemCount] = {
          (booleans & 1) ? "SOUND ON" : "SOUND OFF",
          (booleans & 2) ? "GHOST ON" : "GHOST OFF",
          (booleans & 4) ? "GRID ON" : "GRID OFF",
          (booleans & 8) ? "HOLD ON" : "HOLD OFF",
          nextRow,
          (booleans & 16) ? "GRAYSCALE ON" : "GRAYSCALE OFF",
          (booleans & 32) ? "ORIENTATION PORTRAIT"
                          : "ORIENTATION LANDSCAPE",
          "MUSIC AUTO",
      };
      for (int index = 0; index < kOptionsItemCount; ++index) {
        const int y = kOptionsMenuY + index * kMenuItemHeight;
        const SDL_Rect row = menuRow(y, kMenuScale);
        const int pixels =
            glyphPixelCount(rows[index]) * kMenuScale * kMenuScale;
        if (index == game.optionsSelection()) {
          SC_CHECK(countColor(canvas, row, kText) == pixels);
          SC_CHECK(countColor(canvas, row, kTextDim) == 0);
        } else {
          SC_CHECK(countColor(canvas, row, kTextDim) == pixels);
          SC_CHECK(countColor(canvas, row, kText) == 0);
          SC_CHECK(countColor(canvas, row, kAccent) == 0);
        }
      }
    }
  }

  // Back to the defaults before the marker and navigation checks.
  renderer.setSoundOn(true);
  renderer.setShowGhost(true);
  renderer.setShowGrid(true);
  renderer.setShowHold(true);
  renderer.setGrayscale(false);
  renderer.setNextPreviews(kPreviewCount);

  // The marker sits left of the selected row's text: bright at the opening
  // SOUND row, gone from the row below it.
  renderer.draw(game);
  {
    const int y = kOptionsMenuY;
    const int textLeft =
        kCanvasWidth / 2 - textWidth(kMenuScale, "SOUND ON") / 2;
    const int markerX = textLeft - kMenuMarkerGap - kMenuMarkerSize;
    const int markerY = y + (kGlyphRows * kMenuScale - kMenuMarkerSize) / 2;
    SC_CHECK(areaIsOnly(canvas,
                        SDL_Rect{markerX, markerY, kMenuMarkerSize,
                                 kMenuMarkerSize},
                        kAccent));
    const SDL_Rect rowBelow = menuRow(y + kMenuItemHeight, kMenuScale);
    SC_CHECK(countColor(canvas, rowBelow, kAccent) == 0);
  }

  // Seven downs walk the cursor to the last row, and the marker follows: the
  // first row goes dim and the MUSIC row goes bright.
  for (int step = 0; step < kOptionsItemCount - 1; ++step) {
    menuDown(game);
  }
  SC_CHECK(game.optionsSelection() == kOptionsMusicRow);
  renderer.draw(game);
  SC_CHECK(countColor(canvas, menuRow(kOptionsMenuY, kMenuScale), kText) == 0);
  SC_CHECK(countColor(canvas,
                      menuRow(kOptionsMenuY +
                                  (kOptionsItemCount - 1) * kMenuItemHeight,
                              kMenuScale),
                      kText) > 100);
  // The eighth down stays inside the screen: one more down wraps to the
  // SOUND row, whose band turns bright again.
  menuDown(game);
  SC_CHECK(game.optionsSelection() == 0);
  renderer.draw(game);
  SC_CHECK(countColor(canvas, menuRow(kOptionsMenuY, kMenuScale), kText) > 100);

  // The footer band holds one hint-scale line of the full label height and
  // exactly the glyphs of the footer text, and every row and the footer sit
  // inside the canvas with nothing clipped (task T044).
  const SDL_Rect footerBand{0, kOptionsFooterY - 6, kCanvasWidth,
                            kGlyphRows * kHintScale + 12};
  const std::vector<int> footerRuns = textRowRuns(canvas, footerBand);
  SC_CHECK(footerRuns.size() == 1);
  SC_CHECK(footerRuns[0] == kGlyphRows * kHintScale);
  SC_CHECK(countColor(canvas, footerBand, kTextDim) ==
           glyphPixelCount("UP/DOWN CHOOSE - A CHANGE - B BACK") * kHintScale *
               kHintScale);
  SC_CHECK(kOptionsHeadingY + kGlyphRows * kOptionsHeadingScale <
           kOptionsMenuY);
  SC_CHECK(kOptionsMenuY + (kOptionsItemCount - 1) * kMenuItemHeight +
               kGlyphRows * kMenuScale <
           kOptionsFooterY);
  SC_CHECK(kOptionsFooterY + kGlyphRows * kHintScale < kCanvasHeight);
  // The last row's text and marker never reach the canvas edge: the widest
  // row keeps its whole text and marker on the canvas.
  {
    const int lastY =
        kOptionsMenuY + (kOptionsItemCount - 1) * kMenuItemHeight;
    const int textLeft = kCanvasWidth / 2 -
                         textWidth(kMenuScale, "ORIENTATION PORTRAIT") / 2;
    SC_CHECK(textLeft - kMenuMarkerGap - kMenuMarkerSize >= 0);
    SC_CHECK(textLeft + textWidth(kMenuScale, "ORIENTATION PORTRAIT") <=
             kCanvasWidth);
    SC_CHECK(lastY + kGlyphRows * kMenuScale <= kCanvasHeight);
  }

  // B returns to the title with the cursor still on the OPTIONS item, so the
  // four-item menu shows OPTIONS selected above a dim QUIT.
  TickInput back;
  back.menu.backPressed = true;
  game.tick(back);
  SC_CHECK(game.scene() == Scene::Title);
  SC_CHECK(game.titleSelection() == 2);
  renderer.draw(game);
  SC_CHECK(countColor(canvas,
                      menuRow(kTitleMenuY + 2 * kMenuItemHeight, kMenuScale),
                      kText) > 100);
  SC_CHECK(countColor(canvas,
                      menuRow(kTitleMenuY + 3 * kMenuItemHeight, kMenuScale),
                      kTextDim) > 100);
}

// The music settings page behind the MUSIC row (task T062, MUSIC.md player
// controls): the same heading, menu metrics, and footer hint as the options
// page, over its four rows and the playing track's dim title line, which
// stays empty while music is silent. The exact glyph count pins each row's
// string, exactly as the options page does.
void run_music_options_screen_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);
  Game game;
  game.openTitle(20261104);

  // Reach the page: OPTIONS, seven downs to the MUSIC row, A to open.
  menuDown(game);
  menuDown(game);
  SC_CHECK(game.titleSelection() == 2);
  TickInput select;
  select.menu.selectPressed = true;
  game.tick(select);
  SC_CHECK(game.scene() == Scene::Options);
  for (int step = 0; step < kOptionsMusicRow; ++step) {
    menuDown(game);
  }
  game.tick(select);
  SC_CHECK(game.scene() == Scene::MusicOptions);
  SC_CHECK(game.musicSelection() == 0);

  // Plain background and a bright MUSIC heading at the shared constants.
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 5, 5), kBackground));
  const SDL_Rect heading{0, kOptionsHeadingY, kCanvasWidth,
                         kGlyphRows * kOptionsHeadingScale};
  SC_CHECK(countColor(canvas, heading, kText) > 100);

  // Every mode and volume combination draws: each row reads its label plus
  // the value of its renderer mirror, and nothing else in a row band is
  // bright or dim, so the exact glyph count pins the string. The track title
  // line reads the mirror the platform layer feeds the director's title.
  char modeText[16];
  char musicVolumeText[24];
  char effectsVolumeText[24];
  const char* modeNames[5] = {"AUTO", "ARCADE", "GROOVE", "FOCUS", "OFF"};
  for (int mode = 0; mode <= static_cast<int>(MusicMode::Off); ++mode) {
    for (int music = 0; music <= 10; ++music) {
      for (int effects = 0; effects <= 10; ++effects) {
        renderer.setMusicMode(static_cast<MusicMode>(mode));
        renderer.setMusicVolume(music);
        renderer.setEffectsVolume(effects);
        const bool silent = mode == static_cast<int>(MusicMode::Off);
        renderer.setMusicTrackTitle(silent ? "" : "BOOT SEQUENCE");
        renderer.draw(game);
        std::snprintf(modeText, sizeof(modeText), "MUSIC %s",
                      modeNames[mode]);
        std::snprintf(musicVolumeText, sizeof(musicVolumeText),
                      "MUSIC VOLUME %d", music);
        std::snprintf(effectsVolumeText, sizeof(effectsVolumeText),
                      "EFFECTS VOLUME %d", effects);
        const char* rows[kMusicOptionsItemCount] = {
            modeText, musicVolumeText, effectsVolumeText, "NEXT TRACK"};
        for (int index = 0; index < kMusicOptionsItemCount; ++index) {
          const int y = kOptionsMenuY + index * kMenuItemHeight;
          const SDL_Rect row = menuRow(y, kMenuScale);
          const int pixels =
              glyphPixelCount(rows[index]) * kMenuScale * kMenuScale;
          if (index == game.musicSelection()) {
            SC_CHECK(countColor(canvas, row, kText) == pixels);
            SC_CHECK(countColor(canvas, row, kTextDim) == 0);
          } else {
            SC_CHECK(countColor(canvas, row, kTextDim) == pixels);
            SC_CHECK(countColor(canvas, row, kText) == 0);
            SC_CHECK(countColor(canvas, row, kAccent) == 0);
          }
        }
        // The title line: exactly the title's glyphs while a track plays,
        // nothing at all while music is silent.
        const SDL_Rect trackBand{0, kMusicTrackY - 6, kCanvasWidth,
                                 kGlyphRows * kMenuScale + 12};
        if (silent) {
          SC_CHECK(countColor(canvas, trackBand, kTextDim) == 0);
          SC_CHECK(countColor(canvas, trackBand, kText) == 0);
        } else {
          SC_CHECK(countColor(canvas, trackBand, kTextDim) ==
                   glyphPixelCount("BOOT SEQUENCE") * kMenuScale * kMenuScale);
          SC_CHECK(countColor(canvas, trackBand, kText) == 0);
        }
      }
    }
  }

  // Back to the defaults, and the cursor walks the four rows with the marker:
  // three downs reach NEXT TRACK and one more wraps to MUSIC.
  renderer.setMusicMode(MusicMode::Auto);
  renderer.setMusicVolume(6);
  renderer.setEffectsVolume(8);
  renderer.setMusicTrackTitle("BOOT SEQUENCE");
  for (int step = 0; step < kMusicOptionsItemCount - 1; ++step) {
    menuDown(game);
  }
  SC_CHECK(game.musicSelection() == kMusicOptionsItemCount - 1);
  renderer.draw(game);
  SC_CHECK(countColor(canvas, menuRow(kOptionsMenuY, kMenuScale), kText) == 0);
  SC_CHECK(countColor(canvas,
                      menuRow(kOptionsMenuY +
                                  (kMusicOptionsItemCount - 1) *
                                      kMenuItemHeight,
                              kMenuScale),
                      kText) > 100);
  menuDown(game);
  SC_CHECK(game.musicSelection() == 0);
  renderer.draw(game);
  SC_CHECK(countColor(canvas, menuRow(kOptionsMenuY, kMenuScale), kText) > 100);

  // The footer band holds exactly the shared options hint, and every row, the
  // title line, and the footer sit inside the canvas with nothing clipped.
  const SDL_Rect footerBand{0, kOptionsFooterY - 6, kCanvasWidth,
                            kGlyphRows * kHintScale + 12};
  const std::vector<int> footerRuns = textRowRuns(canvas, footerBand);
  SC_CHECK(footerRuns.size() == 1);
  SC_CHECK(footerRuns[0] == kGlyphRows * kHintScale);
  SC_CHECK(countColor(canvas, footerBand, kTextDim) ==
           glyphPixelCount("UP/DOWN CHOOSE - A CHANGE - B BACK") * kHintScale *
               kHintScale);
  SC_CHECK(kOptionsHeadingY + kGlyphRows * kOptionsHeadingScale <
           kOptionsMenuY);
  SC_CHECK(kMusicTrackY > kOptionsMenuY +
                              (kMusicOptionsItemCount - 1) * kMenuItemHeight +
                              kGlyphRows * kMenuScale);
  SC_CHECK(kMusicTrackY + kGlyphRows * kMenuScale < kOptionsFooterY);
  SC_CHECK(kOptionsFooterY + kGlyphRows * kHintScale < kCanvasHeight);

  // Back returns to the options screen with the cursor still on the MUSIC
  // row, whose band turns bright again.
  TickInput back;
  back.menu.backPressed = true;
  game.tick(back);
  SC_CHECK(game.scene() == Scene::Options);
  SC_CHECK(game.optionsSelection() == kOptionsMusicRow);
  renderer.draw(game);
  SC_CHECK(countColor(canvas,
                      menuRow(kOptionsMenuY + kOptionsMusicRow * kMenuItemHeight,
                              kMenuScale),
                      kText) > 100);
}

void run_pause_overlay_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);

  // A round with settled cells, then paused.
  Game game;
  game.start(30);
  playRounds(game, 8);
  SC_CHECK(game.scene() == Scene::Playing);
  SC_CHECK(game.board().settledCellCount() > 0);

  // A settled cell in the bottom two rows, below every overlay panel, for
  // the veil check.
  SDL_Point veiled{-1, -1};
  for (int y = kBoardHeight - 1; y >= kBoardHeight - 2 && veiled.x < 0; --y) {
    for (int x = 0; x < kBoardWidth; ++x) {
      if (game.board().cellTypes()[boardRowIndex(y)][x] != 0) {
        veiled = SDL_Point{cellCenterX(x), cellCenterY(y)};
        break;
      }
    }
  }
  SC_CHECK(veiled.x >= 0);
  renderer.draw(game);
  const SDL_Color before = pixelAt(canvas, veiled.x, veiled.y);

  InputFrame pause = InputFrame::idle();
  pause.pausePressed = true;
  game.tick(pause);
  SC_CHECK(game.scene() == Scene::Paused);
  renderer.draw(game);

  // The veil dims the frozen scene: every channel of the settled cell moves
  // toward the background and lands between the two colors.
  const SDL_Color after = pixelAt(canvas, veiled.x, veiled.y);
  SC_CHECK(sameColor(after, before) == false);
  const int loR = before.r < kBackground.r ? before.r : kBackground.r;
  const int hiR = before.r < kBackground.r ? kBackground.r : before.r;
  const int loG = before.g < kBackground.g ? before.g : kBackground.g;
  const int hiG = before.g < kBackground.g ? kBackground.g : before.g;
  const int loB = before.b < kBackground.b ? before.b : kBackground.b;
  const int hiB = before.b < kBackground.b ? kBackground.b : before.b;
  SC_CHECK(after.r >= loR && after.r <= hiR);
  SC_CHECK(after.g >= loG && after.g <= hiG);
  SC_CHECK(after.b >= loB && after.b <= hiB);

  // The veil covers the whole canvas, so it also reaches the corners of the
  // enlarged field: the frame's bottom-right pixel at (615, 542) is dimmed,
  // not the plain frame color the play scene drew (task T038).
  const SDL_Rect frame = boardFrame();
  const int frameCornerX = frame.x + frame.w - 1;
  const int frameCornerY = frame.y + frame.h - 1;
  SC_CHECK(sameColor(pixelAt(canvas, frameCornerX, frameCornerY), kFrame) ==
           false);

  // The panel is an opaque card that nests between the HUD columns: its
  // corner holds the exact panel fill, and the columns beside it stay free
  // of the panel fill, so the frozen statistics and previews remain visible
  // around the modal (task T037).
  const SDL_Rect panel = pausePanel();
  SC_CHECK(sameColor(pixelAt(canvas, panel.x + 6, panel.y + 6), kPanelFill));
  const SDL_Rect leftColumnBand{kLeftPanelX, 0, kLeftPanelWidth, kCanvasHeight};
  const SDL_Rect rightColumnBand{kRightPanelX, 0, kRightPanelWidth,
                                 kCanvasHeight};
  SC_CHECK(countColor(canvas, leftColumnBand, kPanelFill) == 0);
  SC_CHECK(countColor(canvas, rightColumnBand, kPanelFill) == 0);

  // The heading, the four items, and the marker follow the layout constants.
  const SDL_Rect heading{panel.x, panel.y + kPauseHeadingOffsetY, panel.w,
                         kGlyphRows * kControlsHeadingScale};
  SC_CHECK(countColor(canvas, heading, kText) > 400);
  const char* items[] = {"RESUME", "RESTART", "CONTROLS", "QUIT TO TITLE"};
  for (int index = 0; index < kPauseItemCount; ++index) {
    const int y = panel.y + kPauseMenuOffsetY + index * kMenuItemHeight;
    const SDL_Rect row = menuRow(y, kMenuScale);
    if (index == game.pauseSelection()) {
      SC_CHECK(countColor(canvas, row, kText) > 100);
      const int textLeft =
          kCanvasWidth / 2 - textWidth(kMenuScale, items[index]) / 2;
      const int markerX = textLeft - kMenuMarkerGap - kMenuMarkerSize;
      SC_CHECK(sameColor(pixelAt(canvas, markerX + kMenuMarkerSize / 2,
                                 y + (kGlyphRows * kMenuScale - kMenuMarkerSize) / 2 +
                                     kMenuMarkerSize / 2),
                         kAccent));
    } else {
      SC_CHECK(countColor(canvas, row, kTextDim) > 100);
      SC_CHECK(countColor(canvas, row, kText) == 0);
    }
  }

  // The footer hint draws at the hint scale: one text line of the full label
  // height inside the panel.
  const SDL_Rect pauseHintBand{panel.x, panel.y + kPauseHintOffsetY - 6,
                               panel.w, kGlyphRows * kHintScale + 12};
  const std::vector<int> pauseHintRuns = textRowRuns(canvas, pauseHintBand);
  SC_CHECK(pauseHintRuns.size() == 1);
  SC_CHECK(pauseHintRuns[0] == kGlyphRows * kHintScale);

  // Menu navigation under the overlay leaves the frozen round byte-identical:
  // two more draws of the same paused state agree everywhere on the board.
  const std::vector<std::uint8_t> frozenBefore = snapshotOf(game);
  menuDown(game);
  const std::vector<std::uint8_t> frozenAfter = snapshotOf(game);
  SC_CHECK(frozenBefore == frozenAfter);
  renderer.draw(game);
  const SDL_Color again = pixelAt(canvas, veiled.x, veiled.y);
  SC_CHECK(sameColor(again, after));
  SC_CHECK(game.scene() == Scene::Paused);

  // The Controls item opens the mappings page over the frozen round: the
  // same three columns of the title route, a footer that names the pause
  // return, and a round that no tick of the visit touches (task T037).
  menuDown(game);  // cursor lands on Controls
  SC_CHECK(game.pauseSelection() == 2);
  const std::uint64_t frozenTicks = game.tickCount();
  const std::uint64_t frozenScore = game.score();
  const auto frozenBoard = game.board().occupancy();
  TickInput select;
  select.menu.selectPressed = true;
  game.tick(select);
  SC_CHECK(game.scene() == Scene::PauseControls);
  renderer.draw(game);
  const SDL_Rect pauseControlsHeading{0, kControlsHeadingY, kCanvasWidth,
                                      kGlyphRows * kControlsHeadingScale};
  SC_CHECK(countColor(canvas, pauseControlsHeading, kText) > 500);
  const SDL_Rect rows{kControlsGamepadX, kControlsFirstRowY,
                      kCanvasWidth - kControlsGamepadX,
                      kControlRowCount * kControlsRowHeight};
  SC_CHECK(countColor(canvas, rows, kText) > 2000);
  const SDL_Rect pauseControlsFooter = menuRow(kControlsFooterY, kLabelScale);
  SC_CHECK(countColor(canvas, pauseControlsFooter, kText) ==
           glyphPixelCount("A OR B BACK TO PAUSE") * kLabelScale * kLabelScale);
  const std::vector<std::uint8_t> visitBefore = snapshotOf(game);
  menuDown(game);  // the page ignores cursor moves
  InputFrame wild = InputFrame::idle();
  wild.hardDropPressed = true;
  game.tick(wild);
  SC_CHECK(game.scene() == Scene::PauseControls);
  SC_CHECK(snapshotOf(game) == visitBefore);
  TickInput back;
  back.menu.backPressed = true;
  game.tick(back);
  SC_CHECK(game.scene() == Scene::Paused);
  SC_CHECK(game.pauseSelection() == 2);
  SC_CHECK(game.tickCount() == frozenTicks);
  SC_CHECK(game.score() == frozenScore);
  SC_CHECK(game.board().occupancy() == frozenBoard);
}

void run_gameover_overlay_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);

  // A short scoring round, ended deterministically: the record note must show.
  Game game;
  game.openTitle(77);
  TickInput start;
  start.menu.selectPressed = true;
  game.tick(start);
  InputFrame soft = InputFrame::idle();
  soft.downHeld = true;
  game.tick(soft);
  game.tick(soft);
  game.tick(soft);
  forceGameOver(game);
  SC_CHECK(game.score() > 0);
  SC_CHECK(game.newRecord());
  renderer.draw(game);

  // The panel nests between the HUD columns like the pause panel: it spans
  // the gap from the left column's edge to the right column's edge, its
  // corner holds the exact panel fill, and the columns beside it stay free
  // of the panel fill, so the dimmed statistics and previews read whole
  // around the modal instead of peeking out at its edge (task T043).
  const SDL_Rect panel = gameoverPanel();
  SC_CHECK(panel.x == kLeftPanelX + kLeftPanelWidth);
  SC_CHECK(panel.x + panel.w == kRightPanelX);
  SC_CHECK(panel.x == pausePanel().x && panel.w == pausePanel().w);
  SC_CHECK(sameColor(pixelAt(canvas, panel.x + 6, panel.y + 6), kPanelFill));
  const SDL_Rect overLeftColumnBand{kLeftPanelX, 0, kLeftPanelWidth,
                                    kCanvasHeight};
  const SDL_Rect overRightColumnBand{kRightPanelX, 0, kRightPanelWidth,
                                     kCanvasHeight};
  SC_CHECK(countColor(canvas, overLeftColumnBand, kPanelFill) == 0);
  SC_CHECK(countColor(canvas, overRightColumnBand, kPanelFill) == 0);

  // The narrowed panel still holds its widest row: both 56 px margins, the
  // five-glyph label, and an eight-digit score at the menu scale - the
  // value scale's six-digit ceiling of the HUD column sits far below this -
  // fit inside the panel's width without the label and value meeting.
  SC_CHECK(2 * 56 + 5 * kGlyphAdvance * kLabelScale +
               textWidth(kMenuScale, "99999999") <=
           panel.w);

  const SDL_Rect heading{panel.x, panel.y + kOverHeadingOffsetY, panel.w,
                         kGlyphRows * kControlsHeadingScale};
  SC_CHECK(countColor(canvas, heading, kText) > 500);

  // The three final values draw right-aligned: every stat line carries a
  // bright value and a dim label.
  for (int index = 0; index < 3; ++index) {
    const int y = panel.y + kOverStatsOffsetY + index * kOverStatsRowHeight;
    const SDL_Rect line{panel.x, y, panel.w, kGlyphRows * kMenuScale};
    SC_CHECK(countColor(canvas, line, kText) > 60);
    const SDL_Rect labelBox{panel.x + 56, y + 3, 5 * kGlyphAdvance * kLabelScale,
                            kGlyphRows * kLabelScale};
    SC_CHECK(countColor(canvas, labelBox, kTextDim) > 60);
  }

  // The record note shows in the accent color exactly when the round set one.
  const SDL_Rect record{panel.x, panel.y + kOverRecordOffsetY, panel.w,
                        kGlyphRows * kLabelScale};
  SC_CHECK(countColor(canvas, record, kAccent) > 200);

  // Play Again is the default item; Title waits below it.
  const char* items[] = {"PLAY AGAIN", "TITLE"};
  for (int index = 0; index < kOverItemCount; ++index) {
    const int y = panel.y + kOverMenuOffsetY + index * kMenuItemHeight;
    const SDL_Rect row = menuRow(y, kMenuScale);
    if (index == game.overSelection()) {
      SC_CHECK(countColor(canvas, row, kText) > 100);
      const int textLeft =
          kCanvasWidth / 2 - textWidth(kMenuScale, items[index]) / 2;
      const int markerX = textLeft - kMenuMarkerGap - kMenuMarkerSize;
      SC_CHECK(sameColor(pixelAt(canvas, markerX + kMenuMarkerSize / 2,
                                 y + (kGlyphRows * kMenuScale - kMenuMarkerSize) / 2 +
                                     kMenuMarkerSize / 2),
                         kAccent));
    } else {
      SC_CHECK(countColor(canvas, row, kTextDim) > 100);
      SC_CHECK(countColor(canvas, row, kText) == 0);
    }
  }

  // Without a record the note disappears; a loaded high score hides it.
  Game better;
  better.openTitle(77);
  better.setHighScore(99999999);
  better.tick(start);
  better.tick(soft);
  forceGameOver(better);
  SC_CHECK(better.scene() == Scene::GameOver);
  SC_CHECK(!better.newRecord());
  renderer.draw(better);
  SC_CHECK(countColor(canvas, record, kAccent) == 0);
}

// ---------------------------------------------------------------------------
// The portrait canvas of task T055 (notes/design-review.md, "Portrait mode").
// The device canvas stays 960x544; portrait rotates every drawn rect one
// quarter inside it, so these checks state the rotation here, on purpose,
// with their own arithmetic: portrait pixel (x, y) lands on device pixel
// (y, 543 - x), and a portrait rect lands as a transposed rect. A draw call
// that misses the rotation fails them before any screenshot records it.
// ---------------------------------------------------------------------------

SDL_Point portraitPixel(int x, int y) {
  return SDL_Point{y, kCanvasHeight - 1 - x};
}

SDL_Color pixelAtPoint(const Canvas& canvas, const SDL_Point& point) {
  return pixelAt(canvas, point.x, point.y);
}

bool sameRect(const SDL_Rect& a, const SDL_Rect& b) {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

SDL_Rect portraitDeviceRect(int x, int y, int w, int h) {
  return SDL_Rect{y, kCanvasHeight - x - w, h, w};
}

// Portrait cell geometry helpers, the counterparts of cellCornerX and friends.
constexpr int pCellCornerX(int x) {
  return kPortraitBoardOriginX + x * kPortraitBoardCellSize;
}
constexpr int pCellCornerY(int y) {
  return kPortraitBoardOriginY + y * kPortraitBoardCellSize;
}

void run_portrait_board_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);
  SC_CHECK(renderer.orientation() == Orientation::Landscape);

  // The pinned geometry of notes/design-review.md: 40 px cells, a 400x800
  // field at (72, 120), its frame at x=71..472 and y=119..920, all inside
  // the 544x960 portrait canvas with quiet margins on every side.
  SC_CHECK(kPortraitCanvasWidth == 544);
  SC_CHECK(kPortraitCanvasHeight == 960);
  SC_CHECK(kPortraitBoardCellSize == 40);
  SC_CHECK(kPortraitBoardOriginX == 72);
  SC_CHECK(kPortraitBoardOriginY == 120);
  SC_CHECK(kPortraitBoardPixelWidth == 400);
  SC_CHECK(kPortraitBoardPixelHeight == 800);
  const SDL_Rect frame = portraitBoardFrame();
  SC_CHECK(frame.x == 71 && frame.y == 119);
  SC_CHECK(frame.w == 402 && frame.h == 802);
  SC_CHECK(frame.x + frame.w - 1 == 472);
  SC_CHECK(frame.y + frame.h - 1 == 920);
  SC_CHECK(sameRect(portraitHoldBox(), SDL_Rect{12, 78, 56, 32}));
  SC_CHECK(sameRect(portraitNextSlotBox(0), SDL_Rect{240, 78, 56, 32}));
  SC_CHECK(sameRect(portraitNextSlotBox(4), SDL_Rect{480, 78, 56, 32}));
  SC_CHECK(sameRect(pausePanelPortrait(), SDL_Rect{72, 280, 400, 360}));
  SC_CHECK(sameRect(gameoverPanelPortrait(), SDL_Rect{32, 200, 480, 440}));

  renderer.setOrientation(Orientation::Portrait);
  SC_CHECK(renderer.orientation() == Orientation::Portrait);

  Game game;
  game.start(20261103);
  renderer.draw(game);

  // A column and a row of the empty field no active or ghost cell covers,
  // the portrait counterpart of the landscape boundary suite's fixed column.
  int clearColumn = -1;
  for (int column = 0; column < kBoardWidth && clearColumn < 0; ++column) {
    bool covered = false;
    for (const Cell& cell : boardCells(game.activePiece())) {
      covered = covered || cell.x == column;
    }
    for (const Cell& cell : boardCells(game.ghostPiece())) {
      covered = covered || cell.x == column;
    }
    if (!covered) {
      clearColumn = column;
    }
  }
  SC_CHECK(clearColumn >= 0);
  const int columnMidY =
      kCanvasHeight - 1 - (pCellCornerX(clearColumn) + 20);

  // The frame outline on the device canvas, through the rotation: portrait
  // x=71..472 and y=119..920 becomes device x=119..920 and y=71..472. Along
  // the clear column, from the canvas top of the turned frame to its bottom:
  // background, frame, well, then the mirror image.
  SC_CHECK(sameColor(pixelAt(canvas, 118, columnMidY), kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, 119, columnMidY), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, 120, columnMidY), kBoardWell));
  SC_CHECK(sameColor(pixelAt(canvas, 919, columnMidY), kBoardWell));
  SC_CHECK(sameColor(pixelAt(canvas, 920, columnMidY), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, 921, columnMidY), kBackground));
  // Along the clear middle row of the field, from the frame's device left to
  // its right: the same ladder.
  const int rowMidX = pCellCornerY(0) + 10 * kPortraitBoardCellSize + 20;
  SC_CHECK(sameColor(pixelAt(canvas, rowMidX, 70), kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, rowMidX, 71), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, rowMidX, 72), kBoardWell));
  SC_CHECK(sameColor(pixelAt(canvas, rowMidX, 471), kBoardWell));
  SC_CHECK(sameColor(pixelAt(canvas, rowMidX, 472), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, rowMidX, 473), kBackground));

  // The quiet margins of the portrait frame, in portrait coordinates read
  // through the rotation: the columns left and right of the board and the
  // rows below it stay background, so nothing the strip draws can clip.
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(5, 500)), kBackground));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(539, 500)), kBackground));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(272, 945)), kBackground));

  // The grid at the 40 px pitch: the first interior boundary sits one cell
  // in from the field's top-left corner, on the device canvas.
  const SDL_Point firstGridX = portraitPixel(pCellCornerX(0) +
                                             kPortraitBoardCellSize,
                                             pCellCornerY(0) + 7);
  const SDL_Point firstGridY = portraitPixel(pCellCornerX(0) + 7,
                                             pCellCornerY(0) +
                                                 kPortraitBoardCellSize);
  SC_CHECK(sameColor(pixelAt(canvas, firstGridX.x, firstGridX.y), kGridLine));
  SC_CHECK(sameColor(pixelAt(canvas, firstGridY.x, firstGridY.y), kGridLine));
  const SDL_Point lastGridX = portraitPixel(
      pCellCornerX(kBoardWidth - 1), pCellCornerY(0) + 7);
  SC_CHECK(sameColor(pixelAt(canvas, lastGridX.x, lastGridX.y), kGridLine));

  // A whole 40 px cell, settled at the field's top-left corner. Portrait
  // (72, 120) is device (120, 471); the cell spans device x=120..159 and
  // y=432..471, the transpose of its portrait block, with the frame at
  // device x=119 to its portrait top and device y=472 to its portrait left.
  Board& board = stackfall::GameTestPeer::board(game);
  SC_CHECK(board.settleCell(0, 0, PieceId::I));
  SC_CHECK(board.settleCell(kBoardWidth - 1, kBoardHeight - 1, PieceId::O));
  renderer.draw(game);
  const SDL_Point cellTopLeft = portraitPixel(pCellCornerX(0), pCellCornerY(0));
  SC_CHECK(cellTopLeft.x == 120 && cellTopLeft.y == 471);
  SC_CHECK(sameColor(pixelAt(canvas, 119, cellTopLeft.y), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, cellTopLeft.x, 472), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, cellTopLeft.x, cellTopLeft.y),
                     kCellEdge));
  SC_CHECK(sameColor(pixelAt(canvas, cellTopLeft.x, 432), kCellEdge));
  // The light and dark bands turn with the cell: portrait's light top and
  // left become the device block's left column and bottom row, and portrait's
  // dark bottom and right become the device right column and top row.
  SC_CHECK(sameColor(pixelAt(canvas, 121, 450),
                     cellPalette(PieceId::I).light));
  SC_CHECK(sameColor(pixelAt(canvas, 140, 470),
                     cellPalette(PieceId::I).light));
  SC_CHECK(sameColor(pixelAt(canvas, 158, 450), cellPalette(PieceId::I).dark));
  SC_CHECK(sameColor(pixelAt(canvas, 140, 433), cellPalette(PieceId::I).dark));
  SC_CHECK(sameColor(pixelAt(canvas, 140, 450), pieceFill(PieceId::I)));
  // The bottom-right cell lands whole at the far corner of the device frame.
  const SDL_Point farCorner = portraitPixel(pCellCornerX(kBoardWidth - 1),
                                            pCellCornerY(kBoardHeight - 1));
  SC_CHECK(farCorner.x == 880 && farCorner.y == 111);
  SC_CHECK(sameColor(pixelAt(canvas, farCorner.x, farCorner.y), kCellEdge));
  SC_CHECK(sameColor(pixelAt(canvas, farCorner.x + 20, farCorner.y - 20),
                     pieceFill(PieceId::O)));
}

// The portrait HUD strip above the board and the screens of section 4.7, all
// through the same rotation: text bands assert in device space, so a glyph
// row that drifts out of the strip fails here.
void run_portrait_hud_and_screens_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);
  renderer.setOrientation(Orientation::Portrait);

  Game game;
  game.start(20261103);
  playRounds(game, 2);
  SC_CHECK(game.scene() == Scene::Playing);
  renderer.draw(game);

  // The three statistics groups: a dim label row and a bright value row per
  // group, each inside its 128 px field of the strip, above the board frame.
  for (int index = 0; index < 3; ++index) {
    const int centerX = kPortraitStatsCenterX[index];
    const SDL_Rect label = portraitDeviceRect(
        centerX - kPortraitStatsFieldWidth / 2, kPortraitStatsLabelY,
        kPortraitStatsFieldWidth, kGlyphRows * kLabelScale);
    const SDL_Rect value = portraitDeviceRect(
        centerX - kPortraitStatsFieldWidth / 2, kPortraitStatsValueY,
        kPortraitStatsFieldWidth, kGlyphRows * kValueScale);
    SC_CHECK(countColor(canvas, label, kTextDim) > 40);
    SC_CHECK(countColor(canvas, value, kText) > 20);
  }

  // The Hold and Next groups share one row of the strip: their dim headings
  // sit above their boxes, and the preview queue draws one box per preview
  // at the 12 px cell.
  const SDL_Rect holdHeading = portraitDeviceRect(
      kPortraitHoldCenterX - 40, kPortraitPanelHeadingY, 80,
      kGlyphRows * kLabelScale);
  SC_CHECK(countColor(canvas, holdHeading, kTextDim) > 20);
  for (int index = 0; index < kPreviewCount; ++index) {
    const SDL_Rect slot = portraitNextSlotBox(index);
    const SDL_Rect deviceSlot =
        portraitDeviceRect(slot.x, slot.y, slot.w, slot.h);
    // Every slot but the last holds queue pieces; the fifth is the fourth
    // preview drawn dim. The first draws at full color.
    if (index == 0) {
      SC_CHECK(countColor(canvas, deviceSlot, kCellEdge) > 0);
    }
  }
  const SDL_Rect nextHeading = portraitDeviceRect(
      kPortraitNextCenterX - 40, kPortraitPanelHeadingY, 80,
      kGlyphRows * kLabelScale);
  SC_CHECK(countColor(canvas, nextHeading, kTextDim) > 20);

  // The whole strip sits above the board frame and inside the canvas: no
  // label, value, or box of the strip reaches the frame's portrait row 119.
  SC_CHECK(kPortraitPanelBoxY + kPortraitPreviewBoxHeight <=
           portraitBoardFrame().y);
  SC_CHECK(kPortraitStatsValueY + kGlyphRows * kValueScale <=
           portraitBoardFrame().y);

  // The title page: logo, preview row, four-item menu, the high score, and
  // the hint, each inside its band of the tall canvas.
  Game title;
  title.openTitle(20261103);
  renderer.draw(title);
  const SDL_Rect logo = portraitDeviceRect(
      0, kPTitleLogoY, kPortraitCanvasWidth, kGlyphRows * kTitleLogoScale);
  SC_CHECK(countColor(canvas, logo, kText) > 400);
  const SDL_Rect menu = portraitDeviceRect(
      0, kPTitleMenuY, kPortraitCanvasWidth,
      kTitleItemCount * kMenuItemHeight);
  SC_CHECK(countColor(canvas, menu, kText) > 100);
  SC_CHECK(countColor(canvas, menu, kAccent) > 20);
  const SDL_Rect highScore = portraitDeviceRect(
      0, kPTitleHighScoreLabelY, kPortraitCanvasWidth,
      kPTitleHighScoreValueY + kGlyphRows * kValueScale -
          kPTitleHighScoreLabelY);
  SC_CHECK(countColor(canvas, highScore, kText) > 30);
  const SDL_Rect hint = portraitDeviceRect(
      0, kPTitleHintY, kPortraitCanvasWidth, kGlyphRows * kHintScale);
  SC_CHECK(countColor(canvas, hint, kTextDim) > 30);
  SC_CHECK(kPTitleHintY + kGlyphRows * kHintScale < kPortraitCanvasHeight);

  // The options page: seven rows including ORIENTATION, the shared menu
  // metrics, heading, and footer, all inside the tall canvas.
  Game options;
  options.openTitle(20261103);
  renderer.setOrientationPreference(Orientation::Portrait);
  menuDown(options);
  menuDown(options);
  TickInput select;
  select.menu.selectPressed = true;
  options.tick(select);
  SC_CHECK(options.scene() == Scene::Options);
  renderer.draw(options);
  const SDL_Rect rows = portraitDeviceRect(
      0, kPOptionsMenuY, kPortraitCanvasWidth,
      kOptionsItemCount * kMenuItemHeight);
  SC_CHECK(countColor(canvas, rows, kText) > 100);
  SC_CHECK(countColor(canvas, rows, kTextDim) > 100);
  SC_CHECK(kPOptionsMenuY + kOptionsItemCount * kMenuItemHeight <
           kPOptionsFooterY);
  const SDL_Rect footer = portraitDeviceRect(
      0, kPOptionsFooterY, kPortraitCanvasWidth, kGlyphRows * kHintScale);
  SC_CHECK(countColor(canvas, footer, kTextDim) > 30);
  SC_CHECK(kPOptionsFooterY + kGlyphRows * kHintScale <
           kPortraitCanvasHeight);
  renderer.setOrientationPreference(Orientation::Landscape);

  // The controls page: one heading, its two mapping columns, ten rows, and
  // the footer inside the tall canvas.
  Game controls;
  controls.openTitle(20261103);
  menuDown(controls);
  TickInput openControls;
  openControls.menu.selectPressed = true;
  controls.tick(openControls);
  SC_CHECK(controls.scene() == Scene::Controls);
  renderer.draw(controls);
  const SDL_Rect controlRows = portraitDeviceRect(
      0, kPControlsFirstRowY, kPortraitCanvasWidth,
      kControlRowCount * kPControlsRowHeight);
  SC_CHECK(countColor(canvas, controlRows, kText) > 200);
  SC_CHECK(countColor(canvas, controlRows, kTextDim) > 200);
  const SDL_Rect controlsHeading = portraitDeviceRect(
      0, kPControlsHeadingY, kPortraitCanvasWidth,
      kGlyphRows * kControlsHeadingScale);
  SC_CHECK(countColor(canvas, controlsHeading, kText) > 100);
  const SDL_Rect controlsFooter = portraitDeviceRect(
      0, kPControlsFooterY, kPortraitCanvasWidth, kGlyphRows * kLabelScale);
  SC_CHECK(countColor(canvas, controlsFooter, kText) > 30);
  SC_CHECK(kPControlsFooterY + kGlyphRows * kHintScale <
           kPortraitCanvasHeight);

  // The pause overlay centers its panel on the board, which owns the canvas
  // alone in portrait; the game-over panel centers on the canvas.
  game.tick(TickInput{});
  TickInput pause;
  pause.game.pausePressed = true;
  game.tick(pause);
  game.tick(TickInput{});
  SC_CHECK(game.scene() == Scene::Paused);
  renderer.draw(game);
  const SDL_Rect pauseBox =
      portraitDeviceRect(72, 280, 400, 360);
  SC_CHECK(countColor(canvas, pauseBox, kPanelFill) > 10000);
  const SDL_Point pauseCenter = portraitPixel(272, 460);
  SC_CHECK(sameColor(pixelAt(canvas, pauseCenter.x, pauseCenter.y),
                     kPanelFill));

  Game over;
  over.openTitle(20261103);
  over.tick(select);
  over.tick(TickInput{});
  forceGameOver(over);
  SC_CHECK(over.scene() == Scene::GameOver);
  renderer.draw(over);
  const SDL_Rect overBox = portraitDeviceRect(32, 200, 480, 440);
  SC_CHECK(countColor(canvas, overBox, kPanelFill) > 20000);
}

// Portrait leaves the landscape pixels exactly as they were: switching there
// and back reproduces the same canvas bytes, because landscape keeps its own
// constants and its own code path (task T055 acceptance).
void run_portrait_landscape_invariance_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);

  Game game;
  game.start(20261103);
  playRounds(game, 3);
  game.tick(TickInput{});
  TickInput pause;
  pause.game.pausePressed = true;
  game.tick(pause);
  game.tick(TickInput{});

  renderer.draw(game);
  std::vector<Uint32> before(
      static_cast<std::size_t>(canvas.surface->w) * canvas.surface->h);
  SDL_LockSurface(canvas.surface);
  std::memcpy(before.data(), canvas.surface->pixels,
              before.size() * sizeof(Uint32));
  SDL_UnlockSurface(canvas.surface);

  renderer.setOrientation(Orientation::Portrait);
  renderer.draw(game);
  renderer.setOrientation(Orientation::Landscape);
  renderer.draw(game);
  SDL_LockSurface(canvas.surface);
  const bool identical =
      std::memcmp(before.data(), canvas.surface->pixels,
                  before.size() * sizeof(Uint32)) == 0;
  SDL_UnlockSurface(canvas.surface);
  SC_CHECK(identical);
}

}  // namespace

int main() {
  if (SDL_Init(0) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  run_board_and_grid_checks();
  run_board_boundary_checks();
  run_cells_and_ghost_checks();
  run_next_and_hold_checks();
  run_grayscale_checks();
  run_preview_checks();
  run_text_checks();
  run_focused_hud_checks();
  run_read_only_and_scaling_checks();
  run_letterbox_canvas_checks();
  run_title_screen_checks();
  run_controls_screen_checks();
  run_options_screen_checks();
  run_music_options_screen_checks();
  run_pause_overlay_checks();
  run_gameover_overlay_checks();
  run_portrait_board_checks();
  run_portrait_hud_and_screens_checks();
  run_portrait_landscape_invariance_checks();

  SDL_Quit();
  stackfall_test::print_summary();
  return stackfall_test::g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
