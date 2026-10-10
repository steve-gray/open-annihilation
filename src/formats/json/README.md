# Strict JSON

The engine's one strict JSON reader and writer. Target `oa-formats-json`,
header `oa/formats/json.hpp`, namespace `oa::formats::json`. The automation
protocol was its first caller; any layer that reads or writes JSON links
this library and no other parser.

`parse_json` reads one value. `Json` is that value: its kind, and a boolean,
a whole number, a string, a number's text as written, an array's elements
or an object's members. `find` returns an object's member of a name.
`JsonWriter` writes a compact text, value by value. `append_json_string`
writes one string. `unicode_text` is how a string that is not UTF-8 is
carried, `utf8_valid` tells whether a text is UTF-8, and `base64_text`
encodes bytes as standard base64.

## Entry points

- `parse_json` reads one JSON value and, on a refusal, sets a `JsonError`
  with a message and the byte where it was found.
- `Json::type`, `boolean`, `integer`, `string`, `number_text`, `elements`,
  `names`, `values` and `find` read a parsed value. `integer` is a whole
  number within 64 bits; a fraction, an exponent or a wider number has no
  integer, and its text is `number_text`.
- `JsonWriter` writes objects, arrays, keys, strings, whole numbers,
  booleans, null and a value given as JSON text. `text` is what it has
  written.
- `unicode_text` returns a text unchanged when it is UTF-8, and otherwise
  `non_unicode_text_marker` followed by the base64 of its bytes.
  `append_json_string` quotes a text and escapes `"`, `\` and the control
  characters, writing a text that is not UTF-8 through `unicode_text`.

## State

None. Each call reads its arguments and returns a value. A `Json` tree and
a `JsonWriter` hold only what that call built.

## Invariants

- RFC 8259, strictly. One value, no comment, no trailing comma, no leading
  zero, no byte-order mark, and a refusal names where it was found.
- UTF-8 only. A text that is not UTF-8 is refused by the reader. The writer
  always writes UTF-8: a string that is not UTF-8 is written, whole, as
  `Non-Unicode Text Error::` (`non_unicode_text_marker`) followed by the
  standard base64 of its bytes (RFC 4648, padded), from which a caller
  recovers them.
- Nesting of arrays and objects is at most 64 (`max_json_depth`).
- A number is kept as written. Nothing turns it into a binary floating
  point value.

## Tests

`formats-json` checks the values the reader accepts, every malformed text
it refuses, numbers kept as written, nesting to the depth limit and one
past it, compact writing read back, text that is not UTF-8 written as the
marker and its base64, the base64 test vectors, UTF-8 well-formed and not,
two members of one name, and a string of 1 MiB.

## Limitations

No size limit of its own: callers bound the text. A text of 4 GiB or longer
is refused, because the reader indexes it with a 32-bit count. Duplicate
member names are kept, and `find` returns the first.
