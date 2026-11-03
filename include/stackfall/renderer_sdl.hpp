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

// The side panels: hold and control hints on the left, next queue and the
// score fields on the right (specification section 6.1).
inline constexpr int kLeftPanelX = 52;
inline constexpr int kLeftPanelWidth = 272;  // x = 52..324
inline constexpr int kRightPanelX = 630;
inline constexpr int kRightPanelWidth = 280;  // x = 630..910

// One preview box: four cells at the preview size plus breathing room. Five
// slots plus their padding and the three score fields fit between the board
// top and bottom rows (y = 2..541).
inline constexpr int kPreviewBoxWidth = 120;
inline constexpr int kPreviewBoxHeight = 56;
inline constexpr int kHoldBoxHeight = 60;
inline constexpr int kNextPanelHeight = kPreviewCount * kPreviewBoxHeight + 10;

// The panel headings sit just above their boxes, clear of the box outline.
inline constexpr int kPanelHeadingY = kBoardOriginY + 2;
inline constexpr int kPanelBoxY = kBoardOriginY + 28;

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
// three-item menu, the high score, and one hint line.
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

// The overlays: a translucent veil over the frozen play scene plus one solid
// panel that carries the text. Offsets are panel-relative.
inline constexpr int kOverlayVeilAlpha = 176;  // 0..255 dim of the play scene
inline constexpr int kPauseHeadingOffsetY = 36;
inline constexpr int kPauseMenuOffsetY = 128;
inline constexpr int kPauseHintOffsetY = 300;
inline constexpr int kOverHeadingOffsetY = 32;
inline constexpr int kOverStatsOffsetY = 104;
inline constexpr int kOverStatsRowHeight = 44;
inline constexpr int kOverRecordOffsetY = 240;
inline constexpr int kOverMenuOffsetY = 296;
inline constexpr int kOverHintOffsetY = 404;

constexpr SDL_Rect pausePanel() {
  return SDL_Rect{260, 92, 440, 360};
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
inline constexpr SDL_Color kPanelLine{44, 50, 70, 255};
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
// keeps no game state of its own beyond the grid setting.
// ---------------------------------------------------------------------------
class Renderer {
 public:
  // Does not take ownership of `renderer`, which must outlive this object.
  explicit Renderer(SDL_Renderer* renderer) noexcept;

  // Draws one complete frame of the scene the game is in: the title or
  // controls page, the play scene of section 6.2, or that scene under the
  // pause or game-over overlay of section 4.7. Reads the game through the
  // const queries of Game only.
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

  // The faint board grid of section 6.2, an optional setting until a settings
  // file arrives in milestone M3. It is on by default.
  void setShowGrid(bool show) noexcept;
  bool showGrid() const noexcept;

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
  void drawScorePanel(const Game& game) const;
  void drawHintsPanel() const;

  // The section 4.7 screens of task T011.
  void drawPlayScene(const Game& game) const;
  void drawTitleScreen(const Game& game) const;
  void drawControlsScreen() const;
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

  SDL_Renderer* renderer_;
  bool showGrid_ = true;
};

}  // namespace stackfall
