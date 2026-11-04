# Options screen expansion

Requested on 2026-11-04.

Add the following settings to the existing Options screen. Keep the Sound
and Ghost settings available. Use the existing keyboard and controller
menu controls.

- **Grid:** On or Off. Show or hide the board grid. Default to On.
- **Hold:** On or Off. Default to On. When Off, ignore all hold inputs and
  hide the Hold preview. When On, keep the existing hold rules.
- **Next pieces:** An integer from 1 through 5. Default to 5. Show exactly
  that number of upcoming pieces, in queue order. Change only the visible
  preview count; keep the randomizer and piece sequence unchanged.
- **Grayscale:** On or Off. Default to Off. When On, render the play scene
  in grayscale, including settled pieces, the active piece, the ghost,
  Hold, and Next previews. Keep shapes, outlines, text, and contrast readable.
  When Off, restore the normal colors.

Save each preference when it changes. Restore the preferences on the next
launch. Load older settings files without losing their Sound or Ghost
values. Use the defaults above for missing new values. Follow the existing
recovery behavior for invalid settings files.

Fit all six settings and the navigation hints inside the 960 by 544 canvas.
Show the current value for each setting. Allow the player to reach every
row with both keyboard and controller input. Selecting Next pieces cycles
through 1, 2, 3, 4, 5, then 1.

## Acceptance criteria

1. Both input methods can change all four new settings and return to the
   title screen. Existing Sound and Ghost behavior still works.
2. Grid Off removes the grid. Grid On restores it without changing pieces.
3. Hold Off prevents every mapped hold input from consuming or swapping a
   piece. Hold On retains the existing once-per-piece restriction.
4. Each Next pieces value shows exactly the selected number of previews.
   The same seed and inputs produce the same piece sequence at every value.
5. Grayscale On removes color from the play scene while retaining readable
   pieces, previews, text, and the ghost. Off restores the normal palette.
6. All preferences survive a restart. Legacy settings keep their saved
   Sound and Ghost values and receive the new defaults.
7. Automated checks cover menu input, persistence, hold behavior, preview
   counts, and grayscale rendering. Update the specification and user
   documentation to describe the settings. Relevant existing tests pass.

## Project context

Use `docs/SPEC.md` for the existing rules, menu behavior, rendering, and
persistence requirements. Use `docs/AGENT_TESTING.md` for desktop checks.
This request extends the two-row Options screen described in the current
specification. Coordinate with pending preference refactoring where needed.
