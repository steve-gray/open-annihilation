# A program file's icon and version text

`oa-data-pe-icon` reads the icon a Windows program file carries, and the
FileDescription in its version resource, and returns the icon as RGBA. The
Library draws that picture on the base game's badge, and chooses the
program whose FileDescription is exactly "Total Annihilation". Target
`oa-data-pe-icon`, header `oa/data/pe_icon.hpp`, namespace
`oa::data::pe_icon`. Where a resource lies is `oa::formats::pe`; this
module only decodes the bytes.

## Entry points

`read_program_icon` takes the file's bytes and returns the first icon
group's largest image: the greatest width times height, and for a tie the
most bits a pixel. A width or height of 0 in the group means 256. The
image is 1, 4, 8, 24 or 32 bits a pixel, uncompressed, rows from the
bottom, and its AND mask is the transparency (a set bit is clear). The
result is rows from the top.

`file_description` reads the first version resource (type 16, its first id
and first language) and returns the first FileDescription in its string
tables. `only_described` returns the one file in a list whose
FileDescription is exactly a given text: none, or more than one, returns
nothing. A file with no version resource does not count; a version
resource whose string tables cannot be read does not count either.

`most_file_bytes` is the longest span the reader accepts.

## State

None. The caller keeps the bytes and the icon.

## Invariants

- Nothing in the file is run, mapped or relocated.
- The resource directory is walked only by `oa::formats::pe`, which checks
  every offset, count and size and refuses a directory that leads back to
  one already on the path.
- At most 32 images in one icon group are accepted. The group's largest
  image is the one drawn.
- The icon's bytes and the version resource's bytes are the ranges
  `oa::formats::pe` returns, and each range must lie in the file.
- A span longer than `most_file_bytes` is refused without being walked.
- A file with no icon, or one the reader does not understand, yields
  nothing. A version resource that is missing is reported as absent; one
  whose string tables cannot be read is reported as malformed.

## Tests

`data-pe-icon` builds program files in memory: a valid icon, a missing
resource section, a truncated directory, an offset outside the resource
section, a directory that loops, and an AND mask (including a 32-bit
image whose pixel alpha is not the transparency). It also cuts a valid
file at every length. The version checks cover a FileDescription of
"Total Annihilation", a different description, no match, two matches, a
file with no version resource, and string tables whose length or value
length is not one the reader can follow.

`data-pe-icon-data` (skipped without `OA_GAME_DIR`) looks at the regular
program files directly in the installed game folder, takes the one whose
FileDescription is "Total Annihilation", and checks that a 32 by 32 icon
came back. No byte of that file is kept in the repository.

## Limitations

- Only an uncompressed device-independent bitmap is read. A compressed
  image, a header other than 40 bytes, or a bit count other than 1, 4, 8,
  24 or 32 yields nothing.
- Named icon groups are not read. The first group is the first id under
  the icon-group type, and its language is the first id under that group.
- The whole file has to be handed over. A file longer than
  `most_file_bytes` is refused whole.
