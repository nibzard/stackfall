# STACKFALL

STACKFALL is an offline block-stacking game for PortMaster handhelds.
Clear lines, keep room for the next piece, and chase your high score as the
game gets faster. Hold a piece for later, plan around five previews, and use
the ghost outline to place each drop.

The game runs entirely on the device. It needs no network, downloads no
files, and collects no data. Every block, outline, and letter is drawn by the
game itself from rectangles and lines, and every sound is synthesized at run
time. The rules, the look, and the sound are original work.

The game carries a twelve-track soundtrack it composes and synthesizes at
run time. Three collections of four tracks set the mood from the title screen
through a round, and each track layers more voices as your level rises. No
audio files ship.

Thank you to the PortMaster project for the packaging format, the build
environment, and the launcher template this port follows.

## Controls

| Action | Controller |
| --- | --- |
| Move | D-pad left / right |
| Soft drop | D-pad down |
| Rotate clockwise | A |
| Rotate counterclockwise | B |
| Hard drop | X |
| Hold | Y / Left shoulder |
| Pause or resume | Start |
| Mute or unmute | Select |

Use the D-pad to move in menus, A to select, and B to go back.
Pause with Start and choose **Quit to Title** to end a round; the points
survive. Select **Quit** on the title screen to close the game, or leave
through the PortMaster exit chord of your device.

## Options

Open **Options** from the title screen to change eight settings:

- Sound on or off. This is the master switch for all audio.
- Ghost outline on or off.
- Board grid on or off.
- Hold preview on or off.
- Next preview count from one to five.
- Grayscale play scene on or off.
- Orientation: landscape or portrait. The next launch turns the whole game
  into a tall 544 x 960 frame with 40-pixel board squares. Hold the device
  sideways; every control keeps the action it points at.
- Music. Opens the music settings page.

The music settings page carries four rows: Music (Auto, Arcade, Groove,
Focus, Off), Music volume, Effects volume, and Next track. Auto draws from
every collection; the other modes lock the pool; Off stops the music alone.
Both volumes step through eleven levels, 0 silent. Next track plays another
track of the pool right away. The page also names the track now playing.

Your high score and all ten settings persist between sessions.

## Saves

The game writes its save files into `ports/stackfall/conf/`. A save holds the
high score and the ten settings. A damaged save file costs only those values;
the game replaces it with defaults and keeps running.

## What is in this port

```text
Stackfall.sh               the launcher
stackfall/
  stackfall.aarch64        the game, built for ARM64
  port.json                PortMaster package metadata
  gameinfo.xml             the Ports menu entry
  screenshot.png           the gameplay screenshot
  stackfall.md             this readme
  release-notes.md         changes in this release
  BUILDING.md              how to rebuild the game from source
  licenses/                the licenses of this port
```

## Building it yourself

The game is open source under the MIT license. `BUILDING.md` in this folder
describes how to reproduce `stackfall.aarch64` from a clean ARM64 environment
with the documented commands and hashes.

## Credits and license

STACKFALL was written by the STACKFALL project contributors.
The game is licensed under the MIT license; see `licenses/LICENSE`.
It links Simple DirectMedia Layer (SDL) 2, licensed under the zlib license;
see `licenses/LICENSE.SDL2.txt`.
