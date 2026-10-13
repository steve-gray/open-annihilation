# UDIF disk images

A reader for UDIF disk images. Target `oa-formats-udif`, header
`oa/formats/udif.hpp`, namespace `oa::formats::udif`. The image is not
mounted. This module does not open files: the caller hands it a read hook.

## Entry points

- `ReadHooks` copies bytes from the image file.
- `Image::open` reads the `koly` trailer and the block map. A second call
  on an open image does nothing and succeeds. A call that fails leaves the
  image unopened.
- `Image::disk_bytes` is the size of the disk the image expands to.
- `Image::read` copies disk bytes. A range past the end fails. Bytes no
  chunk covers are zeroes.

## State

An open `Image` keeps the block map and up to four inflated chunks. The
hooks have to outlive the image. The image does not own the file.

## Invariants

- The trailer is 512 bytes at the end of the file, signature `koly`,
  version 4. An image split into more than one file is refused.
- The resource plist is scanned for the `blkx` array and each entry's
  base64 `Data`. It is not a general XML reader. An entry that does not
  start with `mish` is skipped. No table at all is refused.
- A run's sectors are relative to its table's first sector. A chunk's file
  offset is the trailer's data-fork offset plus the table's data offset
  plus the run's own offset.
- Raw chunks (`0x00000001`) are copied. Zlib chunks (`0x80000005`) are
  inflated with zlib's wrapping header. Zero-fill (`0x00000000`) and
  ignored (`0x00000002`) chunks read back as zeroes. A comment
  (`0x7ffffffe`) and the terminator (`0xffffffff`) are not data. ADC,
  bzip2, LZFSE, LZMA and any other kind are refused when the image is
  opened, with a message that names the kind. Checksums are not checked.
- The plist is at most 32 MiB, the tables hold at most a million runs, one
  zlib or raw chunk is at most 64 MiB, and the disk is at most 1 TiB.

## Tests

`formats-udif` opens a small fixture image, then images built in memory:
a zlib chunk, a raw chunk, zeroes, a read that spans chunks, a kind this
reader refuses, a trailer that is not UDIF, and a block map that does not
parse.

## Limitations

No ADC, bzip2, LZFSE or LZMA chunks, and no image split across files. The
block map's checksum is not verified. Four inflated chunks are kept; an
older one is dropped when a fifth is needed.
