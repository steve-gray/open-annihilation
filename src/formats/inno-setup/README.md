# Windows installer (Inno Setup 5.5.7)

Reads one Windows installer made by Inno Setup 5.5.7 (Unicode), the kind
published as a single file, so the game can be copied out of it without
installing it first. Target `oa-formats-inno-setup`, header
`oa/formats/inno_setup/installer.hpp`, namespace `oa::formats::inno_setup`.

The setup loader's offset table is a resource in the program file. The
setup header that follows is LZMA-compressed and lists the installed files.
Each file is one or more pieces. A piece is stored or LZMA2, carries a
SHA-1, and is then a zlib wrapping of the bytes the player gets. Pieces
that belong together are joined, in order, into the installed path.

## Entry points

`Reader::open` reads the offset table, the setup header and the file list.
`Reader::list` returns each installed file's path, size and modification
time. `Reader::read` streams one listed file. A checksum is checked before
any byte of a piece is handed to the caller. Lengths and offsets are
checked against the file, and a malformed file fails.

The reader does not decide whether the installer is one the game knows. The
application checks the file's size and SHA-256 before it asks for a file.

## What it keeps

Nothing of the installer is written. `open` keeps the file list and where
each piece lies. `read` opens the file again and holds one piece's bytes
while it checks them.

## Invariants

- Only setup data whose version text is `Inno Setup Setup Data (5.5.7) (u)`
  is opened. Anything else fails.
- An installed path is relative, uses `/`, and has no empty part, `.` or
  `..`. A drive, a leading slash and a trailing slash are refused.
- A piece that is encrypted, split across several files, or stored in a
  form this reader does not undo, fails instead of returning other bytes.
- LZMA and LZMA2 go through `oa::formats::lzma`. This module does not carry
  a second decoder.

## Tests

`oa-formats-inno-setup-test` builds a tiny installer around
`tests/data/setup-body.bin` and reads it back, including a stored piece and
an LZMA2 piece that are joined, and a file that is truncated or is not this
version. `tests/make_fixture.py` is how that body was made. The test does
not run it, and the body holds no game data. The application's
`--check-installer FILE` reads a real installer and prints each bundle and
every file's SHA-256.

## Limitations

The reader opens this one version, as one file. It does not open an
installer split into further parts, and it does not open a later or earlier
setup version. A piece marked as transformed, or encrypted, is refused.
