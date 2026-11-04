#!/bin/bash
# SPDX-License-Identifier: MIT
#
# STACKFALL PortMaster launcher (docs/SPEC.md sections 5, 9, and 10.3,
# task T017).
#
# Written from the current official PortMaster launch template
# (https://portmaster.games/packaging.html, reviewed 2026-11-04): find the
# control folder, source control.txt, source the CFW modifier the install
# ships, call get_controls, run from the port's own game folder, export the
# save and controller environment, call pm_platform_helper on the
# executable, run it, then call pm_finish.
#
# Installed layout (specification section 10.3), staged by
# tools/portmaster/install-tree.sh:
#
#   /roms/ports/Stackfall.sh     this file
#   /roms/ports/stackfall/
#     stackfall.aarch64          the AArch64 build (task T016)
#     conf/                      created below; record, settings, session log
#     libs.aarch64/              optional extra libraries; none are shipped
#     log.txt                    this script's stdout, replaced each launch

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source "$controlfolder/control.txt"

# The per-CFW modifier, for example mod_ROCKNIX.txt, when this install
# ships one.
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR="/$directory/ports/stackfall"
CONFDIR="$GAMEDIR/conf"

mkdir -p "$CONFDIR"

cd "$GAMEDIR" || {
  echo "STACKFALL: cannot enter the game folder $GAMEDIR"
  exit 1
}

# One log per launch: the redirection replaces the file, and the game's own
# session log stays separate in conf/, so the two sinks never share a file.
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# The save paths of specification section 9. Both name the port's own conf/
# directory: XDG_DATA_HOME follows the PortMaster template, and the explicit
# STACKFALL_SAVE_DIR removes every other reading of it, because the game
# resolves that variable first.
export XDG_DATA_HOME="$CONFDIR"
export STACKFALL_SAVE_DIR="$CONFDIR"

# Extra libraries ride in the port folder; none ship today, so this names a
# directory that may not exist.
export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"

# The controller mapping get_controls built for this device.
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

EXEC="stackfall.${DEVICE_ARCH}"
if [ ! -f "$GAMEDIR/$EXEC" ] && [ -f "$GAMEDIR/stackfall.aarch64" ]; then
  echo "STACKFALL: no $EXEC for ${DEVICE_ARCH}; running the AArch64 build"
  EXEC="stackfall.aarch64"
fi

if [ ! -f "$GAMEDIR/$EXEC" ]; then
  pm_message "STACKFALL: $GAMEDIR/$EXEC is missing. Install the port again."
  sleep 3
  exit 1
fi

$ESUDO chmod +x "$GAMEDIR/$EXEC"

# PortMaster's platform-wide exit chord (Select and Start together on the
# RG503) is delivered by a background gptokeyb process on the installs that
# rely on it. Started with no mapping file, gptokeyb watches that chord only
# and injects no keys: STACKFALL reads the gamepad natively through SDL, so
# nothing here remaps a button, and the game never captures the chord itself
# (specification section 5). Only an install that provides gptokeyb gets the
# process; pm_finish cleans it up when the port ends.
if [ -n "${GPTOKEYB:-}" ] && [ -e "$controlfolder/gptokeyb" ]; then
  $GPTOKEYB "$EXEC" &
else
  echo "STACKFALL: this PortMaster provides no gptokeyb; the exit chord stays with the platform"
fi

# Platform bring-up right before the port runs, then the game. PortMaster
# passes no arguments, and forwarding them lets a shell run reach the game's
# own developer options, for example --frames N.
pm_platform_helper "$GAMEDIR/$EXEC"
"./$EXEC" "$@"
status=$?
echo "STACKFALL: $EXEC exited with status $status"

pm_finish
