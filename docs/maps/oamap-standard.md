# The OAMAP standard

A map pack, `oamap.yaml` inside a `.oamap` file, adds maps to a game. It
carries each map and what that map draws: its tiles, its features, the
animations and models those features use, and a preview for the map picker.
It can only ever add to a game. Whether a map fits is decided for the game
data and the mod it will be played with, before anyone plays it.

This document is the format and its rules as the engine implements them.
The archive a pack is shipped in is the mod package's; this standard names
what is different for maps and does not repeat the archive's rules.

## Contents

- [1. Goals](#1-goals)
- [2. The package](#2-the-package)
- [3. The manifest](#3-the-manifest)
- [4. The index](#4-the-index)
- [5. Names](#5-names)
- [6. What a pack holds](#6-what-a-pack-holds)
- [7. How a map fits](#7-how-a-map-fits)
- [8. Versions and releases](#8-versions-and-releases)
- [9. A full example](#9-a-full-example)

## 1. Goals

A pack adds maps. It never replaces a unit, a weapon, a sound or a texture,
and two maps in one pack never have to see each other.

| Goal | What it means in practice |
| --- | --- |
| Maps only add | Playing a pack map mounts that map's files for one match and unmounts them afterwards. Nothing already in the game is replaced. |
| One manifest | `oamap.yaml` is what the packer writes, what the installer checks and what the game lists. A key spelled wrong is refused, so it is noticed. |
| Names travel | The name a map carries in the battle room and in a saved game is formed from its stem and the pack's id, and it has to fit both. |
| Players see the title | Lists and the map picker show the map's own title. The joined name is how the game tells maps apart. |

## 2. The package

A map pack is one `.oamap` file. The archive, the names it may hold and the
limits on its size are the `.oamod` package's
([4.6 The .oamod package](../mods/oamod-standard.md#46-the-oamod-package)):
the same zip, the same unpacked size, and the same refusal of links, of
device names and of paths that would not unpack on every system. Those
rules are not repeated here. The manifest `oamap.yaml` takes the place of a
mod's `oamod.yaml`, at the top of the archive or in the one folder the top
holds.

The manifest is the strict YAML a mod profile uses
([3.3 Strict YAML rules](../mods/oamod-standard.md#33-strict-yaml-rules)).
One document, one top-level mapping, no anchors and no block scalars.

**Where it installs.** Into `Documents/Open Annihilation/Maps/<id>`, by the
manifest's `id`, never by the file's name.

## 3. The manifest

`oamap` is `1`. A key that is not in this table is refused. `homepage`,
`tags` and `requires.engine` use the same rules as a mod profile and a
language pack; they are not defined a second time for maps.

| Key | Required | Rule |
| --- | --- | --- |
| `oamap` | yes | `1` |
| `id` | yes | lower-case kebab-case, 1 to 32 bytes: words of `a`-`z` and `0`-`9` joined by single hyphens. It never contains `@` |
| `name` | yes | one line, 1 to 64 characters, the pack's name |
| `version` | yes | one line, 1 to 32 characters. Free text. Nothing orders it |
| `description` | no | one line, at most 120 characters |
| `homepage` | no | an `http` or `https` address, the shared package rule |
| `tags` | no | a list of lower-case kebab-case tags, the shared package rule. An empty list is refused; leave the key out |
| `author.name` | yes | 1 to 128 bytes. `unknown` when nobody is known |
| `author.email` | no | an e-mail address, such as `name@example.com` |
| `packaging.revision` | yes | an integer from 1 to 65535. It rises when the same version is packed again |
| `packaging.date` | yes | `YYYY-MM-DD`, a day that exists |
| `packaging.packager` | yes | 1 to 64 characters |
| `requires.base` | no | when written, `ta-3.1c` |
| `requires.engine` | no | the shared engine requirement, written in quotes. An unquoted `>` starts a block scalar, which is refused. Reading the manifest does not ask whether this Open Annihilation meets it |
| `maps` | yes | 1 to 256 map entries. [4. The index](#4-the-index) |

`requires` holds `base` and `engine` and nothing else. `author` holds `name`
and `email` and nothing else. `packaging` holds `revision`, `date` and
`packager` and nothing else.

## 4. The index

`oa-tool pack` writes `maps`. An author may write the stems to include, and
may leave the rest out; the packer fills each entry and rewrites it. In the
packed manifest every entry is complete, and `files` is required. It is not
optional. The game lists a pack's maps from this index and does not open the
maps to do it.

| Key | In a packed entry | Rule |
| --- | --- | --- |
| `stem` | required | 1 to 40 bytes of ASCII letters, digits, space, `_`, `-`, `.`, `'`, `(` and `)`. It does not start or end with a space or a dot, and it never contains `@`. Stems are unique in the pack, ignoring the case of ASCII letters |
| `name` | required | the title players see. One line, 1 to 64 characters |
| `players` | required | an integer from 2 to 10 |
| `size` | required | `<width>x<height>`, each side an integer from 1 to 128 with no leading zero |
| `description` | optional | one line of at most 127 bytes. Left out when the map has none |
| `preview` | optional | a path under `previews/` ending in `.png`. Left out when the map has none |
| `files` | required | 2 to 1024 paths. Everything that map loads from the pack, and nothing else |

A path is relative, uses `/`, and has no empty, `.` or `..` part and no `\`,
`:` or control character. A map lists no path twice, ignoring the case of
ASCII letters. Under `maps/` the only paths are that map's own
`maps/<stem>.ota` and `maps/<stem>.tnt`, and both are listed. Every other
path is under `features/` and ends in `.tdf`, under `anims/` and ends in
`.gaf`, or under `objects3d/` and ends in `.3do`. Extensions are compared
without regard to case.

Two maps in one pack may name the same path. A feature both of them draw is
one file in the pack and a line in each map's `files`. While a map is
played, nothing else in the pack is visible: only that map's `files` are
mounted, and they are unmounted when the match ends.

## 5. Names

A map has three names.

- The **stem** is the file name of its OTA and TNT, without the extension:
  `isle_of_ashes`, so `maps/isle_of_ashes.ota` and `maps/isle_of_ashes.tnt`.
- The **id** is the pack's id, `archipelago`. Ids are unique across a
  catalogue, so two packs do not share one.
- The **name** the battle room and a saved game carry is the stem, `@`, then
  the id: `isle_of_ashes@archipelago`. The pack's own files keep the plain
  stem. Players mostly see the title, "Isle of Ashes".

An id and a stem never contain `@`, so the name always splits back at its
last `@`. `a@b@archipelago` does not split: `a@b` is not a stem.

The joined name is at most 63 bytes. The battle room carries it in
`PlayerSetupInfo::map_name`, and a saved game's summary keeps it in
`LoadSummary::mission`. Both are C strings. The summary's field is the
shorter of the two, 64 bytes with the terminating NUL, so 63 bytes of name
fits there and in the battle room. A longer name is refused, and the
diagnostic names it:

```text
isle_of_ashes_and_the_outer_shoalsxxxxx@archipelago-remastered-x is 64 bytes; a map's name must fit in 63 bytes to travel in the battle room and in saved games
```

The length in that message is the name's own length in bytes. The stem is
39 bytes and the id is 24, both within their own limits, and the joined
name is one byte over.

## 6. What a pack holds

A pack holds:

- each map's OTA and TNT, under `maps/`. The map's tiles live in the TNT;
- feature definitions, under `features/`, ending in `.tdf`;
- feature animations, under `anims/`, ending in `.gaf`;
- 3DO models those features use, under `objects3d/`, ending in `.3do`;
- previews for the map picker, under `previews/`, ending in `.png`.

A pack never holds:

- sounds. Maps have none of their own;
- units or weapons. They belong to mods;
- textures. A model's textures come from the game or the mod;
- files of the original game.

## 7. How a map fits

When a pack map is played, only that map's `files` are mounted, on top of
the game data and the mod being played, for that match. No other map, from
this pack or another, is mounted beside it. What remains is the original
rule: a map must not replace or contradict anything in the game or the mod
it is played with.

| Rule | Holds when | An example of it failing |
| --- | --- | --- |
| Isolated | Only the chosen map's files are mounted, for one match, and unmounted after it. | Isle of Ashes is the map being played, and a wreck that exists only in Black Shoals' files is still visible. |
| Adds only | None of the map's files exists in the game data plus the mod, ignoring case. | The map lists `features/trees.tdf`, and the game data or the mod already has that path. |
| New names | Every top-level section in the map's feature TDFs is new to the game data plus the mod, ignoring case. The map's own name is already unique by its `@` suffix. | The map's feature TDF opens a section `Tree01` that the game or the mod already defines. |
| Complete | Every name the map uses resolves in its own files or in the game data plus the mod. | The map's OTA names a feature `LavaVent` that neither the map's files nor the game and the mod define. |

A map may use the rocks and trees that are already in the game. It may not
redefine them.

**Fit depends on the mod.** A map can fit the base game and one mod, and
clash with another mod that already defines a feature it adds. The fit is
worked out against the base game and against each mod, per map. A pack whose
map clashes with the base game itself is refused when it is installed. A
clash with one mod only greys that map in the pickers, with the reason.

**Clean** is policy, not an engine check. No file of a pack may be a file of
the original game. This version does not check that. The publisher's review
keeps those files out until a check is built; the check is deferred to the
post-build review.

## 8. Versions and releases

`version` is the author's text. It is never ordered, and two versions are
not compared as numbers. `packaging.revision` marks a repack of the same
version, so a file install still sees a newer build of that version when no
catalogue is involved.

A catalogue numbers releases of a package key and does not order version
strings. That is the mod standard's rule for catalogue releases
([4.7 Catalogue releases](../mods/oamod-standard.md#47-catalogue-releases)),
and it is the same for a map pack.

`requires.engine` is written in quotes (`engine: ">= 0.8.0"`). Without the
quotes, `>` starts a block scalar and the manifest is refused before the
requirement is read. A requirement this Open Annihilation does not meet is
still a valid manifest; meeting it is decided when the pack is installed.

## 9. A full example

`archipelago.oamap` holds two maps. They share one feature file. Black
Shoals has no preview of its own. The packed manifest is:

```yaml
oamap: 1
id: archipelago
name: Archipelago Pack
version: "1.2"
description: "Islands, shoals and the wrecks between them."
homepage: "https://example.com/archipelago"
tags:
  - islands
  - water
author: {name: Core Prime, email: maps@example.com}
packaging: {revision: 1, date: 2026-11-01, packager: oa-tool 0.8}
requires: {base: ta-3.1c, engine: ">= 0.8.0"}
maps: # written by oa-tool pack
  - stem: isle_of_ashes
    name: Isle of Ashes
    players: 4
    size: 16x16
    description: "A ring of ash."
    preview: previews/isle_of_ashes.png
    files: # all this map loads from the pack
      - maps/isle_of_ashes.ota
      - maps/isle_of_ashes.tnt
      - features/archipelago/islands.tdf
      - anims/islands.gaf
      - objects3d/wreck.3do
  - stem: black_shoals
    name: Black Shoals
    players: 4
    size: 12x12
    files:
      - maps/black_shoals.ota
      - maps/black_shoals.tnt
      - features/archipelago/islands.tdf
      - anims/islands.gaf
      - objects3d/wreck.3do
```

Before packing, the same header can name only the stems to include. `oa-tool
pack` replaces that list with the entries above.

```yaml
maps:
  - stem: isle_of_ashes
  - stem: black_shoals
```
