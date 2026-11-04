# Redline (`arcade-redline`)

| Field | Value |
| --- | --- |
| Collection | Arcade |
| Catalogue index | 3 of 12 |
| Tempo | 150 BPM |
| Length | 80 bars, about 128 seconds |
| Key and mode | E2 minor (MIDI root 40) |
| Swing | straight sixteenths |
| Composed by | The native Arranger of `src/music_tracks.cpp`; no sketch, no samples, no randomness |

## Form

The arrangement contract of MUSIC.md: Intro bars 0-7; A bars 8-23; B bars 24-39; Breakdown bars 40-47; A' bars 48-79.

## Harmony

- Progression A: scale degrees 0-6-5-6 (i-VII-VI-VII).
- Progression B: scale degrees 0-3-4-6 (i-iv-v-VII).

## Instruments

- Lead: PulseLead 15-event motifs, the longest lines of the pool.
- Bass: TriangleBass Drive8 at the fastest harmonic rate of the catalogue.
- Drums: HousePump four-on-the-floor; FocusSparse figures in the breakdown.
- Harmony: Seventh stabs from the medium map.

## Character

The Arcade ceiling: 150 BPM, relentless. The fastest track; the engine's voice high-water of 12 sounds here at high intensity.

## Layers

The three intensity maps share this timeline: the essential melody,
bass, and kick anchor play at every intensity; the pad or stab layer and
the medium drum figures join from the medium map up; the high drum fills,
the arpeggio or counter voice, and the seventh stabs belong to the high
map alone. The engine releases a layer's held voices when the map drops.

## Preview

Rendered by the offline native engine at 44100 Hz:

- `build/music-previews/arcade-redline-low-44100.wav`
- `build/music-previews/arcade-redline-medium-44100.wav`
- `build/music-previews/arcade-redline-high-44100.wav`

The level report of these renders sits in `build/music-previews/report.txt`.

## Listening status

Pending. A human listening pass on headphones and on the handheld
speakers must confirm the mix, the loop seam, and the intensity steps; an
agent must not claim a listening check from source inspection alone
(MUSIC.md, listening checks).
