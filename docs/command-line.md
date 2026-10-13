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
