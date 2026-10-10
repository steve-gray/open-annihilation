// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A second start hands its files to the copy of the game already running:
// on Windows and Linux, opening one of the four file types while the game
// runs starts another copy. The running copy holds an instance lock for its
// life; a start that carries only files to open, and finds the lock held,
// writes one request file for each into a hand-off folder and ends, and the
// running copy takes the requests at its main menu. The folder keeps the
// name opened-mods.
#pragma once

#include "oa/app/package_install.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace oa::app::package_install {

/// The instance lock's file, in the game's data folder.
inline constexpr std::string_view instance_lock_name = "open-annihilation.lock";
/// The hand-off folder, in the game's data folder.
inline constexpr std::string_view handoff_folder_name = "opened-mods";
/// A request file's extension.
inline constexpr std::string_view request_extension = ".request";

/// Takes the instance lock for this process's life.
///
/// @param file the lock file, made when it is missing
/// @return the lock; null when another copy holds it or the file cannot be made
[[nodiscard]] std::unique_ptr<FileLock> take_instance_lock(const std::filesystem::path& file);

/// Writes one request file for each file: <folder>/<name>.request holding
/// the file's path in UTF-8, written under a temporary name first and
/// renamed, so that a reader never sees half of one. The names sort in the
/// order the files were given, after any written before.
///
/// @param folder the hand-off folder, made when it is missing
/// @param files the files
/// @return the request files written; empty when the folder cannot be written
[[nodiscard]] std::vector<std::filesystem::path> hand_files_over(
    const std::filesystem::path& folder, const std::vector<std::filesystem::path>& files
);

/// Takes the requests waiting, oldest first, deleting each request file. A
/// request whose file no longer exists is dropped, and logged.
///
/// @param folder the hand-off folder
/// @return the files
[[nodiscard]] std::vector<std::filesystem::path>
take_handed_files(const std::filesystem::path& folder);

} // namespace oa::app::package_install
