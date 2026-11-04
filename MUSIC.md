# Music implementation plan

Date: 2026-11-04.

## Goal

Give STACKFALL a varied original soundtrack with chip melodies and strong beats.
Deliver 12 tracks across three collections: Arcade, Groove, and Focus.
Use generated instruments and a native sequencer. Keep the game offline.

Each track has a distinct melody, bass pattern, rhythm, and arrangement.
Each arrangement lasts two to three minutes before its loop repeats.
A track must remain pleasant during a long game.

This document plans the work. It does not submit Looper tasks or change game behavior.
Phase labels below are planning identifiers, not assigned backlog task IDs.

## Reference and existing behavior

The user supplied the [Strudel getting started page](https://strudel.cc/workshop/getting-started/).
Its “coastline” example layers drums, bass, extended chords, and a changing melody.
Use its layered rhythm and evolving arrangement as musical references.
Compose original melodies and patterns for STACKFALL.
The reference code was reviewed on 2026-11-04. No listening comparison was performed.

The existing implementation is in `src/audio_sdl.cpp` and `include/stackfall/audio_sdl.hpp`.
It generates five effects from square, triangle, and sine waves at startup.
It plays signed 16-bit pulse-code modulation (PCM) through Simple DirectMedia Layer 2 (SDL2).
The device requests 44,100 samples per second, with stereo and a mono fallback.
The callback mixes one voice per effect. It currently has no music sequencer.

The specification excludes music files. This plan keeps that asset restriction.
Update `docs/SPEC.md` to add generated music and its controls explicitly.
Keep the existing rules, piece sequence, scoring, and input timing unchanged.

## Musical direction

Use pulse leads, triangle bass, short arpeggios, and generated percussion.
Use soft chord voices where they improve the Groove and Focus collections.
The soundtrack can exceed old console channel limits.
Its identity comes from the instruments and composition, not hardware emulation.

- Arcade uses clear melodic hooks, lively bass, and crisp drums.
- Groove uses swing, syncopation, warm chords, and fuller percussion.
- Focus uses sparse melodies, softer timbres, and restrained percussion.

Avoid constant high notes, harsh sustained pulse tones, and repeated dramatic fills.
Leave space for the game's short effects.
Do not trigger a musical accent for every movement or rotation.

### Track catalogue

Tempos use beats per minute (BPM). Track IDs remain stable after release.
Names are working titles. All tracks use four beats per bar.

| ID | Collection | Title | BPM | Musical brief |
| --- | --- | --- | ---: | --- |
| arcade-boot | Arcade | Boot Sequence | 112 | Warm pulse motif, light drums, welcoming title theme |
| arcade-bag | Arcade | Seven Bag | 128 | Bright melody, bubbling arpeggios, strong bass response |
| arcade-kick | Arcade | Wall Kick | 138 | Syncopated bass, clipped chords, sharp rhythmic lead |
| arcade-redline | Arcade | Redline | 150 | Fast chip techno, driving drums, short quiet breaks |
| groove-pocket | Groove | Pocket Circuit | 94 | Swung beat, warm chords, short chip hook |
| groove-night | Groove | Night Bus | 104 | Rounded bass, minor ninth chords, sparse melody |
| groove-neon | Groove | Neon Steps | 118 | Broken beat, shifting arpeggios, offbeat accents |
| groove-afterimage | Groove | Afterimage | 126 | House rhythm, chord stabs, playful counter melody |
| focus-ghost | Focus | Ghost Grid | 88 | Soft triangle melody, spacious rhythm, gentle bass |
| focus-drop | Focus | Soft Drop | 100 | Gentle arpeggios, steady beat, restrained percussion |
| focus-orbit | Focus | Orbit | 110 | Repeating motifs, slow chord changes, small rhythmic variations |
| focus-clear | Focus | Clear Skies | 116 | Relaxed melodic groove, open arrangement, uplifting ending |

### Arrangement contract

Use phrases of eight or sixteen bars. Choose each track's total bars from its tempo.
For example, 64 bars at 128 BPM lasts 120 seconds.
At 88 BPM, 48 bars lasts about 131 seconds.
A fixed bar count is not required across the catalogue.

Each track includes these elements:

- An introduction that establishes its rhythm without every layer playing.
- Two distinct sections with a clear melodic or rhythmic difference.
- An alternate section, breakdown, or counter melody.
- Small fills and instrument changes across phrases.
- A loop boundary that preserves the beat and avoids a click.
- Low, medium, and high intensity layer maps that share the same timeline.

Do not count alternate intensity mixes as separate tracks.
Keep essential melody and harmony coherent when optional layers are absent.
Use fixed patterns for the first release. Any later variation uses a music-only seed.

## Player controls

Keep the existing Sound control. Define it as the master switch for all audio.
Keep its current keyboard and controller shortcuts as master mute controls.
Retain the last music mode and both volume settings while muted.

Add these settings:

| Setting | Values | Default | Behavior |
| --- | --- | --- | --- |
| Music | Auto, Arcade, Groove, Focus, Off | Auto | Select the track pool; Off stops music only |
| Music volume | 0 through 10 | 6 | Change music gain; 0 is silent |
| Effects volume | 0 through 10 | 8 | Change effect gain; 0 is silent |
| Next track | Action | None | Request another track in the selected pool |

Auto draws from all three collections. Other modes restrict the pool.
Next track is disabled when Music is Off. Master mute does not block track selection.
A volume step must produce a perceptually useful change. Define an explicit gain table.

Save mode and volume changes immediately through the existing settings store.
Next track is an action, so it does not write settings.
Do not save the playback position or shuffle position between launches.

Show the current track title on the music settings page.
Keep the ordinary play screen free of a permanent music label.
Use a Music submenu from Options if the expanded settings exceed the available space.
The submenu contains the four rows above and uses existing menu controls.
Both keyboard and controller must reach every row and return to the same Options row.
Coordinate this layout with `docs/OPTIONS_EXPANSION.md` when that work is available.

## Playback behavior

| Event or scene | Required behavior |
| --- | --- |
| First title screen | Start Boot Sequence at low intensity when music is enabled |
| Start a round | Select from the current pool and fade into its first bar |
| Levels 1–5 | Use the low intensity map |
| Levels 6–12 | Use the medium intensity map |
| Levels 13–20 | Use the high intensity map |
| Level boundary | Apply the new layer map at the next eight-bar phrase |
| Complete arrangement | Select the next track without an immediate repeat |
| Pause or paused Controls | Fade out over 100 milliseconds, then freeze music transport |
| Resume | Continue the frozen position with a 100-millisecond fade in |
| Game over | Fade music over 250 milliseconds; keep the game-over effect audible |
| Return to title | Restore the title theme with a short transition |
| Music Off | Fade music to silence over 100 milliseconds and stop transport |
| Master mute | Fade both buses to silence; preserve the current music timeline |
| Focus loss | Follow the game's pause policy; never resume an otherwise paused round |
| Missing audio device | Continue silently without blocking startup or gameplay |

Keep each track's tempo steady. Intensity changes add or remove layers.
Do not speed up the whole soundtrack when the level increases.
Do not change music in response to stack height in the first release.

Shuffle without repetition within a pool. Refill the pool after all tracks play.
Prevent the final track of one pool cycle from repeating at the next cycle's start.
Exclude the current track when processing Next track if another track is available.

For Next track or a mode change, schedule the transition at the next bar.
Fade the old track for 100 milliseconds before that boundary.
Start the new track on its downbeat with a 100-millisecond fade in.
Do not overlap tracks with different tempos during the first release.
Coalesce repeated requests so only the latest requested destination remains pending.

When returning from Music Off, start a fresh track from the selected pool.
Master mute differs: keep transport moving unless the scene has paused it.
After game over, leave music silent until another scene requests playback.

## Native audio design

### Modules and ownership

Keep sequencing and synthesis independent of SDL2 and the rules library.
Use a separate music library rather than adding music state to `Game`.

Suggested files:

| File | Responsibility |
| --- | --- |
| `include/stackfall/music.hpp` | Track IDs, events, transport, instruments, render interface |
| `src/music.cpp` | Sequencer, voice rendering, envelopes, gain ramps |
| `src/music_tracks.cpp` | Catalogue, patterns, arrangements, intensity maps |
| `include/stackfall/music_director.hpp` | Scene and level inputs; music policy |
| `src/music_director.cpp` | Pool selection, title behavior, transitions |
| `src/audio_sdl.cpp` | Existing device, effect voices, final mix, callback integration |
| `src/storage.cpp` | Versioned preference loading and saving |
| `tests/test_music.cpp` | Device-independent transport and synthesis checks |
| `tests/test_audio_sdl.cpp` | Device integration, controls, and mixing checks |

Keep this split small. Add files only when they separate a clear responsibility.
Adapt the platform call sites if the pending main-loop refactor has already landed.

### Score representation

Store compositions as typed, immutable C++17 data.
A track contains its ID, title, collection, tempo, patterns, and arrangement.
A note event contains its start tick, length, pitch, velocity, instrument, and layer.
Use separate instrument parameters for pulse width, envelope, filter, and noise type.
Represent percussion with explicit instrument IDs.

Start with 96 musical ticks per quarter note.
This supports straight sixteenths, triplets, and fixed swing offsets.
Validate event order, pattern bounds, instrument IDs, and maximum simultaneous voices.
Expand and validate arrangement data before playback, outside the audio callback.
Apply intensity at phrase boundaries. Release removed voices without cutting their samples abruptly.

Use a sample-based clock with a rational or fixed-point remainder.
Do not advance music from the 60 Hz game tick or wall-clock time.
Preserve fractional timing across callback boundaries to prevent cumulative drift.
Playback speed and pitch must follow the obtained device sample rate.

### Instruments

Provide a compact shared instrument bank:

- Pulse lead with two or three useful pulse widths and a soft envelope.
- Triangle bass with a short attack and controlled release.
- Soft sine or triangle chord voices.
- A short pitch sweep for the kick drum.
- Filtered noise and a short tone for the snare.
- Short and open noise envelopes for hats.
- A quiet metallic or noise accent for occasional fills.

Precompute wave tables, envelope tables, and filter coefficients outside the callback.
Use band-limited wave tables for pulse voices to control harsh upper frequencies.
The callback performs bounded voice generation using those tables.
Do not render complete multi-minute tracks into memory at startup.
Use a small fixed delay buffer only if previews show that it improves the music.

### Thread communication

The main thread sends scene, level, mode, volume, and transition requests.
The callback owns transport, active voices, effect positions, and gain ramps.
It reads immutable scores and instrument data.

Use a fixed-capacity single-producer, single-consumer command queue.
Use latest-value controls for volume and master mute so queue pressure cannot lose them.
Document publication order and thread ownership. Verify required atomics are lock-free on the target.
Do not log, allocate, read files, or acquire mutexes inside the callback.
Report counters from the main thread through safe snapshots.

Limit the initial engine to 12 simultaneous music voices.
The existing five effect voices have separate capacity and remain available during dense music.
When music exceeds its voice limit, release the quietest optional voice first.
Validate the catalogue so normal arrangements never need this fallback.

### Mixing and gain

Mix music and effects into separate 32-bit accumulators.
Apply their volume controls, then combine them and convert to signed 16-bit output once.
Keep a final safety clamp. Tune arrangements to avoid hitting it during normal play.
Do not preserve the current repeated per-voice saturation when adding the music bus.

Duck music by about 4 decibels during clear effects and game over.
Use a short attack and a release near 200 milliseconds.
Do not duck on every move or rotation.
Use gain ramps for mute, volume, track transitions, and scene changes.
Effects keep their existing trigger rules and cue counters.

## Persistence and compatibility

Extend the settings format with music mode and both volume values.
Choose a new canonical version after reviewing any intervening settings changes.
Keep one serializer and one parser for the resulting format.

- Existing Sound Off remains master audio Off after an upgrade.
- Existing Sound and Ghost choices remain intact.
- Missing new values receive Auto, music volume 6, and effects volume 8.
- Preserve Grid, Hold, Next pieces, and Grayscale if those settings have landed.
- Reject unsupported enum values and volumes outside 0–10.
- Keep the documented recovery policy for malformed or unreadable files.
- Retain atomic writes and report save failures without stopping the game.

Music state never enters the gameplay state hash or gameplay randomizer.
Replays produce the same gameplay results with every music setting.
Music output can be deterministic for tests without requiring callback timing in replay files.

## Composition workflow

Use Strudel as an optional composition and preview tool.
The shipped game does not require JavaScript, a browser, or a network connection.
The native score is the authoritative playable composition.

For each track, keep a short composition record under `docs/music/`.
Record its tempo, tonal center, form, instrument choices, and arrangement map.
Retain optional Strudel source with the record when it helps future editing.
Avoid preview patches that depend on downloaded samples if the native engine cannot reproduce them.
Document differences between a Strudel sketch and the native preview.

Generate development previews through the native engine as standard audio files.
Preview files are review artifacts, not required game assets.
Keep generated files in an ignored build directory.

First compose these complete tracks:

1. Seven Bag establishes the Arcade sound.
2. Pocket Circuit establishes the Groove sound.
3. Ghost Grid establishes the Focus sound.

Listen to each complete arrangement and its loop boundary.
Compare quiet and intense mixes with gameplay effects enabled.
Record feedback before composing the other nine tracks.
This review is a work milestone, not an additional publication approval requirement.

## Implementation phases

### M1: Define contracts and amend the specification

Dependencies: none.

Document the score format, controls, scene table, and settings migration.
Update the specification to include native generated music.
Resolve overlap with the pending options and preference tasks before assigning file ownership.

Acceptance: controls and defaults are explicit; no conflicting settings format remains.
Record module boundaries and the target voice limit.

### M2: Build the sequencer and instruments

Dependencies: M1.

Implement score validation, timing, voices, envelopes, and offline rendering.
Add a developer renderer that can select a track, duration, sample rate, and intensity.
Use a short test composition before the full catalogue exists.

Acceptance: timing, chunk independence, loop behavior, and instrument tests pass.
The renderer produces playable preview files without an audio device.

### M3: Compose the three reference tracks

Dependencies: M2.

Complete Seven Bag, Pocket Circuit, and Ghost Grid.
Deliver full arrangements and all three intensity maps.
Render native previews and inspect waveform levels.

Acceptance: listening notes cover melody, rhythm, fatigue, loops, and effect audibility.
Record any instrument changes needed before expanding the catalogue.

### M4: Integrate playback and scene policy

Dependencies: M2 and M3.

Connect the engine to the existing audio device.
Implement pools, level layers, track transitions, pause, mute, and game-over behavior.
Add separate gain paths and safe thread communication.

Acceptance: automated scene sessions match the behavior table.
Effects remain audible, and audio work never changes gameplay hashes.

### M5: Add controls and persistent preferences

Dependencies: M1 and M4.

Implement the music settings page, track title, volumes, and Next track action.
Add migration, failure recovery, and immediate saving.
Integrate with the expanded Options screen without duplicate settings owners.

Acceptance: keyboard and controller sessions cover every setting and return path.
Preferences survive restart; legacy records retain their saved choices.

### M6: Complete the remaining nine tracks

Dependencies: M3 and M4.

Compose the remaining catalogue after applying the reference-track findings.
Balance perceived loudness across collections and intensity maps.
Check every arrangement, transition, and loop through the native renderer.

Acceptance: 12 distinct tracks exist. Each has a two-to-three-minute arrangement.
Every track meets the voice budget and has recorded listening results.

### M7: Validate desktop and handheld playback

Dependencies: M4, M5, and M6.

Run relevant Debug and Release suites and the extended desktop harness.
Measure audio cost and frame timing on the Anbernic RG503.
Update user instructions, audio documentation, and performance notes.

Acceptance: the checks below pass. Report unavailable hardware evidence explicitly.
Desktop results do not count as a physical handheld result.

## Validation plan

### Automated engine checks

- Render at 22,050, 44,100, and 48,000 samples per second.
- Verify note times and loop duration within one output sample of the rational schedule.
- Compare one large render with varied callback chunks, including chunks of one frame.
- Render repeated loops and check for accumulated timing drift.
- Test swing, rests, overlapping releases, intensity boundaries, and pool exhaustion.
- Test command pressure, voice limits, mute ramps, and repeated Next track requests.
- Reject invalid scores before playback.

### Mixing and integration checks

- Exercise mono and stereo output at different callback sizes.
- Verify independent volumes and master mute, including already-playing effects.
- Check peak amplitude, clipping counts, silence, and unintended constant offsets.
- Stress the loudest arrangement with all effect voices active.
- Test pause, resume, game over, restart, title, and unavailable audio.
- Verify old and new settings, failed writes, and restart persistence.
- Compare gameplay hashes with music enabled, disabled, and every collection selected.

Use thread and memory sanitizers where the target tools support them.
Keep existing effect count and duration checks meaningful after integration.
Do not treat an audio checksum as proof of musical quality.

### Listening checks

Listen to every complete native arrangement on headphones and handheld speakers.
Check melody identity, bass clarity, percussion balance, harshness, and repetition.
Inspect pause ramps, loop boundaries, and transitions for clicks or gaps.
Test a long session with frequent movement, rotation, and clears.
Record findings per track in `docs/music/`.
An agent must not claim a listening check from source inspection alone.

### Performance targets

These are proposed budgets. Validate them on the target device.

- No callback allocation, file access, logging, or blocking synchronization.
- At most 12 music voices plus the existing five effect voices.
- At most 8 mebibytes of incremental audio memory, including scores and tables.
- Audio callback processing at the 99th percentile stays below 25% of its buffer duration.
- A 30-minute worst-case device session has no observed audio dropout.
- The game continues to meet its existing performance gate with music enabled.

Measure at the obtained sample rate and actual device buffer size.
Record callback maximums as well as percentiles through a development-only measurement path.
If a budget fails, reduce optional voices, tables, or effects before reducing catalogue size.
Do not require a low-quality fallback until measurements establish that need.

## Completion checklist

- [x] The specification includes generated music and matches these controls.
- [x] All 12 original tracks have complete arrangements and intensity maps.
- [x] Music and effects share one audio device with separate volume controls.
- [x] Track selection, scene transitions, pause, and mute follow the behavior table.
- [x] Settings migrate without losing existing player choices.
- [x] Keyboard and controller can operate the music settings page.
- [x] Gameplay sequences and hashes remain unchanged.
- [x] Automated audio and relevant existing tests pass.
- [x] Native previews exist for every track; listening findings stay pending
      per track in `docs/music/` until the human pass.
- [x] Desktop performance evidence is recorded.
- [ ] Handheld listening and performance evidence is recorded, or remains an
      explicit release prerequisite.
- [x] User documentation explains music selection, volumes, and master mute.
