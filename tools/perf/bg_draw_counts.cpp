// Background draw counts: the instrumented command budget of the wallpaper
// layer (task T085, docs/backgrounds, notes/performance.md).
//
// The renderer counts what the game would draw: a counting Painter receives
// every fill the real adapter would turn into one renderer rectangle, so the
// numbers below are the emitted commands, not the design records' element
// counts - grouping inside a grammar cannot hide work here.
//
//   steady   every design of the catalog, both orientations, swept over its
//            whole motion cycle: the worst frame's fill count and filled
//            area, and the timed cost of one wallpaper draw
//   fade     every fade transition of Auto mode, sampled through its steps:
//            the worst frame while the fade runs, against the two grammars
//            it blends
//   off      Mode::Off, which must emit nothing
//
// The zones restate the renderer's own geometry (renderer_sdl.hpp and
// docs/backgrounds/index.md, "The layout mask"): the landscape frame is the
// well plus its one-pixel ring, the portrait frame the tall board's ring.
//
// The timings describe this host alone. Nothing here measures the RG503.
//
// Run: stackfall_bg_counts [sweep-step-ms]

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include <stackfall/backgrounds.hpp>

namespace {

namespace bg = stackfall::backgrounds;

using bg::BgColor;
using bg::BgRect;
using bg::Design;
using bg::Painter;
using bg::Zones;

class CountingPainter final : public Painter {
 public:
  void fill(BgColor, int, int, int w, int h) override {
    ++fills_;
    area_ += static_cast<long>(w) * h;
  }
  void reset() {
    fills_ = 0;
    area_ = 0;
  }
  int fills() const { return fills_; }
  long area() const { return area_; }

 private:
  int fills_ = 0;
  long area_ = 0;
};

Zones landscapeZones() {
  // renderer_sdl.hpp: the 960x544 canvas, the well's one-pixel ring
  // (344, 1, 272, 542), and the loud margins outside the quiet HUD span
  // x = 192..767.
  Zones zones;
  zones.canvasWidth = 960;
  zones.canvasHeight = 544;
  zones.frame = BgRect{344, 1, 272, 542};
  zones.loud[0] = BgRect{0, 0, 192, 544};
  zones.loud[1] = BgRect{768, 0, 192, 544};
  zones.loudCount = 2;
  return zones;
}

Zones portraitZones() {
  // renderer_sdl.hpp: the 544x960 canvas, the tall board's ring
  // (71, 119, 402, 802), the 12 px guards, and the strip below the board.
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

int motionCycleMs(const Design& design) {
  return std::max(design.motion.cycleMs, 4000);
}

// The worst steady frame of one design: the sweep walks the full motion
// cycle, so wrapping or pulsing phases cannot hide a dense moment.
struct Steady {
  int maxFills = 0;
  long maxArea = 0;
  unsigned worstMs = 0;
};

Steady steadyWorst(const Design& design, const Zones& zones,
                   CountingPainter& painter, int stepMs) {
  Steady out;
  const int span = motionCycleMs(design);
  for (int ms = 0; ms < span; ms += stepMs) {
    painter.reset();
    bg::paintGrammar(design, zones, painter, static_cast<unsigned>(ms));
    // paintGrammar leaves the base fill to the caller's clear, and the
    // runtime draws it as one fill: count it here so the numbers speak in
    // whole frames.
    const int fills = painter.fills() + 1;
    if (fills > out.maxFills) {
      out.maxFills = fills;
      out.maxArea = painter.area() +
                    static_cast<long>(zones.canvasWidth) * zones.canvasHeight;
      out.worstMs = static_cast<unsigned>(ms);
    }
  }
  return out;
}

int timedDraws(const Design& design, const Zones& zones,
               CountingPainter& painter, int stepMs, int repeats) {
  // The mean microseconds of one wallpaper frame paint, drawn `repeats`
  // times around the sweep. The first pass warms nothing special; the
  // mean over a full cycle absorbs the per-phase variance.
  const int span = motionCycleMs(design);
  long long micros = 0;
  int draws = 0;
  for (int i = 0; i < repeats; ++i) {
    for (int ms = 0; ms < span; ms += stepMs) {
      const auto start = std::chrono::steady_clock::now();
      bg::paintGrammar(design, zones, painter, static_cast<unsigned>(ms));
      const auto end = std::chrono::steady_clock::now();
      micros += std::chrono::duration_cast<std::chrono::microseconds>(
                    end - start)
                    .count();
      ++draws;
    }
  }
  return draws == 0 ? 0 : static_cast<int>(micros / draws);
}

}  // namespace

int main(int argc, char** argv) {
  const int stepMs = argc > 1 ? std::atoi(argv[1]) : 25;
  if (stepMs <= 0) {
    std::fprintf(stderr, "usage: bg_draw_counts [sweep-step-ms]\n");
    return 2;
  }

  CountingPainter painter;

  for (const Zones& zones : {landscapeZones(), portraitZones()}) {
    const char* name =
        zones.canvasWidth == 960 ? "landscape" : "portrait";
    std::printf("orientation %s (%dx%d), sweep step %d ms\n", name,
                zones.canvasWidth, zones.canvasHeight, stepMs);

    // Off first: it must emit nothing at all.
    bg::Wallpaper off;
    off.setMode(bg::Mode::Off);
    painter.reset();
    const bool offDrew = off.draw(zones, painter);
    std::printf("  off: drew=%d fills=%d\n", offDrew ? 1 : 0,
                painter.fills());

    int worstId = 0;
    int worstFills = 0;
    int worstMicros = 0;
    int worstTimedId = 0;
    std::printf("  design  steady-fills  filled-px   worst-at-ms"
                "  fade-fills  fade-of\n");
    for (int id = bg::kDesignFirstId;
         id <= bg::kDesignLastId; ++id) {
      const Design& design = *bg::designOf(id);
      const Steady steady =
          steadyWorst(design, zones, painter, stepMs);
      if (steady.maxFills > worstFills) {
        worstFills = steady.maxFills;
        worstId = id;
      }

      // The fade cost of entering this design in Auto mode: the live
      // Wallpaper, its clock, and its fade state, sampled through the
      // steps the way rendered frames would see them.
      int fadeFills = 0;
      const char* fadeOf = "-";
      if (id > bg::kDesignFirstId &&
          design.transition.kind == bg::TransitionKind::Fade) {
        bg::Wallpaper live;
        live.setMode(bg::Mode::Auto);
        live.setLevel(id - 1);
        live.beginRound();
        live.setLevel(id);  // the level change starts the fade
        const int duration = design.transition.durationMs;
        for (int ms = stepMs; ms < duration; ms += stepMs) {
          live.advance(static_cast<unsigned>(ms));
          painter.reset();
          live.draw(zones, painter);
          fadeFills = std::max(fadeFills, painter.fills() + 1);
        }
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%d->%d", id - 1, id);
        fadeOf = buffer;
      }

      std::printf("  %6d  %12d  %9ld  %11u  %11d  %s\n", id,
                  steady.maxFills, steady.maxArea, steady.worstMs,
                  fadeFills, fadeOf);
    }

    // Time the catalog's worst steady design: one wallpaper frame paint.
    const Design& worst = *bg::designOf(worstId);
    worstMicros = timedDraws(worst, zones, painter, stepMs, 8);
    worstTimedId = worstId;
    std::printf("  worst steady: design %d with %d fills (%.2f us per"
                " fill)\n",
                worstTimedId, worstFills,
                worstFills == 0 ? 0.0
                                : static_cast<double>(worstMicros) /
                                      worstFills);
    std::printf("  one %s paint of design %d: %d us per frame\n", name,
                worstTimedId, worstMicros);
  }
  return 0;
}
