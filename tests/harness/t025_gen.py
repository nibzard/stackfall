#!/usr/bin/env python3
"""Generate the scripted sessions of the T025 stability pass.

The game binary's script language is the one of docs/AGENT_TESTING.md, T023:
key/pad presses through the real SDL event path, tick waits, frame stalls,
restarts, checkpoints, and a clean quit. This generator writes the three
sessions T025 needs into the evidence directory, because they are far longer
than a hand-written script:

  cycles N OUT   N restart and menu cycles in one process: pause-menu
                 restart, quit-to-title restart, game-over play-again, and
                 game-over title restart, with mute toggles and movement
                 probes before, between, and after. Run at real-time pacing.
  longplay OUT   The ten-minute session: a dozen full games from title
                 starts (each round draws a fresh seed from the session
                 stream), mixed play with DAS holds, rotations, hold, soft
                 and hard drops, pause and resume, two injected frame
                 stalls, checkpoints with screenshots, and the quit path.
                 Run at real-time pacing; the waits sum to about 36,000
                 ticks, ten minutes at 60 Hz.
  saves OUT      One title-to-game-over round of hard drops, then quit
                 through the title. Every game over that beats the stored
                 record writes the save file, which is what the hard-kill
                 and process-restart passes target.

Usage: t025_gen.py MODE ARGS OUT
"""

import sys

# The tick cost of one button edge plus its settling wait.
TAP = ["wait 2"]


class Script:
    """One scripted session under construction."""

    def __init__(self, title):
        self.lines = ["# %s" % title]
        self.ticks = 0  # waits seen so far, at 60 Hz one tick is 1/60 s

    def say(self, text):
        self.lines.append("# " + text)

    def raw(self, line):
        self.lines.append(line)

    def key(self, name, state="down", wait=2):
        self.lines.append("key %s %s" % (name, state))
        self.wait(wait)

    def tap(self, name, wait=2):
        self.key(name, "down", wait)
        self.key(name, "up", wait)

    def wait(self, ticks):
        self.lines.append("wait %d" % ticks)
        self.ticks += ticks

    def stall(self, ms):
        self.lines.append("stall %d" % ms)

    def check(self, name):
        # A check flushes at the end of its frame, and pump() may already
        # have queued the next input into that same frame (the T023 rule:
        # put a wait between a check and the next input when the record must
        # not include that input's tick). Every check below is followed by
        # one guard tick.
        self.lines.append("check %s" % name)
        self.wait(1)

    def marker(self, text):
        self.lines.append("echo %s" % text)

    def write(self, path):
        with open(path, "w", encoding="ascii") as stream:
            stream.write("\n".join(self.lines) + "\n")
        return self.ticks


def hard_drop_spam(script, drops):
    """Hard-drop spam: locks piece after piece until the round tops out."""
    for _ in range(drops):
        script.key("space", "down", 2)
        script.key("space", "up", 3)


def movement_probe(script, name):
    """Prove horizontal input still moves the piece: tap left at a fresh
    spawn column and checkpoint before and after. The checker compares the
    active piece x of the two records."""
    script.check("%s-before" % name)
    script.tap("left", 2)
    script.wait(4)
    script.check("%s-after" % name)


def pause_restart(script, tag):
    """Cycle through the pause menu's Restart item."""
    script.tap("esc", 2)
    script.tap("down", 2)   # Resume -> Restart
    script.check("%s-paused" % tag)
    script.tap("enter", 2)  # restarts and resumes
    script.wait(20)
    script.check("%s-restarted" % tag)


def quit_to_title_start(script, tag):
    """Leave the round through the pause menu, then start a fresh one."""
    script.tap("esc", 2)
    script.tap("down", 2)
    script.tap("down", 2)
    script.tap("down", 2)   # Resume -> Restart -> Controls -> Quit to Title
    script.check("%s-paused" % tag)
    script.tap("enter", 2)
    script.wait(10)
    script.check("%s-title" % tag)
    script.tap("enter", 2)  # Start Game
    script.wait(20)
    script.check("%s-playing" % tag)


def game_over_play_again(script, tag):
    """Top out, then take Play Again on the game-over menu."""
    hard_drop_spam(script, 90)
    script.wait(10)
    script.check("%s-gameover" % tag)
    script.tap("enter", 2)  # Play Again
    script.wait(20)
    script.check("%s-again" % tag)


def game_over_title_start(script, tag):
    """Top out, return to the title, and start the next round there."""
    hard_drop_spam(script, 90)
    script.wait(10)
    script.check("%s-gameover" % tag)
    script.tap("down", 2)   # Play Again -> Title
    script.tap("enter", 2)
    script.wait(10)
    script.check("%s-title" % tag)
    script.tap("enter", 2)  # Start Game
    script.wait(20)
    script.check("%s-playing" % tag)


def quit_from_title(script):
    """Title -> Quit -> confirm: the clean menu exit."""
    script.wait(5)
    script.check("final-title")
    script.tap("down", 2)   # Start Game -> Controls
    script.tap("down", 2)   # Controls -> Options
    script.tap("down", 2)   # Options -> Quit
    script.check("final-at-quit")
    script.wait(1)
    script.check("quit-confirmed")
    script.tap("enter", 1)


def gen_cycles(count, path):
    script = Script("T025 restart and menu cycles (%d cycles, real-time "
                    "pacing)" % count)
    script.say("Generated by tests/harness/t025_gen.py; do not edit.")
    script.check("start-title")
    script.wait(10)
    script.tap("enter", 2)  # Start Game
    script.wait(30)
    movement_probe(script, "probe0")
    kinds = ["pause-restart", "pause-restart", "title-restart",
             "play-again", "title-restart", "play-again"]
    for index in range(count):
        kind = kinds[index % len(kinds)]
        script.marker("cycle-%d-%s" % (index + 1, kind))
        if kind == "pause-restart":
            pause_restart(script, "c%d" % (index + 1))
        elif kind == "title-restart":
            quit_to_title_start(script, "c%d" % (index + 1))
        elif kind == "play-again":
            game_over_play_again(script, "c%d" % (index + 1))
        if index % 9 == 8:
            script.say("sound toggle inside the cycles")
            script.tap("m", 2)
        if index % 6 == 5:
            movement_probe(script, "probe%d" % (index + 1))
        script.wait(15)
    movement_probe(script, "probe-final")
    script.say("back to the title and out through the menu")
    script.tap("esc", 2)
    script.tap("down", 2)
    script.tap("down", 2)
    script.tap("enter", 2)  # Quit to Title
    quit_from_title(script)
    ticks = script.write(path)
    print("cycles: %d cycles, %d ticks (%.1f s at 60 Hz)"
          % (count, ticks, ticks / 60.0))
    return 0


def play_segment(script, tag, length):
    """One stretch of mixed play: taps, a DAS hold, rotations, hold, soft
    drop, and hard drops, ending mid-round."""
    script.marker("play-%s" % tag)
    script.say("mixed input: taps, a DAS hold, rotations, hold, soft drop")
    script.tap("left", 2)
    script.tap("left", 2)
    script.tap("right", 2)
    script.key("left", "down", 2)
    script.wait(24)  # DAS/ARR hold across many ticks
    script.key("left", "up", 2)
    script.tap("x", 2)   # rotate clockwise
    script.tap("z", 2)   # rotate counter-clockwise
    script.tap("c", 2)   # hold
    script.key("down", "down", 2)
    script.wait(20)      # soft drop under gravity
    script.key("down", "up", 2)
    script.tap("z", 2)
    for _ in range(length):
        script.key("space", "down", 2)
        script.key("space", "up", 3)
    script.wait(10)
    script.check("playing-%s" % tag)


def gen_longplay(path, target_ticks=36000):
    # The board-watching wait of each game is the free variable: build once,
    # read the fixed cost off the result, solve for the wait that lands the
    # total on the ten-minute target, and build again.
    #
    # Every game ends at a game over before any menu navigation, because a
    # round can top out early: hard drops stack the center columns until
    # they block the spawn area, so the mixed play alone may end the round.
    # Navigating from a known scene keeps the menu presses meaningful
    # whatever the seed does.
    games = 12

    def build(watch):
        script = Script("T025 ten-minute session (real-time pacing, several "
                        "seeds, two injected stalls)")
        script.say("Generated by tests/harness/t025_gen.py; do not edit.")
        script.say("Every title start draws the next seed of the session "
                   "stream, so the six title rounds cover six fresh seeds.")
        script.check("open-title")
        script.wait(30)
        script.tap("enter", 2)  # Start Game
        script.wait(30)
        movement_probe(script, "probe0")

        for index in range(games):
            tag = "g%02d" % (index + 1)
            script.marker("game-%s" % tag)
            # The pause, the mute toggle, and the stalls run at the top of
            # the round, while the board is fresh and the scene is playing.
            script.wait(30)
            if index % 3 == 0:
                script.tap("esc", 2)
                script.wait(60)
                script.check("paused-%s" % tag)
                script.tap("enter", 2)  # Resume
                script.wait(30)
            if index % 4 == 3:
                script.tap("m", 2)  # sound toggle
            # Both stalls run at the top of the round, on the fresh board:
            # the segment's own hard drops can stack the center columns
            # into the spawn area and end the round, and the stalls must
            # hit an active simulation.
            if index == 2:
                script.say("the long frame stall: one second frozen, then "
                           "the clamp and the catch-up batch")
                script.stall(1200)
                script.wait(30)
                script.check("after-stall-%s" % tag)
            if index == 6:
                script.say("a shorter stall inside the warning band: logged "
                           "as unusual, caught up without the clamp")
                script.stall(120)
                script.wait(30)
                script.check("after-stall-%s" % tag)
            play_segment(script, tag, 8 + (index % 5))
            # Watch the board for the remainder of this game's slice. This
            # wait runs while the round is still live: unattended gravity
            # stacks the spawn column and may end the round by itself,
            # which is exactly the long unattended stretch T025 wants. It
            # must run before the scripted game-over navigation, so every
            # game's mixed input and both stalls land on an active round.
            script.wait(watch)
            hard_drop_spam(script, 90)
            script.wait(10)
            script.check("gameover-%s" % tag)
            if index == games - 1:
                # The last round leaves through the title; quit_from_title
                # below records the final-title checkpoint and quits.
                script.tap("down", 2)   # Play Again -> Title
                script.tap("enter", 2)
                script.wait(10)
            elif index % 2 == 0:
                # Continue through Play Again: same stream, next round.
                script.tap("enter", 2)
                script.wait(20)
                script.check("again-%s" % tag)
            else:
                # Return to the title and start the next round there, which
                # draws a fresh seed the log names.
                script.tap("down", 2)   # Play Again -> Title
                script.tap("enter", 2)
                script.wait(10)
                script.check("title-%s" % tag)
                script.tap("enter", 2)  # Start Game
                script.wait(20)
                script.check("playing-next-%s" % tag)
        quit_from_title(script)
        return script

    probe = build(90)
    fixed = probe.ticks - games * 90
    watch = max(30, (target_ticks - fixed) // games)
    script = build(watch)
    ticks = script.write(path)
    print("longplay: %d ticks (%.1f s at 60 Hz), target %d, watch wait %d"
          % (ticks, ticks / 60.0, target_ticks, watch))
    if ticks < target_ticks:
        print("longplay: WARNING script ends %.1f s early"
              % ((target_ticks - ticks) / 60.0))
    return 0


def gen_saves(path):
    script = Script("T025 save-writing round (title to game over, then "
                    "quit through the title)")
    script.say("Generated by tests/harness/t025_gen.py; do not edit.")
    script.check("open-title")
    script.wait(10)
    script.tap("enter", 2)
    script.wait(20)
    script.say("hard-drop spam until the round tops out; the game over "
               "writes the record whenever it beats the stored one")
    hard_drop_spam(script, 90)
    script.wait(10)
    script.check("gameover")
    script.tap("down", 2)   # Play Again -> Title
    script.tap("enter", 2)
    quit_from_title(script)
    ticks = script.write(path)
    print("saves: %d ticks (%.1f s at 60 Hz)" % (ticks, ticks / 60.0))
    return 0


def main():
    if len(sys.argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    mode, out = sys.argv[1], sys.argv[-1]
    if mode == "cycles":
        return gen_cycles(int(sys.argv[2]), out)
    if mode == "longplay":
        return gen_longplay(out)
    if mode == "saves":
        return gen_saves(out)
    sys.stderr.write("t025_gen: unknown mode %s\n" % mode)
    return 2


if __name__ == "__main__":
    sys.exit(main())
