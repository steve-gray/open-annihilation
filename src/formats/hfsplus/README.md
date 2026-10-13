# HFS+ volumes

A reader for HFS+ and HFSX volumes. Target `oa-formats-hfsplus`, header
`oa/formats/hfsplus.hpp`, namespace `oa::formats::hfsplus`. It does not
open files and it does not link the disk-image reader. A caller that has
a disk image passes the disk's bytes in through a read hook.

## Entry points

- `ReadHooks` copies bytes from the volume or the whole disk.
- `Volume::open` reads a volume whose header sits at byte 1024.
- `Volume::open_disk` finds that volume on a whole disk.
- `Volume::catalog` returns every catalogue record, in leaf order.
- `Volume::read_file` copies one file's data fork. A compressed file is
  refused and the buffer is left untouched.

## State

An open `Volume` keeps the catalogue and each file's extents. The hooks
have to outlive the volume. The volume does not own the bytes it reads.

## Invariants

- The header signature is `H+` or `HX`, version 4 or 5. The allocation
  block size is a power of two from 512 bytes to 1 MiB, and the block
  count fits in the bytes the caller offered.
- `open_disk` accepts a bare header, a GUID partition that holds an HFS
  header, or an Apple partition map entry of type `Apple_HFS` or
  `Apple_HFSX`. A disk whose partitions are APFS, and no HFS header, is
  refused.
- The catalogue is walked from its first leaf. Folder, file and thread
  records are kept. A key is a parent catalogue id and a UTF-16 name; an
  unpaired surrogate becomes U+FFFD. A folder record shorter than 88
  bytes, or a file record shorter than 248, is refused. The catalogue is
  capped at a million nodes and 500,000 records.
- A file's first eight extents are the ones in its catalogue record.
  Further extents come from the extents overflow tree. A hole reads back
  as zeroes. Bytes past the data fork's logical size fail.
- The owner-flag bit `0x20` marks a compressed file. Symbolic links are
  flagged and left for the caller to skip.

## Tests

`formats-hfsplus` reads the fixture image's catalogue through the
disk-image reader, then a volume built in memory: a file whose ninth
block lives in the extents overflow tree, a compressed file, a truncated
record, an APFS partition and an Apple partition map.

## Limitations

Compressed files are not opened. Resource forks are recorded and not
read. The volume name is not part of a path a caller builds: paths stop
at catalogue id 2.
