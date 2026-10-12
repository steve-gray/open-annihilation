// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Header values the socket client and the system HTTPS hops both read.
#pragma once

#include "oa/netgame/http/client.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace oa::netgame::http {

/// Compares two texts without regard to case.
///
/// @param left one text
/// @param right the other
/// @return true when they are the same letters
inline bool same_name(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        unsigned char a = static_cast<unsigned char>(left[i]);
        unsigned char b = static_cast<unsigned char>(right[i]);
        if (a >= 'A' && a <= 'Z')
            a = static_cast<unsigned char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<unsigned char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

/// Drops spaces and tabs from both ends.
///
/// @param text the text
/// @return the text inside the spaces
inline std::string_view trim_ows(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

/// Reads a decimal length.
///
/// @param text the digits
/// @param[out] value the length
/// @return false when the text is not a decimal length
inline bool parse_u64(std::string_view text, uint64_t& value) {
    if (text.empty())
        return false;
    uint64_t number = 0;
    for (const char byte : text) {
        if (byte < '0' || byte > '9')
            return false;
        const uint64_t digit = static_cast<uint64_t>(byte - '0');
        if (number > (UINT64_MAX - digit) / 10)
            return false;
        number = number * 10 + digit;
    }
    value = number;
    return true;
}

/// Reads a Content-Range value.
///
/// @param text the header value
/// @return the range, or nothing when it is not `bytes first-last/total`
inline std::optional<ContentRange> parse_content_range(std::string_view text) {
    text = trim_ows(text);
    if (text.size() < 6 || !same_name(text.substr(0, 5), "bytes") || text[5] != ' ')
        return std::nullopt;
    text.remove_prefix(6);
    const std::size_t dash = text.find('-');
    const std::size_t slash = text.find('/');
    if (dash == std::string_view::npos || slash == std::string_view::npos || slash < dash ||
        dash == 0)
        return std::nullopt;
    ContentRange range;
    if (!parse_u64(text.substr(0, dash), range.first))
        return std::nullopt;
    if (!parse_u64(text.substr(dash + 1, slash - dash - 1), range.last) || range.last < range.first)
        return std::nullopt;
    const std::string_view total = text.substr(slash + 1);
    if (total == "*")
        return range;
    uint64_t whole = 0;
    if (!parse_u64(total, whole) || range.last >= whole)
        return std::nullopt;
    range.total = whole;
    return range;
}

/// The first header of this name.
///
/// @param headers the headers
/// @param name the name
/// @return the value, or null
inline const std::string* header_value(const std::vector<Header>& headers, std::string_view name) {
    for (const Header& header : headers)
        if (same_name(header.name, name))
            return &header.value;
    return nullptr;
}

} // namespace oa::netgame::http
