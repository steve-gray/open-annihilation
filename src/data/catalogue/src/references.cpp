// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/catalogue/catalogue.hpp"

namespace oa::data::catalogue {
namespace {

/// The most bytes of one reference.
constexpr std::size_t max_reference_bytes = 512;

/// The most bytes of a mod or map-pack key.
constexpr std::size_t max_kebab_bytes = 64;

/// The shortest and longest language tag, in bytes.
constexpr std::size_t min_language_tag_bytes = 2;
constexpr std::size_t max_language_tag_bytes = 35;

/// Reports whether a byte is an ASCII letter.
///
/// @param letter the byte
/// @return true for `A`-`Z` or `a`-`z`
bool letter(char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

/// Reports whether a byte is a decimal digit.
///
/// @param value the byte
/// @return true for `0`-`9`
bool digit(char value) noexcept {
    return value >= '0' && value <= '9';
}

/// Reports whether a key is lower-case kebab-case of 1 to 64 bytes.
///
/// @param text the key
/// @return true for words of `a`-`z` and `0`-`9` joined by single hyphens
bool kebab_key(std::string_view text) noexcept {
    if (text.empty() || text.size() > max_kebab_bytes || text.front() == '-' || text.back() == '-')
        return false;
    char previous = 0;
    for (const char value : text) {
        const bool word = (value >= 'a' && value <= 'z') || digit(value);
        if (!word && value != '-')
            return false;
        if (value == '-' && previous == '-')
            return false;
        previous = value;
    }
    return true;
}

/// Reports whether a key is a language tag of 2 to 35 bytes.
///
/// The first part is letters. Each later part is letters and digits, joined
/// by one hyphen.
///
/// @param text the key
/// @return true when the text is such a tag
bool language_tag(std::string_view text) noexcept {
    if (text.size() < min_language_tag_bytes || text.size() > max_language_tag_bytes)
        return false;
    std::size_t index = 0;
    if (!letter(text[index]))
        return false;
    while (index < text.size() && letter(text[index]))
        ++index;
    while (index < text.size()) {
        if (text[index] != '-')
            return false;
        ++index;
        const std::size_t start = index;
        while (index < text.size() && (letter(text[index]) || digit(text[index])))
            ++index;
        if (index == start)
            return false;
    }
    return true;
}

} // namespace

bool valid_reference(std::string_view text) noexcept {
    if (text.empty() || text.size() > max_reference_bytes)
        return false;
    for (const char value : text) {
        const auto byte = static_cast<unsigned char>(value);
        if (byte <= 0x20 || byte > 0x7E)
            return false;
        if (value == '\\' || value == '%' || value == '?' || value == '#' || value == ':')
            return false;
    }
    if (text.find("//") != std::string_view::npos)
        return false;
    std::string_view rest = text;
    if (rest.front() == '/')
        rest.remove_prefix(1);
    if (rest.empty())
        return false;
    while (!rest.empty()) {
        const auto slash = rest.find('/');
        const std::string_view segment = rest.substr(0, slash);
        if (segment.empty() || segment == "." || segment == "..")
            return false;
        if (slash == std::string_view::npos)
            break;
        rest.remove_prefix(slash + 1);
        if (rest.empty())
            return false;
    }
    return true;
}

bool valid_package_key(Kind kind, std::string_view text) noexcept {
    if (text.find('@') != std::string_view::npos)
        return false;
    if (kind == Kind::oalang)
        return language_tag(text);
    return kebab_key(text);
}

} // namespace oa::data::catalogue
