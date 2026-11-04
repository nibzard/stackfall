# Boot Sequence (`arcade-boot`)

| Field | Value |
| --- | --- |
| Collection | Arcade |
| Catalogue index | 0 of 12 |
| Tempo | 112 BPM |
| Length | 64 bars, about 137 seconds |
| Key and mode | C3 major (MIDI root 48) |
| Swing | straight sixteenths |
| Composed by | The native Arranger of `src/music_tracks.cpp`; no sketch, no samples, no randomness |

## Form

The arrangement contract of MUSIC.md: Intro bars 0-7; A bars 8-23; B bars 24-39; Breakdown bars 40-47; A' bars 48-63.

## Harmony

- Progression A: scale degrees 0-4-5-3 (I-V-vi-IV).
- Progression B: scale degrees 5-3-0-4 (vi-IV-I-V).

## Instruments

- Lead: PulseLead (25 percent pulse), 11-event motifs A and B, with a PulseWide counter voice from the medium map.
- Bass: TriangleBass: Bouncy in the intro and A, Drive8 in B and A', HalfNote in the breakdown.
- Drums: Kick, Snare, HatClosed in ArcadeDrive; the high map adds HatOpen accents and ArcadeBreak fills.
- Harmony: ChordVoice pads in the intro, breakdown, and low map; stabs from the medium map up.

## Character

The title theme: bright, steady, welcoming. The first title screen plays it at low intensity. Melody A walks degrees 1-3-5; melody B answers a fourth lower; the alternate is a 4-event tail figure.

## Layers

The three intensity maps share this timeline: the essential melody,
bass, and kick anchor play at every intensity; the pad or stab layer and
the medium drum figures join from the medium map up; the high drum fills,
the arpeggio or counter voice, and the seventh stabs belong to the high
map alone. The engine releases a layer's held voices when the map drops.

## Preview

Rendered by the offline native engine at 44100 Hz:

- `build/music-previews/arcade-boot-low-44100.wav`
- `build/music-previews/arcade-boot-medium-44100.wav`
- `build/music-previews/arcade-boot-high-44100.wav`

The level report of these renders sits in `build/music-previews/report.txt`.

## Listening status

Pending. A human listening pass on headphones and on the handheld
speakers must confirm the mix, the loop seam, and the intensity steps; an
agent must not claim a listening check from source inspection alone
(MUSIC.md, listening checks).
