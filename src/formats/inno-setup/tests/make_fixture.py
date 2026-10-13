#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Build the small Inno Setup 5.5.7 fixture the reader test opens.

The test does not run this script. It reads tests/data/setup-body.bin, which
this script writes, and wraps that body in a program file of its own.

The body is one Unicode 5.5.7 setup, with no game data:

- a setup header compressed as LZMA, then a block of data entries stored
  with the same checksummed framing and no compression
- docs/readme.txt, one stored piece: three ignored bytes, then a zlib
  wrapping of the text "ridge\\n"
- notes.txt, two pieces joined in order: "ab" stored, then "cd" whose zlib
  wrapping is LZMA2-compressed
- support/ridge.dll, a support file the reader does not list

Python's lzma and zlib modules produce the compressed bytes. The checksums
are SHA-1 of each piece's wrapped bytes and CRC-32 of the framed blocks.
"""

import hashlib
import lzma
import struct
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / "data"
VERSION = b"Inno Setup Setup Data (5.5.7) (u)"
FILETIME_EPOCH = 0x019DB1DED53E8000
README_UNIX = 1600000000


def u32(value):
    return struct.pack("<I", value)


def u64(value):
    return struct.pack("<Q", value)


def blob(raw):
    return u32(len(raw)) + raw


def utf16(text):
    return blob(text.encode("utf-16-le"))


def flags(bits, value=0):
    return int(value).to_bytes((bits + 7) // 8, "little")


def frame(payload, compressed):
    blob_bytes = bytearray()
    position = 0
    while True:
        piece = payload[position : position + 4096]
        position += len(piece)
        blob_bytes += struct.pack("<I", zlib.crc32(piece) & 0xFFFFFFFF)
        blob_bytes += piece
        if position >= len(payload):
            break
    head = struct.pack("<IB", len(blob_bytes), compressed)
    return struct.pack("<I", zlib.crc32(head) & 0xFFFFFFFF) + head + bytes(blob_bytes)


def lzma1(plain):
    filters = [{"id": lzma.FILTER_LZMA1, "lc": 3, "lp": 0, "pb": 2, "dict_size": 65536}]
    raw = lzma.compress(plain, format=lzma.FORMAT_RAW, filters=filters)
    props = bytes([0x5D]) + (65536).to_bytes(4, "little")
    wrapped = props + b"\xff" * 8 + raw
    if lzma.decompress(wrapped, format=lzma.FORMAT_ALONE) != plain:
        raise SystemExit("LZMA header did not round-trip")
    return props + raw


def lzma2(plain):
    filters = [{"id": lzma.FILTER_LZMA2, "dict_size": 4096}]
    raw = lzma.compress(plain, format=lzma.FORMAT_RAW, filters=filters)
    if lzma.decompress(raw, format=lzma.FORMAT_RAW, filters=filters) != plain:
        raise SystemExit("LZMA2 piece did not round-trip")
    return bytes([0]) + raw


def header_body():
    body = bytearray()
    for index in range(28):
        body += utf16("Ridge" if index == 0 else "")
    body += blob(b"")
    body += blob(b"")
    body += blob(b"")
    body += blob(b"")
    # language, message, permission, type, component, task, directory,
    # file, data entry, and the eight counts after the files, left empty.
    counts = [1, 1, 1, 1, 1, 1, 1, 5, 4, 0, 0, 0, 0, 0, 0, 0]
    for count in counts:
        body += u32(count)
    body += b"\x00" * 20
    body += u32(0) + u32(0)
    body += bytes([0])
    body += b"\x00" * 20
    body += b"\x00" * 8
    body += u64(0)
    body += u32(1)
    body += bytes([0, 0, 0, 0, 0, 4, 0, 0, 0, 0])
    body += u64(0)
    body += flags(47, 0)
    for _ in range(10):
        body += blob(b"")
    body += u32(0) + b"\x00" * 16 + bytes([0])
    body += blob(b"") + blob(b"") + u32(0)
    body += blob(b"")
    for _ in range(4):
        body += blob(b"")
    body += b"\x00" * 20 + flags(1, 0) + bytes([0]) + u64(0)
    for _ in range(5):
        body += blob(b"")
    body += u64(0) + struct.pack("<i", 0) + bytes([0]) + b"\x00" * 20 + flags(5, 0) + u64(0)
    for _ in range(6):
        body += blob(b"")
    body += struct.pack("<i", 0) + bytes([0]) + b"\x00" * 20 + flags(5, 0)
    body += blob(b"")
    for _ in range(6):
        body += blob(b"")
    body += u32(0) + b"\x00" * 20 + struct.pack("<h", 0) + flags(5, 0)
    return bytes(body), counts


def file_entry(source, dest, conditions, location):
    entry = bytearray()
    entry += utf16(source)
    entry += utf16(dest)
    entry += utf16("")
    entry += utf16("")
    for condition in conditions:
        entry += utf16(condition)
    entry += b"\x00" * 20
    entry += u32(location)
    entry += u32(0)
    entry += u64(0)
    entry += struct.pack("<h", 0)
    entry += flags(32, 0)
    entry += bytes([0])
    return bytes(entry)


def data_entry(offset, suboffset, original, compressed, digest, filetime, entry_flags):
    entry = bytearray()
    entry += u32(1)
    entry += u32(1)
    entry += u32(offset)
    entry += u64(suboffset)
    entry += u64(original)
    entry += u64(compressed)
    entry += digest
    entry += u64(filetime)
    entry += u32(0)
    entry += u32(0)
    entry += struct.pack("<H", entry_flags)
    if len(entry) != 74:
        raise SystemExit(f"data entry is {len(entry)} bytes")
    return bytes(entry)


def before(path, parts, dependency=False):
    name = "before_install_dependency" if dependency else "before_install"
    return f"{name}('0123456789abcdef0123456789abcdef', '{path}', {parts})"


def after(wrapped, inflated, dependency=False):
    name = "after_install_dependency" if dependency else "after_install"
    return f"{name}('0123456789abcdef0123456789abcdef', {wrapped}, {inflated})"


def chunk(payload):
    return b"zlb\x1a" + payload


def build():
    readme = zlib.compress(b"ridge\n")
    readme_stored = b"xyz" + readme
    notes_ab = zlib.compress(b"ab")
    notes_cd = zlib.compress(b"cd")
    notes_cd_lzma = lzma2(notes_cd)
    support = zlib.compress(b"no\n")
    pieces = [
        ("support", support, 0, len(support), 0x0002, 0),
        ("readme", readme_stored, 3, len(readme), 0x0002, README_UNIX * 10000000 + FILETIME_EPOCH),
        ("ab", notes_ab, 0, len(notes_ab), 0x0002, 0),
        ("cd", notes_cd_lzma, 0, len(notes_cd), 0x0082, 0),
    ]
    # SHA-1 covers the wrapped bytes, not a stored prefix.
    windows = {
        "support": support,
        "readme": readme,
        "ab": notes_ab,
        "cd": notes_cd,
    }
    chunks = bytearray()
    entries = bytearray()
    offsets = {}
    for name, payload, suboffset, original, entry_flags, filetime in pieces:
        offsets[name] = len(chunks)
        chunks += chunk(payload)
        entries += data_entry(
            offsets[name],
            suboffset,
            original,
            len(payload),
            hashlib.sha1(windows[name]).digest(),
            filetime,
            entry_flags,
        )
    conditions = [""] * 6
    files = bytearray()
    files += file_entry("", "", conditions, 0xFFFFFFFF)
    dep = conditions[:]
    dep[5] = before("support/ridge.dll", 1, dependency=True)
    dep[4] = after(len(support), len(b"no\n"), dependency=True)
    files += file_entry("", "", dep, 0)
    readme_cond = conditions[:]
    readme_cond[5] = before("docs\\readme.txt", 1)
    readme_cond[4] = after(len(readme), len(b"ridge\n"))
    files += file_entry("", "", readme_cond, 1)
    notes_first = conditions[:]
    notes_first[5] = before("notes.txt", 2)
    notes_first[4] = after(len(notes_ab), len(b"ab"))
    files += file_entry("", "", notes_first, 2)
    notes_second = conditions[:]
    notes_second[4] = after(len(notes_cd), len(b"cd"))
    files += file_entry("", "", notes_second, 3)
    body, _counts = header_body()
    primary = body + files
    header_block = frame(lzma1(primary), 1)
    entries_block = frame(bytes(entries), 0)
    ident = VERSION + b"\x00" * (64 - len(VERSION))
    if len(ident) != 64:
        raise SystemExit("version text is not 33 bytes")
    setup = bytes(chunks) + ident + header_block + entries_block
    return setup, len(chunks), {
        "readme": zlib.decompress(readme),
        "notes": b"ab" + b"cd",
        "support": zlib.decompress(support),
    }


def skip_blob(data, at):
    if at + 4 > len(data):
        raise SystemExit("a length is outside the header")
    length = struct.unpack_from("<I", data, at)[0]
    at += 4
    if length > 32 * 1024 * 1024 or at + length > len(data):
        raise SystemExit("a length is outside the header")
    return at + length, data[at : at + length]


def read_utf16(data, at):
    at, raw = skip_blob(data, at)
    if len(raw) % 2 != 0:
        raise SystemExit("a text length is odd")
    return at, raw.decode("utf-16-le")


def framed_payload(block):
    expect, stored, compressed = struct.unpack_from("<IIB", block, 0)
    if (zlib.crc32(block[4:9]) & 0xFFFFFFFF) != expect:
        raise SystemExit("a block checksum does not match")
    blob_bytes = block[9 : 9 + stored]
    if 9 + stored != len(block):
        raise SystemExit("a block length does not match")
    payload = bytearray()
    position = 0
    while position < len(blob_bytes):
        if len(blob_bytes) - position < 4:
            raise SystemExit("a block chunk is short")
        chunk_crc = struct.unpack_from("<I", blob_bytes, position)[0]
        position += 4
        piece = blob_bytes[position : position + 4096]
        position += len(piece)
        if (zlib.crc32(piece) & 0xFFFFFFFF) != chunk_crc:
            raise SystemExit("a block chunk checksum does not match")
        payload += piece
    if compressed:
        wrapped = bytes(payload[:5]) + b"\xff" * 8 + bytes(payload[5:])
        return lzma.decompress(wrapped, format=lzma.FORMAT_ALONE)
    return bytes(payload)


def walk_header(primary):
    """The same skips the reader makes, so a wrong field width fails here."""
    at = 0
    for _ in range(28 + 3 + 1):
        at, _raw = skip_blob(primary, at)
    counts = []
    for _ in range(16):
        counts.append(struct.unpack_from("<I", primary, at)[0])
        at += 4
    if counts[7] != 5 or counts[8] != 4:
        raise SystemExit(f"header counts are {counts}")
    at += 20 + 8 + 1 + 20 + 8 + 8 + 4 + 5
    if primary[at] != 4:
        raise SystemExit("compression is not LZMA2")
    at += 1 + 4 + 8 + 6
    # language, message, permission, type, component, task, directory
    for _ in range(counts[0]):
        for _blob in range(10):
            at, _raw = skip_blob(primary, at)
        at += 4 + 16 + 1
    for _ in range(counts[1]):
        at, _raw = skip_blob(primary, at)
        at, _raw = skip_blob(primary, at)
        at += 4
    for _ in range(counts[2]):
        at, _raw = skip_blob(primary, at)
    for _ in range(counts[3]):
        for _blob in range(4):
            at, _raw = skip_blob(primary, at)
        at += 20 + 1 + 1 + 8
    for _ in range(counts[4]):
        for _blob in range(5):
            at, _raw = skip_blob(primary, at)
        at += 8 + 4 + 1 + 20 + 1 + 8
    for _ in range(counts[5]):
        for _blob in range(6):
            at, _raw = skip_blob(primary, at)
        at += 4 + 1 + 20 + 1
    for _ in range(counts[6]):
        for _blob in range(7):
            at, _raw = skip_blob(primary, at)
        at += 4 + 20 + 2 + 1
    files = []
    for _ in range(counts[7]):
        for _blob in range(8):
            at, _raw = skip_blob(primary, at)
        at, after = read_utf16(primary, at)
        at, before = read_utf16(primary, at)
        at += 20
        location = struct.unpack_from("<I", primary, at)[0]
        at += 4 + 4 + 8 + 2 + 4 + 1
        files.append((before, after, location))
    return files


def check_setup(setup, ident_offset):
    ident = setup[ident_offset : ident_offset + 64]
    if ident != VERSION + b"\x00" * (64 - len(VERSION)):
        raise SystemExit("version text is not 5.5.7 Unicode")
    primary_block_at = ident_offset + 64
    # The first block is LZMA; the second, stored. Both use the framing.
    stored_size = struct.unpack_from("<I", setup, primary_block_at + 4)[0]
    primary = framed_payload(setup[primary_block_at : primary_block_at + 9 + stored_size])
    second_at = primary_block_at + 9 + stored_size
    second_stored = struct.unpack_from("<I", setup, second_at + 4)[0]
    secondary = framed_payload(setup[second_at : second_at + 9 + second_stored])
    if len(secondary) != 4 * 74:
        raise SystemExit(f"data entries are {len(secondary)} bytes")
    files = walk_header(primary)
    expected_before = [
        "",
        before("support/ridge.dll", 1, dependency=True),
        before("docs\\readme.txt", 1),
        before("notes.txt", 2),
        "",
    ]
    if [item[0] for item in files] != expected_before:
        raise SystemExit(f"installed paths are {[item[0] for item in files]}")
    if files[2][2] != 1 or files[4][2] != 3:
        raise SystemExit("file locations do not match the pieces")
    readme = secondary[74 : 148]
    suboffset, original = struct.unpack_from("<QQ", readme, 12)
    digest = readme[36:56]
    if suboffset != 3 or hashlib.sha1(zlib.compress(b"ridge\n")).digest() != digest:
        raise SystemExit("the readme piece checksum is wrong")
    if original != len(zlib.compress(b"ridge\n")):
        raise SystemExit("the readme piece size is wrong")


def main():
    setup, ident_offset, plain = build()
    if plain["readme"] != b"ridge\n" or plain["notes"] != b"abcd" or plain["support"] != b"no\n":
        raise SystemExit("fixture plaintext is wrong")
    check_setup(setup, ident_offset)
    DATA.mkdir(parents=True, exist_ok=True)
    (DATA / "setup-body.bin").write_bytes(setup)
    (DATA / "layout.txt").write_text(f"{ident_offset}\n", encoding="ascii")
    print(f"wrote {DATA / 'setup-body.bin'} ({len(setup)} bytes), ident at {ident_offset}")


if __name__ == "__main__":
    sys.exit(main())
