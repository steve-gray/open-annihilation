# platform/file-types

Makes the running copy of the game the opener of the five file types for
the user who runs it — a mod package (`.oamod`), a language pack
(`.oalang`), a map pack (`.oamap`), a registry file (`.oareg`) and a launch
file (`.oalaunch`) — with the game's icon on those files where the system
takes one, so that opening one in the file manager starts the game with it.
The first four the game installs or adds (`Opening::install`); a launch file
starts the game in that launch's battle room, as `-c FILE` does
(`Opening::launch`). On Windows and Linux the game does this at run time,
writing only what differs from what is there. macOS and iOS declare the
types in the bundle's `Info.plist`
([src/app/Info.plist.in](../../app/Info.plist.in),
[platforms/ios/Info.plist.in](../../../platforms/ios/Info.plist.in)). On
macOS a switch in Settings chooses whether this copy is the default opener
of each of the four; a start never replaces a choice made in Finder. A
launch file has no switch: it is registered whenever the game registers,
and on macOS the bundle is its owner. iOS does nothing at run time.

`oa/platform/file_types.hpp` (namespace `oa::platform::file_types`):

- `running_executable()` returns the program's path as the system would
  start it again: the module's file name on Windows, `/proc/self/exe` on
  Linux. It gives none on macOS and iOS, and none on Linux once the
  executable was deleted or replaced since it started. macOS names the
  running bundle with `running_application()` instead.
- `TypeSet` names which of the five a call acts on. Each is on unless the
  caller turns it off, so a call that passes none acts on all five; Settings
  has a switch for each of the four it installs or adds, and leaves
  `launch` on. `installs_any()` tells whether it holds one of those four,
  and `complement()` is the types it does not hold.
- `register_file_types(executable, icon_png, places, types)` registers the
  program for `types` on Windows and Linux and says what it did in a
  `Registration`: whether this system registers at run time, whether
  anything was written, a line for the log for each thing written, skipped
  or run, and the error that stopped it. It never throws. `Places` names
  where it writes, so that the tests write into scratch folders and keys.
  On Linux the MIME package and the two desktop entries then list exactly
  `types`. A type left out loses its mimetype icon. With none of the four
  the game installs or adds, their desktop entry is removed; without a
  launch file, the launch file's entry is; with none of the five, the MIME
  package is too. The program's icon stays.
- `withdraw_file_types(types, places)` withdraws those types. On Windows a
  value is this game's, and is removed, only when it is the type's
  program-identifier key, the `OpenWithProgids` value whose name is that
  program identifier, or the extension's (Default) or `Content Type` while
  that value's data is still the game's program identifier or MIME type.
  Another program's values, and the player's `UserChoice`, stay. On Linux
  it takes those types out of the package and the desktop entries and
  removes their mimetype icons, and removes an entry or the package once it
  lists no type. The program's icon stays. A missing value is already
  withdrawn. macOS and iOS withdraw nothing at run time. On macOS the
  switches call `apply_opener_changes`, which changes the default opener
  and leaves the declaration.
- The texts it writes: `desktop_entry`, `launch_desktop_entry`,
  `mime_package`, `desktop_exec_argument`, `open_command` and
  `default_icon`.
- `file_types` names the five, each with its extension, MIME type, the name
  file managers show, its Windows program identifier, the MIME type it is a
  kind of (`application/zip` for the three packages, `text/plain` for
  `.oareg` and `.oalaunch`) and what the game does with it (`opening`).
- `exported_content_types` names the same five as macOS does: the exported
  type identifier, and the content type a switch that is off hands back to
  (`public.zip-archive` for the three packages, `public.plain-text` for
  `.oareg` and `.oalaunch`).
- `default_opener_is_a_setting()` is true on macOS. There the switches show
  what `reported_openers` reads, and OK calls `apply_opener_changes` for
  the switches that moved. `HandlerCalls` is the two questions that asks,
  so a test can stand in for the system. `system_handler_calls()` is those
  questions on macOS and nothing elsewhere. `running_application()` is this
  copy when it is an application bundle.

## Windows

Under `HKEY_CURRENT_USER\Software\Classes`, six string values for each of
the five file types, 30 in all:

| Key | Value | Data |
|---|---|---|
| `.<extension>` | (Default) | the type's program identifier |
| `.<extension>` | `Content Type` | the type's MIME type |
| `.<extension>\OpenWithProgids` | the program identifier | empty |
| `<program identifier>` | (Default) | the type's name |
| `<program identifier>\DefaultIcon` | (Default) | `<exe>,0`, the executable's first icon |
| `<program identifier>\shell\open\command` | (Default) | `"<exe>" --open "%1"`; for a launch file `"<exe>" -c "%1"` |

| Extension | Program identifier | MIME type | Name |
|---|---|---|---|
| `.oamod` | `OpenAnnihilation.Mod` | `application/x-oamod` | Open Annihilation mod |
| `.oalang` | `OpenAnnihilation.Language` | `application/x-oalang` | Open Annihilation language pack |
| `.oamap` | `OpenAnnihilation.MapPack` | `application/x-oamap` | Open Annihilation map pack |
| `.oareg` | `OpenAnnihilation.Registry` | `application/x-oareg` | Open Annihilation registry |
| `.oalaunch` | `OpenAnnihilation.Launch` | `application/x-oalaunch` | Open Annihilation launch file |

Each value is read first and written only when it is missing or differs.
A registration of some of the five writes those types and leaves the others
as they are; the caller withdraws the rest. Withdrawing a type deletes its
program-identifier key leaf first (`shell\open\command`, then `shell\open`,
`shell`, `DefaultIcon`, then the program identifier) with `RegDeleteKey`,
which stops if an unexpected subkey remains, and deletes the
`OpenWithProgids` value whose name is that program identifier. The
extension's (Default) is deleted only while its data is still that program
identifier, and `Content Type` only while its data is still that MIME type.
The extension key itself stays, and so does every other value on it. The
player's choice of opener, Explorer's `UserChoice`, is never opened. A
missing value is already withdrawn. After any write or withdrawal the shell
is told that a file association changed, so Explorer shows the icon at
once. A copy started from the temporary folder, as one
started from inside a zip in Explorer is, registers nothing and withdraws
nothing. Only calls that
Windows XP has are used. Removing the ten keys `.oamod`,
`OpenAnnihilation.Mod`, `.oalang`, `OpenAnnihilation.Language`, `.oamap`,
`OpenAnnihilation.MapPack`, `.oareg`, `OpenAnnihilation.Registry`,
`.oalaunch` and `OpenAnnihilation.Launch` forgets the registration.

The Windows installers
([packaging/windows/open-annihilation.nsi](../../../packaging/windows/open-annihilation.nsi))
write the launch file's six values under `HKEY_LOCAL_MACHINE\Software\Classes`
for everyone who uses the computer, with the installed
`open-annihilation.exe`, and tell the shell. Uninstalling removes the
program identifier's key and the `OpenWithProgids` value, and the
extension's (Default) and `Content Type` while they still name the game.

## Linux

In the user's XDG data folder (`$XDG_DATA_HOME` when it is absolute, else
`~/.local/share`), nine files, each written only when its bytes differ,
through a temporary file renamed over it:

| File | What |
|---|---|
| `mime/packages/net.coreprime.open-annihilation.xml` | the registered MIME types, each with its comment, its parent and its glob: the packages a kind of `application/zip`, `.oareg` and `.oalaunch` a kind of `text/plain`. A launch file is also known by its content: any text that begins `oalaunch:` (a `magic` match at offset 0). A type left out is not in the package |
| `applications/net.coreprime.open-annihilation.desktop` | the opener of the four the game installs or adds: `Exec="<exe>" %f`, `MimeType=` listing only those registered (`application/x-oamod;application/x-oalang;application/x-oamap;application/x-oareg;` when all four are), `NoDisplay=true` |
| `applications/net.coreprime.open-annihilation.launch.desktop` | the opener of launch files: `Exec="<exe>" -c %f`, `MimeType=application/x-oalaunch;`, `NoDisplay=true` |
| `icons/hicolor/256x256/apps/net.coreprime.open-annihilation.png` | the program's icon |
| `icons/hicolor/256x256/mimetypes/application-x-oamod.png` | the mod packages' icon |
| `icons/hicolor/256x256/mimetypes/application-x-oalang.png` | the language packs' icon |
| `icons/hicolor/256x256/mimetypes/application-x-oamap.png` | the map packs' icon |
| `icons/hicolor/256x256/mimetypes/application-x-oareg.png` | the registry files' icon |
| `icons/hicolor/256x256/mimetypes/application-x-oalaunch.png` | the launch files' icon |

The first desktop entry opens a file as a bare argument naming one of the
four file types the game installs or adds, which the game opens; the
launch file's entry opens it after `-c`, as the start's launch. Started
with no file, either entry starts the game as usual. A launch file opened
while the game runs starts a second copy with it, as any second start
does. `NoDisplay=true` keeps it out of the application menus:
it is an opener that "Open With" lists, not a launcher. After a change the
game runs `update-mime-database` for a new MIME package,
`update-desktop-database` for a new desktop entry, and
`gtk-update-icon-cache` for a new icon where the user's hicolor folder
already holds an icon cache, each found on `PATH` and started without a
shell. A tool that is not installed is no error, and one still running
after five seconds is stopped. A run that succeeds leaves an empty stamp
file, `.net.coreprime.open-annihilation.updated`, in the folder the tool
works on (`mime`, `applications` or `icons/hicolor`); a change to the
tool's files removes it first. While a stamp is missing, after a run that
failed, was stopped or was never made, each start runs that tool again. A
build that starts no other programs (`OA_PROCESS_SPAWNING` off) runs none.
A copy under `$TMPDIR` or `/tmp`, inside a Flatpak or a Snap (which
register their own types), or whose path holds a control character or is
not UTF-8, registers nothing and withdraws nothing. Withdrawing a type
edits the package and the desktop entry that are already there, removes
that type's mimetype icon, and removes a file once it lists no type. The
program's icon is never removed. With all five off, registering the empty
set removes both desktop entries and the MIME package the same way. The stamp
rules are a write's: a file whose bytes change loses its stamp first, and
a removed file loses it too.

## macOS

The bundle keeps the five declarations and their icons. A switch does not
remove one. It chooses the default opener. The bundle owns the launch
file's type (`net.coreprime.open-annihilation.oalaunch`, a kind of
`public.plain-text`), and Settings has no switch for it; the default-opener
questions answer for it as for the others (`TypeSet::launch`;
`apply_opener_changes` hands it back to the opener of plain text). The
bundle also takes `public.plain-text` it is handed without being its opener
(`LSHandlerRank None`), which `open -f -a` needs.

| Switch | On | Off |
|---|---|---|
| `.oamod`, `.oalang`, `.oamap` | this copy opens that exported type | the default opener of `public.zip-archive` |
| `.oareg` | this copy opens that exported type | the default opener of `public.plain-text` |

On every macOS version both questions use
`LSSetDefaultRoleHandlerForContentType` and
`LSCopyDefaultRoleHandlerForContentType`. Those calls are deprecated and
still answer at once; the deprecation warning is accepted only in
`file_types_mac.mm`. After a change the opener is read back. A change that
read does not show is logged (`the change did not take`) and is not
counted as done. A switch shows on when the default opener's bundle
identifier is this copy's. A question that fails is logged, that switch
is left as it was, and the others are still asked.
Nothing here runs at start: a choice made in Finder stays until a switch
moves and OK is pressed. A copy that is not an application bundle asks
nothing. iOS has no such questions, and System keeps the text row.

## Tests

`platform-file-types` checks the texts on every system (the launch file's
own desktop entry, its `-c` command and its magic among them), and the
default-opener questions against a stand-in: a switch that is on asks for
this copy and does not look up a zip or text opener, then reads the opener
back; a switch that is off asks for that opener, sets it on the exported
type and reads it back; a set the read does not show is logged and is not
counted; a type that did not move is not asked, a failed question is
reported and does not stop the others, and a system with no questions
skips. Elsewhere than on
Windows it registers into a scratch data folder, with three recording
scripts standing in for the tools on a scratch `PATH`, given two minutes
each so that a loaded machine never stops them: the six icon files, the
MIME package and the two desktop entries are written with their bytes and
each tool runs once, the next run writes and runs nothing, a moved
executable rewrites the desktop entries alone, a new icon with an icon
cache runs the
cache tool, missing tools are no error and run at the next start that has
them, a tool that hangs is stopped at a one-second limit and runs again at
the next start, and the starts that register nothing write nothing.
`test_xdg_subset_and_withdrawal` registers two of the four and checks that
the package, the desktop entry and the icons hold only those, withdraws one
and checks that the other remains, and with all four off checks that the
desktop entry and the MIME package are gone and the program's icon remains;
with only a launch file on, its entry, its icon and its part of the package
stay, and the entry of the four goes; withdrawn, they go too.
On
Windows it registers under a scratch key of `HKEY_CURRENT_USER` and checks
the 30 values, the launch file's `-c` command among them, that the next run
writes nothing, that a moved executable rewrites two values per type, and
that a copy in the temporary folder writes nothing. `test_windows_withdrawal` registers two types, writes
another program's (Default), `Content Type` and `OpenWithProgids` value on
one extension and a `UserChoice` key beside them, withdraws one type and
checks that the other type and the other program's values are unchanged,
withdraws the launch file alone, then removes the scratch key.
`app-bundle-file-types` (`tools/check_bundle_file_types.py`) checks the two
`Info.plist` templates, the launch file's type and the macOS bundle's plain
text among them.

## Limitations

- Windows XP draws no PNG-compressed icon, and every entry of the
  executable's icon is one, so on XP neither the program nor one of the
  five file types shows the game's icon.
- Settings has no switch for launch files: they stay registered while the
  game registers anything.
- A registration only follows the copy that ran last: two copies in
  different folders take the files from each other at each start.
- Nothing is removed when the game is removed; the installation pages say
  which keys and files to remove.
- On macOS the declaration stays in the bundle after the game is removed,
  until the bundle itself is removed. Off does not restore an opener that
  was neither this copy nor the default opener of a zip or of plain text:
  it hands the type to that default.
