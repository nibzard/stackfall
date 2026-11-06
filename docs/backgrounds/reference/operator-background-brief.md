# Background design brief: 20 procedural level backgrounds

Date: 2026-11-05
Audience: game designer
Status: open for design work

## Purpose

STACKFALL needs one background per level. The game has 20 levels. You design
20 background variations, level 1 through level 20. This brief covers the
visual side only. It states what you design, the medium, the rules, and the
deliverables.

The background is the wallpaper behind the play scene. It shows around the
game field, behind the head-up display (HUD). It never touches the field
itself.

## The medium

The game draws everything with flat color rectangles on a pixel grid. Keep
this mental model:

- Every shape is an axis-aligned rectangle filled with one solid color.
- Edges are hard. There is no anti-aliasing and no smooth blur.
- A gradient is a stack of solid bands. Stepped bands replace smooth ramps.
- A curve or diagonal is stepped, like classic pixel art.
- The smallest element is one pixel.
- A design is a pattern grammar: element sizes, spacing, and placement rules.
  It is not a fixed picture. The pattern must tile or scale to the canvas.

## The stage

Two canvases share every design. The player picks either one.

### Landscape: 960 x 544

```
x=0     192     320 344       616 640     768     960
+--------+-------+--+----------+--+-------+--------+ y=0
| quiet  | stats |  |          |  | next  | quiet  |
| margin | zone  |g |   board  |g | zone  | margin |
|        | (text |a |   well   |a | (pre- |        |
|        |  on   |p | (opaque, |p | views)|        |
|        |  bg)  |  |  hidden) |  |       |        |
+--------+-------+--+----------+--+-------+--------+ y=544
```

- Board well: x = 345..614, y = 2..541, inside a one-pixel frame. Opaque.
  Your design never shows there.
- Stats zone: x = 192..319. Text and the Hold box sit on your background.
- Next zone: x = 640..767. Five preview boxes sit on your background.
- Quiet margins: x = 0..191 and x = 768..959. Your background shows alone.
- Thin gaps of 24 px flank the frame. The top and bottom canvas rows also
  show your background.

### Portrait: 544 x 960

```
x=0                 544
+---------------------+ y=0
|  HUD strip: stats,  |
|  hold, next, all on |
|  your background    |
+---+-------------+---+ y=119
| q |             | q |
| u |    board    | u |
| i |    well     | i |
| m |  (opaque)   | m |
+---+-------------+---+ y=920
|     quiet margin    |
+---------------------+ y=960
```

- Board well: x = 72..471, y = 120..919, inside a one-pixel frame. Opaque.
- HUD strip: y = 0..118, full width. Text and preview boxes sit on your
  background.
- Quiet margins: x = 0..71 and x = 472..543 for the board height, and
  y = 920..959 below the board.

## The fixed scene partners

These colors are final. Design around them, never against them.

### Scene colors

| Role | Hex |
|---|---|
| Canvas ground today | `#10121A` |
| Board well (opaque) | `#0A0C13` |
| Faint grid lines | `#1A1E2A` |
| Board frame | `#4E5874` |
| Text, bright | `#E8ECF6` |
| Text, dim | `#8A94AC` |
| Piece outline | `#08090E` |
| Menu accent | `#24CCD8` |

### The seven piece colors

| Piece | Hex |
|---|---|
| I, cyan | `#24CCD8` |
| O, amber | `#F8C840` |
| T, violet | `#B070E0` |
| S, green | `#48D068` |
| Z, crimson | `#E85460` |
| J, blue | `#5880F0` |
| L, orange | `#F4943C` |

## Design rules

Follow every rule. They protect readability first.

### Contrast and brightness

1. Keep base fields between `#0A0C13` and `#1C2130`. A base field is the
   largest area of the design.
2. Keep large secondary shapes, such as bands and silhouettes, no brighter
   than `#2A3146`.
3. Keep small accents, such as stars, ticks, and thin lines, no brighter
   than `#8A94AC`. Accents may cover at most two percent of the canvas.
4. Inside the quiet zones (the stats and next columns in landscape, the HUD
   strip in portrait), draw nothing brighter than `#2A3146`. Text and
   preview boxes must stay readable.
5. Keep every large area clearly darker and duller than all seven piece
   colors. The pieces must win every comparison at a glance.
6. A small accent may echo one piece hue. Never fill a large area with a
   piece hue.

### Shape and composition

7. Use axis-aligned rectangles only. Step every diagonal and curve.
8. Never draw anything that mimics gameplay: no tetromino-shaped clusters,
   no arrows near the field, no numbers, no letters.
9. The board is the center of attention. Frame it with the design; do not
   crowd it. Density grows toward the canvas edges, not toward the board.

### Motion

10. Levels 1 through 4 are fully static. No motion.
11. From level 5 on, motion is optional. Use at most one moving layer per
    design.
12. Moving elements drift at most 10 pixels per second. A pulse cycle lasts
    at least four seconds.
13. No blinking. No strobe. No sudden changes. Motion is drift, scroll, or
    slow pulse only.

### Budget and scope

14. Keep each design under one of these limits: about 300 small elements or
    about 40 large bands. The hardware is a low-power handheld.
15. One design serves both orientations. Adapt the grammar, keep the
    identity.
16. A grayscale play mode keeps your background as designed. Do not rely on
    piece colors for separation from the background. Luminance alone must
    separate pieces from your base fields.
17. On level change the background switches. State for each design whether
    it cuts or fades. A fade lasts at most one second, in visible steps.

## The 20-slot arc

Speed ramps steeply from level 1 to level 20. At level 1 a piece falls
slowly. At level 20 it falls almost instantly. The background arc follows
that ramp:

| Levels | Chapter | Feel |
|---|---|---|
| 1-4 | Still | Calm, near-empty, wide dark space |
| 5-8 | Stir | First life: sparse motion, one motif |
| 9-12 | Build | Clear theme, layered depth, steady motion |
| 13-16 | Pressure | Denser, faster motion, brighter accents |
| 17-20 | Peak | Maximum density and energy inside the rules |

Each step must read as a rise. A level 17 background must feel more intense
than a level 9 one at a glance. Never break the brightness caps to gain
intensity. Gain it with density, count, and motion instead.

## Concept seeds (optional starting points)

Replace any seed with a better idea. Keep the arc position.

| Level | Seed | One line |
|---|---|---|
| 1 | Still ground | Plain near-black field, faint darker corners |
| 2 | First light | One thin horizontal line, low on the canvas |
| 3 | Starfield | Sparse one and two pixel dots, static |
| 4 | Strata | Six to eight horizontal sediment bands |
| 5 | Drift | The starfield, drifting down very slowly |
| 6 | Dither dusk | Checker dither blend between two dark tones |
| 7 | Skyline | Stepped tower silhouettes along the bottom edge |
| 8 | Scanlines | One-pixel horizontal lines, very low contrast |
| 9 | Fault | One stepped diagonal seam crossing a margin |
| 10 | Circuit | Right-angle traces with square nodes |
| 11 | Aurora | Three wide overlapping stepped bands |
| 12 | Meteors | Rare slow diagonal streaks |
| 13 | Deep grid | Perspective grid receding toward the board |
| 14 | Snow | One-pixel dots drifting down, denser than level 5 |
| 15 | Undertow | Wave bands sliding in two directions |
| 16 | Pulse | Rectangular rings expanding from the board, slow |
| 17 | Ember | Small warm accents rising along the margins |
| 18 | Storm | Rain dashes plus an occasional meteor |
| 19 | Pressure | Bands tightening toward the board from all sides |
| 20 | Collapse | Dense block debris at the canvas edges |

## Deliverables

For each of the 20 variations, deliver:

- A name and a one-line concept.
- The palette in hex. Label each color: base, secondary, accent.
- The pattern grammar in plain words. Sizes, spacing, and placement rules
  must be exact. A programmer redraws it from your text alone, without
  asking you a question.
- A full mockup at 960 x 544, with the real HUD and board overlaid.
- A portrait mockup at 544 x 960, same treatment.
- A motion spec, if the design moves. State direction, speed in pixels per
  second, and cycle length.
- A cut or fade note for the level change.
- A self-rated intensity from 1 to 10.

Also deliver one summary sheet: all 20 names in level order with their base
colors as swatches.

We supply a layout mask image that marks the quiet zones, the zones the HUD
occupies, and the opaque well, in both orientations.

## Process

1. Read this brief and the layout mask.
2. Sketch all 20 concepts as small thumbnails on one sheet.
3. Review the thumbnails against the arc. Check the rise from chapter to
   chapter.
4. Replace weak slots until the row of 20 reads as one climb.
5. Produce full mockups for both orientations.
6. Write the pattern grammar for each design.
7. Revise after the playtest pass.

## Acceptance checks

We judge each design against this list:

- HUD text and preview boxes read clearly in both orientations.
- The board frame stays visible against the background.
- The thumbnail test passes: at one-eighth scale, a design is still distinct
  from its neighbors.
- A short playtest keeps player attention on the field.
- Every element respects the brightness caps and the two percent accent
  budget.
- The grayscale mode keeps every piece visible against the base fields.
- The design works at both 960 x 544 and 544 x 960.
