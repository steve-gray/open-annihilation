# data/map-pack

`oa-data-map-pack` reads, checks and writes a map pack's manifest,
`oamap.yaml`. A map maker's header and the index `oa-tool pack` writes are
the same file: the pack's name and id, and one entry per map with the title
players see, how many players the map is for, its size, its preview and the
files that map loads from the pack.

The public rules are [the OAMAP standard](../../../docs/maps/oamap-standard.md).

## Entry points

`oa/data/map_pack/manifest.hpp` (namespace `oa::data::map_pack`):

- `read_manifest` reads the strict YAML of a mod profile. In `package` use
  every map entry is complete. In `source` use an entry may hold only its
  stem; anything else present is read and checked, and the packer rewrites
  it. Every broken rule is reported, with its line and column, and the
  manifest is left unchanged when any rule fails. `describe` prints one
  problem as `oamap.yaml:<line>:<column>: <key>: <message>`.
- `write_manifest` writes a manifest back in a fixed key order, two-space
  indentation, every string double-quoted, one file path per line. An empty
  description, preview, homepage, tag list or engine requirement is left out.
- `homepage`, `tags` and `requires.engine` are read by the shared
  package-key rules. The text of `requires.engine` is kept as written.

`oa/data/map_pack/map_name.hpp`:

- `pack_map_name` joins a stem and a pack id with `@`. `split_pack_map_name`
  splits at the last `@` and answers only when both pieces could have been
  written. `map_name_fits` tells whether the joined name is short enough to
  travel. `valid_pack_id` and `valid_stem` are the two pieces' own rules.

## State

None. Callers pass bytes and a `Manifest` and get values back. Nothing here
is installed for the rest of the process, and nothing opens a file.

## Invariants

- A map's name, `<stem>@<id>`, is at most `most_map_name_bytes` (63), so it
  fits the battle room's map name and a saved game's summary with the
  terminating NUL.
- `@` is never part of an id or a stem, so a name splits at its last `@`.
- A key the manifest does not define is refused.
- `homepage`, `tags` and `requires.engine` follow the shared package-key
  rules, and are not stated again here.

## Tests

`data-map-pack` reads the standard's example as a packed manifest, reads a
header with only a stem as a source and refuses it as a package, writes a
two-map manifest whose maps share a file and reads it back, refuses each
manifest rule once (including a bad homepage, a tag that is not kebab-case
and an engine requirement that does not parse) while still reading an engine
requirement this build does not meet, reports three problems for one path
that breaks three rules, and splits and fits map names.

## Limitations

Whether this build of Open Annihilation meets `requires.engine` is not
checked here. The installer does that when a pack is installed. There is no
Python reader of the same rules.
