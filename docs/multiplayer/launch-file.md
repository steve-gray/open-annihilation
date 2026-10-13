# Launch files

A launcher can start a network game with the map, the rules, the content
and the player's seat already chosen. The battle room then opens ready.

Two files do this. TA Forever's file is the one it already writes, named
with `-c`. Open Annihilation's own file is named with `--launch`. An
option on the command line beside either file replaces the matching field,
whichever is written first. The password is used for the game and is never
written to the log. A line that refuses the file does not repeat it.

A file the game cannot use ends the game before a window opens. The exit
status is 2. The line names the file, the field and why, for example
`rules.metal: 250000 is above 10000`.

## `-c FILE`

`-c FILE` reads TA Forever's settings file and hosts. The connection is
TCP/IP and the intro is skipped. The file is a `[totala]` group of
`key=value` lines. The keys may be written in any case. Blank lines, and
lines whose first character is `;` or `#`, are skipped. Another section is
skipped. A section without a closing bracket is refused.

| Key | What the battle room uses |
|---|---|
| `watching` | `2` allows watchers. Any other whole number is kept as written. |
| `mapmode` | `1` starts the map unmapped, `2` mapped. Kept as written. |
| `location` | `1` uses fixed start positions, `2` random. Kept as written. |
| `cheats` | `2` allows the cheat codes. `1` turns them off. Kept as written. |
| `los` | `1` true line of sight, `2` circular, `3` permanent. |
| `deathopt` | `1` the commander continues, `2` the game ends, `3` deathmatch. |
| `metal`, `energy` | Starting metal and energy, from 0 through 10000. 0 leaves the battle room's own amount. |
| `lockOptions` | `1` locks the battle room's option controls. |
| `playerlimit` | Players allowed, from 2 through 10. 0 leaves the session uncapped. 1 is refused. |
| `pfs`, `timelimit` | Kept as written and passed on with the score report. |
| `session` | The game's name, at most 63 characters. |
| `provider` | The launcher's name on the battle room's previous-menu caption, at most 15 characters. |
| `mission` | The map, by the name the list shows or by its key, at most 63 characters. |
| `registeredonly` | Read and ignored. |
| `tournment`, `tournament` | A tournament game. When both are written, the later one is kept. |

A key in `[totala]` that is not in this list is refused, and the line
names the key as the file wrote it. `-c` needs a file. `-c` and
`--launch` cannot both be given.

`--join ADDRESS` beside `-c` joins that address instead of hosting. The
file's rules are kept, and a player who joins applies only the seat. The
rules come from the host. `--player-name`, `--game-name`,
`--game-password`, `--dplay-guid`, `--dplay-port`, `-h` and `-n` replace
the fields they name.

## `--launch FILE`

`--launch FILE` reads Open Annihilation's own file. `--launch -` reads it
from standard input. The file is YAML and begins with `oalaunch: 1`. A
missing header, or any other value, is refused. Keys the file does not
list below are refused, and the line names the field (`rules.nope`).

`game.role` is `host` or `join`. A file that leaves it out needs `--host`
or `--join` on the command line. A host plays over TCP/IP. A join uses
`game.address`.

`game.name` is the game's name, at most 63 characters. `game.player` is
the player's name and `game.password` the game's password, each at most
31 characters. `game.dplay.guid` is the application GUID, with or without
braces, as [`--dplay-guid`](../command-line.md) takes it. `game.dplay.port`
is the DirectPlay port, from 1 through 65535.

`content` chooses what this run plays, as the Library's choice would, and
only for this run:

- `base-game` is the base game's id.
- `expansions` is a list of expansion ids. It needs a base game.
- `mod` is a mod's id, or the path of a mod folder or a mod profile.

If this player does not have the base game, an expansion or the mod, the
game ends before a window opens and the line says which.
`--content`, `--base-game`, `--mod` and `--mod-dir` beside the file
replace that choice. An empty `content` group names nothing.

`map` is the map's shown name, or its key (`name@package` for a pack map).
The host's battle room selects that map. A map that is not installed is
refused before a window opens (`map: "Acid Foursum" is not an installed
map`). The game does not open on a different map.

`rules` are the battle room's rules. A host applies them. A player who
joins does not: the host's rules are the ones that play.

| Key | Values |
|---|---|
| `commander` | `continues`, `ends` or `deathmatch`. |
| `metal`, `energy` | A whole number from 0 through 10000. 0 leaves the battle room's own amount. |
| `unit-limit` | A whole number from 20 through 6553. |
| `line-of-sight` | `true`, `circular` or `permanent`. The word `true` may be written as a boolean. A boolean false is refused. |
| `mapping` | `mapped` or `unmapped`. |
| `start-positions` | `random` or `fixed`. |
| `cheats`, `watchers` | `true` or `false`. |
| `player-limit` | A whole number from 2 through 10. |
| `time-limit` | A whole number, kept as written. |

`lock-options` is `true` or `false`. True locks the battle room's option
controls for a host.

`seat.side` is `arm`, `core` or `random`. `seat.team` is a whole number
from 1 through 5, or `none`. The host and a player who joins both take
the seat. `random` and `none` leave the side and the team for the battle
room to choose.

`status` turns on the status stream ([status.md](status.md)).
`stdout: true` is `--status-stdout`. `file` is `--status-file`.
`quit-after-game: true` is `--quit-after-game`, and `after-score` is
`--quit-after-game=after-score`. `false` leaves that part off.
`--status-stdout`, `--status-file` and `--quit-after-game` beside the
file replace the field they name. `--launch -` reads the game, the map
and the rules from standard input. The status block of that stream is
not applied.

| Key | Values |
|---|---|
| `stdout` | `true` or `false`. |
| `file` | A path the launcher asked to be written. |
| `quit-after-game` | `true` leaves when the game ends, `false` stays, `after-score` leaves after the score. |

`--host`, `--join`, `--player-name`, `--game-name`, `--game-password`,
`--dplay-guid` and `--dplay-port` beside the file replace those fields.
`-h` sets the name of the game. `-n` sets the connection.

## Example

A host on Acid Foursome, four players, Arm on team 1, with the made-up
mod Ridge. The GUID in the example is Total Annihilation's own; a
launcher such as TA Forever passes the GUID it uses for Ridge.

```yaml
oalaunch: 1
game:
  role: host
  address: 192.0.2.10
  name: Example game
  player: Commander
  password: secret
  dplay:
    guid: "{99797420-F5F5-11CF-9827-00A0241496C8}"
    port: 47624
content:
  base-game: totala
  expansions: [campaign, core-contingency]
  mod: ridge
map: "Acid Foursome"
rules:
  commander: ends
  metal: 1000
  energy: 1000
  unit-limit: 250
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
status:
  stdout: true
  file: status.jsonl
  quit-after-game: true
```

The battle room then uses commander ends, metal 1000, energy 1000, a unit
limit of 250, true line of sight, a mapped start, random positions, cheats
off, watchers on and a player limit of 4. The map is Acid Foursome.

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
