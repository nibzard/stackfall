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
constexpr int kControlRowCount = 11;

// The suites that pin the board, the HUD, and the letterbox read them on the
// flat legacy ground of background mode Off, so their "pure background"
// expectations stay about the scene partners alone. Since task T079 the
// level 1 play scene carries the Still ground design instead, and the
// wallpaper itself - its pixels, its palette caps, and its margins - is
// pinned by run_background_checks() at the bottom of this file.
void useFlatPlayGround(Renderer& renderer) {
  renderer.setBackgroundMode(backgrounds::Mode::Off);
}

// Advances the wallpaper clock by `ms` in clock-sized chunks: one advance is
// clamped to 250 ms, so a whole fade needs several calls to run out.
void advanceBackgroundBy(Renderer& renderer, std::uint32_t ms) {
  while (ms > 0) {
    const std::uint32_t chunk = ms > 250 ? 250 : ms;
    renderer.advanceBackgroundClock(chunk);
    ms -= chunk;
  }
}

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
  useFlatPlayGround(renderer);

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

// ---------------------------------------------------------------------------
// The per-piece in-cell designs of task T095: the geometry contract of
// cellDesign() at every supported cell size, the pixels of the board cells in
// both orientations and both palettes, the preview cells at their two smaller
// sizes, and the off default that keeps the plain cell exactly.
// ---------------------------------------------------------------------------

// The design mask of `id` in a size x size cell: one flag per pixel, true
// where a mark rectangle covers it.
using DesignMask = std::vector<std::vector<bool>>;

DesignMask designMask(PieceId id, int size) {
  DesignMask mask(size, std::vector<bool>(size, false));
  const CellDesign design = cellDesign(id, size);
  for (int index = 0; index < design.count; ++index) {
    const SDL_Rect& mark = design.rects[index];
    for (int y = mark.y; y < mark.y + mark.h; ++y) {
      for (int x = mark.x; x < mark.x + mark.w; ++x) {
        if (x >= 0 && y >= 0 && x < size && y < size) {
          mask[y][x] = true;
        }
      }
    }
  }
  return mask;
}

// Reads the canvas pixel of portrait (px, py): the portrait pixel (x, y)
// reaches the device canvas at (y, 543 - x), the rotation the portrait suites
// pin, so a check written in portrait coordinates reads the same cell the
// renderer drew.
SDL_Color portraitPixelAt(const Canvas& canvas, int px, int py) {
  return pixelAt(canvas, py, kCanvasHeight - 1 - px);
}

// The interior of a drawn cell holds exactly the two shades a design may use:
// the palette fill where no mark sits and the palette dark shade where one
// does. `readPixel` hands over the pixel of interior offset (ix, iy) of the
// cell whose top-left corner is (x0, y0), in whatever orientation the caller
// drew. Every interior pixel is pinned, so a mark that moved, shrank, or
// vanished fails here, in the color and the grayscale palette alike.
void checkCellInterior(const Canvas& canvas, int x0, int y0, int size,
                       PieceId id, const CellPalette& palette, bool designsOn,
                       const char* what) {
  const DesignMask mask = designMask(id, size);
  for (int iy = 2; iy < size - 2; ++iy) {
    for (int ix = 2; ix < size - 2; ++ix) {
      const SDL_Color got = pixelAt(canvas, x0 + ix, y0 + iy);
      const bool marked = designsOn && mask[iy][ix];
      if (!sameColor(got, marked ? palette.dark : palette.fill)) {
        std::fprintf(stderr,
                     "  context: %s, interior (%d, %d) is r=%u g=%u b=%u, "
                     "wanted the %s shade\n",
                     what, ix, iy, static_cast<unsigned>(got.r),
                     static_cast<unsigned>(got.g),
                     static_cast<unsigned>(got.b),
                     marked ? "dark" : "fill");
        SC_CHECK(false);
        return;
      }
    }
  }
  SC_CHECK(true);
}

void run_piece_design_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);

  // The geometry contract at every supported cell size - the 12 px portrait
  // preview, the 15 px landscape preview, the 27 px landscape cell, and the
  // 40 px portrait cell - plus the small sizes no scene draws but a future
  // caller might: every mark rectangle sits inside the interior the cell
  // frame leaves, every supported size gives every piece a mark, and the
  // seven masks are pairwise distinct, so no two types share a look at any
  // size. Below an 8 px cell there is no interior a mark fits in and the
  // design is empty, which draws the plain cell rather than an unpredictable
  // one.
  for (const int size : {12, 15, 27, 40, 9, 8}) {
    std::vector<DesignMask> masks;
    int empty = 0;
    for (int index = 0; index < kPieceCount; ++index) {
      const PieceId id = kAllPieceIds[index];
      const CellDesign design = cellDesign(id, size);
      int area = 0;
      for (int mark = 0; mark < design.count; ++mark) {
        const SDL_Rect& rect = design.rects[mark];
        SC_CHECK(rect.w > 0);
        SC_CHECK(rect.h > 0);
        SC_CHECK(rect.x >= 2);
        SC_CHECK(rect.y >= 2);
        SC_CHECK(rect.x + rect.w <= size - 2);
        SC_CHECK(rect.y + rect.h <= size - 2);
        area += rect.w * rect.h;
      }
      if (design.count == 0) {
        ++empty;
      }
      SC_CHECK(area <= (size - 4) * (size - 4));
      masks.push_back(designMask(id, size));
    }
    if (size >= 12) {
      SC_CHECK(empty == 0);  // every type carries a mark at a real cell
    }
    for (int a = 0; a < kPieceCount; ++a) {
      for (int b = a + 1; b < kPieceCount; ++b) {
        if (size >= 12) {
          SC_CHECK(masks[a] != masks[b]);  // the distinctness of the table
        }
      }
    }
  }
  for (const int size : {7, 6, 5}) {
    for (int index = 0; index < kPieceCount; ++index) {
      SC_CHECK(cellDesign(kAllPieceIds[index], size).count == 0);
    }
  }

  // One settled cell of every type on the bottom row, so one frame draws all
  // seven marks at once. The ghost is off for this check because its outline
  // may cross the same cells; the ghost carries no fill and no mark.
  Game game;
  game.start(11);
  SC_CHECK(game.scene() == Scene::Playing);
  Board& board = GameTestPeer::board(game);
  for (int index = 0; index < kPieceCount; ++index) {
    SC_CHECK(board.settleCell(index, 19, kAllPieceIds[index]));
  }
  SC_CHECK(game.board().settledCellCount() == kPieceCount);
  SC_CHECK(!game.board().rowIsFull(19));
  renderer.setShowGhost(false);

  // Landscape, the 27 px cell, in both palettes: every interior pixel is the
  // fill or the dark shade exactly where the design mask says, and the off
  // default draws no dark pixel inside the interior at all - the plain cell
  // of the years before the task, pixel for pixel.
  for (const bool grayscale : {false, true}) {
    renderer.setGrayscale(grayscale);
    renderer.setPieceDesigns(false);
    renderer.draw(game);
    for (int index = 0; index < kPieceCount; ++index) {
      const PieceId id = kAllPieceIds[index];
      const CellPalette& palette =
          grayscale ? grayscalePalette(id) : cellPalette(id);
      checkCellInterior(canvas, kBoardOriginX + index * kBoardCellSize,
                        kBoardOriginY + 19 * kBoardCellSize, kBoardCellSize,
                        id, palette, false,
                        grayscale ? "landscape gray off" : "landscape off");
    }
    renderer.setPieceDesigns(true);
    SC_CHECK(renderer.pieceDesigns() == true);
    renderer.draw(game);
    for (int index = 0; index < kPieceCount; ++index) {
      const PieceId id = kAllPieceIds[index];
      const CellPalette& palette =
          grayscale ? grayscalePalette(id) : cellPalette(id);
      checkCellInterior(canvas, kBoardOriginX + index * kBoardCellSize,
                        kBoardOriginY + 19 * kBoardCellSize, kBoardCellSize,
                        id, palette, true,
                        grayscale ? "landscape gray on" : "landscape on");
      // The mark is visible: at least six interior pixels of the dark shade,
      // so the cue survives the smallest supported preview cell too.
      const DesignMask mask = designMask(id, kBoardCellSize);
      int marked = 0;
      for (const std::vector<bool>& row : mask) {
        for (bool flag : row) {
          marked += flag ? 1 : 0;
        }
      }
      SC_CHECK(marked >= 6);
    }
  }

  // Portrait, the 40 px cell, through the rotation: the same interior rule in
  // portrait coordinates, which proves the mark travels with the cell and not
  // with the canvas.
  renderer.setOrientation(Orientation::Portrait);
  renderer.setGrayscale(true);
  renderer.setPieceDesigns(true);
  renderer.draw(game);
  for (int index = 0; index < kPieceCount; ++index) {
    const PieceId id = kAllPieceIds[index];
    const CellPalette& palette = grayscalePalette(id);
    const int px = kPortraitBoardOriginX + index * kPortraitBoardCellSize;
    const int py = kPortraitBoardOriginY + 19 * kPortraitBoardCellSize;
    const DesignMask mask = designMask(id, kPortraitBoardCellSize);
    for (int iy = 2; iy < kPortraitBoardCellSize - 2; ++iy) {
      for (int ix = 2; ix < kPortraitBoardCellSize - 2; ++ix) {
        const SDL_Color got = portraitPixelAt(canvas, px + ix, py + iy);
        const SDL_Color want = mask[iy][ix] ? palette.dark : palette.fill;
        if (!sameColor(got, want)) {
          SC_CHECK(false);
          return;
        }
      }
    }
  }
  SC_CHECK(true);

  // The preview cells at their own smaller sizes: the first Next preview in
  // landscape (15 px) and the Hold box in portrait (12 px, the smallest
  // supported cell) carry their piece's mark, so the queue reads by pattern
  // beside the board. The check recomputes the preview origin from the
  // section 6.1 rule, as previewCellCenter does for the landscape cell.
  renderer.setOrientation(Orientation::Landscape);
  renderer.setGrayscale(false);
  renderer.setShowGhost(true);
  stackfall::GameTestPeer::setHoldSlot(game, PieceId::Z);
  stackfall::GameTestPeer::setHoldLocked(game, false);
  renderer.draw(game);
  {
    const PieceId next = game.queue()[0];
    const CellPalette& palette = cellPalette(next);
    for (const Cell& cell : pieceCells(next, Rotation::Spawn)) {
      const SDL_Point center =
          previewCellCenter(next, nextSlotBox(0), cell);
      const int x0 = center.x - kPreviewCellSize / 2;
      const int y0 = center.y - kPreviewCellSize / 2;
      checkCellInterior(canvas, x0, y0, kPreviewCellSize, next, palette, true,
                        "landscape next preview");
    }
    const PieceId held = game.holdPieceId();
    for (const Cell& cell : pieceCells(held, Rotation::Spawn)) {
      const SDL_Point center = previewCellCenter(held, holdBox(), cell);
      const int x0 = center.x - kPreviewCellSize / 2;
      const int y0 = center.y - kPreviewCellSize / 2;
      checkCellInterior(canvas, x0, y0, kPreviewCellSize, held,
                        cellPalette(held), true, "landscape hold preview");
    }
  }
  renderer.setOrientation(Orientation::Portrait);
  renderer.draw(game);
  {
    const PieceId held = game.holdPieceId();
    const CellPalette& palette = cellPalette(held);
    const std::array<Cell, kCellsPerPiece>& cells =
        pieceCells(held, Rotation::Spawn);
    int minX = 4;
    int maxX = 0;
    int minY = 4;
    int maxY = 0;
    for (const Cell& cell : cells) {
      minX = cell.x < minX ? cell.x : minX;
      maxX = cell.x > maxX ? cell.x : maxX;
      minY = cell.y < minY ? cell.y : minY;
      maxY = cell.y > maxY ? cell.y : maxY;
    }
    const SDL_Rect box = portraitHoldBox();
    const int cellSize = kPortraitPreviewCellSize;
    const int originX =
        box.x + (box.w - (maxX - minX + 1) * cellSize) / 2 - minX * cellSize;
    const int originY =
        box.y + (box.h - (maxY - minY + 1) * cellSize) / 2 - minY * cellSize;
    const DesignMask mask = designMask(held, cellSize);
    for (const Cell& cell : cells) {
      const int px = originX + cell.x * cellSize;
      const int py = originY + cell.y * cellSize;
      for (int iy = 2; iy < cellSize - 2; ++iy) {
        for (int ix = 2; ix < cellSize - 2; ++ix) {
          const SDL_Color got = portraitPixelAt(canvas, px + ix, py + iy);
          const SDL_Color want = mask[iy][ix] ? palette.dark : palette.fill;
          if (!sameColor(got, want)) {
            SC_CHECK(false);
            return;
          }
        }
      }
    }
    SC_CHECK(true);
  }

  // The ghost keeps its hollow outline under the designs: no fill, no mark,
  // so the landing preview reads as the shape it always was.
  renderer.setOrientation(Orientation::Landscape);
  renderer.setGrayscale(false);
  renderer.setPieceDesigns(true);
  renderer.draw(game);
  {
    const Piece ghost = game.ghostPiece();
    SC_CHECK(ghost.y > game.activePiece().y);
    for (const Cell& cell : boardCells(ghost)) {
      if (cell.y < 0 || cell.y >= kBoardHeight) {
        continue;
      }
      const SDL_Color fill = pieceFill(game.activePiece().id);
      const SDL_Color dark = cellPalette(game.activePiece().id).dark;
      SC_CHECK(countColor(canvas,
                          SDL_Rect{cellCornerX(cell.x) + 2,
                                   cellCornerY(cell.y) + 2,
                                   kBoardCellSize - 4, kBoardCellSize - 4},
                          fill) == 0);
      SC_CHECK(countColor(canvas,
                          SDL_Rect{cellCornerX(cell.x) + 2,
                                   cellCornerY(cell.y) + 2,
                                   kBoardCellSize - 4, kBoardCellSize - 4},
                          dark) == 0);
    }
  }

  renderer.setPieceDesigns(false);
  renderer.setGrayscale(false);
  SC_CHECK(renderer.pieceDesigns() == false);
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
  useFlatPlayGround(renderer);

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
  useFlatPlayGround(renderer);

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
  useFlatPlayGround(scaledRenderer);
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
  useFlatPlayGround(letterboxedRenderer);
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
    useFlatPlayGround(renderer);
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

  // The hard-drop row names D-pad up in the gamepad column and Space in the
  // keyboard column (task T067); X appears nowhere on the page. Since task
  // T066 the page draws one row per action, so hard drop sits at index 5 and
  // reads from the binding tables the renderer mirrors. The exact glyph count
  // of each cell pins the strings the way the options rows pin theirs, so
  // reverting only the table fails all three.
  const int dropRowY = kControlsFirstRowY + 5 * kControlsRowHeight;
  const SDL_Rect dropGamepad{kControlsGamepadX, dropRowY,
                             kControlsKeyboardX - kControlsGamepadX,
                             kGlyphRows * kLabelScale};
  const SDL_Rect dropKeyboard{kControlsKeyboardX, dropRowY,
                              kControlsActionX - kControlsKeyboardX,
                              kGlyphRows * kLabelScale};
  const SDL_Rect dropAction{kControlsActionX, dropRowY,
                            kCanvasWidth - kControlsActionX,
                            kGlyphRows * kLabelScale};
  SC_CHECK(countColor(canvas, dropGamepad, kText) ==
           glyphPixelCount("D-PAD UP") * kLabelScale * kLabelScale);
  SC_CHECK(countColor(canvas, dropKeyboard, kText) ==
           glyphPixelCount("SPACE") * kLabelScale * kLabelScale);
  SC_CHECK(countColor(canvas, dropAction, kText) ==
           glyphPixelCount("HARD DROP") * kLabelScale * kLabelScale);

  // The footer invites the way back, and B takes it.
  const SDL_Rect footer = menuRow(kControlsFooterY, kLabelScale);
  SC_CHECK(countColor(canvas, footer, kText) > 100);
  TickInput back;
  back.menu.backPressed = true;
  game.tick(back);
  SC_CHECK(game.scene() == Scene::Title);
}

// The customization page of task T066, opened from the mappings page: the
// heading, the eight action rows reading the binding tables the renderer
// mirrors, the RESTORE DEFAULTS row, the accent capture row, the status
// line, and the footer that names the capture's rule. The exact glyph count
// of each cell pins the strings, so a reverted table or a lost mirror fails
// every pin it touches; the portrait half of the same page draws at the
// portrait metrics of tasks T055 and T068.
void run_rebind_screen_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);
  Game game;
  game.openTitle(7);
  menuDown(game);  // CONTROLS
  TickInput select;
  select.menu.selectPressed = true;
  game.tick(select);
  SC_CHECK(game.scene() == Scene::Controls);
  game.tick(select);  // A CUSTOMIZE opens the customization page
  SC_CHECK(game.scene() == Scene::Rebind);
  SC_CHECK(game.rebindSelection() == 0);
  SC_CHECK(kRebindItemCount == 9);
  SC_CHECK(kRebindRestoreRow == 8);
  renderer.draw(game);

  // The heading and the three column headers.
  SC_CHECK(sameColor(pixelAt(canvas, 5, 5), kBackground));
  const SDL_Rect heading{0, kControlsHeadingY, kCanvasWidth,
                         kGlyphRows * kControlsHeadingScale};
  SC_CHECK(countColor(canvas, heading, kText) ==
           glyphPixelCount("SET CONTROLS") * kControlsHeadingScale *
               kControlsHeadingScale);
  const SDL_Rect header{kControlsGamepadX, kControlsHeaderY,
                        kCanvasWidth - kControlsGamepadX,
                        kGlyphRows * kLabelScale};
  SC_CHECK(countColor(canvas, header, kTextDim) ==
           (glyphPixelCount("GAMEPAD") + glyphPixelCount("KEYBOARD") +
            glyphPixelCount("ACTION")) *
               kLabelScale * kLabelScale);

  // The eight rows read the mirrored tables. Row by row, each cell holds
  // exactly its string's glyphs: the pairs still read "Y OR L1" and
  // "C OR LEFT SHIFT", and an action with one control reads it alone.
  const char* padRows[kRebindableActionCount] = {
      "D-PAD LEFT", "D-PAD RIGHT", "D-PAD DOWN", "A", "B", "D-PAD UP",
      "Y OR L1", "START"};
  const char* keyRows[kRebindableActionCount] = {
      "LEFT ARROW", "RIGHT ARROW", "DOWN ARROW", "X OR UP ARROW", "Z",
      "SPACE", "C OR LEFT SHIFT", "ENTER OR ESC"};
  const char* actionRows[kRebindableActionCount] = {
      "MOVE LEFT", "MOVE RIGHT", "SOFT DROP", "ROTATE CW", "ROTATE CCW",
      "HARD DROP", "HOLD", "PAUSE OR RESUME"};
  for (int row = 0; row < kRebindableActionCount; ++row) {
    const int y = kRebindFirstRowY + row * kRebindRowHeight;
    const SDL_Rect padCell{kControlsGamepadX, y,
                           kControlsKeyboardX - kControlsGamepadX,
                           kGlyphRows * kLabelScale};
    const SDL_Rect keyCell{kControlsKeyboardX, y,
                           kControlsActionX - kControlsKeyboardX,
                           kGlyphRows * kLabelScale};
    const SDL_Rect actionCell{kControlsActionX, y,
                              kCanvasWidth - kControlsActionX,
                              kGlyphRows * kLabelScale};
    SC_CHECK(countColor(canvas, padCell, kText) ==
             glyphPixelCount(padRows[row]) * kLabelScale * kLabelScale);
    SC_CHECK(countColor(canvas, keyCell, kText) ==
             glyphPixelCount(keyRows[row]) * kLabelScale * kLabelScale);
    // The selected row's action label carries the accent; every other row
    // reads bright.
    if (row == game.rebindSelection()) {
      SC_CHECK(countColor(canvas, actionCell, kAccent) ==
               glyphPixelCount(actionRows[row]) * kLabelScale * kLabelScale);
      SC_CHECK(countColor(canvas, actionCell, kText) == 0);
    } else {
      SC_CHECK(countColor(canvas, actionCell, kText) ==
               glyphPixelCount(actionRows[row]) * kLabelScale * kLabelScale);
    }
  }

  // The marker sits left of the selected row's gamepad cell.
  {
    const int y = kRebindFirstRowY;
    SC_CHECK(areaIsOnly(canvas,
                        SDL_Rect{kControlsGamepadX - kMenuMarkerGap -
                                     kMenuMarkerSize,
                                 y + (kGlyphRows * kLabelScale - kMenuMarkerSize) /
                                         2,
                                 kMenuMarkerSize, kMenuMarkerSize},
                        kAccent));
  }

  // RESTORE DEFAULTS sits on its own row below the eight, bright while the
  // cursor is elsewhere; eight downs move the cursor onto it, turn it
  // accent, and draw the marker beside it.
  {
    const SDL_Rect restoreRow{kControlsGamepadX, kRebindRestoreY,
                              kCanvasWidth - kControlsGamepadX,
                              kGlyphRows * kLabelScale};
    SC_CHECK(countColor(canvas, restoreRow, kText) ==
             glyphPixelCount("RESTORE DEFAULTS") * kLabelScale * kLabelScale);
    for (int step = 0; step < kRebindRestoreRow; ++step) {
      menuDown(game);
    }
    SC_CHECK(game.rebindSelection() == kRebindRestoreRow);
    renderer.draw(game);
    SC_CHECK(countColor(canvas, restoreRow, kAccent) ==
             glyphPixelCount("RESTORE DEFAULTS") * kLabelScale * kLabelScale);
    SC_CHECK(countColor(canvas, restoreRow, kText) == 0);
  }
  // The cursor wraps within the nine rows, so the ninth down returns to MOVE
  // LEFT and the restore row goes bright again.
  menuDown(game);
  SC_CHECK(game.rebindSelection() == 0);
  renderer.draw(game);
  SC_CHECK(countColor(canvas,
                      SDL_Rect{kControlsGamepadX, kRebindRestoreY,
                               kCanvasWidth - kControlsGamepadX,
                               kGlyphRows * kLabelScale},
                      kText) ==
           glyphPixelCount("RESTORE DEFAULTS") * kLabelScale * kLabelScale);

  // The ordinary footer names the page's menu rule; arming a capture swaps
  // it for the capture's own rule and turns the captured row's cells accent.
  const SDL_Rect footerBand{0, kRebindFooterY, kCanvasWidth,
                            kGlyphRows * kHintScale};
  SC_CHECK(countColor(canvas, footerBand, kTextDim) ==
           glyphPixelCount("UP/DOWN CHOOSE - A CHANGE - B BACK") *
               kHintScale * kHintScale);
  renderer.setRebindCaptureRow(5);
  renderer.draw(game);
  SC_CHECK(countColor(canvas, footerBand, kTextDim) ==
           glyphPixelCount("PRESS A CONTROL - SAME ONE CANCELS") *
               kHintScale * kHintScale);
  {
    const int y = kRebindFirstRowY + 5 * kRebindRowHeight;
    const SDL_Rect padCell{kControlsGamepadX, y,
                           kControlsKeyboardX - kControlsGamepadX,
                           kGlyphRows * kLabelScale};
    const SDL_Rect keyCell{kControlsKeyboardX, y,
                           kControlsActionX - kControlsKeyboardX,
                           kGlyphRows * kLabelScale};
    SC_CHECK(countColor(canvas, padCell, kAccent) ==
             glyphPixelCount("D-PAD UP") * kLabelScale * kLabelScale);
    SC_CHECK(countColor(canvas, keyCell, kAccent) ==
             glyphPixelCount("SPACE") * kLabelScale * kLabelScale);
    SC_CHECK(countColor(canvas, padCell, kText) == 0);
    SC_CHECK(countColor(canvas, keyCell, kText) == 0);
  }

  // The status line names the last outcome in the accent color, exactly the
  // glyphs of the message and nothing else in its band.
  renderer.setRebindMessage("BOUND HARD DROP TO D");
  renderer.draw(game);
  {
    const SDL_Rect messageBand{kControlsGamepadX, kRebindMessageY,
                               kCanvasWidth - kControlsGamepadX,
                               kGlyphRows * kLabelScale};
    SC_CHECK(countColor(canvas, messageBand, kAccent) ==
             glyphPixelCount("BOUND HARD DROP TO D") * kLabelScale *
                 kLabelScale);
  }

  // Custom tables reach the page through the same mirror: the drop row reads
  // the rebound controls and nothing of the defaults survives on it.
  {
    ControlBindings custom = defaultControlBindings();
    custom.table(DeviceKind::Keyboard, Orientation::Landscape)
        [static_cast<int>(GameAction::HardDrop)][0] = kKeyboardKeyD;
    custom.table(DeviceKind::Keyboard, Orientation::Landscape)
        [static_cast<int>(GameAction::HardDrop)][1] = kNoControl;
    custom.table(DeviceKind::Controller, Orientation::Landscape)
        [static_cast<int>(GameAction::HardDrop)][0] = kControllerButtonX;
    custom.table(DeviceKind::Controller, Orientation::Landscape)
        [static_cast<int>(GameAction::HardDrop)][1] = kNoControl;
    renderer.setControlBindings(custom);
    renderer.setRebindCaptureRow(-1);
    renderer.draw(game);
    const int y = kRebindFirstRowY + 5 * kRebindRowHeight;
    const SDL_Rect padCell{kControlsGamepadX, y,
                           kControlsKeyboardX - kControlsGamepadX,
                           kGlyphRows * kLabelScale};
    const SDL_Rect keyCell{kControlsKeyboardX, y,
                           kControlsActionX - kControlsKeyboardX,
                           kGlyphRows * kLabelScale};
    SC_CHECK(countColor(canvas, padCell, kText) ==
             glyphPixelCount("X") * kLabelScale * kLabelScale);
    SC_CHECK(countColor(canvas, keyCell, kText) ==
             glyphPixelCount("D") * kLabelScale * kLabelScale);
    renderer.setControlBindings(defaultControlBindings());
  }

  // The portrait half of the page at the portrait metrics: the turned frame
  // names its own spellings - the drop row's controls are the logical up of
  // the rotated grip - the capture row stays accent, and RESTORE DEFAULTS
  // draws on its own row. A portrait band (x, y, w, h) reaches the device
  // canvas at (y, 543 - x, h, w), the rotation the pixel suite pins.
  renderer.setOrientation(Orientation::Portrait);
  renderer.setRebindCaptureRow(5);
  renderer.setRebindMessage("");
  renderer.draw(game);
  {
    const auto portraitBand = [](int y, int scale) {
      return SDL_Rect{y, 0, kGlyphRows * scale, kPortraitCanvasWidth};
    };
    const SDL_Rect headingBand =
        portraitBand(kPControlsHeadingY, kControlsHeadingScale);
    SC_CHECK(countColor(canvas, headingBand, kText) > 300);
    // The capture row's value line sits under its action label; the band of
    // that line holds the accent cells of both device columns.
    const int captureValuesY =
        kPRebindFirstRowY + 5 * kPRebindRowHeight + kGlyphRows * kLabelScale + 4;
    SC_CHECK(countColor(canvas, portraitBand(captureValuesY, kLabelScale),
                        kAccent) > 100);
    // The default portrait drop controls spell where the turned device
    // points them (task T067); the band holds both cells' glyphs together.
    SC_CHECK(countColor(canvas, portraitBand(captureValuesY, kLabelScale),
                        kAccent) ==
             (glyphPixelCount("D-PAD LEFT (UP)") +
              glyphPixelCount("LEFT ARROW (UP)")) *
                 kLabelScale * kLabelScale);
    SC_CHECK(countColor(canvas,
                        portraitBand(kPRebindRestoreY, kLabelScale), kText) ==
             glyphPixelCount("RESTORE DEFAULTS") * kLabelScale * kLabelScale);
    SC_CHECK(countColor(canvas, portraitBand(kPRebindFooterY, kHintScale),
                        kTextDim) ==
             glyphPixelCount("PRESS A CONTROL - SAME ONE CANCELS") *
                 kHintScale * kHintScale);
  }

  // The way back out survives everything: back returns to the mappings page
  // with its customize footer, and back again returns to the title.
  renderer.setOrientation(Orientation::Landscape);
  renderer.setRebindCaptureRow(-1);
  TickInput back;
  back.menu.backPressed = true;
  game.tick(back);
  SC_CHECK(game.scene() == Scene::Controls);
  renderer.draw(game);
  const SDL_Rect controlsFooter = menuRow(kControlsFooterY, kLabelScale);
  SC_CHECK(countColor(canvas, controlsFooter, kText) ==
           glyphPixelCount("A CUSTOMIZE - B BACK TO TITLE") * kLabelScale *
               kLabelScale);
  game.tick(back);
  SC_CHECK(game.scene() == Scene::Title);
}

// The options page of tasks T040, T044, and T095: the heading, the setting
// rows with every value combination, the accent marker on the highlighted
// row, and the footer band. The core moves the cursor alone; the values come
// from the renderer settings, so all 320 combinations of the six toggles and
// the next count are drawn, and the exact glyph count of the 5x7 table pins
// each row's string - ON cannot stand in for OFF, and NEXT PIECES 3 cannot
// stand in for NEXT PIECES 5. The ORIENTATION row names the layout the
// renderer draws, and a mid-page turn moves the page itself, so the portrait
// half of that row is drawn and pinned at the portrait metrics (task T068).
void run_options_screen_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);
  Game game;
  game.openTitle(20261103);

  // The defaults mirror a session that loaded no settings file: every toggle
  // on, five Next previews, grayscale off, landscape, the Auto music pool,
  // both MUSIC.md volume defaults (tasks T044, T055, and T062), the Auto
  // wallpaper over design 1 (task T084), and the piece designs off (task
  // T095).
  SC_CHECK(kOptionsItemCount == 10);
  SC_CHECK(kOptionsMusicRow == 7);
  SC_CHECK(kOptionsBackgroundsRow == 8);
  SC_CHECK(kOptionsPieceDesignsRow == 9);
  SC_CHECK(kBackgroundOptionsItemCount == 2);
  SC_CHECK(renderer.soundOn() == true);
  SC_CHECK(renderer.showGhost() == true);
  SC_CHECK(renderer.showGrid() == true);
  SC_CHECK(renderer.showHold() == true);
  SC_CHECK(renderer.nextPreviews() == kPreviewCount);
  SC_CHECK(renderer.grayscale() == false);
  SC_CHECK(renderer.pieceDesigns() == false);
  SC_CHECK(renderer.orientation() == Orientation::Landscape);
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
  // its renderer setting - ON or OFF for the six toggles, the digit for
  // NEXT PIECES, the frame the renderer draws for ORIENTATION - and nothing
  // else in a row band is bright or dim, so the exact glyph count pins the
  // string. The landscape half of the table runs here; the portrait half
  // follows the turn below, because one mirror now names the layout and the
  // row cannot disagree with it (task T068).
  for (int booleans = 0; booleans < 64; ++booleans) {
    for (int next = 1; next <= kPreviewCount; ++next) {
      renderer.setSoundOn((booleans & 1) != 0);
      renderer.setShowGhost((booleans & 2) != 0);
      renderer.setShowGrid((booleans & 4) != 0);
      renderer.setShowHold((booleans & 8) != 0);
      renderer.setGrayscale((booleans & 16) != 0);
      renderer.setPieceDesigns((booleans & 32) != 0);
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
          "ORIENTATION LANDSCAPE",
          "MUSIC AUTO",
          "BACKGROUNDS AUTO",
          (booleans & 32) ? "PIECE DESIGNS ON" : "PIECE DESIGNS OFF",
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

  // The turn itself, in the middle of the open page: setOrientation() moves
  // the same page to the portrait metrics at once, the ORIENTATION row reads
  // PORTRAIT at those metrics, and the landscape row bands go quiet because
  // the page no longer draws there. This is the draw half of the same-session
  // application the harness sessions exercise through the event path
  // (task T068). A portrait rect (x, y, w, h) reaches the device canvas at
  // (y, 543 - x, h, w), the rotation the pixel suite pins below.
  renderer.setOrientation(Orientation::Portrait);
  SC_CHECK(renderer.orientation() == Orientation::Portrait);
  renderer.draw(game);
  const auto portraitRow = [](int index) {
    return SDL_Rect{kPOptionsMenuY + index * kMenuItemHeight, 0,
                    kGlyphRows * kMenuScale, kPortraitCanvasWidth};
  };
  for (int index = 0; index < kOptionsItemCount; ++index) {
    // Every row draws at the portrait metrics: the selected row bright, every
    // other row dim, exactly as the landscape loop above pins row by row.
    if (index == game.optionsSelection()) {
      SC_CHECK(countColor(canvas, portraitRow(index), kText) > 100);
      SC_CHECK(countColor(canvas, portraitRow(index), kTextDim) == 0);
    } else {
      SC_CHECK(countColor(canvas, portraitRow(index), kTextDim) > 100);
      SC_CHECK(countColor(canvas, portraitRow(index), kText) == 0);
    }
  }
  // The page turned, it did not draw twice: portrait rows end at portrait
  // y=617 and the footer starts at 880, so the device columns between them
  // hold nothing at all while the landscape rows they overlap used to carry
  // every row's text.
  SC_CHECK(areaIsOnly(canvas, SDL_Rect{624, 96, 236, 384}, kBackground));
  SC_CHECK(countColor(canvas, portraitRow(6), kTextDim) ==
           glyphPixelCount("ORIENTATION PORTRAIT") * kMenuScale * kMenuScale);
  // The turn back restores the landscape page and its own row string.
  renderer.setOrientation(Orientation::Landscape);
  renderer.draw(game);
  SC_CHECK(countColor(canvas,
                      menuRow(kOptionsMenuY + 6 * kMenuItemHeight, kMenuScale),
                      kTextDim) ==
           glyphPixelCount("ORIENTATION LANDSCAPE") * kMenuScale * kMenuScale);

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

  // Nine downs walk the cursor to the last row, and the marker follows: the
  // first row goes dim and the PIECE DESIGNS row goes bright.
  for (int step = 0; step < kOptionsItemCount - 1; ++step) {
    menuDown(game);
  }
  SC_CHECK(game.optionsSelection() == kOptionsPieceDesignsRow);
  renderer.draw(game);
  SC_CHECK(countColor(canvas, menuRow(kOptionsMenuY, kMenuScale), kText) == 0);
  SC_CHECK(countColor(canvas,
                      menuRow(kOptionsMenuY +
                                  (kOptionsItemCount - 1) * kMenuItemHeight,
                              kMenuScale),
                      kText) > 100);
  // The tenth down stays inside the screen: one more down wraps to the
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

// The backgrounds page behind the BACKGROUNDS row (task T084): the same
// heading, menu metrics, and footer hint as the options page, over its two
// rows - the mode word and the preferred design with its stable number and
// approved name in the menu's uppercase spelling. Every mode and every
// design ID draws, because the renderer mirrors the wallpaper state the
// platform layer pushes, and the exact glyph count pins each row's string:
// MODE OFF cannot stand in for MODE AUTO, and DESIGN 3 DISTANT STARS cannot
// stand in for DESIGN 7 SLEEPING SKYLINE. A portrait pass proves the widest
// design row, its marker included, stays on the 544 px canvas.
void run_background_options_screen_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);
  Game game;
  game.openTitle(20261105);

  // Reach the page: OPTIONS, eight downs to the BACKGROUNDS row, A to open.
  menuDown(game);
  menuDown(game);
  SC_CHECK(game.titleSelection() == 2);
  TickInput select;
  select.menu.selectPressed = true;
  game.tick(select);
  SC_CHECK(game.scene() == Scene::Options);
  for (int step = 0; step < kOptionsBackgroundsRow; ++step) {
    menuDown(game);
  }
  game.tick(select);
  SC_CHECK(game.scene() == Scene::BackgroundOptions);
  SC_CHECK(game.backgroundsSelection() == 0);

  // Plain background and a bright BACKGROUNDS heading at the shared
  // constants, the same plain menu ground as the options page: the page
  // carries no wallpaper of its own (task T084 keeps non-play pages flat).
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 5, 5), kBackground));
  const SDL_Rect heading{0, kOptionsHeadingY, kCanvasWidth,
                         kGlyphRows * kOptionsHeadingScale};
  SC_CHECK(countColor(canvas, heading, kText) > 400);

  // Every mode and design combination draws, with the row strings pinned by
  // their exact glyph counts. The renderer's wallpaper mirror holds the
  // preference in every mode, so the design row reads it even while Off.
  const backgrounds::Mode modes[3] = {backgrounds::Mode::Auto,
                                      backgrounds::Mode::Off,
                                      backgrounds::Mode::Fixed};
  const char* modeWords[3] = {"AUTO", "OFF", "FIXED"};
  for (int mode = 0; mode < 3; ++mode) {
    for (int design = backgrounds::kDesignFirstId;
         design <= backgrounds::kDesignLastId; ++design) {
      renderer.setBackgroundMode(modes[mode]);
      renderer.setBackgroundPreference(design);
      game.tick(TickInput{});  // a quiet tick keeps the scene and the cursor
      renderer.draw(game);
      char modeRow[16];
      std::snprintf(modeRow, sizeof(modeRow), "MODE %s", modeWords[mode]);
      char designRow[48];
      std::snprintf(designRow, sizeof(designRow), "DESIGN %d", design);
      const backgrounds::Design* record =
          backgrounds::designOf(renderer.backgroundPreference());
      SC_CHECK(record != nullptr);
      std::strncat(designRow, " ",
                   sizeof(designRow) - std::strlen(designRow) - 1);
      char upper[32];
      std::size_t at = 0;
      for (const char* p = record->name;
           *p != '\0' && at + 1 < sizeof(upper); ++p) {
        upper[at++] = *p >= 'a' && *p <= 'z'
                          ? static_cast<char>(*p - 'a' + 'A')
                          : *p;
      }
      upper[at] = '\0';
      std::strncat(designRow, upper,
                   sizeof(designRow) - std::strlen(designRow) - 1);
      const char* rows[kBackgroundOptionsItemCount] = {modeRow, designRow};
      for (int index = 0; index < kBackgroundOptionsItemCount; ++index) {
        const int y = kOptionsMenuY + index * kMenuItemHeight;
        const SDL_Rect row = menuRow(y, kMenuScale);
        const int pixels =
            glyphPixelCount(rows[index]) * kMenuScale * kMenuScale;
        if (index == game.backgroundsSelection()) {
          SC_CHECK(countColor(canvas, row, kText) == pixels);
          SC_CHECK(countColor(canvas, row, kTextDim) == 0);
        } else {
          SC_CHECK(countColor(canvas, row, kTextDim) == pixels);
          SC_CHECK(countColor(canvas, row, kText) == 0);
          SC_CHECK(countColor(canvas, row, kAccent) == 0);
        }
      }
      // The widest design row keeps its whole text and its selection marker
      // on the canvas.
      const int textLeft = kCanvasWidth / 2 -
                           textWidth(kMenuScale, designRow) / 2;
      SC_CHECK(textLeft - kMenuMarkerGap - kMenuMarkerSize >= 0);
      SC_CHECK(textLeft + textWidth(kMenuScale, designRow) <= kCanvasWidth);
    }
  }

  // The cursor walks the two rows with the marker: one down reaches DESIGN
  // and one more wraps to MODE.
  for (int step = 0; step < kBackgroundOptionsItemCount - 1; ++step) {
    menuDown(game);
  }
  SC_CHECK(game.backgroundsSelection() == kBackgroundOptionsItemCount - 1);
  renderer.draw(game);
  SC_CHECK(countColor(canvas, menuRow(kOptionsMenuY, kMenuScale), kText) == 0);
  menuDown(game);
  SC_CHECK(game.backgroundsSelection() == 0);

  // The footer band holds exactly the shared options hint, and both rows and
  // the footer sit inside the canvas with nothing clipped.
  const SDL_Rect footerBand{0, kOptionsFooterY - 6, kCanvasWidth,
                            kGlyphRows * kHintScale + 12};
  const std::vector<int> footerRuns = textRowRuns(canvas, footerBand);
  SC_CHECK(footerRuns.size() == 1);
  SC_CHECK(footerRuns[0] == kGlyphRows * kHintScale);
  SC_CHECK(countColor(canvas, footerBand, kTextDim) ==
           glyphPixelCount("UP/DOWN CHOOSE - A CHANGE - B BACK") * kHintScale *
               kHintScale);

  // The portrait page: the widest design row, DESIGN 7 SLEEPING SKYLINE, and
  // its marker stay inside the 544 px portrait canvas, and both rows draw at
  // the portrait metrics. A portrait rect (x, y, w, h) reaches the device
  // canvas at (y, 543 - x, h, w).
  renderer.setBackgroundMode(backgrounds::Mode::Fixed);
  renderer.setBackgroundPreference(7);
  renderer.setOrientation(Orientation::Portrait);
  renderer.draw(game);
  {
    const char* designRow = "DESIGN 7 SLEEPING SKYLINE";
    const int textTop = kPOptionsMenuY + kBackgroundOptionsPreferredRow *
                                              kMenuItemHeight;
    const int textLeft = kPortraitCanvasWidth / 2 -
                         textWidth(kMenuScale, designRow) / 2;
    SC_CHECK(textLeft - kMenuMarkerGap - kMenuMarkerSize >= 0);
    SC_CHECK(textLeft + textWidth(kMenuScale, designRow) <=
             kPortraitCanvasWidth);
    // Row 1 draws bright when selected at the portrait metrics.
    menuDown(game);
    SC_CHECK(game.backgroundsSelection() == 1);
    renderer.draw(game);
    const SDL_Rect portraitRow1{textTop, 0, kGlyphRows * kMenuScale,
                                kPortraitCanvasWidth};
    SC_CHECK(countColor(canvas, portraitRow1, kText) > 100);
  }
  renderer.setOrientation(Orientation::Landscape);

  // Back returns to the options screen with the cursor still on the
  // BACKGROUNDS row, whose band turns bright again.
  TickInput back;
  back.menu.backPressed = true;
  game.tick(back);
  SC_CHECK(game.scene() == Scene::Options);
  SC_CHECK(game.optionsSelection() == kOptionsBackgroundsRow);
  renderer.draw(game);
  SC_CHECK(countColor(canvas,
                      menuRow(kOptionsMenuY +
                                  kOptionsBackgroundsRow * kMenuItemHeight,
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
           glyphPixelCount("A CUSTOMIZE - B BACK TO PAUSE") * kLabelScale *
               kLabelScale);
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
  renderer.setOrientation(Orientation::Portrait);
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

  // The hard-drop row names the physical arm of the turned grip - the
  // device D-pad's left, which points at the player's ceiling - on the
  // gamepad side and the left arrow on the keyboard side (task T067). The
  // exact glyph count of each cell pins both strings and their columns, so
  // the portrait row cannot drift back to the cluster button.
  const int dropRowY = kPControlsFirstRowY + 5 * kPControlsRowHeight;
  const int dropHeading = dropRowY;
  const int dropMappings = dropRowY + kGlyphRows * kLabelScale + 4;
  const SDL_Rect dropActionHeading = portraitDeviceRect(
      kPControlsGamepadX, dropHeading, kPortraitCanvasWidth -
      kPControlsGamepadX, kGlyphRows * kLabelScale);
  const SDL_Rect dropGamepadCell = portraitDeviceRect(
      kPControlsGamepadX, dropMappings,
      kPControlsKeyboardX - kPControlsGamepadX, kGlyphRows * kLabelScale);
  const SDL_Rect dropKeyboardCell = portraitDeviceRect(
      kPControlsKeyboardX, dropMappings,
      kPortraitCanvasWidth - kPControlsKeyboardX, kGlyphRows * kLabelScale);
  SC_CHECK(countColor(canvas, dropActionHeading, kTextDim) ==
           glyphPixelCount("HARD DROP") * kLabelScale * kLabelScale);
  SC_CHECK(countColor(canvas, dropGamepadCell, kText) ==
           glyphPixelCount("D-PAD LEFT (UP)") * kLabelScale * kLabelScale);
  SC_CHECK(countColor(canvas, dropKeyboardCell, kText) ==
           glyphPixelCount("LEFT ARROW (UP)") * kLabelScale * kLabelScale);

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

// ---------------------------------------------------------------------------
// The level backgrounds of task T078: the zones the renderer derives from
// its geometry, the wallpaper pixels of the play scene in both frames, the
// Off legacy path, cuts, stepped fades, the clock freeze, the wallpaper
// beneath the overlays, and the untouched menu grounds.
// ---------------------------------------------------------------------------

// The per-channel maxima of `area`, for the componentwise cap scans.
SDL_Color maxChannels(const Canvas& canvas, const SDL_Rect& area) {
  SDL_Color largest{0, 0, 0, 255};
  for (int y = area.y; y < area.y + area.h; ++y) {
    for (int x = area.x; x < area.x + area.w; ++x) {
      const SDL_Color color = pixelAt(canvas, x, y);
      if (color.r > largest.r) {
        largest.r = color.r;
      }
      if (color.g > largest.g) {
        largest.g = color.g;
      }
      if (color.b > largest.b) {
        largest.b = color.b;
      }
    }
  }
  return largest;
}

void run_background_checks() {
  Canvas canvas = makeCanvas(kCanvasWidth, kCanvasHeight);
  SC_CHECK(canvas.ok);
  Renderer renderer(canvas.renderer);

  // The zones of backgroundZones() are the design package mask
  // (docs/backgrounds/index.md, "The layout mask"), restated here with the
  // mask's own numbers so the derivation cannot drift from the contract.
  const backgrounds::Zones landscape = backgroundZones(Orientation::Landscape);
  SC_CHECK(landscape.canvasWidth == 960 && landscape.canvasHeight == 544);
  SC_CHECK(landscape.frame.x == 344 && landscape.frame.y == 1);
  SC_CHECK(landscape.frame.w == 272 && landscape.frame.h == 542);
  SC_CHECK(landscape.loudCount == 2);
  SC_CHECK(landscape.loud[0].x == 0 && landscape.loud[0].y == 0);
  SC_CHECK(landscape.loud[0].w == 192 && landscape.loud[0].h == 544);
  SC_CHECK(landscape.loud[1].x == 768 && landscape.loud[1].w == 192);
  const backgrounds::Zones portrait = backgroundZones(Orientation::Portrait);
  SC_CHECK(portrait.canvasWidth == 544 && portrait.canvasHeight == 960);
  SC_CHECK(portrait.frame.x == 71 && portrait.frame.y == 119);
  SC_CHECK(portrait.frame.w == 402 && portrait.frame.h == 802);
  SC_CHECK(portrait.loudCount == 3);
  SC_CHECK(portrait.loud[0].x == 0 && portrait.loud[0].y == 119);
  SC_CHECK(portrait.loud[0].w == 59 && portrait.loud[0].h == 802);
  SC_CHECK(portrait.loud[1].x == 485 && portrait.loud[1].w == 59);
  SC_CHECK(portrait.loud[2].x == 0 && portrait.loud[2].y == 921);
  SC_CHECK(portrait.loud[2].w == 544 && portrait.loud[2].h == 39);

  Game game;
  game.start(4242);

  // Auto at level 1 draws the Still ground design of task T079: the legacy
  // base #10121A with two nested corner steps of the darker ink #0C0E16 in
  // each corner, inside the loud margins alone. The suites above read the
  // flat legacy ground through mode Off, so this is where the level 1 pixels
  // are pinned.
  const SDL_Rect leftMargin{0, 0, 192, 544};
  const SDL_Rect rightMargin{768, 0, 192, 544};
  SC_CHECK(renderer.backgroundMode() == backgrounds::Mode::Auto);
  renderer.setBackgroundLevel(1);
  renderer.draw(game);
  const SDL_Color base1{0x10, 0x12, 0x1A, 255};
  const SDL_Color ink1{0x0C, 0x0E, 0x16, 255};
  // The outer step is 160 x 121, the inner 99 x 75, both touching the corner.
  SC_CHECK(sameColor(pixelAt(canvas, 5, 5), ink1));
  SC_CHECK(sameColor(pixelAt(canvas, 159, 120), ink1));
  SC_CHECK(sameColor(pixelAt(canvas, 160, 120), base1));   // past the outer
  SC_CHECK(sameColor(pixelAt(canvas, 161, 0), base1));
  SC_CHECK(sameColor(pixelAt(canvas, 98, 74), ink1));      // inside the inner
  SC_CHECK(sameColor(pixelAt(canvas, 90, 100), ink1));     // outer, past inner
  SC_CHECK(sameColor(pixelAt(canvas, 900, 300), base1));   // right margin, no step
  SC_CHECK(sameColor(pixelAt(canvas, 900, 100), ink1));    // top-right corner
  SC_CHECK(sameColor(pixelAt(canvas, 100, 500), ink1));    // bottom-left corner
  SC_CHECK(sameColor(pixelAt(canvas, 900, 500), ink1));    // bottom-right corner
  SC_CHECK(sameColor(pixelAt(canvas, 100, 300), base1));   // left margin, no step
  // The quiet span holds the base alone: the statistics column, the next
  // column, and both guard gaps.
  SC_CHECK(sameColor(pixelAt(canvas, 250, 300), base1));
  SC_CHECK(sameColor(pixelAt(canvas, 700, 450), base1));
  SC_CHECK(sameColor(pixelAt(canvas, 330, 300), base1));
  SC_CHECK(sameColor(pixelAt(canvas, 630, 100), base1));
  // The corner ink sits below the base and far under every cap, and the
  // well and its ring stay pure.
  const SDL_Color cornerPeak = maxChannels(canvas, leftMargin);
  SC_CHECK(cornerPeak.r <= 0x10 && cornerPeak.g <= 0x12 &&
           cornerPeak.b <= 0x1A);
  SC_CHECK(sameColor(pixelAt(canvas, 490, 400), kBoardWell));
  SC_CHECK(sameColor(pixelAt(canvas, 344, 400), kFrame));

  // A level with a cut record switches in one draw: level 5, the first
  // moving design of the Stir chapter (task T080), cuts in with its base
  // everywhere outside the frame and its 56 hash-ranked stars inside the
  // loud margins alone, the static dust beneath them. The round reset
  // parks the presentation clock at zero, so the stars sit at the t = 0
  // placement of the seed 0x5356.
  renderer.setBackgroundLevel(5);
  SC_CHECK(renderer.backgroundLevel() == 5);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design5{0x0F, 0x14, 0x20, 255};
  const SDL_Color starBright5{0x68, 0x78, 0x92, 255};
  const SDL_Color dust5{0x20, 0x28, 0x3A, 255};
  const SDL_Rect leftGap{320, 0, 24, 544};
  const SDL_Rect rightGap{616, 0, 24, 544};
  // The quiet span holds the base alone: no star or fleck draws under the
  // HUD columns or in the guard gaps.
  SC_CHECK(sameColor(pixelAt(canvas, 250, 300), design5));   // stats column
  SC_CHECK(sameColor(pixelAt(canvas, 700, 450), design5));   // next column
  SC_CHECK(areaIsOnly(canvas, leftGap, design5));
  SC_CHECK(areaIsOnly(canvas, rightGap, design5));
  // The approved anchors: a 1 px bright star at (18, 340), a 2 x 2 bright
  // star at (51, 41), a 3 x 1 dust fleck at (61, 18).
  SC_CHECK(sameColor(pixelAt(canvas, 18, 340), starBright5));
  SC_CHECK(sameColor(pixelAt(canvas, 19, 341), design5));    // past it
  SC_CHECK(sameColor(pixelAt(canvas, 51, 41), starBright5));
  SC_CHECK(sameColor(pixelAt(canvas, 52, 42), starBright5));
  SC_CHECK(sameColor(pixelAt(canvas, 53, 43), design5));     // past the 2 px
  SC_CHECK(sameColor(pixelAt(canvas, 61, 18), dust5));
  SC_CHECK(sameColor(pixelAt(canvas, 63, 18), dust5));
  SC_CHECK(sameColor(pixelAt(canvas, 64, 18), design5));     // past the 3 px
  // The stars sink at the record's 2 px/s: after half a second of
  // presentation clock every star sits one pixel lower - the bright
  // anchor of (18, 340) now at 341 - and the dust beneath holds still.
  advanceBackgroundBy(renderer, 500);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 18, 341), starBright5));
  SC_CHECK(sameColor(pixelAt(canvas, 18, 340), design5));
  SC_CHECK(sameColor(pixelAt(canvas, 51, 42), starBright5));
  SC_CHECK(sameColor(pixelAt(canvas, 61, 18), dust5));
  // The well and its ring stay pure: the background ground hides under them.
  SC_CHECK(sameColor(pixelAt(canvas, 490, 400), kBoardWell));
  SC_CHECK(sameColor(pixelAt(canvas, 344, 400), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, 615, 400), kFrame));
  // The quiet rule holds on the pure-background guard strips: nothing above
  // the secondary cap #2A3146 draws there, componentwise, and the loud
  // margins hold the accent cap #8A94AC.
  const SDL_Color gapPeak = maxChannels(canvas, leftGap);
  SC_CHECK(gapPeak.r <= 0x2A && gapPeak.g <= 0x31 && gapPeak.b <= 0x46);
  const SDL_Color gapPeakRight = maxChannels(canvas, rightGap);
  SC_CHECK(gapPeakRight.r <= 0x2A && gapPeakRight.g <= 0x31 &&
           gapPeakRight.b <= 0x46);
  const SDL_Color marginPeak = maxChannels(canvas, leftMargin);
  SC_CHECK(marginPeak.r <= 0x8A && marginPeak.g <= 0x94 &&
           marginPeak.b <= 0xAC);

  // Fixed shows the preferred design at every level: the level moves and the
  // design does not.
  renderer.setBackgroundPreference(20);
  renderer.setBackgroundMode(backgrounds::Mode::Fixed);
  SC_CHECK(renderer.backgroundPreference() == 20);
  renderer.setBackgroundLevel(3);
  renderer.draw(game);
  const SDL_Color design20{0x19, 0x1E, 0x2B, 255};
  // The landed Collapse grammar holds base-only windows in its margin -
  // its debris piles toward the far canvas edges - so the Fixed proof
  // reads one: the 48 x 48 block at x 48, clear of the ranked fragments
  // and the slabs at the t = 0 placement.
  SC_CHECK(areaIsOnly(canvas, SDL_Rect{48, 0, 48, 48}, design20));
  SC_CHECK(sameColor(pixelAt(canvas, 250, 300), design20));

  // Off restores the legacy flat #10121A at any level, and nothing else:
  // the whole margin holds the old canvas color alone.
  renderer.setBackgroundMode(backgrounds::Mode::Off);
  renderer.setBackgroundLevel(20);
  SC_CHECK(renderer.backgroundLevel() == 20);
  renderer.draw(game);
  SC_CHECK(areaIsOnly(canvas, leftMargin, kBackground));
  SC_CHECK(areaIsOnly(canvas, rightMargin, kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, 250, 300), kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, 490, 400), kBoardWell));

  // A fade record steps visibly. Back on Auto at level 20 the design cuts
  // in (Off never left a fade pending), then level 2 fades from design 20
  // over 400 ms in 12 steps; the anchor of the two bases is the midpoint.
  // The fade reads the base-only window of the landed design 20 above,
  // above the rows the design 2 rule and its under-bands occupy once the
  // fade lands.
  const SDL_Rect upperLeftMargin{48, 0, 48, 48};
  renderer.setBackgroundMode(backgrounds::Mode::Auto);
  renderer.setBackgroundLevel(20);
  renderer.draw(game);
  SC_CHECK(areaIsOnly(canvas, upperLeftMargin, design20));
  renderer.setBackgroundLevel(2);
  SC_CHECK(renderer.backgroundClockFrozen() == false);
  // Step 0: the outgoing frame continues without a pop.
  renderer.draw(game);
  SC_CHECK(areaIsOnly(canvas, upperLeftMargin, design20));
  // Half way: the flat anchor hand-off frame of the two fade halves.
  renderer.advanceBackgroundClock(200);
  renderer.draw(game);
  const SDL_Color anchor{0x14, 0x18, 0x23, 255};  // mid of #191E2B, #10131C
  SC_CHECK(areaIsOnly(canvas, upperLeftMargin, anchor));
  // Frozen mid-fade: focus loss or pause holds the step, and the time the
  // freeze covers never lands when it thaws.
  renderer.freezeBackgroundClock(true);
  SC_CHECK(renderer.backgroundClockFrozen());
  renderer.advanceBackgroundClock(250);
  renderer.advanceBackgroundClock(50);
  renderer.draw(game);
  SC_CHECK(areaIsOnly(canvas, upperLeftMargin, anchor));
  renderer.freezeBackgroundClock(false);
  renderer.advanceBackgroundClock(100);  // 300 ms of fade: step 9
  renderer.draw(game);
  const SDL_Color stepNine{18, 21, 31, 255};  // #10131C six parts from anchor
  SC_CHECK(areaIsOnly(canvas, upperLeftMargin, stepNine));
  // The fade lands on the plain target inside its record.
  renderer.advanceBackgroundClock(100);
  renderer.draw(game);
  const SDL_Color design2{0x10, 0x13, 0x1C, 255};
  SC_CHECK(areaIsOnly(canvas, upperLeftMargin, design2));

  // The landed First light design of the Still chapter: the 1 px rule of
  // #2A3146 at row 440 and the two under-bands of #1C2433 below it, both
  // cut by the frame ring, both within the secondary cap everywhere they
  // draw - the rule is exactly at the cap, so it may cross the quiet zones.
  const SDL_Color rule2{0x2A, 0x31, 0x46, 255};
  const SDL_Color under2{0x1C, 0x24, 0x33, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 5, 440), rule2));
  SC_CHECK(sameColor(pixelAt(canvas, 343, 440), rule2));  // the frame cuts it
  SC_CHECK(sameColor(pixelAt(canvas, 344, 440), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, 616, 440), rule2));
  SC_CHECK(sameColor(pixelAt(canvas, 615, 440), kFrame));
  SC_CHECK(sameColor(pixelAt(canvas, 5, 439), design2));
  SC_CHECK(sameColor(pixelAt(canvas, 5, 441), design2));
  SC_CHECK(sameColor(pixelAt(canvas, 5, 449), under2));   // rows 448..450
  SC_CHECK(sameColor(pixelAt(canvas, 5, 451), design2));
  SC_CHECK(sameColor(pixelAt(canvas, 5, 458), under2));   // rows 456..459
  SC_CHECK(sameColor(pixelAt(canvas, 5, 460), design2));
  SC_CHECK(sameColor(pixelAt(canvas, 250, 440), rule2));  // the stats column
  SC_CHECK(sameColor(pixelAt(canvas, 490, 440), kBoardWell));  // under the well

  // The Distant stars design: the approved hash-ranked placement of 28
  // stars, inside the loud margins alone, in the two shades of its record.
  renderer.setBackgroundLevel(3);
  advanceBackgroundBy(renderer, 750);  // past the 0.5 s fade of the record
  renderer.draw(game);
  const SDL_Color base3{0x0E, 0x12, 0x1C, 255};
  const SDL_Color starDim{0x1B, 0x22, 0x30, 255};
  const SDL_Color starBright{0x64, 0x71, 0x8C, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 779, 371), starDim));    // first star
  SC_CHECK(sameColor(pixelAt(canvas, 780, 372), starDim));    // 2 x 2
  SC_CHECK(sameColor(pixelAt(canvas, 781, 372), base3));      // past it
  SC_CHECK(sameColor(pixelAt(canvas, 954, 395), starDim));    // a 1 px star
  SC_CHECK(sameColor(pixelAt(canvas, 955, 395), base3));
  SC_CHECK(sameColor(pixelAt(canvas, 831, 86), starBright));  // a bright one
  SC_CHECK(sameColor(pixelAt(canvas, 2, 203), starDim));      // left margin
  SC_CHECK(sameColor(pixelAt(canvas, 147, 16), starDim));
  // The quiet span holds the base alone: no star under the HUD. The guard
  // gaps carry wallpaper only, so they pin it exactly; the HUD columns carry
  // their text on top of the same base.
  SC_CHECK(sameColor(pixelAt(canvas, 330, 300), base3));
  SC_CHECK(sameColor(pixelAt(canvas, 630, 100), base3));
  SC_CHECK(areaIsOnly(canvas, leftGap, base3));
  SC_CHECK(areaIsOnly(canvas, rightGap, base3));
  // Every star pixel stays inside the loud margins and under the accent cap.
  const SDL_Color starMarginPeak = maxChannels(canvas, leftMargin);
  SC_CHECK(starMarginPeak.r <= 0x8A && starMarginPeak.g <= 0x94 &&
           starMarginPeak.b <= 0xAC);

  // The Quiet strata design: eight sediment bands of #222A39 stacked in the
  // bottom third, cut by the well, with the 6 px stepped ends at the margin
  // edges. Its record fades in over 0.5 s, so the check waits the fade out.
  renderer.setBackgroundLevel(4);
  advanceBackgroundBy(renderer, 750);
  renderer.draw(game);
  const SDL_Color base4{0x11, 0x14, 0x1D, 255};
  const SDL_Color ink4{0x22, 0x2A, 0x39, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 5, 505), ink4));   // the first band
  SC_CHECK(sameColor(pixelAt(canvas, 10, 504), base4));  // its 9 px gap above
  SC_CHECK(sameColor(pixelAt(canvas, 5, 534), ink4));   // its bottom row
  SC_CHECK(sameColor(pixelAt(canvas, 5, 535), base4));  // below the stack
  SC_CHECK(sameColor(pixelAt(canvas, 5, 277), ink4));   // the last band's top
  SC_CHECK(sameColor(pixelAt(canvas, 10, 276), base4));  // above the stack
  SC_CHECK(sameColor(pixelAt(canvas, 300, 505), ink4)); // under the HUD
  SC_CHECK(sameColor(pixelAt(canvas, 490, 505), kBoardWell));  // under the well
  SC_CHECK(sameColor(pixelAt(canvas, 343, 505), ink4));  // the guard gap
  SC_CHECK(sameColor(pixelAt(canvas, 0, 499), ink4));   // the stepped end
  SC_CHECK(sameColor(pixelAt(canvas, 5, 499), ink4));   // 6 px wide
  SC_CHECK(sameColor(pixelAt(canvas, 6, 499), base4));  // past it
  SC_CHECK(sameColor(pixelAt(canvas, 0, 498), base4));  // above the step
  SC_CHECK(sameColor(pixelAt(canvas, 186, 499), ink4)); // the inner edge step
  SC_CHECK(sameColor(pixelAt(canvas, 192, 499), base4));  // outside the margin
  SC_CHECK(sameColor(pixelAt(canvas, 768, 499), ink4));
  SC_CHECK(sameColor(pixelAt(canvas, 954, 499), ink4));
  // The bands never leave the secondary cap: the guard gaps hold wallpaper
  // alone, and the bands cross them at their secondary shade.
  const SDL_Color strataPeak = maxChannels(canvas, leftGap);
  SC_CHECK(strataPeak.r <= 0x2A && strataPeak.g <= 0x31 &&
           strataPeak.b <= 0x46);
  const SDL_Color strataPeakRight = maxChannels(canvas, rightGap);
  SC_CHECK(strataPeakRight.r <= 0x2A && strataPeakRight.g <= 0x31 &&
           strataPeakRight.b <= 0x46);

  // The landed Stir designs of task T080 in the real frame. Each round
  // reset parks the clock at zero, so every design sits at its t = 0
  // placement, and one advance of the presentation clock shows the
  // record's motion at exact pixels.
  //
  // Level 6, Dither dusk: the first inked dusk zone at rows 163..206 - a
  // band inset 6 px from the margin edges with a 6 px end cap 5 px taller
  // at each edge - and its checker patches of accent #38445B rising at
  // 2 px/s over the margin height plus their own 12 px.
  renderer.setBackgroundLevel(6);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design6{0x12, 0x16, 0x20, 255};
  const SDL_Color zone6{0x24, 0x2B, 0x3A, 255};
  const SDL_Color dither6{0x38, 0x44, 0x5B, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 6, 163), zone6));    // the band top
  SC_CHECK(sameColor(pixelAt(canvas, 5, 163), zone6));    // the end cap
  SC_CHECK(sameColor(pixelAt(canvas, 6, 206), zone6));    // band bottom row
  SC_CHECK(sameColor(pixelAt(canvas, 6, 207), design6));  // the 4 px gap
  SC_CHECK(sameColor(pixelAt(canvas, 0, 158), zone6));    // the end cap
  SC_CHECK(sameColor(pixelAt(canvas, 5, 158), zone6));
  SC_CHECK(sameColor(pixelAt(canvas, 6, 157), design6));  // above the cap
  SC_CHECK(sameColor(pixelAt(canvas, 191, 158), zone6));  // the inner cap
  SC_CHECK(sameColor(pixelAt(canvas, 191, 163), zone6));
  SC_CHECK(sameColor(pixelAt(canvas, 191, 206), zone6));
  SC_CHECK(sameColor(pixelAt(canvas, 192, 163), design6));  // outside the
                                                            // margin
  SC_CHECK(sameColor(pixelAt(canvas, 4, 158), zone6));    // 6 px wide cap
  SC_CHECK(sameColor(pixelAt(canvas, 4, 157), design6));
  // A dither square of the first zone, and its motion: half a second
  // lifts every patch one pixel, the square now at sy - 1 and its old
  // bottom row back to the zone beneath.
  {
    int sx = 0;
    int sy = 0;
    bool found = false;
    for (int y = 169; y < 195 && !found; ++y) {
      for (int x = 10; x < 182 && !found; ++x) {
        if (sameColor(pixelAt(canvas, x, y), dither6)) {
          sx = x;
          sy = y;
          found = true;
        }
      }
    }
    SC_CHECK(found);
    if (found) {
      advanceBackgroundBy(renderer, 500);
      renderer.draw(game);
      SC_CHECK(sameColor(pixelAt(canvas, sx, sy - 1), dither6));
      SC_CHECK(sameColor(pixelAt(canvas, sx, sy + 2), dither6));
      SC_CHECK(sameColor(pixelAt(canvas, sx, sy + 3), zone6));
    }
  }

  // Level 7, Sleeping skyline: the corner tower at (3, 352) 43 px wide
  // and 192 tall with its two roof blocks above, and a breathing window
  // of accent #64728A that swells one pixel upward through the middle of
  // the 8 s cycle while its bottom row holds.
  renderer.setBackgroundLevel(7);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design7{0x11, 0x15, 0x1F, 255};
  const SDL_Color tower7{0x25, 0x2D, 0x3D, 255};
  const SDL_Color window7{0x64, 0x72, 0x8A, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 3, 352), tower7));   // the tower top
  SC_CHECK(sameColor(pixelAt(canvas, 3, 351), design7));  // base above it
  SC_CHECK(sameColor(pixelAt(canvas, 46, 352), design7)); // past its width
  SC_CHECK(sameColor(pixelAt(canvas, 12, 344), tower7));  // roof 1
  SC_CHECK(sameColor(pixelAt(canvas, 37, 344), tower7));
  SC_CHECK(sameColor(pixelAt(canvas, 38, 344), design7));
  SC_CHECK(sameColor(pixelAt(canvas, 18, 338), tower7));  // roof 2
  SC_CHECK(sameColor(pixelAt(canvas, 30, 338), tower7));
  SC_CHECK(sameColor(pixelAt(canvas, 31, 338), design7));
  // A window at rest: 3 px wide, 4 tall, 10 px under the tower top.
  SC_CHECK(sameColor(pixelAt(canvas, 7, 362), window7));
  SC_CHECK(sameColor(pixelAt(canvas, 9, 362), window7));
  SC_CHECK(sameColor(pixelAt(canvas, 10, 362), tower7));  // between columns
  SC_CHECK(sameColor(pixelAt(canvas, 7, 365), window7));
  SC_CHECK(sameColor(pixelAt(canvas, 7, 366), tower7));   // inside the tower
  SC_CHECK(sameColor(pixelAt(canvas, 7, 361), tower7));   // 10 px below top
  // Mid-cycle the window swells: one pixel taller at the top, the same
  // bottom row.
  advanceBackgroundBy(renderer, 4000);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 7, 361), window7));
  SC_CHECK(sameColor(pixelAt(canvas, 7, 360), tower7));
  SC_CHECK(sameColor(pixelAt(canvas, 7, 365), window7));
  SC_CHECK(sameColor(pixelAt(canvas, 7, 366), tower7));

  // Level 8, Low signal: the two accent bars of the left margin - 14 px
  // under its top and 40 px over its bottom - and the scanline burst
  // whose rules scroll down 3 px/s, repeating every 10/3 s.
  renderer.setBackgroundLevel(8);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color line8{0x24, 0x2C, 0x3B, 255};
  const SDL_Color bar8{0x54, 0x61, 0x7A, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 6, 14), bar8));      // the top bar
  SC_CHECK(sameColor(pixelAt(canvas, 185, 16), bar8));
  const SDL_Color design8{0x11, 0x15, 0x1F, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 6, 17), design8));
  SC_CHECK(sameColor(pixelAt(canvas, 6, 504), bar8));     // the bottom bar
  SC_CHECK(sameColor(pixelAt(canvas, 185, 506), bar8));
  SC_CHECK(sameColor(pixelAt(canvas, 6, 507), design8));
  SC_CHECK(sameColor(pixelAt(canvas, 64, 189), line8));   // the first rule
  SC_CHECK(sameColor(pixelAt(canvas, 172, 189), line8));
  SC_CHECK(sameColor(pixelAt(canvas, 173, 189), design8));
  SC_CHECK(sameColor(pixelAt(canvas, 64, 190), design8));
  // A third of a second slides the rule set one pixel down; the bars
  // never move.
  advanceBackgroundBy(renderer, 333);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 64, 190), line8));
  SC_CHECK(sameColor(pixelAt(canvas, 64, 189), design8));
  SC_CHECK(sameColor(pixelAt(canvas, 6, 14), bar8));
  SC_CHECK(sameColor(pixelAt(canvas, 6, 504), bar8));

  // The landed Build designs of task T081 in the real frame, each at its
  // t = 0 placement after the round reset, with one advance of the clock
  // showing the record's motion at exact pixels.
  //
  // Level 9, Fracture: the first seam step of the left margin - a 17 x 5
  // block at (0, 0) with its 22 px tall ledge 12 px toward the center -
  // and the sinking accent ticks: a third of a second drops every tick
  // one pixel while the staircase holds still.
  renderer.setBackgroundLevel(9);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design9{0x10, 0x15, 0x1F, 255};
  const SDL_Color seam9{0x28, 0x30, 0x44, 255};
  const SDL_Color tick9{0x70, 0x7D, 0x96, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 0, 0), seam9));      // the seam step
  SC_CHECK(sameColor(pixelAt(canvas, 16, 4), seam9));
  SC_CHECK(sameColor(pixelAt(canvas, 29, 0), design9));   // past the ledge
  SC_CHECK(sameColor(pixelAt(canvas, 0, 5), design9));    // past its 5 rows
  SC_CHECK(sameColor(pixelAt(canvas, 12, 21), seam9));    // the ledge
  SC_CHECK(sameColor(pixelAt(canvas, 12, 22), design9));  // past the ledge
  SC_CHECK(sameColor(pixelAt(canvas, 29, 45), seam9));    // step 1's seam
  SC_CHECK(sameColor(pixelAt(canvas, 28, 45), seam9));    // its ledge edge
  SC_CHECK(sameColor(pixelAt(canvas, 19, 10), tick9));    // a tick
  SC_CHECK(sameColor(pixelAt(canvas, 20, 11), tick9));
  SC_CHECK(sameColor(pixelAt(canvas, 19, 12), seam9));    // the ledge under
  SC_CHECK(sameColor(pixelAt(canvas, 943, 0), seam9));    // mirrored margin
  SC_CHECK(sameColor(pixelAt(canvas, 931, 21), seam9));
  SC_CHECK(sameColor(pixelAt(canvas, 939, 11), tick9));
  advanceBackgroundBy(renderer, 333);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 19, 11), tick9));    // one pixel down
  SC_CHECK(sameColor(pixelAt(canvas, 19, 10), seam9));    // the ledge under
  SC_CHECK(sameColor(pixelAt(canvas, 0, 0), seam9));      // the seam holds
  SC_CHECK(sameColor(pixelAt(canvas, 12, 21), seam9));

  // Level 10, Circuit hush: the first path of the left margin - a 48 px
  // run at (4, 8), its 31 px drop at the right end, the 22 px jog back,
  // and the 35 px closing drop - with its 4 x 4 node and the 2 x 2 lit
  // core that swells to 3 x 3 through the middle of the 7 s cycle.
  renderer.setBackgroundLevel(10);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design10{0x11, 0x17, 0x21, 255};
  const SDL_Color trace10{0x28, 0x31, 0x44, 255};
  const SDL_Color core10{0x6B, 0x84, 0x98, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 4, 8), trace10));    // the run
  SC_CHECK(sameColor(pixelAt(canvas, 51, 8), trace10));
  SC_CHECK(sameColor(pixelAt(canvas, 52, 8), design10));   // past the run
  SC_CHECK(sameColor(pixelAt(canvas, 50, 8), trace10));    // the drop
  SC_CHECK(sameColor(pixelAt(canvas, 30, 37), trace10));   // the jog back
  SC_CHECK(sameColor(pixelAt(canvas, 31, 68), trace10));   // the node
  SC_CHECK(sameColor(pixelAt(canvas, 32, 71), trace10));
  SC_CHECK(sameColor(pixelAt(canvas, 28, 68), design10));  // past the node
  SC_CHECK(sameColor(pixelAt(canvas, 29, 68), core10));    // the lit core
  SC_CHECK(sameColor(pixelAt(canvas, 30, 69), core10));
  SC_CHECK(sameColor(pixelAt(canvas, 31, 69), trace10));   // node past core
  advanceBackgroundBy(renderer, 4000);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 31, 69), core10));    // swelled to 3 x 3
  SC_CHECK(sameColor(pixelAt(canvas, 30, 70), core10));
  SC_CHECK(sameColor(pixelAt(canvas, 27, 67), design10));
  SC_CHECK(sameColor(pixelAt(canvas, 32, 70), trace10));   // the node holds
  SC_CHECK(sameColor(pixelAt(canvas, 29, 71), trace10));
  advanceBackgroundBy(renderer, 3000);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 28, 68), design10));  // quiet again
  SC_CHECK(sameColor(pixelAt(canvas, 30, 70), trace10));
  SC_CHECK(sameColor(pixelAt(canvas, 30, 69), core10));

  // Level 11, Band aurora: the lowest ribbon's staircase - lifts 0, 1, 2,
  // 1, 0 times 8 px over its 40 px steps - with a riding tick, rising one
  // pixel every quarter second, the whole layer together. The ribbons are
  // secondary, so they cross the guard gaps at the same shade.
  renderer.setBackgroundLevel(11);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design11{0x13, 0x17, 0x25, 255};
  const SDL_Color ribbon11{0x28, 0x30, 0x44, 255};
  const SDL_Color tick11{0x67, 0x75, 0x8D, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 0, 87), ribbon11));   // step 0, lift 0
  SC_CHECK(sameColor(pixelAt(canvas, 39, 87), ribbon11));
  SC_CHECK(sameColor(pixelAt(canvas, 40, 79), ribbon11));   // step 1, lift 8
  SC_CHECK(sameColor(pixelAt(canvas, 80, 71), ribbon11));   // step 2, lift 16
  SC_CHECK(sameColor(pixelAt(canvas, 80, 70), design11));   // above the lift
  SC_CHECK(sameColor(pixelAt(canvas, 120, 79), ribbon11));  // step 3, lift 8
  SC_CHECK(sameColor(pixelAt(canvas, 160, 87), ribbon11));  // step 4, lift 0
  SC_CHECK(sameColor(pixelAt(canvas, 200, 79), ribbon11));  // step 5, lift 8
  SC_CHECK(sameColor(pixelAt(canvas, 240, 105), ribbon11)); // step 6 bottom
  SC_CHECK(sameColor(pixelAt(canvas, 343, 87), ribbon11));  // the guard gap
  SC_CHECK(sameColor(pixelAt(canvas, 616, 87), ribbon11));
  SC_CHECK(sameColor(pixelAt(canvas, 250, 300), design11));  // between rows
  SC_CHECK(sameColor(pixelAt(canvas, 11, 25), tick11));      // a riding tick
  SC_CHECK(sameColor(pixelAt(canvas, 12, 26), tick11));
  SC_CHECK(sameColor(pixelAt(canvas, 13, 25), design11));
  advanceBackgroundBy(renderer, 250);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 0, 86), ribbon11));    // one pixel up
  SC_CHECK(sameColor(pixelAt(canvas, 0, 85), design11));    // the row it left
  SC_CHECK(sameColor(pixelAt(canvas, 40, 78), ribbon11));
  SC_CHECK(sameColor(pixelAt(canvas, 11, 24), tick11));

  // Level 12, Long trails: a bright star of the ranked placement, a trail
  // step with its head above, and the layer's shared diagonal - a third
  // of a second moves every element a pixel down and left together.
  renderer.setBackgroundLevel(12);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design12{0x11, 0x16, 0x20, 255};
  const SDL_Color starDim12{0x29, 0x31, 0x44, 255};
  const SDL_Color head12{0x76, 0x84, 0x9B, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 15, 316), starDim12));  // trail step 0
  SC_CHECK(sameColor(pixelAt(canvas, 17, 316), starDim12));
  SC_CHECK(sameColor(pixelAt(canvas, 18, 316), design12));
  SC_CHECK(sameColor(pixelAt(canvas, 11, 318), starDim12));  // step 1
  SC_CHECK(sameColor(pixelAt(canvas, 14, 313), head12));     // its head
  SC_CHECK(sameColor(pixelAt(canvas, 15, 314), head12));
  SC_CHECK(sameColor(pixelAt(canvas, 16, 313), design12));
  SC_CHECK(sameColor(pixelAt(canvas, 806, 481), head12));    // a bright star
  SC_CHECK(sameColor(pixelAt(canvas, 807, 482), head12));
  SC_CHECK(sameColor(pixelAt(canvas, 808, 481), design12));
  SC_CHECK(sameColor(pixelAt(canvas, 192, 316), design12));  // quiet zone
  SC_CHECK(sameColor(pixelAt(canvas, 330, 316), design12));
  advanceBackgroundBy(renderer, 333);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 14, 317), starDim12));  // the diagonal
  SC_CHECK(sameColor(pixelAt(canvas, 15, 316), design12));
  SC_CHECK(sameColor(pixelAt(canvas, 13, 314), head12));

  // The landed Pressure designs of task T082 in the real frame, each at
  // its t = 0 placement after the round reset, with one advance of the
  // clock showing the record's motion at exact pixels.
  //
  // Level 13, Deep grid: the first scrolling rule of the left margin - a
  // 1 px line of #293145 at row 266, the mid row 272 less the 6 px
  // horizon offset - over the first staircase guide at x 6, whose second
  // quarter shifts 1 px toward the canvas center, closed by its 2 x 2
  // accent node at the margin foot. A fifth of a second scrolls the rule
  // set down one pixel; the guides and nodes hold still.
  renderer.setBackgroundLevel(13);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design13{0x12, 0x17, 0x22, 255};
  const SDL_Color line13{0x29, 0x31, 0x45, 255};
  const SDL_Color node13{0x79, 0x86, 0x9B, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 5, 266), line13));   // the rule
  SC_CHECK(sameColor(pixelAt(canvas, 190, 266), line13));
  SC_CHECK(sameColor(pixelAt(canvas, 5, 265), design13));  // above it
  SC_CHECK(sameColor(pixelAt(canvas, 5, 267), design13));  // below it
  SC_CHECK(sameColor(pixelAt(canvas, 772, 266), line13));  // right margin
  SC_CHECK(sameColor(pixelAt(canvas, 6, 100), line13));    // guide, first
  SC_CHECK(sameColor(pixelAt(canvas, 5, 100), design13));  // quarter
  SC_CHECK(sameColor(pixelAt(canvas, 7, 200), line13));    // second quarter
  SC_CHECK(sameColor(pixelAt(canvas, 6, 200), design13));  // stepped in
  SC_CHECK(sameColor(pixelAt(canvas, 9, 408), line13));    // fourth quarter
  SC_CHECK(sameColor(pixelAt(canvas, 41, 100), line13));   // guide 2
  SC_CHECK(sameColor(pixelAt(canvas, 148, 100), line13));  // guide 5
  SC_CHECK(sameColor(pixelAt(canvas, 774, 100), line13));  // right guide
  SC_CHECK(sameColor(pixelAt(canvas, 774, 200), line13));   // steps away:
  SC_CHECK(sameColor(pixelAt(canvas, 775, 200), design13));  // 774, 773
  SC_CHECK(sameColor(pixelAt(canvas, 6, 538), node13));    // the node
  SC_CHECK(sameColor(pixelAt(canvas, 7, 539), node13));
  SC_CHECK(sameColor(pixelAt(canvas, 8, 538), design13));
  SC_CHECK(sameColor(pixelAt(canvas, 774, 538), node13));
  advanceBackgroundBy(renderer, 200);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 5, 267), line13));    // one pixel down
  SC_CHECK(sameColor(pixelAt(canvas, 5, 266), design13));
  SC_CHECK(sameColor(pixelAt(canvas, 772, 279), line13));  // the +6 rule
  SC_CHECK(sameColor(pixelAt(canvas, 6, 100), line13));    // guides hold
  SC_CHECK(sameColor(pixelAt(canvas, 6, 538), node13));

  // Level 14, Fine snow: the falling field of exactly 160 flakes - a 1 px
  // dim flake of #293145 at (5, 119), a 2 x 2 bright flake of #7A879E at
  // (40, 188) - over the three static bands of #293145, 19 rows tall at
  // 30, 55 and 80 percent height. Half a second drops every flake three
  // pixels; the bands hold still.
  renderer.setBackgroundLevel(14);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design14{0x13, 0x19, 0x23, 255};
  const SDL_Color flakeDim14{0x29, 0x31, 0x45, 255};
  const SDL_Color flakeBright14{0x7A, 0x87, 0x9E, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 5, 119), flakeDim14));   // a 1 px flake
  SC_CHECK(sameColor(pixelAt(canvas, 6, 119), design14));
  SC_CHECK(sameColor(pixelAt(canvas, 40, 188), flakeBright14));  // a 2 x 2
  SC_CHECK(sameColor(pixelAt(canvas, 41, 189), flakeBright14));
  SC_CHECK(sameColor(pixelAt(canvas, 42, 188), design14));
  SC_CHECK(sameColor(pixelAt(canvas, 5, 163), flakeDim14));   // band 1 top
  SC_CHECK(sameColor(pixelAt(canvas, 5, 162), design14));
  SC_CHECK(sameColor(pixelAt(canvas, 5, 181), flakeDim14));   // band bottom
  SC_CHECK(sameColor(pixelAt(canvas, 5, 182), design14));
  SC_CHECK(sameColor(pixelAt(canvas, 5, 299), flakeDim14));   // band 2
  SC_CHECK(sameColor(pixelAt(canvas, 5, 435), flakeDim14));   // band 3
  SC_CHECK(sameColor(pixelAt(canvas, 250, 163), flakeDim14));  // stats zone
  SC_CHECK(sameColor(pixelAt(canvas, 330, 163), flakeDim14));  // guard gap
  SC_CHECK(sameColor(pixelAt(canvas, 490, 163), kBoardWell));  // under well
  advanceBackgroundBy(renderer, 500);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 5, 122), flakeDim14));   // three down
  SC_CHECK(sameColor(pixelAt(canvas, 5, 119), design14));
  SC_CHECK(sameColor(pixelAt(canvas, 40, 191), flakeBright14));
  SC_CHECK(sameColor(pixelAt(canvas, 40, 188), design14));
  SC_CHECK(sameColor(pixelAt(canvas, 5, 163), flakeDim14));   // bands hold

  // Level 15, Undertow: the first wave band - a staircase of 20 px steps
  // whose lift runs 0, 1, 2, 1 times 6 px, at base row 152 - with its foam
  // tick of #74839B two rows above the edge, and the second band's
  // mirrored contour at base row 201 dropped 3 px. Half a second slides
  // the whole layer three pixels left; the foam holds its slot.
  renderer.setBackgroundLevel(15);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design15{0x13, 0x19, 0x23, 255};
  const SDL_Color band15{0x2A, 0x31, 0x46, 255};
  const SDL_Color foam15{0x74, 0x83, 0x9B, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 0, 152), band15));   // step 0, lift 0
  SC_CHECK(sameColor(pixelAt(canvas, 19, 189), band15));
  SC_CHECK(sameColor(pixelAt(canvas, 20, 146), band15));   // step 1, lift 6
  SC_CHECK(sameColor(pixelAt(canvas, 40, 140), band15));   // step 2, lift 12
  SC_CHECK(sameColor(pixelAt(canvas, 40, 139), design15));  // above the lift
  SC_CHECK(sameColor(pixelAt(canvas, 60, 146), band15));   // step 3, lift 6
  SC_CHECK(sameColor(pixelAt(canvas, 39, 145), design15));  // between steps
  SC_CHECK(sameColor(pixelAt(canvas, 0, 192), band15));    // band 1, phase 2
  SC_CHECK(sameColor(pixelAt(canvas, 40, 204), band15));   // its lift 0
  SC_CHECK(sameColor(pixelAt(canvas, 40, 203), design15));
  SC_CHECK(sameColor(pixelAt(canvas, 87, 150), foam15));   // the foam tick
  SC_CHECK(sameColor(pixelAt(canvas, 88, 150), foam15));
  SC_CHECK(sameColor(pixelAt(canvas, 87, 151), design15));
  SC_CHECK(sameColor(pixelAt(canvas, 830, 155), foam15));  // right margin
  SC_CHECK(sameColor(pixelAt(canvas, 330, 160), band15));  // guard gap, ok
  SC_CHECK(sameColor(pixelAt(canvas, 250, 160), band15));  // stats zone
  advanceBackgroundBy(renderer, 500);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 37, 140), band15));   // three left
  SC_CHECK(sameColor(pixelAt(canvas, 36, 140), design15));
  SC_CHECK(sameColor(pixelAt(canvas, 39, 141), band15));
  SC_CHECK(sameColor(pixelAt(canvas, 87, 150), foam15));   // foam holds
  SC_CHECK(sameColor(pixelAt(canvas, 0, 189), band15));    // band still on

  // Level 16, Slow pulse: the outermost broken contour of #2A3146 - 2 px
  // bars inset 10 px, the top bar split by a 24 px opening at x 586..609,
  // the left bar by one at rows 23..46 - with its corner tip of #7C88A0,
  // the second contour at inset 36, and the swell: four seconds in, every
  // contour has breathed 24 px outward.
  renderer.setBackgroundLevel(16);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design16{0x14, 0x1A, 0x26, 255};
  const SDL_Color contour16{0x2A, 0x31, 0x46, 255};
  const SDL_Color tip16{0x7C, 0x88, 0xA0, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 30, 10), contour16));  // top bar
  SC_CHECK(sameColor(pixelAt(canvas, 330, 10), contour16));  // past the
  SC_CHECK(sameColor(pixelAt(canvas, 940, 10), contour16));  // stats zone
  // The 24 px opening of the outer contour hides under the board well
  // (the frame cuts the bar at x 344 and 615); the second contour's
  // opening lies in the open: rows 36..37, x 221..244.
  SC_CHECK(sameColor(pixelAt(canvas, 220, 36), contour16));
  SC_CHECK(sameColor(pixelAt(canvas, 221, 36), design16));  // the opening
  SC_CHECK(sameColor(pixelAt(canvas, 232, 37), design16));
  SC_CHECK(sameColor(pixelAt(canvas, 244, 36), design16));
  SC_CHECK(sameColor(pixelAt(canvas, 245, 36), contour16));  // past it
  SC_CHECK(sameColor(pixelAt(canvas, 10, 22), contour16));  // left bar
  SC_CHECK(sameColor(pixelAt(canvas, 10, 23), design16));   // its opening
  SC_CHECK(sameColor(pixelAt(canvas, 10, 46), design16));
  SC_CHECK(sameColor(pixelAt(canvas, 10, 47), contour16));
  SC_CHECK(sameColor(pixelAt(canvas, 50, 36), contour16));  // contour 2's
  SC_CHECK(sameColor(pixelAt(canvas, 35, 36), design16));   // top bar
  SC_CHECK(sameColor(pixelAt(canvas, 490, 10), kBoardWell));  // well covers
  SC_CHECK(sameColor(pixelAt(canvas, 10, 10), tip16));     // the corner tip
  SC_CHECK(sameColor(pixelAt(canvas, 11, 11), tip16));
  SC_CHECK(sameColor(pixelAt(canvas, 948, 10), tip16));
  SC_CHECK(sameColor(pixelAt(canvas, 10, 532), tip16));
  SC_CHECK(sameColor(pixelAt(canvas, 62, 62), tip16));     // contour 3
  SC_CHECK(sameColor(pixelAt(canvas, 63, 63), tip16));
  SC_CHECK(sameColor(pixelAt(canvas, 64, 64), design16));  // past its bar
  advanceBackgroundBy(renderer, 4000);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 34, 34), tip16));     // swelled 24 out
  SC_CHECK(sameColor(pixelAt(canvas, 40, 34), contour16));
  SC_CHECK(sameColor(pixelAt(canvas, 30, 34), design16));  // inside contour
  SC_CHECK(sameColor(pixelAt(canvas, 30, 10), design16));  // old bar gone
  SC_CHECK(sameColor(pixelAt(canvas, 86, 86), tip16));     // contour 3 in
  SC_CHECK(sameColor(pixelAt(canvas, 85, 85), design16));
  advanceBackgroundBy(renderer, 4000);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 30, 10), contour16));  // back at rest
  SC_CHECK(sameColor(pixelAt(canvas, 34, 34), design16));  // inside ring 0
  SC_CHECK(sameColor(pixelAt(canvas, 50, 36), contour16));  // ring 1 again
  SC_CHECK(sameColor(pixelAt(canvas, 62, 62), tip16));

  // The landed Peak designs of task T083 in the real frame, each at its
  // t = 0 placement after the round reset, with one advance of the clock
  // showing the record's motion at exact pixels.
  //
  // Level 17, Rising ember: the ranked embers of the warm accent #897A72 -
  // a 1 x 2 px ember at (915, 288), a 2 x 2 ember at (9, 520) - rising past
  // the static stepped columns of #2A3040 in the lower fifth: the first
  // column's block at (4, 534) with its half-width step six rows above.
  // Half a second raises every ember four pixels; the columns hold.
  renderer.setBackgroundLevel(17);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design17{0x17, 0x19, 0x1F, 255};
  const SDL_Color ember17{0x89, 0x7A, 0x72, 255};
  const SDL_Color column17{0x2A, 0x30, 0x40, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 5, 5), design17));     // quiet margin
  SC_CHECK(sameColor(pixelAt(canvas, 250, 300), design17));  // stats zone
  SC_CHECK(sameColor(pixelAt(canvas, 330, 300), design17));  // guard gap
  SC_CHECK(sameColor(pixelAt(canvas, 915, 288), ember17));   // a 1 x 2 ember
  SC_CHECK(sameColor(pixelAt(canvas, 916, 288), design17));
  SC_CHECK(sameColor(pixelAt(canvas, 9, 520), ember17));     // a 2 x 2 ember
  SC_CHECK(sameColor(pixelAt(canvas, 10, 521), ember17));
  SC_CHECK(sameColor(pixelAt(canvas, 4, 534), column17));    // the block
  SC_CHECK(sameColor(pixelAt(canvas, 4, 528), column17));    // its step
  SC_CHECK(sameColor(pixelAt(canvas, 4, 527), design17));   // above both
  advanceBackgroundBy(renderer, 500);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 915, 284), ember17));   // four pixels up
  SC_CHECK(sameColor(pixelAt(canvas, 915, 288), design17));
  SC_CHECK(sameColor(pixelAt(canvas, 4, 534), column17));    // columns hold

  // Level 18, Storm lines: the first stepped trail - five 4 x 2 px
  // segments of #2A3146 stepping 4 px left and 2 px down from (96, 44) -
  // beside a dim 2 x 4 dash of #2A3146 at (901, 2) and a bright 2 x 4 dash
  // of #8290A7 at (38, 9). A quarter second drops the whole layer two
  // pixels, dashes and trails together.
  renderer.setBackgroundLevel(18);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design18{0x13, 0x19, 0x25, 255};
  const SDL_Color dashDim18{0x2A, 0x31, 0x46, 255};
  const SDL_Color dashBright18{0x82, 0x90, 0xA7, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 96, 44), dashDim18));   // trail head
  SC_CHECK(sameColor(pixelAt(canvas, 88, 48), dashDim18));   // third step
  SC_CHECK(sameColor(pixelAt(canvas, 99, 44), dashDim18));   // 4 px wide
  SC_CHECK(sameColor(pixelAt(canvas, 100, 44), design18));
  SC_CHECK(sameColor(pixelAt(canvas, 901, 2), dashDim18));   // dim dash
  SC_CHECK(sameColor(pixelAt(canvas, 901, 5), dashDim18));
  SC_CHECK(sameColor(pixelAt(canvas, 901, 6), design18));    // 4 px tall
  SC_CHECK(sameColor(pixelAt(canvas, 38, 9), dashBright18));  // bright dash
  SC_CHECK(sameColor(pixelAt(canvas, 38, 12), dashBright18));
  SC_CHECK(sameColor(pixelAt(canvas, 38, 13), design18));
  SC_CHECK(sameColor(pixelAt(canvas, 330, 300), design18));  // guard gap
  advanceBackgroundBy(renderer, 250);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 901, 4), dashDim18));   // two down
  SC_CHECK(sameColor(pixelAt(canvas, 901, 2), design18));
  SC_CHECK(sameColor(pixelAt(canvas, 96, 46), dashDim18));   // trail too
  SC_CHECK(sameColor(pixelAt(canvas, 38, 11), dashBright18));

  // Level 19, Compression: the outermost band's top bar - 3 px of #2A3146
  // at rows 8..10, split by its 20 px opening under the board and cut by
  // the frame ring - with the accent ticks of #8591A8 riding it every
  // 32 px. Six seconds in, the nest has pressed its 54 px: the fifth
  // band's bars sit 5 px from the canvas edges, the outer bands have
  // slid off them, and the ticks rode in with the bar. Six seconds later
  // the nest rests again.
  renderer.setBackgroundLevel(19);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design19{0x17, 0x1C, 0x28, 255};
  const SDL_Color band19{0x2A, 0x31, 0x46, 255};
  const SDL_Color tick19{0x85, 0x91, 0xA8, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 8, 8), band19));    // the top bar
  SC_CHECK(sameColor(pixelAt(canvas, 12, 8), band19));
  SC_CHECK(sameColor(pixelAt(canvas, 12, 11), design19));  // below it
  SC_CHECK(sameColor(pixelAt(canvas, 14, 8), tick19));   // a tick on it
  SC_CHECK(sameColor(pixelAt(canvas, 17, 8), band19));   // past the tick
  SC_CHECK(sameColor(pixelAt(canvas, 330, 300), band19));  // a band crosses
  // the guard gap at its secondary shade, the cap itself
  SC_CHECK(sameColor(pixelAt(canvas, 500, 8), kBoardWell));  // the bar hides
  SC_CHECK(sameColor(pixelAt(canvas, 344, 8), kFrame));     // under the well
  advanceBackgroundBy(renderer, 6000);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 5, 5), band19));    // pressed to 5 px
  SC_CHECK(sameColor(pixelAt(canvas, 9, 9), design19));  // inside the band
  SC_CHECK(sameColor(pixelAt(canvas, 8, 8), design19));  // outer band gone
  SC_CHECK(sameColor(pixelAt(canvas, 344, 34), kFrame));  // ring still pure
  advanceBackgroundBy(renderer, 6000);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 8, 8), band19));    // rested again
  SC_CHECK(sameColor(pixelAt(canvas, 14, 8), tick19));

  // Level 20, Collapse: the ranked debris of the far edges - a 3 x 7 px
  // fragment of #2A3146 at (184, 2), a 1 x 3 bright fragment of #8A94AC
  // at (950, 124) - falling past the static stepped slabs: the 32 x 13 px
  // slab at (148, 6) with its half-width step on top. Half a second drops
  // every fragment five pixels; the slabs hold.
  renderer.setBackgroundLevel(20);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  const SDL_Color design20c{0x19, 0x1E, 0x2B, 255};
  const SDL_Color debrisDim20{0x2A, 0x31, 0x46, 255};
  const SDL_Color debrisBright20{0x8A, 0x94, 0xAC, 255};
  SC_CHECK(sameColor(pixelAt(canvas, 184, 2), debrisDim20));  // 3 x 7 px
  SC_CHECK(sameColor(pixelAt(canvas, 187, 2), design20c));
  SC_CHECK(sameColor(pixelAt(canvas, 950, 124), debrisBright20));  // 1 x 3
  SC_CHECK(sameColor(pixelAt(canvas, 950, 127), design20c));
  SC_CHECK(sameColor(pixelAt(canvas, 148, 6), debrisDim20));  // the slab
  SC_CHECK(sameColor(pixelAt(canvas, 147, 6), design20c));
  SC_CHECK(sameColor(pixelAt(canvas, 148, 1), debrisDim20));  // its step
  SC_CHECK(sameColor(pixelAt(canvas, 148, 0), design20c));
  SC_CHECK(sameColor(pixelAt(canvas, 180, 6), design20c));  // past 32 px
  SC_CHECK(sameColor(pixelAt(canvas, 330, 300), design20c));  // guard gap
  SC_CHECK(sameColor(pixelAt(canvas, 490, 400), kBoardWell));
  advanceBackgroundBy(renderer, 500);
  renderer.draw(game);
  SC_CHECK(sameColor(pixelAt(canvas, 184, 7), debrisDim20));  // five down
  SC_CHECK(sameColor(pixelAt(canvas, 184, 2), design20c));
  SC_CHECK(sameColor(pixelAt(canvas, 950, 129), debrisBright20));
  SC_CHECK(sameColor(pixelAt(canvas, 148, 6), debrisDim20));  // slab holds

  // Pause keeps the selected wallpaper beneath the veil: the paused margin
  // reads as the veiled design, not as the veiled flat ground, and a frozen
  // pause redraws the same bytes while the clock holds. Design 17 gives the
  // comparison room - its base sits clear of the flat ground in every
  // channel - and its landed grammar holds a base-only stretch of the left
  // margin at the t = 0 placement the frozen clock pins: the block at
  // x 96..135, above the lower-fifth columns and clear of the ranked
  // embers.
  renderer.setBackgroundLevel(17);
  renderer.resetBackgroundRound();
  renderer.draw(game);
  SC_CHECK(areaIsOnly(canvas, SDL_Rect{96, 0, 40, 40}, design17));
  game.tick(TickInput{});
  TickInput pause;
  pause.game.pausePressed = true;
  game.tick(pause);
  game.tick(TickInput{});
  SC_CHECK(game.scene() == Scene::Paused);
  renderer.draw(game);
  const SDL_Color veiledDesign = pixelAt(canvas, 100, 10);
  SC_CHECK(!sameColor(veiledDesign, design17));  // the veil mixed it
  SC_CHECK(!sameColor(veiledDesign, kBackground));
  std::vector<Uint32> pausedFrame(
      static_cast<std::size_t>(canvas.surface->w) * canvas.surface->h);
  SDL_LockSurface(canvas.surface);
  std::memcpy(pausedFrame.data(), canvas.surface->pixels,
              pausedFrame.size() * sizeof(Uint32));
  SDL_UnlockSurface(canvas.surface);
  // The freeze the loop applies on pause: half a second of wall time drops,
  // and the frozen frame redraws the same bytes.
  renderer.freezeBackgroundClock(true);
  renderer.advanceBackgroundClock(250);
  renderer.advanceBackgroundClock(250);
  renderer.draw(game);
  SDL_LockSurface(canvas.surface);
  const bool pausedStable =
      std::memcmp(pausedFrame.data(), canvas.surface->pixels,
                  pausedFrame.size() * sizeof(Uint32)) == 0;
  SDL_UnlockSurface(canvas.surface);
  SC_CHECK(pausedStable);
  renderer.freezeBackgroundClock(false);
  // The same paused frame over Off reads as the flat ground alone, so the
  // two differ: the design really sits beneath the overlay.
  renderer.setBackgroundMode(backgrounds::Mode::Off);
  renderer.draw(game);
  const SDL_Color veiledFlat = pixelAt(canvas, 100, 10);
  SC_CHECK(!sameColor(veiledFlat, veiledDesign));
  renderer.setBackgroundMode(backgrounds::Mode::Auto);

  // The game-over overlay keeps the wallpaper beneath it the same way.
  Game over;
  over.openTitle(4242);
  TickInput select;
  select.menu.selectPressed = true;
  over.tick(select);
  over.tick(TickInput{});
  forceGameOver(over);
  SC_CHECK(over.scene() == Scene::GameOver);
  renderer.setBackgroundLevel(5);
  renderer.resetBackgroundRound();
  renderer.draw(over);
  SC_CHECK(!sameColor(pixelAt(canvas, 5, 5), design5));
  SC_CHECK(!sameColor(pixelAt(canvas, 5, 5), kBackground));

  // Grayscale keeps the wallpaper palette exactly: the pieces gray out
  // and the design does not. The whole loud margin - base, drifting
  // stars, dust - redraws byte for byte while the pieces gray inside the
  // well.
  Game plain;
  plain.start(4242);
  renderer.setBackgroundLevel(5);
  renderer.resetBackgroundRound();
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAt(canvas, 18, 340), starBright5));
  std::vector<Uint32> colorMargin(
      static_cast<std::size_t>(canvas.surface->w) * canvas.surface->h);
  SDL_LockSurface(canvas.surface);
  std::memcpy(colorMargin.data(), canvas.surface->pixels,
              colorMargin.size() * sizeof(Uint32));
  SDL_UnlockSurface(canvas.surface);
  renderer.setGrayscale(true);
  renderer.draw(plain);
  bool grayMarginStable = true;
  for (int y = 0; y < kCanvasHeight && grayMarginStable; ++y) {
    for (int x = 0; x < 192; ++x) {
      const Uint32 before = colorMargin[y * canvas.surface->w + x];
      SDL_LockSurface(canvas.surface);
      const Uint32 after =
          static_cast<const Uint32*>(canvas.surface->pixels)[y * canvas.surface->w + x];
      SDL_UnlockSurface(canvas.surface);
      if (before != after) {
        grayMarginStable = false;
        break;
      }
    }
  }
  SC_CHECK(grayMarginStable);
  renderer.setGrayscale(false);

  // The menus keep their flat ground at any level and mode: the title page
  // never carries the play wallpaper.
  Game title;
  title.openTitle(4242);
  title.tick(TickInput{});
  renderer.setBackgroundLevel(20);
  renderer.draw(title);
  SC_CHECK(sameColor(pixelAt(canvas, 5, 5), kBackground));
  SC_CHECK(sameColor(pixelAt(canvas, 955, 539), kBackground));

  // Portrait draws the same design in its own zones: the loud side margins,
  // the quiet HUD strip, and the bottom strip all carry the base where the
  // landed Deep grid grammar of design 13 draws nothing - beside its rules
  // and guides - and the well and ring stay pure through the rotation.
  renderer.setOrientation(Orientation::Portrait);
  renderer.setBackgroundMode(backgrounds::Mode::Fixed);
  renderer.setBackgroundPreference(13);
  renderer.draw(plain);  // a live round: the wallpaper shows un-veiled
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(5, 500)), design13));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(539, 500)), design13));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(272, 5)), design13));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(272, 945)), design13));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(290, 700)),
                     kBoardWell));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(71, 500)), kFrame));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(472, 500)), kFrame));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(64, 500)), design13));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(478, 500)), design13));

  // The Still ground in the turned frame: the corner steps of design 1 land
  // in the portrait loud margins - the outer 91 x 213, the inner 56 x 132 -
  // and the quiet HUD strip above the board holds the base alone.
  renderer.setBackgroundPreference(1);
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(5, 5)), ink1));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(55, 131)), ink1));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(92, 0)), base1));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(70, 212)), ink1));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(30, 220)), base1));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(30, 500)), base1));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(500, 700)), base1));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(5, 900)), ink1));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(530, 500)), base1));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(272, 60)), base1));
  // A same-frame byte comparison holds the whole turned design still: the
  // static chapter redraws identical bytes at any clock value.
  std::vector<Uint32> stillFrame(
      static_cast<std::size_t>(canvas.surface->w) * canvas.surface->h);
  SDL_LockSurface(canvas.surface);
  std::memcpy(stillFrame.data(), canvas.surface->pixels,
              stillFrame.size() * sizeof(Uint32));
  SDL_UnlockSurface(canvas.surface);
  advanceBackgroundBy(renderer, 40000);
  renderer.draw(plain);
  SDL_LockSurface(canvas.surface);
  const bool stillStable =
      std::memcmp(stillFrame.data(), canvas.surface->pixels,
                  stillFrame.size() * sizeof(Uint32)) == 0;
  SDL_UnlockSurface(canvas.surface);
  SC_CHECK(stillStable);
  renderer.setBackgroundPreference(5);

  // The Stir design of level 5 in the turned frame: its stars sit in the
  // portrait loud margins at the t = 0 placement, sink one pixel over
  // half a second, and the dust beneath holds still.
  renderer.setBackgroundMode(backgrounds::Mode::Auto);
  renderer.setBackgroundLevel(5);
  renderer.resetBackgroundRound();
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(18, 459)),
                     starBright5));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(19, 460)), design5));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(38, 243)),
                     starBright5));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(513, 231)),
                     starBright5));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(513, 232)),
                     starBright5));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(514, 232)), design5));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(513, 230)), design5));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(19, 661)), dust5));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(21, 661)), dust5));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(22, 661)), design5));
  advanceBackgroundBy(renderer, 500);
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(18, 460)),
                     starBright5));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(18, 459)), design5));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(513, 233)),
                     starBright5));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(513, 231)), design5));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(19, 661)), dust5));

  // The Band aurora design of the Build chapter in the turned frame: the
  // lowest ribbon's staircase in the portrait loud margins - the base row
  // 154, the 8 px lift of the next step - rising one pixel over a quarter
  // second, and the frame ring cutting the middle steps.
  renderer.setBackgroundLevel(11);
  renderer.resetBackgroundRound();
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(0, 154)), ribbon11));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(39, 154)), ribbon11));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(40, 146)), ribbon11));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(40, 145)), design11));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(70, 146)), ribbon11));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(72, 146)),
                     kBoardWell));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(64, 154)),
                     ribbon11));  // the ribbons cross the quiet guard gap
                                 // at their secondary shade
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(14, 853)), tick11));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(15, 854)), tick11));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(16, 853)), design11));
  advanceBackgroundBy(renderer, 250);
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(0, 153)), ribbon11));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(0, 152)), design11));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(14, 852)), tick11));

  // The Deep grid design of the Pressure chapter in the turned frame: the
  // first scrolling rule at portrait row 474 inside both side strips, the
  // first staircase guide at x 6 over the strip quarters, its node at the
  // strip foot, and the one-pixel scroll of a fifth of a second.
  renderer.setBackgroundLevel(13);
  renderer.resetBackgroundRound();
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(5, 474)), line13));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(519, 474)), line13));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(5, 473)), design13));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(6, 200)), line13));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(7, 400)), line13));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(6, 916)), node13));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(491, 200)), line13));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(290, 700)),
                     kBoardWell));
  advanceBackgroundBy(renderer, 200);
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(5, 475)), line13));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(5, 474)), design13));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(6, 200)), line13));

  // The Undertow design in the turned frame: the first wave band's
  // staircase in the portrait side strips - base row 269, the next step
  // lifted 6 - sliding three pixels left over half a second while the
  // bands stay on.
  renderer.setBackgroundLevel(15);
  renderer.resetBackgroundRound();
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(10, 300)), band15));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(25, 270)), band15));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(25, 262)),
                     design15));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(71, 300)),
                     kFrame));  // the ring cuts the middle steps
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(290, 700)),
                     kBoardWell));
  advanceBackgroundBy(renderer, 500);
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(18, 264)), band15));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(18, 263)), band15));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(18, 262)),
                     design15));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(10, 300)), band15));

  // The Slow pulse design in the turned frame: the outer contour's top
  // bar at rows 10..11 with its bottom-strip corner tips, breathing to
  // inset 34 four seconds in. The inner contours hide behind the tall
  // board, exactly the record's clipped frame.
  renderer.setBackgroundLevel(16);
  renderer.resetBackgroundRound();
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(50, 10)), contour16));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(50, 12)), design16));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(10, 949)), tip16));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(532, 949)), tip16));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(50, 34)), design16));
  advanceBackgroundBy(renderer, 4000);
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(50, 34)), contour16));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(34, 925)), tip16));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(50, 10)), design16));


  // The Peak designs in the turned frame: the ember field and columns of
  // level 17 in the tall side strips and the flat bottom row, the trail
  // and bright dash of level 18, the pressed bands of level 19 - whose
  // ticks never draw in portrait, the loud zones starting 119 rows below
  // the top bar - and the debris and stepped slabs of level 20.
  renderer.setBackgroundLevel(17);
  renderer.resetBackgroundRound();
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(37, 400)), ember17));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(38, 400)), design17));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(4, 911)),
                     column17));   // the first column block
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(4, 905)),
                     column17));   // its step
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(390, 925)),
                     column17));   // the flat bottom row
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(272, 60)),
                     design17));   // the quiet HUD strip holds the base
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(290, 700)),
                     kBoardWell));
  advanceBackgroundBy(renderer, 500);
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(37, 396)), ember17));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(37, 400)), design17));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(4, 911)),
                     column17));   // columns hold

  // Level 18 in the turned frame: the first trail stepping down the left
  // strip from (30, 183), a bright 2 x 6 dash of #8290A7 in the bottom
  // strip, both two pixels lower after a quarter second.
  renderer.setBackgroundLevel(18);
  renderer.resetBackgroundRound();
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(30, 183)),
                     dashDim18));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(26, 185)),
                     dashDim18));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(33, 185)), design18));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(314, 942)),
                     dashBright18));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(314, 947)),
                     dashBright18));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(316, 942)),
                     design18));
  advanceBackgroundBy(renderer, 250);
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(314, 944)),
                     dashBright18));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(314, 942)), design18));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(30, 185)),
                     dashDim18));

  // Level 19 in the turned frame: the outermost band's top bar at rows
  // 8..10 with no tick beside it - the pressed bar never reaches the loud
  // zones - and the bottom bar at rows 949..951, pressing to 5 px at the
  // crest of the half cycle.
  renderer.setBackgroundLevel(19);
  renderer.resetBackgroundRound();
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(50, 8)), band19));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(50, 11)), design19));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(200, 949)), band19));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(200, 952)), design19));
  // the bands cross the quiet HUD strip at their secondary shade, the cap
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(272, 60)), band19));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(290, 700)),
                     kBoardWell));
  advanceBackgroundBy(renderer, 6000);
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(5, 5)), band19));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(9, 9)), design19));

  // Level 20 in the turned frame: the ranked debris falling past the
  // stepped slabs of the side strips and the bottom strip - the 32 x 13
  // slab at (15, 125) with its half-width step on top - five pixels lower
  // after half a second while the slabs hold.
  renderer.setBackgroundLevel(20);
  renderer.resetBackgroundRound();
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(525, 669)),
                     debrisDim20));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(525, 672)), design20c));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(15, 125)),
                     debrisDim20));  // the side-strip slab
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(14, 125)), design20c));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(15, 120)),
                     debrisDim20));  // its step
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(15, 119)), design20c));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(47, 125)), design20c));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(340, 930)),
                     debrisDim20));  // a bottom-strip slab
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(272, 60)), design20c));
  advanceBackgroundBy(renderer, 500);
  renderer.draw(plain);
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(525, 674)),
                     debrisDim20));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(525, 669)), design20c));
  SC_CHECK(sameColor(pixelAtPoint(canvas, portraitPixel(15, 125)),
                     debrisDim20));  // the slab holds

  // A same-session orientation turn rebuilds the wallpaper geometry without
  // leaking state: landscape bytes before the turn return after it.
  renderer.setOrientation(Orientation::Landscape);
  renderer.draw(plain);
  std::vector<Uint32> beforeTurn(
      static_cast<std::size_t>(canvas.surface->w) * canvas.surface->h);
  SDL_LockSurface(canvas.surface);
  std::memcpy(beforeTurn.data(), canvas.surface->pixels,
              beforeTurn.size() * sizeof(Uint32));
  SDL_UnlockSurface(canvas.surface);
  renderer.setOrientation(Orientation::Portrait);
  renderer.draw(plain);
  renderer.setOrientation(Orientation::Landscape);
  renderer.draw(plain);
  SDL_LockSurface(canvas.surface);
  const bool turnStable =
      std::memcmp(beforeTurn.data(), canvas.surface->pixels,
                  beforeTurn.size() * sizeof(Uint32)) == 0;
  SDL_UnlockSurface(canvas.surface);
  SC_CHECK(turnStable);

  // The default mirrors hold: Auto, design 1.
  Renderer fresh(canvas.renderer);
  SC_CHECK(fresh.backgroundMode() == backgrounds::Mode::Auto);
  SC_CHECK(fresh.backgroundPreference() == 1);
  SC_CHECK(fresh.backgroundLevel() == 1);
  fresh.setBackgroundPreference(0);
  SC_CHECK(fresh.backgroundPreference() == 1);
  fresh.setBackgroundPreference(99);
  SC_CHECK(fresh.backgroundPreference() == 20);
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
  run_piece_design_checks();
  run_preview_checks();
  run_text_checks();
  run_focused_hud_checks();
  run_read_only_and_scaling_checks();
  run_letterbox_canvas_checks();
  run_title_screen_checks();
  run_controls_screen_checks();
  run_rebind_screen_checks();
  run_options_screen_checks();
  run_music_options_screen_checks();
  run_background_options_screen_checks();
  run_pause_overlay_checks();
  run_gameover_overlay_checks();
  run_portrait_board_checks();
  run_portrait_hud_and_screens_checks();
  run_portrait_landscape_invariance_checks();
  run_background_checks();

  SDL_Quit();
  stackfall_test::print_summary();
  return stackfall_test::g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
