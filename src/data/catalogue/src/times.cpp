// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/catalogue/catalogue.hpp"

#include <chrono>

namespace oa::data::catalogue {
namespace {

/// Reads `count` decimal digits into `out`.
///
/// @param text the time
/// @param at the first digit
/// @param count how many digits
/// @param[out] out the number
/// @return true when every one of those bytes is a digit
bool digits(std::string_view text, std::size_t at, std::size_t count, int& out) noexcept {
    if (at + count > text.size())
        return false;
    int value = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const char byte = text[at + index];
        if (byte < '0' || byte > '9')
            return false;
        value = value * 10 + (byte - '0');
    }
    out = value;
    return true;
}

} // namespace

std::optional<int64_t> parse_utc_time(std::string_view text) {
    // YYYY-MM-DDTHH:MM:SSZ is 20 bytes. A fraction, if present, sits before Z.
    if (text.size() < 20)
        return std::nullopt;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!digits(text, 0, 4, year) || text[4] != '-' || !digits(text, 5, 2, month) ||
        text[7] != '-' || !digits(text, 8, 2, day) || text[10] != 'T' ||
        !digits(text, 11, 2, hour) || text[13] != ':' || !digits(text, 14, 2, minute) ||
        text[16] != ':' || !digits(text, 17, 2, second))
        return std::nullopt;
    std::size_t index = 19;
    if (index < text.size() && text[index] == '.') {
        ++index;
        if (index >= text.size() || text[index] < '0' || text[index] > '9')
            return std::nullopt;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9')
            ++index;
    }
    if (index + 1 != text.size() || text[index] != 'Z')
        return std::nullopt;
    if (hour > 23 || minute > 59 || second > 59)
        return std::nullopt;
    // sys_days is the civil date in UTC. The local zone is not consulted.
    const std::chrono::year_month_day date{
        std::chrono::year{year},
        std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)},
    };
    if (!date.ok())
        return std::nullopt;
    const auto stamp = std::chrono::sys_days{date} + std::chrono::hours{hour} +
                       std::chrono::minutes{minute} + std::chrono::seconds{second};
    return stamp.time_since_epoch().count();
}

} // namespace oa::data::catalogue
