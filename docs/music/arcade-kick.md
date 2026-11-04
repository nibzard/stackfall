# Wall Kick (`arcade-kick`)

| Field | Value |
| --- | --- |
| Collection | Arcade |
| Catalogue index | 2 of 12 |
| Tempo | 138 BPM |
| Length | 80 bars, about 139 seconds |
| Key and mode | A2 minor (MIDI root 45) |
| Swing | straight sixteenths |
| Composed by | The native Arranger of `src/music_tracks.cpp`; no sketch, no samples, no randomness |

## Form

The arrangement contract of MUSIC.md: Intro bars 0-7; A bars 8-23; B bars 24-39; Breakdown bars 40-47; A' bars 48-79.

## Harmony

- Progression A: scale degrees 0-0-5-6 (i-i-VI-VII of natural minor).
- Progression B: scale degrees 0-5-0-6 (i-VI-i-VII).

## Instruments

- Lead: PulseLead 11-event motifs over the natural minor scale.
- Bass: TriangleBass Syncope throughout, HalfNote in the breakdown.
- Drums: ArcadeDrive base, ArcadeBreak fills in the high map.
- Harmony: ChordVoice stabs from the medium map up.

## Character

Tense and quick; the pool's mid-game track. The two-bar i-i pedal opening of progression A pushes against the snare syncopation.

## Layers

The three intensity maps share this timeline: the essential melody,
bass, and kick anchor play at every intensity; the pad or stab layer and
the medium drum figures join from the medium map up; the high drum fills,
the arpeggio or counter voice, and the seventh stabs belong to the high
map alone. The engine releases a layer's held voices when the map drops.

## Preview

Rendered by the offline native engine at 44100 Hz:

- `build/music-previews/arcade-kick-low-44100.wav`
- `build/music-previews/arcade-kick-medium-44100.wav`
- `build/music-previews/arcade-kick-high-44100.wav`

The level report of these renders sits in `build/music-previews/report.txt`.

## Listening status

Pending. A human listening pass on headphones and on the handheld
speakers must confirm the mix, the loop seam, and the intensity steps; an
agent must not claim a listening check from source inspection alone
(MUSIC.md, listening checks).
