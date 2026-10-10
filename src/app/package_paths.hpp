// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Paths of package folders as the settings keep them, shared by the install
// runtime and what each kind asks of the running game.
#pragma once

#include "oa/app/game_directory.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>

namespace oa::app {

/// Returns a folder's path as the settings keep it: absolute, normal, UTF-8.
///
/// @param folder the folder
/// @return its path
[[nodiscard]] inline std::string kept_path(const fs::path& folder) {
    std::error_code error;
    const fs::path absolute = fs::absolute(folder, error);
    return path_to_utf8((error ? folder : absolute).lexically_normal());
}

/// Tells whether two kept paths name one folder: compared without case on
/// the systems whose file systems fold it.
///
/// @param left one path
/// @param right the other
/// @return true when they match
[[nodiscard]] inline bool same_folder(std::string_view left, std::string_view right) {
#if defined(_WIN32) || defined(__APPLE__)
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z')
            a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
#else
    return left == right;
#endif
}

} // namespace oa::app
