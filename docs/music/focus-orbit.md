# Orbit (`focus-orbit`)

| Field | Value |
| --- | --- |
| Collection | Focus |
| Catalogue index | 10 of 12 |
| Tempo | 110 BPM |
| Length | 64 bars, about 140 seconds |
| Key and mode | A2 dorian (MIDI root 45) |
| Swing | straight sixteenths |
| Composed by | The native Arranger of `src/music_tracks.cpp`; no sketch, no samples, no randomness |

## Form

The arrangement contract of MUSIC.md: Intro bars 0-7; A bars 8-23; B bars 24-39; A' bars 40-63.

## Harmony

- Progression A: scale degrees 0-3 (i-iv).
- Progression B: scale degrees 6-3 (VII-iv).

## Instruments

- Lead: PulseLead 4-event cells over an ArpVoice sixteenth thread from the medium map.
- Bass: OrbitPedal root pedals.
- Drums: FocusPulse, hats from medium, open hat accents at high.
- Harmony: Pads low; the arpeggio carries the harmony above them.

## Character

Hypnotic two-chord drift; dorian keeps it modal rather than sad. The arpeggio thread is the track's signature: sixteenths on the chord tones from medium intensity up.

## Layers

The three intensity maps share this timeline: the essential melody,
bass, and kick anchor play at every intensity; the pad or stab layer and
the medium drum figures join from the medium map up; the high drum fills,
the arpeggio or counter voice, and the seventh stabs belong to the high
map alone. The engine releases a layer's held voices when the map drops.

## Preview

Rendered by the offline native engine at 44100 Hz:

- `build/music-previews/focus-orbit-low-44100.wav`
- `build/music-previews/focus-orbit-medium-44100.wav`
- `build/music-previews/focus-orbit-high-44100.wav`

The level report of these renders sits in `build/music-previews/report.txt`.

## Listening status

Pending. A human listening pass on headphones and on the handheld
speakers must confirm the mix, the loop seam, and the intensity steps; an
agent must not claim a listening check from source inspection alone
(MUSIC.md, listening checks).
