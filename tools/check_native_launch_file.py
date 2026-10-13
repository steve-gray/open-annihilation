#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Check a network game started from a launch file, end to end.

Each game runs as a program of its own on the dummy SDL drivers, over
127.0.0.1, at a DirectPlay port of the check's own (--dplay-port), so the
check runs beside anything else on this machine:

- --launch with a host file opens the battle room on the file's map and rules;
- -c with TA Forever's file does the same;
- a file whose metal is above the slider ends with status 2 and names the field;
- --list-content --json prints one JSON object and ends with status 0.

The file starts the host. The battle room's lines are read from the game's
log (--log-dir), which is where those lines are kept. The password in the
file is never printed.
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
RULES = ("multiplayer: battle room rules: commander ends, metal 1000, energy 1000, "
         "los true, mapping mapped, starts random, cheats off, watchers on, player limit 4")
MAP_LINE = "multiplayer: battle room map: Acid Foursome"
ROOM_LINE = 'multiplayer: in the battle room of "Example game" as Commander'

YAML = """oalaunch: 1
game:
  role: host
  address: 192.0.2.10
  name: Example game
  player: Commander
  password: secret
  dplay:
    guid: "{99797420-F5F5-11CF-9827-00A0241496C8}"
    port: 47624
map: "Acid Foursome"
rules:
  commander: ends
  metal: 1000
  energy: 1000
  line-of-sight: true
  mapping: mapped
  start-positions: random
  cheats: false
  watchers: true
  player-limit: 4
  time-limit: 0
lock-options: false
seat:
  side: arm
  team: 1
"""

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

    def __init__(self, native, game_dir, workdir, name, arguments, log_dir=None):
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
            text=True, errors="replace")
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
        """Ends the game if it still runs."""
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()

    def output(self):
        """Returns what the game has printed."""
        return "\n".join(self.lines)


def require_quiet_password(game):
    """Raises LaunchFileFailure when the game printed the file's password."""
    if "secret" in game.output() or "secret" in game.log_text():
        raise LaunchFileFailure(f"{game.name} printed the password")


def battle_room(native, game_dir, workdir, name, launch_arguments):
    """Starts a host from a launch file and waits for its map and rules in the log."""
    port = free_port()
    user = workdir / f"{name}-user"
    user.mkdir()
    logs = workdir / f"{name}-log"
    logs.mkdir()
    game = Game(native, game_dir, workdir, name, [
        "--skip-intro", "--dplay-port", str(port), "--player-name", "Commander",
        "--user-folder", str(user), "--log-dir", str(logs), *launch_arguments,
    ], log_dir=logs)
    try:
        game.wait_in_log(ROOM_LINE)
        game.wait_in_log(MAP_LINE)
        game.wait_in_log(RULES)
        require_quiet_password(game)
    finally:
        game.stop()
    require_quiet_password(game)
    print(f"{name}: {ROOM_LINE}")
    print(f"{name}: {MAP_LINE}")
    print(f"{name}: {RULES}")


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


def check_list(native, game_dir, workdir):
    """--list-content --json prints the installed maps, mods and expansions."""
    user = workdir / "list-user"
    user.mkdir()
    game = Game(native, game_dir, workdir, "list-content", [
        "--skip-intro", "--list-content", "--json", "--user-folder", str(user),
    ])
    try:
        game.wait_status(0)
    finally:
        game.stop()
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
    yaml_path = workdir / "launch.yaml"
    yaml_path.write_text(YAML, encoding="utf-8")
    ini_path = workdir / "totala.ini"
    ini_path.write_text(INI, encoding="utf-8")
    battle_room(native, game_dir, workdir, "yaml-host", ["--launch", str(yaml_path)])
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
    with tempfile.TemporaryDirectory(prefix="G52-native-launch-file-",
                                     dir=args.scratch_root.resolve()) as scratch:
        try:
            check(args.native.resolve(), args.game_dir.resolve(), Path(scratch))
        except LaunchFileFailure as failure:
            print(f"launch file check: {failure}", file=sys.stderr)
            return 1
    print("launch file check: --launch and -c opened the battle room on Acid Foursome with the "
          "file's rules, a bad file exited 2, and --list-content --json parsed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
