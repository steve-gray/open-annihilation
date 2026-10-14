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

`-c` and `--launch` cannot both name a file. `--launch` needs one; a `-c`
with no word after it starts no launch, as in 3.1c. `-c` keeps 3.1c's other
rules too, which [The 3.1c command line](#the-31c-command-line) gives.

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

## The 3.1c command line

Open Annihilation takes the command line of Total Annihilation 3.1c, so a
launcher or a shortcut written for 3.1c starts this game the same way. This
section says what each of 3.1c's switches does here, how the file `-c` names
is read, and where this game differs from 3.1c on purpose.

### How the switches are read

The words that do not begin `--` are read as 3.1c reads its command line:

- They are split at spaces and tabs. Quotes do not hold a word together, so
  a file name with a space in it is two words.
- A switch is `-` or `/` and its letter, in either case: `-c`, `-C` and `/c`
  are one switch.
- A switch that takes a value reads the text right after its letter, or,
  when there is none, the next word. That word is then used up: it is never
  read as a switch or a language, even when the switch cannot use it.
- The words are read from left to right, so a later switch replaces what an
  earlier one set.
- A word that does not begin with `-` or `/` names the language. The last
  one counts, and its first 63 characters are kept.
- A letter that is not a switch, a lone `-` or `/`, and a digit are ignored.
- No switch is refused, and no value of the `-c` file. The game ends before
  a window opens, with status 2 and a line that names the file and the key,
  only when it cannot use the launch: a launch file it cannot read, or that
  is neither kind, a map that is not installed, or a base game, expansion or
  mod this player does not have. An `.oalaunch` file's values are checked
  as [Launch files](multiplayer/launch-file.md) says. A line of game
  switches of 4096 characters or more is refused too.

The options that begin `--` are this game's own. They are taken out first,
wherever they stand, and they win over a launch file and over the switches,
whatever their order. `-h` with nothing after it shows the help, as `--help`
does; with a word after it, it is the switch below.

A switch that does nothing here, or less than in 3.1c, is named in the log
as the game starts, one line each: `-f`, `-l`, `-w`, `-y`, the developer
switches, a `-b` that does nothing, a `-c` with no file, and `-h`, `-n` and
`-b` that a later `-c` cleared.

| Switch | What it does here |
|---|---|
| `-b WORD` | Sets one of the battle room's options for the launch: `lock` locks the options; `deathplays`, `deathends` and `deathmatch` choose what a commander's death does; `truelos`, `circlos` and `permlos` the line of sight; `mapping` starts the map mapped; `fixedloc` uses fixed start positions; `cheating` allows the cheat codes; and `watching` allows watchers. The word is in any case, attached (`-bdeathends`) or the next word. It applies to the launch `-c`, `--launch`, `--host` or `--join` starts, as the file's key would, and an option such as `--cheats` replaces it. Without a launch, or with a word that names no option, it does nothing. |
| `-c FILE` | Skips the intro and clears the launch, then reads FILE: TA Forever's settings file, read as 3.1c reads it ([below](#the--c-file)), or an `.oalaunch` file. `-c -` reads it from standard input. The next word is the file whatever it begins with. With no word after it, `-c` only skips the intro, and no launch starts. |
| `-d`, `-df` | `-d` runs the game in a window, where the intro does not play, and `-df` full screen, for this run only: neither is remembered. Another letter after `d` counts as `-d`. |
| `-e PERCENT` | Drops that percentage of this machine's outgoing network frames at random, to test a poor connection: 0 to 100. Anything else, or a value that begins with `-`, is 0. |
| `-f` | Nothing. |
| `-h NAME` | Marks this machine as the host and names the game; 63 characters are kept. A next word that begins with `-` is used up and sets nothing. |
| `-l` | Nothing, as in 3.1c. |
| `-n TYPE[:ADDRESS]` | Skips the intro and names the transport the multiplayer screens open on: `1` TCP/IP, with the address to connect to after a colon (`-n1:192.0.2.10`); `2` IPX, `3` a modem and `4` a serial cable, which this game does not have, so the screens open on the provider list. Another number only skips the intro. |
| `-p RATE` | The network send rate in packets per second: 1 counts as 2 and above 30 as 30, 0 is 5 a second, and below 0, or a value that begins with `-`, turns paced sending off. A launch file's `pfs` replaces it. |
| `-r TEXT` | Says, on standard output, that this game needs no DirectPlay registration, and ends with status 0 without opening a window. TEXT is the rest of the line. |
| `-s` | No sound. |
| `-t SECONDS` | The network timeout: a player silent for longer brings up the timeout window. 30 to 300; anything else is 30. |
| `-w` | Turns the sound off, as `-s` does. |
| `-y` | Skips the intro. |
| `-memfussy` and the other developer switches | Nothing. The others are `-memnofussy`, `-memfrontalign`, `-gonzo`, `-memset`, `-memnoset`, `-fpufussy`, `-fpunofussy`, `-dprinton`, `-dprintoff`, `-dprintfile`, `-memorystatus`, `-performancestatus`, `-disableimagehlp`, `-enableimagehlp`, `-disableimagehlplines`, `-enableimagehlplines`, `-debughelper`, `-debughelperold`, `-saveresources` and `-assertmethod`. A word that begins with one of them is skipped whole. |
| `LANGUAGE` | A word that is not a switch names the language, as above. |

### The `-c` file

TA Forever, and other launchers written for 3.1c, write a settings file and
pass it with `-c`. This game reads it as 3.1c reads it:

- A name with no `\`, `/` or `:` in it is looked for in the current folder,
  then in the game folder: the one `--game-dir` names, or else the one this
  game remembers from its last start. Any other name is used as written. The
  log says when the file came from the game folder.
- Only the first `[TotalA]` section is read, its name in any case. The keys
  are read in any case, the first copy of a key counts, and a key the file
  leaves out takes its default.
- Spaces around a value are dropped, and so are quotes around it. A text is
  cut to fit its field. A number is the whole number the value begins with,
  so `12abc` is 12 and `abc` is 0; an empty value is the key's default.
- No value is refused: each row of the table says what an odd one does.
  Lines that are not `key=value`, other sections, and keys 3.1c does not
  read are skipped, and the log names each key that is not read, once.

`connect`, `creator` and `address` decide how the game starts, as in 3.1c.
With TCP/IP and an address, the game is hosted at once when `creator` is not
0, and the game at the address is joined at once when it is. With TCP/IP and
no address, the multiplayer screens ask for the address; with another
transport they open on the provider list; with none, the main menu waits for
the player. When the file names none of the three, as TA Forever's does, the
game hosts over TCP/IP at once: that rule is this game's own, so that those
launchers start a game.

`-c` clears the launch before it reads the file, as 3.1c does, so `-h`, `-n`
and `-b` written before `-c` are lost, and the log says so. Written after
`-c`, they replace the file's game name and host flag (`-h`), transport and
address (`-n`), and battle room options (`-b`); `connect`, `creator` and
`address` then decide with those. `pfs` replaces `-p` wherever it stands.
The options that begin `--` replace the file's values whatever their order.

The launch stays active for the whole run, and every battle room applies the
file's options as it opens.

<!-- netgame-launch-help checks that this table is the table of the keys of
the -c file in src/app/netgame/launch_file.cpp, word for word. -->

| Key | Field | What the game does with it |
|---|---|---|
| `connect` | none | The transport: `tcpip`, `internet` or `1` is TCP/IP; `ipx`, `novell` or `2`, `modem` or `3`, and `serial` or `4` name ones this game does not have, and the provider list opens. A word of three letters or more counts when it and one of these begin alike, so `tcp` is TCP/IP. `0`, the default, leaves the transport to the menus. |
| `creator` | `game.role` | Any whole number but 0 hosts, and 0 joins. With TCP/IP and an `address` the game is hosted or joined at once; otherwise the multiplayer screens ask. |
| `registeredonly` | none | Read and not used. |
| `playerlimit` | `game.player-limit` | How many players the game takes, from 2 through 10; above 10 counts as 10, and 1 or less leaves the game's own. 6 when the key is absent. |
| `timelimit` | `rules.time-limit` | Passed on with the score report; it does not end the game. Below 0 counts as 0. |
| `pfs` | none | The network send rate in packets per second, in place of `-p`: below 0 turns paced sending off, 1 counts as 2, above 30 counts as 30, and 0 leaves the rate alone. |
| `provider` | `game.launcher-name` | The launcher's name, its first 15 characters kept. The battle room shows it in place of Previous Menu when it has at most 11. |
| `session` | `game.name` | The game's name, its first 63 characters kept. |
| `mission` | `map` | The map the host plays, by the name the list shows or its key, its first 63 characters kept. A map that is not installed ends the start. |
| `password` | `game.password` | The game's password, its first 31 characters kept. |
| `player` | `player.name` | The player's name, its first 31 characters kept. Empty leaves the player's own name. |
| `address` | `game.address` | The TCP/IP address, its first 63 characters kept. With TCP/IP, a host creates its game at once and a player who joins joins the game there. |
| `lockOptions` | `rules.lock-options` | Any whole number but 0 locks the battle room's options, and 0 unlocks them. |
| `maxunits` | `rules.unit-limit` | The most units each player may have; 0 leaves the battle room's own. Below 20 counts as 20, and above 6553 as 6553. |
| `energy` | `rules.energy` | Starting energy; 0 leaves the battle room's own 1000. Above 10000 counts as 10000, and below 0 as 0. |
| `metal` | `rules.metal` | Starting metal; 0 leaves the battle room's own 1000. Above 10000 counts as 10000, and below 0 as 0. |
| `deathopt` | `rules.commander-death` | `1` the game continues, `2` the player's game ends, `3` deathmatch; any other number leaves the battle room's choice. |
| `los` | `rules.line-of-sight` | `1` true line of sight, `2` circular, `3` permanent; any other number leaves the battle room's choice. |
| `cheats` | `rules.cheats` | `2` allows the cheat codes, any other number but 0 disallows them, and 0 leaves the battle room's choice. |
| `location` | `rules.start-positions` | `1` fixed start positions, any other number but 0 random, and 0 leaves the battle room's choice. |
| `mapmode` | `rules.mapping` | `1` starts the map unmapped, any other number but 0 mapped, and 0 leaves the battle room's choice. |
| `watching` | `rules.watchers` | `2` allows watchers, any other number but 0 disallows them, and 0 leaves the battle room's choice. |
| `tournament` | none | Not read: 3.1c never reads it. The game logs it and goes on; a tournament game is `game.tournament` in an `.oalaunch` file. |
| `tournment` | none | Not read: 3.1c never reads it. The game logs it and goes on. |

### Where this game differs from 3.1c

- **The file is always read.** Not every 3.1c reads it: on some installs
  3.1c ignores the file, and `-c` only skips the intro. Launchers expect the
  file read, as the 3.1c that reads it does, so this game reads it whenever
  it is given.
- **A bare name is looked for in the current folder first.** 3.1c looks in
  the game's folder alone.
- **A file that names no connection hosts**, as above. 3.1c leaves the
  choice to its menus.
- **A player who joins joins at once**, as `--join` does. 3.1c shows the
  games at the address and waits for the player to choose one.
- **Some launches are refused.** 3.1c refuses nothing: a file it cannot read
  starts with every key at its default, and a map it does not have leaves
  the battle room on the map it had. This game will not start a game nobody
  chose, so it ends with status 2 when the file cannot be read, when the
  file is neither a settings file nor an `.oalaunch` file (it reads both,
  and cannot tell what such a file meant), when the map is not installed,
  and when the content is not.
- **Values 3.1c mishandles are kept in range.** A `maxunits` below 20 breaks
  3.1c's unit slider and is 20 here; an `energy` or `metal` below 0 or above
  32767 goes wrong in 3.1c, and is 0 or 10000 here.
- **`-b` works.** 3.1c compares the whole word, `-b` included, with its list
  of words, so no word ever matches and `-b` does nothing. This game gives
  each word the meaning its name says.
- **`-y`, `-r`, `-f` and `-w` do less.** This game has no built-in internet
  lobby for `-y` to open, needs no DirectPlay registration for `-r` to make,
  reads no disc for `-f` to find, and has no second sound player for `-w` to
  use. `-r` ends with status 0, where 3.1c ends with 1.
- **The launcher's name** (`provider`) replaces Previous Menu for every
  launch. 3.1c shows it only when a lobby program started the game.
- **The window.** Without `-d` or `-df`, a player's run opens full screen on
  Windows and in a window on macOS and Linux. 3.1c always starts full screen.
- **Long lines.** A line of game switches of 4096 characters or more is
  refused; 3.1c reads any length.
