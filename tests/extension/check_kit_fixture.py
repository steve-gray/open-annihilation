#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Check the Kit fixture: an extension's Settings page, kit screen, prompt and match overlay.

Runs an open-annihilation built with the test extensions
(-DOA_RECORD_EXTENSION_HOOKS=ON), whose Kit fixture (kit_fixture.cpp) uses
the OA UI kit through the extension table alone, over the installation
OA_GAME_DIR names. In a 640x480, a 1280x720 and a 1920x1080 window, each
started on the dummy drivers with --fark, it drives the game through the
automation endpoint:

1. The OA button opens the Library and its SETTINGS... Settings, whose nav
   lists the fixture's page (oa.settings.nav.kit-fixture.options) below
   Developer's entry. Shown, the page's controls lie inside the window and
   no two overlap, but a switch and its halves.
2. Tab from the page's nav entry focuses the page's controls in the order
   the page declares them: the show-chip toggle, then the mode choice.
3. A click on the toggle's Off half and on OK stores kit-fixture.show-chip
   as 0: the game prints "kit fixture: applied kit-fixture.show-chip=0" and
   prefs gives 0.
4. Started with --kit-fixture-screen, the kit screen shows: screen's dialogs
   hold oa.kit-fixture.screen, its controls lie inside the window, Tab
   focuses its three rows and CLOSE in order, and CLOSE closes it, the game
   printing "kit fixture: screen closed".
5. Started with --kit-fixture-prompt, the prompt lists oa.prompt.yes, and a
   click on it prints "kit fixture: answered yes".
6. Headless, with --match-ticks 30, --snapshot and kit-fixture.show-chip 1 in
   its preferences file, the match's frame shows the chip the fixture prints:
   "kit fixture: chip at <x>,<y> <width>x<height> outline <rrggbb>" gives
   the chip's rectangle in the overlay's pixels, whose origin is the
   battlefield's corner. The frame shows the battlefield's layer at a whole
   magnification (1 at 640x480, 2 on the larger windows): placed at the
   battlefield's corner and magnified so, every pixel of that rectangle's
   edge holds the outline colour, and no other place in the frame could be
   it. With 0 stored (at 640x480 only) the fixture draws no chip and prints
   no such line.

When OA_GAME_DIR is unset, empty or names no directory it exits with
--skip-code (OA_GAME_DATA_SKIP_CODE), which ctest reports as skipped, or
fails when OA_REQUIRE_GAME_DATA=1. Each run makes its own folder under
--scratch-root and removes it, so the check passes run after run.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import check_native_automation as automation  # noqa: E402
import check_native_automation_settings as settings  # noqa: E402

# The window sizes the check runs the game at.
RESOLUTIONS = ("640x480", "1280x720", "1920x1080")
# The fixture's Settings page: its nav entry, the prefix of its controls'
# names and Developer's entry, which it is listed under.
PAGE_NAV = "oa.settings.nav.kit-fixture.options"
PAGE_PREFIX = "oa.settings.kit-fixture.options."
DEVELOPER_NAV = "oa.settings.nav.developer"
SHOW_CHIP_OFF = PAGE_PREFIX + "show-chip.off"
# The page's rows as Tab focuses them, in the order the page declares them.
PAGE_TAB_ORDER = (PAGE_PREFIX + "show-chip", PAGE_PREFIX + "mode")
SHOW_CHIP_KEY = "kit-fixture.show-chip"
# The kit screen's automation dialog and its controls in their tab order.
SCREEN_DIALOG = "oa.kit-fixture.screen"
SCREEN_TAB_ORDER = ("oa.kit-fixture.screen.first", "oa.kit-fixture.screen.second",
                    "oa.kit-fixture.screen.third", "oa.kit-fixture.screen.close")
SCREEN_CLOSE = "oa.kit-fixture.screen.close"
PROMPT_YES = "oa.prompt.yes"
# Tab's scancode and key code, as a keyboard sends them.
TAB_SCANCODE = 43
TAB_KEYCODE = 0x09
# What the fixture prints.
APPLIED_LINE = "kit fixture: applied kit-fixture.show-chip=0"
CLOSED_LINE = "kit fixture: screen closed"
ANSWERED_LINE = "kit fixture: answered yes"
CHIP_RE = re.compile(r"kit fixture: chip at (-?\d+),(-?\d+) (\d+)x(\d+) outline ([0-9a-f]{6})")
# The headless match's ticks, and its wall-time limit on a loaded machine.
MATCH_TICKS = "30"
HEADLESS_TIMEOUT = 900
# The whole magnifications at which the frame may show the battlefield's layer.
MAGNIFICATIONS = (1, 2, 3, 4)
# A preferences file that stores show-chip.
PREFERENCES = 'open-annihilation-preferences 1\n"kit-fixture.show-chip" "{value}"\n'


def wait_for(what, found):
    """Waits until found() answers something true and returns it, or fails naming what."""
    deadline = time.monotonic() + automation.TIMEOUT
    while True:
        answer = found()
        if answer:
            return answer
        if time.monotonic() > deadline:
            raise automation.AutomationFailure(f"{what} never came")
        time.sleep(0.1)


def wait_line(game, line):
    """Waits for the game to print a line holding a text."""
    def printed():
        game.take_output()
        return any(line in text for text in game.lines)
    wait_for(f"the line {line!r}", printed)


def press_tab(client, screen):
    """Presses and releases Tab on the screen named, once the game has taken it."""
    events = [{"kind": kind, "key": "Tab", "scancode": TAB_SCANCODE, "keycode": TAB_KEYCODE}
              for kind in ("key_down", "key_up")]
    answer = client.request("input", expect_screen=screen, events=events)
    if not answer.get("ok"):
        raise automation.AutomationFailure(f"Tab was answered {answer}")


def focused(client):
    """Returns the name of the control that holds the focus, or None."""
    return client.request("screen").get("focused")


def tab_through(client, order):
    """Presses Tab once for each name, requiring each to take the focus in turn."""
    for name in order:
        press_tab(client, "main_menu")
        deadline = time.monotonic() + automation.TIMEOUT
        holder = focused(client)
        while holder != name:
            if time.monotonic() > deadline:
                raise automation.AutomationFailure(f"Tab put the focus on {holder}, not {name}")
            time.sleep(0.1)
            holder = focused(client)


def inside(rect, window):
    """Tells whether a window rectangle has pixels and lies inside the window."""
    x, y, width, height = rect
    return width > 0 and height > 0 and x >= 0 and y >= 0 and \
        x + width <= window["width"] and y + height <= window["height"]


def overlap(first, second):
    """Tells whether two rectangles share a pixel."""
    return first[0] < second[0] + second[2] and second[0] < first[0] + first[2] and \
        first[1] < second[1] + second[3] and second[1] < first[1] + first[3]


def part_of(name, other):
    """Tells whether one control is a part of another, such as a switch's Off half of the switch."""
    return name.startswith(other + ".") or other.startswith(name + ".")


def check_laid_out(controls, window, what):
    """Requires each control inside the window, and no two of them overlapping but a control and its parts."""
    if not controls:
        raise automation.AutomationFailure(f"{what} lists no control")
    for control in controls:
        if not inside(control.get("window_rect") or (0, 0, 0, 0), window):
            raise automation.AutomationFailure(
                f"{control.get('name')} lies at {control.get('window_rect')}, outside the "
                f"{window['width']}x{window['height']} window")
    for index, control in enumerate(controls):
        for other in controls[index + 1:]:
            if part_of(control["name"], other["name"]):
                continue
            if overlap(control["window_rect"], other["window_rect"]):
                raise automation.AutomationFailure(
                    f"{control['name']} at {control['window_rect']} overlaps {other['name']} "
                    f"at {other['window_rect']}")


class Session:
    """One windowed run of the game, driven through its endpoint."""

    def __init__(self, native, game_dir, workdir, resolution, arguments=()):
        self.resolution = resolution
        self.game = automation.Game(native, game_dir, workdir, ["--resolution", resolution, *arguments])
        self.client = None

    def __enter__(self):
        try:
            endpoint = self.game.endpoint()
            address, token = endpoint.get("address", ""), endpoint.get("token", "")
            if not address.startswith("127.0.0.1:") or len(token) != 32:
                raise automation.AutomationFailure(f"the endpoint file holds {endpoint}")
            self.client = automation.Client(address)
            hello = self.client.request("hello", versions=[1], client="kit fixture", token=token)
            if not hello.get("ok"):
                raise automation.AutomationFailure(f"hello was answered {hello}")
            self.window = hello.get("window", {})
            automation.wait_screen(self.client, "main_menu")
        except BaseException:
            self.__exit__(*sys.exc_info())
            raise
        return self

    def finish(self):
        """Asks the game to quit and waits for it to end with status 0."""
        self.client.request("quit")
        self.game.wait_end()

    def __exit__(self, kind, failure, trace):
        if failure is not None:
            self.game.stop()
            self.game.take_output()
            print("\n".join(f"game: {line}" for line in self.game.lines))
        if self.client is not None:
            self.client.close()
        return False


def check_page(native, game_dir, workdir, resolution):
    """Steps 1 to 3: the Settings page, its tab order and OK's write."""
    with Session(native, game_dir, workdir, resolution) as session:
        client, window = session.client, session.window
        settings.open_settings(client)
        controls = settings.listed(client)
        named = {control.get("name"): control for control in controls}
        if PAGE_NAV not in named or DEVELOPER_NAV not in named:
            raise automation.AutomationFailure(f"Settings' nav does not list the page and Developer: {controls}")
        if named[PAGE_NAV]["window_rect"][1] <= named[DEVELOPER_NAV]["window_rect"][1]:
            raise automation.AutomationFailure(
                f"the page's entry at {named[PAGE_NAV]['window_rect']} is not below Developer's "
                f"at {named[DEVELOPER_NAV]['window_rect']}")
        settings.click(client, PAGE_NAV)
        settings.wait_control(client, PAGE_NAV, lambda control: control.get("checked"))
        page = [control for control in settings.listed(client)
                if control.get("name", "").startswith(PAGE_PREFIX)]
        check_laid_out(page, window, "the page")
        tab_through(client, PAGE_TAB_ORDER)
        settings.click(client, SHOW_CHIP_OFF)
        settings.wait_control(client, SHOW_CHIP_OFF, lambda control: control.get("checked"))
        settings.click(client, "oa.settings.ok")
        settings.wait_gone(client, "oa.settings.ok")
        wait_line(session.game, APPLIED_LINE)
        settings.wait_preference(client, SHOW_CHIP_KEY, "0")
        settings.close_library(client)
        session.finish()


def check_screen(native, game_dir, workdir, resolution):
    """Step 4: the kit screen, its tab order and CLOSE."""
    with Session(native, game_dir, workdir, resolution, ["--kit-fixture-screen"]) as session:
        client, window = session.client, session.window
        wait_for(f"the dialog {SCREEN_DIALOG}",
                 lambda: SCREEN_DIALOG in client.request("screen").get("dialogs", []))
        controls = [control for control in settings.listed(client)
                    if control.get("dialog") == SCREEN_DIALOG]
        names = [control.get("name") for control in controls]
        if sorted(names) != sorted(SCREEN_TAB_ORDER):
            raise automation.AutomationFailure(f"the screen lists {names}, not {list(SCREEN_TAB_ORDER)}")
        check_laid_out(controls, window, "the screen")
        tab_through(client, SCREEN_TAB_ORDER)
        settings.click(client, SCREEN_CLOSE)
        wait_line(session.game, CLOSED_LINE)
        wait_for(f"{SCREEN_DIALOG} to close",
                 lambda: SCREEN_DIALOG not in client.request("screen").get("dialogs", []))
        session.finish()


def check_prompt(native, game_dir, workdir, resolution):
    """Step 5: the prompt and its answer."""
    with Session(native, game_dir, workdir, resolution, ["--kit-fixture-prompt"]) as session:
        client = session.client
        settings.wait_control(client, PROMPT_YES, lambda control: control.get("enabled"))
        settings.click(client, PROMPT_YES)
        wait_line(session.game, ANSWERED_LINE)
        settings.wait_gone(client, PROMPT_YES)
        session.finish()


def read_ppm(path):
    """Returns a binary PPM's width, height and RGB bytes."""
    data = path.read_bytes()
    fields = []
    at = 0
    while len(fields) < 4:
        while data[at:at + 1].isspace():
            at += 1
        if data[at:at + 1] == b"#":
            at = data.index(b"\n", at) + 1
            continue
        end = at
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[at:end])
        at = end
    if fields[0] != b"P6" or fields[3] != b"255":
        raise automation.AutomationFailure(f"{path.name} is not an 8-bit binary PPM")
    width, height = int(fields[1]), int(fields[2])
    pixels = data[at + 1:]
    if len(pixels) < width * height * 3:
        raise automation.AutomationFailure(f"{path.name} is cut short")
    return width, height, pixels


def edge_holds(pixels, width, left, top, rect_width, rect_height, colour):
    """Tells whether every pixel on a rectangle's edge holds a colour."""
    def at(x, y):
        offset = (y * width + x) * 3
        return pixels[offset:offset + 3]
    right, bottom = left + rect_width - 1, top + rect_height - 1
    return all(at(x, top) == colour and at(x, bottom) == colour for x in range(left, right + 1)) and \
        all(at(left, y) == colour and at(right, y) == colour for y in range(top, bottom + 1))


def chip_places(pixels, width, height, rect_width, rect_height, colour):
    """Returns every frame pixel at which a rectangle of a size has its whole edge in a colour."""
    run = colour * rect_width
    places = []
    for top in range(height - rect_height + 1):
        row = pixels[top * width * 3:(top + 1) * width * 3]
        start = row.find(run)
        while start >= 0:
            if start % 3 == 0 and edge_holds(pixels, width, start // 3, top, rect_width, rect_height, colour):
                places.append((start // 3, top))
            start = row.find(run, start + 1)
    return places


def run_headless(native, game_dir, workdir, resolution, show_chip):
    """Runs a headless match with show-chip stored; returns its output and its frame's path."""
    preferences = workdir / f"headless-{show_chip}.conf"
    preferences.write_text(PREFERENCES.format(value=show_chip), encoding="utf-8")
    snapshot = workdir / f"headless-{show_chip}.ppm"
    environment = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy")
    result = subprocess.run(
        [*automation.RUNNER, str(native), "--game-dir", str(game_dir), "--skip-intro", "--mute",
         "--headless-check", "--resolution", resolution, "--match-ticks", MATCH_TICKS,
         "--snapshot", str(snapshot), "--preferences-file", str(preferences)],
        cwd=workdir, env=environment, capture_output=True, text=True, errors="replace",
        timeout=HEADLESS_TIMEOUT, check=False)
    output = result.stdout + result.stderr
    if result.returncode != 0:
        tail = "\n".join(output.splitlines()[-20:])
        raise automation.AutomationFailure(f"the headless match exited with {result.returncode}:\n{tail}")
    return output, snapshot


def check_chip(native, game_dir, workdir, resolution, check_off):
    """Step 6: the chip over a headless match's battlefield, where the fixture says it is."""
    output, snapshot = run_headless(native, game_dir, workdir, resolution, "1")
    printed = CHIP_RE.findall(output)
    if len(printed) != 1:
        raise automation.AutomationFailure(f"the fixture printed {len(printed)} chip lines, not one")
    x, y, rect_width, rect_height = (int(value) for value in printed[0][:4])
    colour = bytes.fromhex(printed[0][4])
    width, height, pixels = read_ppm(snapshot)
    # The overlay's pixels start at the battlefield's corner, right of the
    # side column and under the top bar, and the frame shows the
    # battlefield's layer at a whole magnification (2 on the larger windows),
    # so the chip lies at the corner plus its printed place times that.
    found = []
    for magnification in MAGNIFICATIONS:
        for left, top in chip_places(pixels, width, height, rect_width * magnification,
                                     rect_height * magnification, colour):
            corner = (left - x * magnification, top - y * magnification)
            if corner[0] >= 0 and corner[1] >= 0:
                found.append((magnification, corner))
    if len(found) != 1:
        raise automation.AutomationFailure(
            f"the frame holds {len(found)} places edged in #{printed[0][4]} that the printed "
            f"chip at {x},{y} {rect_width}x{rect_height} could be, not one: {found}")
    magnification, corner = found[0]
    print(f"extension-kit: {resolution}: the chip at {x},{y} {rect_width}x{rect_height} is edged in "
          f"#{printed[0][4]} in the {width}x{height} frame, from the battlefield's corner at "
          f"{corner[0]},{corner[1]} at {magnification}x")
    if check_off:
        output, _ = run_headless(native, game_dir, workdir, resolution, "0")
        if CHIP_RE.search(output):
            raise automation.AutomationFailure("the fixture drew its chip with show-chip stored as 0")


def check_at(native, game_dir, scratch_root, resolution):
    """Runs every step in a window of one size; raises AutomationFailure when one differs."""
    steps = (("the page", check_page), ("the screen", check_screen), ("the prompt", check_prompt))
    for what, step in steps:
        with tempfile.TemporaryDirectory(prefix="kit-", dir=scratch_root) as scratch:
            try:
                step(native, game_dir, Path(scratch), resolution)
            except automation.AutomationFailure as failure:
                raise automation.AutomationFailure(f"{resolution}: {what}: {failure}") from failure
            except (OSError, ValueError) as failure:
                raise automation.AutomationFailure(
                    f"{resolution}: {what}: {type(failure).__name__}: {failure}") from failure
    with tempfile.TemporaryDirectory(prefix="kit-", dir=scratch_root) as scratch:
        try:
            check_chip(native, game_dir, Path(scratch), resolution, resolution == RESOLUTIONS[0])
        except (automation.AutomationFailure, OSError, ValueError, subprocess.TimeoutExpired) as failure:
            raise automation.AutomationFailure(f"{resolution}: the chip: {failure}") from failure
    print(f"extension-kit: {resolution}: the page, the screen, the prompt and the chip answered as expected")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", required=True, type=Path, help="open-annihilation built with the test extensions")
    parser.add_argument("--scratch-root", required=True, type=Path, help="where each run's own folder is made")
    parser.add_argument("--skip-code", required=True, type=int,
                        help="exit status of a skipped run (OA_GAME_DATA_SKIP_CODE)")
    args = parser.parse_args(argv)
    # Path("") is the current directory, so an empty OA_GAME_DIR is checked first.
    game_dir = os.environ.get("OA_GAME_DIR", "")
    if not game_dir or not Path(game_dir).is_dir():
        if os.environ.get("OA_REQUIRE_GAME_DATA") == "1":
            print("extension-kit: OA_REQUIRE_GAME_DATA is set but OA_GAME_DIR names no installation")
            return 1
        print("extension-kit: skipped: OA_GAME_DIR names no installation")
        return args.skip_code
    args.scratch_root.mkdir(parents=True, exist_ok=True)
    for resolution in RESOLUTIONS:
        try:
            check_at(args.game.resolve(), Path(game_dir), args.scratch_root, resolution)
        except automation.AutomationFailure as failure:
            print(f"extension-kit: {failure}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
