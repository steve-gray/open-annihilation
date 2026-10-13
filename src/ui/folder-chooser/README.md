# Folder chooser

The in-engine locator (`oa/ui/folder_chooser.hpp`, `oa::ui::folder_chooser`):
the screen that asks for the Total Annihilation folder when the system's
picker cannot. It is driven by touch, a pointer, the keys or a gamepad, and
has no SDL and no app header. The app (`folder_chooser_screen.cpp` in
[src/app](../../app/README.md)) fills a `Model`, lays it out with `lay_out`
and paints it with the Game files screen's painter. One usable folder found
is still played without asking. On macOS, Windows and a Linux desktop whose
folder dialog opens, that dialog asks. The locator opens in Steam Deck Game
Mode, in a build with no dialog, and when the dialog cannot open. Game Mode
does not offer **Use the system picker…**, and shows pad hints instead
(A open, X choose, Y filter, B back). Where the button is offered, it opens
the system's dialog.

The header says what to choose, and a line under it says what qualifies: a
folder that holds `totala1.hpi`, or a Commander Pack or the 1997 demo
installer. Each row is an icon, a name and a size, folders first. Typing
filters those names, and the filter shows the text. The list sits on a panel,
with the standard buttons in a footer.

`describe_source` says what a file or a folder is, for the side panel. A
Commander Pack installer is recognised by the size in the built-in bundle
table. The 1997 demo's installer is recognised when its size is the
`demo_installer_bytes` the caller passes. A folder is a game when a file
inside it matches a bundle's file by name and size. The side panel names it
("Total Annihilation", "GOG installer (macOS)", "TA demo installer") and
lists the bundles it holds. The checksum is checked later, when the game is
read out.

## Entry points

- `describe_source(name, size, directory, children, demo_installer_bytes)`
  returns the column, the panel and whether the player can use it.
- `order_entries` puts folders before files, then orders names with A–Z
  folded. `name_matches` matches a filter against the name only.
- `lay_out` lays out the title, the line that says what qualifies, the
  sidebar (places, found installs, volumes, games), the folder's rows, the
  selection's panel and the footer. A window whose shorter side is under 600
  points, or one too narrow for the panel, hides the panel. Every control is
  at least 44 points high. The rows scroll; the sidebar, the panel and the
  buttons stay put. On a Steam Deck the rows are taller and the system picker
  is absent.
- `key` moves the focus. The first key shows it. Enter activates the focused
  target (a folder opens, a file is selected). Escape goes up a folder, or
  cancels. The choose key uses the selection whatever has the focus, when
  the accent button is enabled. The filter key asks the app to take typed
  text. Page keys scroll.
- `press_down`, `press_up`, `hit_test` and `scroll` are the pointer, touch
  and wheel.

Every word the module shows goes through `oa::data::languages::interface_text`.
The English list is `interface_words.hpp`.

Pictures of the locator at the desktop, Steam Deck and phone sizes are in
[docs/images/folder-chooser](../../../docs/images/folder-chooser).

## Tests

`ui-folder-chooser` checks recognition (the three Commander Pack installers,
the 1997 demo installer, a game folder, a demo folder, an expansion that
cannot be played on its own, a plain file and an archive), the order of rows
and the name filter, the desktop and Deck layouts (the system picker only on
the desktop, pad hints on the Deck, the side panel hidden on a phone, the
title and the qualifying line, a row's icon and size, every control at least
44 points high, only the rows in the scroll group) and selection (Tab onto a
row, the choose key, the filter key, Escape up or cancel, a press that
activates a row, and the scroll's limits). Each of those layouts at the Steam
Deck's size and at a phone's size keeps the digest `tests/layout-digests.txt`
records.
