# Neon Steps (`groove-neon`)

| Field | Value |
| --- | --- |
| Collection | Groove |
| Catalogue index | 6 of 12 |
| Tempo | 118 BPM |
| Length | 64 bars, about 130 seconds |
| Key and mode | C3 mixolydian (MIDI root 48) |
| Swing | 8 ticks of swing on every eighth offbeat |
| Composed by | The native Arranger of `src/music_tracks.cpp`; no sketch, no samples, no randomness |

## Form

The arrangement contract of MUSIC.md: Intro bars 0-7; A bars 8-23; B bars 24-39; Breakdown bars 40-47; A' bars 48-63.

## Harmony

- Progression A: scale degrees 0-6-3-6 (I-bVII-IV-bVII of mixolydian).
- Progression B: scale degrees 0-5-6-3 (I-vi-bVII-IV).

## Instruments

- Lead: PulseLead 8-event motifs with an ArpVoice sixteenth arpeggio from the high map.
- Bass: OrbitPedal: whole-bar root pedals under every chord.
- Drums: GrooveSwing hats over a FocusPulse kick pattern.
- Harmony: Stabs from the medium map.

## Character

The brightest Groove track; mixolydian flat-sevenths keep it buoyant. The only mixolydian track, and the only one whose bass never changes note within a bar.

## Layers

The three intensity maps share this timeline: the essential melody,
bass, and kick anchor play at every intensity; the pad or stab layer and
the medium drum figures join from the medium map up; the high drum fills,
the arpeggio or counter voice, and the seventh stabs belong to the high
map alone. The engine releases a layer's held voices when the map drops.

## Preview

Rendered by the offline native engine at 44100 Hz:

- `build/music-previews/groove-neon-low-44100.wav`
- `build/music-previews/groove-neon-medium-44100.wav`
- `build/music-previews/groove-neon-high-44100.wav`

The level report of these renders sits in `build/music-previews/report.txt`.

## Listening status

Pending. A human listening pass on headphones and on the handheld
speakers must confirm the mix, the loop seam, and the intensity steps; an
agent must not claim a listening check from source inspection alone
(MUSIC.md, listening checks).
