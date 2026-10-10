# data/map-fit

`oa-data-map-fit` checks one map against the game and the mod it would be
played with. The map fits when it only adds files and feature names the game
does not already have, and every feature and unit it places can be loaded.
The game is whatever an `AssetStore` provides from loose files and archives.
The pack layer is never part of that game.

## Entry points

`oa/data/map_fit/map_fit.hpp` (namespace `oa::data::map_fit`):

- `check_map` reports every rule one map breaks: Isolated, Adds only, New
  names and Complete. Each failure names the rule, the path or the name at
  fault, and a reason. `describe` prints that as a sentence. `rule_name`
  names the rule.
- `collect_game_names` reads the game's feature sections, weapon names and
  unit-file stems once. The caller keeps the `GameNames` for as long as the
  store's mounts stay as they were. The check does not collect them again.
- `folder_map_files` reads one installed map's folder. `mounted_map_files`
  reads the store's mounted pack layer, and borrows the store.
  `layer_files` lists the paths the store would show: the OTA and the TNT at
  `maps/<stem>@<id>.ota` and `.tnt`, and every other file at its own path.
- `uses_of` names the features and units the map places, and where each name
  was written. `features_from` is parallel to the feature names ("the TNT"
  or "Schema 2's features"). `units_from` is parallel to the unit names
  ("Schema 2's units"). `references_of` names the sprite, the model, the
  burn weapon and the linked sections of one feature section.

## State

None. `GameNames` belongs to the caller. `mounted_map_files` borrows the
store it was given, and the store must outlive it. Nothing here is installed
for the rest of the process.

## Invariants

- The game a check reads never includes the pack layer. Every listing asks
  for loose files and archives only.
- Every failure carries a rule, a subject and a detail.
- `check_map` does not collect `GameNames` again.
- A check through `folder_map_files` ignores whatever pack layer is mounted.

## Tests

`data-map-fit` builds a small game and a map. The map fits. Shipping a file
the game already has fails Adds only, and the detail names that archive.
Defining a feature section the game already names fails New names. A missing
model, a missing linked feature (including one named by `featureburnt`), a
burn weapon the game does not have, and a unit the game does not have each
fail Complete and name that subject. A feature the game defines is not
followed into the game's own links. Another map's mounted layer fails
Isolated, while the same map read from its folder does not. The same map
mounted through `layer_files` fails exactly as the folder check does.

`data-map-fit-data` reads the installed game. Its feature sections include
`DragonsTeeth`. A map that places that section and one section of its own
fits. The same map defining `DragonsTeeth` fails New names and names the
game's resource.

## Limitations

GAF sequence names inside a GAF are not checked. A 3DO's textures are not
checked. Unit names are matched to unit file stems. The design's Clean rule,
that a map holds no file of the original game, is not checked: it is
deferred to the post-build review.
