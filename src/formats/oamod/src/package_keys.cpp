// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/oamod/package_keys.hpp"

#include <string>

namespace oa::formats::oamod {
namespace {

/// The most bytes an engine requirement holds.
constexpr size_t engine_range_max_bytes = 64;
/// The most comparisons an engine requirement holds.
constexpr size_t engine_range_max_terms = 4;
/// The most bytes a homepage holds.
constexpr size_t homepage_max_bytes = 256;
/// The most bytes a tag holds.
constexpr size_t tag_max_bytes = 32;
/// The most tags a manifest lists.
constexpr size_t tags_max_count = 8;
/// The largest number a version part holds.
constexpr uint32_t version_part_max = 65535;
/// The https scheme and its separator, in lower case.
constexpr std::string_view https_scheme = "https://";
/// The http scheme and its separator, in lower case.
constexpr std::string_view http_scheme = "http://";

/// The 1-based byte a 0-based index names, for a requirement's error.
///
/// @param index the index
/// @return its byte, counted from 1
std::string byte_of(size_t index) {
    return std::to_string(index + 1);
}

/// Writes what a requirement gets wrong, when the caller asked.
///
/// @param error the caller's string, or null
/// @param message what is wrong
void set_error(std::string* error, std::string message) {
    if (error != nullptr)
        *error = std::move(message);
}

/// Tells whether `text` begins with `scheme`, ignoring the case of letters.
///
/// @param text the address
/// @param scheme the scheme and its separator, in lower case
/// @return true when `text` begins with `scheme`
bool starts_with_scheme(std::string_view text, std::string_view scheme) noexcept {
    if (text.size() < scheme.size())
        return false;
    for (size_t index = 0; index < scheme.size(); ++index) {
        unsigned char byte = static_cast<unsigned char>(text[index]);
        if (byte >= 'A' && byte <= 'Z')
            byte = static_cast<unsigned char>(byte - 'A' + 'a');
        if (byte != static_cast<unsigned char>(scheme[index]))
            return false;
    }
    return true;
}

/// Reads one version part and advances past it.
///
/// @param text the requirement
/// @param[in,out] index the part's first byte
/// @param[out] part the part
/// @param[out] error what is wrong, when the part is not one
/// @return true when a part was read
bool read_version_part(std::string_view text, size_t& index, uint16_t& part, std::string& error) {
    if (index >= text.size() || text[index] < '0' || text[index] > '9') {
        error = "expected a version part at byte " + byte_of(index);
        return false;
    }
    if (text[index] == '0' && index + 1 < text.size() && text[index + 1] >= '0' &&
        text[index + 1] <= '9') {
        error = "a version part has a leading zero at byte " + byte_of(index);
        return false;
    }
    uint32_t value = 0;
    const size_t start = index;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
        value = value * 10u + static_cast<uint32_t>(text[index] - '0');
        ++index;
        if (value > version_part_max) {
            error = "a version part is above 65535 at byte " + byte_of(start);
            return false;
        }
    }
    part = static_cast<uint16_t>(value);
    return true;
}

/// Reads MAJOR.MINOR.PATCH starting at `index`.
///
/// @param text the requirement
/// @param[in,out] index the version's first byte; the first byte after it on success
/// @param[out] version the version
/// @param[out] error what is wrong, when the text there is not a version
/// @return true when a version was read
bool read_version_at(
    std::string_view text, size_t& index, EngineVersion& version, std::string& error
) {
    uint16_t major = 0;
    uint16_t minor = 0;
    uint16_t patch = 0;
    if (!read_version_part(text, index, major, error))
        return false;
    if (index >= text.size() || text[index] != '.') {
        error = "expected '.' in a version at byte " + byte_of(index);
        return false;
    }
    ++index;
    if (!read_version_part(text, index, minor, error))
        return false;
    if (index >= text.size() || text[index] != '.') {
        error = "expected '.' in a version at byte " + byte_of(index);
        return false;
    }
    ++index;
    if (!read_version_part(text, index, patch, error))
        return false;
    if (index < text.size() && text[index] == '.') {
        error = "a version has three parts at byte " + byte_of(index);
        return false;
    }
    version = EngineVersion{major, minor, patch};
    return true;
}

/// The operator at `index`, and how many bytes it occupies.
///
/// @param text the requirement
/// @param index where an operator would start
/// @param[out] comparison the operator
/// @param[out] length its bytes
/// @return true when an operator is there
bool read_operator(
    std::string_view text, size_t index, EngineComparison& comparison, size_t& length
) noexcept {
    const std::string_view rest = text.substr(index);
    if (rest.starts_with(">=")) {
        comparison = EngineComparison::at_least;
        length = 2;
        return true;
    }
    if (rest.starts_with("<=")) {
        comparison = EngineComparison::at_most;
        length = 2;
        return true;
    }
    if (rest.starts_with(">")) {
        comparison = EngineComparison::above;
        length = 1;
        return true;
    }
    if (rest.starts_with("<")) {
        comparison = EngineComparison::below;
        length = 1;
        return true;
    }
    if (rest.starts_with("=")) {
        comparison = EngineComparison::exactly;
        length = 1;
        return true;
    }
    return false;
}

/// The operator as it is written, for an error that follows it.
///
/// @param comparison the operator
/// @return its spelling
std::string_view operator_text(EngineComparison comparison) noexcept {
    switch (comparison) {
    case EngineComparison::at_least:
        return ">=";
    case EngineComparison::above:
        return ">";
    case EngineComparison::at_most:
        return "<=";
    case EngineComparison::below:
        return "<";
    case EngineComparison::exactly:
        return "=";
    }
    return {};
}

/// Skips ASCII spaces.
///
/// @param text the requirement
/// @param[in,out] index the first space, or the next other byte
void skip_spaces(std::string_view text, size_t& index) noexcept {
    while (index < text.size() && text[index] == ' ')
        ++index;
}

/// Tells whether one comparison holds for a release.
///
/// @param comparison the operator
/// @param version this build
/// @param required the release the comparison names
/// @return true when the comparison holds
bool comparison_holds(
    EngineComparison comparison, EngineVersion version, EngineVersion required
) noexcept {
    const auto order = version <=> required;
    switch (comparison) {
    case EngineComparison::at_least:
        return order >= 0;
    case EngineComparison::above:
        return order > 0;
    case EngineComparison::at_most:
        return order <= 0;
    case EngineComparison::below:
        return order < 0;
    case EngineComparison::exactly:
        return order == 0;
    }
    return false;
}

/// One phrase of a requirement, as a player reads that comparison.
///
/// @param comparison the operator
/// @param version the release it names
/// @return the phrase
std::string comparison_phrase(EngineComparison comparison, EngineVersion version) {
    const std::string text = engine_version_text(version);
    switch (comparison) {
    case EngineComparison::at_least:
        return text + " or later";
    case EngineComparison::above:
        return "later than " + text;
    case EngineComparison::at_most:
        return text + " or earlier";
    case EngineComparison::below:
        return "before " + text;
    case EngineComparison::exactly:
        return "exactly " + text;
    }
    return {};
}

/// Appends one problem.
///
/// @param problems the list
/// @param position where the value starts
/// @param path the key's path
/// @param message what is wrong
void add_problem(
    std::vector<KeyProblem>& problems, TextPosition position, std::string path, std::string message
) {
    problems.push_back(KeyProblem{position, std::move(path), std::move(message)});
}

/// Reads the tags, when the list follows every rule.
///
/// @param node the tags value
/// @param[out] tags the tags, when the whole list passed
/// @param problems the problems
void read_tags(
    const Node& node, std::vector<std::string>& tags, std::vector<KeyProblem>& problems
) {
    if (node.kind != NodeKind::sequence) {
        add_problem(problems, node.position, "tags", "tags must be a list of 1 to 8 strings");
        return;
    }
    if (node.children.empty()) {
        add_problem(
            problems, node.position, "tags", "tags must list 1 to 8 tags; leave the key out instead"
        );
        return;
    }
    if (node.children.size() > tags_max_count) {
        add_problem(
            problems,
            node.position,
            "tags",
            "tags lists " + std::to_string(node.children.size()) + " tags; at most 8"
        );
        return;
    }
    std::vector<std::string> read;
    bool passed = true;
    for (size_t index = 0; index < node.children.size(); ++index) {
        const Node& item = node.children[index];
        const std::string path = "tags[" + std::to_string(index) + "]";
        if (item.kind != NodeKind::string || !tag_valid(item.text)) {
            add_problem(
                problems,
                item.position,
                path,
                path + " must be 1 to 32 bytes of lower-case kebab-case"
            );
            passed = false;
            continue;
        }
        bool repeated = false;
        for (const std::string& earlier : read) {
            if (earlier == item.text) {
                repeated = true;
                break;
            }
        }
        if (repeated) {
            add_problem(problems, item.position, path, path + " repeats " + item.text);
            passed = false;
            continue;
        }
        read.push_back(item.text);
    }
    if (passed)
        tags = std::move(read);
}

} // namespace

std::optional<EngineVersion> parse_engine_version(std::string_view text) noexcept {
    EngineVersion version{};
    std::string error;
    size_t index = 0;
    if (!read_version_at(text, index, version, error) || index != text.size())
        return std::nullopt;
    return version;
}

std::string engine_version_text(EngineVersion version) {
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
           std::to_string(version.patch);
}

std::optional<EngineRange> parse_engine_range(std::string_view text, std::string* error) {
    if (text.size() > engine_range_max_bytes) {
        set_error(error, "an engine requirement is longer than 64 bytes");
        return std::nullopt;
    }
    if (text.empty()) {
        set_error(error, "an engine requirement is empty");
        return std::nullopt;
    }
    EngineRange range{};
    size_t index = 0;
    for (;;) {
        if (range.terms.size() == engine_range_max_terms) {
            set_error(error, "at most 4 comparisons at byte " + byte_of(index));
            return std::nullopt;
        }
        EngineComparison comparison{};
        size_t length = 0;
        if (!read_operator(text, index, comparison, length)) {
            set_error(error, "expected a comparison at byte " + byte_of(index));
            return std::nullopt;
        }
        index += length;
        skip_spaces(text, index);
        const size_t version_at = index;
        const bool version_started =
            version_at < text.size() && text[version_at] >= '0' && text[version_at] <= '9';
        EngineVersion version{};
        std::string version_error;
        if (!read_version_at(text, index, version, version_error)) {
            if (!version_started)
                set_error(
                    error,
                    "expected a version after '" + std::string{operator_text(comparison)} +
                        "' at byte " + byte_of(version_at)
                );
            else
                set_error(error, std::move(version_error));
            return std::nullopt;
        }
        range.terms.emplace_back(comparison, version);
        if (index == text.size())
            return range;
        const size_t after = index;
        skip_spaces(text, index);
        if (index >= text.size() || text[index] != ',') {
            set_error(error, "unexpected text at byte " + byte_of(after));
            return std::nullopt;
        }
        ++index;
        skip_spaces(text, index);
        if (index >= text.size()) {
            set_error(error, "expected a comparison at byte " + byte_of(index));
            return std::nullopt;
        }
    }
}

bool engine_range_met(const EngineRange& range, EngineVersion version) noexcept {
    for (const auto& [comparison, required] : range.terms) {
        if (!comparison_holds(comparison, version, required))
            return false;
    }
    return true;
}

std::string describe_engine_range(const EngineRange& range) {
    std::string text;
    for (const auto& [comparison, version] : range.terms) {
        if (!text.empty())
            text += ", ";
        text += comparison_phrase(comparison, version);
    }
    return text;
}

bool homepage_valid(std::string_view text) noexcept {
    if (text.empty() || text.size() > homepage_max_bytes)
        return false;
    std::string_view body;
    if (starts_with_scheme(text, https_scheme))
        body = text.substr(https_scheme.size());
    else if (starts_with_scheme(text, http_scheme))
        body = text.substr(http_scheme.size());
    else
        return false;
    if (body.empty())
        return false;
    for (const char byte : body) {
        const auto value = static_cast<unsigned char>(byte);
        if (value <= ' ' || value == '\x7f')
            return false;
    }
    return true;
}

bool tag_valid(std::string_view text) noexcept {
    if (text.empty() || text.size() > tag_max_bytes || text.front() == '-' || text.back() == '-')
        return false;
    char previous = 0;
    for (const char character : text) {
        const bool word =
            (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
        if (!word && character != '-')
            return false;
        if (character == '-' && previous == '-')
            return false;
        previous = character;
    }
    return true;
}

void read_package_keys(
    const Node& root,
    const Node* requires_block,
    PackageKeys& keys,
    std::vector<KeyProblem>& problems
) {
    if (const Node* homepage = find_entry(root, "homepage")) {
        if (homepage->kind != NodeKind::string)
            add_problem(problems, homepage->position, "homepage", "homepage must be a string");
        else if (!homepage_valid(homepage->text))
            add_problem(
                problems,
                homepage->position,
                "homepage",
                "homepage must be an http or https address of 1 to 256 bytes"
            );
        else
            keys.homepage = homepage->text;
    }
    if (const Node* tags = find_entry(root, "tags"))
        read_tags(*tags, keys.tags, problems);
    if (requires_block == nullptr)
        return;
    const Node* engine = find_entry(*requires_block, "engine");
    if (engine == nullptr)
        return;
    if (engine->kind != NodeKind::string) {
        add_problem(
            problems, engine->position, "requires.engine", "requires.engine must be a string"
        );
        return;
    }
    std::string error;
    if (!parse_engine_range(engine->text, &error)) {
        add_problem(problems, engine->position, "requires.engine", std::move(error));
        return;
    }
    keys.requires_engine = engine->text;
}

} // namespace oa::formats::oamod
