#!/usr/bin/env python3
"""Independent checker for the T017 PortMaster launcher evidence.

Usage: check_t017.py <run-directory>

Reads the launcher, the staging tool output, and every run the driver
produced, and re-derives its expectations from two sources alone: the
current official PortMaster launch template
(https://portmaster.games/packaging.html, reviewed 2026-11-04) and
docs/SPEC.md sections 5, 9, and 10.3. A launcher that satisfies its own
stub run but departs from either source fails here.

Exit status is nonzero when any check fails.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

CHECKS = 0
FAILURES = []


def check(condition, message):
    """Records one expectation. Prints pass or FAIL for it."""
    global CHECKS
    CHECKS += 1
    if condition:
        print(f"pass: {message}")
    else:
        FAILURES.append(message)
        print(f"FAIL: {message}")


def read(path):
    """Returns the text of a file, or an empty string when it is missing."""
    try:
        return Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def norm(path):
    """Collapses repeated slashes.

    The template builds the game folder as "/$directory/ports/...". On a
    device $directory is "roms", so the result is "/roms/ports/..."; in a
    sandbox run the stub names an absolute directory, which leaves a
    harmless double slash after the join. The launcher follows the
    template, so the checker compares normalized paths.
    """
    return re.sub(r"/{2,}", "/", path) if path else path


def lines(path):
    return [line for line in read(path).splitlines() if line.strip()]


# The control-folder candidates of the official template, in order, spelled
# exactly as the template spells them.
TEMPLATE_CONTROLFOLDERS = [
    "/opt/system/Tools/PortMaster/",
    "/opt/tools/PortMaster/",
    "$XDG_DATA_HOME/PortMaster/",
    "/roms/ports/PortMaster",
]


def discovery_block(text):
    """Returns the control-folder discovery block, verbatim."""
    lines = text.splitlines()
    start = next((i for i, line in enumerate(lines)
                  if line.startswith('if [ -d "/opt/system/Tools/PortMaster/" ]')),
                 None)
    if start is None:
        return []
    end = next((i for i in range(start, len(lines)) if lines[i] == "fi"), None)
    return lines[start:end + 1] if end is not None else []


def review_launcher(launcher_text, template_text):
    """Checks the launcher against the template and the specification."""
    print("== launcher review")
    check("#!/bin/bash" in launcher_text.splitlines()[0],
          "the launcher runs under bash, like the template")

    result = subprocess.run(["bash", "-n", str(LAUNCHER)],
                            capture_output=True, text=True)
    check(result.returncode == 0,
          f"bash -n accepts the launcher{'' if result.returncode == 0 else ': ' + result.stderr.strip()}")

    check(template_text.startswith("#!/bin/bash"),
          "the reviewed official template is in the evidence")
    check(discovery_block(launcher_text) == discovery_block(template_text),
          "the discovery block is the official template's, verbatim")

    # Control folder discovery: the template's three tested candidates in
    # order, with the /roms/ports/PortMaster of the device layout as the
    # untested fallback of the else branch.
    tested = re.findall(r'\[ -d "([^"]+)" \]', launcher_text)
    check(tested == TEMPLATE_CONTROLFOLDERS[:3],
          f"the discovery block tests the three template paths in order "
          f"(found {tested})")
    check('controlfolder="/roms/ports/PortMaster"' in launcher_text,
          "the discovery falls back to /roms/ports/PortMaster (template)")

    check('source "$controlfolder/control.txt"' in launcher_text,
          "control.txt is sourced from the control folder")
    check(re.search(r'\[ -f "\$\{controlfolder\}/mod_\$\{CFW_NAME\}\.txt" \]'
                    r'\s*&&\s*source', launcher_text) is not None,
          "the CFW modifier is sourced only when the install ships it")
    check(re.search(r"^get_controls$", launcher_text, re.M) is not None,
          "get_controls is called")

    match = re.search(r'^GAMEDIR="/\$directory/ports/([a-z0-9._-]+)"$',
                      launcher_text, re.M)
    check(match is not None and match.group(1) == "stackfall",
          "the game folder is /$directory/ports/stackfall (section 10.3)")
    check('CONFDIR="$GAMEDIR/conf"' in launcher_text,
          "CONFDIR is the game folder's conf/")

    mkdir_at = launcher_text.find('mkdir -p "$CONFDIR"')
    run_at = launcher_text.find('"./$EXEC" "$@"')
    check(0 <= mkdir_at < run_at,
          "conf/ is created before the game runs")
    check(re.search(r'> "\$GAMEDIR/log\.txt" && exec > >\(tee "\$GAMEDIR/log\.txt"\) '
                    r'2>&1', launcher_text) is not None,
          "each launch replaces log.txt with the script's stdout (template)")

    check('export XDG_DATA_HOME="$CONFDIR"' in launcher_text,
          "XDG_DATA_HOME points at the port's conf/ (section 9)")
    check('export STACKFALL_SAVE_DIR="$CONFDIR"' in launcher_text,
          "the explicit save-directory variable names conf/ (section 9)")
    check('export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"'
          in launcher_text,
          "the port's libs.<arch> folder leads LD_LIBRARY_PATH (template)")
    check('export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"'
          in launcher_text,
          "the mapping get_controls built is exported (section 5)")

    # gptokeyb: exit-hotkey-only, and only when this install uses it.
    gptk_lines = [line for line in launcher_text.splitlines()
                  if "$GPTOKEYB" in line]
    check(len(gptk_lines) == 1,
          f"exactly one line starts gptokeyb (found {gptk_lines})")
    if gptk_lines:
        started = gptk_lines[0].strip()
        check(started == '$GPTOKEYB "$EXEC" &',
              f"gptokeyb runs in exit-hotkey-only mode, with no mapping file: "
              f"{started}")
    guard = re.search(r'if \[ -n "\$\{GPTOKEYB:-\}" \] && '
                      r'\[ -e "\$controlfolder/gptokeyb" \]; then',
                      launcher_text) is not None
    check(guard, "gptokeyb starts only when the installed PortMaster provides it")

    helper_at = launcher_text.find('pm_platform_helper "$GAMEDIR/$EXEC"')
    check(0 <= helper_at < run_at,
          "pm_platform_helper is called on the executable before it runs")
    check(launcher_text.find("pm_finish", run_at) > run_at,
          "pm_finish runs after the game")

    # Nothing in the launcher may feed the game a button mapping or capture
    # the platform's exit chord itself (section 5).
    check(".gptk" not in launcher_text and "-c " not in launcher_text,
          "no gptokeyb mapping file and no option argument appears anywhere")


def review_stage(stage_dir):
    """Checks the staged install against the section 10.3 layout."""
    print("== staged install layout")
    tree = set(lines(stage_dir / "tree.txt"))
    check(tree == {str(stage_dir / "ports"),
                   str(stage_dir / "ports" / "Stackfall.sh"),
                   str(stage_dir / "ports" / "stackfall"),
                   str(stage_dir / "ports" / "stackfall" / "stackfall.x86_64")},
          f"the staged tree is exactly the section 10.3 layout: {sorted(tree)}")
    for path, why in [(stage_dir / "ports" / "Stackfall.sh", "launcher"),
                      (stage_dir / "ports" / "stackfall" / "stackfall.x86_64",
                       "binary")]:
        check(path.is_file() and os.access(path, os.X_OK),
              f"the staged {why} is present and executable")
    check(not (stage_dir / "ports" / "stackfall" / "conf").exists()
          and not (stage_dir / "ports" / "stackfall" / "log.txt").exists(),
          "the staged port creates no conf/ and no log.txt: both are runtime "
          "files (section 10.3)")


def parse_game_env(path):
    """Turns the stub binary's record into a dictionary."""
    env = {"argv": []}
    for line in lines(path):
        if line.startswith("game: argv ["):
            env["argv"].append(line[len("game: argv ["):line.rindex("]")])
        elif line.startswith("game: "):
            rest = line[len("game: "):]
            if "=" in rest:
                key, value = rest.split("=", 1)
                if value.startswith("[") and value.endswith("]"):
                    value = value[1:-1]
                env[key] = value
            else:
                env[rest] = rest
    return env


def trace_events(path):
    """Returns the stub control folder's recorded events, in order."""
    events = []
    for line in lines(path):
        if line.startswith("control.txt sourced: "):
            events.append(("control.txt", line.split(": ", 1)[1]))
        elif line == "get_controls":
            events.append(("get_controls", ""))
        elif line == "mod_ROCKNIX.txt sourced":
            events.append(("mod", ""))
        elif line.startswith("gptokeyb argv:"):
            args = line[len("gptokeyb argv:"):].rsplit(" argc=", 1)[0]
            events.append(("gptokeyb", re.findall(r"\[([^\]]*)\]", args)))
        elif line.startswith("pm_platform_helper: "):
            events.append(("pm_platform_helper", line.split(": ", 1)[1]))
        elif line == "pm_finish":
            events.append(("pm_finish", ""))
        elif line.startswith("pm_message: "):
            events.append(("pm_message", line[len("pm_message: "):]))
    return events


def event_kinds(events):
    return [kind for kind, _ in events]


def review_sandbox_run(name, run_dir, gamedir, controlfolder, arch,
                       expect_gptokeyb=True, expect_game=True,
                       expect_status_zero=True, expect_mod=True,
                       expect_argv=None, extra_log=None,
                       control_txt=None, conf_tree=None, log_file=None):
    """Checks one stub run of the launcher.

    The last three arguments cover a run whose install tree lives outside
    the evidence directory and is removed when the driver ends: the /roms
    run copies its control.txt, its tree listing, and its log.txt in, and
    the checker reads those instead of the live paths.
    """
    print(f"== sandbox run {name}")
    status = read(run_dir / "status.txt").strip()
    if expect_status_zero:
        check(status == "0", f"{name}: the launcher exits 0 (status {status})")
    else:
        check(status not in ("", "0"),
              f"{name}: the launcher fails without a game binary "
              f"(status {status})")

    events = trace_events(run_dir / "trace.txt")
    kinds = event_kinds(events)
    control_sources = [detail for kind, detail in events if kind == "control.txt"]
    check(control_sources == [controlfolder],
          f"{name}: control.txt comes from the discovered folder "
          f"{controlfolder} (got {control_sources})")
    check("get_controls" in kinds, f"{name}: get_controls was called")

    if expect_mod:
        check("mod" in kinds, f"{name}: the CFW modifier was sourced")
    else:
        check("mod" not in kinds,
              f"{name}: no CFW modifier error when the install ships none")

    gptokeyb = [detail for kind, detail in events if kind == "gptokeyb"]
    if expect_gptokeyb:
        check(len(gptokeyb) == 1, f"{name}: gptokeyb started exactly once")
        if len(gptokeyb) == 1:
            argv = gptokeyb[0]
            check(argv == [f"stackfall.{arch}"],
                  f"{name}: gptokeyb runs exit-hotkey-only with just the "
                  f"process name (argv {argv})")
        # The launcher starts gptokeyb in the background, so its trace line
        # can land after pm_platform_helper's; what must hold is that it is
        # running before the port ends. The source order is checked above.
        check("gptokeyb" in kinds and "pm_finish" in kinds
              and kinds.index("gptokeyb") < kinds.index("pm_finish"),
              f"{name}: gptokeyb runs before pm_finish ({kinds})")
    else:
        check(gptokeyb == [], f"{name}: no gptokeyb when the install "
                              f"defines none (events {kinds})")

    if expect_game:
        helpers = [norm(detail) for kind, detail in events
                   if kind == "pm_platform_helper"]
        check(helpers == [f"{norm(gamedir)}/stackfall.{arch}"],
              f"{name}: pm_platform_helper receives the executable path "
              f"(got {helpers})")

    game_env_path = run_dir / "game-env.txt"
    if expect_game:
        env = parse_game_env(game_env_path)
        check(env.get("started") == "started" and env.get("done") == "done",
              f"{name}: the game binary ran to the end")
        check(norm(env.get("cwd")) == norm(gamedir),
              f"{name}: the game runs from its own folder "
              f"(cwd {env.get('cwd')})")
        check(norm(env.get("XDG_DATA_HOME")) == f"{norm(gamedir)}/conf",
              f"{name}: XDG_DATA_HOME is the game's conf/ "
              f"(got {env.get('XDG_DATA_HOME')})")
        check(norm(env.get("STACKFALL_SAVE_DIR")) == f"{norm(gamedir)}/conf",
              f"{name}: the explicit save directory is the game's conf/ "
              f"(got {env.get('STACKFALL_SAVE_DIR')})")
        mapping = re.search(r'^export sdl_controllerconfig="(.*)"$',
                            read(control_txt or Path(controlfolder) / "control.txt"),
                            re.M)
        check(mapping is not None
              and env.get("SDL_GAMECONTROLLERCONFIG") == mapping.group(1),
              f"{name}: the game receives the mapping get_controls built")
        check(norm(env.get("LD_LIBRARY_PATH", ""))
              .startswith(f"{norm(gamedir)}/libs.{arch}:"),
              f"{name}: LD_LIBRARY_PATH leads with the port's libs folder")
        check(env.get("STACKFALL_LOG_FILE", "") == "",
              f"{name}: the launcher leaves the game's own log location alone")
        if expect_argv is None:
            check(env["argv"] == [],
                  f"{name}: PortMaster passes the game no arguments "
                  f"(argv {env['argv']})")
        else:
            check(env["argv"] == expect_argv,
                  f"{name}: the launcher forwards its own arguments "
                  f"(argv {env['argv']})")
        check("pm_platform_helper" in kinds and "pm_finish" in kinds
              and kinds.index("pm_platform_helper") < kinds.index("pm_finish"),
              f"{name}: pm_finish ran after pm_platform_helper ({kinds})")
        if conf_tree is None:
            check((Path(gamedir) / "conf").is_dir(),
                  f"{name}: the launcher created conf/")
        else:
            check(f"{norm(gamedir)}/conf" in set(lines(conf_tree)),
                  f"{name}: the launcher created conf/ (tree {conf_tree.name})")
        log = read(log_file or Path(gamedir) / "log.txt")
        check("stub game stdout: STACKFALL launcher check" in log,
              f"{name}: the game's stdout reached the game folder's log.txt")
        if extra_log is not None:
            check(extra_log in log,
                  f"{name}: {extra_log!r} appears in log.txt")
    else:
        check(not lines(game_env_path),
              f"{name}: the game binary never ran")
        messages = [detail for kind, detail in events if kind == "pm_message"]
        check(any("is missing" in m for m in messages),
              f"{name}: the launcher reports the missing binary "
              f"(messages {messages})")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    global LAUNCHER
    run_dir = Path(sys.argv[1]).resolve()
    repo = Path(__file__).resolve().parent.parent.parent
    launcher = repo / "portmaster" / "Stackfall.sh"
    LAUNCHER = launcher
    launcher_text = read(launcher)
    check(launcher_text != "", f"{launcher} is readable")
    review_launcher(launcher_text, read(run_dir / "official-template.sh"))

    # Every staged launcher is the repository's own, byte for byte.
    for staged in [run_dir / "base" / "root" / "roms" / "ports" / "Stackfall.sh",
                   run_dir / "nomod" / "root" / "roms" / "ports" / "Stackfall.sh",
                   run_dir / "nogptk" / "root" / "roms" / "ports" / "Stackfall.sh",
                   run_dir / "nobinary" / "root" / "roms" / "ports" / "Stackfall.sh",
                   run_dir / "stage" / "ports" / "Stackfall.sh",
                   run_dir / "game" / "root" / "roms" / "ports" / "Stackfall.sh"]:
        check(read(staged) == launcher_text,
              f"{staged.relative_to(run_dir)} is the repository launcher")

    review_stage(run_dir / "stage")

    base_root = run_dir / "base" / "root"
    base_gamedir = base_root / "roms" / "ports" / "stackfall"
    review_sandbox_run("base", run_dir / "base", str(base_gamedir),
                       str(base_root / "xdg" / "PortMaster"), "aarch64")

    before = set(lines(run_dir / "base" / "tree-before.txt"))
    after = set(lines(run_dir / "base" / "tree-after.txt"))
    added = after - before
    check(added == {str(base_gamedir / "conf"),
                    str(base_gamedir / "log.txt")},
          f"base: one launch adds only conf/ and log.txt to the port "
          f"(added {sorted(added)})")
    check(not any(p.endswith("/conf") or p.endswith("log.txt") for p in before),
          "base: the staged port carried no conf/ or log.txt")

    review_sandbox_run("args", run_dir / "args", str(base_gamedir),
                       str(base_root / "xdg" / "PortMaster"), "aarch64",
                       expect_argv=["--frames", "12", "--seed", "7"])

    review_sandbox_run("repeat", run_dir / "repeat", str(base_gamedir),
                       str(base_root / "xdg" / "PortMaster"), "aarch64")
    repeat_log = read(run_dir / "repeat" / "log.txt")
    check(repeat_log.count("stub game stdout: STACKFALL launcher check") == 1,
          "repeat: the second launch replaced log.txt instead of appending")

    nomod_root = run_dir / "nomod" / "root"
    review_sandbox_run("nomod", run_dir / "nomod",
                       str(nomod_root / "roms" / "ports" / "stackfall"),
                       str(nomod_root / "xdg" / "PortMaster"), "aarch64",
                       expect_mod=False)

    nogptk_root = run_dir / "nogptk" / "root"
    review_sandbox_run("nogptk", run_dir / "nogptk",
                       str(nogptk_root / "roms" / "ports" / "stackfall"),
                       str(nogptk_root / "xdg" / "PortMaster"), "aarch64",
                       expect_gptokeyb=False,
                       extra_log="this PortMaster provides no gptokeyb")

    nobinary_root = run_dir / "nobinary" / "root"
    review_sandbox_run("nobinary", run_dir / "nobinary",
                       str(nobinary_root / "roms" / "ports" / "stackfall"),
                       str(nobinary_root / "xdg" / "PortMaster"), "aarch64",
                       expect_game=False, expect_status_zero=False,
                       expect_gptokeyb=False)

    # The literal device paths of section 10.3, when the host allows them.
    if (run_dir / "roms-skipped.txt").exists():
        print(f"unavailable: the /roms/ports run: "
              f"{read(run_dir / 'roms-skipped.txt').strip()}")
    else:
        print("== /roms/ports run")
        review_sandbox_run("roms", run_dir / "roms", "/roms/ports/stackfall",
                           "/roms/ports/PortMaster", "aarch64",
                           control_txt=run_dir / "roms" / "control.txt.copy",
                           conf_tree=run_dir / "roms" / "tree-after.txt",
                           log_file=run_dir / "roms" / "log.txt")
        log = read(run_dir / "roms" / "log.txt")
        check("stub game stdout: STACKFALL launcher check" in log,
              "roms: the game's stdout reached /roms/ports/stackfall/log.txt")

    # The real game binary, staged by the install tool and launched by the
    # script, reports the environment the launcher exported.
    print("== game run")
    game_gamedir = run_dir / "game" / "root" / "roms" / "ports" / "stackfall"
    status = read(run_dir / "game" / "status.txt").strip()
    check(status == "0", f"game: the launcher exits 0 (status {status})")
    events = trace_events(run_dir / "game" / "trace.txt")
    kinds = event_kinds(events)
    check(kinds[:3] == ["control.txt", "mod", "get_controls"]
          and kinds[-1] == "pm_finish"
          and set(kinds[3:-1]) == {"gptokeyb", "pm_platform_helper"},
          f"game: the whole PortMaster sequence ran in order ({kinds})")
    gptokeyb = [detail for kind, detail in events if kind == "gptokeyb"]
    check(gptokeyb == [["stackfall.x86_64"]],
          f"game: gptokeyb runs exit-hotkey-only (argv {gptokeyb})")

    launcher_log = read(game_gamedir / "log.txt")
    check("STACKFALL " in launcher_log,
          "game: the game's banner reached the launcher's log.txt")
    check("Architecture: x86_64" in launcher_log,
          "game: the launched binary is the staged desktop build")
    check("Renderer:" in launcher_log,
          "game: the game initialized a renderer through the launcher")
    own_log_line = re.search(r"^Log: stdout and (.*)$", launcher_log, re.M)
    check(own_log_line is not None
          and norm(own_log_line.group(1))
          == f"{norm(str(game_gamedir))}/conf/log.txt",
          "game: the game's own session log resolves to conf/log.txt "
          f"(got {own_log_line.group(1) if own_log_line else 'no line'})")
    own_log = read(game_gamedir / "conf" / "log.txt")
    check("STACKFALL " in own_log and "Renderer:" in own_log,
          "game: conf/log.txt carries the game's own session log")
    before = set(lines(run_dir / "game" / "tree-before.txt"))
    after = set(lines(run_dir / "game" / "tree-after.txt"))
    added = after - before
    check(added == {str(game_gamedir / "conf"),
                    str(game_gamedir / "conf" / "log.txt"),
                    str(game_gamedir / "log.txt")},
          f"game: the launch adds only conf/ and the two logs "
          f"(added {sorted(added)})")

    print()
    print(f"checks: {CHECKS}, failures: {len(FAILURES)}")
    for message in FAILURES:
        print(f"FAIL: {message}")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
