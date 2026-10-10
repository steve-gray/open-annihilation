// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// One install ID per registry, when to check for catalogue updates, and the
// downloaded files. The values live in the player's preferences.
#pragma once

#include "oa/app/content/service.hpp"
#include "oa/platform/preferences.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace oa::app::content {

/// Prefix of the preference key that holds one registry's install ID.
///
/// The key is this prefix plus the registry id. It holds 32 lower-case
/// hexadecimal digits in groups of 8, 4, 4, 4 and 12, or `off`.
inline constexpr std::string_view install_id_key_prefix = "open-annihilation.install-id.";

/// The preference value that turns one registry's install ID off.
inline constexpr std::string_view install_id_off_value = "off";

/// The preference key for when catalogues are checked for updates.
inline constexpr std::string_view content_updates_key = "open-annihilation.content-updates";

/// The folder name, under the content folder, that holds downloaded packages.
inline constexpr std::string_view downloads_folder_name = "downloads";

/// Returns the preference key that holds one registry's install ID.
///
/// @param registry the registry id
/// @return install_id_key_prefix followed by `registry`
[[nodiscard]] std::string install_id_key(std::string_view registry);

/// Whether a registry's install ID is stored, and as what.
enum class InstallIdState : uint8_t {
    not_made, ///< absent, or text that is not an ID and not off
    on,       ///< a stored ID
    off,      ///< the player turned this registry's ID off
};

/// One registry's install ID as the preferences hold it.
struct InstallId {
    InstallIdState state = InstallIdState::not_made; ///< whether an ID is stored
    std::string text{};                              ///< the ID when state is on; empty otherwise
};

/// Reads one registry's install ID from the preference values.
///
/// Absent text, and any text other than a valid ID or install_id_off_value,
/// reads as not made. The values are not changed.
///
/// @param values the preference values
/// @param registry the registry id
/// @return the stored state, and the ID when one is stored
[[nodiscard]] InstallId
read_install_id(const oa::platform::preferences::Values& values, std::string_view registry);

/// Makes a new install ID from 16 bytes of the system's generator.
///
/// The text is 32 lower-case hexadecimal digits in groups of 8, 4, 4, 4 and
/// 12. Nothing is made when the generator cannot be read, and no other
/// source of bytes is used.
///
/// @return the ID, or nothing when the generator cannot be read
[[nodiscard]] std::optional<std::string> make_install_id_text();

/// Reports whether text is an install ID.
///
/// A valid ID is 36 characters: lower-case hexadecimal digits with hyphens
/// after 8, 12, 16 and 20 of those digits.
///
/// @param text the text to judge
/// @return true when `text` is an install ID
[[nodiscard]] bool install_id_text_valid(std::string_view text) noexcept;

/// Returns the form Settings shows for an install ID.
///
/// The first 8 hexadecimal digits, then a gap, then the last 4, as in
/// `7f3a90d2 · · · c91e`. Text that is not an install ID gives an empty
/// string.
///
/// @param text an install ID
/// @return the short form, or empty when `text` is not an install ID
[[nodiscard]] std::string install_id_shown(std::string_view text);

/// Returns one registry's install ID, making and storing one when none exists.
///
/// An ID that is already stored is returned unchanged. Nothing is made when
/// the stored value is off, or when the generator cannot be read, and then
/// the values are left as they were. A value that is not an ID and not off
/// is replaced with a new ID.
///
/// @param[in,out] values the preference values
/// @param registry the registry id
/// @param[out] changed true when a new ID was stored
/// @return the ID, or nothing when it is off or the generator cannot be read
[[nodiscard]] std::optional<std::string> ensure_install_id(
    oa::platform::preferences::Values& values, std::string_view registry, bool& changed
);

/// Stores a new install ID for one registry, replacing off or an older ID.
///
/// The values are left unchanged when the generator cannot be read.
///
/// @param[in,out] values the preference values
/// @param registry the registry id
/// @return true when a new ID was stored
[[nodiscard]] bool
reset_install_id(oa::platform::preferences::Values& values, std::string_view registry);

/// Turns one registry's install ID off.
///
/// The key is set to install_id_off_value. The ID that was stored is gone.
///
/// @param[in,out] values the preference values
/// @param registry the registry id
void turn_install_id_off(oa::platform::preferences::Values& values, std::string_view registry);

/// Forgets one registry's install ID so the next need makes a new one.
///
/// The key is removed. No ID is made here, including when the key was off.
///
/// @param[in,out] values the preference values
/// @param registry the registry id
void turn_install_id_on(oa::platform::preferences::Values& values, std::string_view registry);

/// Reads when the player wants catalogues checked for updates.
///
/// `automatically`, `library` and `never` are the three settings. Absent
/// text, and any other text, reads as automatically.
///
/// @param values the preference values
/// @return the setting
[[nodiscard]] CheckForUpdates
read_check_for_updates(const oa::platform::preferences::Values& values);

/// Writes when the player wants catalogues checked for updates.
///
/// The stored words are `automatically`, `library` and `never`.
///
/// @param[in,out] values the preference values
/// @param check the setting
void write_check_for_updates(oa::platform::preferences::Values& values, CheckForUpdates check);

/// Returns the folder that holds downloaded packages.
///
/// The folder is `downloads` inside the content folder of `data_folder`.
/// It need not exist.
///
/// @param data_folder the engine's data folder
/// @return the downloads folder
[[nodiscard]] std::filesystem::path downloads_folder(const std::filesystem::path& data_folder);

/// Measures the downloaded packages in a folder.
///
/// Counts the bytes of regular files in `folder` itself whose names end in
/// `.oalang`, `.oamod` or `.oamap`, and of the matching `.part` files. A
/// file in a subfolder is not counted, and nor is `queue.yaml` or any other
/// name. A folder that cannot be read measures 0.
///
/// @param folder the downloads folder
/// @return the total size, in bytes
[[nodiscard]] uint64_t downloaded_bytes(const std::filesystem::path& folder) noexcept;

/// What emptying the downloads folder removed and what it left in use.
struct EmptyResult {
    uint64_t removed_bytes = 0; ///< bytes of the files removed
    std::size_t kept = 0;       ///< download files left because they are in use
};

/// Removes downloaded packages from a folder, except the ones in use.
///
/// Removes regular files in `folder` itself whose names end in `.oalang`,
/// `.oamod` or `.oamap`, and the matching `.part` files. Never removes
/// `queue.yaml`, a file named in `in_use`, a directory, a link, or anything
/// outside `folder`. A name in `in_use` may be the file's path or its name
/// in `folder`.
///
/// @param folder the downloads folder
/// @param in_use files a download is using now
/// @return the bytes removed, and how many download files were left in use
EmptyResult
empty_downloads(const std::filesystem::path& folder, std::span<const std::filesystem::path> in_use);

} // namespace oa::app::content
