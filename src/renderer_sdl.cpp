// The procedural renderer of milestone M2 (tasks T010 and T011,
// specification sections 4.7, 6.1, and 6.2).
//
// Every frame comes from filled rectangles and lines: the board well, its
// faint grid and frame, the settled cells in their stored colors, the ghost,
// the active piece, the hold and next previews at the smaller preview cell,
// and the score fields and control hints drawn as glyphs. The section 4.7
// screens draw the same way: the title and controls pages from glyphs and
// preview pieces alone, the pause and game-over overlays as one translucent
// veil plus a solid panel over the frozen play scene. Nothing loads a
// texture, a surface, or an image, so the build carries no asset files at
// all. draw() reads the round through the const queries of Game only, in the
// order of section 6.2, and never touches a mutator or a random source
// (specification section 7).
//
// Cells above the visible field are never drawn: a piece that still sits in
// the hidden rows is not on screen, which keeps the spawn rows invisible
// exactly as the rules core models them (specification section 4.1).
//
// Beside the scene itself this file owns the letterbox canvas of section 6.1:
// applyCanvas(), beginFrame(), and readOutputPixels() carry the 960x544
// canvas onto any window size, paint the letterbox bars black, and capture
// the whole output. The game binary's platform layer calls them around
// Renderer::draw.

#include "stackfall/renderer_sdl.hpp"

#include <cstdio>

#include "stackfall/font.hpp"
#include "stackfall/pieces.hpp"

namespace stackfall {
namespace {

// Text scales: one font pixel becomes a scale x scale square. Labels sit at
// the small scale in the dim color, values at twice that in the bright color,
// which keeps the score readable on the 5-inch panel without filling the side
// column. The hints draw at the label scale as well: at scale 1 their glyphs
// measured 7 px, which falls to 4 px in a 640x480 letterbox, so the hints now
// match the 14 px label height (design review finding
// control-hint-glyph-scale).
constexpr int kLabelScale = 2;
constexpr int kValueScale = 2;
constexpr int kHintScale = 2;

// The control hints of the left panel, in section 5 order: the RG503 control
// and what it does. The play panel stays gamepad-first on purpose: the full
// gamepad and keyboard tables of the controls screen would not fit beside the
// board, and the controls page of task T011 carries both columns.
struct Hint {
  const char* control;
  const char* action;
};

constexpr Hint kHints[] = {
    {"LEFT/RIGHT", "MOVE"},  {"DOWN", "SOFT DROP"}, {"A", "ROTATE CW"},
    {"B", "ROTATE CCW"},     {"X", "HARD DROP"},    {"Y OR L1", "HOLD"},
    {"START", "PAUSE"},
};
constexpr int kHintCount = static_cast<int>(sizeof(kHints) / sizeof(kHints[0]));

// The section 4.7 menu items, drawn in listed order; the cursor indexes of
// Game address them by position.
constexpr const char* kTitleItems[] = {"START GAME", "CONTROLS", "QUIT"};
constexpr const char* kPauseItems[] = {"RESUME", "RESTART", "QUIT TO TITLE"};
constexpr const char* kOverItems[] = {"PLAY AGAIN", "TITLE"};

// The controls screen rows: the section 5 gamepad mapping, its keyboard
// counterpart, and the action. One table, next to the two mapping tables of
// src/input_sdl.cpp, so a mapping review reads them together.
struct ControlRow {
  const char* gamepad;
  const char* keyboard;
  const char* action;
};

constexpr ControlRow kControlRows[] = {
    {"D-PAD LEFT/RIGHT", "LEFT/RIGHT ARROWS", "MOVE"},
    {"D-PAD DOWN", "DOWN ARROW", "SOFT DROP"},
    {"A", "X OR UP ARROW", "ROTATE CW"},
    {"B", "Z", "ROTATE CCW"},
    {"X", "SPACE", "HARD DROP"},
    {"Y OR L1", "C OR LEFT SHIFT", "HOLD"},
    {"START", "ENTER OR ESC", "PAUSE OR RESUME"},
    {"D-PAD UP/DOWN", "UP/DOWN ARROWS", "MENU CURSOR"},
    {"A", "ENTER", "SELECT"},
    {"B", "ESC", "BACK"},
};
constexpr int kControlRowCount =
    static_cast<int>(sizeof(kControlRows) / sizeof(kControlRows[0]));

// Formats an unsigned 64-bit score in decimal. The buffer always holds a
// terminated string, and snprintf clamps rather than overruns, so a score of
// any size renders safely (specification section 4.6).
void formatUnsigned(std::uint64_t value, char* buffer, std::size_t capacity) {
  std::snprintf(buffer, capacity, "%llu",
                static_cast<unsigned long long>(value));
}

// The top-left canvas pixel of board cell (x, y).
constexpr int cellX(int x) { return kBoardOriginX + x * kBoardCellSize; }
constexpr int cellY(int y) { return kBoardOriginY + y * kBoardCellSize; }

}  // namespace

Renderer::Renderer(SDL_Renderer* renderer) noexcept : renderer_(renderer) {}

void Renderer::setShowGrid(bool show) noexcept { showGrid_ = show; }

bool Renderer::showGrid() const noexcept { return showGrid_; }

// ---------------------------------------------------------------------------
// The letterbox canvas (section 6.1, design review finding
// letterbox-scene-centered).
// ---------------------------------------------------------------------------

bool applyCanvas(SDL_Renderer* renderer) {
  if (renderer == nullptr) {
    return false;
  }
  return SDL_RenderSetLogicalSize(renderer, kCanvasWidth, kCanvasHeight) == 0;
}

// One output pixel of pure black, the color of the letterbox bars. No scene
// color reaches it: the darkest ground the scene draws is the well at
// (10, 12, 19), so the bars read as bars.
constexpr SDL_Color kLetterboxBlack{0, 0, 0, 255};

void beginFrame(SDL_Renderer* renderer) {
  if (renderer == nullptr) {
    return;
  }
  int width = 0;
  int height = 0;
  if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) {
    return;
  }
  if (width == kCanvasWidth && height == kCanvasHeight) {
    // The native size has no bars: the scene fills the canvas, which is the
    // whole output, so a base here would only repeat that fill.
    return;
  }
  // Drop the canvas transform, so one clear covers the whole output, then put
  // the canvas back. The renderer never draws outside the letterbox viewport,
  // so without this base the bars keep whatever the output buffers held from
  // before the resize.
  SDL_RenderSetLogicalSize(renderer, 0, 0);
  SDL_SetRenderDrawColor(renderer, kLetterboxBlack.r, kLetterboxBlack.g,
                         kLetterboxBlack.b, kLetterboxBlack.a);
  SDL_RenderClear(renderer);
  applyCanvas(renderer);
}

bool readOutputPixels(SDL_Renderer* renderer, SDL_Surface* surface) {
  if (renderer == nullptr || surface == nullptr) {
    return false;
  }
  // Read in the device space of the whole output, not the logical space of
  // the letterbox viewport, then state the canvas again for the next frame.
  SDL_RenderSetLogicalSize(renderer, 0, 0);
  const bool ok =
      SDL_RenderReadPixels(renderer, nullptr, surface->format->format,
                           surface->pixels, surface->pitch) == 0;
  applyCanvas(renderer);
  return ok;
}

// ---------------------------------------------------------------------------
// Primitives. Each one sets its own color and clips nothing; SDL applies the
// logical-size scaling, so these coordinates are logical pixels.
// ---------------------------------------------------------------------------

void Renderer::fillRect(SDL_Color color, int x, int y, int w, int h) const {
  if (renderer_ == nullptr || w <= 0 || h <= 0) {
    return;
  }
  SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
  const SDL_Rect rect{x, y, w, h};
  SDL_RenderFillRect(renderer_, &rect);
}

void Renderer::drawRectOutline(SDL_Color color, int x, int y, int w,
                               int h) const {
  if (renderer_ == nullptr || w <= 0 || h <= 0) {
    return;
  }
  SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
  const SDL_Rect rect{x, y, w, h};
  SDL_RenderDrawRect(renderer_, &rect);
}

// One block: a dark one-pixel outline, the flat fill, a lighter top and left
// edge, and a darker bottom and right edge. Straight rectangles and lines
// give the flat geometric look of section 6.2 without any shading texture.
void Renderer::drawCell(int x, int y, int size,
                        const CellPalette& palette) const {
  fillRect(palette.edge, x, y, size, size);
  fillRect(palette.fill, x + 1, y + 1, size - 2, size - 2);
  SDL_SetRenderDrawColor(renderer_, palette.light.r, palette.light.g,
                         palette.light.b, palette.light.a);
  SDL_RenderDrawLine(renderer_, x + 1, y + 1, x + size - 2, y + 1);
  SDL_RenderDrawLine(renderer_, x + 1, y + 1, x + 1, y + size - 2);
  SDL_SetRenderDrawColor(renderer_, palette.dark.r, palette.dark.g,
                         palette.dark.b, palette.dark.a);
  SDL_RenderDrawLine(renderer_, x + 1, y + size - 2, x + size - 2, y + size - 2);
  SDL_RenderDrawLine(renderer_, x + size - 2, y + 1, x + size - 2, y + size - 2);
}

// The ghost draws as a hollow outline in a fraction of the piece color
// (specification section 6.2, "same piece outline or dim fill").
void Renderer::drawGhostCell(int x, int y, int size, SDL_Color color) const {
  drawRectOutline(color, x, y, size, size);
}

// ---------------------------------------------------------------------------
// Glyphs.
// ---------------------------------------------------------------------------

void Renderer::drawText(int x, int y, int scale, SDL_Color color,
                        const char* text) const {
  if (renderer_ == nullptr || text == nullptr || scale <= 0) {
    return;
  }
  SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
  int penX = x;
  for (const char* p = text; *p != '\0'; ++p) {
    const Glyph& glyph = glyphFor(*p);
    for (int row = 0; row < kGlyphRows; ++row) {
      for (int column = 0; column < kGlyphColumns; ++column) {
        if (glyphPixel(glyph, column, row)) {
          const SDL_Rect rect{penX + column * scale, y + row * scale, scale,
                              scale};
          SDL_RenderFillRect(renderer_, &rect);
        }
      }
    }
    penX += kGlyphAdvance * scale;
  }
}

void Renderer::drawTextCentered(int centerX, int y, int scale, SDL_Color color,
                                const char* text) const {
  drawText(centerX - textWidth(scale, text) / 2, y, scale, color, text);
}

// ---------------------------------------------------------------------------
// The frame. draw() picks the scene of section 4.7; the play scene keeps the
// draw order of specification section 6.2 and the two overlays stack on top
// of its frozen frame.
// ---------------------------------------------------------------------------

void Renderer::draw(const Game& game) {
  switch (game.scene()) {
    case Scene::Boot:
    case Scene::Title:
      drawTitleScreen(game);
      return;
    case Scene::Controls:
      drawControlsScreen();
      return;
    case Scene::Playing:
      drawPlayScene(game);
      return;
    case Scene::Paused:
      drawPlayScene(game);
      drawPauseOverlay(game);
      return;
    case Scene::GameOver:
      drawPlayScene(game);
      drawGameOverOverlay(game);
      return;
  }
}

void Renderer::drawPlayScene(const Game& game) const {
  drawBackground();      // 1. background and the optional faint grid
  if (showGrid_) {
    drawGridLines();
  }
  drawBoardFrame();          // 2. board border ...
  drawSettledCells(game);    //    ... and settled cells in stored colors
  drawGhostPiece(game);      // 3. ghost below the active piece
  drawActivePiece(game);     // 4. active piece
  drawHoldPanel(game);       // 5. hold and next previews
  drawNextPanel(game);
  drawScorePanel(game);      // 6. score fields and panel text
  drawHintsPanel();
}

// The play background is the canvas color plus the darker board well, so the
// field reads as a recessed area before anything else lands on it
// (specification section 6.2, step 1).
void Renderer::drawBackground() const {
  clearBackground();
  fillRect(kBoardWell, kBoardOriginX, kBoardOriginY, kBoardPixelWidth,
           kBoardPixelHeight);
}

// The canvas color alone, the ground of the title and controls pages, which
// carry no board. A filled rectangle of the whole canvas, not SDL_RenderClear:
// the clear covers the entire rendering target and would paint the letterbox
// bars of a window that is not the canvas in the canvas color as well, wiping
// out the black base beginFrame() laid down before the draw (section 6.1).
void Renderer::clearBackground() const {
  fillRect(kBackground, 0, 0, kCanvasWidth, kCanvasHeight);
}

void Renderer::drawGridLines() const {
  if (renderer_ == nullptr) {
    return;
  }
  SDL_SetRenderDrawColor(renderer_, kGridLine.r, kGridLine.g, kGridLine.b,
                         kGridLine.a);
  for (int column = 1; column < kBoardWidth; ++column) {
    const int x = kBoardOriginX + column * kBoardCellSize;
    SDL_RenderDrawLine(renderer_, x, kBoardOriginY, x,
                       kBoardOriginY + kBoardPixelHeight - 1);
  }
  for (int row = 1; row < kBoardHeight; ++row) {
    const int y = kBoardOriginY + row * kBoardCellSize;
    SDL_RenderDrawLine(renderer_, kBoardOriginX, y,
                       kBoardOriginX + kBoardPixelWidth - 1, y);
  }
}

// The border alone: the well fill happens with the background so the grid
// lines land on it (specification section 6.2, steps 1 and 2).
void Renderer::drawBoardFrame() const {
  drawRectOutline(kFrame, kBoardOriginX - 1, kBoardOriginY - 1,
                  kBoardPixelWidth + 2, kBoardPixelHeight + 2);
}

void Renderer::drawSettledCells(const Game& game) const {
  const auto& cellTypes = game.board().cellTypes();
  for (int y = 0; y < kBoardHeight; ++y) {
    for (int x = 0; x < kBoardWidth; ++x) {
      const std::uint8_t code = cellTypes[boardRowIndex(y)][x];
      if (code == 0) {
        continue;
      }
      drawCell(cellX(x), cellY(y), kBoardCellSize,
               cellPalette(static_cast<PieceId>(code)));
    }
  }
}

void Renderer::drawGhostPiece(const Game& game) const {
  if (!game.hasActivePiece()) {
    return;
  }
  const Piece active = game.activePiece();
  const Piece ghost = game.ghostPiece();
  if (ghost.y <= active.y) {
    return;  // the landing row is the live row, so there is nothing to show
  }
  const SDL_Color color = ghostColor(active.id);
  for (const Cell& cell : boardCells(ghost)) {
    if (cell.y < 0) {
      continue;  // hidden rows are never drawn
    }
    drawGhostCell(cellX(cell.x), cellY(cell.y), kBoardCellSize, color);
  }
}

void Renderer::drawActivePiece(const Game& game) const {
  if (!game.hasActivePiece()) {
    return;
  }
  const Piece active = game.activePiece();
  const CellPalette& palette = cellPalette(active.id);
  for (const Cell& cell : boardCells(active)) {
    if (cell.y < 0) {
      continue;  // the part of a spawning piece in the hidden rows
    }
    drawCell(cellX(cell.x), cellY(cell.y), kBoardCellSize, palette);
  }
}

// ---------------------------------------------------------------------------
// Panels.
// ---------------------------------------------------------------------------

// Draws `id` in its spawn orientation from the same shape cell coordinates,
// scaled to the preview cell and centered in `box` (specification section
// 6.1). `dimmed` marks a hold slot that cannot be used again until the next
// lock, so an unavailable hold reads clearly.
void Renderer::drawPreviewPiece(PieceId id, const SDL_Rect& box,
                                bool dimmed) const {
  const CellPalette& palette = dimmed ? dimCellPalette(id) : cellPalette(id);
  const std::array<Cell, kCellsPerPiece>& cells =
      pieceCells(id, Rotation::Spawn);
  // Matrix coordinates run 0..3, so 4 is one past the largest possible value.
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
  const int width = (maxX - minX + 1) * kPreviewCellSize;
  const int height = (maxY - minY + 1) * kPreviewCellSize;
  const int originX =
      box.x + (box.w - width) / 2 - minX * kPreviewCellSize;
  const int originY = box.y + (box.h - height) / 2 - minY * kPreviewCellSize;
  for (const Cell& cell : cells) {
    drawCell(originX + cell.x * kPreviewCellSize,
             originY + cell.y * kPreviewCellSize, kPreviewCellSize, palette);
  }
}

void Renderer::drawHoldPanel(const Game& game) const {
  const int centerX = kLeftPanelX + kLeftPanelWidth / 2;
  drawTextCentered(centerX, kPanelHeadingY, kLabelScale, kTextDim, "HOLD");
  const SDL_Rect box = holdBox();
  drawRectOutline(kPanelLine, box.x - 8, box.y - 8, box.w + 16, box.h + 16);
  if (game.hasHoldPiece()) {
    drawPreviewPiece(game.holdPieceId(), box, game.holdLocked());
  }
}

void Renderer::drawNextPanel(const Game& game) const {
  const int centerX = kRightPanelX + kRightPanelWidth / 2;
  drawTextCentered(centerX, kPanelHeadingY, kLabelScale, kTextDim, "NEXT");
  const SDL_Rect panel = nextPanelBox();
  drawRectOutline(kPanelLine, panel.x - 8, panel.y - 8, panel.w + 16,
                  panel.h + 16);
  const auto& queue = game.queue();
  for (int index = 0; index < kPreviewCount; ++index) {
    drawPreviewPiece(queue[index], nextSlotBox(index), false);
  }
}

void Renderer::drawScorePanel(const Game& game) const {
  const int centerX = kRightPanelX + kRightPanelWidth / 2;
  char value[24];

  int y = nextPanelBox().y + nextPanelBox().h + 16;
  struct Field {
    const char* label;
    std::uint64_t number;
  };
  const Field fields[] = {
      {"SCORE", game.score()},
      {"LINES", game.lines()},
      {"LEVEL", static_cast<std::uint64_t>(game.level())},
  };
  for (const Field& field : fields) {
    drawTextCentered(centerX, y, kLabelScale, kTextDim, field.label);
    y += kGlyphRows * kLabelScale + 4;
    formatUnsigned(field.number, value, sizeof(value));
    drawTextCentered(centerX, y, kValueScale, kText, value);
    y += kGlyphRows * kValueScale + 18;
  }
}

void Renderer::drawHintsPanel() const {
  const int centerX = kLeftPanelX + kLeftPanelWidth / 2;
  int y = holdBox().y + holdBox().h + 28;
  drawTextCentered(centerX, y, kLabelScale, kTextDim, "CONTROLS");
  y += kGlyphRows * kLabelScale + 12;

  // Two aligned columns: the control on the left, its action beside it. At
  // the hint scale the widest control ("LEFT/RIGHT") spans 118 px and ends at
  // 182, and the widest action ("ROTATE CCW") spans 118 px more, so the action
  // column starts past the first end and stops at 310, inside the panel edge
  // at 324.
  const int controlX = kLeftPanelX + 12;
  constexpr int kActionColumnX = 192;
  for (int index = 0; index < kHintCount; ++index) {
    drawText(controlX, y, kHintScale, kTextDim, kHints[index].control);
    drawText(kActionColumnX, y, kHintScale, kText, kHints[index].action);
    y += kGlyphRows * kHintScale + 6;
  }
}

// ---------------------------------------------------------------------------
// The section 4.7 screens of task T011.
// ---------------------------------------------------------------------------

// One menu entry: the selected item draws bright with a filled square marker
// to its left; the rest draw dim. All rectangles, like every other pixel.
void Renderer::drawMenuItem(int centerX, int y, int scale, const char* text,
                            bool selected) const {
  if (selected) {
    const int textLeft = centerX - textWidth(scale, text) / 2;
    fillRect(kAccent, textLeft - kMenuMarkerGap - kMenuMarkerSize,
             y + (kGlyphRows * scale - kMenuMarkerSize) / 2, kMenuMarkerSize,
             kMenuMarkerSize);
    drawTextCentered(centerX, y, scale, kText, text);
    return;
  }
  drawTextCentered(centerX, y, scale, kTextDim, text);
}

// The title page: the logo, the seven pieces as one preview strip, the menu
// of section 4.7, the saved high score, and one hint line. The preview strip
// reuses the play preview drawing, so the page needs no new art.
void Renderer::drawTitleScreen(const Game& game) const {
  clearBackground();
  drawTextCentered(kCanvasWidth / 2, kTitleLogoY, kTitleLogoScale, kText,
                   "STACKFALL");
  for (int index = 0; index < kPieceCount; ++index) {
    drawPreviewPiece(kAllPieceIds[index], titlePreviewBox(index), false);
  }
  for (int index = 0; index < kTitleItemCount; ++index) {
    drawMenuItem(kCanvasWidth / 2, kTitleMenuY + index * kMenuItemHeight,
                 kMenuScale, kTitleItems[index],
                 index == game.titleSelection());
  }
  const int centerX = kCanvasWidth / 2;
  drawTextCentered(centerX, kTitleHighScoreLabelY, kLabelScale, kTextDim,
                   "HIGH SCORE");
  char value[24];
  formatUnsigned(game.highScore(), value, sizeof(value));
  drawTextCentered(centerX, kTitleHighScoreValueY, kValueScale, kText, value);
  drawTextCentered(centerX, kTitleHintY, kHintScale, kTextDim,
                   "UP/DOWN CHOOSE - A CONFIRM - B BACK");
}

// The controls page: three aligned columns of the section 5 tables plus one
// footer line; every press of A or B returns to the title.
void Renderer::drawControlsScreen() const {
  clearBackground();
  drawTextCentered(kCanvasWidth / 2, kControlsHeadingY, kControlsHeadingScale,
                   kText, "CONTROLS");
  drawText(kControlsGamepadX, kControlsHeaderY, kLabelScale, kTextDim,
           "GAMEPAD");
  drawText(kControlsKeyboardX, kControlsHeaderY, kLabelScale, kTextDim,
           "KEYBOARD");
  drawText(kControlsActionX, kControlsHeaderY, kLabelScale, kTextDim,
           "ACTION");
  int y = kControlsFirstRowY;
  for (int index = 0; index < kControlRowCount; ++index) {
    drawText(kControlsGamepadX, y, kLabelScale, kText,
             kControlRows[index].gamepad);
    drawText(kControlsKeyboardX, y, kLabelScale, kText,
             kControlRows[index].keyboard);
    drawText(kControlsActionX, y, kLabelScale, kText,
             kControlRows[index].action);
    y += kControlsRowHeight;
  }
  drawTextCentered(kCanvasWidth / 2, kControlsFooterY, kLabelScale, kText,
                   "A OR B BACK TO TITLE");
}

// The translucent veil that dims the frozen play scene under an overlay. One
// blended filled rectangle, then the blend mode returns to none, so the next
// solid draw of the panel and the scene behind stay exact.
void Renderer::drawVeil() const {
  if (renderer_ == nullptr) {
    return;
  }
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
  fillRect(SDL_Color{kBackground.r, kBackground.g, kBackground.b,
                     kOverlayVeilAlpha},
           0, 0, kCanvasWidth, kCanvasHeight);
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

// One solid card: the panel fill with a frame outline just outside it.
void Renderer::drawOverlayPanel(const SDL_Rect& panel) const {
  fillRect(kPanelFill, panel.x, panel.y, panel.w, panel.h);
  drawRectOutline(kFrame, panel.x - 1, panel.y - 1, panel.w + 2, panel.h + 2);
}

// The pause overlay of section 4.7 over the frozen round: Resume / Restart /
// Quit to Title, with Resume the default item.
void Renderer::drawPauseOverlay(const Game& game) const {
  drawVeil();
  const SDL_Rect panel = pausePanel();
  drawOverlayPanel(panel);
  drawTextCentered(kCanvasWidth / 2, panel.y + kPauseHeadingOffsetY,
                   kControlsHeadingScale, kText, "PAUSED");
  for (int index = 0; index < kPauseItemCount; ++index) {
    drawMenuItem(kCanvasWidth / 2, panel.y + kPauseMenuOffsetY +
                                        index * kMenuItemHeight,
                 kMenuScale, kPauseItems[index], index == game.pauseSelection());
  }
  drawTextCentered(kCanvasWidth / 2, panel.y + kPauseHintOffsetY, kHintScale,
                   kTextDim, "A CONFIRM - START OR B RESUME");
}

// The game-over overlay of section 4.7: the final score, total lines, and
// level, the new-record note when the round beat the high score, and Play
// Again / Title.
void Renderer::drawGameOverOverlay(const Game& game) const {
  drawVeil();
  const SDL_Rect panel = gameoverPanel();
  drawOverlayPanel(panel);
  const int centerX = kCanvasWidth / 2;
  drawTextCentered(centerX, panel.y + kOverHeadingOffsetY,
                   kControlsHeadingScale, kText, "GAME OVER");

  char value[24];
  struct Field {
    const char* label;
    std::uint64_t number;
  };
  const Field fields[] = {
      {"SCORE", game.score()},
      {"LINES", game.lines()},
      {"LEVEL", static_cast<std::uint64_t>(game.level())},
  };
  int y = panel.y + kOverStatsOffsetY;
  for (const Field& field : fields) {
    drawText(panel.x + 56, y + 3, kLabelScale, kTextDim, field.label);
    formatUnsigned(field.number, value, sizeof(value));
    // Right-aligned against the panel edge, so the columns line up.
    drawText(panel.x + panel.w - 56 - textWidth(kMenuScale, value), y,
             kMenuScale, kText, value);
    y += kOverStatsRowHeight;
  }
  if (game.newRecord()) {
    drawTextCentered(centerX, panel.y + kOverRecordOffsetY, kLabelScale,
                     kAccent, "NEW RECORD");
  }
  for (int index = 0; index < kOverItemCount; ++index) {
    drawMenuItem(centerX, panel.y + kOverMenuOffsetY + index * kMenuItemHeight,
                 kMenuScale, kOverItems[index], index == game.overSelection());
  }
  drawTextCentered(centerX, panel.y + kOverHintOffsetY, kHintScale, kTextDim,
                   "A CONFIRM");
}

}  // namespace stackfall
