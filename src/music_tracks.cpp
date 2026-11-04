// The soundtrack catalogue of MUSIC.md (tasks T062/M3 and M6): twelve
// original tracks across the Arcade, Groove, and Focus collections, each a
// complete two-to-three-minute arrangement with low, medium, and high
// intensity layer maps over one timeline.
//
// The scores are generated once, here, by deterministic composition code:
// fixed motif and pattern data - no randomness, no assets, no runtime input
// - expanded into the typed note lists the engine validates and plays. The
// arrangement contract of MUSIC.md is the shape every track follows: an
// introduction that holds layers back, two distinct sections, an alternate
// section or breakdown, fills and instrument changes across phrases, and a
// loop boundary that keeps the beat.
//
// Composition time unit: one sixteenth note is 24 musical ticks, one bar is
// 16 sixteenths, phrases are eight bars. Melodies are scale degrees over the
// track's mode; chords are diatonic triads (some with a seventh) over a
// per-bar progression; drums, bass figures, arpeggios, and chord stabs come
// from the per-style pattern sets below. Groove tracks swing their eighth
// offbeats by a fixed tick offset, which the 96-tick grid holds exactly.

#include "stackfall/music.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace stackfall {
namespace {

constexpr int kSixteenthTicks = kMusicTicksPerSixteenth;  // 24
constexpr int kBarSixteenths = 16;
constexpr int kMaxBars = 96;

// ---------------------------------------------------------------------------
// Fixed scale and pattern data.
// ---------------------------------------------------------------------------

// Seven-degree scales in semitones from the tonic.
constexpr std::int8_t kMajor[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr std::int8_t kMinor[7] = {0, 2, 3, 5, 7, 8, 10};
constexpr std::int8_t kDorian[7] = {0, 2, 3, 5, 7, 9, 10};
constexpr std::int8_t kMixolydian[7] = {0, 2, 4, 5, 7, 9, 10};

// One melodic motif: scale degrees (0 is the tonic; 7 lifts one octave)
// placed on the two-bar sixteenth grid.
struct MotifEvent {
  std::int8_t degree;
  std::uint8_t start16;
  std::uint8_t len16;
};

struct Motif {
  const MotifEvent* events;
  int count;
};

enum class DrumStyle {
  ArcadeDrive,    // four-on-the-floor flavored arcade backbeat
  ArcadeBreak,    // the same with open hats and short quiet breaks
  GrooveSwing,    // swung offbeats, ghost snare, late kick
  HousePump,      // offbeat stabs and a pumped kick
  FocusSparse,    // soft kick and offbeat hats only
  FocusPulse,     // a steady soft pulse with a rim accent
};

enum class BassStyle {
  Drive8,      // straight eighth notes
  Bouncy,      // root and fifth on the strongs
  Syncope,     // sixteenth pushes and seventh color
  HalfNote,    // two long notes a bar
  OrbitPedal,  // one whole-bar pedal per chord
};

enum class ChordStyle {
  None,
  Stabs,   // short triads on offbeats
  Pads,    // whole-bar soft triads
  Stabs7,  // short triads with a seventh
};

// ---------------------------------------------------------------------------
// The arranger: expands the fixed data into one validated note list.
// ---------------------------------------------------------------------------

class Arranger {
 public:
  Arranger(MusicTrackData* track, const std::int8_t* scale, int tonic,
           int swingTicks)
      : track_(track), scale_(scale), tonic_(tonic), swing_(swingTicks) {
    chordOfBar_.fill(-1);
  }

  // One chord root degree per bar for `bars` bars, cycling through
  // `degrees`.
  void progression(int startBar, int bars, const std::int8_t* degrees,
                   int degreeCount) {
    for (int i = 0; i < bars; ++i) {
      chordOfBar_[static_cast<std::size_t>(startBar + i)] =
          degrees[i % degreeCount];
    }
  }

  // Melodic pitch of a scale degree in the lead register.
  int melodyPitch(int degree) const {
    const int octave = degree >= 0 ? degree / 7 : (degree - 6) / 7;
    const int index = degree - octave * 7;
    return tonic_ + 24 + scale_[index] + 12 * octave;
  }

  // Chord voice pitch: tone 0 root, 1 third, 2 fifth, 3 seventh, one
  // octave above the bass register.
  int chordPitch(int bar, int tone) const {
    const int degree = chordDegree(bar);
    const int toneDegree = degree + 2 * tone;
    const int octave = toneDegree / 7;
    return tonic_ + 12 + scale_[toneDegree - octave * 7] + 12 * octave;
  }

  int bassPitch(int bar, int semitoneOffset) const {
    const int degree = chordDegree(bar);
    const int octave = degree / 7;
    return tonic_ + scale_[degree - octave * 7] + 12 * octave + semitoneOffset;
  }

  int chordDegree(int bar) const {
    const int degree = chordOfBar_[static_cast<std::size_t>(bar)];
    return degree < 0 ? 0 : degree;
  }

  // Absolute tick of a sixteenth, with the track's swing applied to eighth
  // offbeats.
  int tickOf(int bar, int sixteenth) const {
    int swung = sixteenth;
    if (swing_ > 0 && (sixteenth % 4) == 2) {
      swung += 1;  // a fixed 3:2-ish lilt inside the 96-tick grid
    }
    return (bar * kBarSixteenths + swung) * kSixteenthTicks;
  }

  void note(int startTick, int lengthTicks, int pitch, int velocity,
            MusicInstrument instrument, int layerMask) {
    // Every event stays inside the loop: a pattern that would run past the
    // final bar is trimmed, and one that starts past it is dropped, so the
    // loop boundary never truncates a sounding note (MUSIC.md, arrangement
    // contract).
    const std::uint32_t total = track_->totalTicks();
    if (startTick < 0 || static_cast<std::uint32_t>(startTick) >= total) {
      return;
    }
    if (static_cast<std::uint32_t>(startTick + lengthTicks) > total) {
      lengthTicks = static_cast<int>(total) - startTick;
      if (lengthTicks <= 0) {
        return;
      }
    }
    MusicNote entry;
    entry.startTick = static_cast<std::uint32_t>(startTick);
    entry.lengthTicks = static_cast<std::uint16_t>(lengthTicks);
    entry.pitch = static_cast<std::int16_t>(pitch);
    entry.velocity = static_cast<std::uint8_t>(velocity);
    entry.layerMask = static_cast<std::uint8_t>(layerMask);
    entry.instrument = instrument;
    track_->notes.push_back(entry);
  }

  // A melodic section: the two motifs alternate over two-bar slots
  // (a a b a), with an octave lift on the last slot. `sparse` halves the
  // density for breakdown sections.
  void melody(int startBar, int bars, const Motif& a, const Motif& b,
              MusicInstrument lead, int layer, int baseDegree, bool sparse) {
    const int slots = bars / 2;
    for (int slot = 0; slot < slots; ++slot) {
      if (sparse && (slot % 2) == 1) {
        continue;  // a breakdown breathes: every other slot rests
      }
      const Motif& motif = (slot % 4 == 2) ? b : a;
      const int lift = (slot == slots - 1) ? 7 : 0;
      for (int i = 0; i < motif.count; ++i) {
        const MotifEvent& event = motif.events[i];
        const int pitch = melodyPitch(baseDegree + lift + event.degree);
        note(tickOf(startBar + slot * 2, event.start16),
             event.len16 * kSixteenthTicks - 6, pitch,
             std::min(127, 92 + event.degree), lead, layer);
      }
    }
  }

  // A counter-melody line (the PulseWide voice) for the high map.
  void counter(int startBar, int bars, const Motif& motif, int layer,
               int baseDegree) {
    const int slots = bars / 2;
    for (int slot = 0; slot < slots; ++slot) {
      if (slot % 2 == 1) {
        continue;
      }
      for (int i = 0; i < motif.count; ++i) {
        const MotifEvent& event = motif.events[i];
        note(tickOf(startBar + slot * 2, event.start16),
             event.len16 * kSixteenthTicks - 6,
             melodyPitch(baseDegree + event.degree - 7), 78,
             MusicInstrument::PulseWide, layer);
      }
    }
  }

  void bass(int startBar, int bars, BassStyle style, int layer) {
    for (int bar = 0; bar < bars; ++bar) {
      const int at = startBar + bar;
      const int root = bassPitch(at, 0);
      const int fifth = bassPitch(at, 7);
      const int seventh = bassPitch(at, 10);
      const int eighth = 2 * kSixteenthTicks;
      switch (style) {
        case BassStyle::Drive8:
          for (int s = 0; s < 16; s += 2) {
            note(tickOf(at, s), eighth - 4,
                 s == 14 ? fifth : root, s % 4 == 0 ? 108 : 92,
                 MusicInstrument::TriangleBass, layer);
          }
          break;
        case BassStyle::Bouncy:
          note(tickOf(at, 0), 5 * kSixteenthTicks, root, 110,
               MusicInstrument::TriangleBass, layer);
          note(tickOf(at, 6), 2 * kSixteenthTicks, fifth, 92,
               MusicInstrument::TriangleBass, layer);
          note(tickOf(at, 8), 5 * kSixteenthTicks, root, 104,
               MusicInstrument::TriangleBass, layer);
          note(tickOf(at, 14), 2 * kSixteenthTicks, fifth, 88,
               MusicInstrument::TriangleBass, layer);
          break;
        case BassStyle::Syncope:
          note(tickOf(at, 0), 3 * kSixteenthTicks, root, 110,
               MusicInstrument::TriangleBass, layer);
          note(tickOf(at, 3), 2 * kSixteenthTicks, root, 86,
               MusicInstrument::TriangleBass, layer);
          note(tickOf(at, 6), 3 * kSixteenthTicks, fifth, 98,
               MusicInstrument::TriangleBass, layer);
          note(tickOf(at, 10), 2 * kSixteenthTicks, seventh, 90,
               MusicInstrument::TriangleBass, layer);
          note(tickOf(at, 12), 3 * kSixteenthTicks, root, 104,
               MusicInstrument::TriangleBass, layer);
          break;
        case BassStyle::HalfNote:
          note(tickOf(at, 0), 7 * kSixteenthTicks, root, 104,
               MusicInstrument::TriangleBass, layer);
          note(tickOf(at, 8), 7 * kSixteenthTicks, fifth, 96,
               MusicInstrument::TriangleBass, layer);
          break;
        case BassStyle::OrbitPedal:
          note(tickOf(at, 0), 15 * kSixteenthTicks, root, 100,
               MusicInstrument::TriangleBass, layer);
          break;
      }
    }
  }

  // One drum bar of the pattern set. layerMax bounds which map hears the
  // backbeat: the low map always keeps the kick's anchor hits.
  void drumBar(int at, DrumStyle style, int layerMax, bool fill) {
    const int low = kMusicLayersEssential;
    const int med = kMusicLayersFromMedium;
    const int high = kMusicLayersHighOnly;
    const auto kick = [&](int s, int velocity, int layer) {
      note(tickOf(at, s), kSixteenthTicks, 0, velocity, MusicInstrument::Kick,
           layer);
    };
    const auto snare = [&](int s, int velocity, int layer) {
      note(tickOf(at, s), kSixteenthTicks, 0, velocity,
           MusicInstrument::Snare, layer);
    };
    const auto hat = [&](int s, int velocity, int layer,
                         MusicInstrument which = MusicInstrument::HatClosed) {
      note(tickOf(at, s), kSixteenthTicks / 2, 0, velocity, which, layer);
    };
    const auto accent = [&](int s, int velocity) {
      note(tickOf(at, s), kSixteenthTicks, 0, velocity,
           MusicInstrument::Accent, high);
    };
    switch (style) {
      case DrumStyle::ArcadeDrive:
      case DrumStyle::ArcadeBreak:
        kick(0, 118, low);
        kick(8, 112, low);
        kick(6, 96, med);
        snare(4, 108, med);
        snare(12, 112, med);
        for (int s = 0; s < 16; s += 2) {
          hat(s, s % 4 == 0 ? 84 : 66, med);
        }
        if (style == DrumStyle::ArcadeBreak) {
          snare(7, 56, high);
          hat(14, 76, high, MusicInstrument::HatOpen);
          accent(15, 72);
        } else if (layerMax == kMusicLayersHighOnly) {
          snare(14, 60, high);
        }
        break;
      case DrumStyle::GrooveSwing:
        kick(0, 116, low);
        kick(3, 92, med);
        kick(10, 104, med);
        snare(4, 106, med);
        snare(12, 110, med);
        snare(13, 52, high);
        for (int s = 0; s < 16; s += 2) {
          hat(s, s % 4 == 0 ? 82 : 70, med);
        }
        hat(6, 78, high, MusicInstrument::HatOpen);
        break;
      case DrumStyle::HousePump:
        kick(0, 118, low);
        kick(4, 108, low);
        kick(8, 114, low);
        kick(12, 108, low);
        snare(4, 88, med);
        snare(12, 92, med);
        for (int s = 2; s < 16; s += 4) {
          hat(s, 86, med, MusicInstrument::HatOpen);
        }
        hat(0, 62, med);
        hat(8, 62, med);
        snare(15, 58, high);
        break;
      case DrumStyle::FocusSparse:
        kick(0, 96, low);
        if (at % 2 == 1) {
          kick(10, 78, med);
        }
        hat(4, 56, med);
        hat(12, 60, med);
        break;
      case DrumStyle::FocusPulse:
        kick(0, 92, low);
        kick(8, 84, low);
        hat(4, 54, med);
        hat(12, 54, med);
        if (layerMax == kMusicLayersHighOnly) {
          hat(6, 46, high);
          hat(14, 46, high);
        }
        break;
    }
    if (fill && layerMax == kMusicLayersHighOnly) {
      // The phrase fill: a rising sixteenth run in the last bar.
      for (int s = 8; s < 16; ++s) {
        snare(s, 60 + (s - 8) * 8, high);
      }
    }
  }

  void drums(int startBar, int bars, DrumStyle style, int layerMax) {
    for (int bar = 0; bar < bars; ++bar) {
      drumBar(startBar + bar, style, layerMax,
              bar % kMusicBarsPerPhrase == kMusicBarsPerPhrase - 1);
    }
  }

  void chords(int startBar, int bars, ChordStyle style, int layer) {
    if (style == ChordStyle::None) {
      return;
    }
    const bool seventh = style == ChordStyle::Stabs7;
    for (int bar = 0; bar < bars; ++bar) {
      const int at = startBar + bar;
      if (style == ChordStyle::Pads) {
        for (int tone = 0; tone < (seventh ? 4 : 3); ++tone) {
          note(tickOf(at, 0), 15 * kSixteenthTicks, chordPitch(at, tone), 64,
               MusicInstrument::ChordVoice, layer);
        }
      } else {
        // Stabs stay triads: three voices, short, on offbeats.
        for (int s : {2, 7}) {
          for (int tone = 0; tone < 3; ++tone) {
            note(tickOf(at, s), 2 * kSixteenthTicks, chordPitch(at, tone), 72,
                 MusicInstrument::ChordVoice, layer);
          }
        }
      }
    }
  }

  // A rising arpeggio cycle over each bar's chord. `triplet` uses the
  // 32-tick triplet-eighth grid the 96-tick quarter makes possible.
  void arp(int startBar, int bars, int layer, bool triplet) {
    for (int bar = 0; bar < bars; ++bar) {
      const int at = startBar + bar;
      if (triplet) {
        // Twelve triplet eighths per bar sweep the triad upward.
        for (int i = 0; i < 12; ++i) {
          const int tone = i % 3;
          note(tickOf(at, 0) + i * 32, 26, chordPitch(at, tone), 74,
               MusicInstrument::ArpVoice, layer);
        }
      } else {
        static const int kShape[8] = {0, 1, 2, 3, 2, 1, 0, 1};
        for (int i = 0; i < 8; ++i) {
          note(tickOf(at, i * 2), 2 * kSixteenthTicks - 8,
               chordPitch(at, kShape[i]), 70 + (i % 2) * 8,
               MusicInstrument::ArpVoice, layer);
        }
      }
    }
  }

 private:
  MusicTrackData* track_;
  const std::int8_t* scale_;
  int tonic_;
  int swing_;
  std::array<int, kMaxBars> chordOfBar_{};
};

// ---------------------------------------------------------------------------
// Motifs. Degrees are scale degrees; the lead register sits two octaves
// above the bass tonic. Each track lists three: two section motifs and one
// breakdown figure.
// ---------------------------------------------------------------------------

// Boot Sequence: a warm rising welcome.
constexpr MotifEvent kBootA[] = {
    {0, 0, 3},  {2, 4, 2},  {4, 6, 2},  {5, 8, 4},  {4, 12, 2},
    {2, 14, 2}, {0, 16, 4}, {4, 20, 2}, {2, 22, 2}, {0, 24, 6},
    {7, 28, 3}};
constexpr MotifEvent kBootB[] = {
    {4, 0, 4},  {5, 4, 2},   {7, 6, 2},  {9, 8, 4},   {7, 12, 2},
    {5, 14, 2}, {4, 16, 4},  {2, 20, 2}, {0, 22, 2},  {2, 24, 6},
    {0, 28, 3}};
constexpr MotifEvent kBootAlt[] = {
    {0, 0, 8}, {4, 8, 8}, {7, 16, 8}, {4, 24, 8}};

// Seven Bag: bright, bubbling.
constexpr MotifEvent kBagA[] = {
    {7, 0, 2},  {5, 2, 2},  {4, 4, 2},  {5, 6, 2},  {7, 8, 3},
    {9, 12, 2}, {7, 14, 2}, {4, 16, 2}, {5, 18, 2}, {7, 20, 2},
    {5, 22, 2}, {4, 24, 4}, {2, 28, 3}};
constexpr MotifEvent kBagB[] = {
    {9, 0, 2},  {7, 2, 2},  {9, 4, 2},  {11, 6, 2}, {9, 8, 3},
    {7, 12, 2}, {5, 14, 2}, {7, 16, 2}, {9, 18, 2}, {11, 20, 2},
    {9, 22, 2}, {7, 24, 4}, {5, 28, 3}};
constexpr MotifEvent kBagAlt[] = {
    {4, 0, 6}, {7, 8, 6}, {9, 16, 6}, {7, 24, 6}};

// Wall Kick: clipped, rhythmic.
constexpr MotifEvent kKickA[] = {
    {0, 0, 2},  {0, 3, 2},  {3, 6, 2},  {2, 8, 2},  {0, 11, 2},
    {3, 14, 2}, {4, 16, 2}, {3, 19, 2}, {2, 22, 2}, {0, 24, 3},
    {7, 28, 3}};
constexpr MotifEvent kKickB[] = {
    {7, 0, 2},  {5, 3, 2},  {7, 6, 2},  {9, 8, 2},  {7, 11, 2},
    {5, 14, 2}, {4, 16, 2}, {5, 19, 2}, {7, 22, 2}, {9, 24, 3},
    {7, 28, 3}};
constexpr MotifEvent kKickAlt[] = {
    {0, 0, 4}, {0, 8, 4}, {5, 16, 4}, {5, 24, 4}};

// Redline: fast chip techno, tight cells.
constexpr MotifEvent kRedA[] = {
    {0, 0, 1},  {2, 2, 1},  {4, 4, 1},  {2, 6, 1},  {0, 8, 1},
    {4, 10, 1}, {7, 12, 2}, {4, 14, 1}, {0, 16, 1}, {2, 18, 1},
    {4, 20, 1}, {5, 22, 1}, {7, 24, 2}, {5, 26, 1}, {4, 28, 3}};
constexpr MotifEvent kRedB[] = {
    {7, 0, 1},  {5, 2, 1},  {7, 4, 1},  {9, 6, 1},  {7, 8, 1},
    {5, 10, 1}, {4, 12, 2}, {5, 14, 1}, {7, 16, 1}, {9, 18, 1},
    {11, 20, 1}, {9, 22, 1}, {7, 24, 2}, {9, 26, 1}, {7, 28, 3}};
constexpr MotifEvent kRedAlt[] = {
    {0, 0, 4}, {7, 8, 4}, {9, 16, 4}, {7, 24, 4}};

// Pocket Circuit: a short chip hook with space around it.
constexpr MotifEvent kPocketA[] = {
    {0, 0, 4},  {2, 6, 2},  {4, 8, 4},  {2, 14, 2}, {0, 16, 4},
    {4, 22, 2}, {5, 24, 6}};
constexpr MotifEvent kPocketB[] = {
    {5, 0, 4},  {4, 6, 2},  {2, 8, 4},  {4, 14, 2}, {5, 16, 4},
    {7, 22, 2}, {5, 24, 6}};
constexpr MotifEvent kPocketAlt[] = {
    {0, 0, 8}, {4, 12, 4}, {7, 20, 4}};

// Night Bus: sparse and rounded.
constexpr MotifEvent kNightA[] = {
    {0, 0, 6},  {2, 8, 4},  {3, 16, 6}, {2, 24, 4}, {0, 28, 3}};
constexpr MotifEvent kNightB[] = {
    {5, 0, 6},  {4, 8, 4},  {2, 16, 6}, {4, 24, 4}, {5, 28, 3}};
constexpr MotifEvent kNightAlt[] = {
    {0, 0, 12}, {4, 16, 12}};

// Neon Steps: offbeat accents, broken steps.
constexpr MotifEvent kNeonA[] = {
    {0, 2, 2},  {4, 6, 2},  {7, 8, 2},  {5, 12, 2}, {4, 16, 2},
    {2, 20, 2}, {4, 24, 4}, {2, 28, 3}};
constexpr MotifEvent kNeonB[] = {
    {7, 2, 2},  {9, 6, 2},  {7, 10, 2}, {5, 14, 2}, {7, 18, 2},
    {9, 22, 2}, {11, 24, 4}, {9, 28, 3}};
constexpr MotifEvent kNeonAlt[] = {
    {4, 0, 4}, {4, 8, 4}, {7, 16, 4}, {7, 24, 4}};

// Afterimage: playful counter lines.
constexpr MotifEvent kAfterA[] = {
    {0, 0, 3},  {4, 4, 2},  {5, 6, 2},  {7, 8, 4},  {5, 12, 2},
    {4, 14, 2}, {2, 16, 4}, {4, 22, 2}, {0, 24, 6}};
constexpr MotifEvent kAfterB[] = {
    {9, 0, 3},  {7, 4, 2},  {5, 6, 2},  {4, 8, 4},  {5, 12, 2},
    {7, 14, 2}, {9, 16, 4}, {7, 22, 2}, {5, 24, 6}};
constexpr MotifEvent kAfterAlt[] = {
    {0, 0, 8}, {5, 8, 8}, {7, 16, 8}, {5, 24, 8}};

// Ghost Grid: soft triangle melody, wide spaces.
constexpr MotifEvent kGhostA[] = {
    {0, 0, 8},  {4, 12, 4}, {2, 20, 4}, {0, 28, 4}};
constexpr MotifEvent kGhostB[] = {
    {5, 0, 8},  {4, 12, 4}, {7, 20, 4}, {5, 28, 4}};
constexpr MotifEvent kGhostAlt[] = {
    {0, 0, 16}, {4, 16, 16}};

// Soft Drop: gentle arpeggio-like melody.
constexpr MotifEvent kDropA[] = {
    {0, 0, 4},  {2, 4, 4},  {4, 8, 4},  {2, 12, 4}, {0, 16, 4},
    {4, 20, 4}, {5, 24, 8}};
constexpr MotifEvent kDropB[] = {
    {5, 0, 4},  {4, 4, 4},  {2, 8, 4},  {4, 12, 4}, {5, 16, 4},
    {7, 20, 4}, {5, 24, 8}};
constexpr MotifEvent kDropAlt[] = {
    {0, 0, 8}, {4, 8, 8}, {5, 16, 8}, {4, 24, 8}};

// Orbit: repeating motifs over slow changes.
constexpr MotifEvent kOrbitA[] = {
    {0, 0, 4},  {4, 8, 4},  {0, 16, 4}, {4, 24, 4}};
constexpr MotifEvent kOrbitB[] = {
    {7, 0, 4},  {5, 8, 4},  {7, 16, 4}, {5, 24, 4}};
constexpr MotifEvent kOrbitAlt[] = {
    {0, 0, 8}, {2, 8, 8}, {4, 16, 8}, {2, 24, 8}};

// Clear Skies: relaxed and uplifting.
constexpr MotifEvent kClearA[] = {
    {0, 0, 4},  {2, 4, 2},  {4, 6, 4},  {5, 12, 2}, {7, 16, 4},
    {5, 20, 2}, {4, 22, 2}, {2, 24, 6}};
constexpr MotifEvent kClearB[] = {
    {9, 0, 4},  {7, 4, 2},  {5, 6, 4},  {4, 12, 2}, {5, 16, 4},
    {7, 20, 2}, {9, 22, 2}, {7, 24, 6}};
constexpr MotifEvent kClearAlt[] = {
    {4, 0, 8}, {7, 8, 8}, {9, 16, 8}, {11, 24, 8}};

constexpr Motif motif(const MotifEvent* events, int count) {
  return Motif{events, count};
}

// ---------------------------------------------------------------------------
// The twelve tracks. Every compose function follows the arrangement
// contract: intro, section A, section B, an alternate or breakdown, and a
// closing A' that lands the loop.
// ---------------------------------------------------------------------------

void composeBootSequence(MusicTrackData* track) {
  Arranger arr(track, kMajor, 48, 0);
  const std::int8_t progA[4] = {0, 4, 5, 3};
  const std::int8_t progB[4] = {5, 3, 0, 4};
  const Motif a = motif(kBootA, 11);
  const Motif b = motif(kBootB, 11);
  const Motif alt = motif(kBootAlt, 4);
  arr.progression(0, 64, progA, 4);
  arr.progression(16, 16, progB, 4);
  arr.progression(40, 8, progB, 4);
  // Intro: bass, pads, and a light kick; the lead waits.
  arr.progression(0, 8, progA, 4);
  arr.bass(0, 8, BassStyle::Bouncy, kMusicLayersEssential);
  arr.chords(0, 8, ChordStyle::Pads, kMusicLayersEssential);
  arr.drums(0, 8, DrumStyle::ArcadeDrive, kMusicLayersEssential);
  // A: the welcoming theme.
  arr.melody(8, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(8, 16, BassStyle::Bouncy, kMusicLayersEssential);
  arr.drums(8, 16, DrumStyle::ArcadeDrive, kMusicLayersFromMedium);
  arr.arp(8, 16, kMusicLayersFromMedium, false);
  // B: brighter, moving.
  arr.melody(24, 16, b, a, MusicInstrument::PulseLead, kMusicLayersEssential, 2,
             false);
  arr.bass(24, 16, BassStyle::Drive8, kMusicLayersEssential);
  arr.drums(24, 16, DrumStyle::ArcadeDrive, kMusicLayersHighOnly);
  arr.chords(24, 16, ChordStyle::Stabs, kMusicLayersFromMedium);
  arr.counter(24, 16, alt, kMusicLayersHighOnly, 3);
  // Alternate: pads and a sparse figure.
  arr.melody(40, 8, alt, alt, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             true);
  arr.bass(40, 8, BassStyle::HalfNote, kMusicLayersEssential);
  arr.chords(40, 8, ChordStyle::Pads, kMusicLayersEssential);
  arr.drums(40, 8, DrumStyle::ArcadeDrive, kMusicLayersEssential);
  // A' with the high map and the loop tail.
  arr.melody(48, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(48, 16, BassStyle::Bouncy, kMusicLayersEssential);
  arr.drums(48, 16, DrumStyle::ArcadeDrive, kMusicLayersHighOnly);
  arr.chords(48, 16, ChordStyle::Stabs, kMusicLayersFromMedium);
  arr.arp(56, 8, kMusicLayersHighOnly, false);
}

void composeSevenBag(MusicTrackData* track) {
  Arranger arr(track, kMajor, 43, 0);
  const std::int8_t progA[4] = {0, 5, 3, 4};
  const std::int8_t progB[4] = {0, 4, 3, 4};
  const Motif a = motif(kBagA, 13);
  const Motif b = motif(kBagB, 13);
  const Motif alt = motif(kBagAlt, 4);
  arr.progression(0, 72, progA, 4);
  arr.progression(24, 16, progB, 4);
  arr.progression(56, 8, progB, 4);
  arr.bass(0, 8, BassStyle::Drive8, kMusicLayersEssential);
  arr.drums(0, 8, DrumStyle::ArcadeDrive, kMusicLayersEssential);
  arr.arp(0, 8, kMusicLayersEssential, true);
  arr.melody(8, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(8, 16, BassStyle::Drive8, kMusicLayersEssential);
  arr.drums(8, 16, DrumStyle::ArcadeDrive, kMusicLayersFromMedium);
  arr.arp(8, 16, kMusicLayersFromMedium, true);
  arr.melody(24, 16, b, a, MusicInstrument::PulseLead, kMusicLayersEssential, 3,
             false);
  arr.bass(24, 16, BassStyle::Syncope, kMusicLayersEssential);
  arr.drums(24, 16, DrumStyle::ArcadeBreak, kMusicLayersHighOnly);
  arr.chords(24, 16, ChordStyle::Stabs, kMusicLayersFromMedium);
  arr.melody(40, 8, alt, alt, MusicInstrument::ArpVoice, kMusicLayersFromMedium, 0,
             true);
  arr.bass(40, 8, BassStyle::HalfNote, kMusicLayersEssential);
  arr.drums(40, 8, DrumStyle::ArcadeDrive, kMusicLayersEssential);
  arr.arp(40, 8, kMusicLayersEssential, true);
  arr.melody(48, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(48, 16, BassStyle::Drive8, kMusicLayersEssential);
  arr.drums(48, 16, DrumStyle::ArcadeBreak, kMusicLayersHighOnly);
  arr.chords(48, 16, ChordStyle::Stabs, kMusicLayersFromMedium);
  arr.counter(56, 16, alt, kMusicLayersHighOnly, 2);
  arr.arp(56, 16, kMusicLayersHighOnly, true);
}

void composeWallKick(MusicTrackData* track) {
  Arranger arr(track, kMinor, 45, 0);
  const std::int8_t progA[4] = {0, 0, 5, 6};
  const std::int8_t progB[4] = {0, 5, 0, 6};
  const Motif a = motif(kKickA, 11);
  const Motif b = motif(kKickB, 11);
  const Motif alt = motif(kKickAlt, 4);
  arr.progression(0, 80, progA, 4);
  arr.progression(24, 16, progB, 4);
  arr.progression(56, 8, progB, 4);
  arr.bass(0, 8, BassStyle::Syncope, kMusicLayersEssential);
  arr.drums(0, 8, DrumStyle::ArcadeDrive, kMusicLayersEssential);
  arr.chords(0, 8, ChordStyle::Stabs, kMusicLayersFromMedium);
  arr.melody(8, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(8, 16, BassStyle::Syncope, kMusicLayersEssential);
  arr.drums(8, 16, DrumStyle::ArcadeDrive, kMusicLayersFromMedium);
  arr.chords(8, 16, ChordStyle::Stabs, kMusicLayersFromMedium);
  arr.melody(24, 16, b, a, MusicInstrument::PulseLead, kMusicLayersEssential, 2,
             false);
  arr.bass(24, 16, BassStyle::Syncope, kMusicLayersEssential);
  arr.drums(24, 16, DrumStyle::ArcadeBreak, kMusicLayersHighOnly);
  arr.counter(32, 8, alt, kMusicLayersHighOnly, 0);
  arr.melody(40, 8, alt, alt, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             true);
  arr.bass(40, 8, BassStyle::HalfNote, kMusicLayersEssential);
  arr.drums(40, 8, DrumStyle::ArcadeDrive, kMusicLayersEssential);
  arr.melody(48, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(48, 16, BassStyle::Syncope, kMusicLayersEssential);
  arr.drums(48, 16, DrumStyle::ArcadeBreak, kMusicLayersHighOnly);
  arr.chords(48, 16, ChordStyle::Stabs, kMusicLayersFromMedium);
  arr.arp(64, 16, kMusicLayersHighOnly, false);
}

void composeRedline(MusicTrackData* track) {
  Arranger arr(track, kMinor, 40, 0);
  const std::int8_t progA[4] = {0, 6, 5, 6};
  const std::int8_t progB[4] = {0, 3, 4, 6};
  const Motif a = motif(kRedA, 15);
  const Motif b = motif(kRedB, 15);
  const Motif alt = motif(kRedAlt, 4);
  arr.progression(0, 80, progA, 4);
  arr.progression(24, 16, progB, 4);
  arr.progression(56, 16, progB, 4);
  arr.bass(0, 8, BassStyle::Drive8, kMusicLayersEssential);
  arr.drums(0, 8, DrumStyle::HousePump, kMusicLayersEssential);
  arr.melody(8, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(8, 16, BassStyle::Drive8, kMusicLayersEssential);
  arr.drums(8, 16, DrumStyle::HousePump, kMusicLayersFromMedium);
  arr.arp(8, 16, kMusicLayersFromMedium, false);
  arr.melody(24, 16, b, a, MusicInstrument::PulseLead, kMusicLayersEssential, 3,
             false);
  arr.bass(24, 16, BassStyle::Drive8, kMusicLayersEssential);
  arr.drums(24, 16, DrumStyle::HousePump, kMusicLayersHighOnly);
  arr.chords(24, 16, ChordStyle::Stabs7, kMusicLayersFromMedium);
  // The short quiet break of the brief: two bars near silence.
  arr.melody(40, 4, alt, alt, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             true);
  arr.bass(40, 2, BassStyle::HalfNote, kMusicLayersEssential);
  arr.bass(44, 4, BassStyle::Drive8, kMusicLayersEssential);
  arr.drums(40, 2, DrumStyle::FocusSparse, kMusicLayersEssential);
  arr.drums(42, 6, DrumStyle::HousePump, kMusicLayersEssential);
  arr.melody(48, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(48, 16, BassStyle::Drive8, kMusicLayersEssential);
  arr.drums(48, 16, DrumStyle::HousePump, kMusicLayersHighOnly);
  arr.chords(48, 16, ChordStyle::Stabs7, kMusicLayersFromMedium);
  arr.counter(64, 16, alt, kMusicLayersHighOnly, 2);
  arr.arp(72, 8, kMusicLayersHighOnly, false);
}

void composePocketCircuit(MusicTrackData* track) {
  Arranger arr(track, kDorian, 38, 12);
  const std::int8_t progA[4] = {0, 0, 3, 6};
  const std::int8_t progB[4] = {0, 6, 3, 6};
  const Motif a = motif(kPocketA, 7);
  const Motif b = motif(kPocketB, 7);
  const Motif alt = motif(kPocketAlt, 3);
  arr.progression(0, 56, progA, 4);
  arr.progression(24, 16, progB, 4);
  arr.bass(0, 8, BassStyle::Syncope, kMusicLayersEssential);
  arr.drums(0, 8, DrumStyle::GrooveSwing, kMusicLayersEssential);
  arr.chords(0, 8, ChordStyle::Stabs7, kMusicLayersEssential);
  arr.melody(8, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(8, 16, BassStyle::Syncope, kMusicLayersEssential);
  arr.drums(8, 16, DrumStyle::GrooveSwing, kMusicLayersFromMedium);
  arr.chords(8, 16, ChordStyle::Stabs7, kMusicLayersFromMedium);
  arr.melody(24, 16, b, a, MusicInstrument::PulseLead, kMusicLayersEssential, 2,
             false);
  arr.bass(24, 16, BassStyle::Syncope, kMusicLayersEssential);
  arr.drums(24, 16, DrumStyle::GrooveSwing, kMusicLayersHighOnly);
  arr.counter(32, 8, alt, kMusicLayersHighOnly, 0);
  arr.melody(40, 8, alt, alt, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             true);
  arr.bass(40, 8, BassStyle::HalfNote, kMusicLayersEssential);
  arr.drums(40, 8, DrumStyle::FocusSparse, kMusicLayersEssential);
  arr.chords(40, 8, ChordStyle::Pads, kMusicLayersEssential);
  arr.melody(48, 8, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(48, 8, BassStyle::Syncope, kMusicLayersEssential);
  arr.drums(48, 8, DrumStyle::GrooveSwing, kMusicLayersHighOnly);
  arr.chords(48, 8, ChordStyle::Stabs7, kMusicLayersFromMedium);
}

void composeNightBus(MusicTrackData* track) {
  Arranger arr(track, kMinor, 42, 10);
  const std::int8_t progA[4] = {0, 5, 3, 4};
  const std::int8_t progB[4] = {0, 2, 5, 6};
  const Motif a = motif(kNightA, 5);
  const Motif b = motif(kNightB, 5);
  const Motif alt = motif(kNightAlt, 2);
  arr.progression(0, 64, progA, 4);
  arr.progression(24, 16, progB, 4);
  arr.progression(48, 16, progB, 4);
  arr.bass(0, 8, BassStyle::Bouncy, kMusicLayersEssential);
  arr.drums(0, 8, DrumStyle::GrooveSwing, kMusicLayersEssential);
  arr.chords(0, 8, ChordStyle::Stabs7, kMusicLayersEssential);
  arr.melody(8, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(8, 16, BassStyle::Bouncy, kMusicLayersEssential);
  arr.drums(8, 16, DrumStyle::GrooveSwing, kMusicLayersFromMedium);
  arr.chords(8, 16, ChordStyle::Stabs7, kMusicLayersFromMedium);
  arr.melody(24, 16, b, a, MusicInstrument::PulseLead, kMusicLayersEssential, 2,
             false);
  arr.bass(24, 16, BassStyle::Syncope, kMusicLayersEssential);
  arr.drums(24, 16, DrumStyle::GrooveSwing, kMusicLayersHighOnly);
  arr.counter(32, 8, alt, kMusicLayersHighOnly, 0);
  arr.melody(40, 8, alt, alt, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             true);
  arr.bass(40, 8, BassStyle::HalfNote, kMusicLayersEssential);
  arr.drums(40, 8, DrumStyle::FocusSparse, kMusicLayersEssential);
  arr.chords(40, 8, ChordStyle::Pads, kMusicLayersEssential);
  arr.melody(48, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(48, 16, BassStyle::Bouncy, kMusicLayersEssential);
  arr.drums(48, 16, DrumStyle::GrooveSwing, kMusicLayersHighOnly);
  arr.chords(48, 16, ChordStyle::Stabs7, kMusicLayersFromMedium);
}

void composeNeonSteps(MusicTrackData* track) {
  Arranger arr(track, kMixolydian, 48, 8);
  const std::int8_t progA[4] = {0, 6, 3, 6};
  const std::int8_t progB[4] = {0, 5, 6, 3};
  const Motif a = motif(kNeonA, 8);
  const Motif b = motif(kNeonB, 8);
  const Motif alt = motif(kNeonAlt, 4);
  arr.progression(0, 64, progA, 4);
  arr.progression(24, 16, progB, 4);
  arr.bass(0, 8, BassStyle::Syncope, kMusicLayersEssential);
  arr.drums(0, 8, DrumStyle::GrooveSwing, kMusicLayersEssential);
  arr.arp(0, 8, kMusicLayersEssential, false);
  arr.melody(8, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(8, 16, BassStyle::Syncope, kMusicLayersEssential);
  arr.drums(8, 16, DrumStyle::GrooveSwing, kMusicLayersFromMedium);
  arr.arp(8, 16, kMusicLayersFromMedium, false);
  arr.melody(24, 16, b, a, MusicInstrument::PulseLead, kMusicLayersEssential, 3,
             false);
  arr.bass(24, 16, BassStyle::Syncope, kMusicLayersEssential);
  arr.drums(24, 16, DrumStyle::GrooveSwing, kMusicLayersHighOnly);
  arr.chords(24, 16, ChordStyle::Stabs, kMusicLayersFromMedium);
  arr.melody(40, 8, alt, alt, MusicInstrument::ArpVoice, kMusicLayersFromMedium, 0,
             true);
  arr.bass(40, 8, BassStyle::OrbitPedal, kMusicLayersEssential);
  arr.drums(40, 8, DrumStyle::FocusPulse, kMusicLayersEssential);
  arr.arp(40, 8, kMusicLayersFromMedium, true);
  arr.melody(48, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(48, 16, BassStyle::Syncope, kMusicLayersEssential);
  arr.drums(48, 16, DrumStyle::GrooveSwing, kMusicLayersHighOnly);
  arr.chords(48, 16, ChordStyle::Stabs, kMusicLayersFromMedium);
  arr.counter(56, 8, alt, kMusicLayersHighOnly, 2);
}

void composeAfterimage(MusicTrackData* track) {
  Arranger arr(track, kMinor, 43, 0);
  const std::int8_t progA[4] = {0, 3, 5, 6};
  const std::int8_t progB[4] = {0, 5, 3, 5};
  const Motif a = motif(kAfterA, 9);
  const Motif b = motif(kAfterB, 9);
  const Motif alt = motif(kAfterAlt, 4);
  arr.progression(0, 72, progA, 4);
  arr.progression(24, 16, progB, 4);
  arr.progression(56, 16, progB, 4);
  arr.bass(0, 8, BassStyle::Drive8, kMusicLayersEssential);
  arr.drums(0, 8, DrumStyle::HousePump, kMusicLayersEssential);
  arr.melody(8, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(8, 16, BassStyle::Drive8, kMusicLayersEssential);
  arr.drums(8, 16, DrumStyle::HousePump, kMusicLayersFromMedium);
  arr.chords(8, 16, ChordStyle::Stabs, kMusicLayersFromMedium);
  arr.melody(24, 16, b, a, MusicInstrument::PulseLead, kMusicLayersEssential, 2,
             false);
  arr.bass(24, 16, BassStyle::Drive8, kMusicLayersEssential);
  arr.drums(24, 16, DrumStyle::HousePump, kMusicLayersHighOnly);
  arr.chords(24, 16, ChordStyle::Stabs7, kMusicLayersFromMedium);
  arr.counter(32, 8, alt, kMusicLayersHighOnly, 0);
  arr.melody(40, 8, alt, alt, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             true);
  arr.bass(40, 8, BassStyle::HalfNote, kMusicLayersEssential);
  arr.drums(40, 8, DrumStyle::FocusSparse, kMusicLayersEssential);
  arr.chords(40, 8, ChordStyle::Pads, kMusicLayersEssential);
  arr.melody(48, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(48, 16, BassStyle::Drive8, kMusicLayersEssential);
  arr.drums(48, 16, DrumStyle::HousePump, kMusicLayersHighOnly);
  arr.chords(48, 16, ChordStyle::Stabs7, kMusicLayersFromMedium);
  arr.counter(64, 8, alt, kMusicLayersHighOnly, 2);
}

void composeGhostGrid(MusicTrackData* track) {
  Arranger arr(track, kMinor, 36, 0);
  const std::int8_t progA[2] = {0, 5};
  const std::int8_t progB[2] = {0, 3};
  const Motif a = motif(kGhostA, 4);
  const Motif b = motif(kGhostB, 4);
  const Motif alt = motif(kGhostAlt, 2);
  arr.progression(0, 48, progA, 2);
  arr.progression(24, 8, progB, 2);
  arr.progression(32, 8, progA, 2);
  arr.bass(0, 8, BassStyle::HalfNote, kMusicLayersEssential);
  arr.drums(0, 8, DrumStyle::FocusSparse, kMusicLayersEssential);
  arr.chords(0, 8, ChordStyle::Pads, kMusicLayersEssential);
  arr.melody(8, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(8, 16, BassStyle::HalfNote, kMusicLayersEssential);
  arr.drums(8, 16, DrumStyle::FocusSparse, kMusicLayersFromMedium);
  arr.chords(8, 16, ChordStyle::Pads, kMusicLayersEssential);
  arr.arp(8, 8, kMusicLayersFromMedium, true);
  arr.melody(24, 8, b, a, MusicInstrument::PulseLead, kMusicLayersEssential, 2,
             false);
  arr.bass(24, 8, BassStyle::HalfNote, kMusicLayersEssential);
  arr.drums(24, 8, DrumStyle::FocusPulse, kMusicLayersFromMedium);
  arr.chords(24, 8, ChordStyle::Pads, kMusicLayersEssential);
  arr.melody(32, 8, alt, alt, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             true);
  arr.bass(32, 8, BassStyle::OrbitPedal, kMusicLayersEssential);
  arr.drums(32, 8, DrumStyle::FocusSparse, kMusicLayersEssential);
  arr.melody(40, 8, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(40, 8, BassStyle::HalfNote, kMusicLayersEssential);
  arr.drums(40, 8, DrumStyle::FocusPulse, kMusicLayersHighOnly);
  arr.chords(40, 8, ChordStyle::Pads, kMusicLayersEssential);
  arr.arp(40, 8, kMusicLayersFromMedium, true);
}

void composeSoftDrop(MusicTrackData* track) {
  Arranger arr(track, kMajor, 41, 0);
  const std::int8_t progA[4] = {0, 5, 3, 4};
  const std::int8_t progB[4] = {3, 4, 0, 5};
  const Motif a = motif(kDropA, 7);
  const Motif b = motif(kDropB, 7);
  const Motif alt = motif(kDropAlt, 4);
  arr.progression(0, 56, progA, 4);
  arr.progression(24, 16, progB, 4);
  arr.bass(0, 8, BassStyle::HalfNote, kMusicLayersEssential);
  arr.drums(0, 8, DrumStyle::FocusPulse, kMusicLayersEssential);
  arr.arp(0, 8, kMusicLayersEssential, false);
  arr.melody(8, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(8, 16, BassStyle::HalfNote, kMusicLayersEssential);
  arr.drums(8, 16, DrumStyle::FocusPulse, kMusicLayersFromMedium);
  arr.arp(8, 16, kMusicLayersFromMedium, false);
  arr.melody(24, 16, b, a, MusicInstrument::PulseLead, kMusicLayersEssential, 2,
             false);
  arr.bass(24, 16, BassStyle::Bouncy, kMusicLayersEssential);
  arr.drums(24, 16, DrumStyle::FocusPulse, kMusicLayersHighOnly);
  arr.chords(24, 16, ChordStyle::Pads, kMusicLayersFromMedium);
  arr.melody(40, 8, alt, alt, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             true);
  arr.bass(40, 8, BassStyle::HalfNote, kMusicLayersEssential);
  arr.drums(40, 8, DrumStyle::FocusSparse, kMusicLayersEssential);
  arr.arp(40, 8, kMusicLayersEssential, true);
  arr.melody(48, 8, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(48, 8, BassStyle::HalfNote, kMusicLayersEssential);
  arr.drums(48, 8, DrumStyle::FocusPulse, kMusicLayersHighOnly);
  arr.chords(48, 8, ChordStyle::Pads, kMusicLayersFromMedium);
  arr.arp(48, 8, kMusicLayersHighOnly, false);
}

void composeOrbit(MusicTrackData* track) {
  Arranger arr(track, kDorian, 45, 0);
  const std::int8_t progA[2] = {0, 3};
  const std::int8_t progB[2] = {6, 3};
  const Motif a = motif(kOrbitA, 4);
  const Motif b = motif(kOrbitB, 4);
  const Motif alt = motif(kOrbitAlt, 4);
  arr.progression(0, 64, progA, 2);
  arr.progression(24, 16, progB, 2);
  arr.progression(48, 16, progA, 2);
  arr.bass(0, 8, BassStyle::OrbitPedal, kMusicLayersEssential);
  arr.drums(0, 8, DrumStyle::FocusPulse, kMusicLayersEssential);
  arr.melody(8, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(8, 16, BassStyle::OrbitPedal, kMusicLayersEssential);
  arr.drums(8, 16, DrumStyle::FocusPulse, kMusicLayersFromMedium);
  arr.chords(8, 16, ChordStyle::Pads, kMusicLayersEssential);
  arr.melody(24, 16, b, a, MusicInstrument::PulseLead, kMusicLayersEssential, 2,
             false);
  arr.bass(24, 16, BassStyle::OrbitPedal, kMusicLayersEssential);
  arr.drums(24, 16, DrumStyle::FocusPulse, kMusicLayersHighOnly);
  arr.chords(24, 16, ChordStyle::Pads, kMusicLayersFromMedium);
  arr.arp(24, 8, kMusicLayersFromMedium, true);
  arr.melody(40, 8, alt, alt, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             true);
  arr.bass(40, 8, BassStyle::OrbitPedal, kMusicLayersEssential);
  arr.drums(40, 8, DrumStyle::FocusSparse, kMusicLayersEssential);
  arr.melody(48, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(48, 16, BassStyle::OrbitPedal, kMusicLayersEssential);
  arr.drums(48, 16, DrumStyle::FocusPulse, kMusicLayersHighOnly);
  arr.chords(48, 16, ChordStyle::Pads, kMusicLayersFromMedium);
  arr.counter(56, 8, alt, kMusicLayersHighOnly, 0);
}

void composeClearSkies(MusicTrackData* track) {
  Arranger arr(track, kMajor, 38, 0);
  const std::int8_t progA[4] = {0, 4, 5, 3};
  const std::int8_t progB[4] = {3, 4, 0, 5};
  const Motif a = motif(kClearA, 8);
  const Motif b = motif(kClearB, 8);
  const Motif alt = motif(kClearAlt, 4);
  arr.progression(0, 64, progA, 4);
  arr.progression(24, 16, progB, 4);
  arr.bass(0, 8, BassStyle::Bouncy, kMusicLayersEssential);
  arr.drums(0, 8, DrumStyle::ArcadeDrive, kMusicLayersEssential);
  arr.chords(0, 8, ChordStyle::Pads, kMusicLayersEssential);
  arr.melody(8, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(8, 16, BassStyle::Bouncy, kMusicLayersEssential);
  arr.drums(8, 16, DrumStyle::ArcadeDrive, kMusicLayersFromMedium);
  arr.chords(8, 16, ChordStyle::Stabs, kMusicLayersFromMedium);
  arr.melody(24, 16, b, a, MusicInstrument::PulseLead, kMusicLayersEssential, 2,
             false);
  arr.bass(24, 16, BassStyle::Drive8, kMusicLayersEssential);
  arr.drums(24, 16, DrumStyle::ArcadeDrive, kMusicLayersHighOnly);
  arr.chords(24, 16, ChordStyle::Stabs, kMusicLayersFromMedium);
  arr.counter(32, 8, alt, kMusicLayersHighOnly, 0);
  arr.melody(40, 8, alt, alt, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             true);
  arr.bass(40, 8, BassStyle::HalfNote, kMusicLayersEssential);
  arr.drums(40, 8, DrumStyle::FocusSparse, kMusicLayersEssential);
  arr.chords(40, 8, ChordStyle::Pads, kMusicLayersEssential);
  arr.melody(48, 16, a, b, MusicInstrument::PulseLead, kMusicLayersEssential, 0,
             false);
  arr.bass(48, 16, BassStyle::Bouncy, kMusicLayersEssential);
  arr.drums(48, 16, DrumStyle::ArcadeDrive, kMusicLayersHighOnly);
  arr.chords(48, 16, ChordStyle::Stabs, kMusicLayersFromMedium);
  arr.arp(56, 8, kMusicLayersHighOnly, false);
}

struct TrackSpec {
  const char* id;
  const char* title;
  MusicCollection collection;
  int bpm;
  int bars;
  void (*compose)(MusicTrackData*);
};

// The catalogue order is public and stable: the title theme is index 0.
constexpr TrackSpec kTrackSpecs[] = {
    {"arcade-boot", "Boot Sequence", MusicCollection::Arcade, 112, 64,
     composeBootSequence},
    {"arcade-bag", "Seven Bag", MusicCollection::Arcade, 128, 72,
     composeSevenBag},
    {"arcade-kick", "Wall Kick", MusicCollection::Arcade, 138, 80,
     composeWallKick},
    {"arcade-redline", "Redline", MusicCollection::Arcade, 150, 80,
     composeRedline},
    {"groove-pocket", "Pocket Circuit", MusicCollection::Groove, 94, 56,
     composePocketCircuit},
    {"groove-night", "Night Bus", MusicCollection::Groove, 104, 64,
     composeNightBus},
    {"groove-neon", "Neon Steps", MusicCollection::Groove, 118, 64,
     composeNeonSteps},
    {"groove-afterimage", "Afterimage", MusicCollection::Groove, 126, 72,
     composeAfterimage},
    {"focus-ghost", "Ghost Grid", MusicCollection::Focus, 88, 48,
     composeGhostGrid},
    {"focus-drop", "Soft Drop", MusicCollection::Focus, 100, 56,
     composeSoftDrop},
    {"focus-orbit", "Orbit", MusicCollection::Focus, 110, 64, composeOrbit},
    {"focus-clear", "Clear Skies", MusicCollection::Focus, 116, 64,
     composeClearSkies},
};

constexpr int kTrackSpecCount =
    static_cast<int>(sizeof(kTrackSpecs) / sizeof(kTrackSpecs[0]));

void sortTrackNotes(MusicTrackData* track) {
  std::sort(track->notes.begin(), track->notes.end(),
            [](const MusicNote& a, const MusicNote& b) {
              return a.startTick < b.startTick;
            });
}

}  // namespace

const std::vector<MusicTrackData>& musicCatalogue() {
  static const std::vector<MusicTrackData> catalogue = [] {
    std::vector<MusicTrackData> built;
    built.reserve(kTrackSpecCount);
    for (int i = 0; i < kTrackSpecCount; ++i) {
      const TrackSpec& spec = kTrackSpecs[i];
      MusicTrackData track;
      track.id = spec.id;
      track.title = spec.title;
      track.collection = spec.collection;
      track.bpm = spec.bpm;
      track.bars = spec.bars;
      spec.compose(&track);
      sortTrackNotes(&track);
      MusicValidationReport report;
      if (!validateMusicTrack(track, i, &report)) {
        // A track that fails its own validation never plays: the catalogue
        // keeps the slot (the IDs are stable) and the tests fail loudly on
        // validateMusicCatalogue().
        std::fprintf(stderr,
                     "Music: track \"%s\" failed validation: %s (tick %u, "
                     "note %d); it stays silent\n",
                     spec.id, report.error, report.tick, report.note);
        track.notes.clear();
      }
      built.push_back(std::move(track));
    }
    return built;
  }();
  return catalogue;
}

MusicValidationReport validateMusicCatalogue() {
  MusicValidationReport report;
  report.ok = true;
  const std::vector<MusicTrackData>& tracks = musicCatalogue();
  for (std::size_t i = 0; i < tracks.size(); ++i) {
    MusicValidationReport one;
    if (!validateMusicTrack(tracks[i], static_cast<int>(i), &one)) {
      return one;
    }
    report.maxVoices = std::max(report.maxVoices, one.maxVoices);
  }
  report.trackId = "catalogue";
  return report;
}

}  // namespace stackfall
