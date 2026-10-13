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
