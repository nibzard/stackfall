# Clear Skies (`focus-clear`)

| Field | Value |
| --- | --- |
| Collection | Focus |
| Catalogue index | 11 of 12 |
| Tempo | 116 BPM |
| Length | 64 bars, about 132 seconds |
| Key and mode | D2 major (MIDI root 38) |
| Swing | straight sixteenths |
| Composed by | The native Arranger of `src/music_tracks.cpp`; no sketch, no samples, no randomness |

## Form

The arrangement contract of MUSIC.md: Intro bars 0-7; A bars 8-23; B bars 24-39; Breakdown bars 40-47; A' bars 48-63.

## Harmony

- Progression A: scale degrees 0-4-5-3 (I-V-vi-IV).
- Progression B: scale degrees 3-4-0-5 (IV-V-I-vi).

## Instruments

- Lead: PulseLead 8-event motifs.
- Bass: Bouncy in A, Drive8 in B and A'.
- Drums: ArcadeDrive at reduced velocity, the quietest kit of the straight-time tracks.
- Harmony: Pads low, stabs from medium.

## Character

The Focus ceiling at 116 BPM: still level, but moving. Shares the IV-first B progression with Soft Drop, transposed to D major.

## Layers

The three intensity maps share this timeline: the essential melody,
bass, and kick anchor play at every intensity; the pad or stab layer and
the medium drum figures join from the medium map up; the high drum fills,
the arpeggio or counter voice, and the seventh stabs belong to the high
map alone. The engine releases a layer's held voices when the map drops.

## Preview

Rendered by the offline native engine at 44100 Hz:

- `build/music-previews/focus-clear-low-44100.wav`
- `build/music-previews/focus-clear-medium-44100.wav`
- `build/music-previews/focus-clear-high-44100.wav`

The level report of these renders sits in `build/music-previews/report.txt`.

## Listening status

Pending. A human listening pass on headphones and on the handheld
speakers must confirm the mix, the loop seam, and the intensity steps; an
agent must not claim a listening check from source inspection alone
(MUSIC.md, listening checks).
