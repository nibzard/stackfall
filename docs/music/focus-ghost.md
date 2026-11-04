# Ghost Grid (`focus-ghost`)

| Field | Value |
| --- | --- |
| Collection | Focus |
| Catalogue index | 8 of 12 |
| Tempo | 88 BPM |
| Length | 48 bars, about 131 seconds |
| Key and mode | C2 minor (MIDI root 36) |
| Swing | straight sixteenths |
| Composed by | The native Arranger of `src/music_tracks.cpp`; no sketch, no samples, no randomness |

## Form

The arrangement contract of MUSIC.md: Intro bars 0-7; A bars 8-23; B bars 24-31; A' bars 32-47.

## Harmony

- Progression A: scale degrees 0-5 (i-VI, two-chord).
- Progression B: scale degrees 0-3 (i-iv).

## Instruments

- Lead: PulseLead 4-event cells; long spaces between entries.
- Bass: HalfNote whole-bar roots, the sparsest bass of the catalogue.
- Drums: FocusSparse: kick and rim-like snare, hats only from the medium map.
- Harmony: ChordVoice pads on every chord, no stabs at any intensity.

## Character

The quietest track; the level 1-5 companion. Two chords per progression and 48 bars: the shortest, slowest arrangement. The timing suite renders this one at three sample rates because its long bars stress the loop math.

## Layers

The three intensity maps share this timeline: the essential melody,
bass, and kick anchor play at every intensity; the pad or stab layer and
the medium drum figures join from the medium map up; the high drum fills,
the arpeggio or counter voice, and the seventh stabs belong to the high
map alone. The engine releases a layer's held voices when the map drops.

## Preview

Rendered by the offline native engine at 44100 Hz:

- `build/music-previews/focus-ghost-low-44100.wav`
- `build/music-previews/focus-ghost-medium-44100.wav`
- `build/music-previews/focus-ghost-high-44100.wav`

The level report of these renders sits in `build/music-previews/report.txt`.

## Listening status

Pending. A human listening pass on headphones and on the handheld
speakers must confirm the mix, the loop seam, and the intensity steps; an
agent must not claim a listening check from source inspection alone
(MUSIC.md, listening checks).
