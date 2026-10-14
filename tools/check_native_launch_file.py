#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Check a network game started from a launch, end to end.

Each game runs as a program of its own on the dummy SDL drivers, over
127.0.0.1, at a DirectPlay port of the check's own (--dplay-port), so the
check runs beside anything else on this machine:

- --launch with an .oalaunch host file opens the battle room on the file's
  map and rules, seats the player and two computer players as the file says;
- -c with the same file, under another name, does the same;
- the same launch piped in on standard input, with no option, does the same;
- the same launch as options alone does the same;
- -c with TA Forever's file opens the battle room on its map and rules;
- a file whose metal is above the slider ends with status 2 and names the
  field, and an option above it names the option;
- --list-content --json prints one JSON object and ends with status 0, and
  does so at once with an idle pipe as its standard input.

The battle room's lines are read from the game's log (--log-dir), which is
where those lines are kept. The password in the file is never printed.
"""
import argparse
import json
import os
from pathlib import Path
import queue
import shlex
import socket
import subprocess
import sys
import tempfile
import threading
import time

# A command that starts the executable under test, such as the compatibility
# layer a cross build runs its tests through; empty runs it directly.
RUNNER = shlex.split(os.environ.get("OA_TEST_RUNNER", ""))
ADDRESS = "127.0.0.1"
# Seconds a game may take to print a line the check waits for, or to end.
LINE_TIMEOUT = 120
# Seconds an idle pipe on standard input may hold a start up: the game waits
# a quarter of a second for its first byte; the rest is the start itself.
IDLE_PIPE_LIMIT = 60
RULES = ("multiplayer: battle room rules: commander ends, metal 1000, energy 1000, "
         "los true, mapping mapped, starts random, cheats off, watchers on, player limit 4")
LAUNCH_RULES = ("multiplayer: battle room rules: commander ends, metal 1000, energy 1000, "
                "units 250, los true, mapping mapped, starts random, cheats off, watchers on, "
                "player limit 4")
MAP_LINE = "multiplayer: battle room map: Acid Foursome"
ROOM_LINE = 'multiplayer: in the battle room of "Example game" as Commander'
SEATS_LINE = ("multiplayer: battle room seats: Commander core team 1 colour 2, "
              "AI:Commander arm team 2 colour 5 hard, AI:Commander core team 1 colour 1 easy")

YAML = """oalaunch: 1
game:
  role: host
  name: Example game
  password: secret
  player-limit: 4
  launcher-name: Ladder
  directplay:
    application-guid: "{99797420-F5F5-11CF-9827-00A0241496C8}"
player:
  name: Commander
  side: core
  team: 1
  colour: 2
map: "Acid Foursome"
rules:
  commander-death: ends
  metal: 1000
  energy: 1000
  unit-limit: 250
  line-of-sight: true
  mapping: mapped
  start-positions: random
  cheats: off
  watchers: on
  time-limit: 0
  lock-options: false
computer-players:
  - side: arm
    team: 2
    colour: 5
    difficulty: hard
  - side: core
    team: 1
    difficulty: easy
"""

# The same launch as options alone.
OPTIONS = [
    "--host", "--game-name", "Example game", "--game-password", "secret",
    "--player-limit", "4", "--launcher-name", "Ladder", "--side", "core", "--team", "1",
    "--colour", "2", "--map", "Acid Foursome", "--commander-death", "ends", "--metal", "1000",
    "--energy", "1000", "--unit-limit", "250", "--line-of-sight", "true", "--mapping", "mapped",
    "--start-positions", "random", "--cheats", "off", "--watchers", "on", "--time-limit", "0",
    "--computer-player", "arm:2:5:hard", "--computer-player", "core:1::easy",
]

INI = """[totala]
watching=2
mapmode=2
location=2
cheats=1
los=1
deathopt=2
metal=1000
energy=1000
lockOptions=0
playerlimit=4
pfs=0
session=Example game
provider=TAForever
mission=Acid Foursome
timelimit=0
registeredonly=0
tournment=1
tournament=1
"""

BAD_YAML = """oalaunch: 1
game:
  role: host
  name: Example game
  password: secret
map: "Acid Foursome"
rules:
  metal: 250000
"""


class LaunchFileFailure(Exception):
    """A behaviour that differs from the expected one."""


def free_port():
    """Returns a UDP port of 127.0.0.1 that nothing holds now."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind((ADDRESS, 0))
        return probe.getsockname()[1]


class Game:
    """One running game. Standard output and, when asked, the log are both read."""

    def __init__(self, native, game_dir, workdir, name, arguments, log_dir=None, piped=None):
        """Starts the game. piped is the text written to its standard input,
        which is then closed; "" leaves the pipe open and idle; None gives
        the game no pipe."""
        self.name = name
        self.lines = []
        self.log_dir = log_dir
        self._queue = queue.Queue()
        self._ended = False
        environment = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy",
                           SDL_RENDER_DRIVER="software")
        self.preferences = workdir / f"{name}.conf"
        self.process = subprocess.Popen(
            [*RUNNER, str(native), "--game-dir", str(game_dir), "--mute",
             "--preferences-file", str(self.preferences), *arguments],
            cwd=workdir, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL if piped is None else subprocess.PIPE,
            text=True, errors="replace")
        if piped:
            self.process.stdin.write(piped)
            self.process.stdin.close()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.process.stdout:
            self._queue.put(line.rstrip("\n"))
        self._queue.put(None)

    def _take(self, timeout):
        """Moves the next line read into self.lines, waiting up to timeout seconds.

        Returns False once the output has ended.
        """
        if self._ended:
            return False
        try:
            line = self._queue.get(timeout=timeout)
        except queue.Empty:
            return True
        if line is None:
            self._ended = True
            return False
        self.lines.append(line)
        return True

    def log_text(self):
        """Returns the text of every log file written so far."""
        if self.log_dir is None or not self.log_dir.is_dir():
            return ""
        parts = []
        for path in sorted(self.log_dir.glob("*.log")):
            parts.append(path.read_text(encoding="utf-8", errors="replace"))
        return "\n".join(parts)

    def wait_for(self, expected):
        """Waits for a line of the game's output; raises LaunchFileFailure when it never comes."""
        deadline = time.monotonic() + LINE_TIMEOUT
        while expected not in self.lines and time.monotonic() < deadline:
            if not self._take(0.5):
                break
        if expected not in self.lines:
            raise LaunchFileFailure(f"{self.name} never printed: {expected}")

    def wait_in_log(self, expected):
        """Waits for a line of the game's log; raises LaunchFileFailure when it never comes.

        Standard output may have been sent to the log. The process staying up
        is not the end of the log, so a closed standard output is not the end
        of the wait.
        """
        deadline = time.monotonic() + LINE_TIMEOUT
        while expected not in self.log_text() and time.monotonic() < deadline:
            if self.process.poll() is not None:
                time.sleep(0.2)
                break
            time.sleep(0.2)
        while self._take(0):
            pass
        if expected not in self.log_text():
            raise LaunchFileFailure(f"{self.name} never logged: {expected}\n{self.log_text()}")

    def wait_status(self, expected):
        """Waits for the game to end; raises LaunchFileFailure unless the status is expected."""
        try:
            status = self.process.wait(timeout=LINE_TIMEOUT)
        except subprocess.TimeoutExpired:
            self.stop()
            raise LaunchFileFailure(f"{self.name} did not end within {LINE_TIMEOUT} s") from None
        while self._take(LINE_TIMEOUT):
            pass
        if status != expected:
            raise LaunchFileFailure(f"{self.name} exited with {status}, expected {expected}")

    def stop(self):
        """Ends the game if it still runs, and closes a pipe left open."""
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        if self.process.stdin is not None and not self.process.stdin.closed:
            self.process.stdin.close()

    def output(self):
        """Returns what the game has printed."""
        return "\n".join(self.lines)


def require_quiet_password(game):
    """Raises LaunchFileFailure when the game printed the file's password."""
    if "secret" in game.output() or "secret" in game.log_text():
        raise LaunchFileFailure(f"{game.name} printed the password")


def battle_room(native, game_dir, workdir, name, launch_arguments, rules=RULES, seats=None,
                piped=None):
    """Starts a host from a launch and waits for its map, rules and seats in the log."""
    port = free_port()
    user = workdir / f"{name}-user"
    user.mkdir()
    logs = workdir / f"{name}-log"
    logs.mkdir()
    game = Game(native, game_dir, workdir, name, [
        "--skip-intro", "--dplay-port", str(port), "--player-name", "Commander",
        "--user-folder", str(user), "--log-dir", str(logs), *launch_arguments,
    ], log_dir=logs, piped=piped)
    expected = [ROOM_LINE, MAP_LINE, rules] + ([seats] if seats else [])
    try:
        for line in expected:
            game.wait_in_log(line)
        require_quiet_password(game)
    finally:
        game.stop()
    require_quiet_password(game)
    for line in expected:
        print(f"{name}: {line}")


def check_bad_file(native, game_dir, workdir):
    """A metal value above the slider ends with status 2 and names the field."""
    path = workdir / "bad.yaml"
    path.write_text(BAD_YAML, encoding="utf-8")
    user = workdir / "bad-user"
    user.mkdir()
    game = Game(native, game_dir, workdir, "bad-file", [
        "--skip-intro", "--launch", str(path), "--user-folder", str(user),
    ])
    try:
        game.wait_status(2)
    finally:
        game.stop()
    text = game.output()
    if "rules.metal" not in text:
        raise LaunchFileFailure("bad-file did not name rules.metal")
    if "secret" in text or "secret" in game.log_text():
        raise LaunchFileFailure("bad-file printed the password")
    named = next(line for line in game.lines if "rules.metal" in line)
    print(f"bad-file: {named}")
    option_user = workdir / "bad-option-user"
    option_user.mkdir()
    option = Game(native, game_dir, workdir, "bad-option", [
        "--skip-intro", "--host", "--metal", "20000", "--user-folder", str(option_user),
    ])
    try:
        option.wait_status(2)
    finally:
        option.stop()
    if "--metal: 20000 is above 10000" not in option.output():
        raise LaunchFileFailure(f"bad-option did not name --metal:\n{option.output()}")
    print("bad-option: --metal: 20000 is above 10000")


def check_list(native, game_dir, workdir):
    """--list-content --json prints the installed maps, mods and expansions.

    Its standard input is a pipe nothing is written to, which the game
    leaves after a short wait rather than holding the start up.
    """
    user = workdir / "list-user"
    user.mkdir()
    started = time.monotonic()
    game = Game(native, game_dir, workdir, "list-content", [
        "--skip-intro", "--list-content", "--json", "--user-folder", str(user),
    ], piped="")
    try:
        game.wait_status(0)
    finally:
        game.stop()
    took = time.monotonic() - started
    if took > IDLE_PIPE_LIMIT:
        raise LaunchFileFailure(f"an idle pipe held the start up for {took:.1f} s")
    listed = None
    for line in game.lines:
        if line.startswith("{") and '"maps"' in line:
            listed = json.loads(line)
            break
    if not isinstance(listed, dict):
        raise LaunchFileFailure("list-content printed no JSON object")
    for key in ("maps", "mods", "expansions"):
        if not isinstance(listed.get(key), list):
            raise LaunchFileFailure(f"list-content JSON has no {key} list")
    foursome = [
        item for item in listed["maps"]
        if item.get("shown-name") == "Acid Foursome" or item.get("key") == "Acid Foursome"
    ]
    if listed["maps"] and not foursome:
        raise LaunchFileFailure("list-content omitted Acid Foursome")
    for item in foursome:
        if item.get("players") != 4 or item.get("size") != "13 x 13":
            raise LaunchFileFailure(
                f"Acid Foursome is {item.get('players')} players, size {item.get('size')!r}"
            )
    shown = foursome[0] if foursome else {"players": None, "size": None}
    print(
        f"list-content: {len(listed['maps'])} maps, {len(listed['mods'])} mods, "
        f"{len(listed['expansions'])} expansions; "
        f"Acid Foursome players {shown.get('players')} size {shown.get('size')!r}"
    )


def check(native, game_dir, workdir):
    """Runs each launch-file case; raises LaunchFileFailure when one differs."""
    launch_path = workdir / "host.oalaunch"
    launch_path.write_text(YAML, encoding="utf-8")
    renamed_path = workdir / "host-settings.ini"
    renamed_path.write_text(YAML, encoding="utf-8")
    ini_path = workdir / "totala.ini"
    ini_path.write_text(INI, encoding="utf-8")
    battle_room(native, game_dir, workdir, "launch-host", ["--launch", str(launch_path)],
                LAUNCH_RULES, SEATS_LINE)
    battle_room(native, game_dir, workdir, "c-launch-host", ["-c", str(renamed_path)],
                LAUNCH_RULES, SEATS_LINE)
    battle_room(native, game_dir, workdir, "piped-host", [], LAUNCH_RULES, SEATS_LINE,
                piped=YAML)
    battle_room(native, game_dir, workdir, "options-host", OPTIONS, LAUNCH_RULES, SEATS_LINE)
    battle_room(native, game_dir, workdir, "ini-host", ["-c", str(ini_path)])
    check_bad_file(native, game_dir, workdir)
    check_list(native, game_dir, workdir)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True, help="open-annihilation")
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--scratch-root", type=Path, required=True)
    args = parser.parse_args()
    args.scratch_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="G104-native-launch-file-",
                                     dir=args.scratch_root.resolve()) as scratch:
        try:
            check(args.native.resolve(), args.game_dir.resolve(), Path(scratch))
        except LaunchFileFailure as failure:
            print(f"launch file check: {failure}", file=sys.stderr)
            return 1
    print("launch file check: --launch, -c, a pipe and the options opened the battle room on "
          "Acid Foursome with the launch's rules and seats, -c read TA Forever's file, a bad "
          "file and a bad option exited 2, and --list-content --json parsed beside an idle pipe")
    return 0


if __name__ == "__main__":
    sys.exit(main())
