# PortMaster packaging tools

This directory holds the tools that install and package STACKFALL for
PortMaster (docs/SPEC.md sections 10.3 and 10.4). The package sources
themselves live in `portmaster/`; see that directory's readme for the
player-facing text that ships inside the archive.

## Contents

- `install-tree.sh` — stage the local `/roms/ports` install layout of
  specification section 10.3 from a built binary.
- `capture-screenshot.sh` — capture the 4:3 gameplay screenshot of
  specification section 10.4 from the desktop build.
- `bmp2png.py` — convert the checkpoint BMP of a capture into the PNG the
  package needs, with no image library.
- `package.sh` — assemble the distributable `stackfall.zip` of
  specification section 10.4 from the ARM64 binary, the package sources,
  and a captured screenshot.

## The two layouts

`install-tree.sh` builds the on-device tree of section 10.3:

```text
<dest>/
├── Stackfall.sh              <- portmaster/Stackfall.sh
└── stackfall/
    └── stackfall.aarch64     <- tools/arm64/build-arm64.sh output
```

`package.sh` builds the release archive of section 10.4, whose entry list
follows the current PortMaster build system
(<https://portmaster.games/packaging.html> and the `build_release.py` of
<https://github.com/PortsMaster/PortMaster-New>, reviewed 2026-11-04):
the launch script and the game folder sit at the archive root, and the
metadata the installer consumes lands inside the game folder:

```text
stackfall.zip
├── Stackfall.sh
└── stackfall/
    ├── stackfall.aarch64
    ├── port.json
    ├── gameinfo.xml
    ├── screenshot.png
    ├── stackfall.md           <- portmaster/README.md, renamed as upstream does
    ├── release-notes.md
    ├── BUILDING.md
    └── licenses/
```

Unpacking the archive into a ports directory produces exactly the section
10.3 install layout, so the launcher the archive ships works unchanged.
The port folder name (`stackfall`) and the launch script name
(`Stackfall.sh`) follow the PortMaster naming rules: the folder is
lowercase, and the script carries a capital letter.

The archive is written deterministically. Entries are sorted, every entry
timestamp is pinned, and file modes are fixed, so two runs from the same
sources produce the same archive bytes. The evidence directory of a run
records the SHA-256 of every file and of the archive.

## Reproducing the package

```sh
tools/arm64/build-arm64.sh        # build-arm64/stackfall.aarch64
tools/portmaster/package.sh       # build/portmaster-package/stackfall.zip
```

The screenshot capture inside `package.sh` needs `Xvfb`, `xdotool`, and
`openbox`. Pass `--screenshot PATH` to package an existing PNG instead,
for example the one staged by an earlier run.

## The launcher

`portmaster/Stackfall.sh` follows the official template in order:
discover the control folder (`/opt/system/Tools/PortMaster`,
`/opt/tools/PortMaster`, `$XDG_DATA_HOME/PortMaster`, then the
`/roms/ports/PortMaster` of the device layout), source `control.txt`,
source the CFW modifier (`mod_${CFW_NAME}.txt`) when the install ships
one, call `get_controls`, create the port's `conf/` directory, switch to
the game folder, and redirect its own output to `log.txt`.

It then exports the environment the game needs:

- `XDG_DATA_HOME="$GAMEDIR/conf"` and `STACKFALL_SAVE_DIR="$GAMEDIR/conf"`,
  so the high-score record, the settings file, and the game's own session
  log land in the port's `conf/` directory (specification section 9).
- `LD_LIBRARY_PATH` with the port's `libs.${DEVICE_ARCH}` first, for the
  day the port ships extra libraries.
- `SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"`, the mapping
  `get_controls` built for the device (specification section 5).

`gptokeyb` runs in exit-hotkey-only mode — started with the process name
and no mapping file, so it watches the platform's Select and Start chord
and injects no keys. STACKFALL reads the gamepad natively through SDL, and
the game never captures the chord itself. The process starts only when the
installed PortMaster provides `$GPTOKEYB` and its `gptokeyb` binary, so an
install that delivers the exit chord another way gets no background
process. `pm_finish` cleans it up.

`pm_platform_helper` runs against the executable right before the game,
then the game runs, then `pm_finish`. Arguments passed to `Stackfall.sh`
reach the game, which PortMaster never supplies and a shell run can use
for the game's own developer options.

## Checking it

```sh
tests/harness/t017.sh    # the launcher and the section 10.3 install tree
tests/harness/t019.sh    # the release archive of section 10.4
```

`t017.sh` drives the launcher against a stub PortMaster install, at the
literal `/roms/ports` paths when the host allows creating `/roms`, and
once with the real desktop binary staged by `install-tree.sh`. `t019.sh`
assembles the archive, unpacks it into a fake ports directory, drives its
launcher the same way, and checks every required item. Both write their
evidence to `build/agent-checks/<run-id>/`; the reports live in
`notes/agent-testing.md`. Execution on a real RG503 stays with the blocked
device task (T018), and public publication stays with the release task.
