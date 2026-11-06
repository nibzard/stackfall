#pragma once

// The procedural level backgrounds of the approved design package (task T078,
// docs/backgrounds and docs/BACKGROUND_IMPLEMENTATION.md).
//
// This header is the SDL-free half of the background system: the catalog of
// the twenty approved designs, the cosmetic hash that drives their layouts,
// the bounded presentation clock, the transition stepper, and the clipping
// layer every design draws through. It links into stackfall_core, so the
// headless suite pins all of it without a display. src/renderer_sdl.cpp
// carries the SDL half: it derives the layout zones from the renderer
// geometry and paints through one adapter into the renderer's primitives.
//
// Everything a design draws is one solid axis-aligned rectangle. There is no
// texture, no image, no shader, and no per-frame allocation anywhere in this
// layer; all state is fixed-size and every draw call is O(1) in the element
// count the design emits.
//
// Determinism: the designs place their elements through hash32(), a CRC32
// over packed little-endian integers that matches the h32() of the design
// package tooling byte for byte. The hash never reads the game, its seeds,
// or its randomizer, so the wallpaper of a level is the same on every run
// and under every gameplay seed. The chapter tasks T079 through T083
// reproduce the record grammars from the same formulas and the same seeds;
// the one float in the layer - the 54 px press of Compression - walks the
// same IEEE-754 double path the design package walks, because that
// design's exact-half clocks round off the tie in the package's arithmetic
// and the runtime matches it pixel for pixel (src/backgrounds.cpp).
//
// Draw cost (the contract the chapter tasks inherit):
//   * One element emits at most 12 fills: an accent-colored element is
//     intersected with the at most three loud margins and each piece is cut
//     around the board frame, which splits a rectangle at most four ways.
//     A secondary-colored element emits at most 4 fills.
//   * The base field is one whole-canvas fill, and a design's elements draw
//     on top of it. The Still chapter of task T079 (levels 1 through 4)
//     emits 8 corner steps, 3 rules and under-bands, 28 stars, or 8 strata
//     plus at most 32 stepped ends - 49 fills at the busiest, landscape
//     Quiet strata. The Stir chapter of task T080 (levels 5 through 8)
//     emits 56 stars and 10 dust flecks (67 fills), 18 zone rectangles and
//     120 dither squares (139), 14 towers, 28 roofs and up to 168 windows
//     (211 portrait), or up to 63 scan rules and 4 bars. The Build
//     chapter of task T081 (levels 9 through 12) emits 48 seam and ledge
//     blocks plus up to 48 sinking ticks (96 fills at the busiest), 64
//     traces, 16 nodes and 6 breathing cores (87 fills), 9 staircase
//     ribbons of one fill per 40 px step the frame ring spares plus 8
//     riding ticks (181 fills at the busiest), or 72 stars, 72 trail
//     steps and up to 12 heads (159 fills at the busiest). The Pressure
//     chapter of task T082 (levels 13 through 16) emits 24 scrolling
//     rules, 40 guide segments and 10 nodes (74 fills at every clock),
//     160 flakes and 3 frame-split bands (167 fills at the busiest), 7
//     staircase bands of one fill per 20 px step the frame ring spares
//     plus up to 31 foam ticks (287 fills at the busiest, landscape
//     Undertow - 7 band elements inside the 40-band budget, expanding
//     like any grouped band), or 72 contour bars and up to 12 corner tips
//     (97 at the busiest). The Peak chapter of task T083 (levels 17
//     through 20) emits 190 embers and up to 78 column fills (268 at
//     every landscape clock, 261 at the busiest portrait), 200 dashes
//     and 20 trail steps (222 at the busiest), 238 press bars and up to
//     22 ticks - 260 placed elements inside the 300-element budget that
//     the frame ring's clip expands to 301 fills at the busiest
//     landscape frame, 80 portrait - or 280 fragments and up to 13 slab
//     fills (293 at the busiest) - one moving layer per design.
//   * A cut costs the same as a steady frame. A fade step draws exactly one
//     design's grammar - never two - so a transition never doubles the
//     steady-state budget.
//   * A design may emit at most kMaxSmallElements small elements or
//     kMaxBandElements band elements, per its catalog budget class.

#include <cstddef>
#include <cstdint>

namespace stackfall {
namespace backgrounds {

// ---------------------------------------------------------------------------
// Colors and the brightness caps of the design contract.
//
// Every cap comparison is componentwise: a color is within a cap when each
// of its red, green, and blue channels is at or below the cap's channel
// (docs/backgrounds/index.md, "The design contract").
// ---------------------------------------------------------------------------

struct BgColor {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
};

// One color from a 0xRRGGBB literal.
constexpr BgColor colorOf(std::uint32_t hex) {
  return BgColor{static_cast<std::uint8_t>((hex >> 16) & 0xFF),
                 static_cast<std::uint8_t>((hex >> 8) & 0xFF),
                 static_cast<std::uint8_t>(hex & 0xFF)};
}

// The caps of the operator brief. Base fields stay inside the floor and the
// ceiling; large secondary shapes stay at or below the secondary cap; small
// accents stay at or below the accent cap and cover at most two percent of
// the canvas (the coverage share is a property of each grammar, checked by
// the design package and the chapter suites).
inline constexpr BgColor kBaseFloor = colorOf(0x0A0C13);
inline constexpr BgColor kBaseCeiling = colorOf(0x1C2130);
inline constexpr BgColor kSecondaryCap = colorOf(0x2A3146);
inline constexpr BgColor kAccentCap = colorOf(0x8A94AC);

// The flat wallpaper of the years before the designs: what mode Off draws.
inline constexpr BgColor kLegacyFlat = colorOf(0x10121A);

constexpr bool withinCap(BgColor color, BgColor cap) {
  return color.r <= cap.r && color.g <= cap.g && color.b <= cap.b;
}

// True when `color` is a legal base field: every channel at or above the
// floor's channel and at or below the ceiling's channel.
constexpr bool baseInRange(BgColor color) {
  return color.r >= kBaseFloor.r && color.g >= kBaseFloor.g &&
         color.b >= kBaseFloor.b && color.r <= kBaseCeiling.r &&
         color.g <= kBaseCeiling.g && color.b <= kBaseCeiling.b;
}

// One channel blended by parts/whole with integer arithmetic only, the same
// arithmetic as mixChannel() of renderer_sdl.hpp.
constexpr std::uint8_t mixChannel(std::uint8_t from, std::uint8_t to, int parts,
                                  int whole) {
  const int blended = static_cast<int>(from) +
                      (static_cast<int>(to) - static_cast<int>(from)) * parts /
                          whole;
  return static_cast<std::uint8_t>(blended);
}

constexpr BgColor mixToward(BgColor from, BgColor to, int parts, int whole) {
  return BgColor{mixChannel(from.r, to.r, parts, whole),
                 mixChannel(from.g, to.g, parts, whole),
                 mixChannel(from.b, to.b, parts, whole)};
}

// The componentwise midpoint of two colors, the anchor a fade passes through.
constexpr BgColor midColor(BgColor a, BgColor b) {
  return BgColor{
      static_cast<std::uint8_t>((static_cast<int>(a.r) + b.r) / 2),
      static_cast<std::uint8_t>((static_cast<int>(a.g) + b.g) / 2),
      static_cast<std::uint8_t>((static_cast<int>(a.b) + b.b) / 2)};
}

// ---------------------------------------------------------------------------
// The layout zones. The SDL layer derives them from the renderer geometry of
// the orientation; the design package mask (docs/backgrounds/index.md) states
// the same rectangles, so the two cannot drift apart unnoticed.
//
//   frame  the opaque board well plus its one-pixel ring: no design shape
//          ever lands inside, not even one the well would later cover.
//   loud   the margins where accent colors may draw. Everything else outside
//          the frame is quiet: base and secondary colors draw there, and no
//          color above the secondary cap ever does.
// ---------------------------------------------------------------------------

struct BgRect {
  int x;
  int y;
  int w;
  int h;
};

inline constexpr int kMaxLoudMargins = 3;

struct Zones {
  int canvasWidth = 0;
  int canvasHeight = 0;
  BgRect frame{0, 0, 0, 0};
  BgRect loud[kMaxLoudMargins] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
  int loudCount = 0;
};

// ---------------------------------------------------------------------------
// The painter sink. The SDL adapter fills renderer rectangles; the headless
// suite records the fills. Painting never allocates and never reads the game.
// ---------------------------------------------------------------------------

class Painter {
 public:
  virtual ~Painter() = default;
  virtual void fill(BgColor color, int x, int y, int w, int h) = 0;
};

// Paints one element rectangle under the layout contract: clipped to the
// canvas, never inside the frame, and - when the color sits above the
// secondary cap in any channel - only inside the loud margins. The clip
// classification uses `color` exactly as passed, so a tinted fade still
// respects the placement of the untinted element.
void paintElement(const Zones& zones, Painter& painter, BgColor color, int x,
                  int y, int w, int h);

// Paints a design's base field: one whole-canvas fill, the ground layer
// every design record opens with. The opaque well and its ring cover their
// rectangle after the wallpaper draws, so the visible base is the canvas
// outside them; the paintElement() exclusion above governs the shapes.
void paintBase(const Zones& zones, Painter& painter, BgColor base);

// ---------------------------------------------------------------------------
// The catalog: the twenty approved designs of the owner-approved set
// (approval 2026-11-05). The stable design ID is the level number, 1 through
// 20; the ID never changes meaning. Each record restates its design record's
// palette roles, motion, level-change transition, and budget class
// (docs/backgrounds/level-XX.md).
//
// The catalog carries the contract data, not the grammars: the chapter tasks
// T079 through T083 land the pattern grammars that draw on this machinery,
// one chapter at a time, from the seeds and formulas their records name.
// The Still chapter (levels 1 through 4) landed with task T079, the Stir
// chapter (levels 5 through 8) with task T080, the Build chapter (levels 9
// through 12) with task T081, the Pressure chapter (levels 13 through 16)
// with task T082, and the Peak chapter (levels 17 through 20) with task
// T083, completing the twenty.
// ---------------------------------------------------------------------------

inline constexpr int kDesignFirstId = 1;
inline constexpr int kDesignCount = 20;
inline constexpr int kDesignLastId = kDesignFirstId + kDesignCount - 1;

// The budget ceiling of each class: at most 300 small elements or at most 40
// band elements per design, the operator brief's rule 14.
inline constexpr int kMaxSmallElements = 300;
inline constexpr int kMaxBandElements = 40;

enum class Chapter : std::uint8_t { Still, Stir, Build, Pressure, Peak };

enum class MotionKind : std::uint8_t {
  None,    // levels 1 through 4: fully static
  Drift,   // elements move and wrap over a span
  Scroll,  // a periodic rule set shifts phase
  Pulse    // one layer swells on a cycle
};

enum class Direction : std::uint8_t {
  None, Down, Up, Left, Right, DownLeft, Out, In
};

struct MotionSpec {
  MotionKind kind = MotionKind::None;
  Direction direction = Direction::None;
  int speedMilliPxPerS = 0;  // at most 10000, that is 10 px/s
  int cycleMs = 0;           // wrap period, or the pulse cycle (at least 4 s)
};

enum class TransitionKind : std::uint8_t { Cut, Fade };

struct TransitionSpec {
  TransitionKind kind = TransitionKind::Cut;
  int durationMs = 0;  // a fade lasts at most 1000 ms
  int steps = 0;       // a fade holds at least 10 visible steps
};

enum class BudgetClass : std::uint8_t { SmallElements, BandElements };

struct Design {
  int level;                    // the stable design ID, 1 through 20
  const char* name;             // the approved name
  Chapter chapter;
  BgColor base;                 // the base field, inside the cap pair
  BgColor secondary;            // large shapes, at or below kSecondaryCap
  BgColor accent;               // small marks, at or below kAccentCap
  MotionSpec motion;
  TransitionSpec transition;
  BudgetClass budget;
  int intensityTenths;          // the record's intensity, times ten
};

// The twenty designs in ID order. The array is a compile-time constant: the
// pointer outlives the call and nothing ever writes it.
const Design* catalog();

// The design of a stable ID, or nullptr outside 1 through 20.
const Design* designOf(int id);

// The design Auto mode selects for a level, clamped to the ID range.
const Design* designForLevel(int level);

// Paints one design's grammar at `timeMs`: its base field plus every element
// its record places, drawn through paintElement() so the layout contract
// holds. The Still chapter of levels 1 through 4 is landed (task T079), the
// Stir chapter of levels 5 through 8 (task T080), the Build chapter of
// levels 9 through 12 (task T081), the Pressure chapter of levels 13
// through 16 (task T082), and the Peak chapter of levels 17 through 20
// (task T083): all twenty designs of the catalog draw their grammars.
// `timeMs` is the presentation clock, never a simulation tick.
void paintGrammar(const Design& design, const Zones& zones, Painter& painter,
                  std::uint32_t timeMs);

// ---------------------------------------------------------------------------
// The cosmetic generator. hash32() is the h32() of tools/backgrounds/
// designs.py: the standard CRC-32 of the seed and the values packed as
// little-endian 32-bit two's-complement integers. It is independent of the
// gameplay randomizer and of every game state.
// ---------------------------------------------------------------------------

std::uint32_t hash32(std::uint32_t seed, const int* values, std::size_t count);

inline std::uint32_t hash32(std::uint32_t seed) { return hash32(seed, nullptr, 0); }

inline std::uint32_t hash32(std::uint32_t seed, int a) {
  const int values[1] = {a};
  return hash32(seed, values, 1);
}

inline std::uint32_t hash32(std::uint32_t seed, int a, int b) {
  const int values[2] = {a, b};
  return hash32(seed, values, 2);
}

inline std::uint32_t hash32(std::uint32_t seed, int a, int b, int c) {
  const int values[3] = {a, b, c};
  return hash32(seed, values, 3);
}

inline std::uint32_t hash32(std::uint32_t seed, int a, int b, int c, int d) {
  const int values[4] = {a, b, c, d};
  return hash32(seed, values, 4);
}

// The whole-pixel displacement after `tMs` at `speedMilliPxPerS`, rounded to
// nearest. Pure integer arithmetic, so a grammar redraws identically on every
// machine.
int driftPixels(int speedMilliPxPerS, std::uint32_t tMs);

// The pulse phase of `tMs` on `cycleMs`, as thousandths of the triangle
// 0..1000..0: a swell that never jumps. `cycleMs` of zero returns 0.
int pulsePhasePerMille(std::uint32_t tMs, int cycleMs);

// ---------------------------------------------------------------------------
// The presentation clock. It advances on rendered frames, never on
// simulation ticks, so the fixed step of the rules never moves. While frozen
// - pause, or a window that lost focus - advance() drops the time instead of
// banking it, so resuming continues without a jump. The counter is a 32-bit
// millisecond value and wraps rather than growing without bound.
// ---------------------------------------------------------------------------

class PresentationClock {
 public:
  // One frame may add at most this much, so a stall or a debugging pause of
  // the process never teleports the wallpaper.
  static constexpr std::uint32_t kMaxAdvanceMs = 250;

  void advance(std::uint32_t deltaMs);
  // Adds exactly frames times 1000/60 ms, the frame period, with the
  // remainder carried: the deterministic harness mode advances the clock by
  // whole frames without drift.
  void advanceFrames(int frames);
  void setFrozen(bool frozen) noexcept { frozen_ = frozen; }
  bool frozen() const noexcept { return frozen_; }
  void reset() noexcept;
  std::uint32_t timeMs() const noexcept { return timeMs_; }

 private:
  std::uint32_t timeMs_ = 0;
  std::uint8_t remainderThirds_ = 0;  // the sub-millisecond carry of frames
  bool frozen_ = false;
};

// ---------------------------------------------------------------------------
// The wallpaper: the play scene's background layer and its presentation
// state. One instance serves the whole session.
//
// The modes of the owner's plan:
//   Off    nothing draws; the caller keeps the legacy flat clear of
//          #10121A and skips all pattern and animation work.
//   Auto   the design of the current level, clamped to 1 through 20. A level
//          change switches with the new design's cut or stepped fade.
//   Fixed  the preferred design at every level. Level changes change nothing;
//          the design, its motion, and its clock continue across rounds.
//
// New rounds reset predictably: in Auto the clock returns to zero and the
// selection cuts in, in Fixed the design keeps running unbroken, and in Off
// there is nothing to reset. Manual mode and preference changes cut.
//
// The retained state is fixed-size: two design IDs, one timestamp, and the
// clock. draw() is a const reader of that state.
// ---------------------------------------------------------------------------

enum class Mode : std::uint8_t { Off, Auto, Fixed };

// The settings-file word of each mode (task T084): auto, off, fixed, the
// order the mode row of the backgrounds page cycles. The words are the
// canonical spellings the settings parser accepts and nothing else.
const char* modeName(Mode mode);

// The mode of one settings word, or false for every other spelling. The one
// place the word-to-mode mapping lives, so the file, the menu, and the
// wallpaper never disagree about a mode's name.
bool modeFromName(const char* name, Mode* mode);

// The uppercase menu spelling of each mode, the word the options row and the
// backgrounds page draw.
const char* modeDisplayName(Mode mode);

class Wallpaper {
 public:
  Wallpaper() = default;

  void setMode(Mode mode) noexcept;
  Mode mode() const noexcept { return mode_; }
  // The preferred design of Fixed mode, clamped into 1 through 20. The
  // preference survives switches to Off and Auto.
  void setPreference(int designId) noexcept;
  int preference() const noexcept { return preference_; }
  // The level Auto mode selects from. Outside Auto it only records the level.
  void setLevel(int level) noexcept;
  int level() const noexcept { return level_; }

  // A new round began: reset per the mode rules above.
  void beginRound();
  // Pause and focus loss freeze motion and fade progress; resuming continues
  // without a time jump.
  void setFrozen(bool frozen) noexcept { clock_.setFrozen(frozen); }
  bool frozen() const noexcept { return clock_.frozen(); }
  void advance(std::uint32_t deltaMs);
  void advanceFrames(int frames);
  std::uint32_t timeMs() const noexcept { return clock_.timeMs(); }

  // Draws the wallpaper layer of the current state. Returns false in Off
  // mode, where nothing draws and the caller keeps its legacy clear.
  bool draw(const Zones& zones, Painter& painter) const;

  // The design the canvas shows now: the target once a transition completes.
  int activeDesign() const noexcept { return active_; }
  // True while a fade is running.
  bool fading() const noexcept { return fading_; }

 private:
  const Design* selection() const noexcept;
  void applySelection(const Design* target);
  void sweepFade();

  Mode mode_ = Mode::Auto;  // the plan's default
  int preference_ = kDesignFirstId;
  int level_ = kDesignFirstId;
  int active_ = kDesignFirstId;
  int outgoing_ = 0;  // the design a fade is leaving, 0 while none is
  std::uint32_t fadeStartMs_ = 0;
  bool fading_ = false;
  PresentationClock clock_;
};

}  // namespace backgrounds
}  // namespace stackfall
