# The automation endpoint

A program on the same machine as the game can drive it and watch it
through the automation endpoint: a service on the loopback address that
the game, `open-annihilation`, serves when it is started with `--fark`. The
program reads what the game shows and holds (the screen, its controls, the
frames presented, the preferences, the running match and the battle room),
hands it keys, text, pointer and finger events as a device does, and is
told of changes as they happen. It is meant for programs that test the
game or play scripted journeys through it, on every platform the game
runs on, in the same build players run.

It is off unless `--fark` is on the command line. A player never needs it.

## Contents

- [Turning it on](#turning-it-on)
- [Connecting](#connecting)
- [Requests](#requests)
- [OA's own screens](#oas-own-screens)
- [Input](#input)
- [Events](#events)
- [What it never does](#what-it-never-does)
- [In a battle room](#in-a-battle-room)
- [Checks](#checks)

## Turning it on

```
--fark                     serve the automation endpoint
--fark-address ADDRESS     where: 127.0.0.1:PORT or [::1]:PORT; port 0, the default
                           (127.0.0.1:0), lets the system choose
--fark-file PATH           where the address and token are written; by default
                           automation.json in the folder of the preferences file
```

- Only the command line turns it on: no setting, environment variable or
  file does.
- Only the loopback address can be served. Any other is refused with
  `automation: only a loopback address can be served`, and the game exits
  as for any refused option. `--fark-address` or `--fark-file` without
  `--fark` is refused too.
- The endpoint is served from the game's main loop, so `--fark` cannot go
  with an option that runs no main loop or runs its own:
  `--headless-check` and the options that imply it, the `--check-*`
  options, `--benchmark`, `--match-ticks`, `--render-script`,
  `--generate-script` and `--showcase`. It goes with `--host` and
  `--join`, so a game can start in a battle room and still be driven.
- With no window to open, start the game on SDL's dummy drivers
  (`SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy`): the endpoint works
  the same, frames included.
- While `--fark` is on, the game keeps running every frame when its window
  is not the active one, so the endpoint is always served.

As it starts, the game listens and writes the file, whole (first beside
it, then renamed over it) and readable only by its owner where the system
has such modes:

```json
{"version":1,"address":"127.0.0.1:53122","token":"9c1f…(32 hex digits)","pid":41877}
```

The token is 128 random bits from the system's generator, new each run.
The log says `automation: serving on 127.0.0.1:53122` and where the file
is, and never holds the token. [settings.md](settings.md#where-the-settings-are-kept)
says where the preferences file, and so the default file, is kept;
`--preferences-file` names another.

## Connecting

The endpoint speaks the automation protocol over one TCP connection. Every
message, both ways, is a frame: a header line, a JSON object and an
optional payload of raw bytes.

```
AUTO/1 - <json-length> <payload-length> <crc32>\n
<json-length bytes of UTF-8 JSON><payload-length bytes>
```

The lengths are in decimal, and the CRC is CRC-32 (IEEE 802.3) over the
JSON and the payload together, in eight lowercase hexadecimal digits. A
JSON part above 1 MiB or a payload above 64 MiB is refused.
[src/app/automation](../src/app/automation/README.md#the-codec) gives the
framing in full.

- One client at a time. A connection whose first bytes are not `AUTO/1 `
  is closed unanswered.
- The first request is `hello` with the file's token:
  `{"id": 1, "op": "hello", "versions": [1], "client": "NAME", "token": "…"}`.
  Its JSON is at most 4 KiB, with no payload, or the connection is closed
  unanswered. A wrong token is answered `denied` and the connection
  closed. While a client is connected, a second connection is answered
  `busy` and closed.
- A request is `{"id", "op", ...}`. Its answer is
  `{"id", "ok": true, "frame", "tick", ...}`, where `frame` counts the
  frames served and `tick` is the running match's, or
  `{"id", "ok": false, "error": {"code", "message"}}`. Requests are
  answered in order. A client may send several requests at once and then
  end its side of the connection: each is still answered before the
  connection closes. The game reads no more from a client that sends
  faster than it answers until it has caught up.
- Every text the game gives is UTF-8. Text it holds in a code page that is
  not, as an installation whose files use another code page or another
  player's game can give it (captions, list rows, names, chat,
  preferences), is replaced whole by `Non-Unicode Text Error::` followed
  by the standard base64 (RFC 4648, with padding) of its bytes, from which
  the client recovers them: the four bytes of `Jörg` in Latin-1 come as
  `"Non-Unicode Text Error::SvZyZw=="`.

A short client in Python:

```python
import json, socket, zlib

def frame(message):
    body = json.dumps(message).encode()
    return b"AUTO/1 - %d 0 %08x\n" % (len(body), zlib.crc32(body)) + body

def read(sock):
    header = b""
    while not header.endswith(b"\n"):
        header += sock.recv(1)
    _, _, json_length, payload_length, _ = header.split()
    size = int(json_length) + int(payload_length)
    data = b""
    while len(data) < size:
        data += sock.recv(size - len(data))
    return json.loads(data[:int(json_length)]), data[int(json_length):]

endpoint = json.load(open("automation.json"))
host, _, port = endpoint["address"].rpartition(":")
sock = socket.create_connection((host.strip("[]"), int(port)))
sock.sendall(frame({"id": 1, "op": "hello", "versions": [1], "client": "example",
                    "token": endpoint["token"]}))
print(read(sock)[0])
sock.sendall(frame({"id": 2, "op": "screen"}))
print(read(sock)[0]["screen"])  # main_menu
```

## Requests

| Request | What it answers |
|---|---|
| `hello` | the protocol's version, the engine's, the window's size, where the game's 640x480 canvas lies in it, the tick rate (30) |
| `screen` | the screen shown (`main_menu`, `skirmish`, `mp_battleroom`, `match` and the like), the dialogs over it, the control that takes the keys, the pointer, and in a match where the camera looks |
| `controls` | an extension's windows first, then a message box's, then Open Annihilation's own screens' ([below](#oas-own-screens)), then every control of the panel that takes the pointer, a dialog's first: its name, its kind, the extension window it belongs to (or none), its place on the canvas and in the window, its state and text, and a list's rows |
| `input` | once the game has taken the events it was given, the frame and tick at which it took them ([below](#input)) |
| `frame` | the frame the game presented in its window, or the one it composed, whole or a region of it, as RGB pixels, a PNG file, or a hash and mean colour |
| `prefs` | the preferences as the game holds them now, and their file |
| `match` | the running match: pause, speed, each player's name, side, team, allies, units, kills and losses, the outcome and winner; with `state_hash`, the digest of its state that a saved game carries |
| `room` | the battle room: the game's name, its host, the seated players with their ready flags, sides, colours, teams and pings, the map, the options and the chat |
| `subscribe` | the kinds of event the client is sent from then on |
| `quit` | nothing more: the game then quits as a player closing its window does |

[src/app/automation](../src/app/automation/README.md#the-protocol) gives
every request's fields and answers, and the errors it is refused with.

## OA's own screens

Open Annihilation's own screens (the OA button, Settings, a notice, a
prompt, and every later one: the Library, the map browser, the gallery)
are drawn by the engine's UI kit, and `controls` lists them under names of
one scheme, so that a journey presses them by name as it presses the
original screens' controls.

**The names.** Each control is listed as `oa.` followed by its kit name:
lower-case words of `a`–`z`, `0`–`9` and `-` joined by dots, at most 120
bytes in all, unique ignoring case. Every word is one of the code's, never
a text as the player sees it, so no name changes with the language:

- a word made from a code name is the enumerator's name with `_` as `-`;
- a word made from a value is the word the setting's preference stores,
  lower-cased, every other character a `-` (`1/32` gives `1-32`, `zh-Hans`
  gives `zh-hans`);
- a button's word is its English caption as the code holds it, lower-cased,
  an ellipsis dropped and spaces as `-` (MANAGE… gives `manage`).

A part of a control that takes a press of its own is listed after it with
one word more: a switch's halves `.off` and `.on`, a strip's levels and an
open drop-down's items by their values, a row of buttons' buttons by their
words. A click at a part's centre presses that part; a click at a switch's
centre would land on the line between Off and On.

| Control | Name | Kind, and what it reports |
|---|---|---|
| The OA button (main menu, in-game menu) | `oa.button` | button; no dialog; its caption, empty while it has none |
| A section's entry | `oa.settings.nav.<page>` | check box, checked while its section shows |
| A switch, and its halves | `oa.settings.<row>`, `oa.settings.<row>.off`, `oa.settings.<row>.on` | the switch a label with its text; the halves check boxes, checked on the half shown |
| A level strip, and its levels | `oa.settings.<row>`, `oa.settings.<row>.<value>` | label; check boxes, checked on the level chosen |
| A drop-down, closed | `oa.settings.<row>` | button; its text the choice shown |
| A drop-down's item, while open | `oa.settings.<row>.<value>` | check box, checked on the choice |
| A slider | `oa.settings.<row>` | slider; its text the value shown |
| A row's own button (MANAGE…) | `oa.settings.<row>.<button>`, as `oa.settings.game-files-summary.manage` | button |
| Your files' buttons | `oa.settings.user-folder.saves`, `.screenshots`, `.mods` | button |
| A mod's row and its ROLL BACK | `oa.settings.mod.<mod>.switch`, `oa.settings.mod.<mod>.roll-back`; OPEN MODS FOLDER `oa.settings.open-mods-folder` | check box, checked on the mod played; button |
| Developer's own controls | `oa.settings.developer-mode.off`/`.on`, `oa.settings.frame-stats.off`/`.on`, `oa.settings.active-only.off`/`.on`, `oa.settings.restore-profile-values` | as above |
| Developer's list | `oa.settings.hack-area.<area>`, `oa.settings.hack.<hack>` (its header, with its switch's `.off`/`.on`), `oa.settings.hack.<hack>.<parameter>` (a switch's with `.off`/`.on`) | the headers check boxes, a hack's checked while it is on; a parameter's switch as above, its slider a slider |
| The footer | `oa.settings.restore-defaults`, `oa.settings.cancel`, `oa.settings.ok` | button |
| The Switch Mod question | `oa.settings.question.yes`, `oa.settings.question.no` | button |
| The section's scroll bar | `oa.settings.scroll-bar` | slider |
| A notice's buttons | `oa.<word>.ok`, `oa.<word>.open` | button |
| A prompt's buttons | `oa.<word>.<id>`, or `oa.<word>.button-<n>` from 1 where a button has no id | button |
| A notice's or a prompt's words | `oa.<word>.body` | label; its title and paragraphs, one a line |

`<page>` is the section's word (`controls`, `common-tweaks`, `mod-keys`),
`<row>` the setting's (`wheel-zoom`, `hud-scaling`), `<value>` the stored
word (`whole-map`, `whole-steps`, `1-32`). `<mod>` is the mod's profile id,
`no-mod` for No Mod, a folder without a profile taking its folder's name in
that form, a second row of the same word `-2` and a third `-3`. `<hack>`
is the standard hack's id with its dots kept. `<word>` is a notice's or a
prompt's own word: `notice` and `prompt` unless the code that raises it
gives one, as the missing-language question does (`language-notice`). The
installs' prompts' ids are their answers: `cancel`, `ok`, `replace`,
`alongside`, `reinstall`, `open-folder`, `play-now`.

**The kinds.** Each kit control maps to one kind: a button, a link and a
row of buttons' button are `button`; a switch, a strip and a row of
buttons themselves are `label` with the shown text, their parts listed
after them; a drop-down is a `button` whose text is the shown choice, an
open one's items `check box`; a slider and a scroll bar are `slider`; a tab
and a list's row are `check box`, checked while selected; a text field is
`text field` with the typed text; an area is `area`, but one neither
enabled nor focusable is a line a journey reads, such as a notice's words,
listed as `label` with its text; a list is `list` with `items` empty, its
rows being controls of their own.

**Where they are listed.** `dialog` is the OA screen's name after `oa.`:
`oa.settings`, `oa.notice`, `oa.prompt` (or `oa.<word>`), and `oa.<name>`
for every later screen; it is null for `oa.button`, so that `screen`'s
`dialogs` names the screen that is open. The OA screen's controls come
right after a message box's, ahead of everything else but an extension's
windows. While an OA screen takes every input, the controls listed after
it, the screen's own and its panels', are listed disabled. `rect` is on
the canvas, which on the menus is the 640x480 frame while the OA screens
are drawn in the window's pixels: `window_rect` is the exact rectangle in
the window, and `rect` that rectangle's corners mapped back onto the
canvas, rounded outwards, so it may reach past 640. A click at `rect`'s
centre through `input` presses the control or part at every window size.

**Every other OA screen** is listed by the same rule, with no table of its
own: each control `oa.` and its kit name, each part as above, `dialog`
`oa.` and the screen's name. A screen names its kit controls with `[a-z0-9-]`
words joined by dots, and a journey clicks the listed names, `oa.`
included.

**An example journey.** Turning the mouse wheel's zoom off and letting the
map zoom out to the whole map, from the main menu:

1. click `oa.button`; wait until `oa.settings.ok` is listed;
2. click `oa.settings.nav.controls`, then `oa.settings.wheel-zoom.off`
   (its `checked` turns true);
3. click `oa.settings.max-zoom-out`, then
   `oa.settings.max-zoom-out.whole-map`;
4. click `oa.settings.ok`: `prefs` then holds
   `open-annihilation.wheel-zoom` `0` and `open-annihilation.max-zoom-out`
   `whole-map`.

## Input

`input` carries `events`: `key_down` and `key_up` with the key's SDL
scancode and key code, `text`, `pointer_move`, `button_down` and
`button_up` with the button and the clicks, `wheel`, and `finger_down`,
`finger_move` and `finger_up`. Points are on the canvas (`space`
`game`, the default) or in the window's pixels (`window`).

- The events go onto SDL's event queue, and the game takes them in its
  next frame exactly as it takes a keyboard's, a mouse's or a touch
  screen's. The endpoint never calls the game's handlers, never gives
  orders and never types console lines on its own.
- The answer comes once the game has taken them: `consumed`, with the
  frame and tick.
- With `expect_screen`, the events are refused `screen_changed`, and none
  is pushed, when another screen shows by then, so that a click never
  lands on the screen that replaced the one it was meant for.
- Keys and buttons the events hold down count as held for the game's
  reads of what is held (the arrow keys that scroll the camera, Shift,
  Ctrl, a held button) until an event lets them go or the client leaves.
  The desktop's pointer is never moved.
- Text is typed only into a field that takes it, as SDL types it.

## Events

After `subscribe`, the client is sent events unasked, among the answers:
`{"event", "seq", "frame", "tick", ...}`. The kinds are `screen`,
`player` (a battle-room seat taken or left), `ready`, `chat`, `loaded`,
`match` (started or ended), `pause`, `speed`, `alliance` and `game_over`.
The endpoint learns them by comparing what the game holds from one frame
to the next; nothing is added to the game to produce them.

## What it never does

- **Change the game.** It reads the match and the battle room and changes
  neither. A match played with the endpoint on, and a client reading it at
  every frame, plays out tick for tick as it does without it.
- **Change what goes to other players.** Nothing the endpoint reads or is
  given is sent over a network game's connections, and the network
  protocol is the same with it on. The one difference the other players
  see is the engine's chat line in the battle room
  ([below](#in-a-battle-room)).
- **Run without `--fark`.** Without it no socket is opened and no file is
  written.
- **Listen beyond this machine.** It serves the loopback address only.
- **Read or write files, start programs or reach the network** for a
  client: it has no request that does.
- **Hold the game up.** It is served on the game's own thread at fixed
  points of each frame, takes requests for at most a millisecond each
  time, and never waits.
  A client that stops reading is dropped once 68 MiB of answers wait for
  it (`automation: client dropped: not reading` in the log).

## In a battle room

Every player in a battle room is told when a game there can be driven: the
engine's line in the room's chat ends with ` REMOTED` while `--fark` is
on, as it ends with ` DEV MODE` while Developer Mode is:

```
[Engine: OpenAnnihilation v0.7.2 REMOTED]
[Engine: OpenAnnihilation v0.7.2 DEV MODE REMOTED]
```

## Checks

The engine's tests of the endpoint are `automation-protocol`,
`automation-options`, `automation-input`, `automation-reports` and the
native checks `native-automation-menus`, `native-automation-input`,
`native-automation-frame`, `native-automation-settings` and
`native-automation-digest`, which run the game with `--fark` on the dummy
drivers over an installed game.
[testing.md](development/testing.md#the-automation-endpoint) says how to run them,
and [src/app/automation](../src/app/automation/README.md#tests) what each
checks.
