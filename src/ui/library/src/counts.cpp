// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// How many times a package was downloaded, as the Library's details write it.
#include "oa/ui/library/text.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace oa::ui::library {
namespace {

/// Groups a number's digits in threes from the right.
///
/// @param count the number
/// @return the digits, with a comma between each group of three
std::string grouped_digits(uint64_t count) {
    const std::string digits = std::to_string(count);
    std::string grouped;
    grouped.reserve(digits.size() + digits.size() / 3);
    std::size_t placed = 0;
    for (std::size_t index = digits.size(); index > 0; --index) {
        if (placed == 3) {
            grouped.push_back(',');
            placed = 0;
        }
        grouped.push_back(digits[index - 1]);
        ++placed;
    }
    std::reverse(grouped.begin(), grouped.end());
    return grouped;
}

} // namespace

std::string downloads_text(uint64_t count) {
    if (count == 1)
        return std::string(shown({}, words::download_count_one));
    return filled({}, words::download_count_many, {{"count", grouped_digits(count)}});
}

} // namespace oa::ui::library
