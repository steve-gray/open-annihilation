// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Makes the running copy of the game the opener of the four file types for the user who runs
// it, with the game's icon on those files where the system takes one: the registry under
// HKEY_CURRENT_USER on Windows, and on Linux the MIME package, desktop entry and icons in the
// user's XDG data folder, with the system's database tools run after a change. macOS and iOS
// register through the bundle's Info.plist instead, and nothing is done at run time there.
// Each registration writes only what differs from what is there.
#pragma once

#include <stdint.h>

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::platform::file_types {

/// The mod package's file name extension, without its dot; matched without case.
inline constexpr std::string_view mod_extension = "oamod";
/// The MIME type of a mod package.
inline constexpr std::string_view mod_mime_type = "application/x-oamod";
/// What the system calls a mod package in its file managers.
inline constexpr std::string_view mod_type_name = "Open Annihilation mod";
/// The Windows program identifier the mod package's extension names, one of the four file types.
inline constexpr std::string_view mod_program_id = "OpenAnnihilation.Mod";
/// A language pack's file name extension, without its dot; matched without case.
inline constexpr std::string_view language_extension = "oalang";
/// The MIME type of a language pack.
inline constexpr std::string_view language_mime_type = "application/x-oalang";
/// What the system calls a language pack in its file managers.
inline constexpr std::string_view language_type_name = "Open Annihilation language pack";
/// The Windows program identifier a language pack's extension names.
inline constexpr std::string_view language_program_id = "OpenAnnihilation.Language";
/// A map pack's file name extension, without its dot; matched without case.
inline constexpr std::string_view map_extension = "oamap";
/// The MIME type of a map pack.
inline constexpr std::string_view map_mime_type = "application/x-oamap";
/// What the system calls a map pack in its file managers.
inline constexpr std::string_view map_type_name = "Open Annihilation map pack";
/// The Windows program identifier a map pack's extension names.
inline constexpr std::string_view map_program_id = "OpenAnnihilation.MapPack";
/// A registry file's extension, without its dot; matched without case. A .oareg file.
inline constexpr std::string_view registry_extension = "oareg";
/// The MIME type of a .oareg registry file.
inline constexpr std::string_view registry_mime_type = "application/x-oareg";
/// What the system calls a .oareg registry file in its file managers.
inline constexpr std::string_view registry_type_name = "Open Annihilation registry";
/// The Windows program identifier a .oareg file's extension names.
inline constexpr std::string_view registry_program_id = "OpenAnnihilation.Registry";
/// The name of the desktop entry, MIME package and program icon on Linux, without their
/// extensions; also the macOS and iOS bundle identifier.
inline constexpr std::string_view desktop_id = "net.coreprime.open-annihilation";
/// The command-line option the registrations use to open one of the four file types.
inline constexpr std::string_view open_option = "--open";
/// The most milliseconds a database tool may run before it is stopped.
inline constexpr uint32_t default_tool_time_limit_ms = 5000;

/// One of the four file types the game opens.
struct FileType {
    std::string_view extension{};        ///< the file name extension, without its dot
    std::string_view mime_type{};        ///< the MIME type
    std::string_view type_name{};        ///< what file managers call it
    std::string_view program_id{};       ///< the Windows program identifier
    std::string_view parent_mime_type{}; ///< the MIME type it is a kind of
};

/// The four file types, in the order a registration writes them: the three packages, each a
/// kind of zip, then a .oareg registry file, a kind of plain text.
inline constexpr std::array<FileType, 4> file_types{{
    {mod_extension, mod_mime_type, mod_type_name, mod_program_id, "application/zip"},
    {language_extension,
     language_mime_type,
     language_type_name,
     language_program_id,
     "application/zip"},
    {map_extension, map_mime_type, map_type_name, map_program_id, "application/zip"},
    {registry_extension, registry_mime_type, registry_type_name, registry_program_id, "text/plain"},
}};

/// Where a registration writes, and what it runs after a change; tests name scratch ones.
struct Places {
    /// The XDG data folder; empty for $XDG_DATA_HOME when it is absolute, else
    /// $HOME/.local/share.
    std::filesystem::path data_home{};
    /// Windows: the key under HKEY_CURRENT_USER that holds the file types.
    std::string classes_key{"Software\\Classes"};
    /// XDG: run the database tools (update-mime-database, update-desktop-database and, where
    /// the user's icon cache exists, gtk-update-icon-cache) whose run is due: after a change,
    /// and until a run succeeds.
    bool run_tools{true};
    /// Windows: tell the shell that a file association changed, after a change.
    bool notify_shell{true};
    /// XDG: the most milliseconds a database tool may run before it is stopped.
    uint32_t tool_time_limit_ms{default_tool_time_limit_ms};
};

/// What a registration did.
struct Registration {
    bool supported{}; ///< this system registers at run time (Windows, Linux)
    bool changed{};   ///< something was written
    /// What was written, skipped or run, one line each, for the log.
    std::vector<std::string> lines{};
    std::string error{}; ///< why it stopped, for the log; empty when it did not
};

/// Returns the program's path as the system would start it again: the executable's own path
/// on Windows (the module's file name) and on Linux (/proc/self/exe).
///
/// @return the path; nullopt where it is unknown, where the executable was deleted or replaced
///         since it started, and on macOS and iOS, which register through the bundle
[[nodiscard]] std::optional<std::filesystem::path> running_executable();

/// Registers `executable` as the opener of the four file types for the user who runs it,
/// writing only what differs from what is there, with `icon_png` as the program's and the
/// files' icon where the system takes an icon file (XDG). A Windows copy started from the
/// temporary folder, as one started from inside a zip is, and a Linux copy under $TMPDIR or
/// /tmp, inside a Flatpak or a Snap, or whose path holds a control character or is not UTF-8,
/// register nothing and say so in a line. macOS and iOS register through the bundle's
/// Info.plist: there supported is false and nothing is done. Never throws.
///
/// @param executable the program, absolute, as running_executable gives it
/// @param icon_png the icon, a PNG file's bytes; empty writes no icon
/// @param places where it writes; the defaults are the user's own
/// @return what was done
[[nodiscard]] Registration register_file_types(
    const std::filesystem::path& executable,
    std::span<const uint8_t> icon_png,
    const Places& places = {}
);

#ifndef _WIN32
/// Registers `executable` as the opener of the four file types in an XDG data folder: the MIME
/// package (mime_package), the desktop entry (desktop_entry) and the icon as the program's
/// and each type's, 256 pixels square, in the hicolor theme. Each file is written only
/// when its bytes differ, through a temporary file renamed over it. After a change it runs
/// update-mime-database for a new MIME package, update-desktop-database for a new desktop
/// entry, and gtk-update-icon-cache for a new icon where the user's hicolor folder holds an
/// icon cache, without a shell, each stopped after places.tool_time_limit_ms; a tool that is
/// not installed is no error. A run that succeeds leaves a stamp file in the folder the tool
/// works on (a dot, desktop_id and ".updated"), which a change to its files removes first;
/// while a tool's stamp is missing, each later start runs it again. Built on every system but
/// Windows, so that its tests run there too; register_file_types calls it on Linux only.
/// Never throws.
///
/// @param executable the program, absolute
/// @param icon_png the icon, a PNG file's bytes; empty writes no icon
/// @param places where it writes, and whether and how long it runs the tools
/// @return what was done
[[nodiscard]] Registration register_xdg(
    const std::filesystem::path& executable, std::span<const uint8_t> icon_png, const Places& places
);
#else
/// Registers `executable` as the opener of the four file types in the registry under
/// HKEY_CURRENT_USER\<places.classes_key>: each extension's program identifier and MIME type,
/// the program identifier's name, its icon (the executable's first icon, default_icon) and
/// its open command (open_command). Each value is written only when it is missing or differs,
/// and the shell is told once after any write. A choice of opener the player made in Windows
/// is never touched. Uses only calls Windows XP has. Never throws.
///
/// @param executable the program, absolute
/// @param places where it writes, and whether it tells the shell
/// @return what was done
[[nodiscard]] Registration
register_windows(const std::filesystem::path& executable, const Places& places);
#endif

/// Returns the desktop entry that opens the four file types with `executable`, with no entry
/// in the application menus.
///
/// @param executable the program, absolute; its UTF-8 spelling is written
/// @return the entry's text, UTF-8; empty when the path cannot be written in an entry
///         (desktop_exec_argument)
[[nodiscard]] std::string desktop_entry(const std::filesystem::path& executable);

/// Returns the shared-mime-info package that names the four file types, each a kind of its
/// parent: the three packages a kind of zip, and a .oareg file a kind of plain text.
///
/// @return the package's XML text, UTF-8
[[nodiscard]] std::string mime_package();

/// Returns a path as one argument of a desktop entry's Exec line: in double quotes, with ",
/// `, $ and \ preceded by a backslash, every backslash then doubled by the entry's own escape
/// rule, and each % written %%.
///
/// @param path_utf8 the path, UTF-8
/// @return the argument; empty when the path holds a control character or is not UTF-8,
///         which an entry cannot hold
[[nodiscard]] std::string desktop_exec_argument(std::string_view path_utf8);

/// Returns the Windows command that opens one of the four file types with `executable`:
/// "<executable>" --open "%1".
///
/// @param executable the program, absolute
/// @return the command, UTF-8
[[nodiscard]] std::string open_command(const std::filesystem::path& executable);

/// Returns the Windows icon location of `executable`'s first icon: <executable>,0, which the
/// system splits at its last comma.
///
/// @param executable the program, absolute
/// @return the location, UTF-8
[[nodiscard]] std::string default_icon(const std::filesystem::path& executable);

} // namespace oa::platform::file_types
