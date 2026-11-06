// The SDL-free half of the procedural level backgrounds (task T078,
// docs/backgrounds and docs/BACKGROUND_IMPLEMENTATION.md).
//
// This file carries the clipping layer, the cosmetic hash, the catalog of the
// twenty approved designs, the presentation clock, and the wallpaper state
// machine with its cut and stepped-fade transitions. The SDL half lives in
// src/renderer_sdl.cpp: it derives the layout zones from the renderer
// geometry and adapts the painter to the renderer's fill primitive.
//
// Nothing here reads or writes the game. A draw walks fixed-size state only,
// so the wallpaper can never perturb a round, its hash, or its randomizer.

#include "stackfall/backgrounds.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace stackfall {
namespace backgrounds {
namespace {

// ---------------------------------------------------------------------------
// The CRC-32 of hash32(): the standard reflected polynomial 0xEDB88320 with
// initial and final 0xFFFFFFFF, the same value zlib.crc32 returns over the
// packed bytes. The table is built at compile time.
// ---------------------------------------------------------------------------

constexpr std::uint32_t crcTableEntry(std::uint32_t index) {
  std::uint32_t value = index;
  for (int bit = 0; bit < 8; ++bit) {
    value = (value & 1u) != 0u ? 0xEDB88320u ^ (value >> 1) : value >> 1;
  }
  return value;
}

struct CrcTable {
  std::uint32_t entries[256];
  constexpr CrcTable() : entries{} {
    for (int index = 0; index < 256; ++index) {
      entries[index] = crcTableEntry(static_cast<std::uint32_t>(index));
    }
  }
};

constexpr CrcTable kCrcTable{};

// The largest piece count one element can emit: an accent intersects the at
// most three loud margins, and the frame cut splits a rectangle four ways.
constexpr int kMaxElementPieces = kMaxLoudMargins * 4;

// ---------------------------------------------------------------------------
// Rectangle clipping.
// ---------------------------------------------------------------------------

// The overlap of two rectangles, or the empty rectangle.
BgRect intersectRect(const BgRect& a, const BgRect& b) {
  const int x0 = a.x > b.x ? a.x : b.x;
  const int y0 = a.y > b.y ? a.y : b.y;
  const int x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
  const int y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
  if (x1 <= x0 || y1 <= y0) {
    return BgRect{0, 0, 0, 0};
  }
  return BgRect{x0, y0, x1 - x0, y1 - y0};
}

// The pieces of `r` that fall outside `hole`, at most four: the band above,
// the band below, and the two side bands beside the hole. The pieces are
// pairwise disjoint, so their paint order carries no meaning.
int subtractRect(const BgRect& r, const BgRect& hole, BgRect* out) {
  const BgRect cut = intersectRect(r, hole);
  if (cut.w <= 0 || cut.h <= 0) {
    out[0] = r;
    return 1;
  }
  int count = 0;
  if (cut.y > r.y) {
    out[count++] = BgRect{r.x, r.y, r.w, cut.y - r.y};
  }
  const int cutBottom = cut.y + cut.h;
  const int rBottom = r.y + r.h;
  if (rBottom > cutBottom) {
    out[count++] = BgRect{r.x, cutBottom, r.w, rBottom - cutBottom};
  }
  if (cut.x > r.x) {
    out[count++] = BgRect{r.x, cut.y, cut.x - r.x, cut.h};
  }
  const int cutRight = cut.x + cut.w;
  const int rRight = r.x + r.w;
  if (rRight > cutRight) {
    out[count++] = BgRect{cutRight, cut.y, rRight - cutRight, cut.h};
  }
  return count;
}

// ---------------------------------------------------------------------------
// The fade tint. One fade frame draws exactly one design - the outgoing one
// while the fade's first half runs, the incoming one after - with every
// color stepped toward the midpoint of the two base fields. The first step
// (parts 0) reproduces the outgoing frame exactly and the last reproduces the
// incoming frame exactly, so a fade never pops; every step between moves each
// channel by whole integers only, and a blend of two colors that both sit at
// or below a cap stays at or below that cap componentwise.
// ---------------------------------------------------------------------------

class TintPainter final : public Painter {
 public:
  TintPainter(Painter& sink, BgColor toward, int parts, int whole)
      : sink_(sink), toward_(toward), parts_(parts), whole_(whole) {}

  void fill(BgColor color, int x, int y, int w, int h) override {
    sink_.fill(mixToward(color, toward_, parts_, whole_), x, y, w, h);
  }

 private:
  Painter& sink_;
  BgColor toward_;
  int parts_;
  int whole_;
};

// ---------------------------------------------------------------------------
// The catalog. Every value restates an approved design record of
// docs/backgrounds/level-XX.md (owner approval 2026-11-05); the chapter
// tasks T079 through T083 land the grammars that draw from these records.
// ---------------------------------------------------------------------------

constexpr Chapter chapterOf(int level) {
  if (level <= 4) {
    return Chapter::Still;
  }
  if (level <= 8) {
    return Chapter::Stir;
  }
  if (level <= 12) {
    return Chapter::Build;
  }
  if (level <= 16) {
    return Chapter::Pressure;
  }
  return Chapter::Peak;
}

constexpr MotionSpec stillMotion() { return MotionSpec{}; }

constexpr MotionSpec driftSpec(Direction direction, int speedMilli,
                               int cycleMs) {
  return MotionSpec{MotionKind::Drift, direction, speedMilli, cycleMs};
}

constexpr MotionSpec scrollSpec(Direction direction, int speedMilli,
                                int cycleMs) {
  return MotionSpec{MotionKind::Scroll, direction, speedMilli, cycleMs};
}

constexpr MotionSpec pulseSpec(Direction direction, int speedMilli,
                               int cycleMs) {
  return MotionSpec{MotionKind::Pulse, direction, speedMilli, cycleMs};
}

constexpr TransitionSpec cutTransition() {
  return TransitionSpec{TransitionKind::Cut, 0, 0};
}

constexpr TransitionSpec fadeTransition(int durationMs, int steps) {
  return TransitionSpec{TransitionKind::Fade, durationMs, steps};
}

constexpr Design designOf(int level, const char* name, std::uint32_t base,
                          std::uint32_t secondary, std::uint32_t accent,
                          const MotionSpec& motion,
                          const TransitionSpec& transition,
                          BudgetClass budget, int intensityTenths) {
  return Design{level, name, chapterOf(level), colorOf(base),
                colorOf(secondary), colorOf(accent), motion, transition,
                budget, intensityTenths};
}

constexpr Design kCatalog[kDesignCount] = {
    designOf(1, "Still ground", 0x10121A, 0x0C0E16, 0x283044, stillMotion(),
             cutTransition(), BudgetClass::BandElements, 10),
    designOf(2, "First light", 0x10131C, 0x1C2433, 0x2A3146, stillMotion(),
             fadeTransition(400, 12), BudgetClass::SmallElements, 14),
    designOf(3, "Distant stars", 0x0E121C, 0x1B2230, 0x64718C, stillMotion(),
             fadeTransition(500, 15), BudgetClass::SmallElements, 18),
    designOf(4, "Quiet strata", 0x11141D, 0x222A39, 0x2A3146, stillMotion(),
             fadeTransition(500, 15), BudgetClass::BandElements, 22),
    designOf(5, "Slow drift", 0x0F1420, 0x20283A, 0x687892,
             driftSpec(Direction::Down, 2000, 272000), cutTransition(),
             BudgetClass::SmallElements, 26),
    designOf(6, "Dither dusk", 0x121620, 0x242B3A, 0x38445B,
             driftSpec(Direction::Up, 2000, 278000),
             fadeTransition(500, 15), BudgetClass::SmallElements, 30),
    designOf(7, "Sleeping skyline", 0x11151F, 0x252D3D, 0x64728A,
             pulseSpec(Direction::None, 250, 8000), fadeTransition(500, 15),
             BudgetClass::SmallElements, 34),
    designOf(8, "Low signal", 0x11151F, 0x242C3B, 0x54617A,
             scrollSpec(Direction::Down, 3000, 3300), cutTransition(),
             BudgetClass::SmallElements, 38),
    designOf(9, "Fracture", 0x10151F, 0x283044, 0x707D96,
             driftSpec(Direction::Down, 3000, 181300),
             fadeTransition(500, 15), BudgetClass::SmallElements, 42),
    designOf(10, "Circuit hush", 0x111721, 0x283144, 0x6B8498,
             pulseSpec(Direction::None, 290, 7000), fadeTransition(500, 15),
             BudgetClass::SmallElements, 46),
    designOf(11, "Band aurora", 0x131725, 0x283044, 0x67758D,
             driftSpec(Direction::Up, 4000, 161250), fadeTransition(600, 18),
             BudgetClass::BandElements, 50),
    designOf(12, "Long trails", 0x111620, 0x293144, 0x76849B,
             driftSpec(Direction::DownLeft, 4000, 90700), cutTransition(),
             BudgetClass::SmallElements, 54),
    designOf(13, "Deep grid", 0x121722, 0x293145, 0x79869B,
             scrollSpec(Direction::Down, 5000, 108800),
             fadeTransition(600, 18), BudgetClass::SmallElements, 58),
    designOf(14, "Fine snow", 0x131923, 0x293145, 0x7A879E,
             driftSpec(Direction::Down, 6000, 90700), cutTransition(),
             BudgetClass::SmallElements, 62),
    designOf(15, "Undertow", 0x131923, 0x2A3146, 0x74839B,
             scrollSpec(Direction::Left, 6000, 13300),
             fadeTransition(600, 18), BudgetClass::BandElements, 66),
    designOf(16, "Slow pulse", 0x141A26, 0x2A3146, 0x7C88A0,
             pulseSpec(Direction::Out, 6000, 8000), fadeTransition(600, 18),
             BudgetClass::SmallElements, 70),
    designOf(17, "Rising ember", 0x17191F, 0x2A3040, 0x897A72,
             driftSpec(Direction::Up, 7000, 77700), cutTransition(),
             BudgetClass::SmallElements, 77),
    designOf(18, "Storm lines", 0x131925, 0x2A3146, 0x8290A7,
             driftSpec(Direction::Down, 8000, 68000), cutTransition(),
             BudgetClass::SmallElements, 84),
    designOf(19, "Compression", 0x171C28, 0x2A3146, 0x8591A8,
             pulseSpec(Direction::In, 9000, 12000), fadeTransition(500, 15),
             BudgetClass::SmallElements, 92),
    designOf(20, "Collapse", 0x191E2B, 0x2A3146, 0x8A94AC,
             driftSpec(Direction::Down, 10000, 80200),
             fadeTransition(600, 18), BudgetClass::SmallElements, 100),
};

// ---------------------------------------------------------------------------
// The per-design grammars. Each design paints through paintBase() and
// paintElement(), so the clipping and the brightness placement of the layout
// contract hold for every chapter alike.
//
// Task T078 shipped the base field of every design. The chapter tasks land
// the pattern grammars here - T079 Still (1 through 4), T080 Stir (5 through
// 8), T081 Build (9 through 12), T082 Pressure (13 through 16), T083 Peak
// (17 through 20) - from the exact sizes, placements, seeds, and counts
// their design records state, in docs/backgrounds/level-XX.md and the
// reference grammars of tools/backgrounds/designs.py.
// ---------------------------------------------------------------------------

// Python's round(): halves to even. Every size formula a record states is
// written in that arithmetic (round(width / 6), round(height * share)), so
// the port rounds the same way and lands on the same whole pixels. No
// formula of the catalog sits on an exact half; nearbyint keeps the
// equivalence even if one ever does.
int roundTo(double value) { return static_cast<int>(std::nearbyint(value)); }

// The milli-pixel form of the same rounding: one value in millionths of a
// pixel, rounded to whole pixels with the exact halves going to the even
// neighbor. The moving grammars of the Stir chapter and later carry their
// displacement at this precision through the wrap and round once at the
// end, so the port lands where the design package's float arithmetic lands
// for every whole-millisecond clock - including the exact halves a float
// can misrepresent, which the integer path below cannot.
int roundMilliPx(std::int64_t milli) {
  const std::int64_t whole = milli / 1000000;
  const std::int64_t rest = milli % 1000000;
  const bool up = rest > 500000 || (rest == 500000 && (whole & 1) != 0);
  return static_cast<int>(whole + (up ? 1 : 0));
}

// One coordinate moved by `speedMilli` milli-pixels per second over `tMs`
// and wrapped over `span` pixels: the drift() of tools/backgrounds/
// designs.py. The displacement survives the wrap at milli-pixel precision
// and rounds once at the end, so a star at 2 px/s sits still through the
// first 250 ms of its step and never drifts a different sub-pixel than the
// package's plates.
int driftWrapped(int base, int home, int span, int speedMilli,
                 std::uint32_t tMs) {
  const std::int64_t milli = (static_cast<std::int64_t>(base) - home) *
                                 1000000 +
                             static_cast<std::int64_t>(speedMilli) *
                                 static_cast<std::int64_t>(tMs);
  const std::int64_t modulus = static_cast<std::int64_t>(span) * 1000000;
  std::int64_t wrapped = milli % modulus;  // the span is positive
  if (wrapped < 0) {
    wrapped += modulus;  // Python's modulo never goes below zero
  }
  // The rounding sees home + wrapped, the value the package rounds: the
  // even neighbor of an exact half depends on the margin origin too.
  return roundMilliPx(static_cast<std::int64_t>(home) * 1000000 + wrapped);
}

// True when the whole rectangle lies inside one loud margin: the fit test
// the design package applies before drawing a dot or a stratum end, so a
// shape never crosses out of the margin the contract gave it.
bool fitsLoud(const Zones& zones, int x, int y, int w, int h) {
  for (int index = 0; index < zones.loudCount; ++index) {
    const BgRect& margin = zones.loud[index];
    if (x >= margin.x && y >= margin.y && x + w <= margin.x + margin.w &&
        y + h <= margin.y + margin.h) {
      return true;
    }
  }
  return false;
}

// Python's floor division and modulo over the step indices of the band
// expansion below, whose first index runs negative when the scroll offset
// is positive: (0 - 14) // 40 is -1 there, not 0.
int floorDiv(int value, int divisor) {
  const int quotient = value / divisor;
  const int remainder = value % divisor;
  return remainder != 0 && ((remainder < 0) != (divisor < 0)) ? quotient - 1
                                                             : quotient;
}

int pyMod(int value, int modulus) {
  const int remainder = value % modulus;
  return remainder < 0 ? remainder + modulus : remainder;
}

// Level 1, Still ground: four faint dark corner steps and nothing else.
// Into each corner two nested steps of the secondary ink: an outer
// rectangle of round(width / 6) x round(height / 4.5) touching the two
// canvas edges, then an inner one of 62 percent of those sizes touching the
// same corner.
void stillGround(const Design& design, const Zones& zones, Painter& painter) {
  const BgColor ink = design.secondary;
  const int outerW = roundTo(static_cast<double>(zones.canvasWidth) / 6.0);
  const int outerH = roundTo(static_cast<double>(zones.canvasHeight) / 4.5);
  const int innerW = roundTo(outerW * 0.62);
  const int innerH = roundTo(outerH * 0.62);
  for (int corner = 0; corner < 4; ++corner) {
    const bool right = (corner & 1) != 0;
    const bool bottom = (corner & 2) != 0;
    paintElement(zones, painter, ink, right ? zones.canvasWidth - outerW : 0,
                 bottom ? zones.canvasHeight - outerH : 0, outerW, outerH);
    paintElement(zones, painter, ink,
                 right ? zones.canvasWidth - innerW : 0,
                 bottom ? zones.canvasHeight - innerH : 0, innerW, innerH);
  }
}

// Level 2, First light: one fine dim rule low on the canvas with two
// broader darker under-bands. The rule row lands on a multiple of 8, so the
// one-eighth review scale of the thumbnail sheet samples it directly.
void firstLight(const Design& design, const Zones& zones, Painter& painter) {
  const int rule = roundTo(zones.canvasHeight * 0.82) / 8 * 8;
  paintElement(zones, painter, design.accent, 0, rule, zones.canvasWidth, 1);
  paintElement(zones, painter, design.secondary, 0, rule + 8,
               zones.canvasWidth, 3);
  paintElement(zones, painter, design.secondary, 0, rule + 16,
               zones.canvasWidth, 4);
}

// One dot shape and its weight of 100, the weighted size pick of the
// design package's dot grammars.
struct DotShape {
  int weight;
  int w;
  int h;
};

// The ceiling of the dot grids: the catalog's finest cell is 16 px, whose
// widest grid - the two 192 x 544 landscape margins - holds 816 cells, so
// 1024 covers every grammar the catalog states.
constexpr int kMaxDotCandidates = 1024;

// The candidate rank of one dot-grid cell: the h32() of the cell alone, or
// - under biasOuter - the pair (outer-edge closeness, hash) of collapse,
// whose debris piles against the far canvas edges. The closeness is the
// rational min(cx, cols - 1 - cx) / max(1, cols - 1) ranked at hundredths,
// the round(inner * 100) of the package, with the exact halves rounded to
// even the same way Python rounds them (no denominator of the catalog's dot
// grids ever sits on one).
int outerCloseness(int cx, int cols) {
  const int inner = cx < cols - 1 - cx ? cx : cols - 1 - cx;
  const int denominator = cols - 1 > 0 ? cols - 1 : 1;
  const int numerator = inner * 100;
  int quotient = numerator / denominator;
  const int remainder = numerator % denominator;
  if (2 * remainder > denominator ||
      (2 * remainder == denominator && (quotient & 1) != 0)) {
    ++quotient;
  }
  return quotient;
}


// One star field placed by hash rank: the `count` cells of the `cell` px
// grid inside the loud margins with the lowest h32(seed, margin_x, cx, cy)
// rank, each drawn in a hashed shape at a hashed jitter inside its cell. A
// dot whose jitter would leave its margin is skipped and the next-ranked
// cell takes its place, so the count stays exact in both orientations.
//
// This is the exact_dots() of tools/backgrounds/designs.py: the walk order
// is the package's stable sort by (closeness, rank, enumeration), which the
// bounded insertion below reproduces without sorting the whole grid or
// allocating: a frame walks fixed-size stack state only. A dot field with a
// speed in milli-pixels per second drifts inside its margin over the margin
// span plus `extentY` on the fall axis - a dot wider than 4 px wraps over a
// span that includes its own extent, so it leaves its margin fully before it
// re-enters, and a dot of 4 px or less may pass extentY 0 and cross
// sub-perceptually - with the placement itself never moving: the fit test
// runs on the undrifted cell.
void exactDots(const Zones& zones, Painter& painter, std::uint32_t seed,
               int count, int cell, const DotShape* shapes, int shapeCount,
               const BgColor* colors, int colorCount, int speedMilliX,
               int speedMilliY, std::uint32_t timeMs, bool biasOuter,
               int extentY) {
  struct Candidate {
    int closeness;  // the outer-edge rank of biasOuter, 0 otherwise
    std::uint32_t rank;
    int order;
    int margin;
    int cx;
    int cy;
  };
  const auto rankBefore = [](const Candidate& a, const Candidate& b) {
    if (a.closeness != b.closeness) {
      return a.closeness < b.closeness;
    }
    return a.rank != b.rank ? a.rank < b.rank : a.order < b.order;
  };

  // The `count` best candidates by (closeness, rank, order), kept sorted.
  Candidate best[kMaxSmallElements];
  int kept = 0;
  int order = 0;
  bool full = false;
  for (int margin = 0; margin < zones.loudCount && !full; ++margin) {
    const BgRect& home = zones.loud[margin];
    const int cols = home.w / cell;
    const int rows = home.h / cell;
    for (int cy = 0; cy < rows && !full; ++cy) {
      for (int cx = 0; cx < cols; ++cx) {
        if (order >= kMaxDotCandidates) {
          full = true;  // past the ceiling no catalog grammar reaches
          break;
        }
        const Candidate candidate{
            biasOuter ? outerCloseness(cx, cols) : 0,
            hash32(seed, home.x, cx, cy), order++, margin, cx, cy};
        if (kept == count) {
          if (!rankBefore(candidate, best[count - 1])) {
            continue;  // not among the best
          }
          --kept;  // the worst falls out
        }
        int slot = kept;
        while (slot > 0 && rankBefore(candidate, best[slot - 1])) {
          best[slot] = best[slot - 1];
          --slot;
        }
        best[slot] = candidate;
        ++kept;
      }
    }
  }

  // Place in rank order: the first `count` candidates whose jittered dot
  // stays inside its margin.
  int placed = 0;
  for (int index = 0; index < kept && placed < count; ++index) {
    const Candidate& candidate = best[index];
    const BgRect& home = zones.loud[candidate.margin];
    int shapeIndex = shapeCount - 1;
    int cumulative = 0;
    const int pick = static_cast<int>(
        hash32(seed + 1, home.x, candidate.cx, candidate.cy) % 100u);
    for (int shape = 0; shape < shapeCount; ++shape) {
      cumulative += shapes[shape].weight;
      if (pick < cumulative) {
        shapeIndex = shape;
        break;
      }
    }
    const DotShape& shape = shapes[shapeIndex];
    const int roomX = cell - shape.w;
    const int roomY = cell - shape.h;
    const int x =
        home.x + candidate.cx * cell +
        static_cast<int>(hash32(seed + 3, home.x, candidate.cx, candidate.cy) %
                         static_cast<std::uint32_t>(roomX + 1));
    const int y =
        home.y + candidate.cy * cell +
        static_cast<int>(hash32(seed + 4, home.x, candidate.cx, candidate.cy) %
                         static_cast<std::uint32_t>(roomY + 1));
    if (!fitsLoud(zones, x, y, shape.w, shape.h)) {
      continue;  // skipped; the next-ranked cell takes its place
    }
    const BgColor color =
        colors[hash32(seed + 2, home.x, candidate.cx, candidate.cy) %
               static_cast<std::uint32_t>(colorCount)];
    const int drawnX =
        speedMilliX != 0
            ? driftWrapped(x, home.x, home.w, speedMilliX, timeMs)
            : x;
    const int drawnY = speedMilliY != 0
                           ? driftWrapped(y, home.y, home.h + extentY,
                                          speedMilliY, timeMs)
                           : y;
    paintElement(zones, painter, color, drawnX, drawnY, shape.w, shape.h);
    ++placed;
  }
}

// Level 3, Distant stars: exactly 28 tiny square stars concentrated toward
// the outer margins, 1 x 1 px in 55 percent of the cells and 2 x 2 in the
// rest, secondary or accent in equal share, on the 24 px cell grid the seed
// 0x5355 ranks.
void distantStars(const Design& design, const Zones& zones, Painter& painter) {
  const DotShape shapes[2] = {{55, 1, 1}, {45, 2, 2}};
  const BgColor colors[2] = {design.secondary, design.accent};
  exactDots(zones, painter, 0x5355, 28, 24, shapes, 2, colors, 2, 0, 0, 0,
            false, 0);
}

// Level 4, Quiet strata: eight broad uneven sediment bands stacked from the
// bottom edge upward, brightest reading first, with a stepped end at each
// band end that reaches a loud margin and fits inside it.
void quietStrata(const Design& design, const Zones& zones, Painter& painter) {
  static constexpr double kShares[8] = {0.055, 0.045, 0.060, 0.040,
                                        0.050, 0.035, 0.045, 0.030};
  const BgColor ink = design.secondary;
  int y = zones.canvasHeight;
  for (double share : kShares) {
    const int height = roundTo(zones.canvasHeight * share);
    y -= height + 9;
    paintElement(zones, painter, ink, 0, y, zones.canvasWidth, height);
    for (int margin = 0; margin < zones.loudCount; ++margin) {
      const BgRect& home = zones.loud[margin];
      if (home.y + home.h <= y || home.y >= y + height) {
        continue;  // the margin's rows miss this band
      }
      for (int side = 0; side < 2; ++side) {
        const int stepX = side == 0 ? home.x : home.x + home.w - 6;
        // The step: 6 px wide, 6 px taller than the band, sitting on the
        // band's bottom edge at the margin's outer edge.
        if (fitsLoud(zones, stepX, y - 6, 6, height + 6)) {
          paintElement(zones, painter, ink, stepX, y - 6, 6, height + 6);
        }
      }
    }
  }
}

// Level 5, Slow drift: exactly 56 square stars sinking at 2 px/s over their
// margin spans, with 10 static dust flecks beneath them. The stars wrap at
// the margin span alone - every star is 2 px or less, so the crossing is
// sub-perceptual - and the dust holds still, so the layer beneath the
// moving one is still.
void slowDrift(const Design& design, const Zones& zones, Painter& painter,
               std::uint32_t timeMs) {
  const DotShape starShapes[2] = {{60, 1, 1}, {40, 2, 2}};
  const BgColor starColors[2] = {design.secondary, design.accent};
  exactDots(zones, painter, 0x5356, 56, 20, starShapes, 2, starColors, 2, 0,
            2000, timeMs, false, 0);
  const DotShape dustShapes[1] = {{100, 3, 1}};
  const BgColor dustColors[1] = {design.secondary};
  exactDots(zones, painter, 0x5357, 10, 28, dustShapes, 1, dustColors, 1, 0, 0,
            timeMs, false, 0);
}

// Level 6, Dither dusk: five stepped dusk zones down the side margins - the
// first, third and fifth carrying the secondary ink as one inset band plus
// two 6 px end caps 5 px taller than the zone, the others staying base -
// with two coarse checker patches per zone rising at 2 px/s. A patch is a
// 16 x 12 block of 4 px squares on the even cells of its own grid; it wraps
// over the margin height plus its own 12 px height, so it leaves the canvas
// fully before re-entering below the bottom edge: the rise never pops.
void ditherDusk(const Design& design, const Zones& zones, Painter& painter,
                std::uint32_t timeMs) {
  static constexpr double kShares[5] = {0.08, 0.10, 0.12, 0.14, 0.16};
  const BgColor zone = design.secondary;
  const BgColor ink = design.accent;
  constexpr int kPatchW = 16;
  constexpr int kPatchH = 12;
  int zoneY[5] = {0, 0, 0, 0, 0};
  int zoneH[5] = {0, 0, 0, 0, 0};
  int y = roundTo(zones.canvasHeight * 0.30);
  for (int index = 0; index < 5; ++index) {
    zoneH[index] = roundTo(zones.canvasHeight * kShares[index]);
    zoneY[index] = y;
    y += zoneH[index] + 4;
  }
  // The zones hold still: two loops, one for the ink of both side margins,
  // one for the patches that ride over them, matching the package's
  // emission order.
  for (int margin = 0; margin < zones.loudCount && margin < 2; ++margin) {
    const BgRect& home = zones.loud[margin];
    for (int index = 0; index < 5; index += 2) {
      const int zy = zoneY[index];
      const int zh = zoneH[index];
      for (int side = 0; side < 2; ++side) {
        const int sx = side == 0 ? home.x : home.x + home.w - 6;
        if (fitsLoud(zones, sx, zy - 5, 6, zh + 5)) {
          paintElement(zones, painter, zone, sx, zy - 5, 6, zh + 5);
        }
      }
      paintElement(zones, painter, zone, home.x + 6, zy, home.w - 12, zh);
    }
  }
  for (int margin = 0; margin < zones.loudCount && margin < 2; ++margin) {
    const BgRect& home = zones.loud[margin];
    for (int index = 0; index < 5; ++index) {
      const int zy = zoneY[index];
      const int zh = zoneH[index];
      const int roomY =
          std::max(1, std::min(zh, home.y + home.h - zy - 6) - kPatchH - 6);
      for (int which = 0; which < 2; ++which) {
        const int px =
            home.x + 10 +
            static_cast<int>(hash32(0x6D, home.x, index, which) %
                             static_cast<std::uint32_t>(
                                 std::max(1, home.w - 10 - kPatchW - 10)));
        const int py =
            zy + 6 + static_cast<int>(hash32(0x6E, home.x, index, which) %
                                      static_cast<std::uint32_t>(roomY));
        const int driftY = driftWrapped(py, home.y, home.h + kPatchH, -2000,
                                        timeMs);
        for (int cy = 0; cy < 3; ++cy) {
          for (int cx = 0; cx < 4; ++cx) {
            if ((cx + cy) % 2 != 0) {
              continue;  // the odd cells stay base: the checker reads
            }
            paintElement(zones, painter, ink, px + cx * 4, driftY + cy * 4, 4,
                         4);
          }
        }
      }
    }
  }
}

// Level 7, Sleeping skyline: fourteen stepped tower silhouettes anchored on
// the canvas bottom edge - across its full width in landscape, seven per
// outer side strip in portrait - each with two stepped roof blocks above
// its top, highest at the corners. The windows of the tall towers swell one
// pixel of height and return over the 8 s cycle: the approved window pulse
// as a slow size change, because a brightness pulse would blink. The swell
// is round(triangle(t, 8)) of the package - one through the middle half of
// the cycle, zero at its ends, with the exact halves rounded to even - so
// the rising top edge moves at most 0.25 px/s and the bottom edge holds.
void sleepingSkyline(const Design& design, const Zones& zones,
                     Painter& painter, std::uint32_t timeMs) {
  const BgColor tower = design.secondary;
  const BgColor window = design.accent;
  const int cycleMs = design.motion.cycleMs;
  const std::int64_t through =
      static_cast<std::int64_t>(timeMs) % cycleMs;
  const int swell =
      (4 * through > cycleMs && 4 * through < 3 * cycleMs) ? 1 : 0;
  const bool portrait = zones.canvasWidth < zones.canvasHeight;
  const int slots = 14;
  const int windowFloor = roundTo(zones.canvasHeight * 0.30);
  for (int slot = 0; slot < slots; ++slot) {
    int baseX = 0;
    int width = 0;
    if (portrait) {
      const BgRect& side = zones.loud[slot % 2];
      const int place = slot / 2;
      const double slotW = static_cast<double>(side.w) / 7.0;
      baseX = side.x + roundTo(place * slotW) +
              static_cast<int>(hash32(0x571, side.x, place) %
                               static_cast<std::uint32_t>(
                                   std::max(1, roundTo(slotW * 0.4))));
      width = roundTo(slotW * 0.6);
    } else {
      const double slotW = static_cast<double>(zones.canvasWidth) / 14.0;
      baseX = roundTo(slot * slotW) +
              static_cast<int>(hash32(0x571, 0, slot) %
                               static_cast<std::uint32_t>(
                                   std::max(1, roundTo(slotW * 0.3))));
      width = roundTo(slotW * 0.62);
    }
    // The height share rises toward the corners: the towers frame the
    // board instead of crowding it.
    const double center =
        std::min(baseX, zones.canvasWidth - baseX) /
        std::max(1.0, static_cast<double>(zones.canvasWidth) / 2.0);
    const double share =
        0.10 + 0.24 * (1.0 - center) +
        static_cast<double>(hash32(0x572, slot) % 100u) / 1000.0;
    const int height = roundTo(zones.canvasHeight * share);
    const int top = zones.canvasHeight - height;
    paintElement(zones, painter, tower, baseX, top, width, height);
    paintElement(zones, painter, tower, baseX + roundTo(width * 0.2), top - 8,
                 roundTo(width * 0.6), 8);
    paintElement(zones, painter, tower, baseX + roundTo(width * 0.35),
                 top - 14, roundTo(width * 0.3), 6);
    if (height < windowFloor) {
      continue;  // the short towers of the middle sleep dark
    }
    int columns[2] = {0, 0};
    int columnCount = 1;
    if (width >= 24) {
      columns[0] = baseX + 4;
      columns[1] = baseX + width - 8;
      columnCount = 2;
    } else {
      columns[0] = baseX + std::max(1, (width - 3) / 2);
    }
    for (int wy = top + 10; wy < top + height / 2; wy += 14) {
      for (int column = 0; column < columnCount; ++column) {
        if (fitsLoud(zones, columns[column], wy - swell, 3, 4 + swell)) {
          paintElement(zones, painter, window, columns[column], wy - swell, 3,
                       4 + swell);
        }
      }
    }
  }
}

// Level 8, Low signal: two scanline bursts per loud margin - 1 px rules of
// the secondary every 10 rows over an uneven x run inside an uneven y band,
// so the runs read as broken transmissions - plus two solid accent bars in
// the first two margins. The rule set scrolls down 3 px/s: the whole set
// shifts by round(3t) mod 10 inside its fixed band, a texture shift that
// repeats every 10/3 s, and the bars hold still.
void lowSignal(const Design& design, const Zones& zones, Painter& painter,
               std::uint32_t timeMs) {
  const BgColor line = design.secondary;
  const BgColor bar = design.accent;
  const int offset = roundMilliPx(
                         static_cast<std::int64_t>(3000) * timeMs) %
                     10;
  for (int margin = 0; margin < zones.loudCount; ++margin) {
    const BgRect& home = zones.loud[margin];
    for (int group = 0; group < 2; ++group) {
      const int bandH = 80 +
                        static_cast<int>(hash32(0x86, home.x, group) % 70u);
      const int bandY =
          home.y + 8 +
          static_cast<int>(hash32(0x87, home.x, group) %
                           static_cast<std::uint32_t>(
                               std::max(1, home.h - bandH - 16)));
      int spanW = 44 + static_cast<int>(hash32(0x88, home.x, group) %
                                        static_cast<std::uint32_t>(
                                            std::max(1, home.w - 52)));
      const int spanX =
          home.x + 6 +
          static_cast<int>(hash32(0x89, home.x, group) %
                           static_cast<std::uint32_t>(
                               std::max(1, home.w - spanW - 12)));
      if (spanX + spanW > home.x + home.w - 6) {
        spanW = home.x + home.w - 6 - spanX;
      }
      const int lines = bandH / 10;
      for (int index = 0; index < lines; ++index) {
        const int ruleY = bandY + (index * 10 + offset) % bandH;
        paintElement(zones, painter, line, spanX, ruleY, spanW, 1);
      }
    }
  }
  for (int margin = 0; margin < zones.loudCount && margin < 2; ++margin) {
    const BgRect& home = zones.loud[margin];
    paintElement(zones, painter, bar, home.x + 6, home.y + 14, home.w - 12, 3);
    paintElement(zones, painter, bar, home.x + 6, home.y + home.h - 40,
                 home.w - 12, 3);
  }
}

// One full-width staircase band of the design package: steps of `stepW`
// whose lift runs 0, 1, 2, 1 times `stepH`, dropped `drop` px and scrolled
// `offset` px, with a `phase` of 2 mirroring the contour to 2, 1, 0, 1.
// This is the band expansion of tools/backgrounds/designs.py - the segment
// walk runs one step past both ends so the staircase covers the full width
// whatever the offset - drawn through paintElement(), so the frame cut and
// the canvas clip shape it like any other element.
void staircaseBand(const Zones& zones, Painter& painter, BgColor color, int y,
                   int height, int stepW, int stepH, int drop, int offset,
                   int phase) {
  static constexpr int kLift[4] = {0, 1, 2, 1};
  const int end = zones.canvasWidth;
  const int first = floorDiv(0 - offset, stepW) - 1;
  const int last = floorDiv(end - offset, stepW) + 1;
  for (int step = first; step <= last; ++step) {
    const int left =
        step == first ? 0 : std::max(0, step * stepW + offset);
    const int right = step == last
                          ? end
                          : std::min(end, (step + 1) * stepW + offset);
    if (right <= left) {
      continue;
    }
    const int lift = kLift[pyMod(step + phase, 4)] * stepH;
    paintElement(zones, painter, color, left, y - lift + drop, right - left,
                 height + lift);
  }
}

// Level 9, Fracture: two stepped diagonal seams in the far margins - each
// one staircase of twelve secondary blocks walking from the margin's outer
// top corner toward its bottom inner corner, each block step-width wide
// (margin width / 12 + 1) and 5 px thick - with a darker offset ledge, a
// 22 px tall staircase of the same ink shifted 12 px toward the canvas
// center. Beside every step, on both sides of the seam, sits one accent
// tick 2 x 2 px at a hashed depth of 6 to 15 px below the step; the ticks
// sink 3 px/s and wrap at their margin span, a crossing of 2 px marks the
// contract calls sub-perceptual, while both staircases hold still.
void fracture(const Design& design, const Zones& zones, Painter& painter,
              std::uint32_t timeMs) {
  const BgColor seam = design.secondary;
  const BgColor tick = design.accent;
  constexpr int kSteps = 12;
  for (int which = 0; which < 2; ++which) {
    const BgRect& margin = zones.loud[which];
    const int stepW =
        roundTo(static_cast<double>(margin.w) / kSteps) + 1;
    for (int index = 0; index < kSteps; ++index) {
      const double share = index / static_cast<double>(kSteps);
      int x = 0;
      int ledgeX = 0;
      if (which == 0) {
        x = margin.x + roundTo(margin.w * share);
        x = std::min(x, margin.x + margin.w - stepW);
        ledgeX = std::min(x + 12, margin.x + margin.w - stepW);
      } else {
        x = margin.x + margin.w - roundTo(margin.w * share) - stepW;
        x = std::max(x, margin.x);
        ledgeX = std::max(x - 12, margin.x);
      }
      const int y = roundTo(margin.h * share) + margin.y;
      paintElement(zones, painter, seam, x, y, stepW, 5);
      paintElement(zones, painter, seam, ledgeX, y, stepW, 22);
      for (int side = 0; side < 2; ++side) {
        const int sideSign = side == 0 ? 1 : -1;
        const int chipX = x + (sideSign > 0 ? stepW + 2 : -4);
        const int chipY =
            y + 6 +
            static_cast<int>(hash32(0x9F, which, index, sideSign) % 10u);
        if (!fitsLoud(zones, chipX, chipY, 2, 2)) {
          continue;  // the tick would leave its margin: no tick there
        }
        const int drawnY =
            driftWrapped(chipY, margin.y, margin.h, 3000, timeMs);
        paintElement(zones, painter, tick, chipX, drawnY, 2, 2);
      }
    }
  }
}

// Level 10, Circuit hush: sixteen right-angle circuit paths, eight per
// side margin, one per band of margin height / 8 starting 8 px into the
// band. A path is a horizontal run of the secondary, 2 px thick and 24 to
// 71 px long at a hashed x; a vertical drop 16 to 39 px tall at the run's
// right end; a second run 8 to 71 px back toward the margin edge; and a
// closing drop 14 to 35 px tall - four trace rectangles per path. Every
// path closes with a 4 x 4 px square node at the second drop's foot, and
// every third node carries a lit core of the accent that swells from 2 x 2
// to 3 x 3 px over the 7 s cycle: the approved node pulse as a slow size
// change. No traces in the well, no letters, no arrowheads.
void circuitHush(const Design& design, const Zones& zones, Painter& painter,
                 std::uint32_t timeMs) {
  const BgColor trace = design.secondary;
  const BgColor lit = design.accent;
  const int cycleMs = design.motion.cycleMs;
  const std::int64_t through =
      static_cast<std::int64_t>(timeMs) % cycleMs;
  const int swell =
      (4 * through > cycleMs && 4 * through < 3 * cycleMs) ? 1 : 0;
  constexpr int kPaths = 16;
  constexpr int kRows = kPaths / 2;
  for (int index = 0; index < kPaths; ++index) {
    const BgRect& home = zones.loud[index % 2];  // the side strips
    const int row = index / 2;
    const int baseY = home.y + 8 + row * (home.h - 70) / kRows;
    const int run = 24 + static_cast<int>(hash32(0xA1, home.x, row) % 48u);
    const int x0 =
        home.x + 4 +
        static_cast<int>(hash32(0xA2, home.x, row) %
                         static_cast<std::uint32_t>(
                             std::max(1, home.w - run - 8)));
    const int fall = 16 + static_cast<int>(hash32(0xA3, home.x, row) % 24u);
    paintElement(zones, painter, trace, x0, baseY, run, 2);
    paintElement(zones, painter, trace, x0 + run - 2, baseY, 2, fall);
    const int back = 8 + static_cast<int>(
                             hash32(0xA4, home.x, row) %
                             static_cast<std::uint32_t>(std::max(1, run - 10)));
    paintElement(zones, painter, trace, x0 + run - back, baseY + fall - 2,
                 back, 2);
    const int drop2 = 14 + static_cast<int>(hash32(0xA5, home.x, row) % 22u);
    paintElement(zones, painter, trace, x0 + run - back, baseY + fall - 2, 2,
                 drop2);
    const int nodeY = baseY + fall + drop2 - 6;
    if (nodeY + 4 > home.y + home.h) {
      continue;  // the node would leave its margin: the path stays open
    }
    const int nodeX = x0 + run - back - 1;
    paintElement(zones, painter, trace, nodeX, nodeY, 4, 4);
    if (index % 3 == 0) {
      paintElement(zones, painter, lit, nodeX - swell / 2, nodeY - swell / 2,
                   2 + swell, 2 + swell);
    }
  }
}

// Level 11, Band aurora: three broad stepped ribbons rising together as
// one layer. Each ribbon sits at a base row of 16, 38, then 60 percent of
// the canvas height and is three overlapping staircase bands - bodies of
// round(height * 0.035) rows, offset 6 px per layer, staircase steps of
// 40 px with an 8 px lift, the drop and phase alternating per layer and
// ribbon so the edges cascade in opposing contours. Eight accent ticks of
// 2 x 2 px ride with the ribbons at hashed positions inside the loud
// margins.
//
// The whole layer rises 4 px/s. A ribbon wraps over the canvas height plus
// its own extent - the three bands, their offsets, and the tallest lift,
// 101 px landscape and 146 px portrait - with the wrap phase anchored one
// extent below the base row, so a ribbon that has left the top edge
// re-enters at the bottom edge rather than popping back at its base row:
// the seamless rise the record states (161.25 s per pass landscape, 276.5
// s portrait; the contract correction task T081 recorded in
// docs/backgrounds/level-11.md).
void bandAurora(const Design& design, const Zones& zones, Painter& painter,
                std::uint32_t timeMs) {
  const BgColor ribbon = design.secondary;
  const BgColor tick = design.accent;
  const int body = roundTo(zones.canvasHeight * 0.035);
  const int extent = 3 * (body + 6) + 2 * 8 + 10;
  const int span = zones.canvasHeight + extent;
  for (int index = 0; index < 3; ++index) {
    const int baseY =
        roundTo(zones.canvasHeight * (0.16 + 0.22 * index));
    const int y =
        driftWrapped(baseY + extent, 0, span, -4000, timeMs) - extent;
    for (int layer = 0; layer < 3; ++layer) {
      staircaseBand(zones, painter, ribbon, y + layer * (body + 6), body, 40,
                    8, index * 5 + layer * 2, 0, ((index + layer) % 2) * 2);
    }
  }
  for (int index = 0; index < 8; ++index) {
    const BgRect& home = zones.loud[index % zones.loudCount];
    const int tx =
        home.x + 8 +
        static_cast<int>(hash32(0xB1, home.x, index) %
                         static_cast<std::uint32_t>(std::max(1, home.w - 18)));
    const int ty =
        home.y + 10 +
        static_cast<int>(hash32(0xB2, home.x, index) %
                         static_cast<std::uint32_t>(std::max(1, home.h - 20)));
    // The ticks ride the whole canvas, wrapping over its height plus their
    // own 2 px, and draw only while they sit inside a loud margin.
    const int y = driftWrapped(ty, 0, zones.canvasHeight + 2, -4000, timeMs);
    if (!fitsLoud(zones, tx, y, 2, 2)) {
      continue;
    }
    paintElement(zones, painter, tick, tx, y, 2, 2);
  }
}

// Level 12, Long trails: seventy-two dim square stars on the 20 px cell
// grid the seed 0xC12 ranks - 1 x 1 px in 55 percent of the cells, 2 x 2 in
// the rest, secondary or accent in equal share - and twelve staircase
// meteor trails, alternating between the loud margins, each at a hashed
// position between 0 and 79 percent across and 0 and 89 percent down its
// margin. A trail is six steps of the secondary, each 3 x 1 px, stepping
// 4 px left and 2 px down per segment so the trail points to the upper
// right, closed by one 2 x 2 px accent head above the first step, dropped
// when it would leave the margin. Stars and trails share one velocity,
// 2.4 px/s left and 3.2 px/s down, wrapping inside their margin.
void longTrails(const Design& design, const Zones& zones, Painter& painter,
                std::uint32_t timeMs) {
  const DotShape starShapes[2] = {{55, 1, 1}, {45, 2, 2}};
  const BgColor starColors[2] = {design.secondary, design.accent};
  exactDots(zones, painter, 0xC12, 72, 20, starShapes, 2, starColors, 2,
            -2400, 3200, timeMs, false, 0);
  for (int index = 0; index < 12; ++index) {
    const BgRect& home = zones.loud[index % zones.loudCount];
    const double across =
        static_cast<double>(hash32(0xC3A, index) % 80u) / 100.0;
    const double down =
        static_cast<double>(hash32(0xC3B, index) % 90u) / 100.0;
    const int x = home.x + roundTo(home.w * across);
    const int y = home.y + roundTo(home.h * down);
    for (int segment = 0; segment < 6; ++segment) {
      const int stepX = driftWrapped(x - segment * 4, home.x, home.w, -2400,
                                     timeMs);
      const int stepY = driftWrapped(y + segment * 2, home.y, home.h, 3200,
                                     timeMs);
      paintElement(zones, painter, design.secondary, stepX, stepY, 3, 1);
    }
    const int headX = driftWrapped(x - 1, home.x, home.w, -2400, timeMs);
    const int headY = driftWrapped(y - 3, home.y, home.h, 3200, timeMs);
    if (!fitsLoud(zones, headX, headY, 2, 2)) {
      continue;
    }
    paintElement(zones, painter, design.accent, headX, headY, 2, 2);
  }
}

// mid-height horizon - twelve horizontal 1 px rules of the secondary at the
// mid row +/- the offsets 6, 11, 18, 27, 38 and 51 px, so the spacing
// tightens toward the horizon, one rule inside every tall loud margin (and
// one static rule inside a short margin that holds the row) - plus ten
// vertical 2 px guides, five in each of the first two margins, each in four
// quarter-height segments that step 1 px toward the canvas center so the
// guides converge in staircases, each closed by one 2 x 2 px accent node at
// its foot. The rules scroll down 5 px/s over their margin span as one
// rigid set - every rule shares one velocity, so the spacing pattern
// translates without deforming - and the guides and nodes hold still.
void deepGrid(const Design& design, const Zones& zones, Painter& painter,
              std::uint32_t timeMs) {
  const BgColor line = design.secondary;
  const BgColor node = design.accent;
  const int mid = zones.canvasHeight / 2;
  const int scroll = roundMilliPx(5000LL * timeMs);
  static const int kOffsets[6] = {6, 11, 18, 27, 38, 51};
  for (int away : kOffsets) {
    for (int sign = -1; sign <= 1; sign += 2) {
      const int y0 = mid + sign * away;
      if (y0 < 0 || y0 >= zones.canvasHeight) {
        continue;
      }
      for (int margin = 0; margin < zones.loudCount; ++margin) {
        const BgRect& home = zones.loud[margin];
        if (home.h > 100) {
          // A tall side margin: the rule scrolls over its span.
          const int y = home.y + pyMod(y0 - home.y + scroll, home.h);
          paintElement(zones, painter, line, home.x, y, home.w, 1);
        } else if (y0 >= home.y && y0 < home.y + home.h) {
          paintElement(zones, painter, line, home.x, y0, home.w, 1);
        }
      }
    }
  }
  for (int margin = 0; margin < zones.loudCount && margin < 2; ++margin) {
    const BgRect& home = zones.loud[margin];
    for (int guide = 0; guide < 5; ++guide) {
      const int x = home.x + 6 + floorDiv(guide * (home.w - 14), 5);
      const int toward = x < zones.canvasWidth / 2 ? 1 : -1;
      const int segmentH = home.h / 4;
      for (int step = 0; step < 4; ++step) {
        paintElement(zones, painter, line, x + toward * step,
                     home.y + step * segmentH, 2, segmentH);
      }
      if (fitsLoud(zones, x, home.y + home.h - 6, 2, 2)) {
        paintElement(zones, painter, node, x, home.y + home.h - 6, 2, 2);
      }
    }
  }
}

// Level 14, Fine snow: exactly 160 tiny square particles falling over
// three dark bands. The particles are the 160 cells of the 20 px grid the
// seed 0xE14 ranks lowest inside the loud margins - 1 x 1 px in 55 percent,
// 2 x 2 in the rest, secondary or accent in equal share - and the layer
// falls 6 px/s, wrapping at its margin span (every particle is 2 px or
// less, so the crossing is sub-perceptual). The bands - full width, 3.5
// percent of the canvas height, at 30, 55 and 80 percent height - hold
// still and paint over the field, exactly the package's emission order.
void fineSnow(const Design& design, const Zones& zones, Painter& painter,
              std::uint32_t timeMs) {
  const DotShape shapes[2] = {{55, 1, 1}, {45, 2, 2}};
  const BgColor colors[2] = {design.secondary, design.accent};
  exactDots(zones, painter, 0xE14, 160, 20, shapes, 2, colors, 2, 0, 6000,
            timeMs, false, 0);
  const int bandH = roundTo(zones.canvasHeight * 0.035);
  static const double kShares[3] = {0.30, 0.55, 0.80};
  for (double share : kShares) {
    paintElement(zones, painter, design.secondary, 0,
                 roundTo(zones.canvasHeight * share), zones.canvasWidth,
                 bandH);
  }
}

// Level 15, Undertow: seven interlocking stepped wave bands sliding left
// as one layer. Each band is one full-width staircase - a body of
// round(height * 0.07) rows at base row round(height * (0.28 + 0.09 i)),
// 20 px steps with a 6 px lift (an 80 px period), dropped 3 px per index,
// alternate bands carrying the mirrored phase so the stepped edges read as
// two opposing currents while every rectangle shares one velocity. The
// whole layer slides left 6 px/s - the record, the approved concept, and
// the declared velocity all name the leftward slide, and the corrected
// generator offset advances negatively - wrapping every 80 px, a texture
// scroll that cannot jump. Two foam ticks of accent, 2 x 1 px, ride at
// hashed positions 5 to 14 px around each band edge inside every loud
// margin and hold their slots while the layer moves.
void undertow(const Design& design, const Zones& zones, Painter& painter,
              std::uint32_t timeMs) {
  const BgColor band = design.secondary;
  const BgColor foam = design.accent;
  const int offset = pyMod(-roundMilliPx(6000LL * timeMs), 80);
  const int height = roundTo(zones.canvasHeight * 0.07);
  for (int index = 0; index < 7; ++index) {
    const int y = roundTo(zones.canvasHeight * (0.28 + 0.09 * index));
    staircaseBand(zones, painter, band, y, height, 20, 6, index * 3, offset,
                  (index % 2) * 2);
    for (int margin = 0; margin < zones.loudCount; ++margin) {
      const BgRect& home = zones.loud[margin];
      for (int which = 0; which < 2; ++which) {
        const int fx =
            home.x + 8 +
            static_cast<int>(hash32(0x0F, index, home.x, which) %
                             static_cast<std::uint32_t>(
                                 std::max(1, home.w - 12)));
        const int fy = y - 5 +
                       static_cast<int>(hash32(0xF1, index, home.x, which) %
                                        10u);
        if (!fitsLoud(zones, fx, fy, 2, 1)) {
          continue;
        }
        paintElement(zones, painter, foam, fx, fy, 2, 1);
      }
    }
  }
}

// The broken bars of one rectangular contour of level 16, the
// ring_pieces() of tools/backgrounds/designs.py: a contour `thickness` px
// thick inset `d` px from the canvas edges, each of its four sides split
// once by a `gap` px opening at a hashed offset at least 8 px inside the
// bar, so no contour is ever a closed ring. Everything stays axis-aligned.
void contourBars(const Zones& zones, Painter& painter, BgColor color, int d,
                 int thickness, int gap, std::uint32_t seed) {
  const int w = zones.canvasWidth;
  const int h = zones.canvasHeight;
  const int sides[4][4] = {
      {d, d, w - 2 * d, thickness},                // top
      {d, h - d - thickness, w - 2 * d, thickness},  // bottom
      {d, d, thickness, h - 2 * d},                // left
      {w - d - thickness, d, thickness, h - 2 * d},  // right
  };
  for (int side = 0; side < 4; ++side) {
    const int x = sides[side][0];
    const int y = sides[side][1];
    const int sw = sides[side][2];
    const int sh = sides[side][3];
    const int length = std::max(sw, sh);
    if (length <= gap + 8) {
      paintElement(zones, painter, color, x, y, sw, sh);
      continue;
    }
    const int cut =
        8 + static_cast<int>(hash32(seed, side, d) %
                             static_cast<std::uint32_t>(
                                 std::max(1, length - gap - 16)));
    if (sw >= sh) {
      // A horizontal bar: the opening splits its x span.
      paintElement(zones, painter, color, x, y, cut, sh);
      paintElement(zones, painter, color, x + cut + gap, y, sw - cut - gap,
                   sh);
    } else {
      // A vertical bar: the opening splits its y span.
      paintElement(zones, painter, color, x, y, sw, cut);
      paintElement(zones, painter, color, x, y + cut + gap, sw,
                   sh - cut - gap);
    }
  }
}

// Level 16, Slow pulse: nine nested broken rectangular contours breathing
// outward around the scene. The contours are 2 px thick, inset 10 + 26 i px
// from the canvas edges plus the swell, each side split once by a 24 px
// opening at a hashed offset, so no contour is a closed ring; the
// outermost three carry one 2 x 2 px accent tip at each corner that lies
// inside a loud margin. Every contour swells 24 px outward and returns over
// the 8 s cycle - 48 px of travel in 8 s, 6 px/s at the fastest edge - a
// size change only, rectangular pressure: never circular, never neon.
void slowPulse(const Design& design, const Zones& zones, Painter& painter,
               std::uint32_t timeMs) {
  const BgColor contour = design.secondary;
  const BgColor tip = design.accent;
  const int cycleMs = design.motion.cycleMs;
  const std::int64_t through = static_cast<std::int64_t>(timeMs) % cycleMs;
  // 24 * triangle(t, cycle) in millionths of a pixel, on exact integers:
  // the phase advances 48000000/cycle micro-px per millisecond - 6000 for
  // the 8 s catalog cycle, which divides it exactly - rising to the
  // 24000000 micro peak at half the cycle and mirroring back. The exact
  // halves round to even exactly where the package's floats land on them.
  const std::int64_t triMicro = 48000000LL * through / cycleMs;
  const std::int64_t swellMicro =
      2 * through < cycleMs ? triMicro : 48000000LL - triMicro;
  const int swell = roundMilliPx(swellMicro);
  for (int index = 0; index < 9; ++index) {
    const int d = 10 + 26 * index + swell;
    if (2 * d + 2 >= std::min(zones.canvasWidth, zones.canvasHeight)) {
      continue;
    }
    contourBars(zones, painter, contour, d, 2, 24, 0x161 + index);
    if (index >= 3) {
      continue;
    }
    const int w = zones.canvasWidth;
    const int h = zones.canvasHeight;
    const int corners[4][2] = {{d, d},
                               {w - d - 2, d},
                               {d, h - d - 2},
                               {w - d - 2, h - d - 2}};
    for (const auto& corner : corners) {
      if (!fitsLoud(zones, corner[0], corner[1], 2, 2)) {
        continue;
      }
      paintElement(zones, painter, tip, corner[0], corner[1], 2, 2);
    }
  }
}

// Level 17, Rising ember: exactly 190 warm-gray embers rising past stepped
// block columns. The embers are the 190 cells of the 18 px grid the seed
// 0xE17 ranks lowest inside the loud margins - 1 x 2 px in half of them and
// 2 x 2 in the other half, all in the accent #897A72, the one warm shade of
// the design: the muted warmth lives in the accent pixels alone, never in a
// wash. Along the lower fifth of each side loud margin stand stepped block
// columns of the secondary #2A3040 - blocks 12 to 31 px wide and 10 px tall,
// end to end with 8 px gaps, stacked every 34 px up from the bottom edge,
// each carrying a half-width 6 px step 6 px above each block - and one row
// of the same blocks lies flat in the portrait bottom margin. The embers
// rise 7 px/s and wrap at their margin span; the columns hold still.
void risingEmber(const Design& design, const Zones& zones, Painter& painter,
                 std::uint32_t timeMs) {
  const DotShape shapes[2] = {{50, 1, 2}, {50, 2, 2}};
  const BgColor colors[1] = {design.accent};
  exactDots(zones, painter, 0xE17, 190, 18, shapes, 2, colors, 1, 0, -7000,
            timeMs, false, 0);
  const BgColor column = design.secondary;
  for (int margin = 0; margin < zones.loudCount && margin < 2; ++margin) {
    const BgRect& home = zones.loud[margin];
    int x = home.x + 4;
    const int floor = home.y + roundTo(home.h * 0.8);
    while (x < home.x + home.w - 12) {
      const int w = 12 + static_cast<int>(hash32(0xE4, x) % 20u);
      int y = home.y + home.h - 10;
      while (y > floor) {
        paintElement(zones, painter, column, x, y, w, 10);
        y -= 34;
      }
      int stepY = home.y + home.h - 16;
      while (stepY > floor) {
        if (fitsLoud(zones, x, stepY, w / 2, 6)) {
          paintElement(zones, painter, column, x, stepY, w / 2, 6);
        }
        stepY -= 34;
      }
      x += w + 8;
    }
  }
  for (int margin = 2; margin < zones.loudCount; ++margin) {
    const BgRect& home = zones.loud[margin];
    int x = home.x;
    while (x < home.x + home.w - 12) {
      const int w = 8 + static_cast<int>(hash32(0xE8, x) % 14u);
      if (fitsLoud(zones, x, home.y, w, 10)) {
        paintElement(zones, painter, column, x, home.y, w, 10);
      }
      x += w + 6;
    }
  }
}

// Level 18, Storm lines: two hundred rain dashes and four stepped trails
// falling as one layer. The dashes are the 200 cells of the 16 px grid the
// seed 0x518 ranks lowest inside the loud margins - 2 px wide and 4 px tall
// in 65 percent of them and 2 x 6 in 35, secondary #2A3146 or accent
// #8290A7 in equal share - wrapping over their margin span plus 6 px, the
// tallest dash's extent, so every dash leaves its margin fully before it
// re-enters. The trails cycle through the loud margins at 50 percent across
// and 8, 38, 62 and 86 percent down: staircases of five secondary segments,
// each 4 x 2 px, stepping 4 px left and 2 px down per segment, riding the
// same 8 px/s fall over the same span. Dim slate blue; no lightning, no
// glow.
void stormLines(const Design& design, const Zones& zones, Painter& painter,
                std::uint32_t timeMs) {
  const DotShape shapes[2] = {{65, 2, 4}, {35, 2, 6}};
  const BgColor colors[2] = {design.secondary, design.accent};
  exactDots(zones, painter, 0x518, 200, 16, shapes, 2, colors, 2, 0, 8000,
            timeMs, false, 6);
  static const double kDownShares[4] = {0.08, 0.38, 0.62, 0.86};
  for (int which = 0; which < 4; ++which) {
    const BgRect& home = zones.loud[which % zones.loudCount];
    const int x = home.x + roundTo(home.w * 0.5);
    const int y = home.y + roundTo(home.h * kDownShares[which]);
    for (int segment = 0; segment < 5; ++segment) {
      const int stepY =
          driftWrapped(y + segment * 2, home.y, home.h + 6, 8000, timeMs);
      paintElement(zones, painter, design.secondary, x - segment * 4, stepY, 4,
                   2);
    }
  }
}

// Level 19, Compression: thirty nested broken rectangular bands pressing
// inward from all four edges. The bands are 3 px thick, inset 8 px plus the
// accumulated max(5, 13 - index / 3) px per band - the spacing tightens from
// 13 px outside to 5 px near the board - each of the four bars split once by
// a 20 px opening at a hashed offset, so the nest draws 238 bars and the
// innermost band's two short cross-axis bars stay whole. On the two
// outermost bands, scarce accent ticks of #8591A8, 2 x 2 px every 32 px,
// ride the horizontal top-bar pieces that pass through the loud margins.
// The whole nest contracts 54 px onto the board and returns over the 12 s
// seamless cycle - 108 px of travel in 12 s, 9 px/s at the fastest edge -
// and the outer bands slide off the canvas edges as it presses, re-entering
// on the return: nothing pops or blinks. The board stays the clear center
// of attention.
void compression(const Design& design, const Zones& zones, Painter& painter,
                 std::uint32_t timeMs) {
  const BgColor band = design.secondary;
  const BgColor tick = design.accent;
  // 54 * triangle(t, 12) on the design package's own double path: fmod, one
  // division, one doubling, one scaling, one round-to-even. The exact
  // integer micro triangle of the other pulse designs cannot stand in here:
  // this cycle's exact halves sit on odd half seconds, whose twelfths
  // (17/24 and its kin) no double holds exactly, and the scaled product
  // rounds off the tie - 31.499...96 and 22.500...04 at the 8.5 s and
  // 9.5 s ties, one pixel off every ties-to-even whole. The port walks the
  // same IEEE-754 arithmetic CPython walks - std::fmod is exact,
  // std::nearbyint under the default rounding mode rounds ties of the
  // double to even exactly like Python's round() - so every whole-
  // millisecond clock lands on the package's pixel.
  const double seconds = static_cast<double>(timeMs) / 1000.0;
  const double cycle = design.motion.cycleMs / 1000.0;
  const double phase = std::fmod(seconds, cycle) / cycle;
  const double tri = phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0;
  const int press = static_cast<int>(std::nearbyint(54.0 * tri));
  int inset = 8;
  for (int index = 0; index < 30; ++index) {
    const int d = inset - press;
    inset += std::max(5, 13 - index / 3);
    if (d >= std::min(zones.canvasWidth, zones.canvasHeight) / 2) {
      continue;
    }
    contourBars(zones, painter, band, d, 3, 20, 0xC19 + index);
    if (index >= 2) {
      continue;
    }
    // The ticks of the two outermost bands: the top bar's pieces inside
    // each loud margin, in margin order, one tick every 32 px.
    const BgRect top{d, d, zones.canvasWidth - 2 * d, 3};
    for (int margin = 0; margin < zones.loudCount; ++margin) {
      const BgRect piece = intersectRect(top, zones.loud[margin]);
      if (piece.w <= 0 || piece.h <= 0) {
        continue;
      }
      if (piece.w < 14) {
        continue;
      }
      for (int x = piece.x + 6; x + 2 <= piece.x + piece.w - 4; x += 32) {
        paintElement(zones, painter, tick, x, piece.y, 2, 2);
      }
    }
  }
}

// Level 20, Collapse: two hundred eighty tiny debris fragments falling past
// stepped slabs at the far edges. The fragments are the first 280 cells of
// the 16 px grid ranked by outer-edge closeness first and the seed 0xC0A
// second - the debris piles against the far canvas edges and thins toward a
// clear dark corridor beside the board - each 1 x 3 px in 35 percent of the
// cells, 2 x 5 in 35 and 3 x 7 in 30, two thirds in the secondary #2A3146
// and one third in the accent #8A94AC, wrapping over the margin span plus
// the tallest fragment's 7 px extent. Three stepped slabs of the secondary
// per loud margin - 32 to 71 px wide and 10 to 19 px tall, at hashed
// positions in the upper three quarters, each carrying a half-width 5 px
// step on its top - hold still while the field falls 10 px/s. No tetromino
// clusters, no noise texture, no bright flare.
void collapse(const Design& design, const Zones& zones, Painter& painter,
              std::uint32_t timeMs) {
  const DotShape shapes[3] = {{35, 1, 3}, {35, 2, 5}, {30, 3, 7}};
  const BgColor colors[3] = {design.secondary, design.secondary,
                             design.accent};
  exactDots(zones, painter, 0xC0A, 280, 16, shapes, 3, colors, 3, 0, 10000,
            timeMs, true, 7);
  const BgColor slab = design.secondary;
  for (int margin = 0; margin < zones.loudCount; ++margin) {
    const BgRect& home = zones.loud[margin];
    for (int which = 0; which < 3; ++which) {
      const int w = 32 + static_cast<int>(hash32(0xC2, home.x, which) % 40u);
      const int h = 10 + static_cast<int>(hash32(0xC3, home.x, which) % 10u);
      const int x =
          home.x + 4 +
          static_cast<int>(hash32(0xC4, home.x, which) %
                           static_cast<std::uint32_t>(
                               std::max(1, home.w - w - 8)));
      const int y = home.y + 6 + which * (home.h / 4);
      if (!fitsLoud(zones, x, y, w, h)) {
        continue;
      }
      paintElement(zones, painter, slab, x, y, w, h);
      if (fitsLoud(zones, x, y - 5, w / 2, 5)) {
        paintElement(zones, painter, slab, x, y - 5, w / 2, 5);
      }
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// The public clipping layer.
// ---------------------------------------------------------------------------

void paintElement(const Zones& zones, Painter& painter, BgColor color, int x,
                  int y, int w, int h) {
  if (w <= 0 || h <= 0) {
    return;
  }
  const BgRect clipped =
      intersectRect(BgRect{x, y, w, h}, BgRect{0, 0, zones.canvasWidth,
                                                zones.canvasHeight});
  if (clipped.w <= 0 || clipped.h <= 0) {
    return;
  }
  BgRect pieces[kMaxElementPieces];
  int count = 0;
  if (withinCap(color, kSecondaryCap)) {
    // A base or secondary color draws anywhere outside the frame.
    count = subtractRect(clipped, zones.frame, pieces);
  } else {
    // A color above the secondary cap is an accent: it draws only inside the
    // loud margins, whatever the design asked for.
    for (int index = 0; index < zones.loudCount && count < kMaxElementPieces;
         ++index) {
      const BgRect piece = intersectRect(clipped, zones.loud[index]);
      if (piece.w <= 0 || piece.h <= 0) {
        continue;
      }
      BgRect splits[4];
      const int splitsCount = subtractRect(piece, zones.frame, splits);
      for (int split = 0; split < splitsCount && count < kMaxElementPieces;
           ++split) {
        pieces[count++] = splits[split];
      }
    }
  }
  for (int index = 0; index < count; ++index) {
    painter.fill(color, pieces[index].x, pieces[index].y, pieces[index].w,
                 pieces[index].h);
  }
}

void paintBase(const Zones& zones, Painter& painter, BgColor base) {
  // One whole-canvas fill, the ground layer every design record opens with
  // ("fill the whole canvas with base"). The opaque well and its one-pixel
  // ring cover their rectangle after the wallpaper, so no base pixel stays
  // visible inside them - the suites assert the well and ring pixels
  // exactly. Cutting the base around the frame instead would leave an
  // uncovered sub-pixel gap between the two fills on a window the canvas
  // scales into, which the single fill cannot.
  painter.fill(base, 0, 0, zones.canvasWidth, zones.canvasHeight);
}

void paintGrammar(const Design& design, const Zones& zones, Painter& painter,
                  std::uint32_t timeMs) {
  // The base field first, the ground layer every record opens with; then
  // the design's elements. The Still chapter of task T079 (levels 1 through
  // 4) is static, so the presentation clock never reaches it: the same
  // elements draw at every time value, which the suites assert over
  // arbitrary clocks. The Stir chapter of task T080 (levels 5 through 8),
  // the Build chapter of task T081 (levels 9 through 12), the Pressure
  // chapter of T082 (levels 13 through 16), and the Peak chapter of T083
  // (levels 17 through 20) move exactly one layer per design on that clock.
  paintBase(zones, painter, design.base);
  switch (design.level) {
    case 1: stillGround(design, zones, painter); return;
    case 2: firstLight(design, zones, painter); return;
    case 3: distantStars(design, zones, painter); return;
    case 4: quietStrata(design, zones, painter); return;
    case 5: slowDrift(design, zones, painter, timeMs); return;
    case 6: ditherDusk(design, zones, painter, timeMs); return;
    case 7: sleepingSkyline(design, zones, painter, timeMs); return;
    case 8: lowSignal(design, zones, painter, timeMs); return;
    case 9: fracture(design, zones, painter, timeMs); return;
    case 10: circuitHush(design, zones, painter, timeMs); return;
    case 11: bandAurora(design, zones, painter, timeMs); return;
    case 12: longTrails(design, zones, painter, timeMs); return;
    case 13: deepGrid(design, zones, painter, timeMs); return;
    case 14: fineSnow(design, zones, painter, timeMs); return;
    case 15: undertow(design, zones, painter, timeMs); return;
    case 16: slowPulse(design, zones, painter, timeMs); return;
    case 17: risingEmber(design, zones, painter, timeMs); return;
    case 18: stormLines(design, zones, painter, timeMs); return;
    case 19: compression(design, zones, painter, timeMs); return;
    case 20: collapse(design, zones, painter, timeMs); return;
    default: return;
  }
}

// ---------------------------------------------------------------------------
// The catalog accessors.
// ---------------------------------------------------------------------------

const Design* catalog() { return kCatalog; }

const Design* designOf(int id) {
  if (id < kDesignFirstId || id > kDesignLastId) {
    return nullptr;
  }
  return &kCatalog[id - kDesignFirstId];
}

const Design* designForLevel(int level) {
  if (level < kDesignFirstId) {
    return &kCatalog[0];
  }
  if (level > kDesignLastId) {
    return &kCatalog[kDesignCount - 1];
  }
  return &kCatalog[level - kDesignFirstId];
}

// ---------------------------------------------------------------------------
// The cosmetic generator.
// ---------------------------------------------------------------------------

std::uint32_t hash32(std::uint32_t seed, const int* values,
                     std::size_t count) {
  std::uint32_t crc = 0xFFFFFFFFu;
  const auto feed = [&crc](std::uint32_t word) {
    for (int byte = 0; byte < 4; ++byte) {
      crc = (crc >> 8) ^
            kCrcTable.entries[(crc ^ (word >> (8 * byte))) & 0xFFu];
    }
  };
  feed(seed);
  for (std::size_t index = 0; index < count; ++index) {
    feed(static_cast<std::uint32_t>(values[index]));
  }
  return crc ^ 0xFFFFFFFFu;
}

int driftPixels(int speedMilliPxPerS, std::uint32_t tMs) {
  // Displacement in milli-pixels: speed in milli-pixels per second times the
  // seconds of `tMs`. Rounded to the nearest whole pixel, so 500000
  // milli-pixels is the half step.
  const std::int64_t milli =
      static_cast<std::int64_t>(speedMilliPxPerS) *
      static_cast<std::int64_t>(tMs);
  return static_cast<int>((milli + 500000) / 1000000);
}

int pulsePhasePerMille(std::uint32_t tMs, int cycleMs) {
  if (cycleMs <= 0) {
    return 0;
  }
  const std::uint64_t cycle = static_cast<std::uint64_t>(cycleMs);
  const std::uint64_t through = static_cast<std::uint64_t>(tMs) % cycle;
  // Thousandths of the cycle, then the 0..1000..0 triangle of the swell.
  const int perMille = static_cast<int>(through * 1000 / cycle);
  return perMille <= 500 ? 2 * perMille : 2000 - 2 * perMille;
}

// ---------------------------------------------------------------------------
// The presentation clock.
// ---------------------------------------------------------------------------

void PresentationClock::advance(std::uint32_t deltaMs) {
  if (frozen_) {
    return;  // frozen time is dropped, not banked: resume holds no jump
  }
  if (deltaMs > kMaxAdvanceMs) {
    deltaMs = kMaxAdvanceMs;
  }
  timeMs_ += deltaMs;
}

void PresentationClock::advanceFrames(int frames) {
  if (frozen_) {
    return;
  }
  // The same stall bound as advance(): at most 250 ms of motion per call.
  if (frames < 0) {
    frames = 0;
  }
  const int frameCeiling = static_cast<int>(kMaxAdvanceMs * 3 / 50);
  if (frames > frameCeiling) {
    frames = frameCeiling;
  }
  // One frame is exactly 1000/60 ms, so frames advance the clock without
  // drift: the thirds carry as sub-milliseconds.
  const std::uint32_t milliThirds =
      remainderThirds_ + static_cast<std::uint32_t>(frames) * 50u;
  timeMs_ += milliThirds / 3u;
  remainderThirds_ = static_cast<std::uint8_t>(milliThirds % 3u);
}

void PresentationClock::reset() noexcept {
  timeMs_ = 0;
  remainderThirds_ = 0;
}

// ---------------------------------------------------------------------------
// The wallpaper state machine.
// ---------------------------------------------------------------------------

// The mode words of the settings file and the menus (task T084), in the cycle
// order of the mode row: Auto, Off, Fixed, back to Auto.
const char* modeName(Mode mode) {
  switch (mode) {
    case Mode::Auto:
      return "auto";
    case Mode::Off:
      return "off";
    case Mode::Fixed:
      return "fixed";
  }
  return "auto";
}

bool modeFromName(const char* name, Mode* mode) {
  if (name == nullptr || mode == nullptr) {
    return false;
  }
  for (const Mode candidate : {Mode::Auto, Mode::Off, Mode::Fixed}) {
    if (std::strcmp(name, modeName(candidate)) == 0) {
      *mode = candidate;
      return true;
    }
  }
  return false;
}

const char* modeDisplayName(Mode mode) {
  switch (mode) {
    case Mode::Auto:
      return "AUTO";
    case Mode::Off:
      return "OFF";
    case Mode::Fixed:
      return "FIXED";
  }
  return "AUTO";
}

const Design* Wallpaper::selection() const noexcept {
  switch (mode_) {
    case Mode::Off:
      return nullptr;
    case Mode::Fixed:
      return designOf(preference_);
    case Mode::Auto:
      return designForLevel(level_);
  }
  return nullptr;
}

void Wallpaper::applySelection(const Design* target) {
  if (target == nullptr) {
    fading_ = false;
    outgoing_ = 0;
    return;
  }
  if (target->level == active_) {
    return;  // already shown; a running fade toward it keeps running
  }
  if (target->transition.kind != TransitionKind::Fade) {
    active_ = target->level;
    fading_ = false;
    outgoing_ = 0;
    return;
  }
  // A fade from the design the canvas shows. A selection that arrives while
  // a fade already runs restarts the fade from that fade's target.
  outgoing_ = active_;
  active_ = target->level;
  fadeStartMs_ = clock_.timeMs();
  fading_ = true;
}

void Wallpaper::setMode(Mode mode) noexcept {
  if (mode == mode_) {
    return;
  }
  mode_ = mode;
  // A manual mode change cuts: the flat Off wallpaper and the selected
  // design swap on the next frame, without a transition.
  fading_ = false;
  outgoing_ = 0;
  const Design* target = selection();
  if (target != nullptr) {
    active_ = target->level;
  }
}

void Wallpaper::setPreference(int designId) noexcept {
  if (designId < kDesignFirstId) {
    designId = kDesignFirstId;
  } else if (designId > kDesignLastId) {
    designId = kDesignLastId;
  }
  if (designId == preference_) {
    return;
  }
  preference_ = designId;
  if (mode_ == Mode::Fixed) {
    // The preference takes effect at once in Fixed mode, as a cut.
    fading_ = false;
    outgoing_ = 0;
    active_ = designId;
  }
}

void Wallpaper::setLevel(int level) noexcept {
  level_ = level;
  if (mode_ == Mode::Auto) {
    applySelection(selection());
  }
}

void Wallpaper::beginRound() {
  fading_ = false;
  outgoing_ = 0;
  const Design* target = selection();
  if (mode_ == Mode::Fixed) {
    // Fixed stays fixed across rounds: the preferred design, its motion, and
    // its clock run on without a reset. Only a stray state cuts here.
    if (target != nullptr && target->level != active_) {
      active_ = target->level;
    }
    return;
  }
  clock_.reset();
  if (target != nullptr) {
    active_ = target->level;
  }
}

void Wallpaper::sweepFade() {
  if (!fading_) {
    return;
  }
  const Design* target = designOf(active_);
  if (target == nullptr || target->transition.kind != TransitionKind::Fade) {
    fading_ = false;
    outgoing_ = 0;
    return;
  }
  const std::uint32_t elapsed = clock_.timeMs() - fadeStartMs_;
  if (elapsed >= static_cast<std::uint32_t>(target->transition.durationMs)) {
    fading_ = false;
    outgoing_ = 0;
  }
}

void Wallpaper::advance(std::uint32_t deltaMs) {
  clock_.advance(deltaMs);
  sweepFade();
}

void Wallpaper::advanceFrames(int frames) {
  clock_.advanceFrames(frames);
  sweepFade();
}

bool Wallpaper::draw(const Zones& zones, Painter& painter) const {
  if (mode_ == Mode::Off) {
    return false;
  }
  const Design* show = designOf(active_);
  if (show == nullptr) {
    return false;
  }
  if (fading_ && show->transition.kind == TransitionKind::Fade) {
    const TransitionSpec& spec = show->transition;
    const std::uint32_t elapsed = clock_.timeMs() - fadeStartMs_;
    if (elapsed < static_cast<std::uint32_t>(spec.durationMs)) {
      const int steps = spec.steps > 0 ? spec.steps : 1;
      int step = static_cast<int>(
          (elapsed * static_cast<std::uint32_t>(steps)) /
          static_cast<std::uint32_t>(spec.durationMs));
      if (step >= steps) {
        step = steps - 1;
      }
      const Design* from =
          outgoing_ >= kDesignFirstId ? designOf(outgoing_) : show;
      const BgColor anchor = midColor(from->base, show->base);
      int parts = 0;
      if (2 * step < steps) {
        // The first half: the outgoing design, stepped toward the anchor.
        show = from;
        parts = 2 * step;
      } else {
        // The second half: the incoming design, stepping away from it.
        parts = 2 * (steps - step);
      }
      TintPainter tinted(painter, anchor, parts, steps);
      paintGrammar(*show, zones, tinted, clock_.timeMs());
      return true;
    }
    // The fade is over but not yet swept: the target draws plainly.
  }
  paintGrammar(*show, zones, painter, clock_.timeMs());
  return true;
}

}  // namespace backgrounds
}  // namespace stackfall
