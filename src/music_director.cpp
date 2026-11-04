// The music policy of MUSIC.md (task T062/M4). See
// include/stackfall/music_director.hpp for the behavior table this file
// implements.
//
// Every decision becomes a MusicCommand; the engine owns the timing. The
// director keeps at most one command the queue could not take and retries
// it on the next frame, which is the retry half of MUSIC.md's coalescing
// rule: only the latest requested destination stays pending.

#include "stackfall/music_director.hpp"

#include <algorithm>
#include <cstring>

namespace stackfall {
namespace {

// The fade lengths of the behavior table, in milliseconds.
constexpr std::uint16_t kSwitchFadeMs = 100;
constexpr std::uint16_t kPauseFadeMs = 100;
constexpr std::uint16_t kGameOverFadeMs = 250;
constexpr std::uint16_t kOffFadeMs = 100;

}  // namespace

MusicIntensity musicIntensityForLevel(int level) {
  if (level >= 13) {
    return MusicIntensity::High;
  }
  return level >= 6 ? MusicIntensity::Medium : MusicIntensity::Low;
}

MusicDirector::MusicDirector(MusicEngine& engine) : engine_(engine) {}

void MusicDirector::refillPool() {
  const std::vector<MusicTrackData>& tracks = musicCatalogue();
  pool_.clear();
  for (std::size_t i = 0; i < tracks.size(); ++i) {
    const bool fits = mode_ == MusicMode::Auto ||
                      tracks[i].collection == static_cast<MusicCollection>(
                                                static_cast<int>(mode_) - 1);
    if (fits) {
      pool_.push_back(static_cast<std::uint8_t>(i));
    }
  }
  // Fisher-Yates on the director's own stream (the Randomizer's shuffle
  // rule, one PCG32 generator of its own).
  for (std::size_t i = pool_.size(); i > 1; --i) {
    const std::size_t j = static_cast<std::size_t>(shuffle_.below(
        static_cast<std::uint32_t>(i)));
    std::swap(pool_[i - 1], pool_[j]);
  }
  // The last track of one cycle must not also open the next (MUSIC.md).
  if (pool_.size() > 1 && lastTrack_ >= 0 &&
      pool_.front() == static_cast<std::uint8_t>(lastTrack_)) {
    std::swap(pool_.front(), pool_.back());
  }
  poolPosition_ = 0;
}

int MusicDirector::pickNext() {
  if (pool_.empty()) {
    refillPool();
  }
  if (pool_.empty()) {
    return kMusicTitleTrack;  // an empty catalogue falls back to silence
  }
  if (poolPosition_ >= pool_.size()) {
    refillPool();
  }
  const int pick = pool_[poolPosition_];
  ++poolPosition_;
  return pick;
}

void MusicDirector::pushOrKeep(const MusicCommand& command) {
  if (engine_.tryPushCommand(command)) {
    hasQueuedCommand_ = false;
    queuedCommand_ = MusicCommand{};
  } else {
    hasQueuedCommand_ = true;
    queuedCommand_ = command;
  }
}

void MusicDirector::requestPlay(int trackIndex) {
  MusicCommand command;
  command.type = MusicCommandType::Play;
  command.track = static_cast<std::uint8_t>(trackIndex);
  command.intensity =
      static_cast<std::uint8_t>(scene_ == Scene::Title
                                    ? MusicIntensity::Low
                                    : musicIntensityForLevel(level_));
  command.fadeMs = kSwitchFadeMs;
  pushOrKeep(command);
}

void MusicDirector::begin(std::uint64_t seed) {
  shuffle_.seed(seed, kMusicStream);
  lastTrack_ = -1;
  pool_.clear();
  poolPosition_ = 0;
  scene_ = Scene::Title;
  refillPool();
  if (mode_ != MusicMode::Off) {
    requestPlay(kMusicTitleTrack);
  }
}

void MusicDirector::setMode(MusicMode mode) {
  if (mode == mode_) {
    return;
  }
  const MusicMode previous = mode_;
  mode_ = mode;
  if (mode == MusicMode::Off) {
    MusicCommand command;
    command.type = MusicCommandType::Stop;
    command.fadeMs = kOffFadeMs;
    pushOrKeep(command);
    return;
  }
  // A new pool means the old shuffle no longer applies.
  lastTrack_ = -1;
  pool_.clear();
  if (previous == MusicMode::Off) {
    // Returning from Off starts a fresh track from the selected pool. The
    // title menu pages - Options, Controls, and the music settings page -
    // behave like the title they sit over, and a frozen round restarts from
    // its pool exactly as a resume does.
    if (scene_ == Scene::Title || scene_ == Scene::Options ||
        scene_ == Scene::Controls || scene_ == Scene::MusicOptions) {
      requestPlay(kMusicTitleTrack);
    } else if (scene_ == Scene::Playing || scene_ == Scene::Paused ||
               scene_ == Scene::PauseControls) {
      requestPlay(pickNext());
    }
    return;
  }
  // A pool change while music plays: the next track of the new pool, at
  // the next bar boundary. Silent scenes stay silent.
  if (scene_ == Scene::Playing && transportRunning()) {
    requestPlay(pickNext());
  }
}

void MusicDirector::setLevel(int level) {
  level_ = level;
  if (mode_ == MusicMode::Off || scene_ != Scene::Playing) {
    return;
  }
  MusicCommand command;
  command.type = MusicCommandType::SetIntensity;
  command.intensity =
      static_cast<std::uint8_t>(musicIntensityForLevel(level_));
  pushOrKeep(command);
}

void MusicDirector::onScene(Scene previous, Scene next) {
  scene_ = next;
  if (mode_ == MusicMode::Off) {
    return;  // the master switch stays silent through every scene
  }
  if (next == Scene::Paused || next == Scene::PauseControls) {
    if (previous == Scene::Playing || previous == Scene::Paused ||
        previous == Scene::PauseControls) {
      MusicCommand command;
      command.type = MusicCommandType::Pause;
      command.fadeMs = kPauseFadeMs;
      pushOrKeep(command);
    }
    return;
  }
  if (next == Scene::Playing) {
    if (previous == Scene::Paused || previous == Scene::PauseControls) {
      MusicCommand command;
      command.type = MusicCommandType::Resume;
      command.fadeMs = kPauseFadeMs;
      pushOrKeep(command);
      return;
    }
    // A round begins: a track from the current pool, fading into its first
    // bar. Game over left the transport stopped, and the title theme is
    // playing, so either way this is a fresh Play.
    requestPlay(pickNext());
    return;
  }
  if (next == Scene::GameOver) {
    MusicCommand command;
    command.type = MusicCommandType::Stop;
    command.fadeMs = kGameOverFadeMs;
    pushOrKeep(command);
    return;
  }
  if (next == Scene::Title) {
    // Back to the title: the title theme again, through a short
    // transition. Game over stopped the transport; an abandoned round left
    // a pool track playing.
    requestPlay(kMusicTitleTrack);
  }
  // Controls, Options, and the music settings page carry whatever was
  // playing: they are pauses of the menu, not of the music.
}

void MusicDirector::nextTrack() {
  if (mode_ == MusicMode::Off) {
    return;  // the action is disabled while Music is Off (MUSIC.md)
  }
  if (scene_ != Scene::Title && scene_ != Scene::Playing &&
      scene_ != Scene::Paused && scene_ != Scene::PauseControls &&
      scene_ != Scene::MusicOptions) {
    return;
  }
  // Exclude the current track while another is available.
  const int current = currentTrack();
  if (lastTrack_ != current) {
    lastTrack_ = current;  // the exclusion anchor follows the engine
  }
  int pick = pickNext();
  const std::size_t poolSize = pool_.empty() ? 0 : pool_.size();
  if (pick == current && poolSize > 1) {
    pick = pickNext();
  }
  requestPlay(pick);
}

void MusicDirector::update() {
  if (hasQueuedCommand_) {
    pushOrKeep(queuedCommand_);
    if (hasQueuedCommand_) {
      return;  // the queue is still full; nothing else this frame
    }
  }
  if (mode_ == MusicMode::Off || scene_ != Scene::Playing) {
    return;
  }
  // One bar before the arrangement loops, line up the next pool track so
  // the engine's bar-boundary switch lands exactly on the loop point and
  // no track repeats back to back (MUSIC.md, playback behavior).
  const MusicSnapshot snapshot = engine_.snapshot();
  if (!snapshot.transportRunning || snapshot.pendingSwitch ||
      snapshot.totalBars < 2) {
    return;
  }
  if (snapshot.bar + 1 >= snapshot.totalBars) {
    lastTrack_ = snapshot.track;
    requestPlay(pickNext());
  }
}

const char* MusicDirector::currentTrackTitle() const {
  const MusicSnapshot snapshot = engine_.snapshot();
  if (!snapshot.transportRunning) {
    return "";
  }
  const std::vector<MusicTrackData>& tracks = musicCatalogue();
  if (snapshot.track >= tracks.size()) {
    return "";
  }
  return tracks[snapshot.track].title;
}

int MusicDirector::currentTrack() const {
  return engine_.snapshot().track;
}

}  // namespace stackfall
