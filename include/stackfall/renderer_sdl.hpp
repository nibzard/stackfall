#pragma once

// The procedural SDL2 renderer of milestone M2 (task T010, specification
// sections 6.1 and 6.2).
//
// Renderer::draw(const Game&) is a pure reader of the round: it calls the
// const queries of Game, never a mutator, and it draws every pixel from
// integer coordinates, colors, and piece masks. There is no texture, no
// surface blit, and no image or font loading anywhere in this layer; all text
// comes from the source-defined 5x7 glyph table of stackfall/font.hpp, drawn
// as filled rectangles (specification section 6.2).
//
// All coordinates below are logical pixels on the 960x544 canvas. The owner
// of the SDL_Renderer sets that canvas through applyCanvas(), which the game
// binary does once after creating the renderer and again whenever the window
// resizes; the renderer then draws in logical units and SDL scales and
// letterboxes the output for any window size.
//
// The layout and palette constants are constexpr and live in this header so
// the SDL-path suite in tests/test_renderer_sdl.cpp can assert exact pixels
// at exact positions without guessing at geometry.

#include <SDL.h>

#include <array>

#include "stackfall/game.hpp"
#include "stackfall/types.hpp"

namespace stackfall {

// ---------------------------------------------------------------------------
// Canvas and board geometry (specification section 6.1).
// ---------------------------------------------------------------------------
inline constexpr int kCanvasWidth = 960;
inline constexpr int kCanvasHeight = 544;

// ---------------------------------------------------------------------------
// The letterbox canvas of section 6.1 (design review finding
// letterbox-scene-centered).
//
// A window whose size differs from the canvas letterboxes: SDL scales the
// canvas to fit and centers it, leaving slack bars that no scene draw ever
// touches. Two rules keep those bars correct on every window size.
//
// First, applyCanvas() runs again on every window resize. SDL recomputes the
// letterbox viewport itself on current builds, but a build that does not
// would keep the viewport of the old window size and anchor the scene at the
// top of the new one, so the game states the canvas again instead of relying
// on that.
//
// Second, beginFrame() paints a black base across the whole output before it
// restores the canvas. The renderer clears only the letterbox viewport, so
// without that base the bars hold whatever the window buffers carried before
// the resize: stale scene pixels, never black. At the native canvas size
// there are no bars and beginFrame() draws nothing, so the device path keeps
// its exact pixels.
// ---------------------------------------------------------------------------

// Sets the 960x544 logical canvas. Returns false when SDL rejects the size,
// leaving the renderer at its previous transform.
bool applyCanvas(SDL_Renderer* renderer);

// The black letterbox base plus the canvas of applyCanvas(). Call before
// every draw.
void beginFrame(SDL_Renderer* renderer);

// Reads the whole renderer output into `surface`, in device pixels: the
// letterbox bars and the scene together, exactly as presented.
// SDL_RenderReadPixels reads in the logical space of the current viewport, so
// with the letterbox active it returns only the scene, anchored at the top of
// the image and padded with black below. `surface` must be at least the
// renderer output size. Returns false when the read fails; the canvas is
// restored either way.
bool readOutputPixels(SDL_Renderer* renderer, SDL_Surface* surface);

// The playing field: 10 x 20 cells of 27 px from (345, 2) to (614, 541),
// the board-cells-27px geometry of specification section 6.1. The board is
// centered on the canvas - (345 + 614) / 2 = 480 and (2 + 541) / 2 = 272 -
// and its one-pixel frame, one pixel outside it, still ends at y=542 on the
// 544-row canvas, so no board pixel clips.
inline constexpr int kBoardCellSize = 27;
inline constexpr int kBoardOriginX = 345;
inline constexpr int kBoardOriginY = 2;
inline constexpr int kBoardPixelWidth = kBoardWidth * kBoardCellSize;    // 270
inline constexpr int kBoardPixelHeight = kBoardHeight * kBoardCellSize;  // 540

// The board frame of section 6.1: one pixel of outline around the whole
// field, x=344..615 and y=1..542. drawBoardFrame() draws exactly this
// rectangle, and the suite asserts its edges on the canvas.
constexpr SDL_Rect boardFrame() {
  return SDL_Rect{kBoardOriginX - 1, kBoardOriginY - 1, kBoardPixelWidth + 2,
                  kBoardPixelHeight + 2};
}

// Preview pieces reuse the shape cell coordinates at a smaller cell
// (specification section 6.1).
inline constexpr int kPreviewCellSize = 15;

// ---------------------------------------------------------------------------
// The focused gameplay HUD of the approved composition (task T037,
// specification section 6.1, design review finding focused-gameplay-hud).
//
// Two narrow columns flank the board, each one anchored to its near board
// edge with about 24 pixels of quiet gap, and the outer regions of the canvas
// stay empty. The left column carries the round's statistics - Hold, score,
// lines, and level; the right column carries the five Next previews. The
// suggested 128-pixel column width is a starting point, not a clipping rule:
// the numbers below hold the geometry, and the one rule that cannot bend is
// that no HUD pixel reaches the board or the other column. Ordinary gameplay
// draws no permanent control legend; the mappings live on the controls
// screen, reachable from the title and from the pause overlay.
// ---------------------------------------------------------------------------
inline constexpr int kHudGap = 24;          // quiet columns beside the frame
inline constexpr int kHudColumnWidth = 128; // the suggested starting width

// The left statistics column ends at the gap before the board frame, so it
// grows leftward from x=319; the right preview column starts at the gap
// after the frame. Both stay symmetric about the board center (480).
inline constexpr int kLeftPanelX =
    boardFrame().x - kHudGap - kHudColumnWidth;          // 192
inline constexpr int kLeftPanelWidth = kHudColumnWidth;  // x = 192..319
inline constexpr int kRightPanelX =
    boardFrame().x + boardFrame().w + kHudGap;           // 640
inline constexpr int kRightPanelWidth = kHudColumnWidth; // x = 640..767

// One preview box: four cells at the preview size plus breathing room. Five
// slots plus their padding fit beside the board rows (y = 2..541).
inline constexpr int kPreviewBoxWidth = 120;
inline constexpr int kPreviewBoxHeight = 56;
inline constexpr int kHoldBoxHeight = 60;
inline constexpr int kNextPanelHeight = kPreviewCount * kPreviewBoxHeight + 10;

// The group headings sit just above their boxes, at the board's top edge.
inline constexpr int kPanelHeadingY = kBoardOriginY + 2;
inline constexpr int kPanelBoxY = kBoardOriginY + 28;

// The statistics fields of the left column, below the hold box: one label
// over one value per field, fifty pixels per field.
inline constexpr int kStatsFirstLabelY = kPanelBoxY + kHoldBoxHeight + 20;
inline constexpr int kStatsFieldStride = 50;

// The hold box and the five next slots, in canvas coordinates. The tests and
// the renderer share these functions, so an asserted pixel position and a
// drawn pixel position can never drift apart.
constexpr SDL_Rect holdBox() {
  return SDL_Rect{kLeftPanelX + (kLeftPanelWidth - kPreviewBoxWidth) / 2,
                  kPanelBoxY, kPreviewBoxWidth, kHoldBoxHeight};
}

constexpr SDL_Rect nextPanelBox() {
  return SDL_Rect{kRightPanelX + (kRightPanelWidth - kPreviewBoxWidth) / 2,
                  kPanelBoxY, kPreviewBoxWidth, kNextPanelHeight};
}

constexpr SDL_Rect nextSlotBox(int index) {
  return SDL_Rect{nextPanelBox().x,
                  nextPanelBox().y + 5 + index * kPreviewBoxHeight,
                  kPreviewBoxWidth, kPreviewBoxHeight};
}

// ---------------------------------------------------------------------------
// Screen geometry of the section 4.7 screens (task T011): the title and
// controls pages and the pause and game-over overlays. The same rule as the
// play layout applies: the constants live here, so the SDL-path suite asserts
// exact pixels at exact positions without guessing at geometry.
// ---------------------------------------------------------------------------

// Menu text: one scale for every menu item, one row height, and a filled
// square marker that sits left of the selected item with a fixed gap.
inline constexpr int kMenuScale = 3;
inline constexpr int kMenuItemHeight = 44;
inline constexpr int kMenuMarkerSize = 12;
inline constexpr int kMenuMarkerGap = 20;

// The title page: logo, the seven pieces as a decorative preview row, the
// four-item menu, the high score, and one hint line.
inline constexpr int kTitleLogoY = 56;
inline constexpr int kTitleLogoScale = 8;
inline constexpr int kTitlePreviewY = 124;
inline constexpr int kTitlePreviewBoxWidth = 72;
inline constexpr int kTitlePreviewBoxHeight = 48;
inline constexpr int kTitlePreviewStride = 76;
inline constexpr int kTitlePreviewRowX =
    (kCanvasWidth -
     (kPieceCount * kTitlePreviewStride - (kTitlePreviewStride - kTitlePreviewBoxWidth))) / 2;
inline constexpr int kTitleMenuY = 216;
inline constexpr int kTitleHighScoreLabelY = 396;
inline constexpr int kTitleHighScoreValueY = 418;
inline constexpr int kTitleHintY = 512;

constexpr SDL_Rect titlePreviewBox(int index) {
  return SDL_Rect{kTitlePreviewRowX + index * kTitlePreviewStride, kTitlePreviewY,
                  kTitlePreviewBoxWidth, kTitlePreviewBoxHeight};
}

// The controls page: three left-aligned columns under one heading.
inline constexpr int kControlsHeadingY = 44;
inline constexpr int kControlsHeadingScale = 4;
inline constexpr int kControlsHeaderY = 112;
inline constexpr int kControlsFirstRowY = 142;
inline constexpr int kControlsRowHeight = 26;
inline constexpr int kControlsFooterY = 452;
inline constexpr int kControlsGamepadX = 110;
inline constexpr int kControlsKeyboardX = 410;
inline constexpr int kControlsActionX = 710;

// The options page (tasks T040 and T044): one heading, the six setting rows
// at the shared menu metrics, one footer. The rows read their value from the
// renderer's preference settings, because the rules core holds the cursor
// alone and the values are platform state (specification section 7). Six rows
// at the 44 px menu height span y = 144..385, so the heading moves up and the
// footer moves down from the two-row layout; everything stays inside the
// 960x544 canvas with the hint glyphs at their 14 px scale (task T037).
inline constexpr int kOptionsHeadingY = 64;
inline constexpr int kOptionsHeadingScale = 4;
inline constexpr int kOptionsMenuY = 144;
inline constexpr int kOptionsFooterY = 456;

// The overlays: a translucent veil over the frozen play scene plus one solid
// panel that carries the text. Offsets are panel-relative.
inline constexpr int kOverlayVeilAlpha = 176;  // 0..255 dim of the play scene
inline constexpr int kPauseHeadingOffsetY = 36;
inline constexpr int kPauseMenuOffsetY = 112;
inline constexpr int kPauseHintOffsetY = 300;
inline constexpr int kOverHeadingOffsetY = 32;
inline constexpr int kOverStatsOffsetY = 104;
inline constexpr int kOverStatsRowHeight = 44;
inline constexpr int kOverRecordOffsetY = 240;
inline constexpr int kOverMenuOffsetY = 296;
inline constexpr int kOverHintOffsetY = 404;

// The pause panel nests between the two HUD columns (x = 320..639, task
// T037): the frozen round's statistics and previews stay visible beside it
// under the veil, so the HUD reads continuously even while paused. The
// game-over panel may span the columns; the round it reports is over.
constexpr SDL_Rect pausePanel() {
  return SDL_Rect{320, 92, kRightPanelX - kLeftPanelX - kLeftPanelWidth,
                  360};
}

constexpr SDL_Rect gameoverPanel() {
  return SDL_Rect{240, 52, 480, 440};
}

// ---------------------------------------------------------------------------
// Palette. Seven piece colors plus the UI colors, all defined here so the
// whole game draws from one seven-color scheme with high-contrast outlines
// (specification sections 2 and 6.2).
// ---------------------------------------------------------------------------
inline constexpr SDL_Color kBackground{16, 18, 26, 255};
inline constexpr SDL_Color kBoardWell{10, 12, 19, 255};
inline constexpr SDL_Color kGridLine{26, 30, 42, 255};
inline constexpr SDL_Color kFrame{78, 88, 116, 255};
inline constexpr SDL_Color kText{232, 236, 246, 255};
inline constexpr SDL_Color kTextDim{138, 148, 172, 255};
inline constexpr SDL_Color kCellEdge{8, 9, 14, 255};
// The selection marker of a menu and the new-record note draw in one accent
// shade, the cyan of the I piece, so the highlight reads on the dark ground.
inline constexpr SDL_Color kAccent{36, 204, 216, 255};

// Blends `from` toward `to` by parts/whole with integer arithmetic only.
constexpr Uint8 mixChannel(Uint8 from, Uint8 to, int parts, int whole) {
  const int blended = static_cast<int>(from) +
                      (static_cast<int>(to) - static_cast<int>(from)) * parts /
                          whole;
  return static_cast<Uint8>(blended);
}

constexpr SDL_Color mixToward(SDL_Color from, SDL_Color to, int parts,
                              int whole) {
  return SDL_Color{mixChannel(from.r, to.r, parts, whole),
                   mixChannel(from.g, to.g, parts, whole),
                   mixChannel(from.b, to.b, parts, whole), from.a};
}

// The solid face of the pause and game-over panels: the background lifted
// toward the frame color, so each panel reads as one card over the veil.
inline constexpr SDL_Color kPanelFill = mixToward(kBackground, kFrame, 2, 5);

// The three shades one block draws with: a flat fill, a lighter top-left
// highlight, and a darker bottom-right shade, all under one dark outline.
struct CellPalette {
  SDL_Color edge;
  SDL_Color fill;
  SDL_Color light;
  SDL_Color dark;
};

// The seven custom piece colors, in PieceId order I, O, T, S, Z, J, L.
inline constexpr std::array<SDL_Color, kPieceCount> kPieceFills = {{
    {36, 204, 216, 255},   // I: cyan
    {248, 200, 64, 255},   // O: amber
    {176, 112, 224, 255},  // T: violet
    {72, 208, 104, 255},   // S: green
    {232, 84, 96, 255},    // Z: crimson
    {88, 128, 240, 255},   // J: blue
    {244, 148, 60, 255},   // L: orange
}};

constexpr CellPalette cellPaletteOf(SDL_Color fill) {
  return CellPalette{kCellEdge, fill, mixToward(fill, SDL_Color{255, 255, 255, 255}, 2, 5),
                     mixToward(fill, SDL_Color{0, 0, 0, 255}, 1, 2)};
}

inline constexpr std::array<CellPalette, kPieceCount> kPiecePalettes = {{
    cellPaletteOf(kPieceFills[0]), cellPaletteOf(kPieceFills[1]),
    cellPaletteOf(kPieceFills[2]), cellPaletteOf(kPieceFills[3]),
    cellPaletteOf(kPieceFills[4]), cellPaletteOf(kPieceFills[5]),
    cellPaletteOf(kPieceFills[6]),
}};

inline constexpr std::array<CellPalette, kPieceCount> kPiecePalettesDim = {{
    cellPaletteOf(mixToward(kPieceFills[0], kBackground, 1, 2)),
    cellPaletteOf(mixToward(kPieceFills[1], kBackground, 1, 2)),
    cellPaletteOf(mixToward(kPieceFills[2], kBackground, 1, 2)),
    cellPaletteOf(mixToward(kPieceFills[3], kBackground, 1, 2)),
    cellPaletteOf(mixToward(kPieceFills[4], kBackground, 1, 2)),
    cellPaletteOf(mixToward(kPieceFills[5], kBackground, 1, 2)),
    cellPaletteOf(mixToward(kPieceFills[6], kBackground, 1, 2)),
}};

// The grayscale play scene of the options screen (task T044). Grayscale on
// swaps the piece colors - settled cells, the active piece, the ghost, and
// the Hold and Next previews - for seven distinct grays while the text, the
// outlines, the board, and every other UI color stay as they are, so shapes
// and labels keep their contrast. The shades follow the luminance order of
// the seven color fills (O brightest, J darkest) and never drop below 130:
// the ghost blend of one quarter toward the well then holds the same 3:1
// outline contrast floor as the color mode for every piece, and the darkest
// live shade stays clearly brighter than its ghost (J reaches 3.35:1 against
// the well and 1.52:1 against its fill).
inline constexpr std::array<SDL_Color, kPieceCount> kGrayscaleFills = {{
    {205, 205, 205, 255},  // I: cyan, luminance 0.49
    {235, 235, 235, 255},  // O: amber, the brightest fill
    {150, 150, 150, 255},  // T: violet
    {190, 190, 190, 255},  // S: green
    {140, 140, 140, 255},  // Z: crimson
    {130, 130, 130, 255},  // J: blue, the darkest fill
    {170, 170, 170, 255},  // L: orange
}};

inline constexpr std::array<CellPalette, kPieceCount> kGrayscalePalettes = {{
    cellPaletteOf(kGrayscaleFills[0]), cellPaletteOf(kGrayscaleFills[1]),
    cellPaletteOf(kGrayscaleFills[2]), cellPaletteOf(kGrayscaleFills[3]),
    cellPaletteOf(kGrayscaleFills[4]), cellPaletteOf(kGrayscaleFills[5]),
    cellPaletteOf(kGrayscaleFills[6]),
}};

inline constexpr std::array<CellPalette, kPieceCount> kGrayscalePalettesDim = {{
    cellPaletteOf(mixToward(kGrayscaleFills[0], kBackground, 1, 2)),
    cellPaletteOf(mixToward(kGrayscaleFills[1], kBackground, 1, 2)),
    cellPaletteOf(mixToward(kGrayscaleFills[2], kBackground, 1, 2)),
    cellPaletteOf(mixToward(kGrayscaleFills[3], kBackground, 1, 2)),
    cellPaletteOf(mixToward(kGrayscaleFills[4], kBackground, 1, 2)),
    cellPaletteOf(mixToward(kGrayscaleFills[5], kBackground, 1, 2)),
    cellPaletteOf(mixToward(kGrayscaleFills[6], kBackground, 1, 2)),
}};

// Piece index 0..6 from a PieceId, for the palette arrays above.
constexpr int pieceIndex(PieceId id) { return static_cast<int>(id) - 1; }

constexpr const CellPalette& cellPalette(PieceId id) {
  return kPiecePalettes[pieceIndex(id) < 0 || pieceIndex(id) >= kPieceCount
                            ? 0
                            : pieceIndex(id)];
}

constexpr const CellPalette& dimCellPalette(PieceId id) {
  return kPiecePalettesDim[pieceIndex(id) < 0 || pieceIndex(id) >= kPieceCount
                               ? 0
                               : pieceIndex(id)];
}

// The grayscale counterparts of the four color lookups above, same defensive
// index rule.
constexpr SDL_Color grayscaleFill(PieceId id) {
  return kGrayscaleFills[pieceIndex(id) < 0 || pieceIndex(id) >= kPieceCount
                             ? 0
                             : pieceIndex(id)];
}

constexpr const CellPalette& grayscalePalette(PieceId id) {
  return kGrayscalePalettes[pieceIndex(id) < 0 || pieceIndex(id) >= kPieceCount
                                ? 0
                                : pieceIndex(id)];
}

constexpr const CellPalette& grayscaleDimPalette(PieceId id) {
  return kGrayscalePalettesDim[pieceIndex(id) < 0 ||
                                       pieceIndex(id) >= kPieceCount
                                   ? 0
                                   : pieceIndex(id)];
}

// The ghost blend of one quarter toward the well, applied to the gray fill,
// so the grayscale landing preview holds the same 3:1 outline contrast floor
// as the color one (task T044).
constexpr SDL_Color grayscaleGhostColor(PieceId id) {
  return mixToward(grayscaleFill(id), kBoardWell, 1, 4);
}

// The ghost reads as the same piece at a fraction of its color, so it stays
// identifiable over the dark well without competing with the live piece. One
// quarter of the fill blends toward the well: that is the least piece color
// that keeps every one of the seven outlines above a 3:1 luminance contrast
// against the well (the dimmest, J, reaches 3.48:1), so the landing preview
// stops being the weakest shape on the board. The reviewed blend of three
// fifths toward the well measured 2.44:1 (design review finding
// ghost-outline-contrast).
constexpr SDL_Color ghostColor(PieceId id) {
  return mixToward(kPieceFills[pieceIndex(id) < 0 || pieceIndex(id) >= kPieceCount
                                   ? 0
                                   : pieceIndex(id)],
                   kBoardWell, 1, 4);
}

constexpr SDL_Color pieceFill(PieceId id) {
  return kPieceFills[pieceIndex(id) < 0 || pieceIndex(id) >= kPieceCount ? 0
                                                                         : pieceIndex(id)];
}

// ---------------------------------------------------------------------------
// The renderer. One instance wraps one SDL_Renderer for the whole process and
// keeps no game state of its own beyond the platform settings it mirrors: the
// grid, the ghost, the sound, the hold preview, the Next preview count, and
// the grayscale mode (task T044).
// ---------------------------------------------------------------------------
class Renderer {
 public:
  // Does not take ownership of `renderer`, which must outlive this object.
  explicit Renderer(SDL_Renderer* renderer) noexcept;

  // Draws one complete frame of the scene the game is in: the title,
  // controls, or options page, the play scene of section 6.2, that scene
  // under the pause or game-over overlay of section 4.7, or the mappings
  // page the pause overlay opens over its frozen round (task T037). Reads
  // the game through the const queries of Game only.
  void draw(const Game& game);

  // Draws `text` with one filled rectangle per font pixel, `scale` canvas
  // pixels per font pixel, starting at the top-left corner (x, y). Public so
  // the screens of milestone M2 task T011 and the SDL-path tests share one
  // glyph path.
  void drawText(int x, int y, int scale, SDL_Color color,
                const char* text) const;

  // Same, centered on the horizontal pixel `centerX`.
  void drawTextCentered(int centerX, int y, int scale, SDL_Color color,
                        const char* text) const;

  // The player preferences the platform layer owns and this renderer mirrors,
  // so the options screen can show each row's current value and the play
  // scene can gate its drawing on it (tasks T040 and T044, specification
  // section 6.3): the faint board grid of section 6.2, the ghost landing
  // preview, the sound, the Hold preview, the count of visible Next previews,
  // and the grayscale play scene. Each defaults to on (five previews for
  // NEXT, off for grayscale), and the settings file supplies the starting
  // values.
  void setShowGrid(bool show) noexcept;
  bool showGrid() const noexcept;
  void setShowGhost(bool show) noexcept;
  bool showGhost() const noexcept;
  void setSoundOn(bool on) noexcept;
  bool soundOn() const noexcept;
  void setShowHold(bool show) noexcept;
  bool showHold() const noexcept;
  // `count` clamps to the preview range 1..kPreviewCount, so a stray value
  // can never read past the queue.
  void setNextPreviews(int count) noexcept;
  int nextPreviews() const noexcept;
  void setGrayscale(bool on) noexcept;
  bool grayscale() const noexcept;

  // The glyph scale one statistics value draws at: the full value scale while
  // the number fits the statistics column, one step smaller when it does not,
  // so the longest value the saturating counters can hold stays inside the
  // column (task T037). Public because the pixel suite pins the rule beside
  // the pixels it produces.
  int statValueScale(const char* value) const;

 private:
  void drawBackground() const;
  void clearBackground() const;
  void drawGridLines() const;
  void drawBoardFrame() const;
  void drawSettledCells(const Game& game) const;
  void drawGhostPiece(const Game& game) const;
  void drawActivePiece(const Game& game) const;
  void drawHoldPanel(const Game& game) const;
  void drawNextPanel(const Game& game) const;
  void drawStatsPanel(const Game& game) const;

  // The section 4.7 screens of task T011.
  void drawPlayScene(const Game& game) const;
  void drawTitleScreen(const Game& game) const;
  void drawControlsScreen(const char* footer) const;
  void drawOptionsScreen(const Game& game) const;
  void drawPauseOverlay(const Game& game) const;
  void drawGameOverOverlay(const Game& game) const;
  void drawOverlayPanel(const SDL_Rect& panel) const;
  void drawVeil() const;
  void drawMenuItem(int centerX, int y, int scale, const char* text,
                    bool selected) const;

  void fillRect(SDL_Color color, int x, int y, int w, int h) const;
  void drawRectOutline(SDL_Color color, int x, int y, int w, int h) const;
  void drawCell(int x, int y, int size, const CellPalette& palette) const;
  void drawGhostCell(int x, int y, int size, SDL_Color color) const;
  void drawPreviewPiece(PieceId id, const SDL_Rect& box, bool dimmed) const;

  // The palette of one piece under the current settings: grayscale on swaps
  // the color palettes for the gray ones everywhere the play scene draws a
  // piece, and off restores the color ones exactly (task T044).
  const CellPalette& piecePalette(PieceId id, bool dimmed) const;
  SDL_Color pieceGhostColor(PieceId id) const;

  SDL_Renderer* renderer_;
  bool showGrid_ = true;
  bool showGhost_ = true;
  bool soundOn_ = true;
  bool showHold_ = true;
  int nextPreviews_ = kPreviewCount;
  bool grayscale_ = false;
};

}  // namespace stackfall
