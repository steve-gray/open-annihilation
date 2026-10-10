// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The engine's strict JSON reader and writer: RFC 8259's values and the
// texts it refuses, numbers kept as written, nesting to the depth limit,
// compact writing, text that is not UTF-8 written as the marker and its
// base64, and the base64 and UTF-8 checks. The expectations are those the
// automation protocol's codec check stated before the reader moved here.

#include "oa/formats/json.hpp"
#include "oa/test/check.hpp"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace {

namespace json = oa::formats::json;

/// Returns a string member of a parsed object.
///
/// @param object the object
/// @param name the member's name
/// @return the member's text; empty when it has none
std::string member_text(const json::Json& object, std::string_view name) {
    const auto* member = object.find(name);
    const auto* text = member != nullptr ? member->string() : nullptr;
    return text != nullptr ? *text : std::string();
}

/// Parses a JSON text.
///
/// @param text the text
/// @return the value, or nothing when it is refused
std::optional<json::Json> parse(std::string_view text) {
    json::JsonError error;
    return json::parse_json(text, error);
}

/// The JSON reader takes RFC 8259's values and refuses malformed texts, and
/// the writer writes them compactly.
void check_json() {
    const auto request = parse(
        R"( {"id": -42, "op": "prefs", "names": ["a", "b"], "on": true, "off": false,)"
        R"( "none": null, "ratio": 1.5e3, "nested": {"x": [[], {}]}} )"
    );
    OA_CHECK(request.has_value() && request->type() == json::JsonType::object);
    if (request) {
        OA_CHECK(request->find("id")->integer() == -42);
        OA_CHECK(member_text(*request, "op") == "prefs");
        OA_CHECK(request->find("names")->elements().size() == 2);
        OA_CHECK(request->find("on")->boolean() == true);
        OA_CHECK(request->find("off")->boolean() == false);
        OA_CHECK(request->find("none")->type() == json::JsonType::null);
        OA_CHECK(!request->find("ratio")->integer().has_value());
        OA_CHECK(*request->find("ratio")->number_text() == "1.5e3");
        OA_CHECK(request->find("missing") == nullptr);
        OA_CHECK(!request->find("op")->integer().has_value());
    }
    OA_CHECK(parse("9223372036854775807")->integer() == INT64_MAX);
    OA_CHECK(!parse("9223372036854775808")->integer().has_value());
    OA_CHECK(parse("[]")->type() == json::JsonType::array);
    OA_CHECK(*parse(R"("é😀\/")")->string() == "\xC3\xA9\xF0\x9F\x98\x80/");
    // Half a surrogate pair stands for U+FFFD. The escapes are spelled with
    // a doubled backslash, not in raw strings, which some compilers read
    // as invalid characters of the source.
    OA_CHECK(*parse("\"\\ud800x\"")->string() == "\xEF\xBF\xBDx");
    OA_CHECK(*parse("\"\\udc00\"")->string() == "\xEF\xBF\xBD");
    // Nested 64 deep is taken; 65 is refused.
    OA_CHECK(parse(std::string(64, '[') + std::string(64, ']')).has_value());
    OA_CHECK(!parse(std::string(65, '[') + std::string(65, ']')).has_value());
    for (const std::string_view malformed : {
             "",
             "{",
             "}",
             R"({"a":})",
             R"({"a" 1})",
             R"({"a":1,})",
             R"({a:1})",
             "[1,]",
             "[1 2]",
             "01",
             "1.",
             "-",
             "1e",
             "+1",
             "tru",
             "nul",
             "{} {}",
             "{}x",
             "\"open",
             "\"tab\there\"",
             R"("\x")",
             R"("\u12")",
             R"("\u12G4")",
             "\"\xC3\"",
             "\"\xC0\xAF\"",
             "\"\xED\xA0\x80\"",
             "\"\xF4\x90\x80\x80\"",
             "\xEF\xBB\xBF{}",
         }) {
        json::JsonError error;
        const bool refused = !json::parse_json(malformed, error).has_value();
        if (!refused)
            std::fprintf(
                stderr, "taken: %.*s\n", static_cast<int>(malformed.size()), malformed.data()
            );
        OA_CHECK(refused && !error.message.empty());
    }
    json::JsonError error;
    OA_CHECK(!json::parse_json(R"({"a":1} x)", error).has_value());
    OA_CHECK(error.offset == 8);

    json::JsonWriter writer;
    writer.begin_object();
    writer.key("id");
    writer.integer(9);
    writer.key("ok");
    writer.boolean(true);
    writer.key("list");
    writer.begin_array();
    writer.string("a\"b\\c\n\x01");
    writer.null();
    writer.begin_object();
    writer.end_object();
    writer.end_array();
    writer.key("raw");
    writer.raw(R"({"x":1})");
    writer.key("text");
    writer.string(
        "Ren\xC3\xA9"
        "e"
    );
    writer.end_object();
    OA_CHECK(
        writer.text() == "{\"id\":9,\"ok\":true,\"list\":[\"a\\\"b\\\\c\\n\\u0001\",null,{}],"
                         "\"raw\":{\"x\":1},\"text\":\"Ren\xC3\xA9"
                         "e\"}"
    );
    const auto again = parse(writer.text());
    OA_CHECK(again.has_value());
    OA_CHECK(again && *again->find("list")->elements()[0].string() == "a\"b\\c\n\x01");

    // Text that is UTF-8 is written as it is; text that is not, such as the
    // game's own text in its code page, whole as the marker and the base64
    // of its bytes, so that what is written is always UTF-8.
    const std::string accented = "Ar\xC3\xA8ne \xE4\xB8\xAD";
    OA_CHECK(json::unicode_text(accented) == accented);
    OA_CHECK(json::unicode_text("") == "");
    OA_CHECK(json::unicode_text("D\xE9marrer") == "Non-Unicode Text Error::ROltYXJyZXI=");
    json::JsonWriter marked;
    marked.begin_object();
    marked.key("caf\xE9");
    marked.string("D\xE9marrer \"1\"");
    marked.key("name");
    marked.string(accented + " \"1\"");
    marked.end_object();
    OA_CHECK(
        marked.text() == "{\"Non-Unicode Text Error::Y2Fm6Q==\":"
                         "\"Non-Unicode Text Error::ROltYXJyZXIgIjEi\","
                         "\"name\":\"Ar\xC3\xA8ne \xE4\xB8\xAD \\\"1\\\"\"}"
    );
    const auto read_back = parse(marked.text());
    OA_CHECK(
        read_back &&
        member_text(*read_back, "Non-Unicode Text Error::Y2Fm6Q==") ==
            "Non-Unicode Text Error::ROltYXJyZXIgIjEi" &&
        member_text(*read_back, "name") == accented + " \"1\""
    );
    // The test vectors of RFC 4648, section 10, and every byte value.
    const auto base64 = [](std::string_view text) {
        return json::base64_text({reinterpret_cast<const uint8_t*>(text.data()), text.size()});
    };
    OA_CHECK(base64("") == "" && base64("f") == "Zg==" && base64("fo") == "Zm8=");
    OA_CHECK(base64("foo") == "Zm9v" && base64("foob") == "Zm9vYg==");
    OA_CHECK(base64("fooba") == "Zm9vYmE=" && base64("foobar") == "Zm9vYmFy");
    OA_CHECK(base64(std::string_view("\x00\xFF\xFE\xFB\xEF\xBE", 6)) == "AP/+++++");
    // Two CJK characters, three bytes each.
    OA_CHECK(json::utf8_valid("\344\270\255\346\226\207"));
    // Malformed: a stray continuation byte, a sequence cut short, an
    // overlong form, a surrogate and a code point above U+10FFFF.
    for (const std::string_view bad :
         {"\x80", "\xe4\xb8", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xff"}) {
        OA_CHECK(!json::utf8_valid(bad));
        OA_CHECK(json::unicode_text(bad) == "Non-Unicode Text Error::" + base64(bad));
    }

    // Two members of one name are both kept; find returns the first.
    const auto duplicated = parse(R"({"a":1,"a":2})");
    OA_CHECK(duplicated.has_value() && duplicated->names().size() == 2);
    if (duplicated) {
        OA_CHECK(duplicated->names()[0] == "a" && duplicated->names()[1] == "a");
        OA_CHECK(duplicated->find("a")->integer() == 1);
        OA_CHECK(duplicated->values().size() == 2);
        OA_CHECK(duplicated->values()[0].integer() == 1);
        OA_CHECK(duplicated->values()[1].integer() == 2);
    }
    // A string of 1 MiB is read. The reader sets no smaller bound.
    const std::string megabyte(size_t{1} << 20, 'm');
    const auto wide = parse("\"" + megabyte + "\"");
    OA_CHECK(wide.has_value() && wide->string() != nullptr && *wide->string() == megabyte);
}

} // namespace

int main() {
    check_json();
    return oa::test::check_exit_status();
}
