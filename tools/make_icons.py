#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Make the game's icons from the three Open Annihilation icons, or check them.

Each source in branding/ is a 1024 by 1024 RGBA PNG with clear corners:

  open-annihilation-icon.png           the app icon, the plates alone
  open-annihilation-settings-icon.png  the plates over two gears
  open-annihilation-mods-icon.png      the plates over drive bays

A run on macOS makes from the app icon, beside it in branding/:

  open-annihilation.icns     the macOS application bundle's icon: every size
                             from 16 to 1024 pixels that macOS asks for
  open-annihilation.ico      the Windows executable's icon: ICO_SIZES, each
                             kept as a PNG
  open-annihilation-256.png  the window icon the game embeds and sets at
                             start-up, WINDOW_ICON_SIZE pixels square

and from the other two, at the same size:

  open-annihilation-settings-256.png
  open-annihilation-mods-256.png

and a 512-pixel picture of each source, for a button larger than 256 pixels:

  open-annihilation-512.png
  open-annihilation-settings-512.png
  open-annihilation-mods-512.png

sips scales an icon and iconutil packs the .icns; both come with macOS, so
making the icons needs macOS. The .ico is packed here. The files it makes
are committed; run it again when an icon changes.

--check changes nothing and needs only Python: it checks that the three
icons and the files made from them have the sizes above, and that each
source's corners are clear. The branding-icons test runs it.

Exit status: 0 when the icons were made, or --check found them as they
should be; 1 when --check finds a problem; 2 when a tool is missing or
fails.
"""
import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BRANDING = ROOT / "branding"
ICON = BRANDING / "open-annihilation-icon.png"
SETTINGS_ICON = BRANDING / "open-annihilation-settings-icon.png"
MODS_ICON = BRANDING / "open-annihilation-mods-icon.png"
ICNS = BRANDING / "open-annihilation.icns"
ICO = BRANDING / "open-annihilation.ico"
WINDOW_ICON = BRANDING / "open-annihilation-256.png"
SETTINGS_WINDOW_ICON = BRANDING / "open-annihilation-settings-256.png"
MODS_WINDOW_ICON = BRANDING / "open-annihilation-mods-256.png"
APP_LARGE_ICON = BRANDING / "open-annihilation-512.png"
SETTINGS_LARGE_ICON = BRANDING / "open-annihilation-settings-512.png"
MODS_LARGE_ICON = BRANDING / "open-annihilation-mods-512.png"
# The three sources, and the 256-pixel pictures made from them.
SOURCE_ICONS = (ICON, SETTINGS_ICON, MODS_ICON)
WINDOW_ICONS = (
    (ICON, WINDOW_ICON),
    (SETTINGS_ICON, SETTINGS_WINDOW_ICON),
    (MODS_ICON, MODS_WINDOW_ICON),
)
# The 512-pixel pictures, for a button the 256-pixel one cannot fill sharply.
LARGE_ICONS = (
    (ICON, APP_LARGE_ICON),
    (SETTINGS_ICON, SETTINGS_LARGE_ICON),
    (MODS_ICON, MODS_LARGE_ICON),
)
# The icon's size, in pixels, square.
ICON_SIZE = 1024
WINDOW_ICON_SIZE = 256
LARGE_ICON_SIZE = 512
# The sizes Windows picks among for the executable, its windows and its
# taskbar button at the usual display scales.
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
# The images of a macOS icon set, by file name, and their sizes.
ICONSET = (
    ("icon_16x16.png", 16), ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32), ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128), ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256), ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512), ("icon_512x512@2x.png", 1024),
)
# The .icns element types that hold an image, and its size in pixels: the
# small ones as ARGB, the others as PNG, the retina ones twice the size
# they are shown at.
ICNS_IMAGE_TYPES = {b"ic04": 16, b"ic05": 32, b"icp4": 16, b"icp5": 32, b"icp6": 64, b"ic11": 32, b"ic12": 64,
                    b"ic07": 128, b"ic13": 256, b"ic08": 256, b"ic14": 512, b"ic09": 512, b"ic10": 1024}
# The element types that hold ARGB pixels rather than a PNG.
ICNS_ARGB_TYPES = frozenset({b"ic04", b"ic05"})
# The first eight bytes of every PNG file, and the start of the IHDR chunk
# that follows them: its length and type, then the width, height, bit depth
# and colour type (6 is RGB with alpha).
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_IHDR = struct.Struct(">I4sIIBB")
PNG_RGBA = 6
# An ICO file's header and its directory entries, little-endian: a width or
# height of 0 means 256.
ICO_HEADER = struct.Struct("<HHH")
ICO_ENTRY = struct.Struct("<BBBBHHII")
ICO_TYPE_ICON = 1
ICO_BITS_PER_PIXEL = 32
# An .icns file's header and each element's header: a type and a length
# that counts the header, big-endian.
ICNS_HEADER = struct.Struct(">4sI")
ICNS_MAGIC = b"icns"
# Bound on a file read here.
MAX_FILE_BYTES = 16 << 20


class IconError(Exception):
    """An icon, or a file made from it, that is not as it should be."""


def png_size(data, what):
    """Reads the size of an 8-bit RGBA PNG.

    @param data the PNG file
    @param what the file's name for messages
    @return (width, height) in pixels
    """
    if len(data) < len(PNG_SIGNATURE) + PNG_IHDR.size or not data.startswith(PNG_SIGNATURE):
        raise IconError(f"{what}: not a PNG file")
    _, chunk, width, height, depth, colour = PNG_IHDR.unpack_from(data, len(PNG_SIGNATURE))
    if chunk != b"IHDR":
        raise IconError(f"{what}: the PNG does not start with its header")
    if depth != 8 or colour != PNG_RGBA:
        raise IconError(f"{what}: {depth}-bit colour type {colour}, not 8-bit RGBA")
    return width, height


def read_file(path):
    """Reads a file of at most MAX_FILE_BYTES.

    @param path the file
    @return its bytes
    """
    if not path.is_file():
        raise IconError(f"{path.relative_to(ROOT)}: missing")
    if path.stat().st_size > MAX_FILE_BYTES:
        raise IconError(f"{path.relative_to(ROOT)}: larger than {MAX_FILE_BYTES} bytes")
    return path.read_bytes()


def ico_images(data, what):
    """Reads the images of an ICO file whose images are PNG files.

    @param data the ICO file
    @param what the file's name for messages
    @return {size: PNG bytes}
    """
    if len(data) < ICO_HEADER.size:
        raise IconError(f"{what}: shorter than its header")
    reserved, kind, count = ICO_HEADER.unpack_from(data)
    if reserved != 0 or kind != ICO_TYPE_ICON:
        raise IconError(f"{what}: not an icon file")
    if len(data) < ICO_HEADER.size + count * ICO_ENTRY.size:
        raise IconError(f"{what}: its directory runs past the end")
    images = {}
    for index in range(count):
        width, height, _, _, _, bits, length, offset = ICO_ENTRY.unpack_from(
            data, ICO_HEADER.size + index * ICO_ENTRY.size)
        width, height = width or 256, height or 256
        if offset + length > len(data):
            raise IconError(f"{what}: image {index} runs past the end")
        image = data[offset:offset + length]
        if bits != ICO_BITS_PER_PIXEL or png_size(image, f"{what} image {index}") != (width, height):
            raise IconError(f"{what}: image {index} is not the {width}x{height} 32-bit PNG its entry names")
        if width != height or width in images:
            raise IconError(f"{what}: image {index}, {width}x{height}, is not one more square size")
        images[width] = image
    return images


def icns_sizes(data, what):
    """Reads the sizes of the images an .icns file holds.

    @param data the .icns file
    @param what the file's name for messages
    @return the sizes, in pixels
    """
    if len(data) < ICNS_HEADER.size:
        raise IconError(f"{what}: shorter than its header")
    magic, length = ICNS_HEADER.unpack_from(data)
    if magic != ICNS_MAGIC or length != len(data):
        raise IconError(f"{what}: not an icns file of {len(data)} bytes")
    sizes = set()
    at = ICNS_HEADER.size
    while at < len(data):
        if at + ICNS_HEADER.size > len(data):
            raise IconError(f"{what}: an element header runs past the end")
        kind, length = ICNS_HEADER.unpack_from(data, at)
        if length < ICNS_HEADER.size or at + length > len(data):
            raise IconError(f"{what}: element {kind!r} runs past the end")
        size = ICNS_IMAGE_TYPES.get(kind)
        if size is not None and kind not in ICNS_ARGB_TYPES:
            image = data[at + ICNS_HEADER.size:at + length]
            if png_size(image, f"{what} {kind.decode()}") != (size, size):
                raise IconError(f"{what}: element {kind.decode()} is not {size}x{size}")
        if size is not None:
            sizes.add(size)
        at += length
    return sizes


def unfiltered_rows(raw, width, height):
    """Undoes a PNG's row filters for an 8-bit RGBA image.

    @param raw the inflated image, each row a filter byte then its pixels
    @param width the image's width
    @param height the image's height
    @return the rows, or None when the image is not that form
    """
    bytes_per_pixel = 4
    row_bytes = width * bytes_per_pixel
    stride = 1 + row_bytes
    if len(raw) < height * stride:
        return None
    rows = []
    previous = bytearray(row_bytes)
    at = 0
    for _ in range(height):
        kind = raw[at]
        at += 1
        row = bytearray(raw[at:at + row_bytes])
        at += row_bytes
        if kind == 1 or kind == 3 or kind == 4:
            for index in range(row_bytes):
                left = row[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
                above = previous[index]
                if kind == 1:
                    predictor = left
                elif kind == 3:
                    predictor = (left + above) // 2
                else:
                    up_left = previous[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
                    estimate = left + above - up_left
                    left_distance = abs(estimate - left)
                    above_distance = abs(estimate - above)
                    diagonal = abs(estimate - up_left)
                    if left_distance <= above_distance and left_distance <= diagonal:
                        predictor = left
                    elif above_distance <= diagonal:
                        predictor = above
                    else:
                        predictor = up_left
                row[index] = (row[index] + predictor) & 0xFF
        elif kind == 2:
            for index in range(row_bytes):
                row[index] = (row[index] + previous[index]) & 0xFF
        elif kind != 0:
            return None
        rows.append(row)
        previous = row
    return rows


def corner_clear(data, what):
    """Tells whether a PNG's four corner pixels are wholly transparent.

    The icons' corners stay clear over every backdrop.

    @param data the PNG file
    @param what the file's name for messages
    @return a problem, or None when the corners are clear
    """
    at = len(PNG_SIGNATURE)
    compressed = []
    while at + 8 <= len(data):
        length, kind = struct.unpack_from(">I4s", data, at)
        at += 8
        if at + length + 4 > len(data):
            return f"{what}: a corner could not be read"
        chunk = data[at:at + length]
        at += length + 4
        if kind == b"IDAT":
            compressed.append(chunk)
        elif kind == b"IEND":
            break
    if not compressed:
        return f"{what}: a corner could not be read"
    try:
        raw = zlib.decompress(b"".join(compressed))
    except zlib.error:
        return f"{what}: a corner could not be read"
    width, height = png_size(data, what)
    rows = unfiltered_rows(raw, width, height)
    if rows is None:
        return f"{what}: a corner could not be read"

    def alpha_at(x, y):
        return rows[y][x * 4 + 3]

    corners = (
        alpha_at(0, 0),
        alpha_at(width - 1, 0),
        alpha_at(0, height - 1),
        alpha_at(width - 1, height - 1),
    )
    if any(value != 0 for value in corners):
        return f"{what}: a corner is not clear"
    return None


def check():
    """Checks the three icons and the files made from them; returns the problems found."""
    problems = []
    for path in SOURCE_ICONS:
        try:
            data = read_file(path)
            if png_size(data, path.name) != (ICON_SIZE, ICON_SIZE):
                problems.append(f"{path.name}: not {ICON_SIZE}x{ICON_SIZE}")
            else:
                problem = corner_clear(data, path.name)
                if problem:
                    problems.append(problem)
        except IconError as error:
            problems.append(str(error))
    for _, path in WINDOW_ICONS:
        try:
            data = read_file(path)
            if png_size(data, path.name) != (WINDOW_ICON_SIZE, WINDOW_ICON_SIZE):
                problems.append(f"{path.name}: not {WINDOW_ICON_SIZE}x{WINDOW_ICON_SIZE}")
            else:
                problem = corner_clear(data, path.name)
                if problem:
                    problems.append(problem)
        except IconError as error:
            problems.append(str(error))
    for _, path in LARGE_ICONS:
        try:
            data = read_file(path)
            if png_size(data, path.name) != (LARGE_ICON_SIZE, LARGE_ICON_SIZE):
                problems.append(f"{path.name}: not {LARGE_ICON_SIZE}x{LARGE_ICON_SIZE}")
            else:
                problem = corner_clear(data, path.name)
                if problem:
                    problems.append(problem)
        except IconError as error:
            problems.append(str(error))
    try:
        sizes = tuple(sorted(ico_images(read_file(ICO), ICO.name)))
        if sizes != ICO_SIZES:
            problems.append(f"{ICO.name}: sizes {sizes}, not {ICO_SIZES}")
    except IconError as error:
        problems.append(str(error))
    try:
        sizes = icns_sizes(read_file(ICNS), ICNS.name)
        wanted = {size for _, size in ICONSET}
        if not wanted <= sizes:
            problems.append(f"{ICNS.name}: no image of {sorted(wanted - sizes)} pixels")
    except IconError as error:
        problems.append(str(error))
    return problems


def scaled(source, size, out):
    """Writes the icon scaled to a square size with sips.

    @param source the icon
    @param size the width and height, in pixels
    @param out the PNG to write
    """
    subprocess.run(["sips", "-s", "format", "png", "-z", str(size), str(size), str(source), "--out", str(out)],
                   check=True, capture_output=True)


def ico_file(images):
    """Packs PNG images into an ICO file.

    @param images {size: PNG bytes}, each square
    @return the ICO file
    """
    header = ICO_HEADER.pack(0, ICO_TYPE_ICON, len(images))
    offset = ICO_HEADER.size + len(images) * ICO_ENTRY.size
    entries, data = [], []
    for size in sorted(images):
        image = images[size]
        side = 0 if size == 256 else size
        entries.append(ICO_ENTRY.pack(side, side, 0, 0, 1, ICO_BITS_PER_PIXEL, len(image), offset))
        data.append(image)
        offset += len(image)
    return header + b"".join(entries) + b"".join(data)


def make():
    """Makes the .icns, the .ico, the 256-pixel icons and the 512-pixel icons; returns the exit status."""
    for tool in ("sips", "iconutil"):
        if shutil.which(tool) is None:
            print(f"make_icons: {tool} is missing; the icons are made on macOS", file=sys.stderr)
            return 2
    try:
        for path in SOURCE_ICONS:
            if png_size(read_file(path), path.name) != (ICON_SIZE, ICON_SIZE):
                raise IconError(f"{path.name}: not {ICON_SIZE}x{ICON_SIZE}")
    except IconError as error:
        print(f"make_icons: {error}", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="oa-icons-") as scratch:
        scratch = Path(scratch)
        try:
            iconset = scratch / "open-annihilation.iconset"
            iconset.mkdir()
            for name, size in ICONSET:
                if size == ICON_SIZE:
                    shutil.copyfile(ICON, iconset / name)
                else:
                    scaled(ICON, size, iconset / name)
            subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(ICNS)], check=True,
                           capture_output=True)
            images = {}
            for size in ICO_SIZES:
                out = scratch / f"ico-{size}.png"
                scaled(ICON, size, out)
                images[size] = out.read_bytes()
            ICO.write_bytes(ico_file(images))
            WINDOW_ICON.write_bytes(images[WINDOW_ICON_SIZE])
            scaled(SETTINGS_ICON, WINDOW_ICON_SIZE, SETTINGS_WINDOW_ICON)
            scaled(MODS_ICON, WINDOW_ICON_SIZE, MODS_WINDOW_ICON)
            for source, out in LARGE_ICONS:
                scaled(source, LARGE_ICON_SIZE, out)
        except subprocess.CalledProcessError as error:
            output = (error.stderr or b"").decode(errors="replace").strip()
            print(f"make_icons: {' '.join(error.cmd)} failed: {output}", file=sys.stderr)
            return 2
    problems = check()
    for problem in problems:
        print(f"make_icons: {problem}", file=sys.stderr)
    if not problems:
        for path in (ICNS, ICO, WINDOW_ICON, SETTINGS_WINDOW_ICON, MODS_WINDOW_ICON,
                     APP_LARGE_ICON, SETTINGS_LARGE_ICON, MODS_LARGE_ICON):
            print(f"make_icons: wrote {path.relative_to(ROOT)}")
    return 1 if problems else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="change nothing; check the icons' sizes")
    if parser.parse_args().check:
        problems = check()
        for problem in problems:
            print(f"make_icons: {problem}")
        print(f"make_icons: {'the icons are not as they should be' if problems else 'the icons are as they should be'}")
        return 1 if problems else 0
    return make()


if __name__ == "__main__":
    sys.exit(main())
