#pragma once

// The music policy layer of MUSIC.md (tasks T062/M4): the one component
// that turns scenes, the level, the player's music settings, and the Next
// track action into engine commands. Sequencing and synthesis live in
// stackfall/music.hpp; this header owns the behavior table of MUSIC.md's
// playback section:
//
//   title          Boot Sequence at low intensity, looping
//   round start    a track from the current pool, fading into its first bar
//   levels 1-5/6-12/13-20  the low/medium/high intensity map, applied at
//                  the next eight-bar phrase
//   pause          fade out over 100 ms, then freeze; resume fades back in
//   game over      fade over 250 ms and stay silent until a scene asks
//   Music Off      fade to silence over 100 ms and stop the transport
//   arrangement end / Next track  the next pool track, never an immediate
//                  repeat, switching on a bar boundary
//
// The director is SDL-free and clock-free, so its policy is pinned by the
// headless suite (tests/test_music.cpp). It owns no gameplay state and
// reads the Game through nothing at all: the platform layer hands it scene
// changes and the level, so music state can never reach the rules core,
// the gameplay hash, or the gameplay randomizer (MUSIC.md, persistence and
// compatibility). Its shuffle runs on its own PCG32 stream seeded per
// session, and no playback position or shuffle order persists between
// launches.

#include <cstdint>
#include <vector>

#include "stackfall/game.hpp"
#include "stackfall/music.hpp"
#include "stackfall/randomizer.hpp"

namespace stackfall {

// The catalogue index of the title theme (Boot Sequence), which the first
// title screen plays at low intensity.
inline constexpr int kMusicTitleTrack = 0;

// The director's own PCG32 stream: far from the gameplay stream, so the
// soundtrack's shuffle never shifts a piece sequence.
inline constexpr std::uint64_t kMusicStream = 7632;

// Level bands of the intensity maps (MUSIC.md, playback behavior).
MusicIntensity musicIntensityForLevel(int level);

class MusicDirector {
 public:
  explicit MusicDirector(MusicEngine& engine);

  // Starts a session: shuffles the Auto pool from `seed` and, when music is
  // enabled, starts the title theme at low intensity.
  void begin(std::uint64_t seed);

  // The player's MUSIC setting. Off fades music out over 100 ms and stops
  // the transport; any other mode from Off starts a fresh track from the
  // new pool; a mode change while music plays schedules the new pool's next
  // track at the next bar.
  void setMode(MusicMode mode);
  MusicMode mode() const { return mode_; }

  // The level feed: a band change schedules the new intensity map at the
  // next phrase boundary. The tempo never changes (MUSIC.md).
  void setLevel(int level);

  // One scene transition. The behavior table above is this function.
  void onScene(Scene previous, Scene next);

  // The Next track action: disabled while Music is Off, and never an
  // immediate repeat while another track is available.
  void nextTrack();

  // Once per frame: retries a command the queue could not take, and sends
  // the follow-up track when the current arrangement is one bar from its
  // end, so the switch lands on the loop boundary.
  void update();

  // What the music settings page shows: the playing track, or "" while
  // silent.
  const char* currentTrackTitle() const;
  int currentTrack() const;
  bool transportRunning() const { return engine_.snapshot().transportRunning; }

 private:
  // Refills `pool_` with the mode's collection in shuffled order, keeping
  // the last played track from also opening the next cycle.
  void refillPool();
  int pickNext();
  void requestPlay(int trackIndex);
  void pushOrKeep(const MusicCommand& command);

  MusicEngine& engine_;
  MusicMode mode_ = MusicMode::Auto;
  int level_ = 1;
  Scene scene_ = Scene::Boot;
  Pcg32 shuffle_{1, kMusicStream};
  std::vector<std::uint8_t> pool_;
  std::size_t poolPosition_ = 0;
  int lastTrack_ = -1;
  // The command the queue could not take; update() retries it. A scheduled
  // Play replaces it, exactly as the engine coalesces them.
  bool hasQueuedCommand_ = false;
  MusicCommand queuedCommand_{};
};

}  // namespace stackfall
