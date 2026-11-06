# Implement the approved backgrounds

Date: 2026-11-05
Status: owner authorizes implementation and background options.

The owner reviews the imagegen gallery and says the mockups look great.
The owner requests Looper tasks to implement them and let the player turn
backgrounds off or keep one preferred background across all levels.
No further design approval is required to start this implementation round.

## Approved reference set

The approved gallery contains 20 concept plates. Each plate shows landscape
and portrait views. Use these concepts instead of inventing a different set.

Gallery: https://axinbox.com/h/hnd_SPp-Z2ogfsMJv4nP8y2yZguf7-cbUgX1xWKl43nXCH8

The anonymous gallery expires on 2026-11-08 unless claimed. Retain the local
references in the repository; the implementation must not depend on hosting.

Intake sources in the primary workspace:

- `/home/agent/stackfall/output/background-mocks/originals/level-01.png`
  through `level-20.png`: the original concept plates.
- `/home/agent/stackfall/output/background-mocks/gallery/concepts.json`:
  palettes, concepts, chapter order, intensity, and proposed motion.
- `/home/agent/stackfall/output/background-mocks/gallery/prompts.md`:
  the exact imagegen prompts.
- `/tmp/operator-background-brief.md`: the original design constraints.

The intake task retains the brief, this plan, reference metadata, and the
20 plates under tracked project paths. New worktrees must use those paths.
Do not copy credentials, AXINBOX records, or publish results into the design
package. Do not add Looper private snapshots to `source_files`.

The images establish the visual direction. They are not exact game captures.
Preserve the implemented board, HUD, type, piece palette, and input behavior.
Use the brief's limits and the real renderer geometry when images disagree.
Do not load the PNG plates at runtime. Draw every background procedurally.

| Level | Name | Chapter | Base | Secondary | Accent |
|---|---|---|---|---|---|
| 1 | Still ground | Still | #10121A | #0C0E16 | #283044 |
| 2 | First light | Still | #10131C | #1C2433 | #2A3146 |
| 3 | Distant stars | Still | #0E121C | #1B2230 | #64718C |
| 4 | Quiet strata | Still | #11141D | #222A39 | #2A3146 |
| 5 | Slow drift | Stir | #0F1420 | #20283A | #687892 |
| 6 | Dither dusk | Stir | #121620 | #242B3A | #38445B |
| 7 | Sleeping skyline | Stir | #11151F | #252D3D | #64728A |
| 8 | Low signal | Stir | #11151F | #242C3B | #54617A |
| 9 | Fracture | Build | #10151F | #283044 | #707D96 |
| 10 | Circuit hush | Build | #111721 | #283144 | #6B8498 |
| 11 | Band aurora | Build | #131725 | #283044 | #67758D |
| 12 | Long trails | Build | #111620 | #293144 | #76849B |
| 13 | Deep grid | Pressure | #121722 | #293145 | #79869B |
| 14 | Fine snow | Pressure | #131923 | #293145 | #7A879E |
| 15 | Undertow | Pressure | #131923 | #2A3146 | #74839B |
| 16 | Slow pulse | Pressure | #141A26 | #2A3146 | #7C88A0 |
| 17 | Rising ember | Peak | #17191F | #2A3040 | #897A72 |
| 18 | Storm lines | Peak | #131925 | #2A3146 | #8290A7 |
| 19 | Compression | Peak | #171C28 | #2A3146 | #8591A8 |
| 20 | Collapse | Peak | #191E2B | #2A3146 | #8A94AC |

## Design contract and renderer

Extend the existing T075 package through a follow-up task. Keep active T075
work intact. Reconcile its records with the approved concepts after it lands.
Each final record states exact sizes, spacing, placement, deterministic seed,
element count, orientation adaptation, motion, transition, and palette roles.
Correct any proposed motion that needs discontinuous wrapping or flashing.

Use solid axis-aligned rectangles. Step all diagonals and curves. Do not use
textures, image loading, shaders, smooth gradients, blur, or external assets.
Use a separate deterministic cosmetic generator. Never consume gameplay
randomness or change the fixed simulation step, score, or piece sequence.
Use a bounded background clock. Freeze motion and transition progress during
pause and focus loss. Resume without a time jump. Reset predictably on a new
round. Keep rendering read-only with respect to game state.

Draw wallpaper only outside the opaque well and frame. Derive protected
regions from the existing geometry for both orientations. Keep a dark quiet
region under the statistics and previews. Background shapes must never draw
inside the well, even if the well would cover them later.

Largest base fields stay within #0A0C13 through #1C2130. Large secondary
shapes stay no brighter than #2A3146. Tiny accents stay no brighter than
#8A94AC and cover at most two percent of the canvas. No background pixel
under the landscape HUD columns or portrait HUD strip exceeds #2A3146.
Define a documented componentwise comparison against these color caps.
Check accent coverage against the complete logical canvas. Preserve all
seven piece colors and keep their luminance above the base fields.

Levels 1 through 4 are static. Later levels use at most one moving layer.
Drift stays at or below 10 pixels per second. Pulses take at least four
seconds. No blinking, strobe, or discontinuous visible resets are allowed.
Keep each design within 300 small elements or 40 large bands. Document
primitive expansion and transition costs; grouping must not hide excess work.
A transition uses a cut or a fade of at most one second in visible steps.
Fade colors must respect the same caps. Avoid doubling the rendering budget
by drawing two full backgrounds during a fade.

Grayscale mode keeps the background palette as designed, as the brief
requires. Verify separation from grayscale pieces, previews, and the ghost.
Apply orientation changes at the existing safe boundary. Rebuild background
geometry without leaking input or restarting the game. Keep letterbox bars
and non-play menu pages at their existing appearance. Pause and game-over
overlays retain the selected play wallpaper beneath them.

## Player options

Add one BACKGROUNDS row to OPTIONS. It opens a compact submenu with the
existing keyboard and controller menu controls. Preserve other options.

- **Background mode:** Auto, Off, or Fixed. Default: Auto.
- **Preferred background:** all 20 designs, shown by stable number and name.
  Default: 1, Still ground. Selecting a preference does not leave Off or
  Auto; change the mode explicitly to Fixed to use that preference.

Auto selects the current level's design, clamped to levels 1 through 20.
It changes on level changes with the final record's cut or stepped fade.
Off restores the legacy flat #10121A wallpaper. It does not hide the board,
frame, grid, pieces, previews, or text. Off skips pattern and animation work.
Fixed uses the preferred design at every level. Level changes must not change
or restart that design, its motion, or its transition. Fixed remains fixed
across rounds and relaunches. The selected design may still animate according
to its specification. Keep the preferred number when switching to Off or Auto.

Show the mode and preferred name clearly in both orientations. Every design
must be reachable with either input method. Back returns to OPTIONS with the
cursor on BACKGROUNDS. Do not add gameplay input actions or alter bindings.
Apply changes in the same session, on the next play frame. No app restart or
new game is required to apply the preference.

Save the mode and preferred number atomically whenever either changes.
Load all valid legacy settings forms without losing any existing preference.
Missing new fields use Auto and design 1. Reject malformed values and IDs
outside 1 through 20 using the current recovery policy. Never change a saved
background ID's meaning. Read operations must not rewrite settings files.

## Validation and acceptance

Retain real game captures for all 20 backgrounds in both orientations.
Generated plates remain visual references; actual captures establish results.
Check complete outputs and instrumented draw commands for palettes, regions,
counts, accent area, motion bounds, static early levels, and transitions.
Use representative intermediate motion phases and transition steps so a
single good screenshot cannot hide a violation. Check frame and HUD
readability, neighboring thumbnails, and the chapter progression visually.

Exercise Auto at level boundaries and the level-20 cap. Exercise every fixed
ID, Off, mode changes, persistence, old settings, and invalid settings.
Cover keyboard, virtual controller, both orientations, grayscale, pause,
resume, focus loss, restart, and repeated orientation changes.
Verify identical game state hashes and piece sequences for identical seeds
and inputs across background modes. Run relevant Debug and Release checks.
Background Off must keep the legacy play pixels apart from intentional new
menu rows. Update intentional screenshot fixtures with reviewed evidence.

Measure worst-case background and transition cost with levels 17 through 20
in both orientations. Compare Auto, Fixed, and Off under the same workload.
Record draw counts, CPU cost, memory behavior, and frame-time percentiles.
Avoid per-frame heap allocation and unbounded data. Do not claim RG503 frame
pacing from a dummy renderer or a desktop display. Physical device gates
remain open until the required device evidence exists.

Update docs/SPEC.md, README.md, docs/AGENT_TESTING.md, and the design records
with the implemented options, settings format, and background behavior.
Record validation evidence in the existing agent and performance notes.
Keep the existing T076 visual playtest task. Its owner-approval prerequisite
is satisfied by this request. Its playable-build prerequisite remains until
the implementation and validation land. Then run T076 and revise weak slots.
Do not create a duplicate playtest task or reopen completed tasks.

## Looper task structure

The following nine tasks are already added with `looper add-task`.
The CLI acknowledges request `settings-35779c68b95991fa` as applied.
Do not create duplicate tasks when importing this plan.

1. T077 retains references and finalizes the deterministic contracts.
2. T078 builds the procedural background renderer and clock.
3. T079 implements Still, levels 1 through 4.
4. T080 implements Stir, levels 5 through 8.
5. T081 implements Build, levels 9 through 12.
6. T082 implements Pressure, levels 13 through 16.
7. T083 implements Peak, levels 17 through 20.

T084 adds the submenu with saved Auto, Off, and Fixed modes.
T085 performs the visual, behavioral, and performance validation.
The chapter tasks share the renderer catalog, so order their dependencies.
Use the CLI to reconcile the existing design and playtest tasks. Do not
replace the live backlog file by hand. Keep unrelated task state intact.

Import reconciliation must preserve active T075 unchanged. T077 owns the
follow-up reconciliation against the approved images after T075 lands.
Retain this plan as requirements for T077 through T085. Add its project path
to `source_files` only when that path is retained in the task worktrees.
Use project requirement files instead of private operation snapshots.

Update pending T076 to depend on both T075 and T085. Record the owner design
approval from 2026-11-05 as satisfied. Keep T076 blocked while the playable
implementation is missing. Its remaining unblock condition is that T085
passes on a playable build with the approved catalog and background options.
Then remove its deferred marker and let the visual review proceed.
Do not mark T076 done or ask for another approval of the same concepts.
No task additions beyond T077 through T085 are required by this import.
