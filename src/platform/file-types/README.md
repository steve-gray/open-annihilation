# platform/file-types

Makes the running copy of the game the opener of the four file types for
the user who runs it — a mod package (`.oamod`), a language pack
(`.oalang`), a map pack (`.oamap`) and a registry file (`.oareg`) — with
the game's icon on those files where the system takes one, so that opening
one in the file manager starts the game with it. On Windows and Linux the
game does this at run time, writing only what differs from what is there;
macOS and iOS read it from the bundle's `Info.plist` instead
([src/app/Info.plist.in](../../app/Info.plist.in),
[platforms/ios/Info.plist.in](../../../platforms/ios/Info.plist.in)), and
nothing is done at run time there.

`oa/platform/file_types.hpp` (namespace `oa::platform::file_types`):

- `running_executable()` returns the program's path as the system would
  start it again: the module's file name on Windows, `/proc/self/exe` on
  Linux. It gives none on macOS and iOS, and none on Linux once the
  executable was deleted or replaced since it started.
- `TypeSet` names which of the four a call acts on. Each is on unless the
  caller turns it off, so a call that passes none acts on all four.
  `complement()` is the types it does not hold.
- `register_file_types(executable, icon_png, places, types)` registers the
  program for `types` on Windows and Linux and says what it did in a
  `Registration`: whether this system registers at run time, whether
  anything was written, a line for the log for each thing written, skipped
  or run, and the error that stopped it. It never throws. `Places` names
  where it writes, so that the tests write into scratch folders and keys.
  On Linux the MIME package and the desktop entry then list exactly
  `types`. A type left out loses its mimetype icon. With none of the four,
  the desktop entry and the MIME package are removed; the program's icon
  stays.
- `withdraw_file_types(types, places)` withdraws those types. On Windows a
  value is this game's, and is removed, only when it is the type's
  program-identifier key, the `OpenWithProgids` value whose name is that
  program identifier, or the extension's (Default) or `Content Type` while
  that value's data is still the game's program identifier or MIME type.
  Another program's values, and the player's `UserChoice`, stay. On Linux
  it takes those types out of the package and the desktop entry and removes
  their mimetype icons, and removes the entry and the package once neither
  lists a type. The program's icon stays. A missing value is already
  withdrawn. macOS and iOS withdraw nothing at run time.
- The texts it writes: `desktop_entry`, `mime_package`,
  `desktop_exec_argument`, `open_command` and `default_icon`.
- `file_types` names the four, each with its extension, MIME type, the name
  file managers show, its Windows program identifier and the MIME type it
  is a kind of (`application/zip` for the three packages, `text/plain` for
  `.oareg`).

## Windows

Under `HKEY_CURRENT_USER\Software\Classes`, six string values for each of
the four file types, 24 in all:

| Key | Value | Data |
|---|---|---|
| `.<extension>` | (Default) | the type's program identifier |
| `.<extension>` | `Content Type` | the type's MIME type |
| `.<extension>\OpenWithProgids` | the program identifier | empty |
| `<program identifier>` | (Default) | the type's name |
| `<program identifier>\DefaultIcon` | (Default) | `<exe>,0`, the executable's first icon |
| `<program identifier>\shell\open\command` | (Default) | `"<exe>" --open "%1"` |

| Extension | Program identifier | MIME type | Name |
|---|---|---|---|
| `.oamod` | `OpenAnnihilation.Mod` | `application/x-oamod` | Open Annihilation mod |
| `.oalang` | `OpenAnnihilation.Language` | `application/x-oalang` | Open Annihilation language pack |
| `.oamap` | `OpenAnnihilation.MapPack` | `application/x-oamap` | Open Annihilation map pack |
| `.oareg` | `OpenAnnihilation.Registry` | `application/x-oareg` | Open Annihilation registry |

Each value is read first and written only when it is missing or differs.
A registration of some of the four writes those types and leaves the others
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
Windows XP has are used. Removing the eight keys `.oamod`,
`OpenAnnihilation.Mod`, `.oalang`, `OpenAnnihilation.Language`, `.oamap`,
`OpenAnnihilation.MapPack`, `.oareg` and `OpenAnnihilation.Registry` forgets
the registration.

## Linux

In the user's XDG data folder (`$XDG_DATA_HOME` when it is absolute, else
`~/.local/share`), seven files, each written only when its bytes differ,
through a temporary file renamed over it:

| File | What |
|---|---|
| `mime/packages/net.coreprime.open-annihilation.xml` | the registered MIME types, each with its comment, its parent and its glob: the packages a kind of `application/zip`, `.oareg` a kind of `text/plain`. A type left out is not in the package |
| `applications/net.coreprime.open-annihilation.desktop` | the opener: `Exec="<exe>" %f`, `MimeType=` listing only the registered types (`application/x-oamod;application/x-oalang;application/x-oamap;application/x-oareg;` when all four are), `NoDisplay=true` |
| `icons/hicolor/256x256/apps/net.coreprime.open-annihilation.png` | the program's icon |
| `icons/hicolor/256x256/mimetypes/application-x-oamod.png` | the mod packages' icon |
| `icons/hicolor/256x256/mimetypes/application-x-oalang.png` | the language packs' icon |
| `icons/hicolor/256x256/mimetypes/application-x-oamap.png` | the map packs' icon |
| `icons/hicolor/256x256/mimetypes/application-x-oareg.png` | the registry files' icon |

The desktop entry opens a file as a bare argument naming one of the four
file types, which the game opens; started with no file, the entry starts
the game as usual. `NoDisplay=true` keeps it out of the application menus:
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
program's icon is never removed. With all four off, registering the empty
set removes the desktop entry and the MIME package the same way. The stamp
rules are a write's: a file whose bytes change loses its stamp first, and
a removed file loses it too.

## Tests

`platform-file-types` checks the texts on every system. Elsewhere than on
Windows it registers into a scratch data folder, with three recording
scripts standing in for the tools on a scratch `PATH`, given two minutes
each so that a loaded machine never stops them: the five icon files, the
MIME package and the desktop entry are written with their bytes and each
tool runs once, the next run writes and runs nothing, a moved executable
rewrites the desktop entry alone, a new icon with an icon cache runs the
cache tool, missing tools are no error and run at the next start that has
them, a tool that hangs is stopped at a one-second limit and runs again at
the next start, and the starts that register nothing write nothing.
`test_xdg_subset_and_withdrawal` registers two of the four and checks that
the package, the desktop entry and the icons hold only those, withdraws one
and checks that the other remains, and with all four off checks that the
desktop entry and the MIME package are gone and the program's icon remains.
On
Windows it registers under a scratch key of `HKEY_CURRENT_USER` and checks
the 24 values, that the next run writes nothing, that a moved executable
rewrites two values per type, and that a copy in the temporary folder
writes nothing. `test_windows_withdrawal` registers two types, writes
another program's (Default), `Content Type` and `OpenWithProgids` value on
one extension and a `UserChoice` key beside them, withdraws one type and
checks that the other type and the other program's values are unchanged,
then removes the scratch key. `app-bundle-file-types`
(`tools/check_bundle_file_types.py`) checks the two `Info.plist` templates.

## Limitations

- Windows XP draws no PNG-compressed icon, and every entry of the
  executable's icon is one, so on XP neither the program nor one of the
  four file types shows the game's icon.
- A registration only follows the copy that ran last: two copies in
  different folders take the files from each other at each start.
- Nothing is removed when the game is removed; the installation pages say
  which keys and files to remove.
