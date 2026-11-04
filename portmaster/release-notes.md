# STACKFALL release notes

## 1.0.0 (2026-11-04)

First release. The binary reports the version `1.0.0` in its startup log.

### Game

- Fixed 60 ticks per second. The same seed and the same inputs produce the
  same game.
- All seven pieces in shuffled bags, SRS-style rotation with wall kicks.
- Ghost outline, hold with one swap per piece, and one to five Next
  previews.
- Line clears score by count; the level rises every ten cleared lines up to
  level 20 and multiplies the clear score. Soft and hard drops score by the
  distance traveled.
- Eight settings on the Options screen: sound, ghost outline, board grid,
  hold preview, Next preview count, grayscale play scene, orientation, and
  music. All ten persisted values survive between sessions.
- Portrait play: set Options to Portrait and the next launch turns the whole
  game into a tall 544 x 960 frame with 40-pixel board squares and controls
  that keep the action they point at.
- A generated twelve-track soundtrack in three collections, composed and
  synthesized at run time. The Music settings page selects the collection
  pool, both volumes, and the next track; each track layers more voices as
  the level rises. The sound switch stays the master mute for both buses.
- High score persists, and a round you leave keeps every point it scored.
- Procedural graphics drawn from rectangles and lines, a drawn 5 x 7 font,
  and synthesized sound and music. No asset files.
- Offline only: no network, no telemetry.

### Port

- Ready to run on ARM64 (`aarch64`) PortMaster devices. No runtime and no
  extra libraries.
- Launcher written from the current official PortMaster template. The save
  files live in `ports/stackfall/conf/`.
- Licensed under the MIT license; see `licenses/LICENSE`. Links SDL 2 under
  the zlib license; see `licenses/LICENSE.SDL2.txt`.
- Not yet validated on a physical device. The package was assembled and
  checked locally only.
