"""The script generator of the integrated background validation (T085).

One command writes every scripted session the integrated acceptance pass of
the full twenty-design catalog drives (tools/backgrounds/t085.sh):

  catalog-landscape  the full-catalog ladder walk, landscape half: 4-line
                     fixture cycles walk the level ladder from 1 to 20, and
                     every level contributes a settled frame, a second frame
                     62 ticks later (the motion phase), the first frames of
                     the level's entry transition (three stepped samples for
                     a fade design, the landed frame for a cut), the level-20
                     cap probe, the pause freeze probe, and the restart probe
  catalog-portrait   the same walk through the rotated key table of the
                     portrait orientation (task T055: key left hard drops,
                     Z rotates clockwise, key right carries the menu-down
                     role)
  pair               the short fixed-design session the grayscale and Off
                     comparisons run: one play frame over the tall-stack
                     fixture with a stashed Hold piece
  orientation        the fixed-design continuity probe across repeated
                     orientation turns: play frames between three turns of
                     the ORIENTATION row, one design running unbroken
  rapid-modes        the rapid mode-and-design ring probe on the developer
                     virtual controller: six MODE selects and three DESIGN
                     selects back to back, then the play frame that must
                     show the landed pair
  stress-landscape   the performance workload, landscape half: the same
                     4-line ladder to level 20 and a long dwell there, no
                     checkpoints
  stress-portrait    the portrait twin of the same workload
  det-game           the determinism workload: the ladder to level 20 with
                     a checkpoint after every cycle, replayed under every
                     wallpaper mode
  focus              the X11 focus-loss probe: play frames around a window
                     minimize and restore, for the driver that owns the
                     window manager

Every script is a pure function of its name, so the same generator always
produces the same bytes and a rerun replays the same session.

Usage: catalog_gen.py SCRIPT OUT
"""

import sys

# The catalog's transition and motion tables, restated from
# src/backgrounds.cpp so the generator can shape the capture around each
# level's entry transition and motion: a fade design contributes two extra
# stepped samples, a cut design needs only the landed frame, and a pulse
# design needs a third, wider motion phase before its cycle crosses a whole
# pixel. The values are cross-checked by the runtime suites; the check
# reads the truth from the frames themselves.
FADE_STEPS = {2, 3, 4, 6, 7, 9, 10, 11, 13, 15, 16, 19, 20}
PULSE_DESIGNS = {7, 10, 16, 19}


# Cycles of the 4-line fixture clear that reach a level: level L needs
# 10 * (L - 1) lines, each cycle clears four.
def cycles_for_level(level):
    return (10 * (level - 1) + 3) // 4


class Writer:
    """One scripted session under construction."""

    def __init__(self, title):
        self.lines = []
        self.ticks = 0
        self.comment(title)

    def raw(self, text):
        self.lines.append(text)

    def comment(self, text):
        # The session reader takes 255 bytes per line, so every comment
        # wraps at 76 columns the way the hand-written scripts do.
        words = text.split()
        line = ""
        for word in words:
            if line and len(line) + 1 + len(word) > 76:
                self.lines.append("# %s" % line)
                line = word
            else:
                line = word if not line else line + " " + word
        if line:
            self.lines.append("# %s" % line)

    def press(self, verb, name, count=2):
        """One button or key press: down, `count` ticks, up."""
        self.raw("%s %s down" % (verb, name))
        self.ticks += 1
        self.wait(count)
        self.raw("%s %s up" % (verb, name))
        self.ticks += 1

    def wait(self, count):
        self.raw("wait %d" % count)
        self.ticks += count

    def fixture(self, name):
        self.raw("fixture %s" % name)

    def check(self, name):
        # A wait follows every check, the harness convention: a checkpoint
        # flushes at the end of its own frame, so an input emitted on that
        # same frame would land inside the record it names.
        self.raw("check %s" % name)
        self.raw("wait 1")
        self.ticks += 1

    def write(self, path):
        self.raw("quit")
        with open(path, "w", encoding="ascii") as stream:
            stream.write("\n".join(self.lines) + "\n")
        return self.ticks


# The gameplay keys of one orientation: the rotation key, the hard-drop key,
# the hold key, and the key that carries the menu-down role.
class Table:
    def __init__(self, rotate, drop, hold, menu_down, menu_up, name):
        self.rotate = rotate
        self.drop = drop
        self.hold = hold
        self.menu_down = menu_down
        self.menu_up = menu_up
        self.name = name


LANDSCAPE = Table("x", "space", "c", "down", "up", "landscape")
PORTRAIT = Table("z", "left", "c", "right", "left", "portrait")


def clear_cycle(script, table):
    """One 4-line fixture cycle: rotate the forced I vertical, hard drop."""
    script.fixture("four-line-ready")
    script.wait(2)
    script.press("key", table.rotate, 2)
    script.press("key", table.drop, 2)
    script.wait(10)


def settled_block(script, table, name, lead, phases=2, wide=0):
    """The settled capture `name` over the tall-stack fixture. With
    `phases` 2 the same frame follows `lead` ticks later as the motion
    phase `name`b, the spacing the chapter captures used; `wide` adds one
    further phase `name`c that many ticks after b, the wider window a
    pulse design needs before its cycle crosses a whole pixel."""
    def settle():
        script.fixture("tall-stack-ready")
        script.wait(2)
        script.press("key", table.hold, 2)  # six ticks with the waits

    for suffix in (["", "b", "c"][:phases + (1 if wide else 0)]):
        settle()
        script.check(name + suffix)
        if suffix == "":
            script.wait(lead - 6)
        elif suffix == "b" and wide:
            # The wide window of a pulse design outlives the tall stack at
            # peak gravity, so the fixture reloads inside the wait: the
            # board never tops out and the clock keeps running regardless.
            left = wide - 6
            while left > 0:
                chunk = min(left, 40)
                script.wait(chunk)
                left -= chunk
                if left > 0:
                    script.fixture("tall-stack-ready")
                    script.wait(0)


def gen_catalog(table, path):
    script = Writer(
        "Integrated catalog capture session (docs/backgrounds, task T085), "
        "%s half: the full twenty-design ladder walk with transition, "
        "motion, cap, pause, and restart probes, seed 31337." % table.name)
    script.comment(
        "The round walks the level ladder the only way the rules allow - by "
        "clearing lines. Each four-line-ready fixture forces a vertical I "
        "that clears four rows, so %d cycles reach level 20. Every level "
        "contributes: the first frames of its entry transition (f0 always; "
        "f1 and f2 as the stepped samples of a fade), a settled frame, and "
        "the same frame 62 ticks later. After level 20: two more cycles "
        "prove the level cap, a pause of 96 frames proves the frozen clock, "
        "and a restart proves the Auto round reset."
        % cycles_for_level(20))

    # Level 1: the round's own entry, a cut to the Still ground design.
    script.check("cat-title")
    script.wait(1)
    script.press("key", "enter", 2)
    settled_block(script, table, "cat-01", 62)

    reached = 0
    for level in range(2, 21):
        step = cycles_for_level(level) - reached
        reached = cycles_for_level(level)
        script.comment("level %d: %d more cycle(s), %d lines" %
                       (level, step, 4 * reached))
        for index in range(step):
            if index != step - 1:
                clear_cycle(script, table)
                continue
            # The boundary cycle: the level changes on this hard drop, so
            # the transition samples ride inside it.
            script.fixture("four-line-ready")
            script.wait(2)
            script.press("key", table.rotate, 2)
            script.raw("key %s down" % table.drop)
            script.ticks += 1
            script.wait(2)
            script.check("cat-%02d-f0" % level)
            script.raw("key %s up" % table.drop)
            script.ticks += 1
            if level in FADE_STEPS:
                script.wait(9)
                script.check("cat-%02d-f1" % level)
                script.wait(10)
                script.check("cat-%02d-f2" % level)
                script.wait(26)
            else:
                script.wait(10)
        if level in PULSE_DESIGNS:
            settled_block(script, table, "cat-%02d" % level, 62, wide=186)
        else:
            settled_block(script, table, "cat-%02d" % level, 62)

    # The level cap: 8 more lines stay level 20 and design 20 continues.
    script.comment("the cap: two more cycles, 200 lines, level stays 20")
    clear_cycle(script, table)
    clear_cycle(script, table)
    settled_block(script, table, "cat-20cap", 8, phases=1)

    # The pause freeze: 96 frozen frames, then the resume continuation.
    script.comment("pause: the clock freezes, the overlay holds, the resume "
                   "continues without a jump")
    script.press("key", "esc", 2)
    script.wait(6)
    script.check("cat-pzA")
    script.wait(60)
    script.check("cat-pzB")
    script.wait(30)
    script.press("key", "esc", 2)
    script.wait(2)
    script.check("cat-pzR1")
    script.wait(31)
    script.check("cat-pzR2")

    # The restart: a fresh round cuts the level 1 design in at clock zero.
    script.comment("restart: the Auto round reset cuts design 1 back in")
    script.press("key", "esc", 2)
    script.press("key", table.menu_down, 2)
    script.press("key", "enter", 2)
    script.check("cat-rst1")
    script.wait(54)
    script.check("cat-rst1b")

    total = script.write(path)
    print("catalog-%s: %d ticks scripted" % (table.name, total))
    return 0


def gen_pair(path):
    """The fixed-design play frame the grayscale and Off comparisons share:
    the wallpaper mode and design come from the staged settings file, so the
    same script draws the design under grayscale on, grayscale off, and the
    legacy Off flat."""
    script = Writer(
        "Fixed-design comparison frame (task T085): one play frame over the "
        "tall-stack fixture with a stashed Hold piece. The staged settings "
        "file alone picks the mode, the design, and grayscale, so the frames "
        "this writes differ only where those settings reach.")
    script.check("pair-title")
    script.wait(1)
    script.press("key", "enter", 2)
    settled_block(script, LANDSCAPE, "pair-play", 8, phases=1)
    script.write(path)
    return 0


def gen_orientation(path):
    """The fixed-design continuity probe across three orientation turns.
    Design 20 (the fastest drift) runs in Fixed mode, so its clock is one
    global counter for the whole session: every capture must match the
    package plate at the same solved offset, on either side of a turn."""

    script = Writer(
        "Orientation continuity probe (task T085): design 20 in Fixed mode "
        "across three turns of the ORIENTATION row. The wallpaper clock "
        "never resets, so each capture plate-matches at the one session "
        "offset in whichever orientation the turn left the session in.")

    def leave_round(table):
        """Paused -> Quit to Title -> OPTIONS -> the ORIENTATION row. The
        pause menu holds four items: RESUME, RESTART, CONTROLS, QUIT TO
        TITLE."""
        script.press("key", "esc", 2)
        script.press("key", table.menu_down, 2)  # RESUME -> RESTART
        script.press("key", table.menu_down, 2)  # RESTART -> CONTROLS
        script.press("key", table.menu_down, 2)  # CONTROLS -> QUIT TO TITLE
        script.press("key", "enter", 2)
        # The title cursor sits on START GAME; two menu-downs reach OPTIONS.
        script.press("key", table.menu_down, 2)  # START GAME -> CONTROLS
        script.press("key", table.menu_down, 2)  # CONTROLS -> OPTIONS
        script.press("key", "enter", 2)
        for _ in range(6):  # SOUND .. GRAYSCALE -> ORIENTATION
            script.press("key", table.menu_down, 2)

    def start_round(table):
        """Back out of OPTIONS to the title (the cursor stays on OPTIONS)
        and walk up to START GAME in the table the turn left active."""
        script.press("key", "esc", 2)
        script.press("key", table.menu_up, 2)  # OPTIONS -> CONTROLS
        script.press("key", table.menu_up, 2)  # CONTROLS -> START GAME
        script.press("key", "enter", 2)

    script.comment("start: landscape, level 1, design 20 running")
    script.check("ori-title")
    script.wait(1)
    script.press("key", "enter", 2)
    settled_block(script, LANDSCAPE, "ori-land1", 62)

    script.comment("turn 1: landscape -> portrait")
    leave_round(LANDSCAPE)
    script.press("key", "enter", 2)
    start_round(PORTRAIT)
    settled_block(script, PORTRAIT, "ori-port1", 62)

    script.comment("turns 2 and 3: portrait -> landscape -> portrait")
    leave_round(PORTRAIT)
    script.press("key", "enter", 2)
    script.press("key", "enter", 2)
    start_round(PORTRAIT)
    settled_block(script, PORTRAIT, "ori-port2", 62)

    total = script.write(path)
    print("orientation: %d ticks scripted" % total)
    return 0


def gen_rapid_modes(path):
    """The rapid ring probe on the virtual controller: five MODE selects and
    seventeen DESIGN selects with nothing between them, then the play frame
    that must show the landed pair (Fixed, design 18)."""
    script = Writer(
        "Rapid mode and design ring probe (task T085) on the developer "
        "virtual controller: the MODE ring takes five selects back to back "
        "(Auto -> Off -> Fixed -> Auto -> Off -> Fixed) and the DESIGN ring "
        "seventeen (1 -> 2 -> ... -> 18), one select edge apart, and the "
        "round that follows must draw Fixed design 18 on its first play "
        "frame.")
    script.check("rap-title")
    script.wait(1)
    script.press("key", "enter", 2)
    script.comment("leave the round for the options page")
    script.press("key", "esc", 2)
    script.press("pad", "down", 2)  # RESUME -> RESTART
    script.press("pad", "down", 2)  # RESTART -> CONTROLS
    script.press("pad", "down", 2)  # CONTROLS -> QUIT TO TITLE
    script.press("pad", "a", 2)
    script.comment("title: two D-pad downs reach OPTIONS, A opens it")
    script.press("pad", "down", 2)
    script.press("pad", "down", 2)
    script.press("pad", "a", 2)
    for _ in range(8):  # SOUND .. MUSIC -> BACKGROUNDS
        script.press("pad", "down", 2)
    script.comment("BACKGROUNDS row: A opens the page on MODE")
    script.press("pad", "a", 2)
    for _ in range(5):
        script.press("pad", "a", 1)
    script.comment("five selects landed the ring on Fixed")
    script.press("pad", "down", 2)
    for _ in range(17):
        script.press("pad", "a", 1)
    script.comment("seventeen selects landed the ring on design 18")
    script.press("pad", "b", 2)
    script.press("pad", "b", 2)
    script.comment("title again, cursor on OPTIONS: two ups reach START")
    script.press("pad", "up", 2)
    script.press("pad", "up", 2)
    script.press("pad", "a", 2)
    settled_block(script, LANDSCAPE, "rap-play", 8, phases=1)
    script.write(path)
    return 0


def gen_stress(table, path):
    """The performance workload: the ladder to level 20, then a long dwell
    at the cap. No checkpoints, so the paced runs stay pure work."""
    script = Writer(
        "Background cost workload (task T085), %s half: the 4-line ladder "
        "to level 20 and a long dwell there, seed 31337, no checkpoints." %
        table.name)
    script.press("key", "enter", 2)
    for _ in range(cycles_for_level(20)):
        clear_cycle(script, table)
    script.comment("the dwell: 250 more cycles at the level 20 cap")
    for _ in range(250):
        clear_cycle(script, table)
    total = script.write(path)
    print("stress-%s: %d ticks scripted" % (table.name, total))
    return 0


def gen_det(path):
    """The determinism workload: the ladder walk with a checkpoint after
    every fixture cycle, so the observation stream carries scores, levels,
    boards, and piece queues across every level change. The four wallpaper
    settings replay this one script; the streams must match byte for
    byte."""
    script = Writer(
        "Determinism across wallpaper modes (task T085): the 4-line ladder "
        "to level 20 with a checkpoint after every cycle, seed 20261103. "
        "The same script replays under a fresh save, Off, Auto, and Fixed "
        "17, and the stripped observation streams must agree byte for "
        "byte.")
    script.press("key", "enter", 2)
    total = cycles_for_level(20)
    for index in range(total):
        clear_cycle(script, LANDSCAPE)
        script.check("det-%02d" % (index + 1))
    script.write(path)
    return 0


def gen_focus(path):
    """The X11 focus probe: a fixed design 20 round with checkpoints the
    outer driver minimizes between (real-time pacing, seed 31337)."""
    script = Writer(
        "Focus-loss probe (task T085), driven at real-time pacing under a "
        "window manager: design 20 in Fixed mode, a checkpoint before the "
        "driver minimizes the window, one 120 frames later while the window "
        "holds no focus, and one 30 frames after the restore.")
    script.check("foc-in")
    script.wait(1)
    script.press("key", "enter", 2)
    script.fixture("tall-stack-ready")
    script.wait(2)
    script.press("key", "c", 2)
    script.check("foc-a")
    script.wait(120)
    script.check("foc-b")
    script.wait(30)
    script.check("foc-c")
    script.write(path)
    return 0


GENERATORS = {
    "catalog-landscape": lambda path: gen_catalog(LANDSCAPE, path),
    "catalog-portrait": lambda path: gen_catalog(PORTRAIT, path),
    "pair": gen_pair,
    "orientation": gen_orientation,
    "rapid-modes": gen_rapid_modes,
    "stress-landscape": lambda path: gen_stress(LANDSCAPE, path),
    "stress-portrait": lambda path: gen_stress(PORTRAIT, path),
    "det-game": gen_det,
    "focus": gen_focus,
}


def main(argv):
    if len(argv) != 3 or argv[1] not in GENERATORS:
        print(__doc__)
        return 2
    return GENERATORS[argv[1]](argv[2])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
