# Launch files

A launcher can start a network game with the map, the rules, the players,
the content and the status stream already chosen. The battle room then
opens ready.

Two files do this. Open Annihilation's own `.oalaunch` file holds every
setting the battle room has. TA Forever's settings file, the one it already
writes, holds the settings TA Forever knows. `-c FILE` and `--launch FILE`
read either, told apart by what the file holds, whatever its name. `-c -`
and `--launch -` read it from standard input, and a launch piped in is read
without either (`cat host-now.oalaunch | open-annihilation`). Each setting
also has an option of its own. An option beside a file replaces that field,
whichever is written first. [The command line](../command-line.md#launch-settings)
lists every setting with its field and its option.

The password is used for the game and is never written to the log. A line
that refuses a launch does not repeat it.

A launch the game cannot use ends the game before a window opens. The exit
status is 2. The line names the file and the field, or the option, and why,
for example `host.oalaunch: rules.metal: 250000 is above 10000`.

## The `.oalaunch` file

The file is YAML. Its first line is `oalaunch: 1`; blank lines and comments
may come before it. A missing header, or another number, is refused. Every
other key is a setting's field, grouped by the field's first word, and a
key that is not one is refused with its full name (`rules.nope`). Each key
is named in full words for what it holds.

| Group | What it holds |
|---|---|
| `game` | The network game: whether this player hosts or joins, the address to join, the game's name and password, the player limit, the launcher's name on the battle room's caption, a tournament game, a game closed to more players, and the DirectPlay application GUID and port (`game.directplay`). |
| `player` | This player's seat: name, side or watching, team and colour. |
| `map` | The map the host plays. |
| `rules` | The host's rules: commander death, starting metal and energy, the unit limit, line of sight, mapping, start positions, cheats, watchers, the time limit, the lock on the options and the unit restrictions. A player who joins does not apply them: the host's rules are the ones that play. |
| `computer-players` | The computer players this machine seats, a list. |
| `content` | The base game, expansions and mod this run plays. |
| `status` | The status stream ([status.md](status.md)). |

A setting written `on` or `off` may also be written `true` or `false`. A
setting the file leaves out keeps the battle room's own choice.

`game.role` is `host` or `join`. A file that leaves it out needs `--host` or
`--join ADDRESS` on the command line. A host plays over TCP/IP. A player who
joins needs `game.address`, or `--join ADDRESS`.

Each item of `computer-players` is a group of `side` (`arm` or `core`),
`team` (`1` to `5`, or `none`), `colour` (`1` to `10`) and `difficulty`
(`easy`, `medium` or `hard`). Any but the side may be left out, and the
battle room then chooses it as it does for a computer player a click seats:
no team, the next free colour, and hard. A refusal names an item by its
place, counting from 1 (`computer-players[2].team`). A deathmatch or a
tournament game seats no computer players, and the computer players and the
player must fit `game.player-limit`.

`rules.unit-restrictions` is a group of units and their limits, each unit by
its FBI UnitName in any case: `0` cannot be built, `1` to `100` is the most
each player may build, and `no-limit` lifts the limit, as the battle room's
Restrictions does. A unit the game's content does not have is left out.

`content` chooses what this run plays, as the Library's choice would, and
only for this run. `base-game` is the base game's id, `expansions` a list of
expansion ids, which needs a base game, and `mod` a mod's id, or the path of
a mod folder or a mod profile. If this player does not have the base game,
an expansion or the mod, the game ends before a window opens and the line
says which. `--content`, `--base-game`, `--mod` and `--mod-dir` beside the
file replace that choice.

`map` is the map's shown name, or its key (`name@package` for a pack map). A
map that is not installed is refused before a window opens
(`host.oalaunch: map: "Acid Foursum" is not an installed map`). The game
does not open on a different map.

`status` turns on the status stream. `standard-output: on` is
`--status-stdout`, `file` is `--status-file`, `quit-after-game: on` is
`--quit-after-game` and `after-score` is `--quit-after-game=after-score`.
The options beside the file replace the field they name. A launch from
standard input sets the status stream too.

## Example

A host on Acid Foursome for four players, Core on team 1, with two computer
players and the made-up mod Ridge. The GUID in the example is Total
Annihilation's own; a launcher such as TA Forever passes the GUID it uses
for Ridge.

```yaml
oalaunch: 1
game:
  role: host
  name: Example game
  password: secret
  player-limit: 4
  launcher-name: Ladder
  tournament: off
  closed: off
  directplay:
    application-guid: "{99797420-F5F5-11CF-9827-00A0241496C8}"
    port: 47624
player:
  name: Commander
  side: core
  team: 1
  colour: 2
map: Acid Foursome
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
  unit-restrictions:
    CORKROG: 0
    ARMFLASH: no-limit
computer-players:
  - side: arm
    team: 2
    colour: 5
    difficulty: hard
  - side: core
    team: 1
content:
  base-game: totala
  expansions: [core-contingency]
  mod: ridge
status:
  standard-output: on
  file: status.jsonl
  quit-after-game: on
```

The battle room then uses commander ends, metal 1000, energy 1000, a unit
limit of 250, true line of sight, a mapped start, random positions, cheats
off, watchers on and a player limit of 4, with the Krogoth out and the Flash
without a limit. The host is Core on team 1 in colour 2, beside an Arm
computer player on team 2 in colour 5 and a Core one on team 1. The map is
Acid Foursome. The same launch on the command line:

```sh
open-annihilation --host --game-name "Example game" --game-password secret \
    --player-limit 4 --launcher-name Ladder --player-name Commander --side core --team 1 \
    --colour 2 --map "Acid Foursome" --commander-death ends --metal 1000 --energy 1000 \
    --unit-limit 250 --line-of-sight true --mapping mapped --start-positions random \
    --cheats off --watchers on --unit-restriction CORKROG=0 \
    --unit-restriction ARMFLASH=no-limit --computer-player arm:2:5:hard \
    --computer-player core:1
```

## TA Forever's file

TA Forever's file is a `[totala]` group of `key=value` lines. The keys may
be written in any case. Blank lines, and lines whose first character is `;`
or `#`, are skipped. Another section is skipped. A section without a closing
bracket is refused. The file hosts over TCP/IP, and its keys fill these
fields:

<!-- netgame-launch-help checks that this table is the table of TA
Forever's keys in src/app/netgame/launch_file.cpp, word for word. -->

| Key | Field | What the battle room uses |
|---|---|---|
| `watching` | `rules.watchers` | `2` allows watchers. Any other whole number is kept as written. |
| `mapmode` | `rules.mapping` | `1` starts the map unmapped, `2` mapped. Kept as written. |
| `location` | `rules.start-positions` | `1` uses fixed start positions, `2` random. Kept as written. |
| `cheats` | `rules.cheats` | `2` allows the cheat codes. `1` turns them off. Kept as written. |
| `los` | `rules.line-of-sight` | `1` true line of sight, `2` circular, `3` permanent. |
| `deathopt` | `rules.commander-death` | `1` the commander continues, `2` the game ends, `3` deathmatch. |
| `metal` | `rules.metal` | Starting metal, from 0 through 10000. 0 leaves the battle room's own amount. |
| `energy` | `rules.energy` | Starting energy, from 0 through 10000. 0 leaves the battle room's own amount. |
| `lockOptions` | `rules.lock-options` | `1` locks the battle room's option controls. |
| `playerlimit` | `game.player-limit` | Players allowed, from 2 through 10. 0 leaves the session uncapped. 1 is refused. |
| `pfs` | none | Kept as written and passed on with the score report. |
| `timelimit` | `rules.time-limit` | Kept as written and passed on with the score report. |
| `session` | `game.name` | The game's name, at most 63 characters. |
| `provider` | `game.launcher-name` | The launcher's name on the battle room's previous-menu caption, at most 15 characters. |
| `mission` | `map` | The map, by the name the list shows or by its key, at most 63 characters. |
| `registeredonly` | none | Read and ignored. |
| `tournment` | `game.tournament` | A tournament game. When it and `tournament` are both written, the later one is kept. |
| `tournament` | `game.tournament` | A tournament game. When it and `tournment` are both written, the later one is kept. |

A key in `[totala]` that is not in this table is refused, and the line
names the key as the file wrote it. `--join ADDRESS` beside the file joins
that address instead of hosting.

## What is installed

`--list-content --json` prints the installed maps, mods and expansions as
one JSON object and exits with status 0. It does not open a window. The
maps are sorted by key, and the mods and expansions by id.

```json
{"maps":[{"shown-name":"Acid Foursome","key":"Acid Foursome","players":4,"size":"13 x 13"}],"mods":[{"id":"ridge","name":"Ridge"}],"expansions":[{"id":"core-contingency","name":"Core Contingency"}]}
```

A map's `shown-name` is the name players see, `key` the name a launch file
uses, `players` how many it is for and `size` its width and height. A mod
or expansion has its `id` and the `name` players see.

`--list-content` without `--json`, or `--json` without `--list-content`,
is refused.
