# cpio archives

Reads a cpio archive in the portable ASCII layouts: odc (magic `070707`) and
newc (magic `070701`, and the checksum form `070702`). Target
`oa-formats-cpio`, header `oa/formats/cpio.hpp`, namespace
`oa::formats::cpio`. The archive is fed a piece at a time. This module opens
no files and does not keep a file's bytes.

## Entry points

- `Reader::feed` takes the next bytes and tells a `Visit` when a header
  arrives, when a taken file's bytes arrive, and when that file ends.
- `halted` reports that the visitor stopped the read. `done` reports that
  the trailer has been read. `finish` requires the trailer and a visitor
  that did not stop.
- `name_is_safe` reports whether a recorded name can be used as a relative
  path. The reader itself does not apply that rule.

## State

`Reader` holds the header it is assembling, the name, and how many data
bytes remain. It does not hold the file's data. A new reader starts at the
first header.

## Invariants

- One archive is one layout. odc has no padding. newc pads the name and the
  data so the next header is four-byte aligned from the start of the archive.
- A name is at most 8192 bytes, counting the trailing NUL, and that last
  byte is NUL. The trailer name is `TRAILER!!!`. After it, only NUL bytes
  are accepted.
- The `070702` checksum is the sum of the file's data bytes. Padding is not
  part of it. A visitor that returns false stops the read and is not a
  broken archive.
- `File` records the device and inode. odc stores one device number. newc
  stores the major in the high half and the minor in the low half. Two names
  with one device and inode are one file: one name may carry no bytes while
  another carries them.

## Tests

`formats-cpio` builds odc and newc archives in the test, including a newc
name that needs padding, a `070702` checksum that matches and one that does
not, and the device and inode of both layouts. It also checks a short
archive and `name_is_safe`.

## Limitations

The binary layout and the CRC layout's extra checksum header are not read.
Symbolic links are named from the mode; their text is ordinary data for the
visitor to take or leave.
