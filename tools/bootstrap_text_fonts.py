#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Fetch FreeType and the text fonts, pinned, and build them into local/deps.

The engine draws text the game's own fonts cannot draw with FreeType, from
fonts that travel with the game (src/platform/text-font). This script
downloads FreeType 2.14.3 and the fonts from their projects' own releases,
checks every download against its SHA-256 and every licence text against
the one licenses/ holds, and builds:

  local/deps/freetype-install-2.14.3
      FreeType, a static library for this machine, without zlib, bzip2,
      PNG, HarfBuzz or Brotli and with only the modules the fonts need
      (tools/text-fonts/freetype_build_options.cmake); the folder is named
      after the release, so that a project building on the engine that
      pins another FreeType in the same local/deps keeps its own
  local/deps/text-fonts
      the fonts the build copies beside the game: DejaVu Sans Bold and
      DejaVu Sans 2.37, Noto Sans CJK SC Bold, the endonym face cut from
      it, and Noto Emoji

Noto Sans CJK SC Bold is cut down to the characters of GB 2312, of the
Table of General Standard Chinese Characters (the 8,105 hanzi of 2013), of
Big5's symbols and common hanzi, of JIS X 0208, of KS X 1001 without its
hanja, and of the CJK punctuation, kana, bopomofo, Hangul jamo and
full-width forms: about 15,300 characters in 3.9 MB instead of 16 MB. The
table's characters are those the Unicode Character Database's Unihan files
mark with a kTGH position; the archive is read at build time only and
nothing of it ships. The cut is made with the
fontTools that tools/text-fonts/requirements.txt pins, installed in a
virtual environment of its own (local/deps/fonttools-venv), and must give
the pinned SHA-256. --full-cjk ships the whole face instead.

The endonym face, NotoSansCJKsc-Bold-Endonyms.otf, is that same face cut
to the characters of tools/text-fonts/endonyms.txt that neither DejaVu
Sans Bold nor DejaVu Sans holds: the languages' own names and the lines
of the notice shown before a language pack is installed. It is at most
64 KiB and its SHA-256 is pinned. Its record is
local/deps/text-fonts.records.json, beside the fonts folder.

The fonts folder is shared by every version of the engine, so this script
only adds to it. It makes a font of its own list that is missing or whose
record differs, writing the file to a temporary name and renaming it into
place, and it never deletes or rewrites any other file. The four fonts an
earlier bootstrap made are current when they are present and
build-settings.json holds their record unchanged. That file is written
only when this script makes those four, so an earlier bootstrap finds
them current and leaves the folder alone.

--self-test checks the pins, the character set and the endonym list, and
downloads nothing. tools/bootstrap_macos_deps.py and
tools/bootstrap_windows_deps.py build the same FreeType for the release and
the Windows targets.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import venv
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import bootstrap_sdl  # noqa: E402

ROOT = bootstrap_sdl.ROOT
FREETYPE_VERSION = "2.14.3"
FREETYPE_SHA256 = "e61b31ab26358b946e767ed7eb7f4bb2e507da1cfefeb7a8861ace7fd5c899a1"
FREETYPE_URL = f"https://download.savannah.gnu.org/releases/freetype/freetype-{FREETYPE_VERSION}.tar.gz"
# FreeType's own options: a static library that reads the fonts and nothing
# else, and tools/text-fonts/freetype_build_options.cmake, read after its
# project() call, which keeps only the modules the fonts need.
FREETYPE_OPTIONS = [
    "-DBUILD_SHARED_LIBS=OFF",
    "-DFT_DISABLE_ZLIB=ON",
    "-DFT_DISABLE_BZIP2=ON",
    "-DFT_DISABLE_PNG=ON",
    "-DFT_DISABLE_HARFBUZZ=ON",
    "-DFT_DISABLE_BROTLI=ON",
    f"-DCMAKE_PROJECT_INCLUDE={(ROOT / 'tools' / 'text-fonts' / 'freetype_build_options.cmake').as_posix()}",
]
# What a FreeType install prefix holds once complete.
FREETYPE_INSTALLED = "lib/cmake/freetype/freetype-config.cmake"

# The downloads: each file's name in local/deps, where it comes from and its
# SHA-256.
DOWNLOADS = {
    "dejavu": ("dejavu-fonts-ttf-2.37.zip",
               "https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/"
               "dejavu-fonts-ttf-2.37.zip",
               "7576310b219e04159d35ff61dd4a4ec4cdba4f35c00e002a136f00e96a908b0a"),
    "noto-cjk": ("NotoSansCJK-Bold-2.004.ttc",
                 "https://github.com/notofonts/noto-cjk/raw/Sans2.004/Sans/OTC/NotoSansCJK-Bold.ttc",
                 "faa5f3656a78b2e2d450d27fe8382c778bc2b6bb5ea29c986664a6a435056ceb"),
    "noto-cjk-licence": ("NotoSansCJK-2.004-LICENSE",
                         "https://github.com/notofonts/noto-cjk/raw/Sans2.004/LICENSE",
                         "6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2"),
    "noto-emoji": ("NotoEmoji-3.002.ttf",
                   "https://raw.githubusercontent.com/google/fonts/b979dba422e445492b0eb9951ac52ee0b4d648c3/"
                   "ofl/notoemoji/NotoEmoji%5Bwght%5D.ttf",
                   "de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551"),
    "noto-emoji-licence": ("NotoEmoji-3.002-OFL.txt",
                           "https://raw.githubusercontent.com/google/fonts/b979dba422e445492b0eb9951ac52ee0b4d648c3/"
                           "ofl/notoemoji/OFL.txt",
                           "500bb1ccf43df7bbb522112f9133a52b16e1c35e809632f5d8609b179152de5b"),
    # The Unihan files of Unicode 16.0, whose kTGH field lists the Table of
    # General Standard Chinese Characters; read to choose the CJK cut's
    # characters, never shipped.
    "unihan": ("Unihan-16.0.0.zip",
               "https://www.unicode.org/Public/16.0.0/ucd/Unihan.zip",
               "b8f000df69de7828d21326a2ffea462b04bc7560022989f7cc704f10521ef3e0"),
}
# The member of the Unihan archive that holds the kTGH field.
UNIHAN_GENERAL_STANDARD_FILE = "Unihan_OtherMappings.txt"
# How many characters the Table of General Standard Chinese Characters holds.
GENERAL_STANDARD_CHARACTERS = 8105
# The fonts the game ships. The names are the engine's face_files
# (oa/platform/text_font.hpp). That array follows the Face values, with
# the endonym face last so the earlier faces keep their values; this tuple
# lists the endonym face beside the CJK face.
FONT_FILES = ("DejaVuSans-Bold.ttf", "DejaVuSans.ttf", "NotoSansCJKsc-Bold.otf",
              "NotoSansCJKsc-Bold-Endonyms.otf", "NotoEmoji.ttf")
# The four fonts an earlier bootstrap recorded in build-settings.json.
LEGACY_FONT_FILES = ("DejaVuSans-Bold.ttf", "DejaVuSans.ttf", "NotoSansCJKsc-Bold.otf",
                     "NotoEmoji.ttf")
# The face of the CJK collection the game ships: Noto Sans CJK SC Bold, whose
# character map gives the Simplified Chinese forms.
CJK_FACE_INDEX = 2
CJK_FACE_NAME = "Noto Sans CJK SC Bold"
# The SHA-256 of the cut-down CJK face the pinned fontTools makes.
CJK_SUBSET_SHA256 = "1bff16d425a0bc5048fddb7d33d2a59e08647d4a29380a348ccaabcbd89a29f2"
# How many characters the cut keeps: those of CJK_CHARACTERS the face holds.
CJK_SUBSET_CHARACTERS = 15309
# The endonym face: SC Bold cut to tools/text-fonts/endonyms.txt.
ENDONYM_LIST = ROOT / "tools" / "text-fonts" / "endonyms.txt"
ENDONYM_FONT = "NotoSansCJKsc-Bold-Endonyms.otf"
# The SHA-256 and character count of the cut the pinned fontTools makes.
ENDONYM_SUBSET_SHA256 = "98c6b1f26c100411670f568efab9cd347633ccaf76875725d0b0043a2b05e41d"
ENDONYM_SUBSET_CHARACTERS = 41
# The endonym face's record, beside the fonts folder, never inside it.
ENDONYM_RECORDS = "text-fonts.records.json"
# Each licence text in licenses/ and the file of a download, or a member of
# an archive, that must hold the same text.
LICENCES = {
    "licenses/FreeType-FTL.txt": ("freetype", f"freetype-{FREETYPE_VERSION}/docs/FTL.TXT"),
    "licenses/DejaVu-LICENSE.txt": ("dejavu", "dejavu-fonts-ttf-2.37/LICENSE"),
    "licenses/NotoSansCJK-OFL.txt": ("noto-cjk-licence", None),
    "licenses/NotoEmoji-OFL.txt": ("noto-emoji-licence", None),
}
FONTTOOLS_REQUIREMENTS = ROOT / "tools" / "text-fonts" / "requirements.txt"
SETTINGS_FILE = "build-settings.json"


def general_standard_characters(unihan):
    """The code points of the Table of General Standard Chinese Characters.

    They are the characters the Unihan archive's kTGH field gives a
    position in the table of 2013; the archive must list all 8,105.
    """
    found = set()
    with zipfile.ZipFile(unihan) as archive:
        for line in archive.read(UNIHAN_GENERAL_STANDARD_FILE).decode("utf-8").splitlines():
            fields = line.split("\t")
            if len(fields) == 3 and fields[1] == "kTGH" and fields[0].startswith("U+"):
                found.add(int(fields[0][2:], 16))
    if len(found) != GENERAL_STANDARD_CHARACTERS:
        raise RuntimeError(f"{unihan} lists {len(found)} general standard characters, "
                           f"not {GENERAL_STANDARD_CHARACTERS}")
    return found


def cjk_characters(general_standard=()):
    """The characters the cut-down CJK font keeps, as a sorted list of code points.

    The two-byte characters of GB 2312, of Big5's symbols and level-1 hanzi
    (lead bytes 0xA1-0xC6), of JIS X 0208 (EUC-JP rows 1-84), and of KS X
    1001's symbol and Hangul rows (lead bytes 0xA1-0xAC and 0xB0-0xC8, the
    hanja left out), as Python's codecs map them, the blocks of CJK
    punctuation, kana, bopomofo, Hangul compatibility jamo and half- and
    full-width forms, and the code points of general_standard, the Table of
    General Standard Chinese Characters (general_standard_characters).
    """
    def two_byte(codec, leads, trails):
        found = set()
        for lead in leads:
            for trail in trails:
                try:
                    text = bytes((lead, trail)).decode(codec)
                except UnicodeDecodeError:
                    continue
                if len(text) == 1:
                    found.add(ord(text))
        return found

    characters = set()
    characters |= two_byte("gb2312", range(0xA1, 0xF8), range(0xA1, 0xFF))
    characters |= two_byte("big5", range(0xA1, 0xC7), [*range(0x40, 0x7F), *range(0xA1, 0xFF)])
    characters |= two_byte("euc_jp", range(0xA1, 0xF5), range(0xA1, 0xFF))
    characters |= two_byte("euc_kr", [*range(0xA1, 0xAD), *range(0xB0, 0xC9)], range(0xA1, 0xFF))
    for first, last in ((0x3000, 0x30FF), (0x3100, 0x312F), (0x3130, 0x318F), (0xFF00, 0xFFEF)):
        characters |= set(range(first, last + 1))
    characters |= set(general_standard)
    return sorted(characters)


def fetch(deps, key):
    """The path of a verified download under deps, fetched unless present."""
    name, url, sha256 = DOWNLOADS[key]
    path = deps / name
    bootstrap_sdl.fetch_archive(path, url, sha256)
    return path


def freetype_source(deps):
    """The verified, extracted FreeType source tree under deps."""
    deps.mkdir(parents=True, exist_ok=True)
    archive = deps / f"freetype-{FREETYPE_VERSION}.tar.gz"
    bootstrap_sdl.fetch_archive(archive, FREETYPE_URL, FREETYPE_SHA256)
    source = deps / f"freetype-{FREETYPE_VERSION}"
    bootstrap_sdl.extract_source(archive, source)
    return source


def check_licences(deps):
    """Fails unless every licence text of licenses/ is the one its download holds."""
    for notice, (key, member) in LICENCES.items():
        if key == "freetype":
            text = (freetype_source(deps).parent / member).read_bytes()
        elif member is None:
            text = fetch(deps, key).read_bytes()
        else:
            with zipfile.ZipFile(fetch(deps, key)) as archive:
                text = archive.read(member)
        if text != (ROOT / notice).read_bytes():
            raise RuntimeError(f"{notice} differs from the licence {DOWNLOADS.get(key, ('FreeType',))[0]} holds")


def is_current(folder, files, wanted):
    """Whether folder holds files and records the settings wanted."""
    if not all((folder / name).exists() for name in files):
        return False
    try:
        return json.loads((folder / SETTINGS_FILE).read_text()) == wanted
    except (OSError, ValueError):
        return False


def record(folder, wanted):
    """Records in folder the settings it was made with."""
    (folder / SETTINGS_FILE).write_text(json.dumps(wanted, indent=2, sort_keys=True) + "\n")


def build_freetype(deps, jobs):
    """Builds FreeType for this machine into deps/freetype-install-<release> unless it is current."""
    install = deps / f"freetype-install-{FREETYPE_VERSION}"
    wanted = {"version": FREETYPE_VERSION, "sha256": FREETYPE_SHA256, "options": FREETYPE_OPTIONS}
    if is_current(install, [FREETYPE_INSTALLED], wanted):
        return install
    build = deps / f"freetype-build-{FREETYPE_VERSION}"
    shutil.rmtree(build, ignore_errors=True)
    shutil.rmtree(install, ignore_errors=True)
    subprocess.run(["cmake", "-S", str(freetype_source(deps)), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
                    f"-DCMAKE_INSTALL_PREFIX={install}", *FREETYPE_OPTIONS], check=True)
    subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--parallel", str(jobs)], check=True)
    subprocess.run(["cmake", "--install", str(build), "--config", "Release"], check=True)
    record(install, wanted)
    return install


def fonttools_python(deps):
    """The Python of the virtual environment that holds the pinned fontTools, made on first use."""
    environment = deps / "fonttools-venv"
    names = (pathlib.Path("bin") / "python", pathlib.Path("Scripts") / "python.exe")
    python = next((environment / name for name in names if (environment / name).is_file()), None)
    stamp = environment / "requirements.txt"
    if python is None or not stamp.is_file() or stamp.read_bytes() != FONTTOOLS_REQUIREMENTS.read_bytes():
        print(f"Installing the pinned fontTools into {environment}", flush=True)
        venv.EnvBuilder(with_pip=True, clear=True).create(environment)
        python = next(environment / name for name in names if (environment / name).is_file())
        subprocess.run([str(python), "-m", "pip", "install", "--quiet", "--require-hashes", "-r",
                        str(FONTTOOLS_REQUIREMENTS)], check=True)
        shutil.copyfile(FONTTOOLS_REQUIREMENTS, stamp)
    return python


def cut_face(collection, face, unicodes, out):
    """Writes one face of a collection to out, cut to unicodes.

    Layout tables are dropped, since the engine shapes nothing, and the
    hinting kept; the font's modification time is left as it was, so the
    output depends only on its input. The vertical metrics are kept, so a
    cut of Noto Sans CJK SC Bold has the same rows as the face.
    """
    from fontTools import subset
    from fontTools.ttLib import TTFont

    font = TTFont(collection, fontNumber=face, recalcTimestamp=False, lazy=False)
    options = subset.Options()
    options.layout_features = []
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.notdef_outline = True
    options.recalc_timestamp = False
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=list(unicodes))
    subsetter.subset(font)
    font.save(out)


def cut_cjk_face(collection, unihan, face, out, full):
    """Writes one face of the CJK collection to out: whole when full, else cut to cjk_characters(),
    the general standard characters of the Unihan archive among them.

    Runs in the fontTools environment (--cut-cjk-face).
    """
    from fontTools.ttLib import TTFont

    if full:
        font = TTFont(collection, fontNumber=face, recalcTimestamp=False, lazy=False)
        font.save(out)
        return
    font = TTFont(collection, fontNumber=face, recalcTimestamp=False, lazy=False)
    held = font.getBestCmap()
    font.close()
    kept = [c for c in cjk_characters(general_standard_characters(unihan)) if c in held]
    if len(kept) != CJK_SUBSET_CHARACTERS:
        raise RuntimeError(f"the face holds {len(kept)} of the characters, not {CJK_SUBSET_CHARACTERS}")
    cut_face(collection, face, kept, out)


def endonym_lines(path=None):
    """The strings of the endonym list: comments and blank lines left out.

    The file must be UTF-8 and hold no tab.
    """
    path = ENDONYM_LIST if path is None else path
    raw = path.read_bytes()
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise RuntimeError(f"{path} is not UTF-8") from error
    if b"\t" in raw:
        raise RuntimeError(f"{path} holds a tab")
    lines = []
    for line in text.splitlines():
        if line.startswith("#") or line.strip() == "":
            continue
        lines.append(line)
    return lines


def endonym_characters(lines, sans_cmaps, cjk_cmap):
    """The code points the endonym face keeps, sorted.

    Each non-ASCII character of lines that neither DejaVu Sans Bold nor
    DejaVu Sans holds. The sans faces come first in the chain, so a
    character either of them holds needs no place in the cut. A character
    no face holds is named in the error, every such character in one.
    """
    held_by_sans = set()
    for cmap in sans_cmaps:
        held_by_sans.update(cmap)
    missing = []
    seen = set()
    kept = set()
    for line in lines:
        for character in line:
            code = ord(character)
            if code < 128 or code in held_by_sans:
                continue
            if code in cjk_cmap:
                kept.add(code)
                continue
            if code not in seen:
                seen.add(code)
                missing.append(character)
    if missing:
        named = ", ".join(f"U+{ord(character):04X} ({character})" for character in missing)
        raise RuntimeError(f"no face holds {named}")
    return sorted(kept)


def best_cmap(path, face=0):
    """The font's best character map, as code point to glyph id."""
    from fontTools.ttLib import TTFont

    font = TTFont(path, fontNumber=face, recalcTimestamp=False, lazy=False)
    try:
        return font.getBestCmap() or {}
    finally:
        font.close()


def cut_endonym_face(collection, dejavu_bold, dejavu, out):
    """Writes the endonym face to out, cut from SC Bold of the collection.

    Reads the endonym list, the two sans fonts' character maps and the
    collection's face 2. Runs where fontTools is installed
    (--cut-endonym-face).
    """
    kept = endonym_characters(endonym_lines(),
                              (best_cmap(dejavu_bold), best_cmap(dejavu)),
                              best_cmap(collection, CJK_FACE_INDEX))
    if len(kept) != ENDONYM_SUBSET_CHARACTERS:
        raise RuntimeError(f"the endonym face keeps {len(kept)} characters, "
                           f"not {ENDONYM_SUBSET_CHARACTERS}")
    cut_face(collection, CJK_FACE_INDEX, kept, out)
    print(f"{pathlib.Path(out).name}: {len(kept)} characters", flush=True)


def write_over(path, data):
    """Writes data to path by renaming a temporary file over it."""
    part = path.with_name(path.name + ".part")
    part.write_bytes(data)
    part.replace(path)


def legacy_settings(full_cjk):
    """The build-settings.json dictionary the four earlier fonts were recorded with."""
    return {"downloads": {key: sha256 for key, (_, _, sha256) in DOWNLOADS.items()},
            "cjk_face": CJK_FACE_INDEX, "full_cjk": full_cjk,
            "fonttools": FONTTOOLS_REQUIREMENTS.read_text()}


def endonym_record():
    """The endonym face's settings: the collection, fontTools and the list."""
    return {"collection": DOWNLOADS["noto-cjk"][2],
            "fonttools": FONTTOOLS_REQUIREMENTS.read_text(),
            "list": hashlib.sha256(ENDONYM_LIST.read_bytes()).hexdigest()}


def read_records(deps):
    """The font records beside the fonts folder, or none when the file is absent or unreadable."""
    path = deps / ENDONYM_RECORDS
    try:
        found = json.loads(path.read_text())
    except (OSError, ValueError):
        return {}
    return found if isinstance(found, dict) else {}


def write_records(deps, records):
    """Writes the font records beside the fonts folder, renaming a temporary file over."""
    path = deps / ENDONYM_RECORDS
    part = path.with_name(path.name + ".part")
    part.write_text(json.dumps(records, indent=2, sort_keys=True) + "\n")
    part.replace(path)


def make_legacy_fonts(deps, fonts, full_cjk):
    """Makes the four earlier fonts, each renamed into place, and records their settings."""
    fonts.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(fetch(deps, "dejavu")) as archive:
        for name in ("DejaVuSans-Bold.ttf", "DejaVuSans.ttf"):
            write_over(fonts / name, archive.read(f"dejavu-fonts-ttf-2.37/ttf/{name}"))
    write_over(fonts / "NotoEmoji.ttf", fetch(deps, "noto-emoji").read_bytes())
    part = fonts / "NotoSansCJKsc-Bold.otf.part"
    print(f"Writing NotoSansCJKsc-Bold.otf "
          f"({'the whole face' if full_cjk else 'cut to the common characters'})", flush=True)
    command = [str(fonttools_python(deps)), str(pathlib.Path(__file__).resolve()), "--cut-cjk-face",
               str(fetch(deps, "noto-cjk")), str(fetch(deps, "unihan")), str(part)]
    if full_cjk:
        command.append("--full-cjk")
    subprocess.run(command, check=True)
    if not full_cjk:
        digest = hashlib.sha256(part.read_bytes()).hexdigest()
        if digest != CJK_SUBSET_SHA256:
            part.unlink()
            raise RuntimeError(f"the cut CJK face has SHA-256 {digest}, not the pinned {CJK_SUBSET_SHA256}")
    part.replace(fonts / "NotoSansCJKsc-Bold.otf")
    record(fonts, legacy_settings(full_cjk))


def make_endonym_font(deps, fonts):
    """Makes the endonym face when it is missing or its record differs."""
    wanted = endonym_record()
    records = read_records(deps)
    path = fonts / ENDONYM_FONT
    if path.is_file() and records.get(ENDONYM_FONT) == wanted:
        return
    part = fonts / f"{ENDONYM_FONT}.part"
    print(f"Writing {ENDONYM_FONT} (cut to the languages' own names and the notice)", flush=True)
    command = [str(fonttools_python(deps)), str(pathlib.Path(__file__).resolve()), "--cut-endonym-face",
               str(fetch(deps, "noto-cjk")), str(fonts / "DejaVuSans-Bold.ttf"),
               str(fonts / "DejaVuSans.ttf"), str(part)]
    subprocess.run(command, check=True)
    data = part.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    print(f"  {ENDONYM_FONT}: SHA-256 {digest}, {len(data)} bytes", flush=True)
    if digest != ENDONYM_SUBSET_SHA256:
        part.unlink()
        raise RuntimeError(f"the endonym face has SHA-256 {digest}, not the pinned {ENDONYM_SUBSET_SHA256}")
    if len(data) > 65536:
        part.unlink()
        raise RuntimeError(f"the endonym face is {len(data)} bytes, over 64 KiB")
    part.replace(path)
    records[ENDONYM_FONT] = wanted
    write_records(deps, records)


def make_fonts(deps, full_cjk):
    """Puts the game's fonts in deps/text-fonts, adding a font that is missing or out of date.

    The four earlier fonts are made only when one is missing or
    build-settings.json differs, and that file is written only then. The
    endonym face is made when it is missing or its record differs. Nothing
    else in the folder is deleted or rewritten.
    """
    fonts = deps / "text-fonts"
    fonts.mkdir(parents=True, exist_ok=True)
    if not is_current(fonts, LEGACY_FONT_FILES, legacy_settings(full_cjk)):
        make_legacy_fonts(deps, fonts, full_cjk)
    make_endonym_font(deps, fonts)
    return fonts


def self_test():
    """Checks the pins, the font list and the character set; returns the exit status."""
    failures = []
    pins = [("FreeType", FREETYPE_SHA256, FREETYPE_URL)]
    pins += [(key, sha256, url) for key, (_, url, sha256) in DOWNLOADS.items()]
    pins += [("the CJK cut", CJK_SUBSET_SHA256, "https://")]
    pins += [("the endonym cut", ENDONYM_SUBSET_SHA256, "https://")]
    for what, sha256, url in pins:
        if not bootstrap_sdl.SHA256_RE.fullmatch(sha256) or not url.startswith("https://"):
            failures.append(f"the pin of {what} is malformed")
    for notice, (key, _) in LICENCES.items():
        if key != "freetype" and key not in DOWNLOADS:
            failures.append(f"{notice} names no download")
        if not (ROOT / notice).is_file():
            failures.append(f"{notice} is missing")
    characters = cjk_characters()
    if len(characters) != 14066:
        failures.append(f"the CJK character set holds {len(characters)} characters, not 14066")
    for sample in ("中", "國", "日本", "한국어", "ア", "，"):
        if any(ord(c) not in characters for c in sample):
            failures.append(f"the CJK character set lacks {sample}")
    try:
        raw = ENDONYM_LIST.read_bytes()
    except OSError:
        failures.append("the endonym list is missing")
        raw = b""
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        failures.append("the endonym list is not UTF-8")
        text = ""
    if b"\t" in raw:
        failures.append("the endonym list holds a tab")
    non_ascii = set()
    for line in text.splitlines():
        if line.startswith("#") or line.strip() == "":
            continue
        for character in line:
            if ord(character) >= 128:
                non_ascii.add(ord(character))
    if len(non_ascii) < ENDONYM_SUBSET_CHARACTERS:
        failures.append(f"the endonym list holds {len(non_ascii)} non-ASCII characters, "
                        f"fewer than {ENDONYM_SUBSET_CHARACTERS}")
    for failure in failures:
        print(f"bootstrap_text_fonts self-test: {failure}")
    if failures:
        return 1
    print(f"bootstrap_text_fonts self-test: {len(pins)} pins, {len(LICENCES)} licences and "
          f"{len(characters)} CJK characters besides the general standard table, as expected")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--deps", type=pathlib.Path, default=ROOT / "local" / "deps",
                        help="archive, source and build cache (default: local/deps)")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--full-cjk", action="store_true",
                        help="ship the whole Noto Sans CJK SC Bold face (16 MB), not the cut")
    parser.add_argument("--fonts-only", action="store_true",
                        help="make the fonts and build no FreeType (for builds that bring their own)")
    parser.add_argument("--self-test", action="store_true",
                        help="check the pins, the character set and the endonym list, and exit")
    parser.add_argument("--cut-cjk-face", nargs=3, metavar=("COLLECTION", "UNIHAN", "OUT"), help=argparse.SUPPRESS)
    parser.add_argument("--cut-endonym-face", nargs=4,
                        metavar=("COLLECTION", "DEJAVU_BOLD", "DEJAVU", "OUT"), help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.cut_cjk_face:
        cut_cjk_face(args.cut_cjk_face[0], args.cut_cjk_face[1], CJK_FACE_INDEX, args.cut_cjk_face[2],
                     args.full_cjk)
        return 0
    if args.cut_endonym_face:
        cut_endonym_face(*args.cut_endonym_face)
        return 0
    deps = args.deps.resolve()
    deps.mkdir(parents=True, exist_ok=True)
    check_licences(deps)
    if not args.fonts_only:
        install = build_freetype(deps, args.jobs)
        print(f"FreeType {FREETYPE_VERSION} ready in {install}")
    fonts = make_fonts(deps, args.full_cjk)
    for name in FONT_FILES:
        print(f"  {name}: {(fonts / name).stat().st_size} bytes")
    print(f"Text fonts ready in {fonts}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
