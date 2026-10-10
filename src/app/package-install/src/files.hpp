// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The installer's own file system operations that its sources share
// (files.cpp), and its shared text helpers.
#pragma once

#include "oa/app/package_install.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace oa::app::package_install {

namespace detail {

/// Remembers a kind whose own folders a later step may meet, such as a kind
/// a test built. The kind's prefix is copied, so the kind itself need not
/// outlive the note. The table's kinds need no note.
///
/// @param kind the kind
void note_kind(const PackageKind& kind);

/// Returns the kind whose prefix a folder name starts with, the longest
/// prefix when more than one would. A kind noted with note_kind counts
/// beside the table.
///
/// @param name the folder's name
/// @return the kind; null when the name is not one the installer keeps
[[nodiscard]] const PackageKind* kind_of_reserved(std::string_view name);

#ifdef _WIN32
/// Returns a path in the form that reaches past the system's 259
/// characters: absolute, with backslashes, after "\\?\" (or "\\?\UNC\").
///
/// @param path the path
/// @return its extended spelling
[[nodiscard]] std::wstring extended_path(const std::filesystem::path& path);
#endif

/// Tells whether a refused rename is worth trying again: another program,
/// such as a file sync or a virus scanner, holds the files for a moment.
///
/// @param error the rename's error
/// @return true for a sharing or lock violation or a refused access on
///         Windows, and a busy resource elsewhere
[[nodiscard]] bool retryable(const std::error_code& error) noexcept;

/// Removes one file, link, junction or empty folder, never entering a link;
/// a read-only file on Windows is made writable first.
///
/// @param path the entry
/// @param[out] error why it could not be removed
/// @return true when it was removed
bool remove_entry(const std::filesystem::path& path, std::error_code& error) noexcept;

/// Syncs a folder's entries to the disk where the system needs it (not on
/// Windows); a folder that cannot be opened is left.
///
/// @param folder the folder
void sync_folder(const std::filesystem::path& folder) noexcept;

/// Syncs a folder's entries as sync_folder does and, on macOS, where a sync
/// stops at the drive, has the drive write everything it holds to its
/// storage; a folder that cannot be opened is left.
///
/// @param folder the folder
void flush_to_storage(const std::filesystem::path& folder) noexcept;

/// Returns a text with its ASCII letters lowered.
///
/// @param text the text
/// @return the text
[[nodiscard]] std::string folded(std::string_view text);

/// Returns a path as UTF-8.
///
/// @param path the path
/// @return its UTF-8 spelling
[[nodiscard]] std::string utf8_of(const std::filesystem::path& path);

/// Returns a path from UTF-8.
///
/// @param text the UTF-8 spelling
/// @return the path
[[nodiscard]] std::filesystem::path path_of(std::string_view text);

/// Writes a line to standard error, which the log keeps, after
/// "open-annihilation: package install: ".
///
/// @param line the line
void log_line(std::string_view line);

/// Takes a hold on a kind's root folder's lock when nothing in this process
/// holds it and no other copy of the game does.
///
/// @param kind the kind, whose lock name the file takes
/// @param root the root folder, which must exist
/// @return the hold; null when this process already holds that root or the
///         lock is taken
[[nodiscard]] RootHold hold_root_alone(const PackageKind& kind, const std::filesystem::path& root);

} // namespace detail
} // namespace oa::app::package_install
