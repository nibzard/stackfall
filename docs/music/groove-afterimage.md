# Afterimage (`groove-afterimage`)

| Field | Value |
| --- | --- |
| Collection | Groove |
| Catalogue index | 7 of 12 |
| Tempo | 126 BPM |
| Length | 72 bars, about 137 seconds |
| Key and mode | G2 minor (MIDI root 43) |
| Swing | straight sixteenths |
| Composed by | The native Arranger of `src/music_tracks.cpp`; no sketch, no samples, no randomness |

## Form

The arrangement contract of MUSIC.md: Intro bars 0-7; A bars 8-23; B bars 24-39; Breakdown bars 40-47; A' bars 48-71.

## Harmony

- Progression A: scale degrees 0-3-5-6 (i-iv-VI-VII).
- Progression B: scale degrees 0-5-3-5 (i-VI-iv-VI).

## Instruments

- Lead: PulseLead 9-event motifs answered by a PulseWide counter line from the high map.
- Bass: Drive8 in the grooves, HalfNote in the breakdown.
- Drums: HousePump with FocusSparse breakdown figures.
- Harmony: Pads low, plain stabs medium, seventh stabs high.

## Character

Straight-time house at the top of the Groove band. The counter voice enters only at high intensity, so the high map carries a second melody line no other layer plays.

## Layers

The three intensity maps share this timeline: the essential melody,
bass, and kick anchor play at every intensity; the pad or stab layer and
the medium drum figures join from the medium map up; the high drum fills,
the arpeggio or counter voice, and the seventh stabs belong to the high
map alone. The engine releases a layer's held voices when the map drops.

## Preview

Rendered by the offline native engine at 44100 Hz:

- `build/music-previews/groove-afterimage-low-44100.wav`
- `build/music-previews/groove-afterimage-medium-44100.wav`
- `build/music-previews/groove-afterimage-high-44100.wav`

The level report of these renders sits in `build/music-previews/report.txt`.

## Listening status

Pending. A human listening pass on headphones and on the handheld
speakers must confirm the mix, the loop seam, and the intensity steps; an
agent must not claim a listening check from source inspection alone
(MUSIC.md, listening checks).
