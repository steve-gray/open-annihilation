# oa-tool

`oa-tool` reads archives and images from a game directory, and prints or
writes what a script asks for. Mod authors and translators run it by hand.
The checks under `tools/` run `list` and `extract` and match their output.
`pack` writes a mod or language package. `check` runs the installer's own
checks on a package and reports the facts a catalogue lists. Catalogues and
registries are further commands on the same table.

## How to run it

From a build tree, `build/oa-tool` is the program.

- No arguments prints the command list on standard error and exits 2.
- `oa-tool help`, `oa-tool --help` and `oa-tool -h` print that list on
  standard output and exit 0.
- `oa-tool help COMMAND` and `oa-tool COMMAND --help` (or `-h` before any
  `--`) print that command's usage line and explanation on standard output
  and exit 0. `oa-tool help GROUP SUBCOMMAND` does the same for a
  subcommand.
- A group with no subcommand prints its own list on standard error and
  exits 2.

Options, for a command that has any, may be written before, between or
after the positional arguments. `--name value` and `--name=value` are the
same. `--` ends options. A command takes its options through
`parse_arguments` in `arguments.hpp`.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | The command did what was asked. |
| 1 | The command ran and could not do it, or the answer is no. `Failure`, and any other exception, prints `oa-tool <command>: <message>` on standard error. |
| 2 | The invocation is not one the tool can run: no command, an unknown command, or the wrong number of arguments. The line says what is wrong and names `oa-tool help`. |

## Commands

| Command | What it does |
|---|---|
| `list ARCHIVE` | Prints each file in an HPI archive as `path`, a tab and its size in bytes. |
| `extract ARCHIVE ENTRY OUTPUT` | Writes one archive entry to OUTPUT and prints nothing. |
| `asset-extract ROOT ENTRY OUTPUT [ARCHIVE...]` | Writes ENTRY from loose files under ROOT, or from the named archives, and prints where it was read. A loose file wins. |
| `preview ARCHIVE PCX_ENTRY OUTPUT.ppm\|OUTPUT.png` | Decodes a PCX image from an archive as a PPM file, or a PNG file when the path ends in `.png`, and prints its size. |
| `decode-pcx INPUT.pcx OUTPUT.ppm\|OUTPUT.png` | Decodes a PCX file the same way. |
| `pack FOLDER [--out FILE] [--force]` | Packs FOLDER as a `.oamod` or a `.oalang`. The manifest is first, then every other file in byte order of its path. A file whose extension, without case, is png, jpg, jpeg, gif, ogg, mp3, zip, gz, bz2, xz, 7z, oamod, oalang or oamap, and an empty file, is stored; every other file is deflated. The same files give the same bytes. `--force` replaces an existing file. |
| `check FILE [--game-dir DIR] [--accept-unimplemented-hacks] [--json]` | Runs the checks an install runs before it writes, and reports the facts a catalogue lists. `--json` prints one JSON object. Otherwise each fact is a `key: value` line and the last line is `result: ok` or `result: refused`. Exit 0 when the package would be installed, 1 when it would be refused. |

## Entry points

- `run_tool` in `command.hpp` turns an argument list into one command. The
  program's `main` passes it the process arguments and the standard streams.
  The overload that takes a table runs that table instead, which is how a
  test drives a group the tool does not ship.
- `commands` returns the table in `commands.cpp`.
- `parse_arguments` splits options from positional arguments.
- `read_file`, `write_file` and `write_image` in `files.hpp` move bytes and
  images. Each throws `Failure` when it cannot.

## State

A run reads the archives, loose files and images named on its command line
and writes the output file a command names. It keeps nothing after it exits.
Commands print only through the `Output` they are given, so a test can
capture every line.

## Invariants

- `list`, `extract`, `asset-extract`, `preview` and `decode-pcx` print the
  same bytes on a successful run as they did before the command table, and
  that run exits 0.
- `pack` writes the manifest first and the other files in byte order of
  their paths, stored or deflated by the rule in its help, and the same
  files give the same bytes.
- `check` accepts a package exactly when an install would, and reports
  `sim_hash` and `full_hash` from the profile resolved without the packaged
  settings. A missing fact is left out; `revision` is null only when a
  language pack has no packaging revision.
- The exit code is 0, 1 or 2.
- Help lists commands in the table's order, and a group's subcommands under
  the group.
- A repeating option keeps every value, in the order given. An option that
  does not repeat, and every flag, is refused the second time.

## Adding a command

1. Write its `run` function in a `.cpp` file under `tools/oa-tool`, and
   declare that function in `command.hpp` with the others from the same file.
2. Add its row to the table in `commands.cpp`: the name, the usage arguments,
   a one-line summary, a paragraph of help, how many arguments it takes and
   the function. A group leaves the function null and names its subcommands.
   A command whose options make the argument count vary sets the upper bound
   to `any_count` and checks the arguments itself with `parse_arguments`.
   Declare a repeating option with `repeats`; the parser already keeps every
   value.
3. Add the `.cpp` file to `oa-tool-commands` in `tools/oa-tool/CMakeLists.txt`.
4. Cover the command from `tools/oa-tool/tests/oa_tool_test.cpp`, or from
   the command's own test when it builds packages. The tests run commands
   in-process through `run_tool`, with streams of their own. `pack` and
   `check` are covered by `pack_test.cpp` and `check_test.cpp`.

## Tests

`tools-oa-tool` (`oa-tool-test`) builds an archive in memory, runs every
archive command, checks help and the usage failures, checks `parse_arguments`,
and dispatches a group through `run_tool`'s table overload.

`tools-oa-tool-pack` (`oa-tool-pack-test`) packs a made-up mod and the pseudo
language pack, checks the order and the methods, packs again after the
files' times change, and checks the refusals. Its argument is the pseudo
pack's folder.

`tools-oa-tool-check` (`oa-tool-check-test`) checks a made-up mod package,
its refusals, and a language manifest read on its own. Its argument is the
pseudo pack's `language.yaml`.

## Limitations

The archive commands take no options. `pack` takes `--out` and `--force`.
It does not pack a map. `check` takes `--game-dir`,
`--accept-unimplemented-hacks` and `--json`. It does not describe a map.
Catalogues and registries are further commands, not part of this table yet.
The macOS package does not ship `oa-tool`.
