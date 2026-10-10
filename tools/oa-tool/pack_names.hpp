// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The names a pack will write. pack.cpp collects the folder into Items and
// check_names refuses one the installer would refuse. The test calls
// check_names with names on their own, so a case clash does not need a
// case-sensitive volume.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace oa::tool::detail {

/// One file or empty folder the package will hold.
struct Item {
    std::string name;           ///< path inside the folder, '/' between parts, no trailing '/'
    std::filesystem::path path; ///< where its bytes are read; a folder has none to read
    bool folder{};
    uint64_t bytes{}; ///< uncompressed size; 0 for a folder
};

/// The names a package will unpack, compared without ASCII case, so two
/// that differ only in case, or a file and a folder of one name, are refused.
/// Each folder a path passes through is counted once.
class CaseTree {
  public:

    /// Adds a path and every folder above it.
    ///
    /// @param path the path, '/' between parts, without a trailing '/'
    /// @param folder it is a folder
    /// @return false when a part clashes with one added before
    bool add(std::string_view path, bool folder);

    /// Returns how many folders have been added.
    ///
    /// @return the count
    [[nodiscard]] std::size_t folders() const noexcept;

  private:

    /// One part, in the spelling first added.
    struct Placed {
        std::string exact{};
        bool folder{};
    };

    std::map<std::string, Placed, std::less<>> placed_{};
    std::size_t folders_{};
};

/// Refuses a name the installer would refuse, a case clash, too many folders
/// or too many bytes.
///
/// @param items the items, in the order they will be written
/// @throws Failure when a name, a clash, the folder count or the size is refused
void check_names(const std::vector<Item>& items);

} // namespace oa::tool::detail
