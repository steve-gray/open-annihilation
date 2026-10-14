# What a game reports as it is played

A launcher that starts a network game can ask the game to say what is
happening, and how the game ended for each player, on its standard
output. The launcher reads that output. It does not have to watch the
window, and it does not have to open a connection to the game.

```sh
open-annihilation --status-stdout --quit-after-game --host
```

`--status-stdout` writes one JSON object a line, and flushes each line as
it is written. Nothing else is written there: the game's own log goes to
the log folder (or to the folder `--log-dir` names). `--status-file PATH`
writes the same lines to a file. Give both and both receive every line.
`--status-file` with no file, or a file the game cannot open, is refused
and the game ends as it does for any other refused option, before the
first line. `--quit-after-game` leaves once the game has ended.
`--quit-after-game=after-score` waits until the player has left the score
screen. A word after `--quit-after-game` is not part of the option.

A launch file's `status` group turns the same options on
([launch files](../command-line.md#launch-files)). The command line
replaces one field of that group at a time. A launch read from standard
input turns them on too: the lines then go to standard output while the
launch came in on standard input.

The lines use the same events as the automation endpoint
([automation.md](../automation.md)). `hello` and `exit` only frame the
stream.

## The first and last lines

The first line is hello. `version` is the line `--version` prints, with
no newline of its own. `pid` is this game's process. `status` is 1.

```json
{"event":"hello","status":1,"version":"Open Annihilation 0.8.0 (macos-universal arm64, 2ac0217b)","pid":41877}
```

Every stream that writes hello ends with exit, and nothing is written
after it. `reason` is one of `game-ended`, `left`, `connection-lost`,
`quit` and `refused`.

With `--quit-after-game`, `code` is 0 when the game ended and 3 when the
player left or lost the connection, and the process exits with that code.
Without the option, `code` is the code the process is already going to
return, and the process stays running until the player leaves it.

```json
{"event":"exit","reason":"game-ended","code":0}
```

An option refused as the command line is read ends the game before
hello, with code 1, or 2 for a launch's value, and writes no line. A
launch refused after hello, because its map, base game, expansion or mod
is not installed, ends the stream with `refused` and code 2, the code the
process exits with, with or without `--quit-after-game`. The refusal
itself goes to the log ([exit statuses and
refusals](../command-line.md#exit-statuses-and-refusals)).

```json
{"event":"exit","reason":"refused","code":2}
```

## What happens during the game

Every other line is an event. It carries `event`, `seq`, `frame` and
`tick`, then its own members. `seq` counts the events of this stream,
starting at 1. `frame` counts the frames of this run. `tick` is the
match's tick, or 0 while no match is running. The kinds are `screen`,
`player`, `ready`, `chat`, `loaded`, `match`, `pause`, `speed`,
`alliance` and `game_over`, the same kinds the automation endpoint sends
and the same members, with the additions below.

`screen` names the screen now showing. The first one has `previous`
null. A dialog over the screen is listed in `dialogs`.

`match` says `started` when the match is on screen. When the match ends
it says `ended`, and carries `results`, `winner` and `tick`, taken while
the match was still there. That `tick` is the same number the line
already named.

`game_over` says how the game ended for the player on this machine:
`outcome` is `victory` or `defeat`, and `winner` is a player's name, or
null. That winner is this machine's own reading: the local player when
they won, otherwise the one player still with units, otherwise null.
The line then carries `results` and `tick`, which are the same on every
machine. It does not carry a second `winner`. A line can name `tick`
twice; both numbers are the match's tick.

`results` is one entry for each player who took a seat, left a seat, or
produced a unit:

| Member | Meaning |
|---|---|
| `slot` | the seat, 0 to 9 |
| `name` | the player's name |
| `team` | the team's number, or null when the player is on no team |
| `side` | the side's number, the same number a match answer gives, not the side's name |
| `outcome` | `won`, `lost`, `drawn`, `left` or `watcher` |
| `units` | units still on the field |
| `kills` | units destroyed |
| `losses` | units lost |

A watcher is a watcher. A player who left is `left`, even if units
remain, and does not win. Everyone else is grouped with the players on
the same team and with the players they are allied with in both
directions. When one of those groups still has units, its players won,
including a teammate who has none left, and the other players lost. When
none or more than one group still has units, those players drew. On a
`match` line whose change is `ended`, `winner` is the name of the one
player who still has units, or null when several do or none does. That
can differ from `game_over`'s `winner`: a team that wins together has no
single winner on the `match` line.

The lines do not carry a digest of the match.

## A skirmish

Two players of the mod Ridge, Pilot and Brute. Pilot wins. The stream,
with the frames and ticks of one run, is:

```json
{"event":"hello","status":1,"version":"Open Annihilation 0.8.0 (macos-universal arm64, 2ac0217b)","pid":41877}
{"event":"screen","seq":1,"frame":1,"tick":0,"name":"main_menu","previous":null,"dialogs":[]}
{"event":"screen","seq":2,"frame":40,"tick":0,"name":"single_player","previous":"main_menu","dialogs":[]}
{"event":"screen","seq":3,"frame":80,"tick":0,"name":"skirmish","previous":"single_player","dialogs":[]}
{"event":"screen","seq":4,"frame":120,"tick":0,"name":"loading","previous":"skirmish","dialogs":[]}
{"event":"loaded","seq":5,"frame":120,"tick":0,"player":null,"slot":null,"progress":40,"local":true}
{"event":"screen","seq":6,"frame":200,"tick":0,"name":"match","previous":"loading","dialogs":[]}
{"event":"match","seq":7,"frame":200,"tick":0,"change":"started"}
{"event":"game_over","seq":8,"frame":900,"tick":1200,"outcome":"victory","winner":"Pilot","results":[{"slot":0,"name":"Pilot","team":null,"side":0,"outcome":"won","units":12,"kills":8,"losses":3},{"slot":1,"name":"Brute","team":null,"side":1,"outcome":"lost","units":0,"kills":3,"losses":8}],"tick":1200}
{"event":"match","seq":9,"frame":940,"tick":1200,"change":"ended","results":[{"slot":0,"name":"Pilot","team":null,"side":0,"outcome":"won","units":12,"kills":8,"losses":3},{"slot":1,"name":"Brute","team":null,"side":1,"outcome":"lost","units":0,"kills":3,"losses":8}],"winner":"Pilot","tick":1200}
{"event":"exit","reason":"game-ended","code":0}
```

Pilot's side is 0 and Brute's is 1, the sides' numbers, not their names.
Neither is on a team, so `team` is null. Had they been on one team
against a third player, both would be `won` and the ended line's
`winner` would be null.

`--quit-after-game=after-score` writes the same lines and then waits for
the player to leave the score screen before `exit`.
