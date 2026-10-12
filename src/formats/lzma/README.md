# LZMA and LZMA2

Piece-by-piece decoding of two streams. Target `oa-formats-lzma`, header
`oa/formats/lzma.hpp`, namespace `oa::formats::lzma`.

An LZMA stream is the raw coding with its properties header: one properties
byte, the dictionary size as 4 little-endian bytes, and the uncompressed
size as 8 little-endian bytes. Every size byte `0xff` means the size is not
stated, and the stream then ends with an end mark. The bytes `xz` writes
with `--format=lzma` are this stream.

An LZMA2 stream is one dictionary-property byte, from 0 to 40, then the raw
LZMA2 coding, which ends with a zero control byte. The property byte is the
one `xz` takes from `--lzma2=dict=` and does not write into `--format=raw`;
the fixture prefixes it. A property of 0 is a dictionary of 4096 bytes.

`Decoder::feed` takes the next compressed bytes and writes decoded bytes
into the room it is given. `Decoder::finish` refuses a stream that stopped
short. A dictionary larger than `max_dictionary_bytes` (256 MiB) is refused
when the header has arrived.

The decode is the LZMA SDK 25.01 by Igor Pavlov, public domain, the files
`LzmaDec` and `Lzma2Dec` and the headers they include, unchanged, in
`vendor/`. See [ATTRIBUTIONS.md](../../../ATTRIBUTIONS.md).
