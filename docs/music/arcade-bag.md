# Seven Bag (`arcade-bag`)

| Field | Value |
| --- | --- |
| Collection | Arcade |
| Catalogue index | 1 of 12 |
| Tempo | 128 BPM |
| Length | 72 bars, about 135 seconds |
| Key and mode | G2 major (MIDI root 43) |
| Swing | straight sixteenths |
| Composed by | The native Arranger of `src/music_tracks.cpp`; no sketch, no samples, no randomness |

## Form

The arrangement contract of MUSIC.md: Intro bars 0-7; A bars 8-23; B bars 24-39; Breakdown bars 40-47; A' bars 48-71.

## Harmony

- Progression A: scale degrees 0-5-3-4 (I-vi-IV-V).
- Progression B: scale degrees 0-4-3-4 (I-V-IV-V).

## Instruments

- Lead: PulseLead 13-event motifs A and B, traded two bars at a time.
- Bass: TriangleBass Drive8 through A and A', Syncope in B, HalfNote in the breakdown.
- Drums: ArcadeDrive with ArcadeBreak fills opening the high map.
- Harmony: ChordVoice stabs from the medium map; seventh stabs in the high map.

## Character

Driving and optimistic; the default first round of the Arcade pool. The A melody runs 13 events, so the four-bar phrase never repeats its last bar exactly.

## Layers

The three intensity maps share this timeline: the essential melody,
bass, and kick anchor play at every intensity; the pad or stab layer and
the medium drum figures join from the medium map up; the high drum fills,
the arpeggio or counter voice, and the seventh stabs belong to the high
map alone. The engine releases a layer's held voices when the map drops.

## Preview

Rendered by the offline native engine at 44100 Hz:

- `build/music-previews/arcade-bag-low-44100.wav`
- `build/music-previews/arcade-bag-medium-44100.wav`
- `build/music-previews/arcade-bag-high-44100.wav`

The level report of these renders sits in `build/music-previews/report.txt`.

## Listening status

Pending. A human listening pass on headphones and on the handheld
speakers must confirm the mix, the loop seam, and the intensity steps; an
agent must not claim a listening check from source inspection alone
(MUSIC.md, listening checks).
