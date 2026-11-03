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
//   * the control hints and the footer hint lines draw at the label glyph
//     height and no hint text leaves the left panel;
//   * draw() changes no byte of the game state (specification section 7);
//   * the same logical frame scales and letterboxes onto larger canvases;
//   * a window that is not the canvas letterboxes with black slack bars that
//     split evenly and a capture that reads the whole output, while the
//     native canvas keeps its exact pixels (task T032).
//
// Coverage of the task T011 screens (section 4.7):
//   * the title page draws its logo, preview strip, three-item menu with an
//     accent marker on the selected item, and the high score;
//   * the controls page draws its three columns and follows the layout
//     constants;
//   * the pause overlay veils the frozen round, draws its panel exactly, and
//     navigation under it leaves the round byte-identical;
//   * the game-over overlay draws the final values, the accent record note
//     exactly when the round set one, and its two-item menu.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
constexpr int kHintRowCount = 7;

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
  renderer.setShowGrid(false);
  SC_CHECK(renderer.showGrid() == false);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX + kBoardCellSize,
                             kBoardOriginY + 7),
                     kBoardWell));
  SC_CHECK(sameColor(pixelAt(canvas, kBoardOriginX + 9 * kBoardCellSize, 400),
                     kBoardWell));
  renderer.setShowGrid(true);
  SC_CHECK(renderer.showGrid() == true);
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

  // A freshly spawned piece can sit entirely in the hidden rows (the I piece
  // does until its first fall). Soft-drop it into view so the check covers
  // drawn cells; both games tick in step.
  InputFrame bringIntoView = InputFrame::idle();
  bringIntoView.downHeld = true;
  for (int i = 0; i < kBoardHeight + 4 && visibleActiveCells(subject) == 0;
       ++i) {
    subject.tick(bringIntoView);
    reference.tick(bringIntoView);
  }
  SC_CHECK(visibleActiveCells(subject) > 0);

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
}

void run_preview_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);

  Game game;
  game.start(4242);
  renderer.draw(game);

  // An unused hold slot shows no piece color at all.
  SC_CHECK(game.hasHoldPiece() == false);
  const SDL_Rect hold = holdBox();
  for (const SDL_Color& fill : kPieceFills) {
    SC_CHECK(countColor(canvas, hold, fill) == 0);
  }

  // The five next previews draw the queued pieces in order at the preview
  // cell size, and the gap between two slots stays empty background.
  const auto& queue = game.queue();
  for (int index = 0; index < kPreviewCount; ++index) {
    const SDL_Rect slot = nextSlotBox(index);
    for (const Cell& cell : pieceCells(queue[index], Rotation::Spawn)) {
      const SDL_Point center = previewCellCenter(queue[index], slot, cell);
      SC_CHECK(sameColor(pixelAt(canvas, center.x, center.y),
                         pieceFill(queue[index])));
    }
  }
  const SDL_Rect panelPadding{nextPanelBox().x,
                              nextPanelBox().y + nextPanelBox().h - 5,
                              nextPanelBox().w, 5};
  SC_CHECK(areaIsOnly(canvas, panelPadding, kBackground));

  // One hold stores the piece; the slot is spent, so it draws dimmed.
  InputFrame holdPress = InputFrame::idle();
  holdPress.holdPressed = true;
  game.tick(holdPress);
  SC_CHECK(game.hasHoldPiece());
  SC_CHECK(game.holdLocked());
  renderer.draw(game);
  for (const Cell& cell : pieceCells(game.holdPieceId(), Rotation::Spawn)) {
    const SDL_Point center = previewCellCenter(game.holdPieceId(), hold, cell);
    SC_CHECK(sameColor(pixelAt(canvas, center.x, center.y),
                       dimCellPalette(game.holdPieceId()).fill));
  }

  // A lock re-enables hold, and the stored piece draws at full color.
  InputFrame drop = InputFrame::idle();
  drop.hardDropPressed = true;
  game.tick(drop);
  SC_CHECK(game.holdLocked() == false);
  renderer.draw(game);
  for (const Cell& cell : pieceCells(game.holdPieceId(), Rotation::Spawn)) {
    const SDL_Point center = previewCellCenter(game.holdPieceId(), hold, cell);
    SC_CHECK(sameColor(pixelAt(canvas, center.x, center.y),
                       pieceFill(game.holdPieceId())));
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

  // A whole play frame carries the score fields and the control hints.
  Game game;
  game.start(4242);
  playRounds(game, 4);
  renderer.draw(game);
  const int scoreAreaY = kPanelBoxY + kNextPanelHeight + 8;
  const SDL_Rect scoreArea{kRightPanelX, scoreAreaY, kRightPanelWidth,
                           kCanvasHeight - scoreAreaY};
  SC_CHECK(countColor(canvas, scoreArea, kText) > 50);     // values
  SC_CHECK(countColor(canvas, scoreArea, kTextDim) > 50);  // labels
  const SDL_Rect hintArea{kLeftPanelX, holdBox().y + holdBox().h + 24,
                          kLeftPanelWidth, 160};
  SC_CHECK(countColor(canvas, hintArea, kTextDim) > 50);
  SC_CHECK(countColor(canvas, hintArea, kText) > 20);

  // Every hint line measures the full label height, and no hint text leaves
  // the left panel for the board (design review finding
  // control-hint-glyph-scale).
  const int hintBandY = holdBox().y + holdBox().h + 20;
  const SDL_Rect hintBand{kLeftPanelX, hintBandY, kLeftPanelWidth,
                          kCanvasHeight - hintBandY};
  const std::vector<int> hintRuns = textRowRuns(canvas, hintBand);
  SC_CHECK(hintRuns.size() == kHintRowCount + 1);  // the heading plus the rows
  for (const int height : hintRuns) {
    SC_CHECK(height == kGlyphRows * kHintScale);
  }
  const SDL_Rect pastPanelEdge{
      kLeftPanelX + kLeftPanelWidth, hintBandY,
      kBoardOriginX - 1 - (kLeftPanelX + kLeftPanelWidth),
      kCanvasHeight - hintBandY};
  SC_CHECK(countColor(canvas, pastPanelEdge, kText) == 0);
  SC_CHECK(countColor(canvas, pastPanelEdge, kTextDim) == 0);
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
void forceGameOver(Game& game) {
  Board& board = stackfall::GameTestPeer::board(game);
  const std::array<Cell, kCellsPerPiece> cells =
      boardCells(game.activePiece());
  for (int y = -kHiddenRows; y <= 0; ++y) {
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
  const char* items[] = {"START GAME", "CONTROLS", "QUIT"};
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

  // The panel is an opaque card: its corner holds the exact panel fill.
  const SDL_Rect panel = pausePanel();
  SC_CHECK(sameColor(pixelAt(canvas, panel.x + 6, panel.y + 6), kPanelFill));

  // The heading, the three items, and the marker follow the layout constants.
  const SDL_Rect heading{panel.x, panel.y + kPauseHeadingOffsetY, panel.w,
                         kGlyphRows * kControlsHeadingScale};
  SC_CHECK(countColor(canvas, heading, kText) > 400);
  const char* items[] = {"RESUME", "RESTART", "QUIT TO TITLE"};
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

  const SDL_Rect panel = gameoverPanel();
  SC_CHECK(sameColor(pixelAt(canvas, panel.x + 6, panel.y + 6), kPanelFill));

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

}  // namespace

int main() {
  if (SDL_Init(0) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  run_board_and_grid_checks();
  run_board_boundary_checks();
  run_cells_and_ghost_checks();
  run_preview_checks();
  run_text_checks();
  run_read_only_and_scaling_checks();
  run_letterbox_canvas_checks();
  run_title_screen_checks();
  run_controls_screen_checks();
  run_pause_overlay_checks();
  run_gameover_overlay_checks();

  SDL_Quit();
  stackfall_test::print_summary();
  return stackfall_test::g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
