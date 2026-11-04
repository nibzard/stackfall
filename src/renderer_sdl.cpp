// The procedural renderer of milestone M2 (tasks T010 and T011,
// specification sections 4.7, 6.1, and 6.2), carrying the focused HUD of the
// approved composition (task T037, finding focused-gameplay-hud).
//
// Every frame comes from filled rectangles and lines: the board well, its
// faint grid and frame, the settled cells in their stored colors, the ghost,
// the active piece, the hold and next previews at the smaller preview cell,
// and the statistics fields drawn as glyphs. Two narrow columns flank the
// board - statistics on the left, the five Next previews on the right - and
// ordinary gameplay draws no permanent control legend: the mappings live on
// the controls screen, which the title and the pause overlay both reach. The
// section 4.7 screens draw the same way: the title, controls, and options
// pages from glyphs and preview pieces alone, the pause and game-over
// overlays as one translucent veil plus a solid panel over the frozen play
// scene. Nothing loads a
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

#include <algorithm>
#include <cstdio>

#include "stackfall/font.hpp"
#include "stackfall/pieces.hpp"

namespace stackfall {
namespace {

// Text scales: one font pixel becomes a scale x scale square. Labels sit at
// the small scale in the dim color, values at the same scale in the bright
// color, which keeps the score readable on the 5-inch panel inside the narrow
// statistics column. The footer hints draw at that scale as well: at scale 1
// their glyphs measured 7 px, which falls to 4 px in a 640x480 letterbox, so
// they match the 14 px label height (design review finding
// control-hint-glyph-scale).
constexpr int kLabelScale = 2;
constexpr int kValueScale = 2;
constexpr int kHintScale = 2;

// The glyph rectangle batch of one drawText call. Every pixel of one string
// shares one color, so one SDL_RenderFillRects submit replaces one
// SDL_RenderFillRect per pixel; SDL fills the rectangles in array order under
// the same color state, so the drawn output stays byte-identical. The
// capacity holds the worst case of the longest drawn string (35 glyphs, every
// pixel of the 5x7 cell on) in one submit. A longer string flushes the full
// buffer and continues, so any length draws without overflow.
constexpr int kTextBatchGlyphs = 35;
constexpr int kTextBatchCapacity =
    kTextBatchGlyphs * kGlyphColumns * kGlyphRows;

// The section 4.7 menu items, drawn in listed order; the cursor indexes of
// Game address them by position. The options rows append their ON/OFF value
// at draw time, because the value is platform state the core never holds.
constexpr const char* kTitleItems[] = {"START GAME", "CONTROLS", "OPTIONS",
                                       "QUIT"};
constexpr const char* kPauseItems[] = {"RESUME", "RESTART", "CONTROLS",
                                       "QUIT TO TITLE"};
constexpr const char* kOverItems[] = {"PLAY AGAIN", "TITLE"};

// The controls screen rows: the section 5 gamepad mapping, its keyboard
// counterpart, and the action. One table per orientation, next to the two
// mapping tables of src/input_sdl.cpp, so a mapping review reads them
// together. The portrait rows restate the rotated table of
// notes/design-review.md: the D-pad's device up and down move the piece
// right and left in the rotated frame, its right soft drops, and the two
// cluster pairs reverse (task T055).
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

constexpr ControlRow kPortraitControlRows[] = {
    {"D-PAD UP/DOWN", "UP/DOWN ARROWS", "MOVE"},
    {"D-PAD RIGHT", "RIGHT ARROW", "SOFT DROP"},
    {"B", "Z", "ROTATE CW"},
    {"A", "X", "ROTATE CCW"},
    {"Y", "C OR LEFT SHIFT", "HARD DROP"},
    {"X OR L1", "SPACE", "HOLD"},
    {"START", "ENTER OR ESC", "PAUSE OR RESUME"},
    {"D-PAD LEFT/RIGHT", "LEFT/RIGHT ARROWS", "MENU CURSOR"},
    {"B", "ENTER", "SELECT"},
    {"A", "ESC", "BACK"},
};
constexpr int kPortraitControlRowCount =
    static_cast<int>(sizeof(kPortraitControlRows) / sizeof(kPortraitControlRows[0]));

// Formats an unsigned 64-bit score in decimal. The buffer always holds a
// terminated string, and snprintf clamps rather than overruns, so a score of
// any size renders safely (specification section 4.6).
void formatUnsigned(std::uint64_t value, char* buffer, std::size_t capacity) {
  std::snprintf(buffer, capacity, "%llu",
                static_cast<unsigned long long>(value));
}

// One canvas rectangle in device space. Landscape draws where it is; portrait
// turns the canvas a quarter so it stands upright in the grip the player
// holds, which swaps the rectangle's span with its height and flips it to the
// opposite edge (notes/design-review.md, "The rotation direction"):
//
//   device_x = canvas_y
//   device_y = kCanvasHeight - 1 - canvas_x
//
// The device canvas itself keeps its 960x544 logical size and its letterbox
// rules, so this rotation is the only place the two orientations differ.
constexpr SDL_Rect deviceRect(bool portrait, int x, int y, int w, int h) {
  if (!portrait) {
    return SDL_Rect{x, y, w, h};
  }
  return SDL_Rect{y, kCanvasHeight - 1 - x - w + 1, h, w};
}

}  // namespace

namespace {

// The MUSIC row's own display name: the mode of MUSIC.md's player controls
// in caps, the only case the glyph table draws. The settings file keeps the
// lowercase pair of stackfall/music.hpp; this is the screen's rendering.
const char* musicModeDisplayName(MusicMode mode) {
  switch (mode) {
    case MusicMode::Auto:
      return "AUTO";
    case MusicMode::Arcade:
      return "ARCADE";
    case MusicMode::Groove:
      return "GROOVE";
    case MusicMode::Focus:
      return "FOCUS";
    case MusicMode::Off:
      return "OFF";
  }
  return "?";
}

}  // namespace

Renderer::Renderer(SDL_Renderer* renderer) noexcept : renderer_(renderer) {}

void Renderer::setShowGrid(bool show) noexcept { showGrid_ = show; }

bool Renderer::showGrid() const noexcept { return showGrid_; }

void Renderer::setShowGhost(bool show) noexcept { showGhost_ = show; }

bool Renderer::showGhost() const noexcept { return showGhost_; }

void Renderer::setSoundOn(bool on) noexcept { soundOn_ = on; }

bool Renderer::soundOn() const noexcept { return soundOn_; }

void Renderer::setShowHold(bool show) noexcept { showHold_ = show; }

bool Renderer::showHold() const noexcept { return showHold_; }

void Renderer::setNextPreviews(int count) noexcept {
  // The queue holds kPreviewCount pieces, so a count outside the preference
  // range clamps to it instead of reading past the queue.
  if (count < 1) {
    nextPreviews_ = 1;
  } else if (count > kPreviewCount) {
    nextPreviews_ = kPreviewCount;
  } else {
    nextPreviews_ = count;
  }
}

int Renderer::nextPreviews() const noexcept { return nextPreviews_; }

void Renderer::setGrayscale(bool on) noexcept { grayscale_ = on; }

bool Renderer::grayscale() const noexcept { return grayscale_; }

void Renderer::setOrientation(Orientation orientation) noexcept {
  orientation_ = orientation;
}

Orientation Renderer::orientation() const noexcept { return orientation_; }

void Renderer::setOrientationPreference(Orientation orientation) noexcept {
  orientationPreference_ = orientation;
}

Orientation Renderer::orientationPreference() const noexcept {
  return orientationPreference_;
}

void Renderer::setMusicMode(MusicMode mode) noexcept { musicMode_ = mode; }

MusicMode Renderer::musicMode() const noexcept { return musicMode_; }

void Renderer::setMusicVolume(int step) noexcept {
  // The same clamp the audio driver applies, so a stray value can never name
  // a step the gain table does not hold.
  musicVolume_ = std::max(0, std::min(10, step));
}

int Renderer::musicVolume() const noexcept { return musicVolume_; }

void Renderer::setEffectsVolume(int step) noexcept {
  effectsVolume_ = std::max(0, std::min(10, step));
}

int Renderer::effectsVolume() const noexcept { return effectsVolume_; }

void Renderer::setMusicTrackTitle(const char* title) noexcept {
  musicTrackTitle_ = title != nullptr ? title : "";
}

const char* Renderer::musicTrackTitle() const noexcept {
  return musicTrackTitle_;
}

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
// logical-size scaling, so these coordinates are logical pixels of the
// orientation's canvas, and deviceRect() carries them onto the device output
// (the one rotation portrait adds, task T055).
// ---------------------------------------------------------------------------

void Renderer::fillRect(SDL_Color color, int x, int y, int w, int h) const {
  if (renderer_ == nullptr || w <= 0 || h <= 0) {
    return;
  }
  SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
  const SDL_Rect rect = deviceRect(portrait(), x, y, w, h);
  SDL_RenderFillRect(renderer_, &rect);
}

void Renderer::drawUnitLine(SDL_Color color, int x, int y, int w, int h) const {
  fillRect(color, x, y, w, h);
}

void Renderer::drawRectOutline(SDL_Color color, int x, int y, int w,
                               int h) const {
  if (renderer_ == nullptr || w <= 0 || h <= 0) {
    return;
  }
  // Four one-pixel fills rather than SDL_RenderDrawRect, so the rotation of
  // fillRect() carries the outline exactly as it carries every fill.
  drawUnitLine(color, x, y, w, 1);
  drawUnitLine(color, x, y + h - 1, w, 1);
  drawUnitLine(color, x, y, 1, h);
  drawUnitLine(color, x + w - 1, y, 1, h);
}

// One block: a dark one-pixel outline, the flat fill, a lighter top and left
// edge, and a darker bottom and right edge. Straight rectangles and lines
// give the flat geometric look of section 6.2 without any shading texture.
void Renderer::drawCell(int x, int y, int size,
                        const CellPalette& palette) const {
  fillRect(palette.edge, x, y, size, size);
  fillRect(palette.fill, x + 1, y + 1, size - 2, size - 2);
  drawUnitLine(palette.light, x + 1, y + 1, size - 2, 1);
  drawUnitLine(palette.light, x + 1, y + 1, 1, size - 2);
  drawUnitLine(palette.dark, x + 1, y + size - 2, size - 2, 1);
  drawUnitLine(palette.dark, x + size - 2, y + 1, 1, size - 2);
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
  SDL_Rect rects[kTextBatchCapacity];
  int count = 0;
  int penX = x;
  for (const char* p = text; *p != '\0'; ++p) {
    const Glyph& glyph = glyphFor(*p);
    for (int row = 0; row < kGlyphRows; ++row) {
      for (int column = 0; column < kGlyphColumns; ++column) {
        if (glyphPixel(glyph, column, row)) {
          if (count == kTextBatchCapacity) {
            SDL_RenderFillRects(renderer_, rects, count);
            count = 0;
          }
          rects[count] = deviceRect(portrait(), penX + column * scale,
                                    y + row * scale, scale, scale);
          ++count;
        }
      }
    }
    penX += kGlyphAdvance * scale;
  }
  if (count > 0) {
    SDL_RenderFillRects(renderer_, rects, count);
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
      drawControlsScreen("A OR B BACK TO TITLE");
      return;
    case Scene::PauseControls:
      // The same mappings page over the frozen round; back returns to the
      // pause overlay, not the title (task T037).
      drawControlsScreen("A OR B BACK TO PAUSE");
      return;
    case Scene::Options:
      drawOptionsScreen(game);
      return;
    case Scene::MusicOptions:
      drawMusicOptionsScreen(game);
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
  if (showGhost_) {
    drawGhostPiece(game);    // 3. ghost below the active piece, when shown
  }
  drawActivePiece(game);     // 4. active piece
  if (showHold_) {
    drawHoldPanel(game);     // 5. hold and next previews; hold hides entirely
  }                          //    when its preference is off (task T044)
  drawNextPanel(game);
  drawStatsPanel(game);      // 6. the statistics fields of the left column
}

// The play background is the canvas color plus the darker board well, so the
// field reads as a recessed area before anything else lands on it
// (specification section 6.2, step 1).
void Renderer::drawBackground() const {
  clearBackground();
  fillRect(kBoardWell, boardOriginX(), boardOriginY(), boardPixelWidth(),
           boardPixelHeight());
}

// The canvas color alone, the ground of the title and controls pages, which
// carry no board. A filled rectangle of the whole canvas, not SDL_RenderClear:
// the clear covers the entire rendering target and would paint the letterbox
// bars of a window that is not the canvas in the canvas color as well, wiping
// out the black base beginFrame() laid down before the draw (section 6.1).
void Renderer::clearBackground() const {
  fillRect(kBackground, 0, 0, canvasWidth(), canvasHeight());
}

void Renderer::drawGridLines() const {
  if (renderer_ == nullptr) {
    return;
  }
  for (int column = 1; column < kBoardWidth; ++column) {
    const int x = boardOriginX() + column * boardCellSize();
    drawUnitLine(kGridLine, x, boardOriginY(), 1, boardPixelHeight());
  }
  for (int row = 1; row < kBoardHeight; ++row) {
    const int y = boardOriginY() + row * boardCellSize();
    drawUnitLine(kGridLine, boardOriginX(), y, boardPixelWidth(), 1);
  }
}

// The border alone: the well fill happens with the background so the grid
// lines land on it (specification section 6.2, steps 1 and 2).
void Renderer::drawBoardFrame() const {
  const SDL_Rect frame = boardFrameNow();
  drawRectOutline(kFrame, frame.x, frame.y, frame.w, frame.h);
}

void Renderer::drawSettledCells(const Game& game) const {
  const auto& cellTypes = game.board().cellTypes();
  for (int y = 0; y < kBoardHeight; ++y) {
    for (int x = 0; x < kBoardWidth; ++x) {
      const std::uint8_t code = cellTypes[boardRowIndex(y)][x];
      if (code == 0) {
        continue;
      }
      drawCell(cellXNow(x), cellYNow(y), boardCellSize(),
               piecePalette(static_cast<PieceId>(code), false));
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
  const SDL_Color color = pieceGhostColor(active.id);
  for (const Cell& cell : boardCells(ghost)) {
    if (cell.y < 0) {
      continue;  // hidden rows are never drawn
    }
    drawGhostCell(cellXNow(cell.x), cellYNow(cell.y), boardCellSize(), color);
  }
}

void Renderer::drawActivePiece(const Game& game) const {
  if (!game.hasActivePiece()) {
    return;
  }
  const Piece active = game.activePiece();
  const CellPalette& palette = piecePalette(active.id, false);
  for (const Cell& cell : boardCells(active)) {
    if (cell.y < 0) {
      continue;  // the part of a spawning piece in the hidden rows
    }
    drawCell(cellXNow(cell.x), cellYNow(cell.y), boardCellSize(), palette);
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
  drawPreviewPiece(id, box, dimmed, previewCellSize());
}

// Same, at an explicit cell size: the portrait HUD strip draws its previews
// at the smaller portrait cell while the title page keeps the larger strip
// cell in both orientations (task T055).
void Renderer::drawPreviewPiece(PieceId id, const SDL_Rect& box, bool dimmed,
                                int cellSize) const {
  const CellPalette& palette = piecePalette(id, dimmed);
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
  const int width = (maxX - minX + 1) * cellSize;
  const int height = (maxY - minY + 1) * cellSize;
  const int originX = box.x + (box.w - width) / 2 - minX * cellSize;
  const int originY = box.y + (box.h - height) / 2 - minY * cellSize;
  for (const Cell& cell : cells) {
    drawCell(originX + cell.x * cellSize, originY + cell.y * cellSize,
             cellSize, palette);
  }
}

// The Hold group. Landscape centers it on the left statistics column beside
// the board; portrait centers it at the left of the HUD strip above the board
// (task T055). The heading sits above the box in both.
void Renderer::drawHoldPanel(const Game& game) const {
  const int centerX =
      portrait() ? kPortraitHoldCenterX : kLeftPanelX + kLeftPanelWidth / 2;
  const int headingY = portrait() ? kPortraitPanelHeadingY : kPanelHeadingY;
  drawTextCentered(centerX, headingY, kLabelScale, kTextDim, "HOLD");
  const SDL_Rect box = holdBoxNow();
  if (game.hasHoldPiece()) {
    drawPreviewPiece(game.holdPieceId(), box, game.holdLocked());
  }
}

void Renderer::drawNextPanel(const Game& game) const {
  const int centerX =
      portrait() ? kPortraitNextCenterX : kRightPanelX + kRightPanelWidth / 2;
  const int headingY = portrait() ? kPortraitPanelHeadingY : kPanelHeadingY;
  drawTextCentered(centerX, headingY, kLabelScale, kTextDim, "NEXT");
  const auto& queue = game.queue();
  // Exactly the preference's count of previews draws, in queue order; the
  // count changes nothing about the queue or the randomizer behind it
  // (task T044). The first preview draws at full color and the rest dimmer,
  // so the piece that lands next reads beside the board while the far queue
  // stays identifiable but quiet (task T037 contrast hierarchy).
  for (int index = 0; index < nextPreviews_; ++index) {
    drawPreviewPiece(queue[index], nextSlotBoxNow(index), index > 0);
  }
}

// The statistics fields. A value draws at the value scale while it fits its
// field; a longer one draws one step smaller, because the longest number the
// saturating counter can hold (twenty digits) measures 119 px at scale 1 and
// so still fits the 128 px field with room to spare. Every supported value
// therefore stays inside its field and never reaches the board or another
// group (task T037).
int Renderer::statValueScale(const char* value) const {
  if (textWidth(kValueScale, value) <= statFieldWidth()) {
    return kValueScale;
  }
  return 1;
}

// The statistics group. Landscape stacks the three fields down the left
// column beside the board; portrait spreads them across the top of the canvas
// as three label-over-value groups, above the Hold and Next row of the HUD
// strip (task T055).
void Renderer::drawStatsPanel(const Game& game) const {
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
  if (portrait()) {
    for (int index = 0; index < 3; ++index) {
      const int centerX = kPortraitStatsCenterX[index];
      drawTextCentered(centerX, kPortraitStatsLabelY, kLabelScale, kTextDim,
                       fields[index].label);
      formatUnsigned(fields[index].number, value, sizeof(value));
      drawTextCentered(centerX, kPortraitStatsValueY, statValueScale(value),
                       kText, value);
    }
    return;
  }
  const int centerX = kLeftPanelX + kLeftPanelWidth / 2;
  int y = kStatsFirstLabelY;
  for (const Field& field : fields) {
    drawTextCentered(centerX, y, kLabelScale, kTextDim, field.label);
    y += kGlyphRows * kLabelScale + 4;
    formatUnsigned(field.number, value, sizeof(value));
    drawTextCentered(centerX, y, statValueScale(value), kText, value);
    // The label rows stay anchored at the full value scale, so a long value
    // never shifts the fields below it.
    y += kGlyphRows * kValueScale + 18;
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
  const int centerX = canvasWidth() / 2;
  const int logoY = portrait() ? kPTitleLogoY : kTitleLogoY;
  const int previewY = portrait() ? kPTitlePreviewY : kTitlePreviewY;
  const int menuY = portrait() ? kPTitleMenuY : kTitleMenuY;
  const int highLabelY =
      portrait() ? kPTitleHighScoreLabelY : kTitleHighScoreLabelY;
  const int highValueY =
      portrait() ? kPTitleHighScoreValueY : kTitleHighScoreValueY;
  const int hintY = portrait() ? kPTitleHintY : kTitleHintY;
  drawTextCentered(centerX, logoY, kTitleLogoScale, kText, "STACKFALL");
  for (int index = 0; index < kPieceCount; ++index) {
    // The seven-piece strip keeps the landscape preview cell in both frames:
    // it measures 528 px, which fits the 544 px portrait canvas as it fits
    // the 960 px landscape one.
    SDL_Rect box = titlePreviewBox(index);
    if (portrait()) {
      box.x = (kPortraitCanvasWidth - (kPieceCount * kTitlePreviewStride -
                                       (kTitlePreviewStride -
                                        kTitlePreviewBoxWidth))) / 2 +
              index * kTitlePreviewStride;
      box.y = previewY;
    }
    drawPreviewPiece(kAllPieceIds[index], box, false, kPreviewCellSize);
  }
  for (int index = 0; index < kTitleItemCount; ++index) {
    drawMenuItem(centerX, menuY + index * kMenuItemHeight, kMenuScale,
                 kTitleItems[index], index == game.titleSelection());
  }
  drawTextCentered(centerX, highLabelY, kLabelScale, kTextDim, "HIGH SCORE");
  char value[24];
  formatUnsigned(game.highScore(), value, sizeof(value));
  drawTextCentered(centerX, highValueY, kValueScale, kText, value);
  drawTextCentered(centerX, hintY, kHintScale, kTextDim, menuHintText());
}

// The controls page: three aligned columns of the section 5 tables plus one
// footer line. The title opens it with "A OR B BACK TO TITLE"; the pause
// overlay opens the same page over its frozen round with "A OR B BACK TO
// PAUSE" (task T037), because ordinary gameplay carries no permanent legend.
// Portrait cannot hold the three columns side by side - they measure 570 px of
// a 544 px canvas - so it stacks one action heading over the two mappings of
// each row (task T055).
void Renderer::drawControlsScreen(const char* footer) const {
  clearBackground();
  const int centerX = canvasWidth() / 2;
  drawTextCentered(centerX, portrait() ? kPControlsHeadingY : kControlsHeadingY,
                   kControlsHeadingScale, kText, "CONTROLS");
  if (portrait()) {
    drawText(kPControlsGamepadX, kPControlsHeaderY, kLabelScale, kTextDim,
             "GAMEPAD");
    drawText(kPControlsKeyboardX, kPControlsHeaderY, kLabelScale, kTextDim,
             "KEYBOARD");
    int y = kPControlsFirstRowY;
    for (int index = 0; index < kPortraitControlRowCount; ++index) {
      drawText(kPControlsGamepadX, y, kLabelScale, kTextDim,
               kPortraitControlRows[index].action);
      drawText(kPControlsGamepadX, y + kGlyphRows * kLabelScale + 4, kLabelScale,
               kText, kPortraitControlRows[index].gamepad);
      drawText(kPControlsKeyboardX, y + kGlyphRows * kLabelScale + 4,
               kLabelScale, kText, kPortraitControlRows[index].keyboard);
      y += kPControlsRowHeight;
    }
    drawTextCentered(centerX, kPControlsFooterY, kLabelScale, kText, footer);
    return;
  }
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
  drawTextCentered(centerX, kControlsFooterY, kLabelScale, kText, footer);
}

// The options page of tasks T040, T044, T055, and T062: one heading, the
// eight setting rows at the shared menu metrics, and one footer line. The rows
// carry their value as ON/OFF, the NEXT PIECES count digit, or the
// ORIENTATION or MUSIC name, read from the preferences this renderer mirrors,
// because the rules core moves the cursor alone and never holds a row value
// (specification section 7). A changes the highlighted row - and on the MUSIC
// row opens the music settings page, which the core owns - and B returns to
// the title; the platform layer owns the value actions. The ORIENTATION row
// names the preference the next launch draws, not the layout this process
// already opened (task T055).
void Renderer::drawOptionsScreen(const Game& game) const {
  clearBackground();
  const int centerX = canvasWidth() / 2;
  drawTextCentered(centerX, portrait() ? kPOptionsHeadingY : kOptionsHeadingY,
                   kOptionsHeadingScale, kText, "OPTIONS");
  char nextText[24];
  std::snprintf(nextText, sizeof(nextText), "NEXT PIECES %d", nextPreviews_);
  char musicText[20];
  std::snprintf(musicText, sizeof(musicText), "MUSIC %s",
                musicModeDisplayName(musicMode_));
  const char* rows[kOptionsItemCount] = {
      soundOn_ ? "SOUND ON" : "SOUND OFF",
      showGhost_ ? "GHOST ON" : "GHOST OFF",
      showGrid_ ? "GRID ON" : "GRID OFF",
      showHold_ ? "HOLD ON" : "HOLD OFF",
      nextText,
      grayscale_ ? "GRAYSCALE ON" : "GRAYSCALE OFF",
      orientationPreference_ == Orientation::Portrait
          ? "ORIENTATION PORTRAIT"
          : "ORIENTATION LANDSCAPE",
      musicText,
  };
  const int menuY = portrait() ? kPOptionsMenuY : kOptionsMenuY;
  for (int index = 0; index < kOptionsItemCount; ++index) {
    drawMenuItem(centerX, menuY + index * kMenuItemHeight, kMenuScale,
                 rows[index], index == game.optionsSelection());
  }
  drawTextCentered(centerX, portrait() ? kPOptionsFooterY : kOptionsFooterY,
                   kHintScale, kTextDim, optionsHintText());
}

// The music settings page behind the MUSIC row (task T062, MUSIC.md player
// controls): the same heading, menu metrics, and footer hint as the options
// page, over its four rows - the pool mode, both volume steps of the
// eleven-step table, and the Next track action - and the playing track's
// title on one dim line of its own, which stays empty while music is silent.
// The values are the mirrors the platform layer pushes; the core moves the
// cursor alone, exactly as on the options page. A changes the highlighted row
// and B returns to the options screen; the platform layer owns both actions.
void Renderer::drawMusicOptionsScreen(const Game& game) const {
  clearBackground();
  const int centerX = canvasWidth() / 2;
  drawTextCentered(centerX, portrait() ? kPOptionsHeadingY : kOptionsHeadingY,
                   kOptionsHeadingScale, kText, "MUSIC");
  char modeText[16];
  std::snprintf(modeText, sizeof(modeText), "MUSIC %s",
                musicModeDisplayName(musicMode_));
  char musicVolumeText[24];
  std::snprintf(musicVolumeText, sizeof(musicVolumeText), "MUSIC VOLUME %d",
                musicVolume_);
  char effectsVolumeText[24];
  std::snprintf(effectsVolumeText, sizeof(effectsVolumeText),
                "EFFECTS VOLUME %d", effectsVolume_);
  const char* rows[kMusicOptionsItemCount] = {
      modeText,
      musicVolumeText,
      effectsVolumeText,
      "NEXT TRACK",
  };
  const int menuY = portrait() ? kPOptionsMenuY : kOptionsMenuY;
  for (int index = 0; index < kMusicOptionsItemCount; ++index) {
    drawMenuItem(centerX, menuY + index * kMenuItemHeight, kMenuScale,
                 rows[index], index == game.musicSelection());
  }
  drawTextCentered(centerX, portrait() ? kPMusicTrackY : kMusicTrackY,
                   kMenuScale, kTextDim, musicTrackTitle_);
  drawTextCentered(centerX, portrait() ? kPOptionsFooterY : kOptionsFooterY,
                   kHintScale, kTextDim, optionsHintText());
}

// The palette of one piece under the current settings (task T044): grayscale
// on replaces the color palettes with the gray ones everywhere a piece draws,
// and off restores the color ones exactly.
const CellPalette& Renderer::piecePalette(PieceId id, bool dimmed) const {
  if (grayscale_) {
    return dimmed ? grayscaleDimPalette(id) : grayscalePalette(id);
  }
  return dimmed ? dimCellPalette(id) : cellPalette(id);
}

SDL_Color Renderer::pieceGhostColor(PieceId id) const {
  return grayscale_ ? grayscaleGhostColor(id) : ghostColor(id);
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
           0, 0, canvasWidth(), canvasHeight());
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

// One solid card: the panel fill with a frame outline just outside it.
void Renderer::drawOverlayPanel(const SDL_Rect& panel) const {
  fillRect(kPanelFill, panel.x, panel.y, panel.w, panel.h);
  drawRectOutline(kFrame, panel.x - 1, panel.y - 1, panel.w + 2, panel.h + 2);
}

// The pause overlay of section 4.7 over the frozen round: Resume / Restart /
// Controls / Quit to Title, with Resume the default item. The panel nests
// between the HUD columns, so the frozen statistics and previews stay visible
// beside it (task T037); Controls opens the mappings page and returns here.
void Renderer::drawPauseOverlay(const Game& game) const {
  drawVeil();
  const int centerX = canvasWidth() / 2;
  const SDL_Rect panel = portrait() ? pausePanelPortrait() : pausePanel();
  drawOverlayPanel(panel);
  drawTextCentered(centerX, panel.y + kPauseHeadingOffsetY,
                   kControlsHeadingScale, kText, "PAUSED");
  for (int index = 0; index < kPauseItemCount; ++index) {
    drawMenuItem(centerX, panel.y + kPauseMenuOffsetY + index * kMenuItemHeight,
                 kMenuScale, kPauseItems[index], index == game.pauseSelection());
  }
  drawTextCentered(centerX, panel.y + kPauseHintOffsetY, kHintScale, kTextDim,
                   pauseHintText());
}

// The game-over overlay of section 4.7: the final score, total lines, and
// level, the new-record note when the round beat the high score, and Play
// Again / Title. The panel nests between the HUD columns like the pause
// panel, so the dimmed statistics column reads whole beside it instead of
// peeking out at the panel's edge (task T043).
void Renderer::drawGameOverOverlay(const Game& game) const {
  drawVeil();
  const SDL_Rect panel =
      portrait() ? gameoverPanelPortrait() : gameoverPanel();
  drawOverlayPanel(panel);
  const int centerX = canvasWidth() / 2;
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
                   confirmHintText());
}

}  // namespace stackfall
