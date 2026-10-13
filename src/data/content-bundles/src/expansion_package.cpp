// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/content_bundles/expansion_package.hpp"

#include "oa/formats/oamod.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::content_bundles {
namespace {

namespace oamod = oa::formats::oamod;
using oamod::KeyKind;
using oamod::Node;
using oamod::NodeKind;

/// The manifest's format.
constexpr int64_t format_version = 1;

/// The longest name, version, email or packager, in bytes.
constexpr std::size_t most_short = 64;

/// The longest version, in bytes.
constexpr std::size_t most_version = 32;

/// The highest packaging revision.
constexpr int64_t most_revision = 65535;

/// Keys the top of oaexp.yaml may hold.
constexpr std::string_view top_keys[] = {
    "oaexp",
    "id",
    "name",
    "version",
    "extends",
    "needs",
    "author",
    "packaging",
};

/// Keys author may hold.
constexpr std::string_view author_keys[] = {"name", "email"};

/// Keys packaging may hold.
constexpr std::string_view packaging_keys[] = {"revision", "date", "packager"};

/// Sets `error` when the caller wants it.
///
/// @param error where the text goes; may be null
/// @param text the text
void set_error(std::string* error, std::string text) {
    if (error != nullptr)
        *error = std::move(text);
}

/// Reports whether a mapping uses only the named string keys.
///
/// @param mapping the mapping
/// @param allowed the keys it may use
/// @param[out] error the reason; may be null
/// @return true when every key is one of `allowed`
bool only_keys(const Node& mapping, std::span<const std::string_view> allowed, std::string* error) {
    if (mapping.kind != NodeKind::mapping) {
        set_error(error, "oaexp.yaml is not a mapping");
        return false;
    }
    for (const Node& child : mapping.children) {
        if (child.key.kind != KeyKind::string) {
            set_error(error, "oaexp.yaml has a key it does not use");
            return false;
        }
        bool known = false;
        for (const std::string_view key : allowed) {
            if (key == child.key.text) {
                known = true;
                break;
            }
        }
        if (!known) {
            set_error(error, "oaexp.yaml has a key it does not use");
            return false;
        }
    }
    return true;
}

/// Reads a whole number from a number node.
///
/// @param node the value
/// @param[out] value the number, when it is a whole number
/// @return true when `value` was set
bool whole_number(const Node& node, int64_t& value) noexcept {
    return node.kind == NodeKind::number && oamod::integer_value(node.number, value);
}

/// Reports whether a text is between `least` and `most` bytes.
///
/// @param text the text
/// @param least the fewest bytes
/// @param most the most bytes
/// @return true when the size is in that range
bool sized(std::string_view text, std::size_t least, std::size_t most) noexcept {
    return text.size() >= least && text.size() <= most;
}

/// Reports whether `text` is a real day written `YYYY-MM-DD`.
///
/// @param text the day
/// @return true when it is a Gregorian day, 29 February included in a leap year
bool real_day(std::string_view text) noexcept {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-')
        return false;
    const auto number = [](std::string_view digits) noexcept {
        int value = 0;
        for (const char digit : digits) {
            if (digit < '0' || digit > '9')
                return -1;
            value = value * 10 + (digit - '0');
        }
        return value;
    };
    const int year = number(text.substr(0, 4));
    const int month = number(text.substr(5, 2));
    const int day = number(text.substr(8, 2));
    if (year < 1 || month < 1 || month > 12 || day < 1)
        return false;
    const int lengths[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int length = lengths[month];
    const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (month == 2 && leap)
        length = 29;
    return day <= length;
}

/// Reports whether `text` can be an email address this manifest stores.
///
/// @param text the address
/// @return true when it holds an `@` that is not the first or last byte
bool email_address(std::string_view text) noexcept {
    if (!sized(text, 1, most_short))
        return false;
    const std::size_t at = text.find('@');
    return at != std::string_view::npos && at > 0 && at + 1 < text.size() &&
           text.find('@', at + 1) == std::string_view::npos;
}

/// Reads a list of ids and requires it to be `expected`, in order.
///
/// @param node the value
/// @param expected the table's ids
/// @param key the key's name, for the reason
/// @param[out] ids the ids, when they match
/// @param[out] error the reason; may be null
/// @return true when the list is `expected`
bool read_id_list(
    const Node& node,
    std::span<const std::string_view> expected,
    std::string_view key,
    std::vector<std::string>& ids,
    std::string* error
) {
    if (node.kind != NodeKind::sequence || node.children.size() != expected.size()) {
        set_error(error, std::string(key) + " is not the expansion's list");
        return false;
    }
    std::vector<std::string> read;
    read.reserve(expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const Node& item = node.children[index];
        if (item.kind != NodeKind::string || item.text != expected[index]) {
            set_error(error, std::string(key) + " is not the expansion's list");
            return false;
        }
        read.push_back(item.text);
    }
    ids = std::move(read);
    return true;
}

/// Reports whether two id lists are the same, in order.
///
/// @param left the first
/// @param right the second
/// @return true when they are the same
bool same_ids(
    std::span<const std::string_view> left, const std::vector<std::string>& right
) noexcept {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index] != right[index])
            return false;
    }
    return true;
}

/// Reports whether a copy name is one this reader compares.
///
/// @param name the name
/// @return true when it is a relative path with `/` between parts
bool copy_name_ok(std::string_view name) noexcept {
    if (name.empty() || name.front() == '/' || name.back() == '/')
        return false;
    if (name.find('\\') != std::string_view::npos)
        return false;
    std::size_t at = 0;
    while (at < name.size()) {
        const std::size_t slash = name.find('/', at);
        const std::string_view part =
            name.substr(0, slash == std::string_view::npos ? name.size() : slash).substr(at);
        if (part.empty() || part == "." || part == "..")
            return false;
        if (slash == std::string_view::npos)
            break;
        at = slash + 1;
    }
    return true;
}

/// The table file of a copy name, compared exactly.
///
/// @param bundle the expansion
/// @param name the copy name
/// @return the file, or null
const ContentFile* file_named(const Bundle& bundle, std::string_view name) noexcept {
    for (const ContentFile& file : bundle.files) {
        if (file.copy_name == name)
            return &file;
    }
    return nullptr;
}

} // namespace

bool read_expansion_manifest(
    std::span<const uint8_t> text, ExpansionManifest& manifest, std::string* error
) {
    Node root;
    oamod::ReadError failure;
    if (!oamod::read_document(text, root, failure)) {
        set_error(error, oamod::rule_message(failure.rule));
        return false;
    }
    if (!only_keys(root, top_keys, error))
        return false;
    const Node* format = oamod::find_entry(root, "oaexp");
    int64_t format_value = 0;
    if (format == nullptr || !whole_number(*format, format_value) ||
        format_value != format_version) {
        set_error(error, "oaexp must be 1");
        return false;
    }
    const Node* id_node = oamod::find_entry(root, "id");
    if (id_node == nullptr || id_node->kind != NodeKind::string) {
        set_error(error, "It is not one of the game's expansions.");
        return false;
    }
    const Bundle* bundle = find_bundle(id_node->text);
    if (bundle == nullptr || bundle->kind != Kind::expansion) {
        set_error(error, "It is not one of the game's expansions.");
        return false;
    }
    const Node* name_node = oamod::find_entry(root, "name");
    if (name_node == nullptr || name_node->kind != NodeKind::string ||
        name_node->text != bundle->english_name || !sized(name_node->text, 1, most_short)) {
        set_error(error, "name is not the expansion's name");
        return false;
    }
    const Node* version_node = oamod::find_entry(root, "version");
    if (version_node == nullptr || version_node->kind != NodeKind::string ||
        !sized(version_node->text, 1, most_version)) {
        set_error(error, "version must be 1 to 32 bytes");
        return false;
    }
    const Node* extends_node = oamod::find_entry(root, "extends");
    const Node* needs_node = oamod::find_entry(root, "needs");
    if (extends_node == nullptr || needs_node == nullptr) {
        set_error(error, "extends is not the expansion's list");
        return false;
    }
    ExpansionManifest read;
    if (!read_id_list(*extends_node, bundle->extends, "extends", read.extends, error))
        return false;
    if (!read_id_list(*needs_node, bundle->needs, "needs", read.needs, error))
        return false;
    const Node* author = oamod::find_entry(root, "author");
    if (author == nullptr || !only_keys(*author, author_keys, error)) {
        if (author == nullptr)
            set_error(error, "author must name someone");
        return false;
    }
    const Node* author_name = oamod::find_entry(*author, "name");
    if (author_name == nullptr || author_name->kind != NodeKind::string ||
        !sized(author_name->text, 1, most_short)) {
        set_error(error, "author must name someone");
        return false;
    }
    read.author.name = author_name->text;
    if (const Node* email = oamod::find_entry(*author, "email")) {
        if (email->kind != NodeKind::string || !email_address(email->text)) {
            set_error(error, "author.email is not an email address");
            return false;
        }
        read.author.email = email->text;
    }
    const Node* packaging = oamod::find_entry(root, "packaging");
    if (packaging == nullptr || !only_keys(*packaging, packaging_keys, error)) {
        if (packaging == nullptr)
            set_error(error, "packaging.revision must be a whole number from 1 to 65535");
        return false;
    }
    const Node* revision = oamod::find_entry(*packaging, "revision");
    int64_t revision_value = 0;
    if (revision == nullptr || !whole_number(*revision, revision_value) || revision_value < 1 ||
        revision_value > most_revision) {
        set_error(error, "packaging.revision must be a whole number from 1 to 65535");
        return false;
    }
    read.packaging.revision = static_cast<int32_t>(revision_value);
    if (const Node* date = oamod::find_entry(*packaging, "date")) {
        if (date->kind != NodeKind::string || !real_day(date->text)) {
            set_error(error, "packaging.date is not a day written YYYY-MM-DD");
            return false;
        }
        read.packaging.date = date->text;
    }
    if (const Node* packager = oamod::find_entry(*packaging, "packager")) {
        if (packager->kind != NodeKind::string || !sized(packager->text, 1, most_short)) {
            set_error(error, "packaging.packager must be 1 to 64 bytes");
            return false;
        }
        read.packaging.packager = packager->text;
    }
    read.id = bundle->id;
    read.name = name_node->text;
    read.version = version_node->text;
    read.bundle = bundle;
    manifest = std::move(read);
    return true;
}

ExpansionFaultReport
match_expansion_files(const Bundle& bundle, std::span<const MeasuredExpansionFile> files) {
    ExpansionFaultReport report;
    for (const MeasuredExpansionFile& file : files) {
        if (!copy_name_ok(file.name)) {
            report.fault = ExpansionFault::unsafe;
            report.name = std::string(file.name);
            return report;
        }
        std::size_t seen = 0;
        for (const MeasuredExpansionFile& other : files) {
            if (other.name == file.name)
                ++seen;
        }
        if (seen != 1) {
            report.fault = ExpansionFault::duplicate;
            report.name = std::string(file.name);
            return report;
        }
        if (file_named(bundle, file.name) == nullptr) {
            report.fault = ExpansionFault::stray;
            report.name = std::string(file.name);
            return report;
        }
    }
    for (const ContentFile& expected : bundle.files) {
        const MeasuredExpansionFile* found = nullptr;
        for (const MeasuredExpansionFile& file : files) {
            if (file.name == expected.copy_name) {
                found = &file;
                break;
            }
        }
        if (found == nullptr) {
            report.fault = ExpansionFault::missing;
            report.name = std::string(expected.copy_name);
            return report;
        }
        if (found->size != expected.size) {
            report.fault = ExpansionFault::size;
            report.name = std::string(expected.copy_name);
            return report;
        }
        if (!known_hash(expected, found->digest)) {
            report.fault = ExpansionFault::hash;
            report.name = std::string(expected.copy_name);
            return report;
        }
    }
    return report;
}

bool expansion_identity_matches(const ExpansionManifest& manifest, const Bundle& bundle) noexcept {
    return manifest.bundle != nullptr && bundle.kind == Kind::expansion &&
           manifest.id == bundle.id && manifest.name == bundle.english_name &&
           same_ids(bundle.extends, manifest.extends) && same_ids(bundle.needs, manifest.needs);
}

} // namespace oa::data::content_bundles
