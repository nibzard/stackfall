# Soft Drop (`focus-drop`)

| Field | Value |
| --- | --- |
| Collection | Focus |
| Catalogue index | 9 of 12 |
| Tempo | 100 BPM |
| Length | 56 bars, about 134 seconds |
| Key and mode | F2 major (MIDI root 41) |
| Swing | straight sixteenths |
| Composed by | The native Arranger of `src/music_tracks.cpp`; no sketch, no samples, no randomness |

## Form

The arrangement contract of MUSIC.md: Intro bars 0-7; A bars 8-23; B bars 24-39; A' bars 40-55.

## Harmony

- Progression A: scale degrees 0-5-3-4 (I-vi-IV-V).
- Progression B: scale degrees 3-4-0-5 (IV-V-I-vi).

## Instruments

- Lead: PulseLead 7-event motifs, low register answers.
- Bass: Bouncy root-fifth.
- Drums: FocusPulse kick heartbeat; hats from medium.
- Harmony: Pads only, the calmest voicing.

## Character

Major-key calm: one of the two major-mode Focus tracks. Progression B starts on IV, so the A' return to I reads as a resolution rather than a restart.

## Layers

The three intensity maps share this timeline: the essential melody,
bass, and kick anchor play at every intensity; the pad or stab layer and
the medium drum figures join from the medium map up; the high drum fills,
the arpeggio or counter voice, and the seventh stabs belong to the high
map alone. The engine releases a layer's held voices when the map drops.

## Preview

Rendered by the offline native engine at 44100 Hz:

- `build/music-previews/focus-drop-low-44100.wav`
- `build/music-previews/focus-drop-medium-44100.wav`
- `build/music-previews/focus-drop-high-44100.wav`

The level report of these renders sits in `build/music-previews/report.txt`.

## Listening status

Pending. A human listening pass on headphones and on the handheld
speakers must confirm the mix, the loop seam, and the intensity steps; an
agent must not claim a listening check from source inspection alone
(MUSIC.md, listening checks).
