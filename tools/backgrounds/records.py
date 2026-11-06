"""Write the design records of `docs/backgrounds` (tasks T075, T077).

One Markdown record per level plus the index, in the pattern of
`docs/music`: a field table, the plain-word grammar, the motion spec, and
the mockup links. The measured numbers come from the acceptance run, so a
record can only claim what `check.py` verified.

T077 reconciled the T075 set with the owner-approved imagegen set
(approval 2026-11-05): every record states its approved name, palette,
counts, motion, and transition, and a reconciliation note records what
changed against the T075 design of the same slot without discarding it.

Run after `render.py` and `check.py`; the index cites the check results of
`build/backgrounds/check-results.json`.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import designs

REPO = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
DOCS = os.path.join(REPO, "docs", "backgrounds")

DESIGN_DATE = "2026-11-05"
SOURCE_BRIEF = ("operator background brief, retained at "
                "docs/backgrounds/reference/operator-background-brief.md")
SOURCE_APPROVAL = ("owner-approved imagegen set of 2026-11-05, retained at "
                   "docs/backgrounds/reference/ (concepts.json, prompts.md, "
                   "and the 20 concept plates)")
CHECK_COMMAND = "python3 tools/backgrounds/check.py"

CHAPTER_RANGE = {"Still": "levels 1-4", "Stir": "levels 5-8",
                 "Build": "levels 9-12", "Pressure": "levels 13-16",
                 "Peak": "levels 17-20"}
CHAPTER_ROLE = {
    "Still": "Fully static fields. Nothing moves; the board reads alone.",
    "Stir": "First motion. One slow layer per design, 2 to 3 px/s.",
    "Build": "Structure appears: seams, traces, ribbons, streaks.",
    "Pressure": "The set tightens: faster drift, denser marks, pulses.",
    "Peak": "The climax: the busiest fields, all near the caps.",
}

# The T075 name of each slot, for the reconciliation note.
T075_NAME = {1: "Still Ground", 2: "First Light", 3: "Starfield",
             4: "Strata", 5: "Drift", 6: "Dither Dusk", 7: "Skyline",
             8: "Scanlines", 9: "Fault", 10: "Circuit", 11: "Aurora",
             12: "Meteors", 13: "Deep Grid", 14: "Snow", 15: "Undertow",
             16: "Pulse", 17: "Ember", 18: "Storm", 19: "Pressure",
             20: "Collapse"}


def transition_text(transition):
    if transition[0] == "cut":
        return "cut"
    return ("fade, %.1f s in %d visible steps" % (transition[1],
                                                  transition[2]))


def motion_text(motion):
    if motion is None:
        return "static"
    if motion["direction"] == "none":
        return "%s swell, %.2g px/s, %.1f s cycle" % (
            motion["kind"], motion["speed"], motion["cycle"])
    return "%s %s, %.2g px/s, %.1f s cycle" % (
        motion["kind"], motion["direction"], motion["speed"],
        motion["cycle"])


def intensity_text(value):
    return "%g of 10" % value


def slug(level):
    return "%02d" % level


def counts_text(design):
    if not design["counts"]:
        return "see the grammar"
    return ", ".join("%s x%d" % (tag.replace("-", " "), count)
                     for tag, count in sorted(design["counts"].items()))


def write_record(design, measured):
    level = design["level"]
    path = os.path.join(DOCS, "level-%s.md" % slug(level))
    lines = []
    lines.append("# %s (level %d)" % (design["name"], level))
    lines.append("")
    lines.append("%s." % design["concept"].rstrip("."))
    lines.append("")
    lines.append("| Field | Value |")
    lines.append("| --- | --- |")
    lines.append("| Level | %d of 20 |" % level)
    lines.append("| Chapter | %s (%s) |" % (design["chapter"],
                                            CHAPTER_RANGE[design["chapter"]]))
    lines.append("| Base field | `%s` |" % design["base"])
    if design["secondary"]:
        lines.append("| Secondary shades | %s |" % ", ".join(
            "`%s`" % shade for shade in design["secondary"]))
    else:
        lines.append("| Secondary shades | none |")
    if design["accent"]:
        lines.append("| Accent shades | %s |" % ", ".join(
            "`%s`" % shade for shade in design["accent"]))
    else:
        lines.append("| Accent shades | none |")
    lines.append("| Motion | %s |" % motion_text(design["motion"]))
    lines.append("| Level change | %s |" %
                 transition_text(design["transition"]))
    lines.append("| Intensity | %s |" % intensity_text(design["intensity"]))
    lines.append("| Budget | %s |" % (
        "at most 40 band elements" if design["budget"] == "bands" else
        "at most 300 small elements"))
    lines.append("| Verified counts | %s |" % counts_text(design))
    lines.append("| Measured marks | %.0f per 1000 samples at one-eighth "
                 "scale |" % measured["density"])
    lines.append("| Measured elements | %d |" % measured["count"])
    lines.append("| Measured speed | %.2g px/s |" % measured["speed"])
    lines.append("")
    lines.append("## Pattern grammar")
    lines.append("")
    lines.append("One grammar draws both canvases; only the canvas size "
                 "and the layout zones change. Protected regions: nothing "
                 "draws inside the opaque well (x 345..614, y 2..541 "
                 "landscape; x 72..471, y 120..919 portrait) or over the "
                 "one-pixel frame, and nothing brighter than `#2A3146` "
                 "draws inside the quiet zones (the statistics and next "
                 "columns, the portrait HUD strip, and the guard gaps). "
                 "Accents draw only inside the loud margins. The full "
                 "mask: [mask-landscape.png](images/mask-landscape.png), "
                 "[mask-portrait.png](images/mask-portrait.png).")
    lines.append("")
    for index, statement in enumerate(design["grammar"], start=1):
        lines.append("%d. %s" % (index, statement))
    lines.append("")
    if design["motion"]:
        lines.append("## Motion")
        lines.append("")
        lines.append("%s" % design["motion"]["note"])
        lines.append("")
    lines.append("## Approved reference")
    lines.append("")
    lines.append("Owner-approved concept plate: "
                 "[reference/level-%s.png](reference/level-%s.png). The "
                 "plate is visual direction, not an exact capture: the "
                 "generated image is anti-aliased with off-palette noise, "
                 "and this record's rectangle geometry is authoritative "
                 "wherever the two differ."
                 % (slug(level), slug(level)))
    lines.append("")
    lines.append("## Reconciliation")
    lines.append("")
    lines.append("Reconciled in T077 against the owner-approved set "
                 "(approval 2026-11-05). The T075 design of this slot was "
                 "\"%s\"; its grammar machinery draws on, with the "
                 "approved name, palette, counts, motion, and transition "
                 "adopted here. The T075 mockups and records were "
                 "regenerated from the same machinery, so no completed "
                 "design work was discarded." % T075_NAME[level])
    lines.append("")
    lines.append("## Mockups")
    lines.append("")
    lines.append("The design under the real HUD and board, taken from the "
                 "capture session `tools/backgrounds/mockup-hud.script.txt`"
                 " and its portrait twin:")
    lines.append("")
    lines.append("- Landscape 960x544: [mockup-%s-landscape.png](images/"
                 "mockup-%s-landscape.png)" % (slug(level), slug(level)))
    lines.append("- Portrait 544x960: [mockup-%s-portrait.png](images/"
                 "mockup-%s-portrait.png)" % (slug(level), slug(level)))
    lines.append("")
    lines.append("## Review status")
    lines.append("")
    lines.append("Passed the mechanical acceptance check of `%s` (run of "
                 "%s). Visually reviewed on the thumbnail sheet: no "
                 "letters, numerals, arrows, or tetromino clusters, and "
                 "distinct from both neighbors at one-eighth scale. An "
                 "on-device look on the handheld remains open; the "
                 "playtest pass of task T076 stays blocked until the "
                 "playable implementation of T078 through T085 lands."
                 % (CHECK_COMMAND, DESIGN_DATE))
    lines.append("")
    with open(path, "w") as handle:
        handle.write("\n".join(lines))
    return path


def chapter_table(catalogue):
    lines = ["| Level | Name | Chapter | Base | Motion | Peak speed | "
             "Intensity |", "| --- | --- | --- | --- | --- | --- | --- |"]
    for design in catalogue:
        lines.append("| %d | %s | %s | `%s` | %s | %.2g px/s | %g |" % (
            design["level"], design["name"], design["chapter"],
            design["base"], motion_text(design["motion"]).split(",")[0],
            (design["motion"] or {}).get("speed", 0.0),
            design["intensity"]))
    return lines


def arc_table(measured):
    chapters = [("Still", 1, 4), ("Stir", 5, 8), ("Build", 9, 12),
                ("Pressure", 13, 16), ("Peak", 17, 20)]
    lines = ["| Chapter | Mean marks per 1000 | Mean elements | "
             "Peak speed |", "| --- | --- | --- | --- |"]
    rows = {row["level"]: row for row in measured}
    for name, first, last in chapters:
        levels = range(first, last + 1)
        lines.append("| %s | %.0f | %.0f | %.2g px/s |" % (
            name,
            sum(rows[l]["density"] for l in levels) / 4.0,
            sum(rows[l]["count"] for l in levels) / 4.0,
            max(rows[l]["speed"] for l in levels)))
    return lines


def reference_section():
    return [
        "## The approved reference set",
        "",
        "The owner approved the imagegen gallery on 2026-11-05 and "
        "requested implementation. The references this approval rests on "
        "are retained inside the repository, so no worktree, hosted "
        "gallery, or `/tmp` file is needed:",
        "",
        "- [reference/operator-background-brief.md](reference/"
        "operator-background-brief.md) - the design brief the set was "
        "built against.",
        "- [reference/concepts.json](reference/concepts.json) - the "
        "approved names, palettes, concept lines, motion, and "
        "intensities, one record per level.",
        "- [reference/prompts.md](reference/prompts.md) - the exact "
        "imagegen prompts of the 20 concept plates.",
        "- `reference/level-01.png` through `reference/level-20.png` - "
        "the original concept plates, 1536 x 1024 presentation rasters.",
        "- [BACKGROUND_IMPLEMENTATION.md](../BACKGROUND_IMPLEMENTATION.md) "
        "- the owner's implementation plan: renderer contract, player "
        "options, validation, and the task structure T077 through T085.",
        "",
        "The plates are direction, not captures. Each was requested as a "
        "1536 x 1024 presentation raster holding two screenshots (a "
        "960 x 544 landscape view and a 544 x 960 portrait view) with a "
        "level label above the left view; the generator placed them "
        "approximately and painted with anti-aliasing and heavy noise "
        "(over 100,000 distinct colors per plate), and several plates "
        "ignore the requested `#080A10` surround or drift past the "
        "brightness caps. Wherever a plate and this package disagree, the "
        "rectangle geometry, palette, and counts of the records here are "
        "authoritative, per the implementation plan.",
    ]


def contract_section():
    return [
        "## The design contract",
        "",
        "Every cap comparison is componentwise: a color is within a cap "
        "when each of its red, green, and blue channels is at or below "
        "the cap's channel.",
        "",
        "- Base fields stay within `#0A0C13` through `#1C2130`, "
          "componentwise in and componentwise out.",
        "- Large secondary shapes stay at or below `#2A3146`.",
        "- Small accents stay at or below `#8A94AC` and cover at most "
          "2 percent of the canvas. Accent coverage is accounted over "
          "the complete logical canvas: accent-colored pixels painted in "
          "the final plate, divided by the full canvas area (960 x 544 "
          "or 544 x 960), with the opaque well and frame counting "
          "against the budget wherever they cover accent shapes.",
        "- Nothing brighter than `#2A3146` draws inside the quiet zones "
          "or the guard gaps of either orientation.",
        "- Accent shades may echo a piece hue; no large area ever takes "
          "a piece hue. Every base field and secondary shade stays at "
          "least 2 gray steps from all seven piece grays and below "
          "every piece gray, so the grayscale play mode keeps the "
          "pieces readable against every large area.",
        "- Motion: levels 1 through 4 are static. From level 5, at most "
          "one moving layer per design, drift or scroll at or below "
          "10 px/s, pulse cycles of at least 4 s. No blinking, no "
          "strobe. Wrapping is seamless: an element over 4 px in its "
          "motion axis wraps over a span that includes its own extent, "
          "so it leaves the canvas fully before re-entering; elements "
          "of 4 px or less may wrap at the margin span, where the "
          "crossing is sub-perceptual.",
        "- Level changes use a cut or a fade of at most 1 s in visible "
          "steps; fade colors respect the same caps.",
        "- Budget: at most 300 small elements or 40 band elements per "
          "design. Grouped elements (bands, rule sets, dither fields) "
          "count as one element but expand into primitives; the "
          "expanded counts live in each record's verified counts and in "
          "the manifests under `build/backgrounds/`.",
    ]


def main():
    results_path = os.path.join(REPO, "build", "backgrounds",
                                "check-results.json")
    if not os.path.exists(results_path):
        print("no check results at %s; run check.py first" % results_path)
        return 2
    with open(results_path) as handle:
        results = json.load(handle)
    if results["failures"]:
        print("check results carry %d failures; fix them before writing "
              "records" % len(results["failures"]))
        return 2
    measured = {row["level"]: row for row in results["designs"]}
    catalogue = designs.catalogue()

    os.makedirs(DOCS, exist_ok=True)
    for design in catalogue:
        write_record(design, measured[design["level"]])

    checks = results["checks"]
    lines = []
    lines.append("# The STACKFALL level backgrounds")
    lines.append("")
    lines.append("Twenty procedural level backgrounds in five chapters of "
                 "four. Every design is one pattern grammar in "
                 "`tools/backgrounds/designs.py` that draws axis-aligned "
                 "rectangles onto either canvas; the tools render the "
                 "plates and mockups, and the acceptance check verifies "
                 "every criterion of the brief mechanically.")
    lines.append("")
    lines.append("T075 designed the set from the operator brief. T077 "
                 "reconciled it with the owner-approved imagegen set "
                 "(approval 2026-11-05): the approved names, palettes, "
                 "concept lines, element counts, motion, and transitions "
                 "are the contract, drawn with the T075 grammar "
                 "machinery. The renderer integration follows as tasks "
                 "T078 through T085 under "
                 "[BACKGROUND_IMPLEMENTATION.md](../"
                 "BACKGROUND_IMPLEMENTATION.md).")
    lines.append("")
    lines.append("## Source and date")
    lines.append("")
    lines.append("- Design date: %s; reconciliation date: %s."
                 % (DESIGN_DATE, DESIGN_DATE))
    lines.append("- Brief: %s." % SOURCE_BRIEF)
    lines.append("- Approved set: %s." % SOURCE_APPROVAL)
    lines.append("- Layout mask: derived from the implemented geometry "
                 "of `include/stackfall/renderer_sdl.hpp` (`kBoardOrigin`, "
                 "`kBoardFrame`, the HUD zone constants, "
                 "`kPortraitBoard`). The brief promised a mask image that "
                 "never arrived; the approved prompts request the same "
                 "geometry this mask encodes, so the derived mask is the "
                 "authoritative one.")
    lines.append("")
    lines.extend(reference_section())
    lines.append("")
    lines.append("## The layout mask")
    lines.append("")
    lines.append("Landscape, 960x544:")
    lines.append("")
    lines.append("| Zone | Rectangle | Rule |")
    lines.append("| --- | --- | --- |")
    lines.append("| Board well | x 345..614, y 2..541 | opaque "
                 "`#0A0C13`; no design shows |")
    lines.append("| Board frame | x 344..615, y 1..542 | one-pixel ring "
                 "`#4E5874`; stays visible |")
    lines.append("| Statistics zone | x 192..319, full height | quiet: "
                 "nothing brighter than `#2A3146` |")
    lines.append("| Next zone | x 640..767, full height | quiet |")
    lines.append("| Gaps | x 320..343 and x 616..639 | quiet, 24 px each |")
    lines.append("| Loud margins | x 0..191 and x 768..959 | accents may "
                 "draw here |")
    lines.append("")
    lines.append("Portrait, 544x960:")
    lines.append("")
    lines.append("| Zone | Rectangle | Rule |")
    lines.append("| --- | --- | --- |")
    lines.append("| Board well | x 72..471, y 120..919 | opaque |")
    lines.append("| Board frame | x 71..472, y 119..920 | one-pixel ring |")
    lines.append("| HUD strip | y 0..118, full width | quiet |")
    lines.append("| Gaps | x 59..70 and x 473..484 | quiet, 12 px each |")
    lines.append("| Loud margins | x 0..58, x 485..543, y 921..959 | "
                 "accents may draw here |")
    lines.append("")
    lines.append("Mask sheets: [mask-landscape.png](images/"
                 "mask-landscape.png), [mask-portrait.png](images/"
                 "mask-portrait.png).")
    lines.append("")
    lines.extend(contract_section())
    lines.append("")
    lines.append("## The twenty designs")
    lines.append("")
    lines.extend(chapter_table(catalogue))
    lines.append("")
    lines.append("One record per level sits beside this file:")
    lines.append("")
    for design in catalogue:
        lines.append("- [%s](level-%02d.md) - level %d, %s" % (
            design["name"], design["level"], design["level"],
            design["concept"].rstrip(".")))
    lines.append("")
    lines.append("## The chapter arc")
    lines.append("")
    lines.append("The arc the brief demands: speed ramps from level 1 to "
                 "level 20, and the background follows it.")
    lines.append("")
    for name in ("Still", "Stir", "Build", "Pressure", "Peak"):
        lines.append("- %s (%s): %s" % (name, CHAPTER_RANGE[name],
                                        CHAPTER_ROLE[name]))
    lines.append("")
    lines.append("Measured on the rendered plates, at the one-eighth "
                 "review scale of the thumbnail sheet:")
    lines.append("")
    lines.extend(arc_table(results["designs"]))
    lines.append("")
    lines.append("All three measures rise chapter over chapter, and the "
                 "Peak chapter carries the most marks per sample of the "
                 "five.")
    lines.append("")
    lines.append("## Sheets")
    lines.append("")
    lines.append("- [thumbnails.png](images/thumbnails.png) - all 20 "
                 "designs at one-eighth scale, one row per chapter.")
    lines.append("- [summary.png](images/summary.png) - all 20 names with "
                 "their base color swatches.")
    lines.append("- [mask-landscape.png](images/mask-landscape.png) and "
                 "[mask-portrait.png](images/mask-portrait.png) - the "
                 "layout mask of both orientations.")
    lines.append("- `images/mockup-XX-landscape.png` and `images/"
                 "mockup-XX-portrait.png` - every design under the real "
                 "HUD and board.")
    lines.append("")
    lines.append("## Check results")
    lines.append("")
    lines.append("`%s` re-paints every plate from its manifest with an "
                 "independent rectangle-only painter, then runs every "
                 "criterion of the brief: componentwise palette caps, "
                 "accent coverage over the complete canvas, quiet-zone "
                 "brightness, well purity, frame visibility on the "
                 "mockups, motion bounds and layer count with the "
                 "seamless-wrap rule, verified element counts, budget, "
                 "grayscale separation from the seven piece colors, one "
                 "grammar per level, neighbor distinctness at one-eighth "
                 "scale, and the chapter rise above." % CHECK_COMMAND)
    lines.append("")
    lines.append("Run of %s: **%d checks, %d failures**. The results also "
                 "land in `build/backgrounds/check-results.json`."
                 % (DESIGN_DATE, checks, len(results["failures"])))
    lines.append("")
    lines.append("Regenerate everything with `tools/backgrounds/run.sh`.")
    lines.append("")
    lines.append("## Open questions")
    lines.append("")
    lines.append("1. The brief promised a layout mask image; none "
                 "arrived. The mask above is derived from the renderer "
                 "geometry, and the approved prompts request the same "
                 "zones, so it stands as authoritative.")
    lines.append("2. Accent share is accounted over the complete logical "
                 "canvas, as the implementation plan directs. Confirmed "
                 "by the owner's plan of 2026-11-05.")
    lines.append("3. An on-device look on the handheld screen remains "
                 "open, and the playtest revision pass of task T076 "
                 "stays blocked until the implementation tasks T078 "
                 "through T085 deliver a playable build.")
    lines.append("4. The renderer integration itself is task T078 "
                 "through T085; this package carries the contracts they "
                 "implement. The Still chapter of levels 1 through 4 "
                 "landed in the renderer with task T079, from these "
                 "records and the same seeds; the correction of that "
                 "task - the landscape rule row of First light, 440, "
                 "which the formula and the retained mockups always drew "
                 "and the record prose mistyped as 448 - is already in "
                 "level-02.md above. The Stir chapter of levels 5 "
                 "through 8 landed with task T080, again from these "
                 "records and the same seeds; its correction - the "
                 "tower-height share of Sleeping skyline, `0.10 + 0.24 * "
                 "(1 - corner closeness)`, which the machinery, mockups, "
                 "and runtime draw and the record prose mistyped as "
                 "0.30 - is in level-07.md above. The Build chapter of "
                 "levels 9 through 12 landed with task T081; its "
                 "correction - the wrap phase of Band aurora, anchored "
                 "one extent below the base row so a ribbon that has "
                 "left the top edge re-enters below the bottom edge "
                 "instead of popping back at its base row, and the pass "
                 "periods that follow, 161.25 s landscape and 276.5 s "
                 "portrait - is in level-11.md above. The Pressure "
                 "chapter of levels 13 through 16 landed with task "
                 "T082; its correction - the slide direction of "
                 "Undertow, whose generator offset advanced positively "
                 "and slid the staircase right against its own declared "
                 "velocity, its record, and the approved concept, all "
                 "of which name the leftward 6 px/s slide the generator "
                 "now draws - is in level-15.md above. The Peak "
                 "chapter of levels 17 through 20 landed with task "
                 "T083; its corrections - the cell pitch of Storm "
                 "lines, whose prose named a 22 px grid while the "
                 "generator, the retained mockups, and the runtime all "
                 "draw the 16 px grid, and the wrap note of the same "
                 "design, whose dashes wrap over margin height plus "
                 "the tallest dash's 6 px exactly as its trails do, not "
                 "at the bare margin span - are in level-18.md above.")
    lines.append("")
    with open(os.path.join(DOCS, "index.md"), "w") as handle:
        handle.write("\n".join(lines))
    print("wrote index and %d records to %s" % (len(catalogue), DOCS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
