#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Read the strict YAML subset of mod profiles and of the OAMOD registry.

The same rules as the engine's reader (src/formats/oamod/include/oa/formats/oamod.hpp):
UTF-8 without a byte order mark, at most 256 KiB, one document whose top level
is a mapping, at most 16 levels of nesting; block and flow collections; plain,
single- and double-quoted scalars on one line; '#' comments; one optional
'---' before the document and '...' after it. Tags, anchors, aliases, merge
keys, directives, complex keys, block scalars, a second document, duplicate
keys and tabs in indentation are refused with the rule and the position.

Values come back as Python values: dict (keys str, or int for a plain integer
key), list, str, int, float, bool and None. A double-quoted string written
with a byte or Unicode escape (\\x, \\u, \\U) comes back as an EscapedStr, so
that a generator can keep those bytes escaped in what it writes.

loads(text) reads a document; loads_value(text) reads one value of any kind.
"""

import re
from pathlib import Path

MAX_INPUT_BYTES = 256 * 1024
MAX_NESTING_DEPTH = 16
MAX_NODE_COUNT = 1 << 16
MAX_STRING_BYTES = 4096
MAX_INTEGER_MAGNITUDE = 1 << 53
MAX_SIGNIFICANT_DIGITS = 15
MAX_DECIMAL_EXPONENT = 300

NUMBER_RE = re.compile(r"^-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][-+]?[0-9]+)?$")
NULLS = {"", "~", "null", "Null", "NULL"}
TRUES = {"true", "True", "TRUE"}
FALSES = {"false", "False", "FALSE"}
ESCAPES = {"0": "\0", "a": "\a", "b": "\b", "t": "\t", "\t": "\t", "n": "\n", "v": "\v", "f": "\f",
           "r": "\r", "e": "\x1b", " ": " ", '"': '"', "/": "/", "\\": "\\", "N": "\x85",
           "_": "\xa0", "L": " ", "P": " "}
HEX_ESCAPES = {"x": 2, "u": 4, "U": 8}


class EscapedStr(str):
    """A double-quoted string written with byte or Unicode escapes."""


class OamodYamlError(Exception):
    """A text that breaks a rule of the subset."""

    def __init__(self, rule, line, column):
        self.rule, self.line, self.column = rule, line, column
        super().__init__(f"{line}:{column}: {rule}")


def number_value(text, leading_zeros=False):
    """The int or float a number's text stands for, None when it is not a number.

    Raises OamodYamlError's rule names (as ValueError arguments) for numbers
    beyond what a profile holds."""
    if leading_zeros:
        m = re.match(r"^-?[0-9]+(\.[0-9]+)?([eE][-+]?[0-9]+)?$", text)
    else:
        m = NUMBER_RE.match(text)
    if not m:
        return None
    integer = "." not in text and "e" not in text and "E" not in text
    mantissa, _, exponent = text.lower().partition("e")
    whole, _, fraction = mantissa.lstrip("-").partition(".")
    digits = (whole + fraction).lstrip("0").rstrip("0")
    if integer:
        value = int(text)
        if abs(value) > MAX_INTEGER_MAGNITUDE:
            raise ValueError("integer_too_large")
        return value
    if digits:
        if len(digits) > MAX_SIGNIFICANT_DIGITS:
            raise ValueError("too_many_digits")
        stripped = (whole + fraction).lstrip("0")
        point = len(whole + fraction) - len(stripped) if stripped else 0
        leading = len(whole) - point - 1 + (int(exponent) if exponent else 0)
        if abs(leading) > MAX_DECIMAL_EXPONENT:
            raise ValueError("number_out_of_range")
    return float(text)


class _Reader:
    def __init__(self, text):
        self.text = text
        self.offset = 0
        self.line = 1
        self.line_start = 0
        self.nodes = 0
        self.cached = None

    # ---------------------------------------------------------------- basics
    def peek(self, distance=0):
        i = self.offset + distance
        return self.text[i] if i < len(self.text) else ""

    def here(self):
        return self.line, self.offset - self.line_start + 1

    def column(self):
        return self.offset - self.line_start

    def fail(self, rule, position=None):
        line, column = position or self.here()
        raise OamodYamlError(rule, line, column)

    def fail_here(self, rule="unexpected_character"):
        self.fail("unexpected_end" if self.offset >= len(self.text) else rule)

    def count(self, position):
        if self.nodes >= MAX_NODE_COUNT:
            self.fail("too_many_nodes", position)
        self.nodes += 1

    @staticmethod
    def break_or_end(c):
        return c in ("\n", "\r", "")

    def blank_or_end(self, c):
        return c in (" ", "\t") or self.break_or_end(c)

    @staticmethod
    def flow_indicator(c):
        return c != "" and c in ",[]{}"

    def consume_break(self):
        if self.peek() == "\r":
            self.offset += 1
        self.offset += 1
        self.line += 1
        self.line_start = self.offset

    def skip_inline_space(self):
        while self.peek() in (" ", "\t") and self.peek() != "":
            self.offset += 1

    def at_comment(self):
        if self.peek() != "#":
            return False
        if self.offset == self.line_start:
            return True
        return self.text[self.offset - 1] in (" ", "\t")

    def skip_to_break(self):
        while not self.break_or_end(self.peek()):
            self.offset += 1

    def end_line(self):
        self.skip_inline_space()
        if self.at_comment():
            self.skip_to_break()
        if self.offset >= len(self.text):
            return
        if self.peek() not in ("\n", "\r"):
            self.fail("unexpected_character")
        self.consume_break()

    def line_info(self):
        """(end, marker, indent) of the next line holding content."""
        if self.cached and self.cached[0] == self.offset:
            return self.cached[1]
        while True:
            first_tab = None
            content = self.offset
            while content < len(self.text) and self.text[content] in " \t":
                if self.text[content] == "\t" and first_tab is None:
                    first_tab = content
                content += 1
            if content >= len(self.text):
                self.offset = content
                info = (True, False, 0)
                break
            c = self.text[content]
            if c in "\n\r":
                self.offset = content
                self.consume_break()
                continue
            if c == "#":
                self.offset = content
                self.skip_to_break()
                continue
            if first_tab is not None:
                self.offset = first_tab
                self.fail("tab_indentation")
            self.offset = content
            indent = self.column()
            info = (False, indent == 0 and self.is_marker(), indent)
            break
        self.cached = (self.offset, info)
        return info

    def is_marker(self):
        c = self.peek()
        return c in ("-", ".") and self.peek(1) == c and self.peek(2) == c and self.blank_or_end(self.peek(3))

    def at_sequence_entry(self):
        return self.peek() == "-" and self.blank_or_end(self.peek(1))

    # ---------------------------------------------------------------- document
    def read(self, mapping_only):
        for i, c in enumerate(self.text):
            if (ord(c) < 0x20 and c not in "\t\n\r") or c == "\x7f" or (
                    c == "\r" and (i + 1 >= len(self.text) or self.text[i + 1] != "\n")):
                line = self.text.count("\n", 0, i) + 1
                self.fail("control_character", (line, i - (self.text.rfind("\n", 0, i) + 1) + 1))
        started = False
        while True:
            end, marker, indent = self.line_info()
            if end:
                if mapping_only:
                    self.fail("not_mapping")
                return None
            if not marker:
                if self.peek() == "%" and indent == 0 and not started:
                    self.fail("directive")
                break
            if self.peek() == "." or started:
                self.fail("several_documents")
            started = True
            self.offset += 3
            self.end_line()
        flow_root = self.peek() in ("[", "{")
        position = self.here()
        root = self.block_node(indent, 1, 0)
        if mapping_only and not isinstance(root, dict):
            self.fail("not_mapping", position)
        end, marker, _ = self.line_info()
        if end:
            return root
        if not marker:
            self.fail("trailing_content" if flow_root else "bad_indentation")
        if self.peek() == "-":
            self.fail("several_documents")
        self.offset += 3
        self.end_line()
        end, _, _ = self.line_info()
        if not end:
            self.fail("several_documents")
        return root

    # ---------------------------------------------------------------- scalars
    def check_value_start(self, flow):
        c = self.peek()
        rules = {"&": "anchor", "*": "alias", "!": "tag"}
        if c in rules:
            self.fail(rules[c])
        if c in ("|", ">"):
            # A value that starts with these is a block scalar, including
            # `engine: >= 0.8.0` written inside a flow mapping.
            self.fail("block_scalar")
        if c == "?":
            if self.blank_or_end(self.peek(1)) or (flow and self.flow_indicator(self.peek(1))):
                self.fail("complex_key")
            return
        if c != "" and c in "%@`,]}#":
            self.fail("unexpected_character")
        if c in ("-", ":") and c != "":
            if self.blank_or_end(self.peek(1)) or (flow and self.flow_indicator(self.peek(1))):
                self.fail_here()

    def plain_text(self, flow):
        first = self.offset
        while True:
            c = self.peek()
            if self.break_or_end(c):
                break
            if c == ":" and (self.blank_or_end(self.peek(1)) or (flow and self.flow_indicator(self.peek(1)))):
                break
            if c == "#" and self.at_comment():
                break
            if flow and self.flow_indicator(c):
                break
            self.offset += 1
        return self.text[first:self.offset].rstrip(" \t")

    def quoted(self):
        start = self.here()
        quote = self.peek()
        self.offset += 1
        out = []
        escaped = False
        while True:
            c = self.peek()
            if self.break_or_end(c):
                self.fail_here("multi_line_scalar")
            if c == quote:
                self.offset += 1
                if quote == "'" and self.peek() == "'":
                    out.append("'")
                    self.offset += 1
                else:
                    break
            elif c == "\\" and quote == '"':
                escape = self.here()
                self.offset += 1
                kind = self.peek()
                if self.break_or_end(kind):
                    self.fail_here("multi_line_scalar")
                self.offset += 1
                if kind in ESCAPES:
                    out.append(ESCAPES[kind])
                elif kind in HEX_ESCAPES:
                    digits = self.text[self.offset:self.offset + HEX_ESCAPES[kind]]
                    if len(digits) < HEX_ESCAPES[kind] and self.offset + len(digits) >= len(self.text):
                        self.offset += len(digits)
                        self.fail_here()
                    if not re.fullmatch(r"[0-9A-Fa-f]+", digits or "-") or len(digits) != HEX_ESCAPES[kind]:
                        self.fail("bad_escape", escape)
                    code = int(digits, 16)
                    if code > 0x10FFFF or 0xD800 <= code <= 0xDFFF:
                        self.fail("bad_escape", escape)
                    out.append(chr(code))
                    self.offset += HEX_ESCAPES[kind]
                    escaped = True
                else:
                    self.fail("bad_escape", escape)
            else:
                out.append(c)
                self.offset += 1
            if len("".join(out).encode("utf-8")) > MAX_STRING_BYTES:
                self.fail("string_too_long", start)
        text = "".join(out)
        return EscapedStr(text) if escaped else text

    def scalar(self, flow):
        """(quoted, value or raw text, position)"""
        position = self.here()
        if self.peek() in ('"', "'"):
            return True, self.quoted(), position
        self.check_value_start(flow)
        raw = self.plain_text(flow)
        if not raw:
            self.fail_here()
        if len(raw.encode("utf-8")) > MAX_STRING_BYTES:
            self.fail("string_too_long", position)
        return False, raw, position

    def resolve(self, raw, position):
        if raw in NULLS:
            return None
        if raw in TRUES:
            return True
        if raw in FALSES:
            return False
        try:
            value = number_value(raw)
        except ValueError as e:
            self.fail(e.args[0], position)
        return raw if value is None else value

    def key(self, quoted, raw, position):
        if quoted:
            return raw
        if raw == "<<":
            self.fail("merge_key", position)
        value = self.resolve(raw, position)
        if isinstance(value, str):
            return value
        if isinstance(value, int) and not isinstance(value, bool):
            return value
        self.fail("key_not_string", position)

    def add_key(self, mapping, key, position):
        if key in mapping:
            self.fail("duplicate_key", position)

    def open_collection(self, depth, position):
        if depth > MAX_NESTING_DEPTH:
            self.fail("too_deep", position)
        self.count(position)

    # ---------------------------------------------------------------- block
    def block_node(self, indent, depth, flow_floor):
        position = self.here()
        if self.at_sequence_entry():
            return self.block_sequence(indent, depth)
        if self.peek() in ("[", "{"):
            value = self.flow_collection(depth, flow_floor)
            self.skip_inline_space()
            if self.peek() == ":" and self.blank_or_end(self.peek(1)):
                self.fail("complex_key", position)
            self.end_line()
            return value
        quoted, raw, position = self.scalar(False)
        self.skip_inline_space()
        if self.peek() == ":" and self.blank_or_end(self.peek(1)):
            return self.block_mapping(indent, depth, self.key(quoted, raw, position), position)
        self.count(position)
        value = raw if quoted else self.resolve(raw, position)
        self.end_line()
        return value

    def block_key(self):
        if self.at_sequence_entry():
            self.fail("bad_indentation")
        if self.peek() in ("[", "{"):
            self.fail("complex_key")
        quoted, raw, position = self.scalar(False)
        self.skip_inline_space()
        if self.peek() != ":" or not self.blank_or_end(self.peek(1)):
            self.fail_here()
        return self.key(quoted, raw, position), position

    def block_mapping(self, indent, depth, key, key_position):
        self.open_collection(depth, key_position)
        out = {}
        while True:
            self.add_key(out, key, key_position)
            self.offset += 1
            out[key] = self.mapping_value(indent, depth)
            end, marker, line_indent = self.line_info()
            if end or marker or line_indent < indent:
                return out
            if line_indent > indent:
                self.fail("bad_indentation")
            key, key_position = self.block_key()

    def mapping_value(self, indent, depth):
        self.skip_inline_space()
        if self.break_or_end(self.peek()) or self.at_comment():
            position = self.here()
            self.end_line()
            end, marker, line_indent = self.line_info()
            if not end and not marker:
                if line_indent > indent:
                    return self.block_node(line_indent, depth + 1, indent + 1)
                if line_indent == indent and self.at_sequence_entry():
                    return self.block_sequence(indent, depth + 1)
            self.count(position)
            return None
        return self.inline_value(depth + 1, indent + 1)

    def inline_value(self, depth, flow_floor):
        position = self.here()
        if self.at_sequence_entry():
            self.fail("unexpected_character")
        if self.peek() in ("[", "{"):
            value = self.flow_collection(depth, flow_floor)
            self.skip_inline_space()
            if self.peek() == ":" and self.blank_or_end(self.peek(1)):
                self.fail("complex_key", position)
            self.end_line()
            return value
        quoted, raw, position = self.scalar(False)
        self.skip_inline_space()
        if self.peek() == ":" and self.blank_or_end(self.peek(1)):
            self.fail("unexpected_character")
        self.count(position)
        value = raw if quoted else self.resolve(raw, position)
        self.end_line()
        return value

    def block_sequence(self, indent, depth):
        self.open_collection(depth, self.here())
        out = []
        while True:
            self.offset += 1
            self.skip_inline_space()
            position = self.here()
            if self.break_or_end(self.peek()) or self.at_comment():
                self.end_line()
                end, marker, line_indent = self.line_info()
                if not end and not marker and line_indent > indent:
                    out.append(self.block_node(line_indent, depth + 1, indent + 1))
                else:
                    self.count(position)
                    out.append(None)
            else:
                out.append(self.block_node(self.column(), depth + 1, indent + 1))
            end, marker, line_indent = self.line_info()
            if end or marker or line_indent < indent:
                return out
            if line_indent > indent:
                self.fail("bad_indentation")
            if not self.at_sequence_entry():
                return out

    # ---------------------------------------------------------------- flow
    def skip_flow_space(self, flow_floor):
        while True:
            self.skip_inline_space()
            if self.at_comment():
                self.skip_to_break()
            if self.peek() not in ("\n", "\r") or self.peek() == "":
                return
            self.consume_break()
            while self.peek() == " ":
                self.offset += 1
            if self.peek() == "\t":
                self.fail("tab_indentation")
            c = self.peek()
            if not self.break_or_end(c) and c not in "#]}" and self.column() < flow_floor:
                self.fail("bad_indentation")

    def flow_node(self, depth, flow_floor):
        if self.peek() in ("[", "{"):
            return self.flow_collection(depth, flow_floor)
        quoted, raw, position = self.scalar(True)
        self.count(position)
        return raw if quoted else self.resolve(raw, position)

    def flow_collection(self, depth, flow_floor):
        position = self.here()
        mapping = self.peek() == "{"
        closing = "}" if mapping else "]"
        self.open_collection(depth, position)
        self.offset += 1
        out = {} if mapping else []
        while True:
            self.skip_flow_space(flow_floor)
            if self.peek() == closing:
                self.offset += 1
                return out
            if mapping:
                key, key_position, value = self.flow_entry(depth, flow_floor)
                self.add_key(out, key, key_position)
                out[key] = value
            else:
                item_position = self.here()
                out.append(self.flow_node(depth + 1, flow_floor))
                self.skip_flow_space(flow_floor)
                if self.peek() == ":":
                    self.fail("complex_key", item_position)
            self.skip_flow_space(flow_floor)
            if self.peek() == ",":
                self.offset += 1
                continue
            if self.peek() != closing:
                self.fail_here()

    def flow_entry(self, depth, flow_floor):
        if self.peek() in ("[", "{"):
            self.fail("complex_key")
        quoted, raw, position = self.scalar(True)
        self.skip_inline_space()
        has_value = self.peek() == ":" and (quoted or self.blank_or_end(self.peek(1))
                                             or self.flow_indicator(self.peek(1)))
        key = self.key(quoted, raw, position)
        if has_value:
            self.offset += 1
            self.skip_flow_space(flow_floor)
        value_position = self.here()
        if not has_value or self.peek() in (",", "}"):
            if not has_value and self.peek() not in (",", "}") and not self.break_or_end(self.peek()) \
                    and not self.at_comment():
                self.fail_here()
            self.count(value_position)
            return key, position, None
        return key, position, self.flow_node(depth + 1, flow_floor)


def _read(data, mapping_only):
    if isinstance(data, str):
        data = data.encode("utf-8")
    if len(data) > MAX_INPUT_BYTES:
        raise OamodYamlError("too_large", 0, 0)
    if data.startswith(b"\xef\xbb\xbf"):
        raise OamodYamlError("byte_order_mark", 1, 1)
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as e:
        line = data.count(b"\n", 0, e.start) + 1
        raise OamodYamlError("invalid_utf8", line, e.start - (data.rfind(b"\n", 0, e.start) + 1) + 1)
    return _Reader(text).read(mapping_only)


def loads(data):
    """Read a document whose top level is a mapping."""
    return _read(data, True)


def loads_value(data):
    """Read one value of any kind; empty text is None."""
    return _read(data, False)


def load_file(path):
    with open(path, "rb") as f:
        return loads(f.read())


# ---------------------------------------------------------------- package keys
# Homepage, tags and requires.engine, shared by every manifest. The same
# rules as src/formats/oamod/src/package_keys.cpp, checked against
# src/formats/oamod/tests/package_keys_cases.txt.

ENGINE_RANGE_MAX_BYTES = 64
ENGINE_RANGE_MAX_TERMS = 4
HOMEPAGE_MAX_BYTES = 256
TAG_MAX_BYTES = 32
TAGS_MAX_COUNT = 8
VERSION_PART_MAX = 65535
_HTTPS_SCHEME = "https://"
_HTTP_SCHEME = "http://"
_OPERATORS = (
    (">=", "at_least"),
    ("<=", "at_most"),
    (">", "above"),
    ("<", "below"),
    ("=", "exactly"),
)


def _byte_of(index):
    """The 1-based byte a 0-based index names."""
    return str(index + 1)


def _starts_with_scheme(text, scheme):
    """True when text begins with scheme, ignoring the case of letters."""
    if len(text) < len(scheme):
        return False
    for index, expected in enumerate(scheme):
        byte = text[index]
        if "A" <= byte <= "Z":
            byte = chr(ord(byte) - ord("A") + ord("a"))
        if byte != expected:
            return False
    return True


def _read_version_part(text, index):
    """One version part, and the index after it."""
    if index >= len(text) or not text[index].isdigit():
        raise ValueError(f"expected a version part at byte {_byte_of(index)}")
    if text[index] == "0" and index + 1 < len(text) and text[index + 1].isdigit():
        raise ValueError(f"a version part has a leading zero at byte {_byte_of(index)}")
    value = 0
    start = index
    while index < len(text) and text[index].isdigit():
        value = value * 10 + int(text[index])
        index += 1
        if value > VERSION_PART_MAX:
            raise ValueError(f"a version part is above 65535 at byte {_byte_of(start)}")
    return value, index


def _read_version_at(text, index):
    """MAJOR.MINOR.PATCH starting at index, and the index after it."""
    major, index = _read_version_part(text, index)
    if index >= len(text) or text[index] != ".":
        raise ValueError(f"expected '.' in a version at byte {_byte_of(index)}")
    index += 1
    minor, index = _read_version_part(text, index)
    if index >= len(text) or text[index] != ".":
        raise ValueError(f"expected '.' in a version at byte {_byte_of(index)}")
    index += 1
    patch, index = _read_version_part(text, index)
    if index < len(text) and text[index] == ".":
        raise ValueError(f"a version has three parts at byte {_byte_of(index)}")
    return (major, minor, patch), index


def _read_operator(text, index):
    """The operator at index and how many bytes it occupies, or (None, 0)."""
    rest = text[index:]
    for spelling, name in _OPERATORS:
        if rest.startswith(spelling):
            return name, spelling, len(spelling)
    return None, "", 0


def _skip_spaces(text, index):
    """The index after a run of ASCII spaces."""
    while index < len(text) and text[index] == " ":
        index += 1
    return index


def parse_engine_version(text):
    """MAJOR.MINOR.PATCH as (major, minor, patch), or None.

    Each part is a decimal from 0 to 65535 with no leading zero. The whole
    text must be the version."""
    if not isinstance(text, str):
        return None
    try:
        version, index = _read_version_at(text, 0)
    except ValueError:
        return None
    if index != len(text):
        return None
    return version


def parse_engine_range(text):
    """The comparisons of an engine requirement, as (operator, version) pairs.

    One to four comparisons separated by commas that spaces may surround.
    Each is an operator (>=, >, <=, < or =), optional spaces, then a version.
    Nothing else may appear, and the text is at most 64 bytes. Raises
    ValueError with what is wrong and the 1-based byte where."""
    if not isinstance(text, str):
        raise ValueError("requires.engine must be a string")
    if len(text.encode("utf-8")) > ENGINE_RANGE_MAX_BYTES:
        raise ValueError("an engine requirement is longer than 64 bytes")
    if text == "":
        raise ValueError("an engine requirement is empty")
    terms = []
    index = 0
    while True:
        if len(terms) == ENGINE_RANGE_MAX_TERMS:
            raise ValueError(f"at most 4 comparisons at byte {_byte_of(index)}")
        comparison, spelling, length = _read_operator(text, index)
        if comparison is None:
            raise ValueError(f"expected a comparison at byte {_byte_of(index)}")
        index += length
        index = _skip_spaces(text, index)
        version_at = index
        version_started = version_at < len(text) and text[version_at].isdigit()
        try:
            version, index = _read_version_at(text, index)
        except ValueError as error:
            if not version_started:
                raise ValueError(
                    f"expected a version after '{spelling}' at byte {_byte_of(version_at)}"
                ) from error
            raise
        terms.append((comparison, version))
        if index == len(text):
            return terms
        after = index
        index = _skip_spaces(text, index)
        if index >= len(text) or text[index] != ",":
            raise ValueError(f"unexpected text at byte {_byte_of(after)}")
        index += 1
        index = _skip_spaces(text, index)
        if index >= len(text):
            raise ValueError(f"expected a comparison at byte {_byte_of(index)}")


def engine_range_met(engine_range, version):
    """True when every comparison holds for version, compared as three numbers."""
    for comparison, required in engine_range:
        if comparison == "at_least" and not version >= required:
            return False
        if comparison == "above" and not version > required:
            return False
        if comparison == "at_most" and not version <= required:
            return False
        if comparison == "below" and not version < required:
            return False
        if comparison == "exactly" and version != required:
            return False
    return True


def homepage_valid(text):
    """True for an http or https address of 1 to 256 bytes that can be opened.

    The scheme's letters may be either case. At least one byte follows it,
    and the address holds no ASCII control character, delete or space."""
    if not isinstance(text, str) or text == "":
        return False
    if len(text.encode("utf-8")) > HOMEPAGE_MAX_BYTES:
        return False
    if _starts_with_scheme(text, _HTTPS_SCHEME):
        body = text[len(_HTTPS_SCHEME):]
    elif _starts_with_scheme(text, _HTTP_SCHEME):
        body = text[len(_HTTP_SCHEME):]
    else:
        return False
    if body == "":
        return False
    for byte in body:
        value = ord(byte)
        if value <= ord(" ") or value == 0x7F:
            return False
    return True


def tag_valid(text):
    """True for 1 to 32 bytes of lower-case kebab-case."""
    if not isinstance(text, str) or text == "":
        return False
    if len(text.encode("utf-8")) > TAG_MAX_BYTES or text[0] == "-" or text[-1] == "-":
        return False
    previous = ""
    for character in text:
        word = ("a" <= character <= "z") or ("0" <= character <= "9")
        if not word and character != "-":
            return False
        if character == "-" and previous == "-":
            return False
        previous = character
    return True


def _tag_problems(value):
    """The problems of a tags value, each naming its path."""
    if not isinstance(value, list):
        return ["tags: tags must be a list of 1 to 8 strings"]
    if len(value) == 0:
        return ["tags: tags must list 1 to 8 tags; leave the key out instead"]
    if len(value) > TAGS_MAX_COUNT:
        return [f"tags: tags lists {len(value)} tags; at most 8"]
    problems = []
    seen = []
    for index, item in enumerate(value):
        path = f"tags[{index}]"
        if not isinstance(item, str) or not tag_valid(item):
            problems.append(f"{path}: {path} must be 1 to 32 bytes of lower-case kebab-case")
            continue
        if item in seen:
            problems.append(f"{path}: {path} repeats {item}")
            continue
        seen.append(item)
    return problems


def package_key_problems(mapping, requires):
    """The broken homepage, tags and requires.engine rules, each as one line.

    mapping is the manifest. requires is its requires mapping, or None when
    the manifest has none. A key that is absent is not a problem. An empty
    tag list is."""
    problems = []
    if isinstance(mapping, dict) and "homepage" in mapping:
        value = mapping["homepage"]
        if not isinstance(value, str):
            problems.append("homepage: homepage must be a string")
        elif not homepage_valid(value):
            problems.append(
                "homepage: homepage must be an http or https address of 1 to 256 bytes"
            )
    if isinstance(mapping, dict) and "tags" in mapping:
        problems.extend(_tag_problems(mapping["tags"]))
    if isinstance(requires, dict) and "engine" in requires:
        value = requires["engine"]
        if not isinstance(value, str):
            problems.append("requires.engine: requires.engine must be a string")
        else:
            try:
                parse_engine_range(value)
            except ValueError as error:
                problems.append(f"requires.engine: {error}")
    return problems


def _case_versions(text):
    """The versions in one side of an ok range, or a failure when one is not."""
    if text == "":
        return []
    versions = []
    for item in text.split(","):
        version = parse_engine_version(item)
        if version is None:
            raise ValueError(f"case version {item!r} does not parse")
        versions.append(version)
    return versions


def _check_case(line):
    """Raises ValueError when one case line does not hold."""
    parts = line.split(" ", 2)
    if len(parts) < 2 or parts[0] not in ("range", "version", "homepage", "tag"):
        raise ValueError(f"not a case: {line!r}")
    kind, status = parts[0], parts[1]
    text = parts[2] if len(parts) == 3 else ""
    if status not in ("ok", "bad"):
        raise ValueError(f"not ok or bad: {line!r}")
    if kind == "range" and status == "ok":
        fields = text.split(" :: ")
        if len(fields) != 3:
            raise ValueError(f"an ok range needs meet and miss: {line!r}")
        parsed = parse_engine_range(fields[0])
        for version in _case_versions(fields[1]):
            if not engine_range_met(parsed, version):
                raise ValueError(f"{fields[0]!r} should meet {version}")
        for version in _case_versions(fields[2]):
            if engine_range_met(parsed, version):
                raise ValueError(f"{fields[0]!r} should not meet {version}")
        return
    if kind == "range":
        try:
            parse_engine_range(text)
        except ValueError:
            return
        raise ValueError(f"range should be refused: {text!r}")
    if kind == "version":
        parsed = parse_engine_version(text)
        if (parsed is None) != (status == "bad"):
            raise ValueError(f"version {status} failed for {text!r}")
        return
    valid = homepage_valid(text) if kind == "homepage" else tag_valid(text)
    if valid != (status == "ok"):
        raise ValueError(f"{kind} {status} failed for {text!r}")


def package_keys_self_test(cases_path=None):
    """Checks every line of the shared case table. Returns 0, or raises."""
    path = cases_path or (
        Path(__file__).resolve().parents[1] / "src" / "formats" / "oamod" / "tests"
        / "package_keys_cases.txt"
    )
    count = 0
    for line in path.read_text(encoding="utf-8").splitlines():
        if line == "" or line.startswith("#"):
            continue
        _check_case(line)
        count += 1
    if count < 40:
        raise ValueError(f"the case table has {count} lines, and needs at least 40")
    listed = package_key_problems(
        {"homepage": "javascript:alert(1)", "tags": ["Balance", "balance", "balance"]},
        {"engine": ">= 1.2"},
    )
    if [item.split(":", 1)[0] for item in listed] != [
        "homepage", "tags[0]", "tags[2]", "requires.engine"
    ]:
        raise ValueError(f"package_key_problems returned {listed}")
    if package_key_problems({"tags": []}, None) == []:
        raise ValueError("an empty tag list was accepted")
    if package_key_problems({}, {"engine": ">= 0.0.1"}) != []:
        raise ValueError("a met-shaped requirement was refused")
    return count


def main(argv):
    """Runs the shared-key self-test."""
    import argparse
    parser = argparse.ArgumentParser(description="The strict YAML reader, and its package-key self-test.")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if not args.self_test:
        parser.error("pass --self-test")
    count = package_keys_self_test()
    print(f"oamod_yaml self-test: {count} package-key cases passed")
    return 0


if __name__ == "__main__":
    import sys
    from pathlib import Path
    sys.exit(main(sys.argv[1:]))
