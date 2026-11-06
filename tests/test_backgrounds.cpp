// Headless suite for the procedural level backgrounds (tasks T078, T079,
// T080, T081, T082, and T083, docs/backgrounds and
// docs/BACKGROUND_IMPLEMENTATION.md).
//
// This suite pins the SDL-free half of the background system against the
// approved design package: the catalog of the twenty designs (names,
// palettes, motion, transitions, budgets), the componentwise brightness
// caps, the cosmetic hash against the h32() values of the design tooling,
// the presentation clock (advance, freeze, resume without a jump, reset,
// bounds), the clipping layer (nothing inside the frame, accents only in the
// loud margins), the Still chapter grammars of levels 1 through 4 (task
// T079: the exact plates of the records, the star placement of the approved
// seed, the static stream at arbitrary clocks, the palette and coverage
// caps, and distinctness at the one-eighth review scale), the Stir chapter
// grammars of levels 5 through 8 (task T080: the approved plates and streams
// of both orientations, the speed and continuity of each design's one
// moving layer, its seamless wrap period, the static layers holding still,
// the palette and coverage caps, the chapter activity rising over Still,
// and distinctness at the one-eighth review scale), the Build chapter
// grammars of levels 9 through 12 (task T081: the fracture seams and
// sinking ticks, the circuit paths and breathing node cores, the rising
// ribbons with their corrected seamless wrap, and the diagonal star and
// trail layer - each pinned by digest, literal geometry, motion probes,
// and wrap returns), the Pressure chapter grammars of levels 13 through
// 16 (task T082: the scrolling grid rules over still guides and nodes,
// the ranked 160-flake snow field over its static bands, the seven
// phase-mirrored wave bands sliding left as one layer with their foam
// holding still, and the nine broken contours breathing on the 8 s
// cycle - each pinned by digest, literal geometry, motion probes, and
// wrap returns), the Peak chapter grammars of levels 17 through 20
// (task T083: the ranked 190-ember field rising past its static stepped
// columns, the 200 dashes and four trails falling as one combined
// layer, the thirty broken bands pressing their 54 px triangle pulse
// whose exact-half clocks pin the ported double rounding, and the
// outer-biased 280-fragment debris field falling past its static slabs
// - each pinned by digest, literal geometry, motion probes, and wrap
// returns), the wallpaper state machine (modes, cuts, stepped
// fades, round resets), and the independence of the wallpaper from
// gameplay randomness. The SDL half - the zone derivation from the
// renderer geometry and the exact pixels of the play scene - is pinned by
// the background checks of tests/test_renderer_sdl.cpp.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "stackfall/backgrounds.hpp"
#include "stackfall/game.hpp"

#include "test_util.hpp"

namespace {

using namespace stackfall;
using namespace stackfall::backgrounds;

// ---------------------------------------------------------------------------
// Painters: one that records the fills, one that marks pixel coverage. Test
// code may allocate; the runtime path never does.
// ---------------------------------------------------------------------------

constexpr int kRecordCapacity = 512;

struct RecordedFill {
  BgColor color;
  int x;
  int y;
  int w;
  int h;
};

class RecordingPainter final : public Painter {
 public:
  void fill(BgColor color, int x, int y, int w, int h) override {
    if (count < kRecordCapacity) {
      fills[count] = RecordedFill{color, x, y, w, h};
    }
    ++count;
  }

  void reset() {
    count = 0;
  }

  bool overflow() const { return count > kRecordCapacity; }

  RecordedFill fills[kRecordCapacity];
  int count = 0;
};

class CoveragePainter final : public Painter {
 public:
  CoveragePainter(int width, int height)
      : width(width), height(height), marks(new int[width * height]()) {}
  ~CoveragePainter() { delete[] marks; }

  void fill(BgColor color, int x, int y, int w, int h) override {
    (void)color;  // coverage counting ignores the shade
    for (int row = y; row < y + h; ++row) {
      for (int column = x; column < x + w; ++column) {
        if (row >= 0 && column >= 0 && row < height && column < width) {
          ++marks[row * width + column];
        }
      }
    }
  }

  int at(int x, int y) const { return marks[y * width + x]; }

  const int width;
  const int height;

 private:
  int* marks;
};

// One plate canvas: the pixels a grammar paints, for the palette, coverage,
// and review-scale checks of the chapter suites.
class PlatePainter final : public Painter {
 public:
  PlatePainter(int width, int height)
      : width(width), height(height), pixels(new BgColor[width * height]) {}
  ~PlatePainter() { delete[] pixels; }

  void fill(BgColor color, int x, int y, int w, int h) override {
    for (int row = y; row < y + h; ++row) {
      for (int column = x; column < x + w; ++column) {
        if (row >= 0 && column >= 0 && row < height && column < width) {
          pixels[row * width + column] = color;
        }
      }
    }
  }

  BgColor at(int x, int y) const { return pixels[y * width + x]; }

  const int width;
  const int height;

 private:
  BgColor* pixels;
};

// The gray of a color, the (r + g + b) / 3 the review scale samples.
int grayOf(const BgColor& color) {
  return (static_cast<int>(color.r) + color.g + color.b) / 3;
}

// ---------------------------------------------------------------------------
// The layout mask of docs/backgrounds/index.md, restated here independently
// so the clipping layer is checked against the design contract, not against
// itself. The SDL suite checks that backgroundZones() of the renderer returns
// these same rectangles.
// ---------------------------------------------------------------------------

Zones maskLandscapeZones() {
  Zones zones;
  zones.canvasWidth = 960;
  zones.canvasHeight = 544;
  zones.frame = BgRect{344, 1, 272, 542};
  zones.loud[0] = BgRect{0, 0, 192, 544};
  zones.loud[1] = BgRect{768, 0, 192, 544};
  zones.loudCount = 2;
  return zones;
}

Zones maskPortraitZones() {
  Zones zones;
  zones.canvasWidth = 544;
  zones.canvasHeight = 960;
  zones.frame = BgRect{71, 119, 402, 802};
  zones.loud[0] = BgRect{0, 119, 59, 802};
  zones.loud[1] = BgRect{485, 119, 59, 802};
  zones.loud[2] = BgRect{0, 921, 544, 39};
  zones.loudCount = 3;
  return zones;
}

bool sameColor(const BgColor& a, const BgColor& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

bool sameFill(const RecordedFill& fill, BgColor color, int x, int y, int w,
              int h) {
  return fill.color.r == color.r && fill.color.g == color.g &&
         fill.color.b == color.b && fill.x == x && fill.y == y &&
         fill.w == w && fill.h == h;
}

// Advances a wallpaper by whole milliseconds in clock-sized chunks, so a test
// never fights the per-advance clamp.
void advanceBy(Wallpaper& wallpaper, std::uint32_t ms) {
  while (ms > 0) {
    const std::uint32_t chunk = ms > PresentationClock::kMaxAdvanceMs
                                    ? PresentationClock::kMaxAdvanceMs
                                    : ms;
    wallpaper.advance(chunk);
    ms -= chunk;
  }
}

// The first fill color a base-only wallpaper draw emits, the frame's canvas
// base.
BgColor firstFillColor(RecordingPainter& painter) {
  return painter.count > 0 ? painter.fills[0].color : BgColor{0, 0, 0};
}

// ---------------------------------------------------------------------------
// The catalog.
// ---------------------------------------------------------------------------

void run_backgrounds_catalog_checks() {
  std::printf("test_backgrounds: the approved catalog of twenty designs\n");

  const Design* catalog = backgrounds::catalog();
  SC_CHECK(catalog != nullptr);

  // One record per level, in stable ID order, with the approved names.
  const struct {
    int level;
    const char* name;
    std::uint32_t base;
    std::uint32_t secondary;
    std::uint32_t accent;
    MotionKind motion;
    Direction direction;
    int speedMilli;
    int cycleMs;
    TransitionKind transition;
    int durationMs;
    int steps;
    BudgetClass budget;
    int intensityTenths;
  } approved[kDesignCount] = {
      {1, "Still ground", 0x10121A, 0x0C0E16, 0x283044, MotionKind::None,
       Direction::None, 0, 0, TransitionKind::Cut, 0, 0,
       BudgetClass::BandElements, 10},
      {2, "First light", 0x10131C, 0x1C2433, 0x2A3146, MotionKind::None,
       Direction::None, 0, 0, TransitionKind::Fade, 400, 12,
       BudgetClass::SmallElements, 14},
      {3, "Distant stars", 0x0E121C, 0x1B2230, 0x64718C, MotionKind::None,
       Direction::None, 0, 0, TransitionKind::Fade, 500, 15,
       BudgetClass::SmallElements, 18},
      {4, "Quiet strata", 0x11141D, 0x222A39, 0x2A3146, MotionKind::None,
       Direction::None, 0, 0, TransitionKind::Fade, 500, 15,
       BudgetClass::BandElements, 22},
      {5, "Slow drift", 0x0F1420, 0x20283A, 0x687892, MotionKind::Drift,
       Direction::Down, 2000, 272000, TransitionKind::Cut, 0, 0,
       BudgetClass::SmallElements, 26},
      {6, "Dither dusk", 0x121620, 0x242B3A, 0x38445B, MotionKind::Drift,
       Direction::Up, 2000, 278000, TransitionKind::Fade, 500, 15,
       BudgetClass::SmallElements, 30},
      {7, "Sleeping skyline", 0x11151F, 0x252D3D, 0x64728A, MotionKind::Pulse,
       Direction::None, 250, 8000, TransitionKind::Fade, 500, 15,
       BudgetClass::SmallElements, 34},
      {8, "Low signal", 0x11151F, 0x242C3B, 0x54617A, MotionKind::Scroll,
       Direction::Down, 3000, 3300, TransitionKind::Cut, 0, 0,
       BudgetClass::SmallElements, 38},
      {9, "Fracture", 0x10151F, 0x283044, 0x707D96, MotionKind::Drift,
       Direction::Down, 3000, 181300, TransitionKind::Fade, 500, 15,
       BudgetClass::SmallElements, 42},
      {10, "Circuit hush", 0x111721, 0x283144, 0x6B8498, MotionKind::Pulse,
       Direction::None, 290, 7000, TransitionKind::Fade, 500, 15,
       BudgetClass::SmallElements, 46},
      {11, "Band aurora", 0x131725, 0x283044, 0x67758D, MotionKind::Drift,
       Direction::Up, 4000, 161250, TransitionKind::Fade, 600, 18,
       BudgetClass::BandElements, 50},
      {12, "Long trails", 0x111620, 0x293144, 0x76849B, MotionKind::Drift,
       Direction::DownLeft, 4000, 90700, TransitionKind::Cut, 0, 0,
       BudgetClass::SmallElements, 54},
      {13, "Deep grid", 0x121722, 0x293145, 0x79869B, MotionKind::Scroll,
       Direction::Down, 5000, 108800, TransitionKind::Fade, 600, 18,
       BudgetClass::SmallElements, 58},
      {14, "Fine snow", 0x131923, 0x293145, 0x7A879E, MotionKind::Drift,
       Direction::Down, 6000, 90700, TransitionKind::Cut, 0, 0,
       BudgetClass::SmallElements, 62},
      {15, "Undertow", 0x131923, 0x2A3146, 0x74839B, MotionKind::Scroll,
       Direction::Left, 6000, 13300, TransitionKind::Fade, 600, 18,
       BudgetClass::BandElements, 66},
      {16, "Slow pulse", 0x141A26, 0x2A3146, 0x7C88A0, MotionKind::Pulse,
       Direction::Out, 6000, 8000, TransitionKind::Fade, 600, 18,
       BudgetClass::SmallElements, 70},
      {17, "Rising ember", 0x17191F, 0x2A3040, 0x897A72, MotionKind::Drift,
       Direction::Up, 7000, 77700, TransitionKind::Cut, 0, 0,
       BudgetClass::SmallElements, 77},
      {18, "Storm lines", 0x131925, 0x2A3146, 0x8290A7, MotionKind::Drift,
       Direction::Down, 8000, 68000, TransitionKind::Cut, 0, 0,
       BudgetClass::SmallElements, 84},
      {19, "Compression", 0x171C28, 0x2A3146, 0x8591A8, MotionKind::Pulse,
       Direction::In, 9000, 12000, TransitionKind::Fade, 500, 15,
       BudgetClass::SmallElements, 92},
      {20, "Collapse", 0x191E2B, 0x2A3146, 0x8A94AC, MotionKind::Drift,
       Direction::Down, 10000, 80200, TransitionKind::Fade, 600, 18,
       BudgetClass::SmallElements, 100},
  };

  int previousIntensity = 0;
  for (int index = 0; index < kDesignCount; ++index) {
    const Design& design = catalog[index];
    const auto& record = approved[index];
    SC_CHECK(design.level == record.level);
    SC_CHECK(std::strcmp(design.name, record.name) == 0);
    SC_CHECK(design.base.r == (record.base >> 16) &&
             design.base.g == ((record.base >> 8) & 0xFF) &&
             design.base.b == (record.base & 0xFF));
    SC_CHECK(design.secondary.r == (record.secondary >> 16) &&
             design.secondary.g == ((record.secondary >> 8) & 0xFF) &&
             design.secondary.b == (record.secondary & 0xFF));
    SC_CHECK(design.accent.r == (record.accent >> 16) &&
             design.accent.g == ((record.accent >> 8) & 0xFF) &&
             design.accent.b == (record.accent & 0xFF));
    SC_CHECK(design.motion.kind == record.motion);
    SC_CHECK(design.motion.direction == record.direction);
    SC_CHECK(design.motion.speedMilliPxPerS == record.speedMilli);
    SC_CHECK(design.motion.cycleMs == record.cycleMs);
    SC_CHECK(design.transition.kind == record.transition);
    SC_CHECK(design.transition.durationMs == record.durationMs);
    SC_CHECK(design.transition.steps == record.steps);
    SC_CHECK(design.budget == record.budget);
    SC_CHECK(design.intensityTenths == record.intensityTenths);
    // The arc rises level over level.
    SC_CHECK(design.intensityTenths > previousIntensity);
    previousIntensity = design.intensityTenths;

    // The contract rules of the package, per design.
    SC_CHECK(baseInRange(design.base));
    SC_CHECK(withinCap(design.secondary, kSecondaryCap));
    SC_CHECK(withinCap(design.accent, kAccentCap));
    SC_CHECK(design.transition.kind == TransitionKind::Cut ||
             (design.transition.durationMs <= 1000 &&
              design.transition.steps >= 10));
    SC_CHECK(design.motion.speedMilliPxPerS <= 10000);
    if (design.motion.kind == MotionKind::Pulse) {
      SC_CHECK(design.motion.cycleMs >= 4000);
    }
    // The chapters of the twenty-slot arc.
    const Chapter chapter[5] = {Chapter::Still, Chapter::Stir, Chapter::Build,
                                Chapter::Pressure, Chapter::Peak};
    SC_CHECK(design.chapter == chapter[(design.level - 1) / 4]);
    // Levels 1 through 4 are fully static.
    if (design.level <= 4) {
      SC_CHECK(design.motion.kind == MotionKind::None);
    } else {
      SC_CHECK(design.motion.kind != MotionKind::None);
    }
  }

  // The stable ID accessors: the range, and the Auto clamp of a level.
  SC_CHECK(designOf(0) == nullptr);
  SC_CHECK(designOf(21) == nullptr);
  SC_CHECK(designOf(-3) == nullptr);
  SC_CHECK(designOf(1) == &catalog[0]);
  SC_CHECK(designOf(20) == &catalog[kDesignCount - 1]);
  SC_CHECK(designForLevel(0) == &catalog[0]);
  SC_CHECK(designForLevel(1) == &catalog[0]);
  SC_CHECK(designForLevel(7) == &catalog[6]);
  SC_CHECK(designForLevel(20) == &catalog[kDesignCount - 1]);
  SC_CHECK(designForLevel(21) == &catalog[kDesignCount - 1]);
  SC_CHECK(designForLevel(999) == &catalog[kDesignCount - 1]);

  // The budget ceiling constants of the brief's rule 14.
  SC_CHECK(kMaxSmallElements == 300);
  SC_CHECK(kMaxBandElements == 40);

  // The mode words of the saved options (task T084): the settings spellings,
  // the menu spellings, and the word-to-mode mapping that rejects every
  // other spelling the file never wrote.
  SC_CHECK(std::strcmp(modeName(Mode::Auto), "auto") == 0);
  SC_CHECK(std::strcmp(modeName(Mode::Off), "off") == 0);
  SC_CHECK(std::strcmp(modeName(Mode::Fixed), "fixed") == 0);
  SC_CHECK(std::strcmp(modeDisplayName(Mode::Auto), "AUTO") == 0);
  SC_CHECK(std::strcmp(modeDisplayName(Mode::Off), "OFF") == 0);
  SC_CHECK(std::strcmp(modeDisplayName(Mode::Fixed), "FIXED") == 0);
  for (const Mode mode : {Mode::Auto, Mode::Off, Mode::Fixed}) {
    Mode read = Mode::Fixed;
    SC_CHECK(modeFromName(modeName(mode), &read));
    SC_CHECK(read == mode);
  }
  const char* notModes[] = {"",  "AUTO",  "Off",    "fixed ", " fixed",
                            "on", "true",  "level", "auto\n", "maybe"};
  for (const char* word : notModes) {
    Mode read = Mode::Auto;
    SC_CHECK(!modeFromName(word, &read));
    SC_CHECK(!modeFromName(word, nullptr));
  }
  Mode read = Mode::Off;
  SC_CHECK(!modeFromName(nullptr, &read));
  SC_CHECK(read == Mode::Off);
  SC_CHECK(!modeFromName("auto", nullptr));
}

// ---------------------------------------------------------------------------
// The caps and the integer blend.
// ---------------------------------------------------------------------------

void run_backgrounds_caps_checks() {
  std::printf("test_backgrounds: componentwise caps and integer blending\n");

  // The cap constants of the operator brief.
  SC_CHECK(kBaseFloor.r == 0x0A && kBaseFloor.g == 0x0C && kBaseFloor.b == 0x13);
  SC_CHECK(kBaseCeiling.r == 0x1C && kBaseCeiling.g == 0x21 &&
           kBaseCeiling.b == 0x30);
  SC_CHECK(kSecondaryCap.r == 0x2A && kSecondaryCap.g == 0x31 &&
           kSecondaryCap.b == 0x46);
  SC_CHECK(kAccentCap.r == 0x8A && kAccentCap.g == 0x94 &&
           kAccentCap.b == 0xAC);
  SC_CHECK(kLegacyFlat.r == 0x10 && kLegacyFlat.g == 0x12 &&
           kLegacyFlat.b == 0x1A);

  // A color is within a cap when every channel is; one channel above the cap
  // puts the color outside, even when the others sit far below.
  SC_CHECK(withinCap(kSecondaryCap, kSecondaryCap));
  SC_CHECK(withinCap(colorOf(0x0C0E16), kSecondaryCap));
  SC_CHECK(!withinCap(colorOf(0x2B0000), kSecondaryCap));
  SC_CHECK(!withinCap(colorOf(0x003200), kSecondaryCap));
  SC_CHECK(!withinCap(colorOf(0x000047), kSecondaryCap));
  SC_CHECK(withinCap(kAccentCap, kAccentCap));
  SC_CHECK(!withinCap(colorOf(0x8B94AC), kAccentCap));

  // Base fields stay inside the pair, in and out.
  SC_CHECK(baseInRange(kBaseFloor));
  SC_CHECK(baseInRange(kBaseCeiling));
  SC_CHECK(baseInRange(kLegacyFlat));
  SC_CHECK(!baseInRange(colorOf(0x090C13)));  // below the floor
  SC_CHECK(!baseInRange(colorOf(0x0A0C31)));  // blue above the ceiling
  SC_CHECK(!baseInRange(colorOf(0x0A2200)));  // green above the ceiling
  SC_CHECK(!baseInRange(colorOf(0x1D2130)));  // red above the ceiling

  // The integer blend: the exact channel arithmetic of the fade steps, with
  // the independent formula restated per channel.
  const BgColor mixed = mixToward(kLegacyFlat, colorOf(0x191E2B), 9, 18);
  SC_CHECK(mixed.r == 16 + (25 - 16) * 9 / 18);
  SC_CHECK(mixed.g == 18 + (30 - 18) * 9 / 18);
  SC_CHECK(mixed.b == 26 + (43 - 26) * 9 / 18);
  SC_CHECK(mixed.r == 20 && mixed.g == 24 && mixed.b == 34);
  // Parts of zero reproduce the source exactly; a full whole reaches the
  // target exactly.
  const BgColor zero = mixToward(kLegacyFlat, colorOf(0x191E2B), 0, 18);
  SC_CHECK(zero.r == 16 && zero.g == 18 && zero.b == 26);
  const BgColor full = mixToward(kLegacyFlat, colorOf(0x191E2B), 18, 18);
  SC_CHECK(full.r == 25 && full.g == 30 && full.b == 43);
  // A blend of two colors under a cap stays under the cap componentwise.
  const BgColor accentMix =
      mixToward(colorOf(0x8A94AC), colorOf(0x1C2130), 7, 9);
  SC_CHECK(withinCap(accentMix, kAccentCap));
  SC_CHECK(baseInRange(midColor(colorOf(0x0A0C13), colorOf(0x1C2130))));
  SC_CHECK(baseInRange(midColor(colorOf(0x10121A), colorOf(0x191E2B))));
}

// ---------------------------------------------------------------------------
// The cosmetic hash and the motion helpers.
// ---------------------------------------------------------------------------

void run_backgrounds_hash_checks() {
  std::printf("test_backgrounds: the cosmetic hash of the design package\n");

  // The pinned values of tools/backgrounds/designs.py h32(): the runtime
  // grammars of T079 through T083 reproduce the Python placements from this
  // same function, so the bridge is pinned on both sides.
  SC_CHECK(hash32(0x5355) == 0xB36DB065u);
  SC_CHECK(hash32(0x5355, 0, 0, 0) == 0xC15FD06Cu);
  SC_CHECK(hash32(0x5355, 192, 0, 0) == 0x91FB087Cu);
  SC_CHECK(hash32(0x5355, 768, 9, 13) == 0xB598759Bu);
  SC_CHECK(hash32(0x5356, 0, 5, 27) == 0x276FBA64u);
  SC_CHECK(hash32(0x5356, 768, 3, 3) == 0xED4FAA92u);
  SC_CHECK(hash32(0x5357, 485, 1, 2) == 0x31E49622u);
  SC_CHECK(hash32(0x6D, 0, 2, 1) == 0x2629E493u);
  SC_CHECK(hash32(0x6E, 485, 0, 1, 99) == 0x7A0E1E09u);
  SC_CHECK(hash32(0x1, -5, 7, -100000) == 0x08015650u);

  // Stable across calls, and a different seed or value stream differs.
  SC_CHECK(hash32(0x5356, 0, 5, 27) == hash32(0x5356, 0, 5, 27));
  SC_CHECK(hash32(0x5355, 0, 0, 0) != hash32(0x5356, 0, 0, 0));
  SC_CHECK(hash32(0x5355, 0, 0, 1) != hash32(0x5355, 0, 0, 0));

  // The whole-pixel displacement of one drift: 2 px/s covers a pixel every
  // half second; 10 px/s every tenth; rounding to the nearest pixel.
  SC_CHECK(driftPixels(2000, 0) == 0);
  SC_CHECK(driftPixels(2000, 250) == 1);
  SC_CHECK(driftPixels(2000, 249) == 0);
  SC_CHECK(driftPixels(2000, 500) == 1);
  SC_CHECK(driftPixels(2000, 1000) == 2);
  SC_CHECK(driftPixels(10000, 100) == 1);
  SC_CHECK(driftPixels(10000, 0) == 0);

  // The pulse triangle in thousandths: 0 at rest, 1000 at the half cycle,
  // symmetric on both sides, and never a jump larger than the step.
  SC_CHECK(pulsePhasePerMille(0, 8000) == 0);
  SC_CHECK(pulsePhasePerMille(2000, 8000) == 500);
  SC_CHECK(pulsePhasePerMille(4000, 8000) == 1000);
  SC_CHECK(pulsePhasePerMille(6000, 8000) == 500);
  SC_CHECK(pulsePhasePerMille(7999, 8000) == 2);
  SC_CHECK(pulsePhasePerMille(8000, 8000) == 0);  // the cycle wraps
  SC_CHECK(pulsePhasePerMille(12345, 0) == 0);    // a null cycle holds still
  int previous = 0;
  for (std::uint32_t t = 0; t <= 4000; t += 100) {
    const int phase = pulsePhasePerMille(t, 8000);
    const int delta = phase - previous;
    SC_CHECK(delta >= -30 && delta <= 30);
    previous = phase;
  }
}

// ---------------------------------------------------------------------------
// The presentation clock.
// ---------------------------------------------------------------------------

void run_backgrounds_clock_checks() {
  std::printf("test_backgrounds: the bounded presentation clock\n");

  PresentationClock clock;
  SC_CHECK(clock.timeMs() == 0);
  SC_CHECK(!clock.frozen());

  clock.advance(100);
  SC_CHECK(clock.timeMs() == 100);
  clock.advance(50);
  SC_CHECK(clock.timeMs() == 150);

  // One frame may add at most the clamp, whatever the stall measured.
  clock.advance(1000000000u);
  SC_CHECK(clock.timeMs() == 150 + PresentationClock::kMaxAdvanceMs);

  // Frozen time is dropped, not banked: resume continues without a jump.
  clock.setFrozen(true);
  SC_CHECK(clock.frozen());
  const std::uint32_t beforeFreeze = clock.timeMs();
  clock.advance(5000);
  clock.advanceFrames(300);
  SC_CHECK(clock.timeMs() == beforeFreeze);
  clock.setFrozen(false);
  SC_CHECK(!clock.frozen());
  clock.advance(100);
  SC_CHECK(clock.timeMs() == beforeFreeze + 100);

  // A reset returns to zero and keeps the freeze state it was given; the
  // freeze is a session condition, not clock data.
  clock.reset();
  SC_CHECK(clock.timeMs() == 0);
  clock.setFrozen(true);
  clock.reset();
  SC_CHECK(clock.timeMs() == 0);
  SC_CHECK(clock.frozen());
  clock.setFrozen(false);

  // Whole frames advance exactly 1000/60 ms each, thirds carried, with no
  // drift over long runs.
  PresentationClock frameClock;
  frameClock.advanceFrames(3);
  SC_CHECK(frameClock.timeMs() == 50);
  frameClock.advanceFrames(1);
  SC_CHECK(frameClock.timeMs() == 66);  // 50 + 16.67 carried to 66
  frameClock.advanceFrames(2);
  SC_CHECK(frameClock.timeMs() == 100);
  for (int frame = 0; frame < 60000; ++frame) {
    frameClock.advanceFrames(1);
  }
  // 60000 frames after 100 ms: 1000 seconds plus 100 ms, exactly.
  SC_CHECK(frameClock.timeMs() == 100 + 1000000);

  // The counter is bounded: it wraps as a 32-bit millisecond value rather
  // than growing without end, and the wrap itself changes nothing observable
  // about the call.
  PresentationClock wrapClock;
  for (long iteration = 0; iteration < 17179870; ++iteration) {
    wrapClock.advance(250);
  }
  // 250 ms times 17179870 is 2^32 + 204.
  SC_CHECK(wrapClock.timeMs() == 204);
}

// ---------------------------------------------------------------------------
// The clipping layer.
// ---------------------------------------------------------------------------

void run_backgrounds_clip_checks() {
  std::printf("test_backgrounds: clipping, the well exclusion, and caps\n");

  const Zones landscape = maskLandscapeZones();
  const Zones portrait = maskPortraitZones();
  const BgColor secondary = colorOf(0x222A39);
  const BgColor accent = colorOf(0x64718C);

  // The base field is one whole-canvas fill, the ground layer of every
  // record. It paints under the well and ring too; the renderer's opaque
  // well covers its rectangle after the wallpaper, and the element checks
  // below hold the shapes themselves out of the frame.
  {
    CoveragePainter coverage(landscape.canvasWidth, landscape.canvasHeight);
    paintBase(landscape, coverage, kLegacyFlat);
    for (int y = 0; y < landscape.canvasHeight; ++y) {
      for (int x = 0; x < landscape.canvasWidth; ++x) {
        SC_CHECK(coverage.at(x, y) == 1);
      }
    }
  }
  {
    CoveragePainter coverage(portrait.canvasWidth, portrait.canvasHeight);
    paintBase(portrait, coverage, colorOf(0x191E2B));
    for (int y = 0; y < portrait.canvasHeight; ++y) {
      for (int x = 0; x < portrait.canvasWidth; ++x) {
        SC_CHECK(coverage.at(x, y) == 1);
      }
    }
  }

  // The base fill is exactly one fill of the whole canvas.
  RecordingPainter painter;
  paintBase(landscape, painter, kLegacyFlat);
  SC_CHECK(painter.count == 1);
  SC_CHECK(sameFill(painter.fills[0], kLegacyFlat, 0, 0, 960, 544));
  painter.reset();
  paintBase(portrait, painter, kLegacyFlat);
  SC_CHECK(painter.count == 1);
  SC_CHECK(sameFill(painter.fills[0], kLegacyFlat, 0, 0, 544, 960));

  // A secondary rectangle draws anywhere outside the frame, cut around it.
  painter.reset();
  paintElement(landscape, painter, secondary, 300, 2, 400, 540);
  // The requested span x = 300..699 crosses the frame's x = 344..615 inside
  // the frame's own rows, so the frame cut leaves a 44 px and an 84 px side
  // piece and nothing above or below.
  SC_CHECK(painter.count == 2);
  SC_CHECK(sameFill(painter.fills[0], secondary, 300, 2, 44, 540));
  SC_CHECK(sameFill(painter.fills[1], secondary, 616, 2, 84, 540));

  // An accent draws only inside the loud margins: a full-canvas accent
  // request lands in the two margins alone, and the quiet span between them
  // - the HUD columns and the guard gaps - holds none of it.
  painter.reset();
  paintElement(landscape, painter, accent, 0, 0, 960, 544);
  SC_CHECK(painter.count == 2);
  SC_CHECK(sameFill(painter.fills[0], accent, 0, 0, 192, 544));
  SC_CHECK(sameFill(painter.fills[1], accent, 768, 0, 192, 544));
  {
    CoveragePainter coverage(landscape.canvasWidth, landscape.canvasHeight);
    paintElement(landscape, coverage, accent, 0, 0, 960, 544);
    for (int y = 0; y < landscape.canvasHeight; ++y) {
      for (int x = 192; x < 768; ++x) {
        SC_CHECK(coverage.at(x, y) == 0);
      }
      for (int x = 0; x < 192; ++x) {
        SC_CHECK(coverage.at(x, y) == 1);
      }
    }
  }

  // An accent request entirely inside a quiet zone draws nothing at all.
  painter.reset();
  paintElement(landscape, painter, accent, 200, 10, 100, 30);
  SC_CHECK(painter.count == 0);
  // Even over the board itself: the well stays pure for accents too.
  painter.reset();
  paintElement(landscape, painter, accent, 400, 100, 50, 50);
  SC_CHECK(painter.count == 0);
  // The secondary counterpart of the same quiet-zone request draws.
  painter.reset();
  paintElement(landscape, painter, secondary, 200, 10, 100, 30);
  SC_CHECK(painter.count == 1);
  SC_CHECK(sameFill(painter.fills[0], secondary, 200, 10, 100, 30));

  // The exact secondary cap is not an accent; one channel above it is.
  painter.reset();
  paintElement(landscape, painter, kSecondaryCap, 200, 10, 100, 30);
  SC_CHECK(painter.count == 1);
  painter.reset();
  paintElement(landscape, painter, colorOf(0x2A3147), 200, 10, 100, 30);
  SC_CHECK(painter.count == 0);

  // Portrait: an accent spanning the full canvas reaches the two side
  // margins and the bottom strip, never the HUD strip or the guards.
  painter.reset();
  paintElement(portrait, painter, accent, 0, 0, 544, 960);
  SC_CHECK(painter.count == 3);
  SC_CHECK(sameFill(painter.fills[0], accent, 0, 119, 59, 802));
  SC_CHECK(sameFill(painter.fills[1], accent, 485, 119, 59, 802));
  SC_CHECK(sameFill(painter.fills[2], accent, 0, 921, 544, 39));
  {
    CoveragePainter coverage(portrait.canvasWidth, portrait.canvasHeight);
    paintElement(portrait, coverage, accent, 0, 0, 544, 960);
    for (int x = 0; x < 544; ++x) {
      for (int y = 0; y < 119; ++y) {
        SC_CHECK(coverage.at(x, y) == 0);  // the HUD strip stays quiet
      }
    }
    for (int y = 119; y < 921; ++y) {
      for (int x = 59; x < 485; ++x) {
        SC_CHECK(coverage.at(x, y) == 0);  // the guards and the frame
      }
    }
  }

  // A request partly off the canvas is clamped to the canvas, not wrapped.
  painter.reset();
  paintElement(landscape, painter, secondary, -50, -10, 100, 60);
  SC_CHECK(painter.count == 1);
  SC_CHECK(sameFill(painter.fills[0], secondary, 0, 0, 50, 50));
  painter.reset();
  paintElement(landscape, painter, secondary, 950, 500, 100, 100);
  SC_CHECK(painter.count == 1);
  SC_CHECK(sameFill(painter.fills[0], secondary, 950, 500, 10, 44));

  // Degenerate rectangles draw nothing.
  painter.reset();
  paintElement(landscape, painter, secondary, 10, 10, 0, 30);
  paintElement(landscape, painter, secondary, 10, 10, 30, -1);
  paintElement(landscape, painter, accent, 10, 10, 0, 0);
  SC_CHECK(painter.count == 0);
}

// ---------------------------------------------------------------------------
// The grammars as they stand: the base field of every design, and the
// elements of the chapters that have landed.
// ---------------------------------------------------------------------------

void run_backgrounds_grammar_checks() {
  std::printf("test_backgrounds: grammar emission bounds\n");

  const Zones orientations[2] = {maskLandscapeZones(), maskPortraitZones()};
  for (int index = 0; index < kDesignCount; ++index) {
    const Design& design = backgrounds::catalog()[index];
    for (const Zones& zones : orientations) {
      RecordingPainter painter;
      paintGrammar(design, zones, painter, 0);
      // The base field of every design: one whole-canvas fill in the
      // design's approved base color, always the first fill.
      SC_CHECK(painter.count >= 1);
      SC_CHECK(sameFill(painter.fills[0], design.base, 0, 0,
                        zones.canvasWidth, zones.canvasHeight));
      // A landed chapter draws its record's elements on top of the base;
      // with the Peak chapter of task T083 all twenty designs have landed.
      const bool landed = design.level <= 20;  // Still T079, Stir T080,
      // Build T081, Pressure T082, Peak T083
      if (!landed) {
        SC_CHECK(painter.count == 1);
      } else {
        SC_CHECK(painter.count > 1);
      }
      for (int fill = 0; fill < painter.count; ++fill) {
        SC_CHECK(sameFill(painter.fills[fill], painter.fills[fill].color,
                          painter.fills[fill].x, painter.fills[fill].y,
                          painter.fills[fill].w, painter.fills[fill].h));
      }
      // A static design holds its stream at every time.
      if (design.motion.kind == MotionKind::None) {
        RecordingPainter later;
        paintGrammar(design, zones, later, 987654);
        SC_CHECK(later.count == painter.count);
        for (int fill = 0; fill < painter.count; ++fill) {
          SC_CHECK(sameFill(later.fills[fill], painter.fills[fill].color,
                            painter.fills[fill].x, painter.fills[fill].y,
                            painter.fills[fill].w, painter.fills[fill].h));
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// The Still chapter of task T079: levels 1 through 4, fully static. Every
// check runs on both orientations against the retained records of
// docs/backgrounds/level-01.md through level-04.md.
// ---------------------------------------------------------------------------

// The one-eighth review samples of a canvas: 8160 in both frames.
constexpr int kEighthSamples = 960 * 544 / 64;

// One expected element of a grammar, restated from its design record.
struct ExpectedRect {
  BgColor color;
  int x;
  int y;
  int w;
  int h;
};

// Paints `design` and compares every pixel outside the frame against the
// base field plus `rects`: the plate the record states and nothing else.
// The frame's own rectangle is skipped because the renderer's opaque well
// and ring cover it after the wallpaper.
bool plateMatches(const Zones& zones, const Design& design,
                  const ExpectedRect* rects, int rectCount) {
  PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
  paintGrammar(design, zones, plate, 0);
  PlatePainter expected(zones.canvasWidth, zones.canvasHeight);
  expected.fill(design.base, 0, 0, zones.canvasWidth, zones.canvasHeight);
  for (int index = 0; index < rectCount; ++index) {
    expected.fill(rects[index].color, rects[index].x, rects[index].y,
                  rects[index].w, rects[index].h);
  }
  for (int y = 0; y < zones.canvasHeight; ++y) {
    for (int x = 0; x < zones.canvasWidth; ++x) {
      const bool inFrame = x >= zones.frame.x &&
                           x < zones.frame.x + zones.frame.w &&
                           y >= zones.frame.y &&
                           y < zones.frame.y + zones.frame.h;
      if (inFrame) {
        continue;
      }
      if (!sameColor(plate.at(x, y), expected.at(x, y))) {
        return false;
      }
    }
  }
  return true;
}

// The one-eighth review scale of the design package: every sample averages
// the gray of four pixels of its 8 x 8 block, at (0,0), (0,4), (4,0), and
// (4,4). Writes `w` by `h` samples into `out`.
void eighthScale(const PlatePainter& plate, int* out, int* w, int* h) {
  *w = plate.width / 8;
  *h = plate.height / 8;
  for (int y = 0; y < *h; ++y) {
    for (int x = 0; x < *w; ++x) {
      const int baseX = x * 8;
      const int baseY = y * 8;
      int total = 0;
      const int offsets[4][2] = {{0, 0}, {0, 4}, {4, 0}, {4, 4}};
      for (int sample = 0; sample < 4; ++sample) {
        total += grayOf(plate.at(baseX + offsets[sample][0],
                                 baseY + offsets[sample][1]));
      }
      out[y * *w + x] = total / 4;
    }
  }
}

void run_backgrounds_still_checks() {
  std::printf("test_backgrounds: the Still chapter of levels 1 through 4\n");

  const Zones landscape = maskLandscapeZones();
  const Zones portrait = maskPortraitZones();
  const Zones orientations[2] = {landscape, portrait};
  const char* orientationNames[2] = {"landscape", "portrait"};

  // ----- Level 1, Still ground: four corners, two nested steps each, of the
  // secondary ink #0C0E16 on the base #10121A, and nothing else. The accent
  // shade of the record stays reserved and unused.
  {
    const BgColor ink = colorOf(0x0C0E16);
    const ExpectedRect landscapeRects[8] = {
        {ink, 0, 0, 160, 121},    {ink, 0, 0, 99, 75},
        {ink, 800, 0, 160, 121},  {ink, 861, 0, 99, 75},
        {ink, 0, 423, 160, 121},  {ink, 0, 469, 99, 75},
        {ink, 800, 423, 160, 121}, {ink, 861, 469, 99, 75}};
    SC_CHECK(plateMatches(landscape, *designOf(1), landscapeRects, 8));
    const ExpectedRect portraitRects[8] = {
        {ink, 0, 0, 91, 213},    {ink, 0, 0, 56, 132},
        {ink, 453, 0, 91, 213},  {ink, 488, 0, 56, 132},
        {ink, 0, 747, 91, 213},  {ink, 0, 828, 56, 132},
        {ink, 453, 747, 91, 213}, {ink, 488, 828, 56, 132}};
    SC_CHECK(plateMatches(portrait, *designOf(1), portraitRects, 8));

    // The fills themselves: the base, then eight elements. In portrait each
    // outer corner reaches the frame ring and splits around it, so the
    // element count grows while the painted plate stays the one above.
    for (int orientation = 0; orientation < 2; ++orientation) {
      RecordingPainter painter;
      paintGrammar(*designOf(1), orientations[orientation], painter, 0);
      SC_CHECK(painter.count == (orientation == 0 ? 9 : 13));
      SC_CHECK(sameFill(painter.fills[0], designOf(1)->base, 0, 0,
                        orientations[orientation].canvasWidth,
                        orientations[orientation].canvasHeight));
      for (int fill = 1; fill < painter.count; ++fill) {
        SC_CHECK(sameColor(painter.fills[fill].color, ink));
      }
    }
  }

  // ----- Level 2, First light: one 1 px rule of accent #2A3146 at row 440
  // landscape, 784 portrait, both multiples of 8, with two under-bands of
  // secondary #1C2433 below it. The rule color sits exactly at the secondary
  // cap, so it draws across the quiet zones and only the frame cuts it.
  {
    const BgColor rule = colorOf(0x2A3146);
    const BgColor under = colorOf(0x1C2433);
    const ExpectedRect landscapeRects[3] = {
        {rule, 0, 440, 960, 1}, {under, 0, 448, 960, 3}, {under, 0, 456, 960, 4}};
    SC_CHECK(plateMatches(landscape, *designOf(2), landscapeRects, 3));
    const ExpectedRect portraitRects[3] = {
        {rule, 0, 784, 544, 1}, {under, 0, 792, 544, 3}, {under, 0, 800, 544, 4}};
    SC_CHECK(plateMatches(portrait, *designOf(2), portraitRects, 3));

    // Three elements, each cut into two fills by the frame ring.
    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(2), zones, painter, 0);
      SC_CHECK(painter.count == 7);
      const int ruleRow = orientation == 0 ? 440 : 784;
      SC_CHECK(sameFill(painter.fills[1], rule, 0, ruleRow, zones.frame.x, 1));
      SC_CHECK(sameFill(painter.fills[2], rule, zones.frame.x + zones.frame.w,
                        ruleRow, zones.canvasWidth - zones.frame.x -
                                     zones.frame.w, 1));
      SC_CHECK(sameFill(painter.fills[3], under, 0, ruleRow + 8,
                        zones.frame.x, 3));
    }
  }

  // ----- Level 3, Distant stars: exactly 28 tiny square stars inside the
  // loud margins, 1 x 1 and 2 x 2, secondary #1B2230 and accent #64718C, at
  // the hashed placement the seed 0x5355 ranks. The placement is the
  // approved one of tools/backgrounds/designs.py, so the whole stream is
  // pinned by a digest plus anchors, and the properties are checked apart
  // from the digest.
  {
    // FNV-1a over every element fill, in emission order: color, x, y, w, h.
    const auto digestOf = [](const RecordingPainter& painter) {
      std::uint32_t hash = 0x811C9DC5u;
      const auto feed = [&hash](std::uint32_t value, int bytes) {
        for (int byte = 0; byte < bytes; ++byte) {
          hash ^= (value >> (8 * byte)) & 0xFFu;
          hash *= 0x01000193u;
        }
      };
      for (int index = 1; index < painter.count && index < kRecordCapacity;
           ++index) {
        const RecordedFill& fill = painter.fills[index];
        feed(fill.color.r, 1);
        feed(fill.color.g, 1);
        feed(fill.color.b, 1);
        feed(static_cast<std::uint32_t>(fill.x), 2);
        feed(static_cast<std::uint32_t>(fill.y), 2);
        feed(static_cast<std::uint32_t>(fill.w), 1);
        feed(static_cast<std::uint32_t>(fill.h), 1);
      }
      return hash;
    };

    // The digests of the approved placement, produced from the same
    // encoding over the design package's own element list.
    const std::uint32_t approvedDigest[2] = {0x6C727405u, 0xFFBEC8EEu};
    // The star counts of each loud margin: landscape spreads them over the
    // two side margins, portrait keeps twelve in each side strip and gives
    // the bottom strip the four that the left strip's rows cannot hold.
    const int marginStars[2][3] = {{11, 17, 0}, {12, 12, 4}};
    const int brightStars[2] = {11, 14};
    const int dimStars[2] = {17, 14};
    const int smallStars[2] = {14, 13};
    const int bigStars[2] = {14, 15};
    // Anchors of the approved stream: its first three stars of each
    // orientation, in emission order.
    const struct {
      int x;
      int y;
      int w;
      std::uint32_t color;
    } anchors[2][3] = {
        {{779, 371, 2, 0x1B2230}, {844, 275, 2, 0x1B2230},
         {954, 395, 1, 0x1B2230}},
        {{2, 322, 1, 0x1B2230}, {34, 882, 1, 0x1B2230},
         {513, 796, 1, 0x1B2230}}};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(3), zones, painter, 0);
      // The base fill, then exactly 28 stars - one fill each, because a
      // star never crosses the edge of the margin that holds it.
      SC_CHECK(painter.count == 29);
      SC_CHECK(digestOf(painter) == approvedDigest[orientation]);

      int bright = 0;
      int dim = 0;
      int small = 0;
      int big = 0;
      int perMargin[3] = {0, 0, 0};
      for (int fill = 1; fill < painter.count; ++fill) {
        const RecordedFill& star = painter.fills[fill];
        SC_CHECK(star.w == star.h);  // squares only
        SC_CHECK(star.w == 1 || star.w == 2);
        if (star.w == 1) {
          ++small;
        } else {
          ++big;
        }
        if (sameColor(star.color, designOf(3)->accent)) {
          ++bright;
        } else if (sameColor(star.color, designOf(3)->secondary)) {
          ++dim;
        } else {
          SC_CHECK(false);  // no third shade exists
        }
        // Every star sits whole inside one loud margin.
        int home = -1;
        for (int margin = 0; margin < zones.loudCount; ++margin) {
          const BgRect& m = zones.loud[margin];
          if (star.x >= m.x && star.y >= m.y && star.x + star.w <= m.x + m.w &&
              star.y + star.h <= m.y + m.h) {
            home = margin;
          }
        }
        SC_CHECK(home >= 0);
        if (home >= 0) {
          ++perMargin[home];
        }
      }
      SC_CHECK(bright == brightStars[orientation]);
      SC_CHECK(dim == dimStars[orientation]);
      SC_CHECK(small == smallStars[orientation]);
      SC_CHECK(big == bigStars[orientation]);
      for (int margin = 0; margin < 3; ++margin) {
        SC_CHECK(perMargin[margin] == marginStars[orientation][margin]);
      }
      for (int anchor = 0; anchor < 3; ++anchor) {
        const RecordedFill& star = painter.fills[1 + anchor];
        SC_CHECK(star.x == anchors[orientation][anchor].x);
        SC_CHECK(star.y == anchors[orientation][anchor].y);
        SC_CHECK(star.w == anchors[orientation][anchor].w);
        SC_CHECK(star.color.r == (anchors[orientation][anchor].color >> 16) &&
                 star.color.g == ((anchors[orientation][anchor].color >> 8) & 0xFF) &&
                 star.color.b == (anchors[orientation][anchor].color & 0xFF));
      }
    }
  }

  // ----- Level 4, Quiet strata: eight full-width bands of secondary
  // #222A39 stacked from the bottom edge upward, one step at each band end
  // that reaches a loud margin and fits inside it.
  {
    const BgColor ink = colorOf(0x222A39);
    // The band rows of both orientations: y and height, bottom first.
    const int bandRows[2][8][2] = {
        {{505, 30}, {472, 24}, {430, 33}, {399, 22},
         {363, 27}, {335, 19}, {302, 24}, {277, 16}},
        {{898, 53}, {846, 43}, {779, 58}, {732, 38},
         {675, 48}, {632, 34}, {580, 43}, {542, 29}}};
    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      ExpectedRect rects[8 + 32];
      int rectCount = 0;
      for (int band = 0; band < 8; ++band) {
        const int y = bandRows[orientation][band][0];
        const int height = bandRows[orientation][band][1];
        rects[rectCount++] = ExpectedRect{ink, 0, y, zones.canvasWidth, height};
        for (int margin = 0; margin < zones.loudCount; ++margin) {
          const BgRect& home = zones.loud[margin];
          if (home.y + home.h <= y || home.y >= y + height) {
            continue;
          }
          for (int side = 0; side < 2; ++side) {
            const int stepX = side == 0 ? home.x : home.x + home.w - 6;
            if (stepX >= home.x && stepX + 6 <= home.x + home.w &&
                y - 6 >= home.y && y + height <= home.y + home.h) {
              rects[rectCount++] =
                  ExpectedRect{ink, stepX, y - 6, 6, height + 6};
            }
          }
        }
      }
      SC_CHECK(plateMatches(zones, *designOf(4), rects, rectCount));
      // The element budget: 8 bands plus their steps, at the 40 band
      // element ceiling landscape, below it portrait.
      SC_CHECK(rectCount == (orientation == 0 ? 40 : 36));
      SC_CHECK(rectCount <= kMaxBandElements);
      // The bands cut around the frame ring: two fills each, three for the
      // portrait band the ring's bottom edge crosses.
      RecordingPainter painter;
      paintGrammar(*designOf(4), zones, painter, 0);
      SC_CHECK(painter.count == 1 + (orientation == 0 ? 16 : 17) +
                               (orientation == 0 ? 32 : 28));
      for (int fill = 1; fill < painter.count; ++fill) {
        SC_CHECK(sameColor(painter.fills[fill].color, ink));
      }
    }
  }

  // ----- The contract of the chapter as a whole, both orientations.
  // The element count each record states: 8 corner steps, 3 rules and
  // under-bands, 28 stars, 8 strata plus the steps that fit their margins.
  const int elementCounts[2][4] = {{8, 3, 28, 40}, {8, 3, 28, 36}};
  for (int orientation = 0; orientation < 2; ++orientation) {
    const Zones& zones = orientations[orientation];
    const long canvasArea =
        static_cast<long>(zones.canvasWidth) * zones.canvasHeight;
    for (int level = 1; level <= 4; ++level) {
      const Design& design = *designOf(level);

      // Palette placement: nothing above the accent cap anywhere, nothing
      // above the secondary cap outside the loud margins, and the accent
      // colors cover at most two percent of the complete canvas.
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(design, zones, plate, 0);
      long accentPixels = 0;
      for (int y = 0; y < zones.canvasHeight; ++y) {
        for (int x = 0; x < zones.canvasWidth; ++x) {
          const BgColor color = plate.at(x, y);
          SC_CHECK(withinCap(color, kAccentCap));
          if (!withinCap(color, kSecondaryCap)) {
            ++accentPixels;
            bool inLoud = false;
            for (int margin = 0; margin < zones.loudCount; ++margin) {
              const BgRect& m = zones.loud[margin];
              inLoud = inLoud || (x >= m.x && x < m.x + m.w && y >= m.y &&
                                  y < m.y + m.h);
            }
            SC_CHECK(inLoud);
          }
        }
      }
      SC_CHECK(accentPixels * 100 <= canvasArea * 2);

      // Fully static: the same fill stream at arbitrary clock values,
      // including the wrap boundary of the 32-bit presentation clock.
      RecordingPainter atZero;
      paintGrammar(design, zones, atZero, 0);
      const std::uint32_t probes[6] = {1, 999, 30000, 3600000, 4294967295u,
                                       4294966300u};
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter later;
        paintGrammar(design, zones, later, probes[probe]);
        SC_CHECK(later.count == atZero.count);
        for (int fill = 0; fill < atZero.count && fill < kRecordCapacity;
             ++fill) {
          SC_CHECK(sameFill(later.fills[fill], atZero.fills[fill].color,
                            atZero.fills[fill].x, atZero.fills[fill].y,
                            atZero.fills[fill].w, atZero.fills[fill].h));
        }
      }

      // The budget class of the record holds the emitted elements.
      const int elementCeiling =
          design.budget == BudgetClass::BandElements ? kMaxBandElements
                                                     : kMaxSmallElements;
      SC_CHECK(elementCounts[orientation][level - 1] <= elementCeiling);
    }
  }

  // ----- Distinctness at the one-eighth review scale: adjacent levels of
  // the chapter differ in a visible step of six of 255 gray in at least one
  // percent of the samples, the rule the design package checks over its own
  // landscape plates. The plates here are the real frame - the grammar's
  // output, with the board well and ring holding the base - so the portrait
  // share runs lower than the package's unclipped plate: the frame covers
  // 74 percent of the portrait canvas width, and what the wallpapers may
  // draw there is the base alone. Portrait carries half the floor.
  for (int orientation = 0; orientation < 2; ++orientation) {
    const Zones& zones = orientations[orientation];
    static int thumbs[4][kEighthSamples];
    int widths[4] = {0, 0, 0, 0};
    int heights[4] = {0, 0, 0, 0};
    for (int level = 1; level <= 4; ++level) {
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(level), zones, plate, 0);
      eighthScale(plate, thumbs[level - 1], &widths[level - 1],
                  &heights[level - 1]);
    }
    const int floorDivisor = orientation == 0 ? 100 : 200;
    for (int level = 1; level < 4; ++level) {
      const int samples = widths[0] * heights[0];
      int differ = 0;
      for (int index = 0; index < samples; ++index) {
        const int delta = thumbs[level - 1][index] - thumbs[level][index];
        if (delta >= 6 || delta <= -6) {
          ++differ;
        }
      }
      SC_CHECK(differ >= samples / floorDivisor);
      std::printf("  %s %d vs %d: %d of %d samples differ (%.2f%%)\n",
                  orientationNames[orientation], level, level + 1, differ,
                  samples, 100.0 * differ / samples);
    }
  }
}

// ---------------------------------------------------------------------------
// The Stir chapter of task T080: levels 5 through 8, one slow moving layer
// per design. Every check runs on both orientations against the retained
// records of docs/backgrounds/level-05.md through level-08.md.
// ---------------------------------------------------------------------------

// The FNV-1a digest over a grammar's element fills, in emission order -
// color, x, y, w, h - the same encoding the Still suite pins its star
// stream with. The expected values come from the design package's own
// element lists of tools/backgrounds/designs.py, which the runtime
// reproduces pixel for pixel (see notes/agent-testing.md, T080).
std::uint32_t stirDigest(const RecordingPainter& painter) {
  std::uint32_t hash = 0x811C9DC5u;
  const auto feed = [&hash](std::uint32_t value, int bytes) {
    for (int byte = 0; byte < bytes; ++byte) {
      hash ^= (value >> (8 * byte)) & 0xFFu;
      hash *= 0x01000193u;
    }
  };
  for (int index = 1; index < painter.count && index < kRecordCapacity;
       ++index) {
    const RecordedFill& fill = painter.fills[index];
    feed(fill.color.r, 1);
    feed(fill.color.g, 1);
    feed(fill.color.b, 1);
    feed(static_cast<std::uint32_t>(fill.x), 2);
    feed(static_cast<std::uint32_t>(fill.y), 2);
    feed(static_cast<std::uint32_t>(fill.w), 1);
    feed(static_cast<std::uint32_t>(fill.h), 1);
  }
  return hash;
}

// True when two streams hold the same fill sequence.
bool sameStream(const RecordingPainter& a, const RecordingPainter& b) {
  if (a.count != b.count) {
    return false;
  }
  for (int index = 0; index < a.count && index < kRecordCapacity; ++index) {
    if (!sameFill(a.fills[index], b.fills[index].color, b.fills[index].x,
                  b.fills[index].y, b.fills[index].w, b.fills[index].h)) {
      return false;
    }
  }
  return true;
}

// Python's round() over a double, halves to even: the arithmetic of the
// design records' size formulas, for the roof and floor geometry the
// Stir suite restates from them.
int recordRound(double value) {
  return static_cast<int>(std::nearbyint(value));
}

// The mark density of the design package's review scale, check.py's
// edge_density(): neighboring one-eighth samples that differ by six of 255
// gray or more, per thousand samples. The chapter-activity check of the
// Stir contract reads it over whole plates.
int eighthMarksPer1000(const PlatePainter& plate) {
  static int thumbs[kEighthSamples];
  int w = 0;
  int h = 0;
  eighthScale(plate, thumbs, &w, &h);
  int transitions = 0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int here = thumbs[y * w + x];
      if (x + 1 < w && std::abs(here - thumbs[y * w + x + 1]) >= 6) {
        ++transitions;
      }
      if (y + 1 < h && std::abs(here - thumbs[(y + 1) * w + x]) >= 6) {
        ++transitions;
      }
    }
  }
  return transitions * 1000 / (w * h);
}

// One moving-layer displacement audit. The streams at `tEarly` and
// `tLate` hold the same element count; every element moved within one
// pixel of the nominal displacement speed * dt, or within one pixel of
// that minus its wrap span, and by nothing else: the exact sub-pixel
// phase the package rounds differs per element, but a jump, a teleport,
// or a second layer moving always breaks the window.
bool driftMovedCleanly(const RecordingPainter& early,
                       const RecordingPainter& late, int speedMilli,
                       long long dtMs, const int* wrapSpans, int spanCount) {
  if (early.count != late.count) {
    return false;
  }
  const long long milli = speedMilli * dtMs / 1000;
  for (int index = 0; index < early.count && index < kRecordCapacity; ++index) {
    const long long dy =
        static_cast<long long>(late.fills[index].y - early.fills[index].y);
    bool ok = (1000 * dy - milli <= 1500) && (milli - 1000 * dy <= 1500);
    for (int span = 0; span < spanCount && !ok; ++span) {
      const long long wrapped = milli - 1000LL * wrapSpans[span];
      ok = (1000 * dy - wrapped <= 1500) && (wrapped - 1000 * dy <= 1500);
    }
    if (!ok) {
      return false;
    }
  }
  return true;
}

void run_backgrounds_stir_checks() {
  std::printf("test_backgrounds: the Stir chapter of levels 5 through 8\n");

  const Zones landscape = maskLandscapeZones();
  const Zones portrait = maskPortraitZones();
  const Zones orientations[2] = {landscape, portrait};
  const char* orientationNames[2] = {"landscape", "portrait"};

  // ----- Level 5, Slow drift: exactly 56 square stars of secondary
  // #20283A and accent #687892 in equal share, 1 x 1 px in 60 percent and
  // 2 x 2 in 40, on the 20 px cell grid the seed 0x5356 ranks, plus 10
  // static dust flecks of the secondary, 3 x 1 px, from the seed 0x5357.
  // The whole stream is pinned by digest plus anchors, as at level 3.
  {
    const std::uint32_t approvedDigest[2] = {0xFC2FED9Bu, 0xB2DBD380u};
    // The digests of the moving layer at the motion probes below: the
    // exact-half times 250, 750 and 1250 ms pin the package's round-to-
    // even arithmetic, where half the stars step and half hold by parity.
    const std::uint32_t probeDigests[2][6] = {
        {0xDE07477Bu, 0x662870E7u, 0x996191A8u, 0xA34434A4u, 0xB927C27Du,
         0x0EF7A391u},
        {0x855B6366u, 0x6B8A04F0u, 0x940889BEu, 0x88B729B4u, 0xB7E6C956u,
         0xBEFCAA95u}};
    const std::uint32_t probeTimes[6] = {250, 500, 750, 1000, 1250, 2000};
    const int marginStars[2][3] = {{31, 25, 0}, {24, 24, 8}};
    const int marginDust[2][3] = {{3, 7, 0}, {4, 4, 2}};
    // Anchors of the approved stream: the first three stars of each
    // orientation, in emission order.
    const struct {
      int x;
      int y;
      int w;
      std::uint32_t color;
    } anchors[2][3] = {
        {{18, 340, 1, 0x687892}, {51, 41, 2, 0x687892},
         {38, 124, 1, 0x687892}},
        {{18, 459, 1, 0x687892}, {38, 243, 1, 0x687892},
         {512, 231, 2, 0x687892}}};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(5), zones, painter, 0);
      // The base fill, then exactly 66 elements - one fill each, because
      // no star of this placement crosses its margin edge at t = 0.
      SC_CHECK(painter.count == 67);
      SC_CHECK(stirDigest(painter) == approvedDigest[orientation]);

      int stars = 0;
      int dust = 0;
      int perMarginStars[3] = {0, 0, 0};
      int perMarginDust[3] = {0, 0, 0};
      for (int fill = 1; fill < painter.count; ++fill) {
        const RecordedFill& dot = painter.fills[fill];
        // Every dot sits whole inside one loud margin at t = 0.
        int home = -1;
        for (int margin = 0; margin < zones.loudCount; ++margin) {
          const BgRect& m = zones.loud[margin];
          if (dot.x >= m.x && dot.y >= m.y && dot.x + dot.w <= m.x + m.w &&
              dot.y + dot.h <= m.y + m.h) {
            home = margin;
          }
        }
        SC_CHECK(home >= 0);
        if (dot.w == 3 && dot.h == 1) {
          ++dust;  // the static layer
          SC_CHECK(sameColor(dot.color, designOf(5)->secondary));
          if (home >= 0) {
            ++perMarginDust[home];
          }
        } else {
          ++stars;
          SC_CHECK(dot.w == dot.h);  // squares only
          SC_CHECK(dot.w == 1 || dot.w == 2);
          SC_CHECK(sameColor(dot.color, designOf(5)->secondary) ||
                   sameColor(dot.color, designOf(5)->accent));
          if (home >= 0) {
            ++perMarginStars[home];
          }
        }
      }
      SC_CHECK(stars == 56);
      SC_CHECK(dust == 10);
      for (int margin = 0; margin < 3; ++margin) {
        SC_CHECK(perMarginStars[margin] == marginStars[orientation][margin]);
        SC_CHECK(perMarginDust[margin] == marginDust[orientation][margin]);
      }
      for (int anchor = 0; anchor < 3; ++anchor) {
        const RecordedFill& star = painter.fills[1 + anchor];
        SC_CHECK(star.x == anchors[orientation][anchor].x);
        SC_CHECK(star.y == anchors[orientation][anchor].y);
        SC_CHECK(star.w == anchors[orientation][anchor].w);
      }

      // The motion probes: the exact digests of the moving layer, and the
      // displacement audit between consecutive streams. The stars sink at
      // 2 px/s: after 500 ms every star sits one pixel lower or has
      // wrapped to its margin top; the half-time steps stay within one
      // pixel of the nominal displacement.
      RecordingPainter previous;
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(5), zones, moved, probeTimes[probe]);
        SC_CHECK(moved.count == 67);
        SC_CHECK(stirDigest(moved) == probeDigests[orientation][probe]);
        if (probe > 0) {
          const long long dt =
              static_cast<long long>(probeTimes[probe]) - probeTimes[probe - 1];
          int spans[3] = {0, 0, 0};
          for (int margin = 0; margin < zones.loudCount; ++margin) {
            spans[margin] = zones.loud[margin].h;
          }
          SC_CHECK(driftMovedCleanly(previous, moved, 2000, dt, spans,
                                     zones.loudCount));
        }
        previous = moved;
      }

      // The dust holds still under the moving layer: the static dust
      // fills are byte-identical at every probe.
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(5), zones, moved, probeTimes[probe]);
        RecordingPainter zero;
        paintGrammar(*designOf(5), zones, zero, 0);
        int dustIndex = 0;
        for (int fill = 1; fill < zero.count; ++fill) {
          if (zero.fills[fill].w == 3) {
            SC_CHECK(sameFill(moved.fills[fill], zero.fills[fill].color,
                              zero.fills[fill].x, zero.fills[fill].y,
                              zero.fills[fill].w, zero.fills[fill].h));
            ++dustIndex;
          }
        }
        SC_CHECK(dustIndex == 10);
      }

      // The wrap period of the record: one full pass of the landscape
      // margin takes 272 s at 2 px/s, so the stream at 272 s is the
      // stream at zero again. Portrait wraps its side margins after 401 s
      // and its bottom strip after 19.5 s; their common multiple returns
      // the whole stream.
      RecordingPainter zero;
      paintGrammar(*designOf(5), zones, zero, 0);
      RecordingPainter afterPass;
      paintGrammar(*designOf(5), zones, afterPass,
                   orientation == 0 ? 272000u : 15639000u);
      SC_CHECK(sameStream(zero, afterPass));
    }
  }

  // ----- Level 6, Dither dusk: five stepped dusk zones down the side
  // margins - zones 1, 3 and 5 carry the secondary #242B3A as one band
  // inset 6 px plus two 6 px end caps 5 px taller than the zone, the
  // others stay base - and two 16 x 12 checker patches of accent #38445B
  // per zone rising at 2 px/s, 4 px squares on the even cells: 120
  // squares, 60 per side margin.
  {
    const std::uint32_t approvedDigest[2] = {0xEFF63442u, 0xC7FCA252u};
    const std::uint32_t probeDigests[2][6] = {
        {0x02E9A372u, 0x617CFBDCu, 0x4E5A8880u, 0x1DA3DD0Cu, 0xC1040F1Cu,
         0x71842C44u},
        {0xA5F6BAC4u, 0x3588D01Eu, 0x06E5B328u, 0x3519EEAEu, 0x4E430A5Cu,
         0x063DF07Au}};
    const std::uint32_t probeTimes[6] = {250, 500, 750, 1000, 1250, 2000};
    // The zone rows of the record: y and height of the three inked zones,
    // identical in both side margins (shares 8, 12 and 16 percent from
    // 30 percent of the canvas height down, 4 px gaps between zones).
    const int zoneRows[2][3][2] = {
        {{163, 44}, {269, 65}, {418, 87}}, {{288, 77}, {469, 115}, {726, 154}}};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(6), zones, painter, 0);
      SC_CHECK(painter.count == 139);
      SC_CHECK(stirDigest(painter) == approvedDigest[orientation]);

      // The zones: for each side margin and each inked zone, the band
      // plus its two end caps, exactly the rectangles the record states.
      int fill = 1;
      for (int margin = 0; margin < 2; ++margin) {
        const BgRect& home = zones.loud[margin];
        for (int zone = 0; zone < 3; ++zone) {
          const int zy = zoneRows[orientation][zone][0];
          const int zh = zoneRows[orientation][zone][1];
          for (int side = 0; side < 2; ++side) {
            const int sx = side == 0 ? home.x : home.x + home.w - 6;
            SC_CHECK(sameFill(painter.fills[fill++], designOf(6)->secondary,
                              sx, zy - 5, 6, zh + 5));
          }
          SC_CHECK(sameFill(painter.fills[fill++], designOf(6)->secondary,
                            home.x + 6, zy, home.w - 12, zh));
        }
      }
      SC_CHECK(fill == 19);  // the base plus 18 zone rectangles

      // The patches: 120 squares of 4 x 4 accent, 60 per side margin, and
      // nothing else in the stream.
      int squares = 0;
      int perMargin[3] = {0, 0, 0};
      for (; fill < painter.count; ++fill) {
        const RecordedFill& square = painter.fills[fill];
        SC_CHECK(square.w == 4 && square.h == 4);
        SC_CHECK(sameColor(square.color, designOf(6)->accent));
        ++squares;
        for (int margin = 0; margin < zones.loudCount; ++margin) {
          const BgRect& m = zones.loud[margin];
          if (square.x >= m.x && square.y >= m.y &&
              square.x + 4 <= m.x + m.w && square.y + 4 <= m.y + m.h) {
            ++perMargin[margin];
          }
        }
      }
      SC_CHECK(squares == 120);
      SC_CHECK(perMargin[0] == 60 && perMargin[1] == 60 && perMargin[2] == 0);

      // Motion probes and the displacement audit: the patch layer rises
      // 2 px/s; the zones hold still.
      RecordingPainter previous;
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(6), zones, moved, probeTimes[probe]);
        SC_CHECK(moved.count == 139);
        SC_CHECK(stirDigest(moved) == probeDigests[orientation][probe]);
        for (int zone = 1; zone <= 18; ++zone) {
          SC_CHECK(sameFill(moved.fills[zone], painter.fills[zone].color,
                            painter.fills[zone].x, painter.fills[zone].y,
                            painter.fills[zone].w, painter.fills[zone].h));
        }
        if (probe > 0) {
          const long long dt =
              static_cast<long long>(probeTimes[probe]) - probeTimes[probe - 1];
          int spans[2] = {zones.loud[0].h + 12, zones.loud[1].h + 12};
          SC_CHECK(driftMovedCleanly(previous, moved, -2000, dt, spans, 2));
        }
        previous = moved;
      }

      // The seamless wrap of the record: a patch leaves the canvas fully
      // before it re-enters, wrapping over the margin height plus its own
      // 12 px. Landscape returns after 556 px (278 s); the portrait side
      // margins after 814 px (407 s).
      RecordingPainter zero;
      paintGrammar(*designOf(6), zones, zero, 0);
      RecordingPainter afterPass;
      paintGrammar(*designOf(6), zones, afterPass,
                   orientation == 0 ? 278000u : 407000u);
      SC_CHECK(sameStream(zero, afterPass));
    }
  }

  // ----- Level 7, Sleeping skyline: fourteen stepped towers of secondary
  // #252D3D anchored on the bottom edge, tallest at the corners, two roof
  // blocks above each top, and windows of accent #64728A on the towers
  // taller than 30 percent of the canvas - 54 landscape, 168 portrait -
  // swelling one pixel of height over the 8 s cycle.
  {
    const std::uint32_t quietDigest[2] = {0xA2A4AA24u, 0x3C925CE9u};
    const std::uint32_t swellDigest[2] = {0x5C29EBE0u, 0x9BC37A86u};
    // The tower geometry of the record at t = 0: x, top, width, height.
    const int towers[2][14][4] = {
        {{3, 352, 43, 192}, {79, 340, 43, 204}, {152, 387, 43, 157},
         {211, 385, 43, 159}, {275, 403, 43, 141}, {352, 402, 43, 142},
         {426, 462, 43, 82}, {493, 470, 43, 74}, {552, 468, 43, 76},
         {628, 397, 43, 147}, {694, 406, 43, 138}, {770, 401, 43, 143},
         {841, 381, 43, 163}, {891, 344, 43, 200}},
        {{0, 619, 5, 341}, {486, 612, 5, 348}, {9, 618, 5, 342},
         {494, 621, 5, 339}, {17, 594, 5, 366}, {504, 574, 5, 386},
         {27, 634, 5, 326}, {511, 633, 5, 327}, {35, 659, 5, 301},
         {519, 562, 5, 398}, {42, 624, 5, 336}, {528, 630, 5, 330},
         {51, 659, 5, 301}, {536, 581, 5, 379}}};
    const int windowCount[2] = {54, 168};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      const int windowFloor = recordRound(
          static_cast<double>(zones.canvasHeight) * 0.30);

      // The plate: every tower's ink at its stated rectangle - the top
      // edge and the bottom row where the frame does not cover them -
      // the base above its top and past its right edge, and the two roof
      // blocks: 60 percent of the width 8 px above the top, then 30
      // percent 6 px higher still. The towers that cross the well keep
      // their ink beside and below the frame ring alone.
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(7), zones, plate, 0);
      const auto outsideFrame = [&zones](int px, int py) {
        return px < zones.frame.x ||
               px >= zones.frame.x + zones.frame.w ||
               py < zones.frame.y || py >= zones.frame.y + zones.frame.h;
      };
      for (int slot = 0; slot < 14; ++slot) {
        const int x = towers[orientation][slot][0];
        const int top = towers[orientation][slot][1];
        const int w = towers[orientation][slot][2];
        const int h = towers[orientation][slot][3];
        SC_CHECK(top == zones.canvasHeight - h);
        // The bottom edge row sits below the frame ring in both frames.
        SC_CHECK(outsideFrame(x, zones.canvasHeight - 1));
        SC_CHECK(sameColor(plate.at(x, zones.canvasHeight - 1),
                           designOf(7)->secondary));
        if (outsideFrame(x, top)) {
          SC_CHECK(sameColor(plate.at(x, top), designOf(7)->secondary));
        }
        if (top > 0 && outsideFrame(x, top - 1)) {
          SC_CHECK(sameColor(plate.at(x, top - 1), designOf(7)->base));
        }
        if (x + w < zones.canvasWidth && outsideFrame(x + w, top)) {
          SC_CHECK(sameColor(plate.at(x + w, top), designOf(7)->base));
        }
        const int roof1x = x + recordRound(w * 0.2);
        const int roof1w = recordRound(w * 0.6);
        const int roof2x = x + recordRound(w * 0.35);
        const int roof2w = recordRound(w * 0.3);
        if (outsideFrame(roof1x, top - 8)) {
          SC_CHECK(sameColor(plate.at(roof1x, top - 8),
                             designOf(7)->secondary));
          SC_CHECK(sameColor(plate.at(roof1x + roof1w - 1, top - 8),
                             designOf(7)->secondary));
        }
        if (outsideFrame(roof2x + roof2w - 1, top - 14)) {
          SC_CHECK(sameColor(plate.at(roof2x + roof2w - 1, top - 14),
                             designOf(7)->secondary));
          if (outsideFrame(roof2x - 1, top - 14)) {
            SC_CHECK(sameColor(plate.at(roof2x - 1, top - 14),
                               designOf(7)->base));
          }
        }
      }

      // The stream at rest: the digest of the quiet phase, its fill
      // count, and the windows - 3 px wide, 4 tall, inside one loud
      // margin, on tall towers only.
      RecordingPainter painter;
      paintGrammar(*designOf(7), zones, painter, 0);
      SC_CHECK(painter.count == (orientation == 0 ? 89 : 211));
      SC_CHECK(stirDigest(painter) == quietDigest[orientation]);
      int windows = 0;
      int towerArea = 0;
      for (int fill = 1; fill < painter.count; ++fill) {
        const RecordedFill& element = painter.fills[fill];
        if (sameColor(element.color, designOf(7)->accent)) {
          ++windows;
          SC_CHECK(element.w == 3 && element.h == 4);
          bool inLoud = false;
          for (int margin = 0; margin < zones.loudCount; ++margin) {
            const BgRect& m = zones.loud[margin];
            inLoud = inLoud || (element.x >= m.x && element.y >= m.y &&
                                element.x + 3 <= m.x + m.w &&
                                element.y + 4 <= m.y + m.h);
          }
          SC_CHECK(inLoud);
          // On a tall tower: the window's column is inside a tower of the
          // stated geometry whose height reaches the 30 percent floor.
          bool onTallTower = false;
          for (int slot = 0; slot < 14; ++slot) {
            const int x = towers[orientation][slot][0];
            const int top = towers[orientation][slot][1];
            const int w = towers[orientation][slot][2];
            const int h = towers[orientation][slot][3];
            if (h >= windowFloor && element.x >= x && element.x + 3 <= x + w &&
                element.y >= top && element.y + 4 <= top + h) {
              onTallTower = true;
            }
          }
          SC_CHECK(onTallTower);
        } else {
          ++towerArea;
        }
      }
      SC_CHECK(windows == windowCount[orientation]);
      // The towers and roofs of the stream: 42 elements portrait, 34
      // fills landscape, where the frame ring hides the middle towers'
      // upper bodies and every center roof under the well.
      SC_CHECK(towerArea == (orientation == 0 ? 34 : 42));

      // The pulse: the swell is one through the middle half of the 8 s
      // cycle and zero at its ends, with the exact halves rounded to even
      // as the package rounds them - quiet at 2000 ms and 6000 ms, swelled
      // from 2001 ms to 5999 ms. The towers hold still; the windows grow
      // one pixel upward and their bottom row holds.
      const std::uint32_t quietTimes[5] = {1, 1000, 2000, 6000, 6001};
      for (int probe = 0; probe < 5; ++probe) {
        RecordingPainter quiet;
        paintGrammar(*designOf(7), zones, quiet, quietTimes[probe]);
        SC_CHECK(stirDigest(quiet) == quietDigest[orientation]);
      }
      const std::uint32_t swellTimes[4] = {2001, 4000, 5000, 5999};
      for (int probe = 0; probe < 4; ++probe) {
        RecordingPainter swelled;
        paintGrammar(*designOf(7), zones, swelled, swellTimes[probe]);
        SC_CHECK(swelled.count == painter.count);
        SC_CHECK(stirDigest(swelled) == swellDigest[orientation]);
        // The same window count, every window one pixel taller, the
        // bottom row and every tower pixel unchanged.
        int quietWindows = 0;
        int swellWindows = 0;
        for (int fill = 1; fill < painter.count; ++fill) {
          const bool isWindow =
              sameColor(painter.fills[fill].color, designOf(7)->accent);
          if (isWindow) {
            ++quietWindows;
            ++swellWindows;
            SC_CHECK(swelled.fills[fill].w == 3);
            SC_CHECK(swelled.fills[fill].h == 5);
            SC_CHECK(swelled.fills[fill].y == painter.fills[fill].y - 1);
            SC_CHECK(swelled.fills[fill].y + 5 ==
                     painter.fills[fill].y + 4);
          } else {
            SC_CHECK(sameFill(swelled.fills[fill], painter.fills[fill].color,
                              painter.fills[fill].x, painter.fills[fill].y,
                              painter.fills[fill].w, painter.fills[fill].h));
          }
        }
        SC_CHECK(quietWindows == windowCount[orientation]);
        SC_CHECK(swellWindows == windowCount[orientation]);
      }

      // The cycle repeats: 8 s of clock return the quiet stream.
      RecordingPainter afterCycle;
      paintGrammar(*designOf(7), zones, afterCycle, 8000);
      SC_CHECK(sameStream(painter, afterCycle));
    }
  }

  // ----- Level 8, Low signal: 1 px rules of secondary #242C3B every 10
  // rows in two bursts per loud margin - an uneven x run inside an uneven
  // y band, so the runs read as broken transmissions - plus two solid
  // bars of accent #54617A, 3 px tall, in the first two margins. The rule
  // set scrolls down 3 px/s; the bars hold still.
  {
    const std::uint32_t approvedDigest[2] = {0x10F37849u, 0x13B89A2Fu};
    // The band geometry the seeds place: margin x, group, band y, band
    // height, span x, span width.
    const int bands[2][6][6] = {
        {{0, 0, 189, 129, 64, 109},
         {0, 1, 105, 90, 37, 120},
         {768, 0, 143, 130, 833, 92},
         {768, 1, 344, 137, 776, 145},
         {0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0}},
        {{0, 0, 326, 129, 6, 46},
         {0, 1, 260, 90, 6, 47},
         {485, 0, 352, 118, 491, 47},
         {485, 1, 205, 107, 491, 47},
         {0, 0, 929, 129, 48, 193},
         {0, 1, 929, 90, 433, 80}}};
    const int bandCount[2] = {4, 6};
    const int ruleCount[2] = {47, 50};  // on-canvas rules at t = 0
    const std::uint32_t probeTimes[6] = {250, 500, 750, 1000, 1250, 2000};
    const std::uint32_t probeDigests[2][6] = {
        {0x40A20C36u, 0xCD3EC103u, 0xCD3EC103u, 0x1B862F4Fu, 0x9BCECC66u,
         0x3554E37Cu},
        {0x3D9B8D2Bu, 0x7A8CECB7u, 0x7A8CECB7u, 0x745186FBu, 0xF2129517u,
         0xF43D552F}};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(8), zones, painter, 0);
      SC_CHECK(painter.count == 1 + ruleCount[orientation] + 4);
      SC_CHECK(stirDigest(painter) == approvedDigest[orientation]);

      // The bands: the rules of each group sit at the rows the record
      // states - one every 10 rows from the band top, wrapping at the
      // band height - with the hashed span, and the two accent bars of
      // the first two margins at 14 px under the top and 40 px over the
      // bottom.
      int fill = 1;
      for (int group = 0; group < bandCount[orientation]; ++group) {
        const int* band = bands[orientation][group];
        const int lines = band[3] / 10;
        for (int index = 0; index < lines; ++index) {
          const int y = band[2] + (index * 10) % band[3];
          if (y >= zones.canvasHeight) {
            continue;  // the portrait bottom bands run past the canvas
          }
          SC_CHECK(sameFill(painter.fills[fill++], designOf(8)->secondary,
                            band[4], y, band[5], 1));
        }
      }
      for (int margin = 0; margin < 2; ++margin) {
        const BgRect& home = zones.loud[margin];
        SC_CHECK(sameFill(painter.fills[fill++], designOf(8)->accent,
                          home.x + 6, home.y + 14, home.w - 12, 3));
        SC_CHECK(sameFill(painter.fills[fill++], designOf(8)->accent,
                          home.x + 6, home.y + home.h - 40, home.w - 12, 3));
      }
      SC_CHECK(fill == painter.count);

      // Motion: the offset round(3t) mod 10 slides the rule set down; the
      // digests of the probes pin it exactly, including the round-to-even
      // step at the 500 ms half (1.5 rounds to 2, so 500 ms and 750 ms
      // agree). The bars hold still at every probe.
      RecordingPainter previous;
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(8), zones, moved, probeTimes[probe]);
        SC_CHECK(stirDigest(moved) == probeDigests[orientation][probe]);
        // The last four fills are the bars; they never move.
        for (int bar = 4; bar >= 1; --bar) {
          SC_CHECK(sameFill(moved.fills[moved.count - bar],
                            painter.fills[painter.count - bar].color,
                            painter.fills[painter.count - bar].x,
                            painter.fills[painter.count - bar].y,
                            painter.fills[painter.count - bar].w,
                            painter.fills[painter.count - bar].h));
        }
        // The rule rows shift by the offset step between consecutive
        // probes, staying a rule of the same span and color.
        if (probe > 0) {
          SC_CHECK(moved.count == previous.count);
          for (int index = 1; index < moved.count - 4 &&
                               index < kRecordCapacity;
               ++index) {
            const int dy = moved.fills[index].y - previous.fills[index].y;
            SC_CHECK(dy >= 0 && dy <= 10);  // 3 px/s within 750 ms, one
                                            // texture period at most
            SC_CHECK(moved.fills[index].w == previous.fills[index].w);
            SC_CHECK(moved.fills[index].x == previous.fills[index].x);
            SC_CHECK(sameColor(moved.fills[index].color,
                               previous.fills[index].color));
          }
        }
        previous = moved;
      }

      // The texture period of the record: the pattern repeats every
      // 10/3 s, so the stream at 3333 ms is the stream at zero.
      RecordingPainter zero;
      paintGrammar(*designOf(8), zones, zero, 0);
      RecordingPainter afterCycle;
      paintGrammar(*designOf(8), zones, afterCycle, 3333);
      SC_CHECK(sameStream(zero, afterCycle));
    }
  }

  // ----- The contract of the chapter as a whole, both orientations.
  // The element count each record states: 56 stars and 10 dust, 18 zones
  // and 120 dither squares, 14 towers and 28 roofs plus the windows that
  // fit the loud margins, 4 or 6 rule groups and 4 bars.
  const int elementCeilings[2][4] = {{66, 138, 96, 55},
                                     {66, 138, 210, 60}};
  for (int orientation = 0; orientation < 2; ++orientation) {
    const Zones& zones = orientations[orientation];
    const long canvasArea =
        static_cast<long>(zones.canvasWidth) * zones.canvasHeight;
    for (int level = 5; level <= 8; ++level) {
      const Design& design = *designOf(level);

      // Palette placement and coverage, at rest and mid-motion: nothing
      // above the accent cap anywhere, nothing above the secondary cap
      // outside the loud margins, the accent colors cover at most two
      // percent of the complete canvas, and the same holds mid-motion -
      // the moving layer never lifts coverage past the budget.
      const std::uint32_t probes[4] = {0, 1300, 4100, 123457};
      for (int probe = 0; probe < 4; ++probe) {
        PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
        paintGrammar(design, zones, plate, probes[probe]);
        long accentPixels = 0;
        for (int y = 0; y < zones.canvasHeight; ++y) {
          for (int x = 0; x < zones.canvasWidth; ++x) {
            const BgColor color = plate.at(x, y);
            SC_CHECK(withinCap(color, kAccentCap));
            if (!withinCap(color, kSecondaryCap)) {
              ++accentPixels;
              bool inLoud = false;
              for (int margin = 0; margin < zones.loudCount; ++margin) {
                const BgRect& m = zones.loud[margin];
                inLoud = inLoud || (x >= m.x && x < m.x + m.w && y >= m.y &&
                                    y < m.y + m.h);
              }
              SC_CHECK(inLoud);
            }
          }
        }
        SC_CHECK(accentPixels * 100 <= canvasArea * 2);
      }

      // The budget class of the record holds the emitted elements.
      RecordingPainter painter;
      paintGrammar(design, zones, painter, 0);
      SC_CHECK(painter.count - 1 <= elementCeilings[orientation][level - 5]);
      SC_CHECK(elementCeilings[orientation][level - 5] <= kMaxSmallElements);

      // One moving layer: the motion spec of the catalog, and a stream
      // that changes only through that layer's displacement - the same
      // count at every probe, the same colors, at rest and far along the
      // clock including its 32-bit wrap.
      SC_CHECK(design.motion.kind != MotionKind::None);
      SC_CHECK(design.motion.speedMilliPxPerS <= 10000);
      if (design.motion.kind == MotionKind::Pulse) {
        SC_CHECK(design.motion.cycleMs >= 4000);
      }
      RecordingPainter atZero;
      paintGrammar(design, zones, atZero, 0);
      int accentsAtZero = 0;
      for (int fill = 1; fill < atZero.count; ++fill) {
        if (!sameColor(atZero.fills[fill].color, design.secondary)) {
          ++accentsAtZero;
        }
      }
      const std::uint32_t late[4] = {70000, 123457, 4294967295u,
                                     4294966300u};
      for (int probe = 0; probe < 4; ++probe) {
        RecordingPainter later;
        paintGrammar(design, zones, later, late[probe]);
        RecordingPainter again;
        paintGrammar(design, zones, again, late[probe]);
        // A pure function of the clock alone, far along the moving layer
        // and across the 32-bit wrap of the presentation clock.
        SC_CHECK(sameStream(later, again));
        // Nothing blinks: every element draws in one of its record's two
        // shades at every phase, never a third color.
        int accents = 0;
        for (int fill = 1; fill < later.count && fill < kRecordCapacity;
             ++fill) {
          SC_CHECK(sameColor(later.fills[fill].color, design.secondary) ||
                   sameColor(later.fills[fill].color, design.accent));
          if (!sameColor(later.fills[fill].color, design.secondary)) {
            ++accents;
          }
        }
        if (level == 7) {
          // The skyline's element count is fixed: its towers never split
          // differently, because nothing but the window height moves.
          SC_CHECK(later.count == atZero.count);
          SC_CHECK(accents == accentsAtZero);
        } else if (level == 8) {
          SC_CHECK(accents == 4);  // the four bars, always
        }
      }
    }
  }

  // ----- The chapter activity: the Stir designs carry more review-scale
  // marks than the Still chapter they follow, in both orientations - the
  // arc's first step from calm to life - while the drift ceiling holds
  // every motion at or under 10 px/s and the pulse at 0.25 px/s.
  for (int orientation = 0; orientation < 2; ++orientation) {
    const Zones& zones = orientations[orientation];
    long stillMarks = 0;
    for (int level = 1; level <= 4; ++level) {
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(level), zones, plate, 0);
      stillMarks += eighthMarksPer1000(plate);
    }
    long stirMarks = 0;
    for (int level = 5; level <= 8; ++level) {
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(level), zones, plate, 0);
      stirMarks += eighthMarksPer1000(plate);
    }
    SC_CHECK(stirMarks > stillMarks);
    std::printf("  %s chapter marks per 1000: Still %ld, Stir %ld\n",
                orientationNames[orientation], stillMarks / 4,
                stirMarks / 4);
  }

  // ----- Distinctness at the one-eighth review scale, across the chapter
  // boundary and inside the chapter: adjacent levels differ in a visible
  // step of six of 255 gray in at least one percent of the samples, half
  // that in portrait where the board covers 74 percent of the width.
  for (int orientation = 0; orientation < 2; ++orientation) {
    const Zones& zones = orientations[orientation];
    static int thumbs[5][kEighthSamples];
    int widths[5] = {0, 0, 0, 0, 0};
    int heights[5] = {0, 0, 0, 0, 0};
    for (int level = 4; level <= 8; ++level) {
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(level), zones, plate, 0);
      eighthScale(plate, thumbs[level - 4], &widths[level - 4],
                  &heights[level - 4]);
    }
    const int floorDivisor = orientation == 0 ? 100 : 200;
    for (int level = 4; level < 8; ++level) {
      const int samples = widths[0] * heights[0];
      int differ = 0;
      for (int index = 0; index < samples; ++index) {
        const int delta = thumbs[level - 4][index] - thumbs[level - 3][index];
        if (delta >= 6 || delta <= -6) {
          ++differ;
        }
      }
      SC_CHECK(differ >= samples / floorDivisor);
      std::printf("  %s %d vs %d: %d of %d samples differ (%.2f%%)\n",
                  orientationNames[orientation], level, level + 1, differ,
                  samples, 100.0 * differ / samples);
    }
  }
}

// ---------------------------------------------------------------------------
// The Build chapter of task T081: levels 9 through 12, structure appearing
// - seams, traces, ribbons, streaks - with one moving layer each. Every
// check runs on both orientations against the retained records of
// docs/backgrounds/level-09.md through level-12.md. The streams were
// verified against the design package's own grammars pixel for pixel over
// thousands of clocks (see notes/agent-testing.md, T081), so the digests
// below pin placements the package itself reproduces.
// ---------------------------------------------------------------------------

void run_backgrounds_build_checks() {
  std::printf("test_backgrounds: the Build chapter of levels 9 through 12\n");

  const Zones landscape = maskLandscapeZones();
  const Zones portrait = maskPortraitZones();
  const Zones orientations[2] = {landscape, portrait};
  const char* orientationNames[2] = {"landscape", "portrait"};
  const std::uint32_t probeTimes[6] = {250, 500, 750, 1000, 1250, 2000};

  // ----- Level 9, Fracture: two stepped diagonal seams in the far
  // margins - twelve steps each of secondary #283044, one step-width wide
  // (margin width / 12 + 1: 17 px landscape, 6 px portrait) and 5 px thick,
  // walking from each margin's outer top corner toward its bottom inner
  // corner - with a 22 px tall offset ledge shifted 12 px toward the canvas
  // center, and one accent tick of #707D96, 2 x 2 px, beside each step on
  // each side of the seam where it fits inside the margin. The ticks sink
  // 3 px/s over their margin span; both staircases hold still.
  {
    const std::uint32_t approvedDigest[2] = {0xDA0337C7u, 0x1C9A587Au};
    const std::uint32_t probeDigests[2][6] = {
        {0x41F01C4Fu, 0xDE296BA5u, 0x052B4B87u, 0xB2D3C7E7u, 0xF26B7A5Eu,
         0xF23183F7u},
        {0x1DEFF28Au, 0xD4BEB958u, 0x0916564Au, 0x9C6C930Au, 0x366868C2u,
         0xEC2E4FC2u}};
    // The first steps of each margin: the seam x, then the ledge x, at the
    // rows the margin's twelfths place them (the portrait fourth row is
    // 319: round-to-even of 802 * 3 / 12, one half).
    const int stepWidth[2] = {17, 6};
    const int stepStride[2] = {16, 5};
    const int firstSeam[2][2][2] = {{{0, 12}, {943, 931}},
                                    {{0, 12}, {538, 526}}};
    const int stepRows[2][4] = {{0, 45, 91, 136}, {119, 186, 253, 319}};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(9), zones, painter, 0);
      // The base fill, then 48 staircase blocks and the 44 ticks whose
      // chip position fits its margin: 92 elements, exactly the record's
      // measured count.
      SC_CHECK(painter.count == 93);
      SC_CHECK(stirDigest(painter) == approvedDigest[orientation]);

      int seams = 0;
      int ledges = 0;
      int ticks = 0;
      int ticksPerMargin[3] = {0, 0, 0};
      int seamSeen[2] = {0, 0};
      for (int fill = 1; fill < painter.count; ++fill) {
        const RecordedFill& element = painter.fills[fill];
        bool inMargin = false;
        int home = -1;
        for (int margin = 0; margin < zones.loudCount; ++margin) {
          const BgRect& m = zones.loud[margin];
          if (element.x >= m.x && element.y >= m.y &&
              element.x + element.w <= m.x + m.w &&
              element.y + element.h <= m.y + m.h) {
            inMargin = true;
            home = margin;
          }
        }
        SC_CHECK(inMargin);  // every fracture block sits whole in a margin
        if (element.w == 2 && element.h == 2) {
          ++ticks;
          SC_CHECK(sameColor(element.color, designOf(9)->accent));
          if (home >= 0) {
            ++ticksPerMargin[home];
          }
        } else {
          SC_CHECK(sameColor(element.color, designOf(9)->secondary));
          SC_CHECK(element.w == stepWidth[orientation]);
          if (element.h == 5) {
            // The literal geometry of the first four steps of each margin:
            // the seam walks one step-width per twelfth, the ledge 12 px
            // toward the canvas center, both on the twelfth rows.
            if (home >= 0 && seamSeen[home] < 4) {
              const int step = seamSeen[home];
              const int direction = home == 0 ? 1 : -1;
              SC_CHECK(element.x ==
                       firstSeam[orientation][home][0] +
                           direction * step * stepStride[orientation]);
              SC_CHECK(element.y == stepRows[orientation][step]);
              SC_CHECK(sameFill(painter.fills[fill + 1],
                                designOf(9)->secondary,
                                firstSeam[orientation][home][1] +
                                    direction * step * stepStride[orientation],
                                stepRows[orientation][step],
                                stepWidth[orientation], 22));
            }
            if (home >= 0) {
              ++seamSeen[home];
            }
            ++seams;
          } else {
            SC_CHECK(element.h == 22);
            ++ledges;
          }
        }
      }
      SC_CHECK(seams == 24);
      SC_CHECK(ledges == 24);
      SC_CHECK(ticks == 44);
      SC_CHECK(seamSeen[0] == 12 && seamSeen[1] == 12);
      SC_CHECK(ticksPerMargin[0] == 22 && ticksPerMargin[1] == 22 &&
               ticksPerMargin[2] == 0);

      // Motion probes: the digests of the sinking tick layer, and the
      // displacement audit between consecutive 250 ms streams - the ticks
      // sink 0.75 px per step, so the still staircases sit inside the
      // audit's window with them. The ticks wrap at their margin span.
      RecordingPainter previous;
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(9), zones, moved, probeTimes[probe]);
        SC_CHECK(moved.count == 93);
        SC_CHECK(stirDigest(moved) == probeDigests[orientation][probe]);
        if (probe > 0 && probeTimes[probe] - probeTimes[probe - 1] == 250) {
          const long long dt = 250;
          int spans[3] = {0, 0, 0};
          for (int margin = 0; margin < zones.loudCount; ++margin) {
            spans[margin] = zones.loud[margin].h;
          }
          SC_CHECK(driftMovedCleanly(previous, moved, 3000, dt, spans,
                                     zones.loudCount));
        }
        previous = moved;
      }

      // The staircases hold still under the sinking ticks: every seam and
      // ledge fill is byte-identical at every probe.
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(9), zones, moved, probeTimes[probe]);
        for (int fill = 1; fill < painter.count; ++fill) {
          if (painter.fills[fill].h == 5 || painter.fills[fill].h == 22) {
            SC_CHECK(sameFill(moved.fills[fill], painter.fills[fill].color,
                              painter.fills[fill].x, painter.fills[fill].y,
                              painter.fills[fill].w, painter.fills[fill].h));
          }
        }
      }

      // The wrap period of the record: one full pass of the margin span
      // at 3 px/s returns the stream - 544 s landscape, 802 s portrait
      // (the side margins the ticks live in).
      RecordingPainter zero;
      paintGrammar(*designOf(9), zones, zero, 0);
      RecordingPainter afterPass;
      paintGrammar(*designOf(9), zones, afterPass,
                   orientation == 0 ? 544000u : 802000u);
      SC_CHECK(sameStream(zero, afterPass));
    }
  }

  // ----- Level 10, Circuit hush: sixteen right-angle paths of secondary
  // #283144, eight per side margin, one per band of margin height / 8
  // starting 8 px into the band - a horizontal run 2 px thick and 24 to
  // 71 px long, a drop 16 to 39 px tall at its right end, a second run 8
  // to 71 px back toward the margin edge, and a closing drop 14 to 35 px
  // tall, so 64 traces in all - each closed with a 4 x 4 px node at the
  // second drop's foot, every third node carrying a 2 x 2 px lit core of
  // accent #6B8498 that swells to 3 x 3 px over the 7 s cycle. The traces
  // hold still; only the cores breathe.
  {
    const std::uint32_t quietDigest[2] = {0x29126919u, 0x47BFFAADu};
    const std::uint32_t swellDigest[2] = {0x967D2DA9u, 0xFAFDD985u};
    // The first path of each margin, as the seeds place it: run x, run
    // length, fall, back, drop2, node x, node y. Index 0 of each margin
    // carries a lit core; index 1 does not.
    const int firstPath[2][2][7] = {
        {{4, 48, 31, 22, 35, 29, 68}, {889, 61, 34, 35, 20, 914, 56}},
        {{6, 48, 31, 22, 35, 31, 187}, {490, 49, 38, 46, 16, 492, 175}}};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(10), zones, painter, 0);
      // Landscape emits every element the grammar draws - 64 traces, 16
      // nodes, 6 cores. Portrait clips two traces whole off its narrow
      // side margins, so its stream holds 84 of the 86.
      SC_CHECK(painter.count == (orientation == 0 ? 87 : 85));
      SC_CHECK(stirDigest(painter) == quietDigest[orientation]);

      int traces = 0;
      int nodes = 0;
      int cores = 0;
      for (int fill = 1; fill < painter.count; ++fill) {
        const RecordedFill& element = painter.fills[fill];
        if (sameColor(element.color, designOf(10)->accent)) {
          ++cores;
          SC_CHECK(element.w == 2 && element.h == 2);
        } else if (element.w == 4 && element.h == 4) {
          ++nodes;
        } else {
          ++traces;
          // Every trace is a 2 px run or a 2 px drop inside the record's
          // length ranges.
          if (element.h == 2) {
            SC_CHECK(element.w >= 8 && element.w <= 71);
          } else {
            SC_CHECK(element.w == 2);
            SC_CHECK(element.h >= 14 && element.h <= 39);
          }
        }
      }
      SC_CHECK(traces == (orientation == 0 ? 64 : 62));
      SC_CHECK(nodes == 16);
      SC_CHECK(cores == 6);

      // The literal geometry of the first path of each margin: the run at
      // its hashed x in the first band, the drop at its right end, the
      // jog back toward the margin edge, the closing drop, and the node
      // with its lit core at the second drop's foot.
      for (int which = 0; which < 2; ++which) {
        const int* path = firstPath[orientation][which];
        // Path 0 of margin 0 emits its traces, node, and core; path 1 of
        // margin 1 follows without a core.
        const int base = 1 + which * 6;
        SC_CHECK(sameFill(painter.fills[base], designOf(10)->secondary,
                          path[0], zones.loud[which].y + 8, path[1], 2));
        SC_CHECK(sameFill(painter.fills[base + 1], designOf(10)->secondary,
                          path[0] + path[1] - 2, zones.loud[which].y + 8, 2,
                          path[2]));
        SC_CHECK(sameFill(painter.fills[base + 2], designOf(10)->secondary,
                          path[0] + path[1] - path[3],
                          zones.loud[which].y + 8 + path[2] - 2, path[3], 2));
        SC_CHECK(sameFill(painter.fills[base + 3], designOf(10)->secondary,
                          path[0] + path[1] - path[3],
                          zones.loud[which].y + 8 + path[2] - 2, 2, path[4]));
        SC_CHECK(sameFill(painter.fills[base + 4], designOf(10)->secondary,
                          path[5], path[6], 4, 4));
        if (which == 0) {
          SC_CHECK(sameFill(painter.fills[base + 5], designOf(10)->accent,
                            path[5], path[6], 2, 2));
        }
      }

      // The pulse: the swell is one through the middle half of the 7 s
      // cycle and zero at its ends, with the exact halves rounded to even
      // as the package rounds them - quiet at 1750 ms and 5250 ms, swelled
      // from 1751 ms to 5249 ms. The traces and nodes hold still; only
      // the six cores grow one pixel and return.
      const std::uint32_t quietTimes[6] = {1, 875, 1750, 5250, 6999, 14000};
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter quiet;
        paintGrammar(*designOf(10), zones, quiet, quietTimes[probe]);
        SC_CHECK(stirDigest(quiet) == quietDigest[orientation]);
      }
      const std::uint32_t swellTimes[4] = {1751, 3000, 4000, 5249};
      for (int probe = 0; probe < 4; ++probe) {
        RecordingPainter swelled;
        paintGrammar(*designOf(10), zones, swelled, swellTimes[probe]);
        SC_CHECK(swelled.count == painter.count);
        SC_CHECK(stirDigest(swelled) == swellDigest[orientation]);
        for (int fill = 1; fill < painter.count; ++fill) {
          if (sameColor(painter.fills[fill].color, designOf(10)->accent)) {
            SC_CHECK(swelled.fills[fill].w == 3);
            SC_CHECK(swelled.fills[fill].h == 3);
            SC_CHECK(swelled.fills[fill].x == painter.fills[fill].x);
            SC_CHECK(swelled.fills[fill].y == painter.fills[fill].y);
          } else {
            SC_CHECK(sameFill(swelled.fills[fill], painter.fills[fill].color,
                              painter.fills[fill].x, painter.fills[fill].y,
                              painter.fills[fill].w, painter.fills[fill].h));
          }
        }
      }

      // The cycle repeats: 7 s of clock return the quiet stream.
      RecordingPainter afterCycle;
      paintGrammar(*designOf(10), zones, afterCycle, 7000);
      SC_CHECK(sameStream(painter, afterCycle));
    }
  }

  // ----- Level 11, Band aurora: three broad stepped ribbons of secondary
  // #283044 rising together, each three overlapping staircase bands - a
  // body of round(height * 0.035) rows (19 landscape, 34 portrait), 40 px
  // steps with an 8 px lift, the drop and phase alternating per layer and
  // ribbon so the edges cascade in opposing contours - plus eight accent
  // ticks of #67758D, 2 x 2 px, riding the same rise inside the loud
  // margins. The whole layer rises 4 px/s, wrapping over the canvas height
  // plus the ribbon's own extent so a ribbon leaves the top edge fully
  // before re-entering below the bottom edge.
  {
    const std::uint32_t approvedDigest[2] = {0x72239B50u, 0x2D9272AEu};
    const std::uint32_t probeDigests[2][6] = {
        {0x3B0C64D6u, 0x6E372E74u, 0x0EE32881u, 0xD36CD73Bu, 0xA7DECF75u,
         0x5CBB1BACu},
        {0x7391C99Fu, 0x0F32F9DCu, 0x7277F7A5u, 0x1BB806E2u, 0xCD17AED3u,
         0xE92D7876u}};
    const int body[2] = {19, 34};
    const int extent[2] = {101, 146};
    // The first staircase segments of the lowest band of ribbon 0 at
    // rest: x, y, width, height - lifts 0, 1, 2, 1, 0 times 8 px, cut by
    // the portrait frame ring where the board covers the middle.
    const int firstBand[2][5][4] = {
        {{0, 87, 40, 19}, {40, 79, 40, 27}, {80, 71, 40, 35},
         {120, 79, 40, 27}, {160, 87, 40, 19}},
        {{0, 154, 40, 34}, {40, 146, 31, 42}, {473, 146, 7, 42},
         {480, 154, 40, 34}, {520, 146, 24, 42}}};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      const int span = zones.canvasHeight + extent[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(11), zones, painter, 0);
      SC_CHECK(painter.count == (orientation == 0 ? 171 : 54));
      SC_CHECK(stirDigest(painter) == approvedDigest[orientation]);

      // Nine staircase bands of the record: each emits one fill per 40 px
      // step the frame does not swallow whole - 162 landscape, 45 portrait
      // - plus the eight riding ticks.
      int segments = 0;
      int ticks = 0;
      for (int fill = 1; fill < painter.count; ++fill) {
        const RecordedFill& element = painter.fills[fill];
        if (sameColor(element.color, designOf(11)->accent)) {
          ++ticks;
          SC_CHECK(element.w == 2 && element.h == 2);
          bool inLoud = false;
          for (int margin = 0; margin < zones.loudCount; ++margin) {
            const BgRect& m = zones.loud[margin];
            inLoud = inLoud || (element.x >= m.x && element.y >= m.y &&
                                element.x + 2 <= m.x + m.w &&
                                element.y + 2 <= m.y + m.h);
          }
          SC_CHECK(inLoud);
        } else {
          ++segments;
          SC_CHECK(element.w <= 40);  // one step, cut at most by the frame
          SC_CHECK(element.h == body[orientation] ||
                   element.h == body[orientation] + 8 ||
                   element.h == body[orientation] + 16);
        }
      }
      SC_CHECK(segments == (orientation == 0 ? 162 : 45));
      SC_CHECK(ticks == 8);

      // The literal staircase of the lowest band at rest: lifts 0, 1, 2,
      // 1, 0 times 8 px over the first five steps.
      for (int step = 0; step < 5; ++step) {
        SC_CHECK(sameFill(painter.fills[1 + step], designOf(11)->secondary,
                          firstBand[orientation][step][0],
                          firstBand[orientation][step][1],
                          firstBand[orientation][step][2],
                          firstBand[orientation][step][3]));
      }

      // Motion probes: the digests of the rising layer, one pixel per
      // 250 ms step of the clock, and the displacement audit over the
      // adjacent probes - the layer rises 1 px per step, so the audit's
      // window holds both it and the frame-cut pieces that hold still.
      RecordingPainter previous;
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(11), zones, moved, probeTimes[probe]);
        SC_CHECK(moved.count == painter.count);
        SC_CHECK(stirDigest(moved) == probeDigests[orientation][probe]);
        if (probe > 0 && probeTimes[probe] - probeTimes[probe - 1] == 250) {
          int spans[2] = {span, span + 2};  // the ribbon and the tick wraps
          SC_CHECK(driftMovedCleanly(previous, moved, -4000, 250, spans, 2));
        }
        previous = moved;
      }

      // The seamless wrap of the record: no quarter second of a whole
      // pass repaints a tenth of the canvas. A ribbon that popped back at
      // its base row would repaint three ribbons at once - over a third
      // of the canvas - where the corrected wrap repaints only the
      // one-pixel rise of the visible bands and the single band crossing
      // an edge. One pass is span / 4 seconds, a quarter second a step.
      int worstChange = 0;
      std::uint32_t worstAtMs = 0;
      PlatePainter before(zones.canvasWidth, zones.canvasHeight);
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(11), zones, before, 0);
      const std::uint32_t passMs =
          static_cast<std::uint32_t>(span) * 250u;
      for (std::uint32_t ms = 250; ms <= passMs; ms += 250) {
        paintGrammar(*designOf(11), zones, plate, ms);
        int changed = 0;
        for (int y = 0; y < zones.canvasHeight; ++y) {
          for (int x = 0; x < zones.canvasWidth; ++x) {
            if (!sameColor(before.at(x, y), plate.at(x, y))) {
              ++changed;
            }
          }
        }
        if (changed > worstChange) {
          worstChange = changed;
          worstAtMs = ms;
        }
        paintGrammar(*designOf(11), zones, before, ms);
      }
      const long canvasArea =
          static_cast<long>(zones.canvasWidth) * zones.canvasHeight;
      SC_CHECK(static_cast<long>(worstChange) * 10 < canvasArea);
      std::printf("  %s level 11: worst quarter-second repaint %d px at "
                  "%u ms of the pass\n",
                  orientationNames[orientation], worstChange, worstAtMs);

      // The wrap periods of the record: the ribbons pass every span / 4
      // seconds and the ticks every (height + 2) / 4, so the whole stream
      // returns at their least common multiple - 29347.5 s landscape,
      // 132996.5 s portrait.
      RecordingPainter zero;
      paintGrammar(*designOf(11), zones, zero, 0);
      RecordingPainter afterPass;
      paintGrammar(*designOf(11), zones, afterPass,
                   orientation == 0 ? 29347500u : 132996500u);
      SC_CHECK(sameStream(zero, afterPass));

      // A ribbon leaves the top edge fully before it re-enters: past the
      // first ribbon's exit the canvas still carries the other two, so
      // the layer never empties and never pops in whole.
      RecordingPainter midPass;
      paintGrammar(*designOf(11), zones, midPass,
                   static_cast<std::uint32_t>(span / 8));
      SC_CHECK(midPass.count > 1 + 3 * 5);  // ribbons still on the canvas
    }
  }

  // ----- Level 12, Long trails: seventy-two dim square stars on the 20 px
  // cell grid the seed 0xC12 ranks - 1 x 1 px in 55 percent, 2 x 2 in 45,
  // secondary #293144 or accent #76849B in equal share - and twelve
  // staircase meteor trails alternating between the loud margins, each six
  // steps of 3 x 1 px secondary stepping 4 px left and 2 px down, its head
  // a 2 x 2 px accent square above the first step, dropped when it would
  // leave its margin. Stars and trails drift as one layer, 2.4 px/s left
  // and 3.2 px/s down.
  {
    const std::uint32_t approvedDigest[2] = {0x1991EE1Bu, 0xF2C48FD4u};
    const std::uint32_t probeDigests[2][6] = {
        {0x17CDE584u, 0x22365EE5u, 0x3DF11556u, 0x8F723845u, 0xC5BF71D4u,
         0xA5671181u},
        {0xE6349DCBu, 0xE3789A0Au, 0xB28E90BFu, 0xBF741CD4u, 0x79A9F3E3u,
         0xFF35DAB9u}};
    // The head crossings of the margin edges breathe the fill count.
    const int probeCounts[2][6] = {{157, 157, 157, 156, 157, 157},
                                   {157, 155, 157, 155, 157, 155}};
    const int starsPerMargin[2][3] = {{36, 36, 0}, {30, 31, 11}};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(12), zones, painter, 0);
      // The base fill, then 72 stars, 72 trail steps, and 12 heads: 156
      // elements, exactly the record's measured count.
      SC_CHECK(painter.count == 157);
      SC_CHECK(stirDigest(painter) == approvedDigest[orientation]);

      int stars = 0;
      int steps = 0;
      int heads = 0;
      int starsPerMarginSeen[3] = {0, 0, 0};
      int onePixel = 0;
      for (int fill = 1; fill < painter.count; ++fill) {
        const RecordedFill& element = painter.fills[fill];
        if (fill <= 72) {
          ++stars;
          SC_CHECK(element.w == element.h);
          SC_CHECK(element.w == 1 || element.w == 2);
          if (element.w == 1) {
            ++onePixel;
          }
          SC_CHECK(sameColor(element.color, designOf(12)->secondary) ||
                   sameColor(element.color, designOf(12)->accent));
          for (int margin = 0; margin < zones.loudCount; ++margin) {
            const BgRect& m = zones.loud[margin];
            if (element.x >= m.x && element.y >= m.y &&
                element.x + element.w <= m.x + m.w &&
                element.y + element.h <= m.y + m.h) {
              ++starsPerMarginSeen[margin];
            }
          }
        } else if (element.h == 1) {
          ++steps;
          SC_CHECK(element.w <= 3);  // 3 px, clipped at a canvas edge
          SC_CHECK(sameColor(element.color, designOf(12)->secondary));
        } else {
          ++heads;
          SC_CHECK(element.w == 2 && element.h == 2);
          SC_CHECK(sameColor(element.color, designOf(12)->accent));
        }
      }
      SC_CHECK(stars == 72);
      SC_CHECK(steps == 72);
      SC_CHECK(heads == 12);
      SC_CHECK(onePixel == 40);
      for (int margin = 0; margin < 3; ++margin) {
        SC_CHECK(starsPerMarginSeen[margin] ==
                 starsPerMargin[orientation][margin]);
      }

      // A trail is a staircase whose steps sit 4 px left and 2 px below
      // the one before, wrapping inside its margin, closed by a head
      // above its first step. A step the canvas edge clips shows narrower
      // than its 3 px; its neighbors carry the staircase.
      int trailIndex = 0;
      for (int fill = 73; fill < painter.count; ++trailIndex) {
        const int firstX = painter.fills[fill].x;
        const int firstY = painter.fills[fill].y;
        int segment = 0;
        int prevX = firstX;
        int prevY = firstY;
        int prevWhole = painter.fills[fill].w == 3;
        while (fill < painter.count && painter.fills[fill].h == 1) {
          if (segment > 0) {
            const int dx = painter.fills[fill].x - prevX;
            const int dy = painter.fills[fill].y - prevY;
            // The next row, or the same row after the margin wrap.
            SC_CHECK(dy == 2 || dy + zones.loud[0].h == 2 ||
                     dy + zones.loud[1].h == 2 ||
                     (zones.loudCount > 2 && dy + zones.loud[2].h == 2));
            const bool whole = painter.fills[fill].w == 3;
            if (whole && prevWhole) {
              SC_CHECK(dx == -4 || dx - zones.loud[0].w == -4 ||
                       dx - zones.loud[1].w == -4 ||
                       (zones.loudCount > 2 && dx - zones.loud[2].w == -4));
            }
            prevWhole = whole;
          }
          prevX = painter.fills[fill].x;
          prevY = painter.fills[fill].y;
          ++segment;
          ++fill;
        }
        SC_CHECK(segment >= 1);  // at least one step of every trail shows
        if (fill < painter.count &&
            sameColor(painter.fills[fill].color, designOf(12)->accent)) {
          const int dx = painter.fills[fill].x - firstX;
          const int dy = painter.fills[fill].y - firstY;
          SC_CHECK(dx >= -2 && dx <= 1);
          SC_CHECK(dy >= -4 && dy <= -1);
          ++fill;
        }
      }
      SC_CHECK(trailIndex == 12);

      // Motion probes: the exact digests and fill counts.
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(12), zones, moved, probeTimes[probe]);
        SC_CHECK(moved.count == probeCounts[orientation][probe]);
        SC_CHECK(stirDigest(moved) == probeDigests[orientation][probe]);
      }

      // The motion is one shared diagonal: a quarter second of clock
      // repaints only the pixels the layer vacated and the pixels it
      // entered - every element is 3 px or less on an axis, so the whole
      // layer's footprint bounds the change. A second layer moving, or a
      // jump, repaints a multiple of it.
      const std::uint32_t stepPairs[3][2] = {{0, 250}, {1250, 1500},
                                             {240000, 240250}};
      for (int pair = 0; pair < 3; ++pair) {
        PlatePainter early(zones.canvasWidth, zones.canvasHeight);
        paintGrammar(*designOf(12), zones, early, stepPairs[pair][0]);
        PlatePainter late(zones.canvasWidth, zones.canvasHeight);
        paintGrammar(*designOf(12), zones, late, stepPairs[pair][1]);
        int changed = 0;
        for (int y = 0; y < zones.canvasHeight; ++y) {
          for (int x = 0; x < zones.canvasWidth; ++x) {
            if (!sameColor(early.at(x, y), late.at(x, y))) {
              ++changed;
            }
          }
        }
        // 156 elements of at most 2 x 2 px each, vacated and entered.
        SC_CHECK(changed <= 156 * 2 * 4);
      }

      // The wrap period of the record: the landscape layer returns when
      // both margin axes have swept their spans whole - 192 px at 2.4 px/s
      // and 544 px at 3.2 px/s, 1360 s. Portrait's three margin pairs
      // share no period inside the 32-bit clock, so its probes above pin
      // the motion instead.
      if (orientation == 0) {
        RecordingPainter zero;
        paintGrammar(*designOf(12), zones, zero, 0);
        RecordingPainter afterPass;
        paintGrammar(*designOf(12), zones, afterPass, 1360000u);
        SC_CHECK(sameStream(zero, afterPass));
      }
    }
  }

  // ----- The contract of the chapter as a whole, both orientations: the
  // element counts, the palette placement and coverage at rest and
  // mid-motion, the budget class, one moving layer, and the stream a pure
  // function of the clock far along it and across its 32-bit wrap.
  const int elementCeilings[2][4] = {{92, 86, 180, 156},
                                     {95, 84, 98, 158}};
  // One full pass of each design's motion per orientation, sampled at its
  // motion quantum: the 250 ms pixel of the 4 px/s layers, finer for the
  // sub-pixel carries of the 3 and 4 px/s designs. Portrait level 12 has
  // no exact return - its three margin pairs share no period inside the
  // 32-bit clock - so its scan covers the landscape pass.
  const std::uint32_t passMs[2][4] = {{544000u, 7000u, 161250u, 1360000u},
                                      {802000u, 7000u, 276500u, 1360000u}};
  const std::uint32_t passStep[4] = {50u, 250u, 250u, 50u};
  for (int orientation = 0; orientation < 2; ++orientation) {
    const Zones& zones = orientations[orientation];
    const long canvasArea =
        static_cast<long>(zones.canvasWidth) * zones.canvasHeight;
    for (int level = 9; level <= 12; ++level) {
      const Design& design = *designOf(level);

      // Palette placement and coverage, at rest and mid-motion.
      const std::uint32_t probes[4] = {0, 1300, 4100, 123457};
      for (int probe = 0; probe < 4; ++probe) {
        PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
        paintGrammar(design, zones, plate, probes[probe]);
        long accentPixels = 0;
        for (int y = 0; y < zones.canvasHeight; ++y) {
          for (int x = 0; x < zones.canvasWidth; ++x) {
            const BgColor color = plate.at(x, y);
            SC_CHECK(withinCap(color, kAccentCap));
            if (!withinCap(color, kSecondaryCap)) {
              ++accentPixels;
              bool inLoud = false;
              for (int margin = 0; margin < zones.loudCount; ++margin) {
                const BgRect& m = zones.loud[margin];
                inLoud = inLoud || (x >= m.x && x < m.x + m.w && y >= m.y &&
                                    y < m.y + m.h);
              }
              SC_CHECK(inLoud);
            }
          }
        }
        SC_CHECK(accentPixels * 100 <= canvasArea * 2);
      }

      // The budget class of the record holds the emitted elements through
      // a whole pass of the motion, not only at rest: as the clock runs
      // the frame ring releases band steps and the small chips cross
      // their margin edges, so the busiest clock emits more than rest -
      // the portrait ribbons of level 11 close to double.
      int busiest = 0;
      for (std::uint32_t ms = 0; ms <= passMs[orientation][level - 9];
           ms += passStep[level - 9]) {
        RecordingPainter pass;
        paintGrammar(design, zones, pass, ms);
        if (pass.count - 1 > busiest) {
          busiest = pass.count - 1;
        }
      }
      SC_CHECK(busiest > 0);
      SC_CHECK(busiest <= elementCeilings[orientation][level - 9]);
      SC_CHECK(elementCeilings[orientation][level - 9] <= kMaxSmallElements);

      // One moving layer: the motion spec of the catalog, and a stream
      // that changes only through that layer - the same two shades at
      // every phase, at rest and far along the clock including its
      // 32-bit wrap.
      SC_CHECK(design.motion.kind != MotionKind::None);
      SC_CHECK(design.motion.speedMilliPxPerS <= 10000);
      if (design.motion.kind == MotionKind::Pulse) {
        SC_CHECK(design.motion.cycleMs >= 4000);
      }
      const std::uint32_t late[4] = {70000, 123457, 4294967295u,
                                     4294966300u};
      for (int probe = 0; probe < 4; ++probe) {
        RecordingPainter later;
        paintGrammar(design, zones, later, late[probe]);
        RecordingPainter again;
        paintGrammar(design, zones, again, late[probe]);
        SC_CHECK(sameStream(later, again));
        for (int fill = 1; fill < later.count && fill < kRecordCapacity;
             ++fill) {
          SC_CHECK(sameColor(later.fills[fill].color, design.secondary) ||
                   sameColor(later.fills[fill].color, design.accent));
        }
      }
    }
  }

  // ----- The chapter activity, read the way the design package's own arc
  // check reads it: over the landscape plates, where the board covers a
  // third of the width. There the Build chapter carries at least the
  // review-scale marks of Stir, more elements per design, and a faster
  // peak speed - the three measures of the arc. Portrait's marks dip a
  // hair below Stir's (34 against 36 per 1000 at rest) because its board
  // covers 74 percent of the width and hides two margins' worth of marks,
  // which is why the package's rule reads the landscape plates.
  {
    const Zones& zones = landscape;
    long stirMarks = 0;
    long stirElements = 0;
    for (int level = 5; level <= 8; ++level) {
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(level), zones, plate, 0);
      stirMarks += eighthMarksPer1000(plate);
      RecordingPainter painter;
      paintGrammar(*designOf(level), zones, painter, 0);
      stirElements += painter.count - 1;
    }
    long buildMarks = 0;
    long buildElements = 0;
    for (int level = 9; level <= 12; ++level) {
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(level), zones, plate, 0);
      buildMarks += eighthMarksPer1000(plate);
      RecordingPainter painter;
      paintGrammar(*designOf(level), zones, painter, 0);
      buildElements += painter.count - 1;
    }
    SC_CHECK(buildMarks >= stirMarks);
    SC_CHECK(buildElements > stirElements);
    SC_CHECK(designOf(12)->motion.speedMilliPxPerS >
             designOf(8)->motion.speedMilliPxPerS);
    std::printf("  landscape chapter means: marks per 1000 Stir %ld, Build "
                "%ld; elements Stir %ld, Build %ld\n",
                stirMarks / 4, buildMarks / 4, stirElements / 4,
                buildElements / 4);
  }

  // ----- Distinctness at the one-eighth review scale, across the chapter
  // boundary and inside the chapter: adjacent levels differ in a visible
  // step of six of 255 gray in at least one percent of the samples, half
  // that in portrait where the board covers 74 percent of the width.
  for (int orientation = 0; orientation < 2; ++orientation) {
    const Zones& zones = orientations[orientation];
    static int thumbs[5][kEighthSamples];
    int widths[5] = {0, 0, 0, 0, 0};
    int heights[5] = {0, 0, 0, 0, 0};
    for (int level = 8; level <= 12; ++level) {
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(level), zones, plate, 0);
      eighthScale(plate, thumbs[level - 8], &widths[level - 8],
                  &heights[level - 8]);
    }
    const int floorDivisor = orientation == 0 ? 100 : 200;
    for (int level = 8; level < 12; ++level) {
      const int samples = widths[0] * heights[0];
      int differ = 0;
      for (int index = 0; index < samples; ++index) {
        const int delta = thumbs[level - 8][index] - thumbs[level - 7][index];
        if (delta >= 6 || delta <= -6) {
          ++differ;
        }
      }
      SC_CHECK(differ >= samples / floorDivisor);
      std::printf("  %s %d vs %d: %d of %d samples differ (%.2f%%)\n",
                  orientationNames[orientation], level, level + 1, differ,
                  samples, 100.0 * differ / samples);
    }
  }
}

// ---------------------------------------------------------------------------
// The Pressure chapter of task T082: levels 13 through 16, the set
// tightening - faster drift, denser marks, pulses. Every check runs on
// both orientations against the retained records of
// docs/backgrounds/level-13.md through level-16.md. The streams were
// verified against the design package's own grammars pixel for pixel over
// 69336 (level, orientation, clock) plates - dense sweeps, the rounding
// and pulse edges, the wrap spans, and random clocks across the full
// 32-bit range (see notes/agent-testing.md, T082) - so the digests below
// pin placements the package itself reproduces.
// ---------------------------------------------------------------------------

void run_backgrounds_pressure_checks() {
  std::printf(
      "test_backgrounds: the Pressure chapter of levels 13 through 16\n");

  const Zones landscape = maskLandscapeZones();
  const Zones portrait = maskPortraitZones();
  const Zones orientations[2] = {landscape, portrait};
  const char* orientationNames[2] = {"landscape", "portrait"};
  const std::uint32_t probeTimes[6] = {250, 500, 750, 1000, 1250, 2000};

  // ----- Level 13, Deep grid: twenty-two staircase grid lines receding
  // toward a mid-height horizon - twelve 1 px rules of secondary #293145
  // at the mid row +/- the offsets 6, 11, 18, 27, 38 and 51 px, one inside
  // every tall loud margin - plus ten 2 px vertical guides, five in each
  // of the first two margins, each in four quarter-height segments
  // stepping 1 px toward the canvas center, closed by a 2 x 2 px accent
  // node of #79869B at its foot. The rules scroll down 5 px/s over their
  // margin span as one rigid set; the guides and nodes hold still.
  {
    const std::uint32_t approvedDigest[2] = {0x9F873E80u, 0xEB598B7Fu};
    const std::uint32_t probeDigests[2][6] = {
        {0xFF666230u, 0x29100652u, 0x13148762u, 0xB8F8D36Au, 0x093FBE8Au,
         0x64AA9182u},
        {0x2FD69C0Bu, 0xE1B44037u, 0x976E4F67u, 0x405FD889u, 0x0F643485u,
         0x116E6945u}};
    // The twelve rule rows of the horizon set, in emission order (away
    // 6, 11, 18, 27, 38, 51, sign -1 then +1), and the first guide's
    // staircase: its four segments step 1 px toward the canvas center.
    const int ruleRows[2][12] = {
        {266, 278, 261, 283, 254, 290, 245, 299, 234, 310, 221, 323},
        {474, 486, 469, 491, 462, 498, 453, 507, 442, 518, 429, 531}};
    const int guideX[2][2] = {{6, 41}, {6, 15}};  // guides 1 and 2, margin 1
    const int guideTop[2] = {0, 119};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(13), zones, painter, 0);
      // The base fill, then 24 rules, 40 guide segments, and 10 nodes:
      // 74 elements at every clock, exactly the record's counts.
      SC_CHECK(painter.count == 75);
      SC_CHECK(stirDigest(painter) == approvedDigest[orientation]);

      int rules = 0;
      int guides = 0;
      int nodes = 0;
      for (int fill = 1; fill < painter.count; ++fill) {
        const RecordedFill& element = painter.fills[fill];
        bool inMargin = false;
        for (int margin = 0; margin < zones.loudCount; ++margin) {
          const BgRect& m = zones.loud[margin];
          inMargin = inMargin || (element.x >= m.x && element.y >= m.y &&
                                  element.x + element.w <= m.x + m.w &&
                                  element.y + element.h <= m.y + m.h);
        }
        SC_CHECK(inMargin);  // the grid lives inside the loud margins
        if (element.h == 1) {
          ++rules;
          SC_CHECK(sameColor(element.color, designOf(13)->secondary));
          SC_CHECK(element.w == 192 || element.w == 59);
        } else if (element.w == 2 && element.h == 2) {
          ++nodes;
          SC_CHECK(sameColor(element.color, designOf(13)->accent));
        } else {
          ++guides;
          SC_CHECK(sameColor(element.color, designOf(13)->secondary));
          SC_CHECK(element.w == 2);
          SC_CHECK(element.h == zones.loud[0].h / 4);
        }
      }
      SC_CHECK(rules == 24);
      SC_CHECK(guides == 40);
      SC_CHECK(nodes == 10);

      // The literal geometry: the first twelve rules of margin 0 sit at
      // the horizon rows, each before its margin-1 twin; the first guide
      // of margin 0 is the staircase x, x+1, x+2, x+3 over the margin
      // quarters, its node at the foot.
      for (int rule = 0; rule < 12; ++rule) {
        SC_CHECK(painter.fills[1 + 2 * rule].y ==
                 ruleRows[orientation][rule]);
        SC_CHECK(painter.fills[2 + 2 * rule].y ==
                 ruleRows[orientation][rule]);
      }
      const int quarter = zones.loud[0].h / 4;
      for (int step = 0; step < 4; ++step) {
        SC_CHECK(sameFill(painter.fills[25 + step], designOf(13)->secondary,
                          guideX[orientation][0] + step,
                          guideTop[orientation] + step * quarter, 2, quarter));
      }
      SC_CHECK(sameFill(painter.fills[29], designOf(13)->accent,
                        guideX[orientation][0],
                        guideTop[orientation] + zones.loud[0].h - 6, 2, 2));
      SC_CHECK(sameFill(painter.fills[30], designOf(13)->secondary,
                        guideX[orientation][1], guideTop[orientation], 2,
                        quarter));

      // Motion probes: the digests of the scrolling rule set, and the
      // displacement audit over adjacent probes - the rules descend
      // 1.25 px per 250 ms step, so the still guides and nodes sit
      // inside the audit's window with them.
      RecordingPainter previous;
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(13), zones, moved, probeTimes[probe]);
        SC_CHECK(moved.count == 75);
        SC_CHECK(stirDigest(moved) == probeDigests[orientation][probe]);
        if (probe > 0 && probeTimes[probe] - probeTimes[probe - 1] == 250) {
          const int spans[1] = {zones.loud[0].h};
          SC_CHECK(driftMovedCleanly(previous, moved, 5000, 250, spans, 1));
        }
        previous = moved;
      }

      // The guides and nodes hold still under the scrolling rules: every
      // fill from 25 on is byte-identical at every probe.
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(13), zones, moved, probeTimes[probe]);
        for (int fill = 25; fill < painter.count; ++fill) {
          SC_CHECK(sameFill(moved.fills[fill], painter.fills[fill].color,
                            painter.fills[fill].x, painter.fills[fill].y,
                            painter.fills[fill].w, painter.fills[fill].h));
        }
      }

      // The wrap period of the record: one full pass of the margin span
      // at 5 px/s returns the stream - 108.8 s landscape, 160.4 s
      // portrait.
      RecordingPainter zero;
      paintGrammar(*designOf(13), zones, zero, 0);
      RecordingPainter afterPass;
      paintGrammar(*designOf(13), zones, afterPass,
                   orientation == 0 ? 108800u : 160400u);
      SC_CHECK(sameStream(zero, afterPass));
    }
  }

  // ----- Level 14, Fine snow: exactly 160 tiny square particles - the
  // ranked cells of the 20 px grid of the seed 0xE14, 1 x 1 px in 55
  // percent and 2 x 2 in the rest, secondary #293145 or accent #7A879E in
  // equal share - falling 6 px/s over their margin spans, beneath three
  // static full-width bands of the secondary, 3.5 percent of the canvas
  // height tall, at 30, 55 and 80 percent height. The bands paint after
  // the field and split at the frame ring, exactly the package's order.
  {
    const std::uint32_t approvedDigest[2] = {0xE0F9D977u, 0xD5C1423Fu};
    const std::uint32_t probeDigests[2][6] = {
        {0x00AF1334u, 0xE82D6C8Cu, 0x5D319A31u, 0x5848B41Cu, 0x0BC2C38Du,
         0x5C3155C3u},
        {0x03AD60F9u, 0x7A0A1B9Au, 0x84311B4Du, 0x5E44E7E3u, 0x622163BDu,
         0xEE194623u}};
    // The head crossings of the margin edges breathe the fill count.
    const int probeCounts[2][6] = {{167, 167, 167, 167, 167, 167},
                                   {167, 167, 169, 167, 167, 167}};
    const int onePixel[2] = {87, 89};
    const int bright[2] = {82, 78};
    const int flakesPerMargin[2][3] = {{77, 83, 0}, {68, 70, 22}};
    // The three bands, each cut by the frame ring into its two sides.
    const int bandRow[2][3] = {{163, 299, 435}, {288, 528, 768}};
    const int bandHeight[2] = {19, 34};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(14), zones, painter, 0);
      // The base fill, then 160 flakes and the frame-split band pieces:
      // 166 elements landscape and portrait at rest.
      SC_CHECK(painter.count == 167);
      SC_CHECK(stirDigest(painter) == approvedDigest[orientation]);

      int one = 0;
      int brightCount = 0;
      int flakes = 0;
      int flakesPerMarginSeen[3] = {0, 0, 0};
      for (int fill = 1; fill <= 160; ++fill) {
        const RecordedFill& element = painter.fills[fill];
        ++flakes;
        SC_CHECK(element.w == element.h);
        SC_CHECK(element.w == 1 || element.w == 2);
        if (element.w == 1) {
          ++one;
        }
        SC_CHECK(sameColor(element.color, designOf(14)->secondary) ||
                 sameColor(element.color, designOf(14)->accent));
        if (sameColor(element.color, designOf(14)->accent)) {
          ++brightCount;
        }
        for (int margin = 0; margin < zones.loudCount; ++margin) {
          const BgRect& m = zones.loud[margin];
          if (element.x >= m.x && element.y >= m.y &&
              element.x + element.w <= m.x + m.w &&
              element.y + element.h <= m.y + m.h) {
            ++flakesPerMarginSeen[margin];
          }
        }
      }
      SC_CHECK(flakes == 160);
      SC_CHECK(one == onePixel[orientation]);
      SC_CHECK(brightCount == bright[orientation]);
      for (int margin = 0; margin < 3; ++margin) {
        SC_CHECK(flakesPerMarginSeen[margin] ==
                 flakesPerMargin[orientation][margin]);
      }

      // The bands paint after the field, each split by the frame ring
      // into its two sides: six pieces in both frames.
      const int bandPieces = 6;
      for (int band = 0; band < 3; ++band) {
        const int row = bandRow[orientation][band];
        const int piece = 161 + band * 2;
        SC_CHECK(sameFill(painter.fills[piece], designOf(14)->secondary, 0,
                          row, zones.frame.x, bandHeight[orientation]));
        SC_CHECK(sameFill(painter.fills[piece + 1], designOf(14)->secondary,
                          zones.frame.x + zones.frame.w, row,
                          zones.canvasWidth - zones.frame.x - zones.frame.w,
                          bandHeight[orientation]));
      }
      SC_CHECK(painter.count == 160 + bandPieces + 1);

      // Motion probes: the exact digests and fill counts. The counts
      // breathe where an accent flake straddles a margin edge and the
      // clip splits it in two, so a fill-by-fill audit cannot track the
      // stream; the digests pin it exactly instead.
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(14), zones, moved, probeTimes[probe]);
        SC_CHECK(moved.count == probeCounts[orientation][probe]);
        SC_CHECK(stirDigest(moved) == probeDigests[orientation][probe]);
      }

      // The motion is one falling field: a quarter second of clock
      // repaints only the pixels the flakes vacated and the pixels they
      // entered - every flake is 2 x 2 px or less, so the whole field's
      // footprint bounds the change and the static bands add nothing. A
      // second layer moving, or a jump, repaints a multiple of it.
      const std::uint32_t stepPairs[3][2] = {{0, 250}, {1250, 1500},
                                             {240000, 240250}};
      for (int pair = 0; pair < 3; ++pair) {
        PlatePainter early(zones.canvasWidth, zones.canvasHeight);
        paintGrammar(*designOf(14), zones, early, stepPairs[pair][0]);
        PlatePainter late(zones.canvasWidth, zones.canvasHeight);
        paintGrammar(*designOf(14), zones, late, stepPairs[pair][1]);
        int changed = 0;
        for (int y = 0; y < zones.canvasHeight; ++y) {
          for (int x = 0; x < zones.canvasWidth; ++x) {
            if (!sameColor(early.at(x, y), late.at(x, y))) {
              ++changed;
            }
          }
        }
        // 160 flakes of at most 2 x 2 px each, vacated and entered, plus
        // the split pieces of a straddling flake.
        SC_CHECK(changed <= 160 * 2 * 4 + 32);
      }

      // The bands hold still under the falling field.
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(14), zones, moved, probeTimes[probe]);
        if (moved.count != painter.count) {
          continue;  // a crossing head breathes the count; the bands stay
        }
        for (int fill = 161; fill < painter.count; ++fill) {
          SC_CHECK(sameFill(moved.fills[fill], painter.fills[fill].color,
                            painter.fills[fill].x, painter.fills[fill].y,
                            painter.fills[fill].w, painter.fills[fill].h));
        }
      }

      // The wrap periods of the record: the landscape field returns when
      // both margins have swept their span whole three times over
      // (6 px/s over 544 px, 272 s); the portrait field returns when the
      // side spans and the bottom strip have both swept whole
      // (lcm(802, 39) px at 6 px/s, 5213 s).
      RecordingPainter zero;
      paintGrammar(*designOf(14), zones, zero, 0);
      RecordingPainter afterPass;
      paintGrammar(*designOf(14), zones, afterPass,
                   orientation == 0 ? 272000u : 5213000u);
      SC_CHECK(sameStream(zero, afterPass));
    }
  }

  // ----- Level 15, Undertow: seven interlocking stepped wave bands of
  // secondary #2A3146 sliding left as one layer - bodies of 38 rows
  // landscape and 67 portrait (round(height * 0.07)) at base rows
  // round(height * (0.28 + 0.09 i)), staircases of 20 px steps with a
  // 6 px lift, dropped 3 px per index, alternate bands carrying the
  // mirrored phase - with two foam ticks of accent #74839B, 2 x 1 px, at
  // hashed positions around each band edge inside every loud margin. The
  // foam holds its margin slots while the layer moves; the 80 px
  // staircase period repeats every 13.3 s, and the whole stream returns
  // every 120 s when the offset has swept the period whole three times.
  {
    const std::uint32_t approvedDigest[2] = {0x49D37EA6u, 0xD145F6BFu};
    const std::uint32_t probeDigests[2][6] = {
        {0x527659F9u, 0x1A52C726u, 0xF87A37F4u, 0xEEBEF387u, 0x35A61823u,
         0xFF9C3BD3u},
        {0xB87701BDu, 0xDB2B444Au, 0xEB80BF13u, 0x5EC76371u, 0xEDD85375u,
         0xB47668D5u}};
    const int probeCounts[2][6] = {{288, 288, 281, 281, 281, 281},
                                   {95, 95, 95, 95, 88, 95}};
    const int foamCount[2] = {28, 31};
    const int bodyHeight[2] = {38, 67};
    // The base rows of the seven bands.
    const int bandRows[2][7] = {{152, 201, 250, 299, 348, 397, 446},
                                {269, 355, 442, 528, 614, 701, 787}};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(15), zones, painter, 0);
      SC_CHECK(painter.count == (orientation == 0 ? 281 : 95));
      SC_CHECK(stirDigest(painter) == approvedDigest[orientation]);

      // The bands: one fill per 20 px step the frame ring spares, every
      // segment a body of the record height plus its lift of 0, 1 or 2
      // times 6 px; the mirrored phases interleave the three lifts. The
      // foam: 2 x 1 px accent ticks inside the loud margins.
      int segments = 0;
      int foam = 0;
      int heights[3] = {0, 0, 0};
      for (int fill = 1; fill < painter.count; ++fill) {
        const RecordedFill& element = painter.fills[fill];
        if (sameColor(element.color, designOf(15)->accent)) {
          ++foam;
          SC_CHECK(element.w == 2 && element.h == 1);
          bool inLoud = false;
          for (int margin = 0; margin < zones.loudCount; ++margin) {
            const BgRect& m = zones.loud[margin];
            inLoud = inLoud || (element.x >= m.x && element.y >= m.y &&
                                element.x + 2 <= m.x + m.w &&
                                element.y + 1 <= m.y + m.h);
          }
          SC_CHECK(inLoud);
        } else {
          ++segments;
          SC_CHECK(sameColor(element.color, designOf(15)->secondary));
          SC_CHECK(element.w <= 20);  // one step, cut at most by the canvas
          SC_CHECK(element.h == bodyHeight[orientation] ||
                   element.h == bodyHeight[orientation] + 6 ||
                   element.h == bodyHeight[orientation] + 12);
          ++heights[(element.h - bodyHeight[orientation]) / 6];
        }
      }
      SC_CHECK(foam == foamCount[orientation]);
      // The lift distribution of the un-cut staircase: one quarter at
      // the crest, half at the shoulders, one quarter at the rest.
      SC_CHECK(heights[0] > 0 && heights[1] > heights[0] && heights[2] > 0);

      // The literal staircase of the first band at rest: lifts 0, 1, 2,
      // 1, 0 times 6 px over the first five steps, and the second band's
      // mirrored contour, dropped 3 px.
      const int body = bodyHeight[orientation];
      SC_CHECK(sameFill(painter.fills[1], designOf(15)->secondary, 0,
                        bandRows[orientation][0], 20, body));
      SC_CHECK(sameFill(painter.fills[2], designOf(15)->secondary, 20,
                        bandRows[orientation][0] - 6, 20, body + 6));
      SC_CHECK(sameFill(painter.fills[3], designOf(15)->secondary, 40,
                        bandRows[orientation][0] - 12, 20, body + 12));
      if (orientation == 0) {
        // Landscape: the fourth and fifth steps are whole. Portrait cuts
        // them at its frame ring, x 71, into an 11 px and a 7 px piece.
        SC_CHECK(sameFill(painter.fills[4], designOf(15)->secondary, 60,
                          bandRows[orientation][0] - 6, 20, body + 6));
        SC_CHECK(sameFill(painter.fills[5], designOf(15)->secondary, 80,
                          bandRows[orientation][0], 20, body));
      } else {
        SC_CHECK(sameFill(painter.fills[4], designOf(15)->secondary, 60,
                          bandRows[orientation][0] - 6, 11, body + 6));
        SC_CHECK(sameFill(painter.fills[5], designOf(15)->secondary, 473,
                          bandRows[orientation][0] - 6, 7, body + 6));
      }

      // Motion probes: the exact digests and fill counts - the frame
      // ring releases and recaptures steps as the phase crosses it.
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(15), zones, moved, probeTimes[probe]);
        SC_CHECK(moved.count == probeCounts[orientation][probe]);
        SC_CHECK(stirDigest(moved) == probeDigests[orientation][probe]);
      }

      // One moving layer: between the 250 ms and 500 ms probes the
      // offset advances exactly one pixel left (round(6 * 0.25) - 1 px)
      // inside the same 80 px window, so every band segment of the equal
      // streams moved one pixel left together - the two contour phases
      // share one velocity, never two layers - and every foam tick held
      // its slot byte for byte. The pieces the frame ring and the canvas
      // edges re-cut keep their anchored edge and breathe their width.
      {
        RecordingPainter early;
        paintGrammar(*designOf(15), zones, early, 250);
        RecordingPainter late;
        paintGrammar(*designOf(15), zones, late, 500);
        SC_CHECK(early.count == late.count);
        for (int fill = 1; fill < early.count; ++fill) {
          SC_CHECK(late.fills[fill].y == early.fills[fill].y);
          SC_CHECK(late.fills[fill].h == early.fills[fill].h);
          if (sameColor(early.fills[fill].color, designOf(15)->accent)) {
            SC_CHECK(sameFill(late.fills[fill], early.fills[fill].color,
                              early.fills[fill].x, early.fills[fill].y,
                              early.fills[fill].w, early.fills[fill].h));
            continue;  // the foam holds its slot
          }
          if (late.fills[fill].w == early.fills[fill].w) {
            SC_CHECK(late.fills[fill].x == early.fills[fill].x - 1);
          } else {
            // A re-cut piece: one edge anchored at the frame ring or a
            // canvas edge, so the other edge moved the shared pixel.
            const bool leftMoved =
                late.fills[fill].x == early.fills[fill].x - 1;
            const bool rightMoved = late.fills[fill].x + late.fills[fill].w ==
                                    early.fills[fill].x + early.fills[fill].w -
                                        1;
            SC_CHECK(leftMoved || rightMoved);
            SC_CHECK(std::abs(late.fills[fill].w - early.fills[fill].w) == 1);
          }
        }
      }

      // The whole stream returns when the leftward 6 px/s slide has
      // swept the 80 px period whole three times: 120 s, both frames.
      RecordingPainter zero;
      paintGrammar(*designOf(15), zones, zero, 0);
      RecordingPainter afterPass;
      paintGrammar(*designOf(15), zones, afterPass, 120000u);
      SC_CHECK(sameStream(zero, afterPass));
    }
  }

  // ----- Level 16, Slow pulse: nine nested broken rectangular contours
  // of secondary #2A3146, 2 px thick, inset 10 + 26 i px plus the swell,
  // each of the four bars split once by a 24 px opening at a hashed
  // offset, so no contour is a closed ring; the outermost three carry
  // one 2 x 2 px accent tip of #7C88A0 at each corner that lies inside a
  // loud margin. Every contour swells 24 px outward and returns over the
  // 8 s cycle - 6 px/s at the fastest edge - a size change only.
  {
    const std::uint32_t approvedDigest[2] = {0x669CB3A9u, 0x77F68152u};
    const std::uint32_t probeDigests[2][6] = {
        {0xD85BC459u, 0xEE151549u, 0x1D247694u, 0xED629BB0u, 0x0523965Fu,
         0x6162C001u},
        {0x7846CF31u, 0x7B5C0B81u, 0x00F190B3u, 0x2C1703C7u, 0x0521B054u,
         0x7D63D3EAu}};
    const int probeCounts[2][6] = {{96, 95, 94, 93, 94, 93},
                                   {37, 37, 37, 33, 35, 29}};
    const int tipCount[2] = {12, 4};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(16), zones, painter, 0);
      SC_CHECK(painter.count == (orientation == 0 ? 94 : 37));
      SC_CHECK(stirDigest(painter) == approvedDigest[orientation]);

      int bars = 0;
      int tips = 0;
      for (int fill = 1; fill < painter.count; ++fill) {
        const RecordedFill& element = painter.fills[fill];
        if (sameColor(element.color, designOf(16)->accent)) {
          ++tips;
          SC_CHECK(element.w == 2 && element.h == 2);
          bool inLoud = false;
          for (int margin = 0; margin < zones.loudCount; ++margin) {
            const BgRect& m = zones.loud[margin];
            inLoud = inLoud || (element.x >= m.x && element.y >= m.y &&
                                element.x + 2 <= m.x + m.w &&
                                element.y + 2 <= m.y + m.h);
          }
          SC_CHECK(inLoud);
        } else {
          ++bars;
          // Every bar piece is 2 px thick on one axis.
          SC_CHECK(element.w == 2 || element.h == 2);
        }
      }
      SC_CHECK(tips == tipCount[orientation]);
      // 72 bars in the manifest; the frame ring splits the full-width
      // bars and hides whole others, so the emitted count sits around it.
      SC_CHECK(bars >= 32);
      SC_CHECK(bars <= 90);

      // The literal geometry of the outermost contour: its top bar split
      // by the 24 px opening and cut by the frame ring, its left bar
      // split at rows 23..46, and its corner tip.
      if (orientation == 0) {
        SC_CHECK(sameFill(painter.fills[1], designOf(16)->secondary, 10, 10,
                          334, 2));
        SC_CHECK(sameFill(painter.fills[2], designOf(16)->secondary, 616, 10,
                          334, 2));
        SC_CHECK(sameFill(painter.fills[5], designOf(16)->secondary, 10, 10,
                          2, 13));
        SC_CHECK(sameFill(painter.fills[6], designOf(16)->secondary, 10, 47,
                          2, 487));
      } else {
        SC_CHECK(sameFill(painter.fills[1], designOf(16)->secondary, 10, 10,
                          152, 2));
        SC_CHECK(sameFill(painter.fills[2], designOf(16)->secondary, 186, 10,
                          348, 2));
        SC_CHECK(sameFill(painter.fills[5], designOf(16)->secondary, 10, 10,
                          2, 497));
      }

      // The swell: quiet at the exact phase edges - swell 0 through 83 ms
      // and from 7917 ms, the first step at 84 ms, the 24 px crest at
      // 4000 ms - and the digests of the moving contours between.
      const std::uint32_t quietTimes[5] = {0, 83, 7917, 8000, 16000};
      for (int probe = 0; probe < 5; ++probe) {
        RecordingPainter quiet;
        paintGrammar(*designOf(16), zones, quiet, quietTimes[probe]);
        SC_CHECK(stirDigest(quiet) == approvedDigest[orientation]);
      }
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(16), zones, moved, probeTimes[probe]);
        SC_CHECK(moved.count == probeCounts[orientation][probe]);
        SC_CHECK(stirDigest(moved) == probeDigests[orientation][probe]);
      }
      // The crest: at the half-cycle every contour has breathed its 24
      // px - the outermost sits at inset 34, its top bar at row 34 with
      // its tip at the corner (landscape) or at the bottom strip corner
      // (portrait, whose loud side strips start 119 rows down).
      {
        RecordingPainter crest;
        paintGrammar(*designOf(16), zones, crest, 4000);
        const int tipY = orientation == 0 ? 34 : 924;
        bool sawCrestTip = false;
        bool sawCrestBar = false;
        for (int fill = 1; fill < crest.count; ++fill) {
          if (crest.fills[fill].y == tipY && crest.fills[fill].x == 34 &&
              sameColor(crest.fills[fill].color, designOf(16)->accent)) {
            sawCrestTip = true;
          }
          if (crest.fills[fill].y == 34 && crest.fills[fill].x > 40 &&
              crest.fills[fill].h == 2) {
            sawCrestBar = true;
          }
        }
        SC_CHECK(sawCrestTip);
        SC_CHECK(sawCrestBar);
      }

      // The cycle repeats: 8 s of clock return the quiet stream.
      RecordingPainter afterCycle;
      paintGrammar(*designOf(16), zones, afterCycle, 8000);
      SC_CHECK(sameStream(painter, afterCycle));
    }
  }

  // ----- The contract of the chapter as a whole, both orientations: the
  // palette placement and coverage at rest and mid-motion, the emitted
  // ceilings through a whole pass of the motion, one moving layer, and
  // the stream a pure function of the clock far along it and across its
  // 32-bit wrap.
  const int elementCeilings[2][4] = {{74, 167, 287, 97},
                                     {74, 168, 101, 36}};
  // One full pass of each design's motion per orientation, sampled finer
  // than its motion quantum: 50 ms for the 5 and 6 px/s layers, 83 ms
  // for the 13.3 s staircase period, 20 ms for the pulse.
  const std::uint32_t passMs[2][4] = {{108800u, 272000u, 120000u, 8000u},
                                      {160400u, 5213000u, 120000u, 8000u}};
  const std::uint32_t passStep[4] = {50u, 50u, 83u, 20u};
  for (int orientation = 0; orientation < 2; ++orientation) {
    const Zones& zones = orientations[orientation];
    const long canvasArea =
        static_cast<long>(zones.canvasWidth) * zones.canvasHeight;
    for (int level = 13; level <= 16; ++level) {
      const Design& design = *designOf(level);

      // Palette placement and coverage, at rest and mid-motion.
      const std::uint32_t probes[4] = {0, 1300, 4100, 123457};
      for (int probe = 0; probe < 4; ++probe) {
        PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
        paintGrammar(design, zones, plate, probes[probe]);
        long accentPixels = 0;
        for (int y = 0; y < zones.canvasHeight; ++y) {
          for (int x = 0; x < zones.canvasWidth; ++x) {
            const BgColor color = plate.at(x, y);
            SC_CHECK(withinCap(color, kAccentCap));
            if (!withinCap(color, kSecondaryCap)) {
              ++accentPixels;
              bool inLoud = false;
              for (int margin = 0; margin < zones.loudCount; ++margin) {
                const BgRect& m = zones.loud[margin];
                inLoud = inLoud || (x >= m.x && x < m.x + m.w && y >= m.y &&
                                    y < m.y + m.h);
              }
              SC_CHECK(inLoud);
            }
          }
        }
        SC_CHECK(accentPixels * 100 <= canvasArea * 2);
      }

      // The emitted ceiling holds through a whole pass of the motion,
      // where the frame ring releases and recaptures steps and the
      // crossing chips breathe the count. The budget class of the record
      // stands in the package: Undertow counts 7 band elements inside
      // the 40-band budget; the runtime expands them like any grouped
      // band, and its busiest frame stays inside the small ceiling.
      int busiest = 0;
      for (std::uint32_t ms = 0; ms <= passMs[orientation][level - 13];
           ms += passStep[level - 13]) {
        RecordingPainter pass;
        paintGrammar(design, zones, pass, ms);
        if (pass.count - 1 > busiest) {
          busiest = pass.count - 1;
        }
      }
      SC_CHECK(busiest > 0);
      SC_CHECK(busiest <= elementCeilings[orientation][level - 13]);
      SC_CHECK(elementCeilings[orientation][level - 13] <= kMaxSmallElements);

      // One moving layer: the motion spec of the catalog, and a stream
      // that changes only through that layer - the same two shades at
      // every phase, at rest and far along the clock including its
      // 32-bit wrap.
      SC_CHECK(design.motion.kind != MotionKind::None);
      SC_CHECK(design.motion.speedMilliPxPerS <= 10000);
      if (design.motion.kind == MotionKind::Pulse) {
        SC_CHECK(design.motion.cycleMs >= 4000);
      }
      const std::uint32_t late[4] = {70000, 123457, 4294967295u,
                                     4294966300u};
      for (int probe = 0; probe < 4; ++probe) {
        RecordingPainter later;
        paintGrammar(design, zones, later, late[probe]);
        RecordingPainter again;
        paintGrammar(design, zones, again, late[probe]);
        SC_CHECK(sameStream(later, again));
        for (int fill = 1; fill < later.count && fill < kRecordCapacity;
             ++fill) {
          SC_CHECK(sameColor(later.fills[fill].color, design.secondary) ||
                   sameColor(later.fills[fill].color, design.accent));
        }
      }
    }
  }

  // ----- The chapter activity, read the way the design package's own arc
  // check reads it: over the landscape plates, where the board covers a
  // third of the width. There the Pressure chapter carries at least the
  // review-scale marks of Build, more elements per design, and a faster
  // peak speed - the three measures of the arc.
  {
    const Zones& zones = landscape;
    long buildMarks = 0;
    long buildElements = 0;
    for (int level = 9; level <= 12; ++level) {
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(level), zones, plate, 0);
      buildMarks += eighthMarksPer1000(plate);
      RecordingPainter painter;
      paintGrammar(*designOf(level), zones, painter, 0);
      buildElements += painter.count - 1;
    }
    long pressureMarks = 0;
    long pressureElements = 0;
    for (int level = 13; level <= 16; ++level) {
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(level), zones, plate, 0);
      pressureMarks += eighthMarksPer1000(plate);
      RecordingPainter painter;
      paintGrammar(*designOf(level), zones, painter, 0);
      pressureElements += painter.count - 1;
    }
    SC_CHECK(pressureMarks >= buildMarks);
    SC_CHECK(pressureElements > buildElements);
    SC_CHECK(designOf(16)->motion.speedMilliPxPerS >
             designOf(12)->motion.speedMilliPxPerS);
    std::printf("  landscape chapter means: marks per 1000 Build %ld, "
                "Pressure %ld; elements Build %ld, Pressure %ld\n",
                buildMarks / 4, pressureMarks / 4, buildElements / 4,
                pressureElements / 4);
  }

  // ----- Distinctness at the one-eighth review scale, across the chapter
  // boundary and inside the chapter: adjacent levels differ in a visible
  // step of six of 255 gray in at least one percent of the samples, half
  // that in portrait where the board covers 74 percent of the width.
  for (int orientation = 0; orientation < 2; ++orientation) {
    const Zones& zones = orientations[orientation];
    static int thumbs[5][kEighthSamples];
    int widths[5] = {0, 0, 0, 0, 0};
    int heights[5] = {0, 0, 0, 0, 0};
    for (int level = 12; level <= 16; ++level) {
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(level), zones, plate, 0);
      eighthScale(plate, thumbs[level - 12], &widths[level - 12],
                  &heights[level - 12]);
    }
    const int floorDivisor = orientation == 0 ? 100 : 200;
    for (int level = 12; level < 16; ++level) {
      const int samples = widths[0] * heights[0];
      int differ = 0;
      for (int index = 0; index < samples; ++index) {
        const int delta =
            thumbs[level - 12][index] - thumbs[level - 11][index];
        if (delta >= 6 || delta <= -6) {
          ++differ;
        }
      }
      SC_CHECK(differ >= samples / floorDivisor);
      std::printf("  %s %d vs %d: %d of %d samples differ (%.2f%%)\n",
                  orientationNames[orientation], level, level + 1, differ,
                  samples, 100.0 * differ / samples);
    }
  }
}


// ---------------------------------------------------------------------------
// The Peak chapter of task T083: levels 17 through 20. Every check runs on
// both orientations against the retained records of docs/backgrounds/
// level-17.md through level-20.md and the reference grammars of
// tools/backgrounds/designs.py, whose plates the runtime reproduces pixel
// for pixel (the bridge sweep of notes/agent-testing.md, T083).
// ---------------------------------------------------------------------------

void run_backgrounds_peak_checks() {
  std::printf("test_backgrounds: the Peak chapter of levels 17 through 20\n");

  const Zones landscape = maskLandscapeZones();
  const Zones portrait = maskPortraitZones();
  const Zones orientations[2] = {landscape, portrait};
  const char* orientationNames[2] = {"landscape", "portrait"};
  const std::uint32_t probeTimes[6] = {250, 500, 750, 1000, 1250, 2000};

  // ----- Level 17, Rising ember: exactly 190 warm-gray embers rising past
  // stepped block columns. The embers are the 190 cells of the 18 px grid
  // the seed 0xE17 ranks lowest inside the loud margins - 1 x 2 px in half
  // of them and 2 x 2 in the other half, all in the warm accent #897A72 -
  // rising 7 px/s and wrapping at their margin span. Along the lower fifth
  // of each side loud margin stand the static stepped columns of the
  // secondary #2A3040 - blocks 12 to 31 px wide and 10 px tall with an 8 px
  // gap, stacked every 34 px up from the bottom edge, each carrying a
  // half-width 6 px step 6 px above each block - and one row of the blocks
  // lies flat in the portrait bottom margin.
  {
    const std::uint32_t approvedDigest[2] = {0x6EACA1BAu, 0xC08420AEu};
    const std::uint32_t probeDigests[2][6] = {
        {0xED9835E8u, 0x59BE40DCu, 0xF8A6E349u, 0x817EED70u, 0xAA2C4B0Au,
         0x7D44700Au},
        {0xD9355842u, 0x0A23233Fu, 0x40892C2Eu, 0x2F923BC9u, 0x01C3A39Fu,
         0xAF90D480u}};
    const int probeCounts[2][6] = {{269, 269, 269, 269, 269, 269},
                                   {260, 257, 260, 260, 261, 261}};
    // The shape split of the 190 embers and the static-column fill counts.
    const int twoWide[2] = {96, 91};
    const int columnFills[2] = {78, 69};
    // Anchors of the ember stream, then the first column of margin 0.
    const int emberAnchors[2][4][4] = {
        {{915, 288, 1, 2}, {37, 281, 1, 2}, {9, 520, 2, 2}, {61, 435, 1, 2}},
        {{37, 400, 1, 2}, {9, 639, 2, 2}, {485, 731, 2, 2}, {26, 328, 2, 2}}};
    const int columnTop[2] = {534, 911};
    const int columnBlocks[2] = {3, 5};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(17), zones, painter, 0);
      SC_CHECK(painter.count == (orientation == 0 ? 269 : 260));
      SC_CHECK(stirDigest(painter) == approvedDigest[orientation]);

      int embers = 0;
      int wide = 0;
      int columns = 0;
      for (int fill = 1; fill < painter.count; ++fill) {
        const RecordedFill& element = painter.fills[fill];
        if (sameColor(element.color, designOf(17)->accent)) {
          ++embers;
          SC_CHECK(element.w <= 2 && element.h == 2);
          if (element.w == 2) {
            ++wide;
          }
          bool inLoud = false;
          for (int margin = 0; margin < zones.loudCount; ++margin) {
            const BgRect& m = zones.loud[margin];
            inLoud = inLoud || (element.x >= m.x && element.y >= m.y &&
                                element.x + element.w <= m.x + m.w &&
                                element.y + element.h <= m.y + m.h);
          }
          SC_CHECK(inLoud);  // the warmth lives in the loud margins alone
        } else {
          ++columns;
          SC_CHECK(sameColor(element.color, designOf(17)->secondary));
          SC_CHECK(element.h == 10 || element.h == 6);
        }
      }
      SC_CHECK(embers == 190);
      SC_CHECK(wide == twoWide[orientation]);
      SC_CHECK(columns == columnFills[orientation]);

      // The literal geometry: the first four embers of the ranked stream
      // and the first column of margin 0 - three 21 px blocks every 34 px
      // up from the bottom, each with its half-width step 6 px above.
      for (int anchor = 0; anchor < 4; ++anchor) {
        SC_CHECK(sameFill(painter.fills[1 + anchor], designOf(17)->accent,
                          emberAnchors[orientation][anchor][0],
                          emberAnchors[orientation][anchor][1],
                          emberAnchors[orientation][anchor][2],
                          emberAnchors[orientation][anchor][3]));
      }
      int firstColumn = 1;
      while (firstColumn < painter.count &&
             sameColor(painter.fills[firstColumn].color,
                       designOf(17)->accent)) {
        ++firstColumn;  // past the ember fills - a straddling portrait
        // ember paints in two margins and inflates them by one
      }
      const int blocks = columnBlocks[orientation];
      for (int block = 0; block < blocks; ++block) {
        SC_CHECK(sameFill(painter.fills[firstColumn + block],
                          designOf(17)->secondary, 4,
                          columnTop[orientation] - 34 * block, 21, 10));
        SC_CHECK(sameFill(
            painter.fills[firstColumn + blocks + block],
            designOf(17)->secondary, 4,
            columnTop[orientation] - 6 - 34 * block, 10, 6));
      }

      // Motion probes: the exact digests and fill counts - a straddling
      // ember at the portrait 920/921 seam paints in both margins, so the
      // portrait count breathes by its straddles.
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(17), zones, moved, probeTimes[probe]);
        SC_CHECK(moved.count == probeCounts[orientation][probe]);
        SC_CHECK(stirDigest(moved) == probeDigests[orientation][probe]);
      }

      // The columns hold still under the rising field: whenever a probe
      // draws the t = 0 count, every static-column fill - the last
      // columnFills of the stream - is byte-identical.
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(17), zones, moved, probeTimes[probe]);
        if (moved.count != painter.count) {
          continue;
        }
        for (int fill = painter.count - columnFills[orientation];
             fill < painter.count; ++fill) {
          SC_CHECK(sameFill(moved.fills[fill], painter.fills[fill].color,
                            painter.fills[fill].x, painter.fills[fill].y,
                            painter.fills[fill].w, painter.fills[fill].h));
        }
      }

      // The motion is one rising field: a quarter second of clock repaints
      // only the pixels the embers vacated and entered - every ember is
      // 2 x 2 px or less, so the whole field's footprint bounds the change
      // and the static columns add nothing. A second layer moving, or a
      // jump, repaints a multiple of it.
      const std::uint32_t stepPairs[3][2] = {{0, 250}, {1250, 1500},
                                             {240000, 240250}};
      for (int pair = 0; pair < 3; ++pair) {
        PlatePainter early(zones.canvasWidth, zones.canvasHeight);
        paintGrammar(*designOf(17), zones, early, stepPairs[pair][0]);
        PlatePainter late(zones.canvasWidth, zones.canvasHeight);
        paintGrammar(*designOf(17), zones, late, stepPairs[pair][1]);
        int changed = 0;
        for (int y = 0; y < zones.canvasHeight; ++y) {
          for (int x = 0; x < zones.canvasWidth; ++x) {
            if (!sameColor(early.at(x, y), late.at(x, y))) {
              ++changed;
            }
          }
        }
        SC_CHECK(changed <= 190 * 2 * 4 + 32);
      }

      // The wrap returns of the record: the field sweeps its margin span
      // whole - 544 px at 7 px/s, 544 s landscape; the portrait side spans
      // and the bottom strip together, lcm(802, 39) px, 3127.8 s.
      RecordingPainter zero;
      paintGrammar(*designOf(17), zones, zero, 0);
      RecordingPainter afterPass;
      paintGrammar(*designOf(17), zones, afterPass,
                   orientation == 0 ? 544000u : 31278000u);
      SC_CHECK(sameStream(zero, afterPass));
    }
  }

  // ----- Level 18, Storm lines: two hundred rain dashes and four stepped
  // trails falling together as one layer. The dashes are the 200 cells of
  // the 16 px grid the seed 0x518 ranks lowest - 2 px wide and 4 px tall in
  // 65 percent, 2 x 6 in 35, secondary #2A3146 or accent #8290A7 in equal
  // share - wrapping over their margin span plus 6 px, the tallest dash's
  // extent, so every dash leaves its margin fully before it re-enters. The
  // trails cycle through the loud margins at 50 percent across and 8, 38,
  // 62 and 86 percent down: staircases of five secondary segments, each
  // 4 x 2 px, stepping 4 px left and 2 px down, riding the same 8 px/s
  // fall over the same span.
  {
    const std::uint32_t approvedDigest[2] = {0x22EFA994u, 0xBEACE555u};
    const std::uint32_t probeDigests[2][6] = {
        {0x8C8F1683u, 0x199F3E41u, 0xCEFAC001u, 0x2C85A66Au, 0x4465CF9Eu,
         0x38F9925Fu},
        {0xB01A49DDu, 0xEA9E3DDEu, 0xC97F5A1Fu, 0xC2C80914u, 0xCCA1EA15u,
         0x3DA70208u}};
    const int probeCounts[2][6] = {{221, 220, 219, 218, 219, 218},
                                   {221, 222, 222, 220, 220, 213}};
    const int fourTall[2] = {134, 121};
    const int brightDashes[2] = {100, 92};
    // The first trail's head of each orientation and the second's.
    const int trailHead[2][2] = {{96, 44}, {30, 183}};
    const int secondHead[2][2] = {{864, 207}, {515, 424}};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(18), zones, painter, 0);
      SC_CHECK(painter.count == 221);
      SC_CHECK(stirDigest(painter) == approvedDigest[orientation]);

      int dashes = 0;
      int shortDash = 0;
      int bright = 0;
      for (int fill = 1; fill <= 200; ++fill) {
        const RecordedFill& dash = painter.fills[fill];
        ++dashes;
        SC_CHECK(dash.w == 2);
        SC_CHECK(dash.h == 4 || dash.h == 6);
        if (dash.h == 4) {
          ++shortDash;
        }
        SC_CHECK(sameColor(dash.color, designOf(18)->secondary) ||
                 sameColor(dash.color, designOf(18)->accent));
        if (sameColor(dash.color, designOf(18)->accent)) {
          ++bright;
          bool inLoud = false;
          for (int margin = 0; margin < zones.loudCount; ++margin) {
            const BgRect& m = zones.loud[margin];
            inLoud = inLoud || (dash.x >= m.x && dash.y >= m.y &&
                                dash.x + 2 <= m.x + m.w &&
                                dash.y + dash.h <= m.y + m.h);
          }
          SC_CHECK(inLoud);
        }
      }
      SC_CHECK(dashes == 200);
      SC_CHECK(shortDash == fourTall[orientation]);
      SC_CHECK(bright == brightDashes[orientation]);

      // The literal staircase of the first trail and the second trail's
      // head: five 4 x 2 px secondary segments stepping 4 px left and 2 px
      // down per segment.
      for (int segment = 0; segment < 5; ++segment) {
        SC_CHECK(sameFill(painter.fills[201 + segment],
                          designOf(18)->secondary,
                          trailHead[orientation][0] - 4 * segment,
                          trailHead[orientation][1] + 2 * segment, 4, 2));
      }
      SC_CHECK(sameFill(painter.fills[206], designOf(18)->secondary,
                        secondHead[orientation][0], secondHead[orientation][1],
                        4, 2));

      // Motion probes: the exact digests and fill counts.
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(18), zones, moved, probeTimes[probe]);
        SC_CHECK(moved.count == probeCounts[orientation][probe]);
        SC_CHECK(stirDigest(moved) == probeDigests[orientation][probe]);
      }

      // One combined layer: a quarter second of clock repaints only the
      // footprint of the falling dashes and trail steps - at most 2 x 6 px
      // and 4 x 2 px each, vacated and entered. A dash that leaves its
      // margin fully while another re-enters keeps the count but shifts
      // the stream, so the footprint bounds the change where a fill-by-
      // fill audit cannot; a second layer moving, or a jump, repaints a
      // multiple of it.
      const std::uint32_t stepPairs[3][2] = {{0, 250}, {1250, 1500},
                                             {240000, 240250}};
      for (int pair = 0; pair < 3; ++pair) {
        PlatePainter early(zones.canvasWidth, zones.canvasHeight);
        paintGrammar(*designOf(18), zones, early, stepPairs[pair][0]);
        PlatePainter late(zones.canvasWidth, zones.canvasHeight);
        paintGrammar(*designOf(18), zones, late, stepPairs[pair][1]);
        int changed = 0;
        for (int y = 0; y < zones.canvasHeight; ++y) {
          for (int x = 0; x < zones.canvasWidth; ++x) {
            if (!sameColor(early.at(x, y), late.at(x, y))) {
              ++changed;
            }
          }
        }
        SC_CHECK(changed <= 200 * 2 * 12 + 20 * 4 * 4 + 64);
      }

      // The wrap returns: every margin sweeps its span plus the 6 px
      // extent whole - 550 px at 8 px/s, 68.75 s landscape; the portrait
      // side spans and the bottom strip together, lcm(808, 45) px, 454.5 s.
      RecordingPainter zero;
      paintGrammar(*designOf(18), zones, zero, 0);
      RecordingPainter afterPass;
      paintGrammar(*designOf(18), zones, afterPass,
                   orientation == 0 ? 68750u : 4545000u);
      SC_CHECK(sameStream(zero, afterPass));
    }
  }

  // ----- Level 19, Compression: thirty nested broken rectangular bands of
  // secondary #2A3146, 3 px thick, inset 8 px and stepping inward by
  // max(5, 13 - index / 3) px per band, each of the four bars split once by
  // a 20 px opening at a hashed offset - the nest draws 238 bars, the
  // innermost band's two short cross-axis bars staying whole - with scarce
  // accent ticks of #8591A8, 2 x 2 px every 32 px, riding the top-bar
  // pieces of the two outermost bands that pass through the loud margins.
  // The whole nest contracts 54 px onto the board and returns over the
  // 12 s cycle; the outer bands slide off the canvas edges as it presses
  // and re-enter on the return, so nothing pops or blinks.
  {
    const std::uint32_t approvedDigest[2] = {0xA3317515u, 0x8399237Du};
    const std::uint32_t probeDigests[2][6] = {
        {0xE2DA6CC2u, 0x6C4845C7u, 0xF38C135Eu, 0x7B7B5E61u, 0x3516B371u,
         0xB3D1A6A2u},
        {0xA768B611u, 0x7FF839B4u, 0x9162CA9Cu, 0x7FA7A3AFu, 0x6B05DFD9u,
         0x6B2485E1u}};
    const int probeCounts[2][6] = {{290, 299, 297, 302, 268, 272},
                                   {67, 65, 65, 71, 62, 69}};
    // The digests of the twelve exact-half clocks of the cycle, 500 ms
    // apart: the swell sits on 4.5, 13.5, ... 49.5 px there, where the
    // package's double arithmetic rounds the scaled product off the tie -
    // 31 px at the 8.5 s tie and 23 at the 9.5 s, one pixel off every
    // ties-to-even whole - so the digests pin the ported rounding itself.
    const std::uint32_t tieDigests[2][12] = {
        {0x6C4845C7u, 0x195B0E6Cu, 0xA393765Cu, 0x58346F03u, 0xBE3B55EEu,
         0xEFBB252Du, 0xEFBB252Du, 0xBE3B55EEu, 0x72046943u, 0xEAD89A9Fu,
         0x195B0E6Cu, 0x6C4845C7u},
        {0x7FF839B4u, 0x61A4FE1Bu, 0x946E6280u, 0xC2713FD6u, 0x8E945202u,
         0x6EA8CA37u, 0x6EA8CA37u, 0x8E945202u, 0x3EFA76B4u, 0xD0F62E52u,
         0x61A4FE1Bu, 0x7FF839B4u}};
    const int tickCount[2] = {22, 0};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(19), zones, painter, 0);
      SC_CHECK(painter.count == (orientation == 0 ? 289 : 63));
      SC_CHECK(stirDigest(painter) == approvedDigest[orientation]);

      int bars = 0;
      int ticks = 0;
      for (int fill = 1; fill < painter.count; ++fill) {
        const RecordedFill& element = painter.fills[fill];
        if (sameColor(element.color, designOf(19)->accent)) {
          ++ticks;
          SC_CHECK(element.w == 2 && element.h == 2);
        } else {
          ++bars;
          // Every bar piece is 3 px thick on one axis.
          SC_CHECK(element.w == 3 || element.h == 3);
        }
      }
      SC_CHECK(ticks == tickCount[orientation]);
      SC_CHECK(bars >= 56);  // 238 bars in the manifest; the tall board and
      // the frame ring hide most of them in portrait

      // The literal geometry of the outermost band at rest: its top bar
      // split by the 20 px opening and cut by the frame ring, its left
      // bar split at rows 331..330, and its tick runs - six per margin
      // landscape, none portrait, where the loud zones start 119 rows
      // below the pressed top bar.
      if (orientation == 0) {
        SC_CHECK(sameFill(painter.fills[1], designOf(19)->secondary, 8, 8,
                          336, 3));
        SC_CHECK(sameFill(painter.fills[2], designOf(19)->secondary, 616, 8,
                          242, 3));
        SC_CHECK(sameFill(painter.fills[3], designOf(19)->secondary, 878, 8,
                          74, 3));
        SC_CHECK(sameFill(painter.fills[7], designOf(19)->secondary, 8, 8, 3,
                          303));
        SC_CHECK(sameFill(painter.fills[8], designOf(19)->secondary, 8, 331, 3,
                          205));
        for (int tick = 0; tick < 6; ++tick) {
          SC_CHECK(sameFill(painter.fills[11 + tick], designOf(19)->accent,
                            14 + 32 * tick, 8, 2, 2));
          SC_CHECK(sameFill(painter.fills[17 + tick], designOf(19)->accent,
                            774 + 32 * tick, 8, 2, 2));
        }
      } else {
        SC_CHECK(sameFill(painter.fills[1], designOf(19)->secondary, 8, 8,
                          166, 3));
        SC_CHECK(sameFill(painter.fills[2], designOf(19)->secondary, 194, 8,
                          342, 3));
      }

      // Motion probes: the exact digests and fill counts, then the twelve
      // exact-half ties of the 12 s cycle.
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(19), zones, moved, probeTimes[probe]);
        SC_CHECK(moved.count == probeCounts[orientation][probe]);
        SC_CHECK(stirDigest(moved) == probeDigests[orientation][probe]);
      }
      for (int tie = 0; tie < 12; ++tie) {
        RecordingPainter tied;
        paintGrammar(*designOf(19), zones, tied, 500 + 1000 * tie);
        SC_CHECK(stirDigest(tied) == tieDigests[orientation][tie]);
      }

      // The nest is quiet at the exact phase edges - no swell through
      // 55 ms and from 11945 ms - and first moves at 56 ms.
      const std::uint32_t quietTimes[4] = {0, 55, 11945, 12000};
      for (int probe = 0; probe < 4; ++probe) {
        RecordingPainter quiet;
        paintGrammar(*designOf(19), zones, quiet, quietTimes[probe]);
        SC_CHECK(stirDigest(quiet) == approvedDigest[orientation]);
      }
      {
        RecordingPainter moved;
        paintGrammar(*designOf(19), zones, moved, 56);
        SC_CHECK(stirDigest(moved) != approvedDigest[orientation]);
      }

      // The crest: at the half cycle every band has pressed its 54 px -
      // the fifth band's inset 59 sits at 5 px from the canvas edges, its
      // top bar cut by the frame ring and split by its opening.
      {
        RecordingPainter crest;
        paintGrammar(*designOf(19), zones, crest, 6000);
        SC_CHECK(stirDigest(crest) ==
                 (orientation == 0 ? 0xFF809551u : 0x4BA087D4u));
        if (orientation == 0) {
          SC_CHECK(sameFill(crest.fills[1], designOf(19)->secondary, 5, 5,
                            339, 3));
          SC_CHECK(sameFill(crest.fills[2], designOf(19)->secondary, 616, 5,
                            263, 3));
          SC_CHECK(sameFill(crest.fills[3], designOf(19)->secondary, 899, 5,
                            56, 3));
        } else {
          SC_CHECK(sameFill(crest.fills[1], designOf(19)->secondary, 5, 5,
                            324, 3));
        }
      }

      // The 12 s cycle returns the quiet stream.
      RecordingPainter afterCycle;
      paintGrammar(*designOf(19), zones, afterCycle, 12000);
      SC_CHECK(sameStream(painter, afterCycle));
    }
  }

  // ----- Level 20, Collapse: two hundred eighty tiny debris fragments
  // falling past stepped slabs at the far edges. The fragments are the
  // first 280 cells of the 16 px grid ranked by outer-edge closeness first
  // and the seed 0xC0A second - the debris piles against the far canvas
  // edges - each 1 x 3 px in 35 percent of the cells, 2 x 5 in 35 and
  // 3 x 7 in 30, two thirds in the secondary #2A3146 and one third in the
  // accent #8A94AC, wrapping over the margin span plus the tallest
  // fragment's 7 px extent. Three stepped slabs of the secondary per loud
  // margin - 32 to 71 px wide and 10 to 19 px tall, at hashed positions in
  // the upper three quarters, each carrying a half-width 5 px step on its
  // top - hold still while the field falls 10 px/s.
  {
    const std::uint32_t approvedDigest[2] = {0xD54E4F06u, 0x3828AFFBu};
    const std::uint32_t probeDigests[2][6] = {
        {0xDCE4C8F6u, 0x7EE8C258u, 0xFD4DEEFFu, 0x983CE358u, 0x7A6E4255u,
         0xB806A86Bu},
        {0x20ED3756u, 0xC6F37A7Fu, 0x9617E5BFu, 0xC835D525u, 0x1F983EE4u,
         0xA077DF8Au}};
    const int probeCounts[2][6] = {{293, 292, 292, 291, 287, 291},
                                   {291, 291, 291, 291, 284, 274}};
    const int sizeSplit[2][3] = {{112, 87, 81}, {90, 100, 90}};
    const int brightShare[2] = {99, 79};
    const int slabFills[2] = {12, 10};

    for (int orientation = 0; orientation < 2; ++orientation) {
      const Zones& zones = orientations[orientation];
      RecordingPainter painter;
      paintGrammar(*designOf(20), zones, painter, 0);
      SC_CHECK(painter.count == (orientation == 0 ? 293 : 291));
      SC_CHECK(stirDigest(painter) == approvedDigest[orientation]);

      int debris = 0;
      int sizes[3] = {0, 0, 0};
      int bright = 0;
      for (int fill = 1; fill <= 280; ++fill) {
        const RecordedFill& fragment = painter.fills[fill];
        ++debris;
        SC_CHECK((fragment.w == 1 && fragment.h == 3) ||
                 (fragment.w == 2 && fragment.h == 5) ||
                 (fragment.w == 3 && fragment.h == 7));
        ++sizes[fragment.w - 1];
        SC_CHECK(sameColor(fragment.color, designOf(20)->secondary) ||
                 sameColor(fragment.color, designOf(20)->accent));
        if (sameColor(fragment.color, designOf(20)->accent)) {
          ++bright;
          bool inLoud = false;
          for (int margin = 0; margin < zones.loudCount; ++margin) {
            const BgRect& m = zones.loud[margin];
            inLoud = inLoud || (fragment.x >= m.x && fragment.y >= m.y &&
                                fragment.x + fragment.w <= m.x + m.w &&
                                fragment.y + fragment.h <= m.y + m.h);
          }
          SC_CHECK(inLoud);
        }
      }
      SC_CHECK(debris == 280);
      for (int size = 0; size < 3; ++size) {
        SC_CHECK(sizes[size] == sizeSplit[orientation][size]);
      }
      SC_CHECK(bright == brightShare[orientation]);

      // The literal slabs of margin 0: a 32 x 13 px slab at a hashed
      // position in the upper quarter with its half-width 5 px step on
      // top - (148, 6) landscape, (15, 125) in the portrait side strip.
      if (orientation == 0) {
        SC_CHECK(sameFill(painter.fills[281], designOf(20)->secondary, 148, 6,
                          32, 13));
        SC_CHECK(sameFill(painter.fills[282], designOf(20)->secondary, 148, 1,
                          16, 5));
      } else {
        SC_CHECK(sameFill(painter.fills[281], designOf(20)->secondary, 15,
                          125, 32, 13));
        SC_CHECK(sameFill(painter.fills[282], designOf(20)->secondary, 15,
                          120, 16, 5));
      }

      // Motion probes: the exact digests and fill counts.
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(20), zones, moved, probeTimes[probe]);
        SC_CHECK(moved.count == probeCounts[orientation][probe]);
        SC_CHECK(stirDigest(moved) == probeDigests[orientation][probe]);
      }

      // The slabs hold still under the falling field: the last slabFills
      // of every stream of the t = 0 count.
      for (int probe = 0; probe < 6; ++probe) {
        RecordingPainter moved;
        paintGrammar(*designOf(20), zones, moved, probeTimes[probe]);
        if (moved.count != painter.count) {
          continue;
        }
        for (int fill = painter.count - slabFills[orientation];
             fill < painter.count; ++fill) {
          SC_CHECK(sameFill(moved.fills[fill], painter.fills[fill].color,
                            painter.fills[fill].x, painter.fills[fill].y,
                            painter.fills[fill].w, painter.fills[fill].h));
        }
      }

      // One falling field: a quarter second repaints only the footprint of
      // the 280 fragments - at most 3 x 7 px each, vacated and entered -
      // while the static slabs add nothing.
      const std::uint32_t stepPairs[3][2] = {{0, 250}, {1250, 1500},
                                             {240000, 240250}};
      for (int pair = 0; pair < 3; ++pair) {
        PlatePainter early(zones.canvasWidth, zones.canvasHeight);
        paintGrammar(*designOf(20), zones, early, stepPairs[pair][0]);
        PlatePainter late(zones.canvasWidth, zones.canvasHeight);
        paintGrammar(*designOf(20), zones, late, stepPairs[pair][1]);
        int changed = 0;
        for (int y = 0; y < zones.canvasHeight; ++y) {
          for (int x = 0; x < zones.canvasWidth; ++x) {
            if (!sameColor(early.at(x, y), late.at(x, y))) {
              ++changed;
            }
          }
        }
        SC_CHECK(changed <= 280 * 3 * 14 + 64);
      }

      // The wrap returns: every margin sweeps its span plus the 7 px
      // extent whole - 551 px at 10 px/s, 55.1 s landscape; the portrait
      // side spans and the bottom strip together, lcm(809, 46) px,
      // 3721.4 s.
      RecordingPainter zero;
      paintGrammar(*designOf(20), zones, zero, 0);
      RecordingPainter afterPass;
      paintGrammar(*designOf(20), zones, afterPass,
                   orientation == 0 ? 55100u : 3721400u);
      SC_CHECK(sameStream(zero, afterPass));
    }
  }

  // ----- The contract of the chapter as a whole, both orientations: the
  // palette placement and coverage at rest and mid-motion, the emitted
  // ceilings through a sampled pass of the motion, one moving layer, and
  // the stream a pure function of the clock far along it and across its
  // 32-bit wrap.
  const int elementCeilings[2][4] = {{268, 220, 301, 292},
                                     {261, 222, 80, 293}};
  // One sampled pass of each design's motion per orientation: at least a
  // full margin span of the moving layer plus the bottom strip's, and the
  // whole 12 s pulse.
  const std::uint32_t passMs[2][4] = {{80000u, 68750u, 12000u, 55100u},
                                      {240000u, 210000u, 12000u, 170000u}};
  const std::uint32_t passStep[4] = {25u, 25u, 20u, 25u};
  for (int orientation = 0; orientation < 2; ++orientation) {
    const Zones& zones = orientations[orientation];
    const long canvasArea =
        static_cast<long>(zones.canvasWidth) * zones.canvasHeight;
    for (int level = 17; level <= 20; ++level) {
      const Design& design = *designOf(level);

      // Palette placement and coverage, at rest and mid-motion.
      const std::uint32_t probes[4] = {0, 1300, 4100, 123457};
      for (int probe = 0; probe < 4; ++probe) {
        PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
        paintGrammar(design, zones, plate, probes[probe]);
        long accentPixels = 0;
        for (int y = 0; y < zones.canvasHeight; ++y) {
          for (int x = 0; x < zones.canvasWidth; ++x) {
            const BgColor color = plate.at(x, y);
            SC_CHECK(withinCap(color, kAccentCap));
            if (!withinCap(color, kSecondaryCap)) {
              ++accentPixels;
              bool inLoud = false;
              for (int margin = 0; margin < zones.loudCount; ++margin) {
                const BgRect& m = zones.loud[margin];
                inLoud = inLoud || (x >= m.x && x < m.x + m.w && y >= m.y &&
                                    y < m.y + m.h);
              }
              SC_CHECK(inLoud);
            }
          }
        }
        SC_CHECK(accentPixels * 100 <= canvasArea * 2);
      }

      // The emitted ceiling holds through the sampled pass, where the
      // frame ring releases and recaptures bar pieces and the crossing
      // fragments breathe the count. Compression's 260 placed elements -
      // 238 press bars and 22 ticks - sit inside the 300-element budget
      // while the frame ring's clip splits the wide bars, the documented
      // primitive expansion; its busiest landscape frame emits 301 fills.
      int busiest = 0;
      for (std::uint32_t ms = 0; ms <= passMs[orientation][level - 17];
           ms += passStep[level - 17]) {
        RecordingPainter pass;
        paintGrammar(design, zones, pass, ms);
        if (pass.count - 1 > busiest) {
          busiest = pass.count - 1;
        }
      }
      SC_CHECK(busiest > 0);
      SC_CHECK(busiest <= elementCeilings[orientation][level - 17]);
      if (level != 19) {
        SC_CHECK(elementCeilings[orientation][level - 17] <=
                 kMaxSmallElements);
      }

      // One moving layer: the motion spec of the catalog, and a stream
      // that changes only through that layer - the same two shades at
      // every phase, at rest and far along the clock including its
      // 32-bit wrap.
      SC_CHECK(design.motion.kind != MotionKind::None);
      SC_CHECK(design.motion.speedMilliPxPerS <= 10000);
      if (design.motion.kind == MotionKind::Pulse) {
        SC_CHECK(design.motion.cycleMs >= 4000);
      }
      const std::uint32_t late[4] = {70000, 123457, 4294967295u,
                                     4294966300u};
      for (int probe = 0; probe < 4; ++probe) {
        RecordingPainter later;
        paintGrammar(design, zones, later, late[probe]);
        RecordingPainter again;
        paintGrammar(design, zones, again, late[probe]);
        SC_CHECK(sameStream(later, again));
        for (int fill = 1; fill < later.count && fill < kRecordCapacity;
             ++fill) {
          SC_CHECK(sameColor(later.fills[fill].color, design.secondary) ||
                   sameColor(later.fills[fill].color, design.accent));
        }
      }
    }
  }

  // ----- The chapter activity, read the way the design package's own arc
  // check reads it: over the landscape plates, where the board covers a
  // third of the width. There the Peak chapter carries at least the
  // review-scale marks of Pressure, more elements per design, and a faster
  // peak speed - the three measures of the arc.
  {
    const Zones& zones = landscape;
    long pressureMarks = 0;
    long pressureElements = 0;
    for (int level = 13; level <= 16; ++level) {
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(level), zones, plate, 0);
      pressureMarks += eighthMarksPer1000(plate);
      RecordingPainter painter;
      paintGrammar(*designOf(level), zones, painter, 0);
      pressureElements += painter.count - 1;
    }
    long peakMarks = 0;
    long peakElements = 0;
    for (int level = 17; level <= 20; ++level) {
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(level), zones, plate, 0);
      peakMarks += eighthMarksPer1000(plate);
      RecordingPainter painter;
      paintGrammar(*designOf(level), zones, painter, 0);
      peakElements += painter.count - 1;
    }
    SC_CHECK(peakMarks >= pressureMarks);
    SC_CHECK(peakElements > pressureElements);
    SC_CHECK(designOf(20)->motion.speedMilliPxPerS >
             designOf(16)->motion.speedMilliPxPerS);
    std::printf("  landscape chapter means: marks per 1000 Pressure %ld, "
                "Peak %ld; elements Pressure %ld, Peak %ld\n",
                pressureMarks / 4, peakMarks / 4, pressureElements / 4,
                peakElements / 4);
  }

  // ----- Distinctness at the one-eighth review scale, across the chapter
  // boundary and inside the chapter: adjacent levels differ in a visible
  // step of six of 255 gray in at least one percent of the samples, half
  // that in portrait where the board covers 74 percent of the width.
  for (int orientation = 0; orientation < 2; ++orientation) {
    const Zones& zones = orientations[orientation];
    static int thumbs[5][kEighthSamples];
    int widths[5] = {0, 0, 0, 0, 0};
    int heights[5] = {0, 0, 0, 0, 0};
    for (int level = 16; level <= 20; ++level) {
      PlatePainter plate(zones.canvasWidth, zones.canvasHeight);
      paintGrammar(*designOf(level), zones, plate, 0);
      eighthScale(plate, thumbs[level - 16], &widths[level - 16],
                  &heights[level - 16]);
    }
    const int floorDivisor = orientation == 0 ? 100 : 200;
    for (int level = 16; level < 20; ++level) {
      const int samples = widths[0] * heights[0];
      int differ = 0;
      for (int index = 0; index < samples; ++index) {
        const int delta =
            thumbs[level - 16][index] - thumbs[level - 15][index];
        if (delta >= 6 || delta <= -6) {
          ++differ;
        }
      }
      SC_CHECK(differ >= samples / floorDivisor);
      std::printf("  %s %d vs %d: %d of %d samples differ (%.2f%%)\n",
                  orientationNames[orientation], level, level + 1, differ,
                  samples, 100.0 * differ / samples);
    }
  }
}

// ---------------------------------------------------------------------------
// The wallpaper state machine: modes, cuts, stepped fades, round resets.
// ---------------------------------------------------------------------------

void run_backgrounds_scene_checks() {
  std::printf("test_backgrounds: modes, transitions, and round resets\n");

  const Zones landscape = maskLandscapeZones();
  RecordingPainter painter;

  // A fresh wallpaper: Auto, design 1 of level 1, the Still ground grammar:
  // its base field plus the eight corner steps (task T079).
  Wallpaper wallpaper;
  SC_CHECK(wallpaper.mode() == Mode::Auto);
  SC_CHECK(wallpaper.preference() == 1);
  SC_CHECK(wallpaper.level() == 1);
  SC_CHECK(wallpaper.activeDesign() == 1);
  SC_CHECK(wallpaper.draw(landscape, painter));
  SC_CHECK(painter.count == 9);
  SC_CHECK(sameFill(painter.fills[0], designOf(1)->base, 0, 0, 960, 544));

  // Mode Off draws nothing: the caller keeps the legacy flat clear.
  wallpaper.setMode(Mode::Off);
  painter.reset();
  SC_CHECK(!wallpaper.draw(landscape, painter));
  SC_CHECK(painter.count == 0);

  // Level changes while Off change nothing.
  wallpaper.setLevel(20);
  SC_CHECK(wallpaper.activeDesign() == 1);

  // Back to Auto cuts straight to the level's design.
  wallpaper.setMode(Mode::Auto);
  SC_CHECK(wallpaper.activeDesign() == 20);
  painter.reset();
  SC_CHECK(wallpaper.draw(landscape, painter));
  SC_CHECK(sameColor(firstFillColor(painter), designOf(20)->base));

  // Auto clamps the level into the design range.
  wallpaper.setLevel(99);
  SC_CHECK(wallpaper.activeDesign() == 20);
  wallpaper.setLevel(0);
  SC_CHECK(wallpaper.activeDesign() == 1);
  wallpaper.setLevel(-7);
  SC_CHECK(wallpaper.activeDesign() == 1);

  // The preference clamps, survives Off and Auto, and cuts only in Fixed.
  wallpaper.setPreference(0);
  SC_CHECK(wallpaper.preference() == 1);
  wallpaper.setPreference(25);
  SC_CHECK(wallpaper.preference() == 20);
  wallpaper.setPreference(7);
  wallpaper.setLevel(3);
  SC_CHECK(wallpaper.activeDesign() == 3);  // Auto still follows the level
  wallpaper.setMode(Mode::Fixed);
  SC_CHECK(wallpaper.activeDesign() == 7);
  wallpaper.setLevel(20);
  wallpaper.setLevel(1);
  SC_CHECK(wallpaper.activeDesign() == 7);  // Fixed ignores the level
  painter.reset();
  SC_CHECK(wallpaper.draw(landscape, painter));
  SC_CHECK(sameColor(firstFillColor(painter), designOf(7)->base));
  wallpaper.setPreference(12);
  SC_CHECK(wallpaper.activeDesign() == 12);

  // Fixed stays fixed across rounds: the clock runs on, unreset.
  advanceBy(wallpaper, 5000);
  wallpaper.beginRound();
  SC_CHECK(wallpaper.activeDesign() == 12);
  SC_CHECK(wallpaper.timeMs() == 5000);

  // Auto resets the clock at a round boundary and cuts to the level design.
  wallpaper.setMode(Mode::Auto);
  wallpaper.setLevel(5);
  advanceBy(wallpaper, 4000);
  wallpaper.beginRound();
  SC_CHECK(wallpaper.activeDesign() == 5);
  SC_CHECK(wallpaper.timeMs() == 0);

  // A cut design changes in one draw: the level's record says cut.
  wallpaper = Wallpaper();
  wallpaper.setLevel(5);
  SC_CHECK(wallpaper.activeDesign() == 5);
  SC_CHECK(!wallpaper.fading());
  painter.reset();
  SC_CHECK(wallpaper.draw(landscape, painter));
  SC_CHECK(sameColor(firstFillColor(painter), designOf(5)->base));

  // A fade design steps through its record's visible steps. Design 20 fades
  // from design 1 in 600 ms and 18 steps; the anchor is the midpoint of the
  // two base fields. The fade draws exactly one design's grammar per frame:
  // the eight corner steps of design 1 in the first half, the landed
  // Collapse grammar of design 20 - its base fill plus 292 elements - in
  // the second.
  wallpaper = Wallpaper();
  wallpaper.setLevel(20);
  SC_CHECK(wallpaper.activeDesign() == 20);
  SC_CHECK(wallpaper.fading());
  const BgColor anchor = midColor(designOf(1)->base, designOf(20)->base);
  SC_CHECK(anchor.r == 20 && anchor.g == 24 && anchor.b == 34);
  int outgoingFills = 0;
  int incomingFills = 0;
  {
    RecordingPainter outgoing;
    paintGrammar(*designOf(1), landscape, outgoing, 0);
    outgoingFills = outgoing.count;
    RecordingPainter incoming;
    paintGrammar(*designOf(20), landscape, incoming, 0);
    incomingFills = incoming.count;
  }
  SC_CHECK(outgoingFills == 9 && incomingFills == 293);

  // Step 0: the outgoing design at full color, the frame before the change.
  painter.reset();
  SC_CHECK(wallpaper.draw(landscape, painter));
  SC_CHECK(painter.count == outgoingFills);
  SC_CHECK(sameColor(firstFillColor(painter), designOf(1)->base));

  // After 150 ms: step 4 of 18, the outgoing design eight parts toward the
  // anchor. One design's fills alone - never two.
  advanceBy(wallpaper, 150);
  painter.reset();
  SC_CHECK(wallpaper.draw(landscape, painter));
  SC_CHECK(painter.count == outgoingFills);
  SC_CHECK(sameColor(firstFillColor(painter), mixToward(designOf(1)->base, anchor, 8, 18)));

  // After 300 ms: the midpoint step, the incoming design fully at the
  // anchor, which is the flat hand-off frame of the two halves.
  advanceBy(wallpaper, 150);
  painter.reset();
  SC_CHECK(wallpaper.draw(landscape, painter));
  SC_CHECK(painter.count == incomingFills);
  SC_CHECK(sameColor(firstFillColor(painter), anchor));

  // After 500 ms: step 15, the incoming design three parts from the anchor.
  advanceBy(wallpaper, 200);
  painter.reset();
  SC_CHECK(wallpaper.draw(landscape, painter));
  SC_CHECK(sameColor(firstFillColor(painter), mixToward(designOf(20)->base, anchor, 6, 18)));

  // Every step respects the palette caps and the budget of one design. The
  // base moves monotonically toward the target, one design's fills at every
  // step, and the fade visits clearly distinct shades on its way: the
  // integer quantization of a 2-to-9-channel-step pair cannot change a
  // pixel at every single step, so the contract is the step count and the
  // monotone climb, both pinned here.
  wallpaper = Wallpaper();
  wallpaper.setLevel(20);
  int previousSum = -1;
  int distinctBases = 0;
  BgColor previousBase{0, 0, 0};
  for (int probe = 0; probe < 18; ++probe) {
    advanceBy(wallpaper, 34);  // one step boundary is 33.34 ms
    painter.reset();
    SC_CHECK(wallpaper.draw(landscape, painter));
    // One design's grammar alone, never two: the outgoing half draws the
    // design 1 corner steps, the incoming half the landed design 20
    // grammar at the running clock, whose falling debris breathes the
    // fill count - the expected count comes from the same design painted
    // at the same time.
    RecordingPainter outgoingNow;
    paintGrammar(*designOf(1), landscape, outgoingNow, wallpaper.timeMs());
    RecordingPainter incomingNow;
    paintGrammar(*designOf(20), landscape, incomingNow, wallpaper.timeMs());
    SC_CHECK(painter.count == outgoingNow.count ||
             painter.count == incomingNow.count);
    for (int fill = 0; fill < painter.count; ++fill) {
      SC_CHECK(withinCap(painter.fills[fill].color, kAccentCap));
      // The tint of a color at or below the secondary cap blends toward
      // the anchor - itself below the cap - and never raises a channel
      // above either source's, and the tinted accents stay at or below
      // the accent cap, checked above.
    }
    const BgColor base = firstFillColor(painter);
    const int sum = base.r + base.g + base.b;
    SC_CHECK(sum >= previousSum);  // never moves away from the target
    previousSum = sum;
    if (probe == 0 || base.r != previousBase.r || base.g != previousBase.g ||
        base.b != previousBase.b) {
      ++distinctBases;
    }
    previousBase = base;
  }
  SC_CHECK(distinctBases >= 10);
  // The fade ends within its 600 ms record and lands on the plain target.
  advanceBy(wallpaper, 100);
  SC_CHECK(!wallpaper.fading());
  painter.reset();
  SC_CHECK(wallpaper.draw(landscape, painter));
  SC_CHECK(sameColor(firstFillColor(painter), designOf(20)->base));

  // Pause freezes a fade mid-step and resumes without a jump.
  wallpaper = Wallpaper();
  wallpaper.setLevel(20);
  advanceBy(wallpaper, 150);  // step 4
  wallpaper.setFrozen(true);
  advanceBy(wallpaper, 400);
  painter.reset();
  SC_CHECK(wallpaper.draw(landscape, painter));
  SC_CHECK(sameColor(firstFillColor(painter), mixToward(designOf(1)->base, anchor, 8, 18)));
  wallpaper.setFrozen(false);
  advanceBy(wallpaper, 50);  // elapsed 200 ms: step 6, twelve parts in
  painter.reset();
  SC_CHECK(wallpaper.draw(landscape, painter));
  SC_CHECK(sameColor(firstFillColor(painter), mixToward(designOf(1)->base, anchor, 12, 18)));

  // A round boundary mid-fade resets predictably: the clock at zero, the
  // fade gone, the level's design plain.
  wallpaper = Wallpaper();
  wallpaper.setLevel(20);
  advanceBy(wallpaper, 300);
  SC_CHECK(wallpaper.fading());
  wallpaper.beginRound();
  SC_CHECK(!wallpaper.fading());
  SC_CHECK(wallpaper.timeMs() == 0);
  SC_CHECK(wallpaper.activeDesign() == 20);
  painter.reset();
  SC_CHECK(wallpaper.draw(landscape, painter));
  SC_CHECK(sameColor(firstFillColor(painter), designOf(20)->base));

  // A second selection that arrives while a fade runs restarts the fade
  // from the running fade's target, predictably and without two grammars.
  wallpaper = Wallpaper();
  wallpaper.setLevel(11);  // fades 1 -> 11
  advanceBy(wallpaper, 200);
  wallpaper.setLevel(4);   // arrives mid-fade: fades 11 -> 4
  SC_CHECK(wallpaper.fading());
  SC_CHECK(wallpaper.activeDesign() == 4);
  painter.reset();
  SC_CHECK(wallpaper.draw(landscape, painter));
  // Step 0 of the new fade: the design 11 base at full color.
  SC_CHECK(sameColor(firstFillColor(painter), designOf(11)->base));

  // A level change to the design a fade is already reaching changes
  // nothing: the fade runs on.
  wallpaper = Wallpaper();
  wallpaper.setLevel(11);
  advanceBy(wallpaper, 200);
  wallpaper.setLevel(11);
  SC_CHECK(wallpaper.fading());
  SC_CHECK(wallpaper.activeDesign() == 11);
}

// ---------------------------------------------------------------------------
// The independence of the wallpaper from gameplay randomness.
// ---------------------------------------------------------------------------

void run_backgrounds_determinism_checks() {
  std::printf("test_backgrounds: determinism independent of the game\n");

  // Two rounds that differ: different seeds, played a while, so their boards
  // genuinely diverge.
  Game left;
  left.start(1);
  Game right;
  right.start(424242);
  InputFrame drop = InputFrame::idle();
  drop.hardDropPressed = true;
  for (int tick = 0; tick < 240; ++tick) {
    left.tick(tick % 30 == 0 ? drop : InputFrame::idle());
    right.tick(tick % 30 == 0 ? drop : InputFrame::idle());
  }
  SC_CHECK(std::memcmp(left.board().cellTypes().data(),
                       right.board().cellTypes().data(),
                       sizeof(left.board().cellTypes())) != 0);
  SC_CHECK(left.level() == right.level());  // both still on the same level

  // The wallpaper of the same level at the same clock time draws the same
  // fills for both rounds: no game state, seed, or randomizer value reaches
  // the cosmetic path.
  const Zones landscape = maskLandscapeZones();
  Wallpaper leftWallpaper;
  leftWallpaper.setLevel(left.level());
  Wallpaper rightWallpaper;
  rightWallpaper.setLevel(right.level());
  advanceBy(leftWallpaper, 1234);
  advanceBy(rightWallpaper, 1234);
  RecordingPainter leftPainter;
  RecordingPainter rightPainter;
  SC_CHECK(leftWallpaper.draw(landscape, leftPainter));
  SC_CHECK(rightWallpaper.draw(landscape, rightPainter));
  SC_CHECK(leftPainter.count == rightPainter.count);
  for (int fill = 0; fill < leftPainter.count && fill < kRecordCapacity;
       ++fill) {
    SC_CHECK(sameFill(leftPainter.fills[fill], rightPainter.fills[fill].color,
                      rightPainter.fills[fill].x, rightPainter.fills[fill].y,
                      rightPainter.fills[fill].w, rightPainter.fills[fill].h));
  }

  // The same wallpaper draws the same stream twice in a row: drawing is a
  // pure reader of the presentation state.
  RecordingPainter again;
  SC_CHECK(leftWallpaper.draw(landscape, again));
  SC_CHECK(again.count == leftPainter.count);
  for (int fill = 0; fill < again.count && fill < kRecordCapacity; ++fill) {
    SC_CHECK(sameFill(again.fills[fill], leftPainter.fills[fill].color,
                      leftPainter.fills[fill].x, leftPainter.fills[fill].y,
                      leftPainter.fills[fill].w, leftPainter.fills[fill].h));
  }

  // The portrait zones draw the same design without touching the frame or
  // the quiet strips, in the tall canvas as in the wide one.
  const Zones portrait = maskPortraitZones();
  RecordingPainter portraitPainter;
  Wallpaper portraitWallpaper;
  portraitWallpaper.setLevel(20);
  advanceBy(portraitWallpaper, 4000);  // past the fade, plain design 20
  SC_CHECK(portraitWallpaper.draw(portrait, portraitPainter));
  // the landed Collapse grammar at the 4000 ms clock, base first
  RecordingPainter portraitNow;
  paintGrammar(*designOf(20), portrait, portraitNow, 4000);
  SC_CHECK(portraitPainter.count == portraitNow.count);
  SC_CHECK(sameFill(portraitPainter.fills[0], designOf(20)->base, 0, 0, 544,
                    960));
}

}  // namespace

void run_backgrounds_tests();

void run_backgrounds_tests() {
  run_backgrounds_catalog_checks();
  run_backgrounds_caps_checks();
  run_backgrounds_hash_checks();
  run_backgrounds_clock_checks();
  run_backgrounds_clip_checks();
  run_backgrounds_grammar_checks();
  run_backgrounds_still_checks();
  run_backgrounds_stir_checks();
  run_backgrounds_build_checks();
  run_backgrounds_pressure_checks();
  run_backgrounds_peak_checks();
  run_backgrounds_scene_checks();
  run_backgrounds_determinism_checks();
}
