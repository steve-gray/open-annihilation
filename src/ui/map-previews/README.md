# Map previews

Small thumbnails for the map rows on screen. A row asks for its map, and a
few of the maps asked for this frame are decoded. A map that scrolled away
is not decoded. Library `oa-ui-map-previews`, header
`include/oa/ui/map_previews/map_previews.hpp`, namespace
`oa::ui::map_previews`.

## Entry points

- `PreviewCache` keeps decoded thumbnails, at most as many as it was given
  (256 unless the caller says otherwise). Each thumbnail fits a square,
  128 pixels on a side unless the caller says otherwise.
- `ask` returns a thumbnail already decoded, or remembers the row and
  returns null. `pump` decodes at most a few waiting rows, the one asked
  for most recently first. `new_frame` forgets a row that was not asked for
  again. `failed` says a row was decoded and could not be, and that row is
  not tried again.
- `PreviewSource::base` reads the map's terrain file through `PreviewHost`,
  and only its header and minimap. The playable share of that minimap is
  fitted into the square and coloured with the game palette.
- `PreviewSource::pack` reads the preview picture's file. The pack is not
  mounted. `PreviewSource::online` takes picture bytes from the caller.
  Both are scaled to fit the square by nearest neighbour, and keep their
  shape. A palette picture uses its own palette.

## State

The cache holds the decoded thumbnails, the rows still waiting, and the
keys whose decode failed. It reads terrain bytes, preview files and online
picture bytes only through the host passed to `pump`, and writes nothing
except its own cache. A thumbnail pointer from `ask` lasts until `pump`
drops that thumbnail.

## Invariants

A frame decodes only rows asked for since the last `new_frame`, and at most
as many as `pump` was given. The least recently asked thumbnail is the one
dropped when the cache is full. An online picture whose bytes are not ready
stays waiting. A failed decode is not repeated.

## Tests

`ui-map-previews` decodes a row only after `pump`, skips a row not asked
for again, decodes the two most recent when asked for two, fails a broken
picture once, and keeps at most the given number of thumbnails.
`ui-map-previews-data` decodes every base map of the installed game, and
the bytes read for one map are its header and minimap, not the whole
terrain file.

## Limitations

The map pickers do not draw these thumbnails yet. Nothing here fetches an
online picture; the caller hands its bytes over when it has them.
