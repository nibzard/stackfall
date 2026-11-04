# Night Bus (`groove-night`)

| Field | Value |
| --- | --- |
| Collection | Groove |
| Catalogue index | 5 of 12 |
| Tempo | 104 BPM |
| Length | 64 bars, about 148 seconds |
| Key and mode | F#2 minor (MIDI root 42) |
| Swing | 10 ticks of swing on every eighth offbeat |
| Composed by | The native Arranger of `src/music_tracks.cpp`; no sketch, no samples, no randomness |

## Form

The arrangement contract of MUSIC.md: Intro bars 0-7; A bars 8-23; B bars 24-39; Breakdown bars 40-47; A' bars 48-63.

## Harmony

- Progression A: scale degrees 0-5-3-4 (i-VI-iv-v).
- Progression B: scale degrees 0-2-5-6 (i-III-VI-VII).

## Instruments

- Lead: PulseLead 5-event cells, the smallest melodic units of the catalogue.
- Bass: TriangleBass Bouncy root-fifth on the strongs.
- Drums: GrooveSwing at a lighter 10-tick swing.
- Harmony: Pads through the low map, stabs joining from medium.

## Character

Cool and rolling; the late-evening Groove entry. Progression B is the only III-degree opening in the pool, so the B section lifts rather than drops.

## Layers

The three intensity maps share this timeline: the essential melody,
bass, and kick anchor play at every intensity; the pad or stab layer and
the medium drum figures join from the medium map up; the high drum fills,
the arpeggio or counter voice, and the seventh stabs belong to the high
map alone. The engine releases a layer's held voices when the map drops.

## Preview

Rendered by the offline native engine at 44100 Hz:

- `build/music-previews/groove-night-low-44100.wav`
- `build/music-previews/groove-night-medium-44100.wav`
- `build/music-previews/groove-night-high-44100.wav`

The level report of these renders sits in `build/music-previews/report.txt`.

## Listening status

Pending. A human listening pass on headphones and on the handheld
speakers must confirm the mix, the loop seam, and the intensity steps; an
agent must not claim a listening check from source inspection alone
(MUSIC.md, listening checks).
