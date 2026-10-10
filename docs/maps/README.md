# Map packs

A map pack adds maps to a game. It is one `.oamap` file: a manifest,
`oamap.yaml`, and the maps' own files, their features, the animations and
models those features use, and previews for the map picker. A pack only
adds. It does not carry units, weapons, sounds or textures, and playing one
of its maps mounts that map's files for one match.

The game lists the maps from the manifest's index, and the name a map
carries in the battle room and in a saved game is its stem joined to the
pack's id.

- [The OAMAP standard](oamap-standard.md) is the format of `oamap.yaml` and
  the rules a map pack follows: the manifest, the index, names, what a pack
  holds, and how a map fits the game it is played with.
