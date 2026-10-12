# Map packs

A map pack adds maps to a game. It is one `.oamap` file: a manifest,
`oamap.yaml`, and the maps' own files, their features, the animations and
models those features use, and previews for the map browser. A pack only
adds. It does not carry units, weapons, sounds or textures, and playing one
of its maps loads that map's files alone.

This page is for players: getting a pack, what its maps are called, how
OA decides whether a map fits the game you play, the map browser, and GET
MAP in the battle room.

- [The OAMAP standard](oamap-standard.md) is the format of `oamap.yaml` and
  the rules a map pack follows: the manifest, the index, names, what a pack
  holds, and how a map fits the game it is played with. It is for people
  who make packs.

## Contents

- [Installing a map pack](#installing-a-map-pack)
- [Map names](#map-names)
- [One map's files at a time](#one-maps-files-at-a-time)
- [Whether a map fits](#whether-a-map-fits)
- [The map lists and MORE MAPS…](#the-map-lists-and-more-maps)
- [The map browser](#the-map-browser)
- [GET MAP in the battle room](#get-map-in-the-battle-room)
- [What 3.1c players see](#what-31c-players-see)

## Installing a map pack

A pack comes from a registry or from a file, and installs into the `Maps`
folder of your own Open Annihilation folder, as `Maps/<id>`, named by the
pack's id whatever the file is called
([where that folder is](../installation/macos.md#where-it-keeps-its-files)).

**From a registry**, Core Prime's or one you added: GET on the Library's
Maps tab ([the Library](../library.md)), **Download and select** in the map
browser ([below](#the-map-browser)), or GET MAP in the battle room
([below](#get-map-in-the-battle-room)). A pack new to you, or one that
replaces the copy the same registry installed, installs with no question,
on any screen but a game. When your Maps folder already holds that pack
from a file, made by hand or from another registry, OA asks before it
replaces it, as for a file, whichever registry the download comes from, and
the question waits for the main menu.

**From a file**: double-click an `.oamap` file, choose Open With and Open
Annihilation, drag it onto the game's window, or start the game with its
path. It waits for the main menu, and asks before it writes anything:

| Your Maps folder holds | The question |
|---|---|
| no folder of the pack's id | **INSTALL MAP PACK**: "Install the map pack *name* *version* (*n* maps)?", **CANCEL** or **INSTALL** |
| the same pack and version, another revision | **UPDATE MAP PACK**: replace it with that revision, **CANCEL** or **REPLACE**; it says when the file's revision is the older |
| the same pack, version and revision | **REINSTALL MAP PACK**: install its files again, **CANCEL** or **REINSTALL** |
| the same pack at another version | **REPLACE MAP PACK**, **CANCEL** or **REPLACE** |
| a folder of that name that holds something else | refused: the folder is left as it is |

A replace keeps the copy it replaced in the pack's folder, as `.backup`.
When the pack is in place, **MAP PACK INSTALLED** says where, with **OPEN
FOLDER** and **OK**.

Nothing installs during a game. Every map in a pack is checked against
Total Annihilation 3.1c as it is installed, and a pack with a map that
clashes with 3.1c itself is refused ([whether a map fits](#whether-a-map-fits)):
**MAP PACK NOT INSTALLED** names the file and why. A pack is refused too
when its manifest has errors, when it holds files outside the folders a map
pack may hold (`maps`, `features`, `anims`, `objects3d` and `previews`) or
files no map uses, when a preview is not a PNG of at most 2 MiB and 1024
pixels a side, or when it needs a later Open Annihilation. A pack from a
registry that is refused says only "the package was refused" in the
Library's queue, the map browser or GET MAP's line. Nothing is changed
when a pack is refused.

To remove a pack, delete its folder while the game is not playing one of
its maps (**OPEN FOLDER** in the Library shows it). In 0.8 a map pack does
not roll back: ROLL BACK is for mods.

## Map names

A pack's map is named by its own file name, the stem, then `@` and the
pack's id: the map `isle_of_ashes` in the pack `archipelago` is
`isle_of_ashes@archipelago`. Pack ids are unique across a registry, so two
packs' maps never share a name, whatever their authors called them. The
whole name fits in 63 bytes, so that it travels in the battle room and in
a saved game; the pack refuses a map whose name would not.

The map browser shows a map's own title, "Isle of Ashes". The skirmish map
list, the battle room and saved games use its name,
`isle_of_ashes@archipelago`, and so does a 3.1c player's battle room.

## One map's files at a time

When you choose a pack's map, in the skirmish map list or the battle room,
OA adds that map's files, and only those, to the game: under 3.1c's own
data and the mod you play, so that nothing of the game or the mod changes.
No other pack's maps, nor the pack's other maps, are there at the same
time, so two maps can never clash, whatever they name their features or
files. The map's files stay through the match, and go when you choose
another map, when the match ends, and on the main menu.

## Whether a map fits

A pack's map may use 3.1c's rocks and trees, but may not replace or redefine
anything in the game or the mod it is played with:

- **Adds only**: none of its files is a file the game or the mod already
  has, ignoring case;
- **New names**: every feature its files define is new to the game and the
  mod;
- **Complete**: everything it uses is in its own files, the game's or the
  mod's;
- **Isolated**: only its own files are added, and no other map's are
  visible beside it.

A map that clashes with 3.1c itself keeps its whole pack out at install.
One that clashes only with a mod, because the mod already defines a
feature the map adds, say, is installed. While that mod is played the
skirmish list and the battle room's map list leave it out. The log says
so once for that pack and that mod:

```
open-annihilation: map pack <id>: 1 map does not fit <key>
```

`<key>` is `ta-3.1c` for the base game, and `<catalogue-id>@<release>` for
a mod installed from a catalogue. Several maps of one pack share one
line, and then it reads `maps do not fit`. A pack from a registry is
listed when that catalogue records the key as true. A pack installed from
a file is listed when its files fit the game and the mod being played.
The map browser still shows the map: its card is greyed with "Doesn't
fit", and Select is greyed ([below](#the-map-browser)). Choosing a map
the lists left out, from a save or by its name, is refused and keeps the
map you had.

OA checks a map again each time it adds its files, and when a match
starts, against the game and the mod being played then.

## The map lists and MORE MAPS…

The skirmish list and the battle room host's map list hold a pack's maps
beside the base maps. A row is the game's name, `<stem>@<id>`. The
skirmish list puts those pack maps after the base maps, in the packs'
order. The battle room's list sorts every name together. Once you choose
a pack map, a screen that has a name under the list shows the title from
the pack's index. The stock skirmish map screen has no such name, so its
rows stay the game's names. The battle room's map name is that title.

A pack installed from the Library joins both lists at once. A pack
removed leaves both lists at once. The base maps stay in the order and
under the names they had.

Under the skirmish map list, and under the battle room's map list for the
host, **MORE MAPS…** opens the map browser, and so do Cmd+F on macOS,
Ctrl+F elsewhere and a controller's Y.

## The map browser

The map browser shows every map you can play, and every map a registry
offers, in one grid with previews, laid out for the window at your
Interface size ([settings](../settings.md#graphics)):

- **The header** says "Maps", where you came from and what you play:
  "Skirmish · Total Annihilation 3.1c", "Battle room · playing Ridge".
- **Filters**: All, Base game, Installed packs or Online; PLAYERS 2, 3, 4
  or 5–10; SIZE S, M or L (S up to 10 on its longer side, M up to 20, L
  above); and "Search maps", which finds a map by its title, its name or
  its pack's name.
- **The cards**: base maps, then maps from installed packs, then maps
  online, each by title, a map you have installed shown once. Each shows
  its preview, "Loading preview" until it is drawn, or "No preview"; a chip
  saying "Base", its pack's name or "Online"; and "4 · 16×16", its players
  and size. Previews are drawn only for the cards in view.
- **The footer** shows the chosen map: its title; "Base game · 4 players ·
  16×16", or its pack, version and registry with its players, size and, for
  a map online, the download's size; whether it fits, "Fits TA 3.1c ✓",
  "Doesn't fit Ridge" with the reason, or, for a map online, "Fit with TA
  3.1c is checked when it's installed" or what the registry's catalogue
  says ("Its catalogue says it does not fit Ridge.").
- **Select**, for a map you have, chooses it in the list you came from,
  where you then press LOAD or start as ever. **Download and select**, for a
  map online, downloads its pack as the Library's GET does, shows how far it
  has come, and chooses the map once the pack is installed. **Cancel**
  closes the browser; a download already started goes on.

A map online that its catalogue says does not fit what you play is greyed,
and OA checks it again against what you have once it is installed.

## GET MAP in the battle room

When the host plays a pack's map you do not have, the battle room's map
name flashes and you cannot ready up, as in 3.1c. If the host is on Open
Annihilation 0.8 and installed that pack from a registry, its presence
record names the pack, its release and its registry, and your battle room
shows **GET MAP** above the map's name, with a line under it:

| You | The line | GET MAP |
|---|---|---|
| can get it | "Archipelago Pack 1.2 · 2.3 MB from Core Prime" | downloads exactly the host's release and installs it |
| have not added the host's registry | "Add *registry* to get Archipelago Pack" | opens Add registry with the host's registry's address, checked, for you to ADD or not ([adding a registry](../downloads.md#adding-a-registry)); once added, the download goes on by itself |
| have another release | "You have release 2 of Archipelago Pack; the host plays release 3." | downloads the host's release, while the registry still offers it |
| cannot get the host's release | "The host's release of Archipelago Pack is no longer offered by Core Prime." | not shown |
| have downloads from that registry off | "Downloads from Core Prime are off (Settings › Downloads)." | not shown |
| are downloading it | "Downloading Archipelago Pack · 1.1 of 2.3 MB", with a bar | not shown |
| saw the download fail | "Couldn't get Archipelago Pack: *why*" | tries again |

The pack installs as a download from its registry does
([which downloads ask first](../library.md#which-downloads-ask-first)), and
the battle room then finds the map and stops flashing its name. GET MAP
never readies you: you press READY yourself. Two releases of one pack can
hold different features and pictures under the same map, which the map's
check would not notice, so OA fetches the host's exact release, never
another.

A host who installed the pack from a file, an OA 0.7 host and a 3.1c host
send no pack in their presence record, or no record at all, so a joiner
sees no GET MAP; get the pack from the Library, the map browser or a
file instead. The host's player card says whether you have its pack:
"✓ installed", "another release here" or "not installed"
([network games](../../README.md#network-games-from-the-command-line)).

## What 3.1c players see

Nothing new. A pack's map is a map a 3.1c player does not have: their
battle room says so with the missing-map line, naming the map as
`isle_of_ashes@archipelago`, and they cannot ready up, as for any map they
lack. Packs change nothing that 3.1c sends or expects.
