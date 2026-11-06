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
#include <cstring>

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

// The controls screen's fixed rows, the three menu roles no binding moves
// (task T066): the cursor arms, the confirm button, and the back button, one
// table per orientation because the turned frame swaps which arms and cluster
// buttons those are. These sit under the eight rebindable rows the binding
// tables supply at draw time.
struct FixedControlRow {
  const char* gamepad;
  const char* keyboard;
  const char* action;
};

constexpr FixedControlRow kLandscapeFixedRows[3] = {
    {"D-PAD UP/DOWN", "UP/DOWN ARROWS", "MENU CURSOR"},
    {"A", "ENTER", "SELECT"},
    {"B", "ESC", "BACK"},
};

constexpr FixedControlRow kPortraitFixedRows[3] = {
    {"D-PAD LEFT/RIGHT", "LEFT/RIGHT ARROWS", "MENU CURSOR"},
    {"B", "ENTER", "SELECT"},
    {"A", "ESC", "BACK"},
};

// One action's controls as the screen spells them: the display name of each
// control joined by " OR ", so the section 5 pairs still read "Y OR L1" and
// "C OR LEFT SHIFT" and a rebound single control reads on its own.
void controlListText(DeviceKind device, const ControlTable& table,
                     GameAction action, Orientation orientation, char* out,
                     std::size_t capacity) {
  int controls[kMaxControlsPerAction];
  const int count = controlsOfAction(table, action, controls);
  std::size_t at = 0;
  out[0] = '\0';
  for (int index = 0; index < count; ++index) {
    if (index != 0 && at + 4 < capacity) {
      std::snprintf(out + at, capacity - at, " OR ");
      at += 4;
    }
    const char* name =
        device == DeviceKind::Keyboard
            ? keyboardControlDisplayName(controls[index], orientation)
            : controllerControlDisplayName(controls[index], orientation);
    const std::size_t written = std::snprintf(out + at, capacity - at, "%s",
                                              name != nullptr ? name : "?");
    at += written < capacity - at ? written : capacity - at - 1;
  }
}

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

void Renderer::setPieceDesigns(bool on) noexcept { pieceDesigns_ = on; }

bool Renderer::pieceDesigns() const noexcept { return pieceDesigns_; }

void Renderer::setOrientation(Orientation orientation) noexcept {
  orientation_ = orientation;
}

Orientation Renderer::orientation() const noexcept { return orientation_; }

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

void Renderer::setControlBindings(const ControlBindings& bindings) noexcept {
  bindings_ = bindings;
}

void Renderer::setRebindCaptureRow(int row) noexcept {
  rebindCaptureRow_ = row;
}

void Renderer::setRebindMessage(const char* text) noexcept {
  rebindMessage_ = text != nullptr ? text : "";
}

const ControlBindings& Renderer::controlBindings() const noexcept {
  return bindings_;
}

int Renderer::rebindCaptureRow() const noexcept { return rebindCaptureRow_; }

const char* Renderer::rebindMessage() const noexcept { return rebindMessage_; }

void Renderer::setMusicTrackTitle(const char* title) noexcept {
  musicTrackTitle_ = title != nullptr ? title : "";
}

const char* Renderer::musicTrackTitle() const noexcept {
  return musicTrackTitle_;
}

// ---------------------------------------------------------------------------
// The level backgrounds of task T078: the wallpaper state the platform layer
// drives, and the zones the design contract clips every background draw with.
// ---------------------------------------------------------------------------

void Renderer::setBackgroundMode(backgrounds::Mode mode) noexcept {
  wallpaper_.setMode(mode);
}

backgrounds::Mode Renderer::backgroundMode() const noexcept {
  return wallpaper_.mode();
}

void Renderer::setBackgroundPreference(int designId) noexcept {
  wallpaper_.setPreference(designId);
}

int Renderer::backgroundPreference() const noexcept {
  return wallpaper_.preference();
}

void Renderer::setBackgroundLevel(int level) noexcept {
  wallpaper_.setLevel(level);
}

int Renderer::backgroundLevel() const noexcept {
  return wallpaper_.level();
}

void Renderer::advanceBackgroundClock(std::uint32_t deltaMs) {
  wallpaper_.advance(deltaMs);
}

void Renderer::advanceBackgroundFrames(int frames) {
  wallpaper_.advanceFrames(frames);
}

void Renderer::freezeBackgroundClock(bool frozen) noexcept {
  wallpaper_.setFrozen(frozen);
}

bool Renderer::backgroundClockFrozen() const noexcept {
  return wallpaper_.frozen();
}

void Renderer::resetBackgroundRound() { wallpaper_.beginRound(); }

backgrounds::Zones backgroundZones(Orientation orientation) {
  backgrounds::Zones zones;
  if (orientation == Orientation::Portrait) {
    const SDL_Rect frame = portraitBoardFrame();
    zones.canvasWidth = kPortraitCanvasWidth;
    zones.canvasHeight = kPortraitCanvasHeight;
    zones.frame = backgrounds::BgRect{frame.x, frame.y, frame.w, frame.h};
    // The loud margins beside the board start outside the 12 px guards; the
    // strip below the frame is loud across the full width. The HUD strip
    // above the frame stays quiet implicitly: no accent draws there because
    // no loud margin covers it.
    const int leftWidth = frame.x - kPortraitBackgroundGuard;
    const int rightX = frame.x + frame.w + kPortraitBackgroundGuard;
    zones.loud[0] = backgrounds::BgRect{0, frame.y, leftWidth, frame.h};
    zones.loud[1] = backgrounds::BgRect{rightX, frame.y,
                                        kPortraitCanvasWidth - rightX,
                                        frame.h};
    zones.loud[2] = backgrounds::BgRect{
        0, frame.y + frame.h, kPortraitCanvasWidth,
        kPortraitCanvasHeight - (frame.y + frame.h)};
    zones.loudCount = 3;
    return zones;
  }
  const SDL_Rect frame = boardFrame();
  zones.canvasWidth = kCanvasWidth;
  zones.canvasHeight = kCanvasHeight;
  zones.frame = backgrounds::BgRect{frame.x, frame.y, frame.w, frame.h};
  // The quiet span of the two HUD columns and their gaps runs from the left
  // column's outer edge to the right column's outer edge; the canvas outside
  // it is loud.
  const int quietEnd = kRightPanelX + kRightPanelWidth;
  zones.loud[0] = backgrounds::BgRect{0, 0, kLeftPanelX, kCanvasHeight};
  zones.loud[1] =
      backgrounds::BgRect{quietEnd, 0, kCanvasWidth - quietEnd, kCanvasHeight};
  zones.loudCount = 2;
  return zones;
}

// The backgrounds::Painter adapter: one background fill is one renderer
// fillRect, so every background pixel passes the same logical-space drawing
// path - including the portrait rotation - as every other pixel.
class Renderer::WallpaperPainter final : public backgrounds::Painter {
 public:
  explicit WallpaperPainter(const Renderer& renderer) : renderer_(renderer) {}

  void fill(backgrounds::BgColor color, int x, int y, int w, int h) override {
    renderer_.fillRect(SDL_Color{color.r, color.g, color.b, 255}, x, y, w, h);
  }

 private:
  const Renderer& renderer_;
};

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

// ---------------------------------------------------------------------------
// The per-piece in-cell designs of task T95 (see the pattern table in
// stackfall/renderer_sdl.hpp). One mark per piece type, built from at most
// four filled rectangles inside the interior the cell frame leaves: the
// interior starts two pixels in from each edge - the one-pixel outline plus
// the one-pixel light and dark frame - and measures n = size - 4 on a side.
//
//   rule thickness t: one pixel below a 16 px interior, two to 31, three above
//   rule inset     m: max(1, n / 8), the margin a rule keeps inside the frame
//   square side    d: max(2, n / 4), the small squares of S, Z, and L
//   ring inset     q: n / 4, where the hollow square of O sits
//
// Every mark centers with (n - extent) / 2, so it sits in the middle of its
// cell at every supported size - the 12 px portrait preview, the 15 px
// landscape preview, the 27 px landscape cell, and the 40 px portrait cell -
// and a smaller or odd extent is off by one pixel at most. A size below 8
// leaves no interior a mark fits in and yields the empty design, and a mark
// that would cross the interior of a small odd cell is dropped rather than
// clipped, so a degenerate call draws the plain cell instead of an
// unpredictable one.
// ---------------------------------------------------------------------------
CellDesign cellDesign(PieceId id, int size) {
  CellDesign design;
  const int n = size - 4;
  if (n < 4) {
    return design;
  }
  const int t = n < 16 ? 1 : (n < 32 ? 2 : 3);
  const int m = std::max(1, n / 8);
  const int d = std::max(2, n / 4);
  const int q = n / 4;
  const int rule = (n - t) / 2;  // where a centered rule of thickness t starts
  const int square = (n - d) / 2;  // where a centered square of side d starts
  const int o = 2;                // the interior origin, inside the frame
  const auto add = [&design, size](int x, int y, int w, int h) {
    if (design.count < kCellDesignMaxRects && w > 0 && h > 0 && x >= 2 &&
        y >= 2 && x + w <= size - 2 && y + h <= size - 2) {
      design.rects[design.count] = SDL_Rect{x, y, w, h};
      ++design.count;
    }
  };
  switch (id) {
    case PieceId::I:  // one wide horizontal rule
      add(o + m, o + rule, n - 2 * m, t);
      break;
    case PieceId::O:  // one hollow square
      add(o + q, o + q, n - 2 * q, t);
      add(o + q, o + n - q - t, n - 2 * q, t);
      add(o + q, o + q + t, t, n - 2 * q - 2 * t);
      add(o + n - q - t, o + q + t, t, n - 2 * q - 2 * t);
      break;
    case PieceId::T:  // one plus, two crossing rules
      add(o + m, o + rule, n - 2 * m, t);
      add(o + rule, o + m, t, n - 2 * m);
      break;
    case PieceId::S:  // two squares stacked
      add(o + square, o + m, d, d);
      add(o + square, o + n - m - d, d, d);
      break;
    case PieceId::Z:  // two squares side by side
      add(o + m, o + square, d, d);
      add(o + n - m - d, o + square, d, d);
      break;
    case PieceId::J:  // one tall vertical rule
      add(o + rule, o + m, t, n - 2 * m);
      break;
    case PieceId::L:  // one small filled square
      add(o + square, o + square, d, d);
      break;
  }
  return design;
}

// One block: a dark one-pixel outline, the flat fill, a lighter top and left
// edge, and a darker bottom and right edge. Straight rectangles and lines
// give the flat geometric look of section 6.2 without any shading texture.
// The per-piece designs of task T95 add the type's mark inside the interior
// the frame leaves, in the palette's own dark shade, so the mark reads as
// part of the cell's shading language rather than a new color.
void Renderer::drawCell(int x, int y, int size, PieceId id,
                        const CellPalette& palette) const {
  fillRect(palette.edge, x, y, size, size);
  fillRect(palette.fill, x + 1, y + 1, size - 2, size - 2);
  drawUnitLine(palette.light, x + 1, y + 1, size - 2, 1);
  drawUnitLine(palette.light, x + 1, y + 1, 1, size - 2);
  drawUnitLine(palette.dark, x + 1, y + size - 2, size - 2, 1);
  drawUnitLine(palette.dark, x + size - 2, y + 1, 1, size - 2);
  if (pieceDesigns_) {
    const CellDesign design = cellDesign(id, size);
    for (int index = 0; index < design.count; ++index) {
      const SDL_Rect& mark = design.rects[index];
      fillRect(palette.dark, x + mark.x, y + mark.y, mark.w, mark.h);
    }
  }
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
      drawControlsScreen("A CUSTOMIZE - B BACK TO TITLE");
      return;
    case Scene::PauseControls:
      // The same mappings page over the frozen round; back returns to the
      // pause overlay, not the title (task T037), and select opens the
      // customization screen of task T066 over the same frozen round.
      drawControlsScreen("A CUSTOMIZE - B BACK TO PAUSE");
      return;
    case Scene::Rebind:
      drawRebindScreen(game);
      return;
    case Scene::Options:
      drawOptionsScreen(game);
      return;
    case Scene::MusicOptions:
      drawMusicOptionsScreen(game);
      return;
    case Scene::BackgroundOptions:
      drawBackgroundOptionsScreen(game);
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

// The play background is the wallpaper layer plus the darker board well, so
// the field reads as a recessed area before anything else lands on it
// (specification section 6.2, step 1; task T078 for the wallpaper).
//
// Mode Off keeps the legacy path exactly: the flat canvas clear and nothing
// else, so Off skips all pattern and animation work and the play pixels of
// the years before the designs stay as they were. A design mode paints the
// wallpaper: one whole-canvas base fill - the ground layer the opaque well
// and its ring then cover - and the design's shapes on top, every one of
// them held outside the frame by the clipping layer, so no shape ever lands
// inside the well or over its ring. The menus keep clearBackground(); only
// the play scene, and the pause and game-over overlays above its frozen
// frame, carry the wallpaper.
void Renderer::drawBackground() const {
  WallpaperPainter painter(*this);
  if (!wallpaper_.draw(backgroundZones(orientation_), painter)) {
    clearBackground();
  }
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
               static_cast<PieceId>(code),
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
    drawCell(cellXNow(cell.x), cellYNow(cell.y), boardCellSize(), active.id,
             palette);
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
             cellSize, id, palette);
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

// The controls page: three aligned columns of the current binding tables plus
// one footer line. The eight gameplay rows read the tables this renderer
// mirrors (task T066), so the page shows the section 5 defaults on a fresh
// installation and whatever the player bound afterwards; the last three rows
// name the fixed menu roles, which no binding moves and which are the reliable
// way back through every menu. The title opens it with "A CUSTOMIZE - B BACK
// TO TITLE"; the pause overlay opens the same page over its frozen round with
// "A CUSTOMIZE - B BACK TO PAUSE" (task T037), because ordinary gameplay
// carries no permanent legend. Portrait cannot hold the three columns side by
// side - they measure 570 px of a 544 px canvas - so it stacks one action
// heading over the two mappings of each row (task T055).
void Renderer::drawControlsScreen(const char* footer) const {
  clearBackground();
  const int centerX = canvasWidth() / 2;
  drawTextCentered(centerX, portrait() ? kPControlsHeadingY : kControlsHeadingY,
                   kControlsHeadingScale, kText, "CONTROLS");
  const ControlTable& padTable =
      bindings_.table(DeviceKind::Controller, orientation_);
  const ControlTable& keyTable =
      bindings_.table(DeviceKind::Keyboard, orientation_);
  char padText[40];
  char keyText[40];
  if (portrait()) {
    drawText(kPControlsGamepadX, kPControlsHeaderY, kLabelScale, kTextDim,
             "GAMEPAD");
    drawText(kPControlsKeyboardX, kPControlsHeaderY, kLabelScale, kTextDim,
             "KEYBOARD");
    int y = kPControlsFirstRowY;
    for (int row = 0; row < kControlsRowCount; ++row) {
      const char* action;
      const char* gamepad;
      const char* keyboard;
      const bool dim = row >= kRebindableActionCount;
      if (dim) {
        const FixedControlRow& fixed =
            kPortraitFixedRows[row - kRebindableActionCount];
        action = fixed.action;
        gamepad = fixed.gamepad;
        keyboard = fixed.keyboard;
      } else {
        const GameAction actionId = kRebindableActions[row];
        action = rebindableActionName(actionId);
        controlListText(DeviceKind::Controller, padTable, actionId,
                        orientation_, padText, sizeof(padText));
        controlListText(DeviceKind::Keyboard, keyTable, actionId, orientation_,
                        keyText, sizeof(keyText));
        gamepad = padText;
        keyboard = keyText;
      }
      drawText(kPControlsGamepadX, y, kLabelScale, kTextDim, action);
      drawText(kPControlsGamepadX, y + kGlyphRows * kLabelScale + 4, kLabelScale,
               kText, gamepad);
      drawText(kPControlsKeyboardX, y + kGlyphRows * kLabelScale + 4,
               kLabelScale, kText, keyboard);
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
  for (int row = 0; row < kControlsRowCount; ++row) {
    const char* gamepad;
    const char* keyboard;
    const char* action;
    if (row >= kRebindableActionCount) {
      const FixedControlRow& fixed =
          kLandscapeFixedRows[row - kRebindableActionCount];
      gamepad = fixed.gamepad;
      keyboard = fixed.keyboard;
      action = fixed.action;
    } else {
      const GameAction actionId = kRebindableActions[row];
      controlListText(DeviceKind::Controller, padTable, actionId, orientation_,
                      padText, sizeof(padText));
      controlListText(DeviceKind::Keyboard, keyTable, actionId, orientation_,
                      keyText, sizeof(keyText));
      gamepad = padText;
      keyboard = keyText;
      action = rebindableActionName(actionId);
    }
    drawText(kControlsGamepadX, y, kLabelScale, kText, gamepad);
    drawText(kControlsKeyboardX, y, kLabelScale, kText, keyboard);
    drawText(kControlsActionX, y, kLabelScale, kText, action);
    y += kControlsRowHeight;
  }
  drawTextCentered(centerX, kControlsFooterY, kLabelScale, kText, footer);
}

// The controls customization page of task T066: the same three columns, one
// row per rebindable gameplay action showing what each device currently
// holds, one RESTORE DEFAULTS row that returns every table of both
// orientations to the section 5 defaults, one status line that names the
// outcome of the last capture or restore, and a footer that names the
// capture's rule. The highlighted row draws its marker; the row whose capture
// is running draws in the accent color, so the player can see which action
// the next press names. The core moves the cursor alone; the binding tables,
// the capture row, and the status line are the platform state this renderer
// mirrors.
void Renderer::drawRebindScreen(const Game& game) const {
  clearBackground();
  const int centerX = canvasWidth() / 2;
  drawTextCentered(centerX, portrait() ? kPControlsHeadingY : kControlsHeadingY,
                   kControlsHeadingScale, kText, "SET CONTROLS");
  const ControlTable& padTable =
      bindings_.table(DeviceKind::Controller, orientation_);
  const ControlTable& keyTable =
      bindings_.table(DeviceKind::Keyboard, orientation_);
  char padText[40];
  char keyText[40];
  const int selected = game.rebindSelection();
  if (portrait()) {
    drawText(kPControlsGamepadX, kPControlsHeaderY, kLabelScale, kTextDim,
             "GAMEPAD");
    drawText(kPControlsKeyboardX, kPControlsHeaderY, kLabelScale, kTextDim,
             "KEYBOARD");
    for (int row = 0; row < kRebindableActionCount; ++row) {
      const int y = kPRebindFirstRowY + row * kPRebindRowHeight;
      const bool capturingRow = row == rebindCaptureRow_;
      const SDL_Color color = capturingRow ? kAccent : kText;
      const GameAction actionId = kRebindableActions[row];
      controlListText(DeviceKind::Controller, padTable, actionId, orientation_,
                      padText, sizeof(padText));
      controlListText(DeviceKind::Keyboard, keyTable, actionId, orientation_,
                      keyText, sizeof(keyText));
      drawText(kPControlsGamepadX, y, kLabelScale,
               row == selected ? kAccent : kTextDim,
               rebindableActionName(actionId));
      drawText(kPControlsGamepadX, y + kGlyphRows * kLabelScale + 4, kLabelScale,
               color, padText);
      drawText(kPControlsKeyboardX, y + kGlyphRows * kLabelScale + 4,
               kLabelScale, color, keyText);
    }
    const bool restoreSelected = selected == kRebindRestoreRow;
    drawText(kPControlsGamepadX, kPRebindRestoreY, kLabelScale,
             restoreSelected ? kAccent : kText, "RESTORE DEFAULTS");
    if (rebindMessage_[0] != '\0') {
      drawText(kPControlsGamepadX, kPRebindMessageY, kLabelScale, kAccent,
               rebindMessage_);
    }
    drawTextCentered(centerX, kPRebindFooterY, kHintScale, kTextDim,
                     rebindCaptureRow_ >= 0 ? rebindCaptureHintText()
                                            : rebindHintText());
    return;
  }
  drawText(kControlsGamepadX, kControlsHeaderY, kLabelScale, kTextDim,
           "GAMEPAD");
  drawText(kControlsKeyboardX, kControlsHeaderY, kLabelScale, kTextDim,
           "KEYBOARD");
  drawText(kControlsActionX, kControlsHeaderY, kLabelScale, kTextDim,
           "ACTION");
  for (int row = 0; row < kRebindableActionCount; ++row) {
    const int y = kRebindFirstRowY + row * kRebindRowHeight;
    const bool capturingRow = row == rebindCaptureRow_;
    const SDL_Color color = capturingRow ? kAccent : kText;
    const GameAction actionId = kRebindableActions[row];
    controlListText(DeviceKind::Controller, padTable, actionId, orientation_,
                    padText, sizeof(padText));
    controlListText(DeviceKind::Keyboard, keyTable, actionId, orientation_,
                    keyText, sizeof(keyText));
    drawText(kControlsGamepadX, y, kLabelScale, color, padText);
    drawText(kControlsKeyboardX, y, kLabelScale, color, keyText);
    drawText(kControlsActionX, y, kLabelScale,
             row == selected ? kAccent : kText,
             rebindableActionName(actionId));
    if (row == selected) {
      fillRect(kAccent, kControlsGamepadX - kMenuMarkerGap - kMenuMarkerSize,
               y + (kGlyphRows * kLabelScale - kMenuMarkerSize) / 2,
               kMenuMarkerSize, kMenuMarkerSize);
    }
  }
  drawText(kControlsGamepadX, kRebindRestoreY, kLabelScale,
           selected == kRebindRestoreRow ? kAccent : kText, "RESTORE DEFAULTS");
  if (selected == kRebindRestoreRow) {
    fillRect(kAccent, kControlsGamepadX - kMenuMarkerGap - kMenuMarkerSize,
             kRebindRestoreY + (kGlyphRows * kLabelScale - kMenuMarkerSize) / 2,
             kMenuMarkerSize, kMenuMarkerSize);
  }
  if (rebindMessage_[0] != '\0') {
    drawText(kControlsGamepadX, kRebindMessageY, kLabelScale, kAccent,
             rebindMessage_);
  }
  drawTextCentered(centerX, kRebindFooterY, kHintScale, kTextDim,
                   rebindCaptureRow_ >= 0 ? rebindCaptureHintText()
                                          : rebindHintText());
}

// The options page of tasks T040, T044, T055, T062, T068, T084, and T095:
// one heading, the ten setting rows at the shared menu metrics, and one
// footer line. The rows carry their value as ON/OFF, the NEXT PIECES count
// digit, or the ORIENTATION, MUSIC, or background mode name, read from the
// preferences this renderer mirrors, because the rules core moves the cursor
// alone and never holds a row value (specification section 7). A changes the
// highlighted row - and on the MUSIC row opens the music settings page, on
// the BACKGROUNDS row the backgrounds page, both of which the core owns -
// and B returns to the title; the platform layer owns the value actions. The
// ORIENTATION row names the layout this process draws, because the select
// that changes it turns the session at once (task T068): the row and the
// layout can no longer disagree, so one mirror serves both.
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
  char backgroundsText[20];
  std::snprintf(backgroundsText, sizeof(backgroundsText), "BACKGROUNDS %s",
                backgrounds::modeDisplayName(wallpaper_.mode()));
  const char* rows[kOptionsItemCount] = {
      soundOn_ ? "SOUND ON" : "SOUND OFF",
      showGhost_ ? "GHOST ON" : "GHOST OFF",
      showGrid_ ? "GRID ON" : "GRID OFF",
      showHold_ ? "HOLD ON" : "HOLD OFF",
      nextText,
      grayscale_ ? "GRAYSCALE ON" : "GRAYSCALE OFF",
      orientation_ == Orientation::Portrait ? "ORIENTATION PORTRAIT"
                                            : "ORIENTATION LANDSCAPE",
      musicText,
      backgroundsText,
      pieceDesigns_ ? "PIECE DESIGNS ON" : "PIECE DESIGNS OFF",
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

// The backgrounds page behind the BACKGROUNDS row (task T084,
// docs/BACKGROUND_IMPLEMENTATION.md): the same heading, menu metrics, and
// footer hint as the options page, over its two rows - the wallpaper mode
// Auto / Off / Fixed and the preferred design with its stable number and
// approved name. Both values are the wallpaper state this renderer already
// mirrors: the mode row reads the mode the play scene draws, and the design
// row the preference that survives switches to Off and Auto, so the page and
// the wallpaper can never disagree. A cycles the highlighted row and B
// returns to the options screen; the platform layer owns both actions.
void Renderer::drawBackgroundOptionsScreen(const Game& game) const {
  clearBackground();
  const int centerX = canvasWidth() / 2;
  drawTextCentered(centerX, portrait() ? kPOptionsHeadingY : kOptionsHeadingY,
                   kOptionsHeadingScale, kText, "BACKGROUNDS");
  char modeText[16];
  std::snprintf(modeText, sizeof(modeText), "MODE %s",
                backgrounds::modeDisplayName(wallpaper_.mode()));
  char designText[48];
  const backgrounds::Design* design =
      backgrounds::designOf(wallpaper_.preference());
  std::snprintf(designText, sizeof(designText), "DESIGN %d",
                wallpaper_.preference());
  if (design != nullptr) {
    // The approved name in the menu's uppercase spelling, after the number.
    const std::size_t at = std::strlen(designText);
    designText[at] = ' ';
    std::size_t out = at + 1;
    for (const char* p = design->name;
         *p != '\0' && out + 1 < sizeof(designText); ++p) {
      designText[out++] =
          *p >= 'a' && *p <= 'z' ? static_cast<char>(*p - 'a' + 'A') : *p;
    }
    designText[out] = '\0';
  }
  const char* rows[kBackgroundOptionsItemCount] = {
      modeText,
      designText,
  };
  const int menuY = portrait() ? kPOptionsMenuY : kOptionsMenuY;
  for (int index = 0; index < kBackgroundOptionsItemCount; ++index) {
    drawMenuItem(centerX, menuY + index * kMenuItemHeight, kMenuScale,
                 rows[index], index == game.backgroundsSelection());
  }
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
