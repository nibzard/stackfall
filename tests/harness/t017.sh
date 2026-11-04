#!/bin/sh
# T017 PortMaster launcher and install-layout check (docs/AGENT_TESTING.md,
# docs/SPEC.md section 10.3).
#
# One command reviews the launcher and drives it against a stub PortMaster
# install:
#
#   tests/harness/t017.sh
#
# Phases, each writing into build/agent-checks/<run-id>/:
#
#   static   syntax check of portmaster/Stackfall.sh and its evidence
#   stage    tools/portmaster/install-tree.sh staging and the /roms/ports
#            layout of specification section 10.3
#   sandbox  the launcher against a stub PortMaster: control folder
#            discovery, control.txt, the CFW modifier, get_controls, conf/,
#            the save and controller environment, gptokeyb in
#            exit-hotkey-only mode, pm_platform_helper, the run, pm_finish,
#            argument forwarding, log replacement, and the missing-binary
#            and no-gptokeyb paths
#   roms     the same launcher at the literal /roms/ports paths of the
#            device layout (creates /roms through sudo when the host has
#            none; T017_SKIP_ROMS=1 skips the phase)
#   game     the real desktop game binary, staged by install-tree.sh,
#            launched through the script under the dummy video and audio
#            drivers, so the environment the launcher exports is checked
#            against the game's own startup log
#   check    tests/harness/check_t017.py, the independent gate checker
#
# The launcher is architecture-independent shell, so the stub binary and
# the x86-64 desktop build prove its behavior here; the AArch64 binary of
# T016 and the real device stay with the blocked device tasks.
#
# Environment variables: T017_BUILD_DIR (default build), T017_RUN_ID
# (default t017-launcher-<date>), T017_TIMEOUT_S (default 60),
# T017_SKIP_BUILD=1 to reuse the build, T017_SKIP_ROMS=1 to skip the
# literal-/roms phase.

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${T017_BUILD_DIR:-$repo/build}
run_id=${T017_RUN_ID:-t017-launcher-$(date +%Y-%m-%d)}
timeout_s=${T017_TIMEOUT_S:-60}
run_dir="$repo/build/agent-checks/$run_id"
launcher="$repo/portmaster/Stackfall.sh"
stager="$repo/tools/portmaster/install-tree.sh"
checker="$repo/tests/harness/check_t017.py"

fail() {
  echo "t017: $*" >&2
  exit 1
}

case "$run_id" in
  */*|""|.|..) fail "T017_RUN_ID must name one directory, not '$run_id'" ;;
esac

[ -f "$launcher" ] || fail "missing launcher $launcher"
[ -f "$stager" ] || fail "missing staging tool $stager"
[ -f "$checker" ] || fail "missing checker $checker"

# The marker file inside the fake /roms install proves this run owns what
# it removes; rmdir only takes directories nothing else uses.
roms_marker=".t017-fake-install"
cleanup_roms() {
  if [ -e "/roms/ports/stackfall/$roms_marker" ]; then
    rm -rf /roms/ports/stackfall /roms/ports/PortMaster /roms/ports/Stackfall.sh
  fi
  rmdir /roms/ports 2>/dev/null || sudo -n rmdir /roms/ports 2>/dev/null || true
  rmdir /roms 2>/dev/null || sudo -n rmdir /roms 2>/dev/null || true
}
trap cleanup_roms EXIT INT TERM

rm -rf "$run_dir"
mkdir -p "$run_dir" || fail "cannot create $run_dir"

# The controller mapping get_controls hands the launcher on a real device.
stub_mapping="03000000c0160000e0de000069010000,Anbernic RG503,a:b1,b:b0,x:b3,y:b2,back:b6,guide:b8,start:b7,leftshoulder:b9,rightshoulder:b10,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,leftx:a0,lefty:a1,"

echo "t017: repository $repo"
echo "t017: launcher $launcher"
echo "t017: evidence $run_dir"

# ---------------------------------------------------------------------------
# Stub PortMaster pieces. They stand in for the real control.txt, which
# exports the same variables, and record what the launcher did.
# ---------------------------------------------------------------------------
write_control_txt() {
  # $1 control folder, $2 value of $directory, $3 DEVICE_ARCH,
  # $4 gptokeyb yes/no
  cf=$1
  roms_value=$2
  arch=$3
  gptk=$4
  mkdir -p "$cf" || fail "cannot create the stub control folder $cf"
  {
    echo '# Stub PortMaster control folder of the T017 launcher check.'
    echo '# It exports what the official control.txt exports and records'
    echo '# every call the launcher makes; see the real file at'
    echo '# https://github.com/PortsMaster/PortMaster-GUI (PortMaster/control.txt).'
    echo 'echo "control.txt sourced: $controlfolder" >> "$T017_TRACE"'
    echo "export directory=\"$roms_value\""
    echo 'export CFW_NAME="ROCKNIX"'
    echo "export DEVICE_ARCH=\"$arch\""
    echo 'export ESUDO=""'
    echo 'export ESUDOKILL=""'
    echo "export sdl_controllerconfig=\"$stub_mapping\""
    echo 'get_controls() {'
    echo '  echo "get_controls" >> "$T017_TRACE"'
    echo '}'
    echo 'pm_message() {'
    echo '  echo "pm_message: $*" >> "$T017_TRACE"'
    echo '}'
    echo 'pm_show_error() {'
    echo '  echo "pm_show_error: $*" >> "$T017_TRACE"'
    echo '}'
    echo 'pm_platform_helper() {'
    echo '  echo "pm_platform_helper: $*" >> "$T017_TRACE"'
    echo '}'
    echo 'pm_finish() {'
    echo '  echo "pm_finish" >> "$T017_TRACE"'
    echo '}'
    if [ "$gptk" = yes ]; then
      echo "export GPTOKEYB=\"$cf/gptokeyb\""
    else
      echo '# This install defines no GPTOKEYB: an old PortMaster, or one that'
      echo '# delivers the exit chord without gptokeyb.'
    fi
  } > "$cf/control.txt"
}

write_gptokeyb_stub() {
  cf=$1
  cat > "$cf/gptokeyb" <<'STUB'
#!/bin/sh
# Stub gptokeyb of the T017 launcher check: it records its arguments and
# returns, where the real one would watch the exit chord until pm_finish.
{
  printf 'gptokeyb argv:'
  count=0
  for arg in "$@"; do
    printf ' [%s]' "$arg"
    count=$((count + 1))
  done
  printf ' argc=%d\n' "$count"
} >> "$T017_TRACE"
STUB
  chmod 755 "$cf/gptokeyb"
}

write_mod_stub() {
  cf=$1
  {
    echo '# Stub CFW modifier of the T017 launcher check.'
    echo 'echo "mod_ROCKNIX.txt sourced" >> "$T017_TRACE"'
  } > "$cf/mod_ROCKNIX.txt"
}

write_game_stub() {
  # $1 path of the game binary, $2 marker to drop beside it
  dest=$1
  cat > "$dest" <<'STUB'
#!/bin/sh
# Stub STACKFALL binary of the T017 launcher check: it records the process
# environment the launcher handed over and returns.
{
  echo "game: started"
  echo "game: cwd=$(pwd)"
  echo "game: argc=$#"
  for arg in "$@"; do
    echo "game: argv [$arg]"
  done
  echo "game: XDG_DATA_HOME=[${XDG_DATA_HOME:-}]"
  echo "game: STACKFALL_SAVE_DIR=[${STACKFALL_SAVE_DIR:-}]"
  echo "game: STACKFALL_LOG_FILE=[${STACKFALL_LOG_FILE:-}]"
  echo "game: SDL_GAMECONTROLLERCONFIG=[${SDL_GAMECONTROLLERCONFIG:-}]"
  echo "game: LD_LIBRARY_PATH=[${LD_LIBRARY_PATH:-}]"
  echo "game: GPTOKEYB=[${GPTOKEYB:-}]"
  echo "game: done"
} >> "$T017_GAME_ENV"
echo "stub game stdout: STACKFALL launcher check"
STUB
  chmod 755 "$dest"
}

# Stages one sandbox install: a control folder under <name>/root/xdg, which
# the launcher reaches through XDG_DATA_HOME, and a ports tree whose game
# folder the stub control.txt names through $directory.
#   $1 name, $2 arch, $3 mod yes/no, $4 gptokeyb yes/no, $5 binary yes/no
stage_sandbox() {
  name=$1
  arch=$2
  mod=$3
  gptk=$4
  binary=$5
  base="$run_dir/$name"
  root="$base/root"
  rm -rf "$base"
  mkdir -p "$root/xdg/PortMaster" "$root/roms/ports/stackfall" "$base"
  write_control_txt "$root/xdg/PortMaster" "$root/roms" "$arch" "$gptk"
  write_gptokeyb_stub "$root/xdg/PortMaster"
  [ "$mod" = yes ] && write_mod_stub "$root/xdg/PortMaster"
  cp "$launcher" "$root/roms/ports/Stackfall.sh"
  chmod 755 "$root/roms/ports/Stackfall.sh"
  if [ "$binary" = yes ]; then
    write_game_stub "$root/roms/ports/stackfall/stackfall.$arch"
  fi
}

# Runs the launcher and records its status. $1 run directory, $2 launcher
# path, $3 XDG_DATA_HOME value; the rest reaches the launcher as arguments.
run_launcher() {
  dir=$1
  script=$2
  xdg=$3
  shift 3
  : > "$dir/trace.txt"
  : > "$dir/game-env.txt"
  env T017_TRACE="$dir/trace.txt" T017_GAME_ENV="$dir/game-env.txt" \
    XDG_DATA_HOME="$xdg" timeout "$timeout_s" "$script" "$@" \
    > "$dir/launcher.out" 2>&1
  echo "$?" > "$dir/status.txt"
  # The tee of the template holds the log open a moment past the script.
  sleep 1
}

tree_snapshot() {
  find "$1" | LC_ALL=C sort
}

# ---------------------------------------------------------------------------
# Phase: static evidence.
# ---------------------------------------------------------------------------
echo
echo "=== phase static ==="
bash -n "$launcher" > "$run_dir/static-syntax.txt" 2>&1 \
  || fail "portmaster/Stackfall.sh fails bash -n"

# The official basic launch template, as reviewed on 2026-11-04 at
# https://portmaster.games/packaging.html. Kept in the evidence so the
# review of Stackfall.sh against it can be reproduced later.
cat > "$run_dir/official-template.sh" <<'TEMPLATE'
#!/bin/bash

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

source $controlfolder/control.txt

#export PORT_32BIT="Y" # If using a 32 bit port
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR=/$directory/ports/portfolder/
CONFDIR="$GAMEDIR/conf/"

mkdir -p "$GAMEDIR/conf"

cd $GAMEDIR

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

export XDG_DATA_HOME="$CONFDIR"
export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
#export TEXTINPUTINTERACTIVE="Y"

# if XDG Path does not work
# Use bind_directories to reroute that to a location within the ports folder.
bind_directories ~/.portfolder $GAMEDIR/conf/.portfolder

# If using gl4es
#if [ -f "${controlfolder}/libgl_${CFW_NAME}.txt" ]; then
#  source "${controlfolder}/libgl_${CFW_NAME}.txt"
#else
#  source "${controlfolder}/libgl_default.txt"
#fi

$GPTOKEYB "portexecutable.${DEVICE_ARCH}" -c "./portname.gptk.$ANALOGSTICKS" &
pm_platform_helper "$GAMEDIR/portexecutable.${DEVICE_ARCH}"
./portexecutable.${DEVICE_ARCH}

pm_finish
TEMPLATE

{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "launcher: $launcher"
  echo "template: https://portmaster.games/packaging.html (reviewed 2026-11-04)"
  cd "$repo" && sha256sum portmaster/Stackfall.sh portmaster/README.md \
    tools/portmaster/install-tree.sh tests/harness/t017.sh \
    tests/harness/check_t017.py
} > "$run_dir/source.sha256" || fail "cannot record source identity"
cp "$launcher" "$run_dir/Stackfall.sh.copy"

# ---------------------------------------------------------------------------
# Phase: install-tree staging, the layout of specification section 10.3.
# ---------------------------------------------------------------------------
echo
echo "=== phase stage ==="
mkdir -p "$run_dir/stage"
if [ "${T017_SKIP_BUILD:-0}" != "1" ]; then
  cmake -S "$repo" -B "$build_dir" -G Ninja >/dev/null \
    || fail "cmake configure failed in $build_dir"
  cmake --build "$build_dir" || fail "build failed in $build_dir"
fi
[ -x "$build_dir/stackfall" ] || fail "missing desktop game $build_dir/stackfall"
sh "$stager" --binary "$build_dir/stackfall" --arch x86_64 \
  --dest "$run_dir/stage/ports" > "$run_dir/stage/stage.txt" 2>&1 \
  || fail "install-tree.sh failed; see $run_dir/stage/stage.txt"
cat "$run_dir/stage/stage.txt"
tree_snapshot "$run_dir/stage/ports" > "$run_dir/stage/tree.txt"

# ---------------------------------------------------------------------------
# Phase: sandbox runs through the stub PortMaster.
# ---------------------------------------------------------------------------
echo
echo "=== phase sandbox ==="

echo "-- run base: stub install, CFW modifier, gptokeyb"
stage_sandbox base aarch64 yes yes yes
tree_snapshot "$run_dir/base/root/roms/ports" > "$run_dir/base/tree-before.txt"
run_launcher "$run_dir/base" "$run_dir/base/root/roms/ports/Stackfall.sh" \
  "$run_dir/base/root/xdg"
tree_snapshot "$run_dir/base/root/roms/ports" > "$run_dir/base/tree-after.txt"

echo "-- run args: the launcher forwards its own arguments"
mkdir -p "$run_dir/args"
run_launcher "$run_dir/args" "$run_dir/base/root/roms/ports/Stackfall.sh" \
  "$run_dir/base/root/xdg" --frames 12 --seed 7

echo "-- run repeat: a second launch replaces log.txt"
mkdir -p "$run_dir/repeat"
run_launcher "$run_dir/repeat" "$run_dir/base/root/roms/ports/Stackfall.sh" \
  "$run_dir/base/root/xdg"
cp "$run_dir/base/root/roms/ports/stackfall/log.txt" "$run_dir/repeat/log.txt"

echo "-- run nomod: the install ships no CFW modifier"
stage_sandbox nomod aarch64 no yes yes
run_launcher "$run_dir/nomod" "$run_dir/nomod/root/roms/ports/Stackfall.sh" \
  "$run_dir/nomod/root/xdg"

echo "-- run nogptk: the install defines no GPTOKEYB"
stage_sandbox nogptk aarch64 yes no yes
run_launcher "$run_dir/nogptk" "$run_dir/nogptk/root/roms/ports/Stackfall.sh" \
  "$run_dir/nogptk/root/xdg"

echo "-- run nobinary: the port folder carries no executable"
stage_sandbox nobinary aarch64 yes yes no
run_launcher "$run_dir/nobinary" "$run_dir/nobinary/root/roms/ports/Stackfall.sh" \
  "$run_dir/nobinary/root/xdg"

# ---------------------------------------------------------------------------
# Phase: the literal /roms/ports paths of specification section 10.3.
# ---------------------------------------------------------------------------
echo
echo "=== phase roms ==="
roms_skipped=""
if [ "${T017_SKIP_ROMS:-0}" = "1" ]; then
  roms_skipped="T017_SKIP_ROMS=1"
elif [ -d /opt/system/Tools/PortMaster ] || [ -d /opt/tools/PortMaster ]; then
  roms_skipped="a PortMaster lives in /opt on this host, so the launcher would not reach the /roms fallback"
elif [ -e /roms ]; then
  roms_skipped="/roms already exists on this host and is not this run's to replace"
elif ! sudo -n true 2>/dev/null; then
  roms_skipped="no passwordless sudo on this host"
else
  mkdir -p "$run_dir/roms"
  # Register the cleanup before creating anything outside the repository.
  if [ -n "${LOOPER_RESOURCE_CLEANUP_FILE:-}" ]; then
    {
      echo "# t017 launcher check: the fake /roms/ports install"
      echo "if [ -e \"/roms/ports/stackfall/$roms_marker\" ]; then"
      echo "  rm -rf /roms/ports/stackfall /roms/ports/PortMaster /roms/ports/Stackfall.sh"
      echo "fi"
      echo "rmdir /roms/ports 2>/dev/null || sudo -n rmdir /roms/ports 2>/dev/null || true"
      echo "rmdir /roms 2>/dev/null || sudo -n rmdir /roms 2>/dev/null || true"
    } >> "$LOOPER_RESOURCE_CLEANUP_FILE" || fail "cannot register the /roms cleanup"
    echo "cleanup registered in $LOOPER_RESOURCE_CLEANUP_FILE" > "$run_dir/roms/cleanup.txt"
  fi
  sudo -n mkdir -p /roms/ports || fail "cannot create /roms/ports"
  sudo -n chown "$(id -u):$(id -g)" /roms /roms/ports \
    || fail "cannot take ownership of the fresh /roms"
  mkdir -p /roms/ports/stackfall
  write_control_txt /roms/ports/PortMaster "roms" aarch64 yes
  write_gptokeyb_stub /roms/ports/PortMaster
  write_mod_stub /roms/ports/PortMaster
  cp "$launcher" /roms/ports/Stackfall.sh
  chmod 755 /roms/ports/Stackfall.sh
  write_game_stub /roms/ports/stackfall/stackfall.aarch64
  echo "created by the t017 launcher check; removed by its own cleanup" \
    > "/roms/ports/stackfall/$roms_marker"
  tree_snapshot /roms/ports/stackfall > "$run_dir/roms/tree-before.txt"
  # XDG_DATA_HOME names a directory with no PortMaster, so the discovery
  # falls through to the /roms/ports/PortMaster of the device layout.
  mkdir -p "$run_dir/roms/xdg-empty"
  run_launcher "$run_dir/roms" /roms/ports/Stackfall.sh "$run_dir/roms/xdg-empty"
  tree_snapshot /roms/ports/stackfall > "$run_dir/roms/tree-after.txt"
  # The install is removed when the run ends, so the checker re-reads the
  # pieces it needs from the evidence directory.
  cp /roms/ports/PortMaster/control.txt "$run_dir/roms/control.txt.copy"
  cp /roms/ports/stackfall/log.txt "$run_dir/roms/log.txt" 2>/dev/null || true
fi
if [ -n "$roms_skipped" ]; then
  echo "t017: roms phase unavailable: $roms_skipped"
  echo "$roms_skipped" > "$run_dir/roms-skipped.txt"
fi

# ---------------------------------------------------------------------------
# Phase: the real game binary through the launcher.
# ---------------------------------------------------------------------------
echo
echo "=== phase game ==="
mkdir -p "$run_dir/game/root/roms" "$run_dir/game/root/xdg/PortMaster"
write_control_txt "$run_dir/game/root/xdg/PortMaster" \
  "$run_dir/game/root/roms" x86_64 yes
write_gptokeyb_stub "$run_dir/game/root/xdg/PortMaster"
write_mod_stub "$run_dir/game/root/xdg/PortMaster"
sh "$stager" --binary "$build_dir/stackfall" --arch x86_64 \
  --dest "$run_dir/game/root/roms/ports" > "$run_dir/game/stage.txt" 2>&1 \
  || fail "install-tree.sh failed for the game run"
tree_snapshot "$run_dir/game/root/roms/ports/stackfall" \
  > "$run_dir/game/tree-before.txt"
: > "$run_dir/game/trace.txt"
: > "$run_dir/game/game-env.txt"
env T017_TRACE="$run_dir/game/trace.txt" \
  T017_GAME_ENV="$run_dir/game/game-env.txt" \
  XDG_DATA_HOME="$run_dir/game/root/xdg" \
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  timeout "$timeout_s" \
  "$run_dir/game/root/roms/ports/Stackfall.sh" --seed 20261103 --frames 240 \
  > "$run_dir/game/launcher.out" 2>&1
echo "$?" > "$run_dir/game/status.txt"
sleep 1
tree_snapshot "$run_dir/game/root/roms/ports/stackfall" \
  > "$run_dir/game/tree-after.txt"

# ---------------------------------------------------------------------------
# Phase: the independent checker.
# ---------------------------------------------------------------------------
echo
echo "=== phase check ==="
python3 "$checker" "$run_dir" || fail "the launcher check failed"
echo "t017: pass"
echo "evidence : $run_dir"
