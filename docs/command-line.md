# The command line

`open-annihilation --help` lists every option. This page is the ones an
installer or a launcher uses before the game starts.

## `--version`

```sh
open-annihilation --version
```

Prints one line and exits with status 0. It does not open a window, look for
the game data, or read or write preferences or a log file.

```text
Open Annihilation 0.8.0 (macos-universal arm64, 2ac0217b)
```

The version is this release's. The platform is the name the release packages
use (`macos-universal`, `windows-x64`, `windows-x86`, `windows-arm64`,
`linux-x86_64`, `linux-arm64`, `linux-armhf`), then the architecture that is
running (`arm64`, `x86_64`, `x86` or `arm`). The last word is the commit the
build was made from, eight digits. On Windows the program writes the line to
the console it was started from.

`oa-tool --version` prints the same line.

## `--print-profile`

`--print-profile` prints the resolved mod profile and then the same version
line, and exits. It needs `--mod FILE`, `--mod-dir PATH` or `--game-dir PATH`.
The profile comes first, so a program that reads the profile's JSON still
finds it at the start of the output. The version line is the last line.

## `--dplay-guid`

`--dplay-guid GUID` sets the application GUID this game hosts under and
looks for. A search lists a game only when that game was hosted under the
same GUID, and a game this one hosts answers a search only when the search
asks for that GUID.

Without the option the GUID is Total Annihilation's own,
`{99797420-F5F5-11CF-9827-00A0241496C8}`. Games then stay visible to 3.1c
and to Open Annihilation started without the option. Nothing else about
hosting or joining changes.

The GUID is written with or without braces, in any case:

```text
--dplay-guid {99797420-F5F5-11CF-9827-00A0241496C8}
--dplay-guid 99797420-f5f5-11cf-9827-00a0241496c8
```

Those two are the same GUID. Anything that is not a GUID of that form is
refused, with the message `--dplay-guid expects a GUID, with or without
braces`, and the game ends as it does for any other refused option.

A launcher such as TA Forever starts each mod under a GUID of its own, a
UUIDv5 of Total Annihilation's GUID and the mod's name. Pass that GUID and
this game hosts under it and lists only games hosted under it. A player of
that mod on 3.1c, started with the same GUID, then sees the game. A player
looking under Total Annihilation's own GUID does not. For a mod such as
Ridge, the GUID is the one the launcher uses for Ridge; this game does not
derive it.

`--dplay-guid` stands with `--host` and `--join`. It needs neither: a game
opened from the multiplayer screens uses it too.
See [which games are listed](multiplayer/application-guid.md).

## Starting a network game: `-c FILE` and `--launch FILE`

`-c FILE` and `--launch FILE` are one option. Each names a launch file and
starts a network game from it: the battle room opens with the file's map,
rules, players and content, and the intro is skipped. The file is either
Open Annihilation's own `.oalaunch` file or the settings file TA Forever
already writes. The game tells them apart by what the file holds, whatever
its name: a file whose first line is `oalaunch: 1` is an `.oalaunch` file,
and a file with a `[totala]` section is TA Forever's. A file that is neither
is refused. `-c -` and `--launch -` read the file from standard input. The
schema of both files, and an example, are in
[Launch files](multiplayer/launch-file.md).

`-c` and `--launch` cannot both be given, and each needs a file.

Each setting of a launch file has an option of its own, listed in
[Launch settings](#launch-settings). An option beside a file replaces that
field of the file, whichever is written first. The options work without a
file too: `--host` or `--join ADDRESS` with any of them starts a launch of
its own, so a launcher can write

```sh
open-annihilation --host --game-name "Friday game" --map "Acid Foursome" \
    --commander-death ends --metal 2000 --computer-player core:2::hard
```

A value is checked as the file's value is. A launch the game cannot use
ends the game before a window opens, with status 2 and one line that names
the file and the field (`host.oalaunch: rules.metal: 250000 is above 10000`),
or the option (`--metal: 250000 is above 10000`). The password is never
part of that line and is never written to the log.

`--join ADDRESS` beside TA Forever's file joins that address instead of
hosting. A player who joins takes the file's player settings and computer
players; the host's rules are the ones that play.

## Standard input

When the game is given no launch file, and its standard input is a pipe or
a file rather than a terminal, it looks at the first bytes. When they are
`oalaunch:` the game reads the launch from standard input, as `--launch -`
would, so

```sh
cat host-now.oalaunch | open-annihilation
```

starts that launch. The game waits at most a quarter of a second for the
first byte. When none comes, or the bytes do not begin `oalaunch:`, it
starts as usual, so a program that starts the game with a pipe it never
writes to does not hold the start up. Once a launch has begun, the rest of
it must arrive within two seconds, and the game refuses a launch that does
not end by then. TA Forever's file is read from standard input only with
`-c -` or `--launch -`.

On Windows the same works from the command prompt:

```bat
type host-now.oalaunch | open-annihilation.exe
```

A program started on macOS from the Finder or the Dock has no standard
input to read, so there the launch is handed to the game as a file:

```sh
cat host-now.oalaunch | open -f -a "Open Annihilation"
```

`open -f` writes what it is given to a temporary `.txt` file and opens that
file in the game, which reads it by what it holds, as the next section says.

## Opening a launch file

`.oalaunch` is one of the file types the game registers, with the game's
icon, so a launch file a lobby or a friend gives you opens with a
double-click, from Open With, or dropped on the game's window:

- **Windows:** the game registers it for you each time it starts, under
  `HKEY_CURRENT_USER\Software\Classes`, and the installer registers it for
  everyone who uses the computer. Both open it with
  `"<install folder>\open-annihilation.exe" -c "%1"`; uninstalling removes
  the installer's association.
- **Linux:** the game adds the MIME type `application/x-oalaunch` to your
  data folder, for files named `*.oalaunch` and for any text that begins
  `oalaunch:`, and a desktop entry of its own that opens it with
  `open-annihilation -c %f`.
- **macOS:** the app declares the type and is its owner, so Finder opens it
  with Open Annihilation. The app also takes a plain text file it is handed
  without being its opener, which is what `open -f -a` sends.

What an opened launch file does depends on when it arrives:

- **When it starts the game,** it is the start's launch, exactly as
  `-c FILE`: the intro is skipped, the launch's content chooses what the
  game plays, and the battle room opens on the file's map, rules and
  players. A file the game cannot use ends the game with status 2 and a
  message that names the file and the field.
- **While the game shows the main menu,** the game goes to that launch's
  battle room. From the single-player menu it goes back to the main menu
  first.
- **During a match,** while a game is being set up (a skirmish, a campaign
  mission, the multiplayer screens), or on any other screen, the game opens
  nothing and says so: in the match's message log, or with a notice. A
  launch is not opened while another launch is under way either.
- **While the game runs,** a launch whose `content` names a mod, base game
  or expansions other than those the game plays is not opened; the notice
  says to quit the game and open the file again, which then starts it with
  that content. A file the battle room cannot use is not opened, and the
  notice names the file and the field.

The `status` group of a launch file opened in the game has no effect: the
status stream is set up from the command line, before the file arrives.

On Windows and Linux, opening a launch file while the game runs starts a
second copy of the game with `-c FILE`, which starts as any second copy
does. On macOS the system hands the file to the copy that runs.

## Launch settings

Every setting a host or a joining player can choose in the battle room is a
field of the `.oalaunch` file and an option. The fields are grouped in the
file by their first word: `game` for the network game, `player` for this
player's seat, `rules` for the host's rules, then `map`, `computer-players`,
`content` and `status`. On the command line a setting written `on|off` takes
`on` or `off`, and in the file `true` or `false` as well.
`--tournament`, `--closed` and `--lock-options` alone turn their setting on,
and `--tournament=off`, `--closed=off` and `--lock-options=off` turn it off.

What a launch does not set is what a player does in the room rather than
chooses for it: READY, an alliance with a player who has joined, the
screen size the RES column shows, which is the player's own setting, and
the buttons a mod adds, which type a command into the chat.

<!-- netgame-launch-help checks that this table is the table of launch
settings in src/app/netgame/launch_file.cpp, word for word. -->

| Field | Option | What it sets |
|---|---|---|
| `game.role` | `--host \| --join ADDRESS` | Hosts the game, or joins the game at `game.address`: `host` or `join`. A host plays over TCP/IP. |
| `game.address` | `--join ADDRESS` | The address of the game to join, at most 63 characters. A host needs none. |
| `game.name` | `--game-name NAME` | The game's name, at most 63 characters. |
| `game.password` | `--game-password PASSWORD` | The game's password, at most 31 characters. It is never written to the log. |
| `game.player-limit` | `--player-limit N` | How many players the game takes, from 2 through 10. |
| `game.launcher-name` | `--launcher-name NAME` | The launcher's name, at most 15 characters. The battle room shows it in place of Previous Menu when it has at most 11. |
| `game.tournament` | `--tournament[=on\|off]` | A tournament game: `on` or `off`. It seats no computer players, and with `rules.lock-options` the players' sides and teams are locked too. |
| `game.closed` | `--closed[=on\|off]` | Closes the game to more players once the launch's own computer players are seated: `on` or `off`. |
| `game.directplay.application-guid` | `--dplay-guid GUID` | The DirectPlay application GUID the game hosts under and looks for, with or without braces. |
| `game.directplay.port` | `--dplay-port PORT` | The DirectPlay port the game hosts on and looks for games at, from 1 through 65535. |
| `player.name` | `--player-name NAME` | The player's name, at most 31 characters. |
| `player.side` | `--side arm\|core\|watcher` | The player's side, or `watcher` to watch instead of play. |
| `player.team` | `--team 1-5\|none` | The player's team, from 1 through 5, or `none`. |
| `player.colour` | `--colour 1-10` | The player's colour, from 1 through 10 in the order the battle room steps through them. A colour another player has gives the next free one. |
| `map` | `--map NAME` | The map the host plays: the name the list shows, or its key (`name@package` for a pack map). At most 63 characters. |
| `rules.commander-death` | `--commander-death continues\|ends\|deathmatch` | What a commander's death does: the game `continues`, the player's game `ends`, or `deathmatch`. |
| `rules.metal` | `--metal N` | Starting metal, from 0 through 10000. 0 leaves the battle room's own amount. |
| `rules.energy` | `--energy N` | Starting energy, from 0 through 10000. 0 leaves the battle room's own amount. |
| `rules.unit-limit` | `--unit-limit N` | The most units each player may have, from 20 through 6553. |
| `rules.line-of-sight` | `--line-of-sight true\|circular\|permanent` | What each player sees: `true` line of sight, `circular` or `permanent`. |
| `rules.mapping` | `--mapping mapped\|unmapped` | Whether the map starts `mapped` or `unmapped`. |
| `rules.start-positions` | `--start-positions fixed\|random` | Whether players start at `fixed` or `random` positions. |
| `rules.cheats` | `--cheats on\|off` | Whether the cheat codes work: `on` or `off`. |
| `rules.watchers` | `--watchers on\|off` | Whether players may watch instead of play: `on` or `off`. |
| `rules.time-limit` | `--time-limit N` | A whole number of 0 or more, kept and passed on with the score report. |
| `rules.lock-options` | `--lock-options[=on\|off]` | Locks the battle room's option controls for a host: `on` or `off`. |
| `rules.unit-restrictions` | `--unit-restriction UNIT=0-100\|no-limit` | Build limits, as the battle room's Restrictions sets them: each unit by its FBI UnitName (such as `CORKROG`), from 0, which cannot be built, through 100, or `no-limit`. Give the option once for each unit. A unit the game's content does not have is left out. |
| `computer-players` | `--computer-player arm\|core[:TEAM[:COLOUR[:DIFFICULTY]]]` | The computer players this machine seats, at most 9, each with a side, and a team, colour and difficulty (`easy`, `medium` or `hard`) the battle room otherwise chooses. Give the option once for each. |
| `content.base-game` | `--content GAME[:EXPANSION,...]` | The base game this run plays, by its id. |
| `content.expansions` | `--content GAME[:EXPANSION,...]` | The expansions this run plays, a list of ids. It needs a base game. |
| `content.mod` | `--mod FILE \| --mod-dir PATH` | The mod this run plays: in the file a mod's id or a path, and on the command line its profile or its folder. |
| `status.standard-output` | `--status-stdout` | Writes the status stream on standard output: `on` or `off`. |
| `status.file` | `--status-file PATH` | Writes the status stream to this file. |
| `status.quit-after-game` | `--quit-after-game[=after-score]` | Leaves when the game ends (`on`), stays (`off`), or leaves after the score (`after-score`). |

`--computer-player` takes the computer player's side, then, after a colon
each, its team (`1` to `5`, or `none`), its colour (`1` to `10`) and its
difficulty (`easy`, `medium` or `hard`). A part left out or left empty is
chosen by the battle room: `core:2::hard` is Core on team 2, in the next
free colour, playing hard. The first `--computer-player` replaces the
file's list, and each adds one computer player.

`--unit-restriction` takes a unit's FBI UnitName, an equals sign and its
limit, such as `CORKROG=0`. In the file the restrictions are a group of
`UNIT: LIMIT` lines under `rules.unit-restrictions`. The first
`--unit-restriction` replaces the file's restrictions, and a later one for
the same unit replaces the earlier.

A deathmatch or a tournament game seats no computer players, the computer
players and the player must fit the player limit, and a host who watches
needs `rules.watchers` on. The game refuses a launch that breaks one of
these, naming the field or the option.

## `--list-content --json`

`--list-content --json` prints the installed maps, mods and expansions as
one JSON object and exits with status 0. It does not open a window. Each
map has its shown name, key, player count and size. Each mod and expansion
has its id and name. The shape is in
[Launch files](multiplayer/launch-file.md).

`--list-content` without `--json`, or `--json` without `--list-content`,
is refused.

## `--status-stdout`, `--status-file` and `--quit-after-game`

`--status-stdout` writes what the game is doing, and how it ended for
each player, as one JSON object a line on standard output. The game's
own log goes to the log folder, so the lines are the only text there.
`--status-file PATH` writes the same lines to a file. Both options
together write every line to both. `--quit-after-game` leaves once the
game has ended. `--quit-after-game=after-score` waits until the player
has left the score screen.

A missing file, or a file the game cannot open, is refused, and the game
ends as it does for any other refused option, before the first line.
See [what a game reports as it is played](multiplayer/status.md).
