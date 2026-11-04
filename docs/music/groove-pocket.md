# Pocket Circuit (`groove-pocket`)

| Field | Value |
| --- | --- |
| Collection | Groove |
| Catalogue index | 4 of 12 |
| Tempo | 94 BPM |
| Length | 56 bars, about 143 seconds |
| Key and mode | D2 dorian (MIDI root 38) |
| Swing | 12 ticks of swing on every eighth offbeat |
| Composed by | The native Arranger of `src/music_tracks.cpp`; no sketch, no samples, no randomness |

## Form

The arrangement contract of MUSIC.md: Intro bars 0-7; A bars 8-23; B bars 24-39; A' bars 40-55.

## Harmony

- Progression A: scale degrees 0-0-3-6 (i-i-iv-VII of dorian).
- Progression B: scale degrees 0-6-3-6 (i-VII-iv-VII).

## Instruments

- Lead: PulseLead 7-event short motifs, space-heavy.
- Bass: TriangleBass Syncope; HalfNote in the breakdown.
- Drums: GrooveSwing: swung hats, snare on the swung offbeat.
- Harmony: ChordVoice pads low, seventh stabs from medium up.

## Character

The deepest swing of the catalogue. The swing test pins this track: offbeat ticks sit at 72 ticks into the half-bar, not 48.

## Layers

The three intensity maps share this timeline: the essential melody,
bass, and kick anchor play at every intensity; the pad or stab layer and
the medium drum figures join from the medium map up; the high drum fills,
the arpeggio or counter voice, and the seventh stabs belong to the high
map alone. The engine releases a layer's held voices when the map drops.

## Preview

Rendered by the offline native engine at 44100 Hz:

- `build/music-previews/groove-pocket-low-44100.wav`
- `build/music-previews/groove-pocket-medium-44100.wav`
- `build/music-previews/groove-pocket-high-44100.wav`

The level report of these renders sits in `build/music-previews/report.txt`.

## Listening status

Pending. A human listening pass on headphones and on the handheld
speakers must confirm the mix, the loop seam, and the intensity steps; an
agent must not claim a listening check from source inspection alone
(MUSIC.md, listening checks).
