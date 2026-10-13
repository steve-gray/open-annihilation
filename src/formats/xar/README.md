# xar archives

Reads an Apple xar archive's header and its zlib-compressed table of
contents, and inflates one gzip member from the heap. Target
`oa-formats-xar`, header `oa/formats/xar.hpp`, namespace `oa::formats::xar`.
The deflate stream is `oa::formats::zip::Inflater`. This module opens no
files: the caller hands the bytes and reads a heap file from the offset the
table names.

## Entry points

- `read_header` reads the 28-byte header. A header longer than 28 bytes, up
  to 4096, is accepted without those extra bytes being present yet.
- `read_toc` inflates a zlib table and lists its files. `read_archive` reads
  both from a span that reaches the first heap byte.
- `find_entry` looks a path up exactly. `fault_text` describes a `Fault`.
- `GzipReader` inflates one gzip member a piece at a time, header and
  trailer included. `inflate_gzip` reads one whole member.

## State

`GzipReader` holds the raw deflate decoder and the few bytes of a header or
trailer that arrived split. It does not keep the member. Nothing else keeps
state between calls.

## Invariants

- The table is zlib-wrapped deflate, at most 32 MiB either compressed or
  inflated, and its Adler-32 has to match. A dictionary is refused.
- A path component is a single name: not empty, not `.` or `..`, and with
  no slash or backslash. One such name fails the whole table.
- An encoding other than stored or gzip is recorded and left unread, so the
  rest of the table can still be used.
- A gzip member is method 8, with no reserved flag bits. Its CRC-32 and size
  trailer have to match, and the member has to end where the caller said.

## Tests

`formats-xar` builds small archives in the test. It checks a stored file and
a gzip file, an encoding that is left unread, a name of `..`, a table that
claims to be too large, a short file, a bad mark, a gzip member with a file
name, and a gzip checksum that does not match.

## Limitations

The header's own checksum is not checked. bzip2 and other encodings are
named and skipped. A gzip member's header CRC is not checked. Only one
member is read: bytes after it are refused.
