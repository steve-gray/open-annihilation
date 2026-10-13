// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Total Annihilation folders found where Steam, Heroic, Lutris or Bottles put
// them: the places looked in, the readers of the files those programs keep
// (Steam's library list and app manifests, Heroic's installed list) and the
// search itself, which reads only within fixed bounds. Also the places a
// folder browser starts from: the home folder, its Downloads folder and the
// drives that are mounted.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdint.h>
#include <string>
#include <string_view>
#include <vector>

namespace oa::platform::game_installs {

/// Total Annihilation's app number on Steam.
inline constexpr uint32_t total_annihilation_steam_app = 298030;
/// The archive every Total Annihilation folder holds, matched without regard to case.
inline constexpr std::string_view game_archive_name = "totala1.hpi";
/// The most bytes of a library or manifest file read.
inline constexpr std::size_t library_file_limit = std::size_t{1} * 1024 * 1024;
/// The most bytes of Heroic's installed list read.
inline constexpr std::size_t heroic_file_limit = std::size_t{4} * 1024 * 1024;
/// How deep a Wine prefix is searched for the game's folder, below drive_c.
inline constexpr uint32_t prefix_search_depth = 4;
/// The most entries listed in one folder while searching a prefix.
inline constexpr std::size_t prefix_entries_limit = 4096;
/// The most folders find_candidates returns.
inline constexpr std::size_t most_candidates = 32;
/// The most library folders taken from one libraryfolders.vdf, and the most install paths
/// taken from one Heroic list.
inline constexpr std::size_t most_library_entries = 256;
/// The most folders looked into below one prefix's drive_c.
inline constexpr std::size_t prefix_folders_limit = 2048;
/// The deepest the key-values and JSON readers follow nested blocks; deeper text is a fault.
inline constexpr std::size_t most_nesting = 64;

/// Where a folder was found.
enum class Source : uint8_t {
    steam,   ///< a Steam library
    heroic,  ///< Heroic's installed list
    lutris,  ///< a Wine prefix under ~/Games
    bottles, ///< a Bottles bottle
};

/// A folder holding game_archive_name, and where it was found.
struct Candidate {
    std::filesystem::path folder{}; ///< the folder
    Source source{Source::steam};   ///< where it was found
    bool removable{};               ///< under a removable drive's mount (an SD card)
};

/// The places looked in.
struct SearchRoots {
    std::filesystem::path home{}; ///< the player's home folder
    /// Steam's own folders (native, ~/.steam/steam, Flatpak).
    std::vector<std::filesystem::path> steam_roots{};
    /// Heroic's config folders (native, Flatpak).
    std::vector<std::filesystem::path> heroic_configs{};
    /// Folders whose children are Wine prefixes (~/Games, Bottles').
    std::vector<std::filesystem::path> prefix_parents{};
    /// Where removable drives mount (/run/media).
    std::vector<std::filesystem::path> removable_mounts{};
    /// Which program keeps the prefixes of each of prefix_parents, by its place there: lutris or
    /// bottles. A parent past the end of this list is Lutris's.
    std::vector<Source> prefix_sources{};
};

/// Returns the usual places under a home folder: ~/.local/share/Steam, ~/.steam/steam,
/// ~/.var/app/com.valvesoftware.Steam/.local/share/Steam; ~/.config/heroic and
/// ~/.var/app/com.heroicgameslauncher.hgl/config/heroic; ~/Games (Lutris), then
/// ~/.local/share/bottles/bottles and ~/.var/app/com.usebottles.bottles/data/bottles/bottles
/// (Bottles); and /run/media.
///
/// @param home the home folder
/// @return the places
[[nodiscard]] SearchRoots search_roots_under(const std::filesystem::path& home);
/// Returns this machine's places: on Linux search_roots_under($HOME) with XDG_DATA_HOME and
/// XDG_CONFIG_HOME honoured; on every other system none.
///
/// @return the places; empty off Linux or without a home folder
[[nodiscard]] SearchRoots default_search_roots();
/// Returns the library folders a Steam libraryfolders.vdf (text key-values) names: each
/// "path" value, unescaped; malformed text gives those read before the fault.
///
/// Older files name each library as the value of a numbered key directly under the top
/// block; those are taken too. At most most_library_entries are returned.
///
/// @param text the file's text
/// @return the folders, in the order named
[[nodiscard]] std::vector<std::filesystem::path> library_folders(std::string_view text);
/// Returns the "installdir" of an appmanifest_<app>.acf whose "appid" is `app`.
///
/// @param text the file's text
/// @param app the app number
/// @return the folder's name under steamapps/common, or none
[[nodiscard]] std::optional<std::string> manifest_install_dir(std::string_view text, uint32_t app);
/// Returns every "install_path" of Heroic's gog_store/installed.json "installed" array.
///
/// At most most_library_entries are returned.
///
/// @param text the file's text
/// @return the folders, in the order listed; malformed text gives those read before the fault
[[nodiscard]] std::vector<std::filesystem::path> heroic_install_paths(std::string_view text);
/// Returns every folder holding game_archive_name found from the roots: Steam libraries (every
/// path read from libraryfolders.vdf, never guessed), then Heroic, then Lutris prefixes under
/// ~/Games, then Bottles; each folder once (by its canonical path), at most most_candidates.
/// Reads only within the bounds above; unreadable places are skipped.
///
/// A prefix is searched below its drive_c to prefix_search_depth, leaving out drive_c's
/// windows and users folders, every folder named by folder_cannot_hold_game, and every
/// link to a folder, and not below a folder that holds the archive.
///
/// @param roots the places looked in
/// @return the folders found, in that order
[[nodiscard]] std::vector<Candidate> find_candidates(const SearchRoots& roots);
/// Returns words for where a folder was found: "your Steam library", "your Steam library on the
/// SD card", "Heroic", "Lutris", "Bottles".
///
/// @param source where it was found
/// @param removable it lies on a removable drive
/// @return the words, untranslated
[[nodiscard]] std::string_view source_words(Source source, bool removable) noexcept;

/// Tells whether a folder holds game_archive_name, matched without regard to case, among the
/// first prefix_entries_limit entries it lists.
///
/// @param folder the folder
/// @return true when it holds the archive as a file; false when it does not or cannot be read
[[nodiscard]] bool holds_game_archive(const std::filesystem::path& folder);

/// Tells whether a folder's name is one that cannot hold the game, so a search
/// does not walk into it. ASCII letters are compared regardless of case.
/// Applications, Downloads, Documents, Desktop, a game's own folders and a
/// wrapper whose name ends in .app are not named here.
///
/// @param name the folder's own name, not a path
/// @return true when a search should leave the folder unopened
[[nodiscard]] bool folder_cannot_hold_game(std::string_view name) noexcept;

/// Returns the player's home folder: HOME, or USERPROFILE on Windows.
///
/// @return the folder; empty when the environment names none
[[nodiscard]] std::filesystem::path home_folder();

/// Returns the folders where drives other than the system's appear, for a folder browser: on
/// Linux each folder under /run/media/<user> and /media/<user> (and each folder directly under
/// /run/media that is not a user's); on macOS each volume under /Volumes but the start-up disk;
/// on Windows each drive's root from D: to Z:; elsewhere none. Each is listed once, in name
/// order, at most most_candidates.
///
/// @return the folders
[[nodiscard]] std::vector<std::filesystem::path> drive_folders();

/// Which band of the locator's sidebar a place belongs to. Found installs are added by the
/// screen, not by locator_places.
enum class LocatorGroup : uint8_t {
    places,     ///< Home, Desktop, Documents, Downloads, Applications, and a Deck's cards
    volumes,    ///< disks and other drives
    deck_games, ///< Steam, Heroic, Lutris and Bottles
};

/// One place the locator can open, before it is checked against the disk.
struct LocatorPlace {
    std::filesystem::path folder{};           ///< the folder
    std::string label{};                      ///< "Downloads", "Steam · Proton"
    LocatorGroup group{LocatorGroup::places}; ///< which band
};

/// The folders a machine might offer the locator. An empty path is left out.
struct LocatorFacts {
    std::filesystem::path home{};         ///< the home folder
    std::filesystem::path desktop{};      ///< the desktop
    std::filesystem::path documents{};    ///< documents
    std::filesystem::path downloads{};    ///< downloads
    std::filesystem::path applications{}; ///< Applications, or Program Files
    bool program_files{};                 ///< applications is Program Files
    /// Disks and other drives. On a Steam Deck these are the microSD cards instead, and
    /// `sd_cards` is used.
    std::vector<std::filesystem::path> volumes{};
    /// Removable cards on a Steam Deck.
    std::vector<std::filesystem::path> sd_cards{};
    std::filesystem::path steam{};   ///< a Steam Proton prefix
    std::filesystem::path heroic{};  ///< Heroic's games folder
    std::filesystem::path lutris{};  ///< Lutris's games folder
    std::filesystem::path bottles{}; ///< Bottles' bottles folder
    bool deck{};                     ///< Steam Deck: cards under places, no volumes
};

/// Returns the places a locator shows, in sidebar order, leaving out every empty path.
///
/// Home, Desktop, Documents, Downloads and Applications come first. On a Steam Deck the cards
/// follow them, labelled "microSD card". Elsewhere the volumes follow, each labelled with its
/// last name, or "System" for the root folder. Steam, Heroic, Lutris and Bottles come last.
///
/// @param facts the folders; empty ones are skipped
/// @return the places
[[nodiscard]] std::vector<LocatorPlace> locator_places(const LocatorFacts& facts);

/// Gathers the places that exist on this machine. A path that is not a folder is left empty,
/// so locator_places leaves it out. Nothing is created.
///
/// @param deck this is a Steam Deck, so removable drives are cards and volumes are left out
/// @return the facts
[[nodiscard]] LocatorFacts locator_place_facts(bool deck);

/// Writes a folder the way a player can share it. A folder under `home` is `~` or `~/…`,
/// so a screenshot does not name whose computer it was. Any other folder is written as it is.
///
/// @param home the player's home folder; empty leaves `folder` unchanged
/// @param folder the folder
/// @return the path to show
[[nodiscard]] std::string
path_under_home(const std::filesystem::path& home, const std::filesystem::path& folder);

/// What kind of place a setup source is. A loose folder is not offered.
enum class SetupPlace : uint8_t {
    installation, ///< a folder that is a game installation
    installer,    ///< an installer file
    loose,        ///< a folder of loose files
};

/// The facts the setup ranks a place by. The caller fills them from what the place holds.
struct SetupSource {
    std::string token{};                 ///< the caller's name for this place
    SetupPlace place{SetupPlace::loose}; ///< which kind of place
    int bundle_count{};                  ///< how many built-in bundles it holds
    /// 2 when it holds the base game, 1 when it holds only the demo, 0 otherwise.
    int base_rank{};
    int expansion_count{}; ///< how many of those bundles are expansions
};

/// Tells whether a folder's name is one a player keeps loose files in: Downloads, Desktop or
/// Documents, ASCII letters regardless of case.
///
/// @param name the folder's own name, not its path
/// @return true for those three names
[[nodiscard]] bool drop_folder_name(std::string_view name) noexcept;

/// Tells whether a folder is offered as a game installation.
///
/// The home folder, a drop folder, a folder with no game archive, and a folder that also holds
/// an installer file are not. The installer files are listed on their own.
///
/// @param folder_name the folder's own name
/// @param holds_archive it holds the game's archive
/// @param holds_installer_file it holds an installer file
/// @param home_itself it is the home folder
/// @return true when the folder itself is a row
[[nodiscard]] bool offer_as_installation(
    std::string_view folder_name, bool holds_archive, bool holds_installer_file, bool home_itself
) noexcept;

/// What a folder contributes to the setup's list.
struct SetupFolderOffer {
    bool folder_offered{};                           ///< the folder itself is an installation
    std::vector<std::filesystem::path> installers{}; ///< installer files directly inside it
};

/// Lists the installer files directly in a folder, and whether the folder itself is an
/// installation. `is_installer` says which regular files are installers; null treats none as
/// one. At most prefix_entries_limit entries are read. Subfolders are not opened.
///
/// @param folder the folder
/// @param is_installer which files are installers; null when the caller has none
/// @param context passed to `is_installer`
/// @return the offer; a folder that cannot be read offers nothing
[[nodiscard]] SetupFolderOffer offer_from_folder(
    const std::filesystem::path& folder,
    bool (*is_installer)(const std::filesystem::path& file, uint64_t size, void* context),
    void* context
);

/// Drops loose places and orders the rest. More bundles come first, then a higher base rank,
/// then more expansions, then an installer or an installation ahead of a loose place. Equal
/// places keep the order they were given. The first place is the one to preselect.
///
/// @param found the places, loose ones included
/// @return the places to list, fullest first
[[nodiscard]] std::vector<SetupSource> rank_setup_sources(std::vector<SetupSource> found);

} // namespace oa::platform::game_installs
