// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The origin record of an installed folder: its text, the reading of one,
// and the replacement of one in a folder that already holds a package.

#include "files.hpp"

#include "oa/app/package_install/origin.hpp"
#include "oa/app/package_install.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/formats/oamod.hpp"
#include "oa/platform/files.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app::package_install {

namespace fs = std::filesystem;
namespace oamod = oa::formats::oamod;

namespace {

/// The comment the record starts with.
constexpr std::string_view origin_comment =
    "# Written by Open Annihilation when it installed this folder; not part of the package.";

/// The keys, in the only order a record writes them. A file origin leaves
/// out registry, catalogue-id and release.
constexpr std::array<std::string_view, 7> origin_keys{
    "oa-origin",
    "origin",
    "registry",
    "catalogue-id",
    "release",
    "sha256",
    "installed",
};

/// Tells whether a value can be written as a plain YAML scalar.
///
/// A date, a registry id and a catalogue key of letters, digits and hyphens
/// are plain. A number, a boolean or null, and anything else, is quoted, so
/// that a hash of digits alone is still read back as text.
///
/// @param text the value
/// @return true when the record can write it without quotes
bool plain_scalar(std::string_view text) {
    if (text.empty())
        return false;
    for (const char letter : text) {
        const bool word = (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') ||
                          (letter >= '0' && letter <= '9') || letter == '-';
        if (!word)
            return false;
    }
    if (text == "true" || text == "True" || text == "TRUE" || text == "false" || text == "False" ||
        text == "FALSE" || text == "null" || text == "Null" || text == "NULL" || text == "~")
        return false;
    std::size_t index = 0;
    if (text[index] == '-') {
        ++index;
        if (index == text.size())
            return false;
    }
    if (text[index] == '0')
        return index + 1 != text.size();
    if (text[index] >= '1' && text[index] <= '9') {
        bool digits = true;
        for (std::size_t at = index; at < text.size(); ++at) {
            if (text[at] < '0' || text[at] > '9')
                digits = false;
        }
        if (digits)
            return false;
    }
    return true;
}

/// Writes a value, quoted when it would not be read back as that text.
///
/// @param text the value
/// @return its spelling in the record
std::string scalar_text(std::string_view text) {
    if (plain_scalar(text))
        return std::string(text);
    std::string quoted = "\"";
    for (const char letter : text) {
        if (letter == '\\' || letter == '"')
            quoted.push_back('\\');
        quoted.push_back(letter);
    }
    quoted.push_back('"');
    return quoted;
}

/// Writes a digest as lower-case hex.
///
/// @param digest the digest
/// @return 64 hex digits
std::string hex_text(const oa::base::sha256::Digest& digest) {
    const auto hex = oa::base::sha256::to_hex(digest);
    return std::string(hex.begin(), hex.end());
}

/// Tells whether a digest's hex contains a letter, so it reads as text unquoted.
///
/// @param hex 64 hex digits
/// @return true when it contains a to f
bool hex_has_letter(std::string_view hex) {
    for (const char letter : hex) {
        if (letter >= 'a' && letter <= 'f')
            return true;
    }
    return false;
}

/// Appends one key and its value, then a newline.
///
/// @param[in,out] text the record
/// @param key the key
/// @param value the value, already spelled
void append_line(std::string& text, std::string_view key, std::string_view value) {
    text.append(key);
    text.append(": ");
    text.append(value);
    text.push_back('\n');
}

/// Sets a refusal and returns nothing.
///
/// @param problem where the reason goes; may be null
/// @param reason why
/// @return nothing
std::optional<Origin> refused(std::string* problem, std::string reason) {
    if (problem != nullptr)
        *problem = std::move(reason);
    return std::nullopt;
}

/// Tells whether a text is a real UTC day, YYYY-MM-DD.
///
/// @param text the text
/// @return true when it is
bool real_utc_date(std::string_view text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-')
        return false;
    const auto digits = [](std::string_view part) {
        int value = 0;
        for (const char letter : part) {
            if (letter < '0' || letter > '9')
                return -1;
            value = value * 10 + (letter - '0');
        }
        return value;
    };
    const int year = digits(text.substr(0, 4));
    const int month = digits(text.substr(5, 2));
    const int day = digits(text.substr(8, 2));
    if (year < 0 || month < 0 || day < 0)
        return false;
    const std::chrono::year_month_day date{
        std::chrono::year{year},
        std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)},
    };
    return date.ok();
}

/// The index of a key in origin_keys, or -1.
///
/// @param key the key
/// @return its index
int key_index(std::string_view key) {
    for (std::size_t index = 0; index < origin_keys.size(); ++index) {
        if (origin_keys[index] == key)
            return static_cast<int>(index);
    }
    return -1;
}

/// Reads a string value.
///
/// @param entry the mapping entry
/// @param[out] text the value
/// @return true when the entry is a string
bool string_value(const oamod::Node& entry, std::string& text) {
    if (entry.kind != oamod::NodeKind::string)
        return false;
    text = entry.text;
    return true;
}

} // namespace

std::string origin_text(const Origin& origin) {
    if (!origin.sha256 || origin.installed.empty())
        return {};
    const std::string hash = hex_text(*origin.sha256);
    // A hash of digits alone is a number to the reader, and one this long
    // is refused; quotes keep it text. A run of zeros is not a number, and
    // is quoted the same way. A hash with a letter is text already.
    const std::string hash_text = hex_has_letter(hash) ? hash : "\"" + hash + "\"";
    std::string text;
    text.reserve(256);
    text.append(origin_comment);
    text.push_back('\n');
    append_line(text, "oa-origin", "1");
    append_line(text, "origin", origin.kind == OriginKind::catalogue ? "catalogue" : "file");
    if (origin.kind == OriginKind::catalogue) {
        append_line(text, "registry", scalar_text(origin.registry));
        append_line(text, "catalogue-id", scalar_text(origin.catalogue_id));
        append_line(text, "release", std::to_string(origin.release));
    }
    append_line(text, "sha256", hash_text);
    append_line(text, "installed", scalar_text(origin.installed));
    return text;
}

std::optional<Origin> parse_origin(std::span<const uint8_t> bytes, std::string* problem) {
    if (bytes.size() > origin_most_bytes)
        return refused(problem, "the origin record is longer than 4 KiB");
    for (const uint8_t byte : bytes) {
        if (byte == '\r')
            return refused(problem, "the origin record is not LF");
    }
    oamod::Node root;
    oamod::ReadError read_error{};
    if (!oamod::read_document(bytes, root, read_error))
        return refused(problem, oamod::rule_message(read_error.rule));
    Origin origin{};
    bool seen[origin_keys.size()] = {};
    int previous = -1;
    for (const oamod::Node& entry : root.children) {
        if (entry.key.kind != oamod::KeyKind::string)
            return refused(problem, "the origin record has a key it does not use");
        const int index = key_index(entry.key.text);
        if (index < 0)
            return refused(problem, "the origin record has a key it does not use");
        if (index <= previous)
            return refused(problem, "the origin record's keys are out of order");
        previous = index;
        seen[static_cast<std::size_t>(index)] = true;
        switch (index) {
        case 0: {
            int64_t version = 0;
            if (entry.kind != oamod::NodeKind::number || !entry.number.integer ||
                !oamod::integer_value(entry.number, version) || version != 1)
                return refused(problem, "the origin record's oa-origin is not 1");
            break;
        }
        case 1: {
            std::string kind;
            if (!string_value(entry, kind) || (kind != "catalogue" && kind != "file"))
                return refused(problem, "the origin record's origin is not catalogue or file");
            origin.kind = kind == "catalogue" ? OriginKind::catalogue : OriginKind::file;
            break;
        }
        case 2:
            if (!string_value(entry, origin.registry) ||
                !oa::data::registry::valid_registry_id(origin.registry))
                return refused(problem, "the origin record's registry is not a registry id");
            break;
        case 3:
            if (!string_value(entry, origin.catalogue_id) ||
                origin.catalogue_id.find('@') != std::string::npos)
                return refused(problem, "the origin record's catalogue-id contains @");
            break;
        case 4: {
            int64_t release = 0;
            if (entry.kind != oamod::NodeKind::number || !entry.number.integer ||
                !oamod::integer_value(entry.number, release) || release < 1 || release > 2147483647)
                return refused(problem, "the origin record's release is not 1 to 2147483647");
            origin.release = release;
            break;
        }
        case 5: {
            std::string hash;
            if (!string_value(entry, hash) || hash.size() != oa::base::sha256::hex_size)
                return refused(problem, "the origin record's sha256 is not 64 hex digits");
            for (const char letter : hash) {
                if (letter >= 'A' && letter <= 'F')
                    return refused(problem, "the origin record's sha256 is not lower case");
            }
            const auto digest = oa::base::sha256::parse_hex(hash);
            if (!digest)
                return refused(problem, "the origin record's sha256 is not 64 hex digits");
            origin.sha256 = *digest;
            break;
        }
        case 6:
            if (!string_value(entry, origin.installed) || !real_utc_date(origin.installed))
                return refused(problem, "the origin record's installed is not a UTC date");
            break;
        default:
            break;
        }
    }
    if (!seen[0] || !seen[1] || !seen[5] || !seen[6])
        return refused(problem, "the origin record is missing a key");
    const bool catalogue = origin.kind == OriginKind::catalogue;
    if (catalogue && (!seen[2] || !seen[3] || !seen[4]))
        return refused(problem, "the origin record is missing a key");
    if (!catalogue && (seen[2] || seen[3] || seen[4]))
        return refused(problem, "the origin record names a release for a file");
    return origin;
}

std::optional<Origin> read_origin(const fs::path& folder) {
    const fs::path file = folder / std::string(origin_file_name);
    std::error_code error;
    const fs::file_status status = fs::symlink_status(file, error);
    if (error || !fs::is_regular_file(status))
        return std::nullopt;
    const auto size = fs::file_size(file, error);
    if (error || size > origin_most_bytes)
        return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
    if (size > 0 &&
        !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
        return std::nullopt;
    return parse_origin(bytes);
}

bool write_origin(
    const PackageKind& kind,
    const fs::path& root,
    std::string_view target,
    const Origin& origin,
    std::string* error
) {
    const auto fail = [&](std::string why) {
        if (error != nullptr)
            *error = std::move(why);
        return false;
    };
    // The lock first: nothing is written while an install, a change or
    // another copy of the game holds the root.
    const RootHold hold = detail::hold_root_alone(kind, root);
    if (!hold)
        return fail("the folder's lock is taken");
    const fs::path folder = root / detail::path_of(target);
    if (is_link_or_junction(folder))
        return fail("the folder is a link");
    if (kind.read_installed == nullptr)
        return fail("the folder holds no package");
    const InstalledPackage held = kind.read_installed(folder);
    if (held.kind != FolderKind::package)
        return fail("the folder holds no package");
    const std::string text = origin_text(origin);
    if (text.empty())
        return fail("the origin names no hash or date");
    if (text.size() > origin_most_bytes)
        return fail("the origin record is longer than 4 KiB");
    const auto* bytes = reinterpret_cast<const uint8_t*>(text.data());
    std::string replace_error;
    if (!oa::platform::replace_file(
            folder / std::string(origin_file_name), {bytes, text.size()}, &replace_error
        ))
        return fail(replace_error.empty() ? "the origin record was not replaced" : replace_error);
    return true;
}

std::string utc_date_text(std::chrono::system_clock::time_point time) {
    const std::chrono::year_month_day date{std::chrono::floor<std::chrono::days>(time)};
    const int year = static_cast<int>(date.year());
    const unsigned month = static_cast<unsigned>(date.month());
    const unsigned day = static_cast<unsigned>(date.day());
    std::string text(10, '0');
    const auto write = [&](int value, int width, int at) {
        int left = value < 0 ? -value : value;
        for (int place = width - 1; place >= 0; --place) {
            text[static_cast<std::size_t>(at + place)] = static_cast<char>('0' + left % 10);
            left /= 10;
        }
    };
    write(year, 4, 0);
    text[4] = '-';
    write(static_cast<int>(month), 2, 5);
    text[7] = '-';
    write(static_cast<int>(day), 2, 8);
    return text;
}

} // namespace oa::app::package_install
