# A program file's icon

`oa-data-pe-icon` reads the icon a Windows program file carries in its
resources and returns it as RGBA. The Library draws that picture on the
base game's badge. Target `oa-data-pe-icon`, header `oa/data/pe_icon.hpp`,
namespace `oa::data::pe_icon`.

## Entry points

`read_program_icon` takes the file's bytes and returns the first icon
group's largest image: the greatest width times height, and for a tie the
most bits a pixel. A width or height of 0 in the group means 256. The
image is 1, 4, 8, 24 or 32 bits a pixel, uncompressed, rows from the
bottom, and its AND mask is the transparency (a set bit is clear). The
result is rows from the top.

`most_file_bytes` is the longest span the reader accepts.

## State

None. The caller keeps the bytes and the icon.

## Invariants

- Nothing in the file is run, mapped or relocated.
- Every offset, count and size is checked before it is read. A directory
  that leads back to one already on the path is refused. At most 96
  sections, 4,096 entries in one directory and 32 images in one group are
  accepted.
- The icon's bytes must lie in the resource section. The reader does not
  look at any other section's bytes beyond the headers that name the
  sections.
- A span longer than `most_file_bytes` is refused without being walked.
- A file with no icon, or one the reader does not understand, yields
  nothing.

## Tests

`data-pe-icon` builds program files in memory: a valid icon, a missing
resource section, a truncated directory, an offset outside the resource
section, a directory that loops, and an AND mask (including a 32-bit
image whose pixel alpha is not the transparency). It also cuts a valid
file at every length.

`data-pe-icon-data` (skipped without `OA_GAME_DIR`) reads the installed
game's program file and checks that a 32 by 32 icon came back. No byte
of that file is kept in the repository.

## Limitations

- Only an uncompressed device-independent bitmap is read. A compressed
  image, a header other than 40 bytes, or a bit count other than 1, 4, 8,
  24 or 32 yields nothing.
- Named icon groups are not read. The first group is the first id under
  the icon-group type, and its language is the first id under that group.
- The whole file has to be handed over, but only the headers and the
  resource section are read. A file longer than `most_file_bytes` is
  refused whole.
