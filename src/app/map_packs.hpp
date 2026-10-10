// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The map packs installed in the player's Maps folder. Each pack's maps are
// read from its manifest once and kept until the pack's SHA-256 changes.
// Nothing here opens a map's OTA or TNT: a terrain's size is the file's size.
#pragma once

#include "oa/data/map_fit/map_fit.hpp"
#include "oa/data/map_pack/manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {

/// One map of an installed map pack, as its manifest lists it.
struct PackMap {
    std::string name{};               ///< `<stem>@<id>`, the name a picker lists
    std::string id{};                 ///< the pack's id
    std::string stem{};               ///< the map's file name without its extension
    std::string title{};              ///< the name players see
    std::string description{};        ///< one line about the map; empty when unwritten
    std::string size{};               ///< the map's width and height, as `<width>x<height>`
    std::string pack_name{};          ///< the pack's name
    std::string pack_version{};       ///< the pack's version
    std::string sha256{};             ///< the pack's key, 64 lower-case hex digits
    std::string registry{};           ///< the catalogue's registry; empty otherwise
    int32_t players{};                ///< how many players the map is for
    int64_t release{};                ///< the catalogue release; 0 otherwise
    uint64_t terrain_bytes{};         ///< the size of the map's TNT file; 0 when it is missing
    std::filesystem::path folder{};   ///< the pack's folder
    std::filesystem::path preview{};  ///< the preview file; empty when the map has none
    std::vector<std::string> files{}; ///< the paths the map loads from the pack
};

/// The map packs in a Maps folder.
///
/// `refresh` reads the folder. A pack's manifest is parsed only when the pack
/// is new or its key changed. The key is the SHA-256 in the pack's origin
/// record, or the SHA-256 of its oamap.yaml when it has no record.
class MapPacks {
  public:

    /// Remembers a Maps folder. Nothing is read until `refresh`.
    ///
    /// @param maps_folder the Maps folder; it need not exist
    explicit MapPacks(std::filesystem::path maps_folder);

    /// Reads Maps again, parsing a manifest only when its pack's key changed.
    ///
    /// A folder whose name starts with '.' is skipped, and so is anything that
    /// is not a folder. A pack whose manifest cannot be read is left out of
    /// `maps` and named by `problems`. A missing Maps folder lists nothing.
    void refresh();

    /// Returns the maps of the last refresh, sorted by name.
    ///
    /// The first call reads the folder.
    ///
    /// @return the maps
    [[nodiscard]] std::span<const PackMap> maps() const;

    /// Returns the map of an exact name.
    ///
    /// The first call reads the folder.
    ///
    /// @param name `<stem>@<id>`
    /// @return the map; null when none has that name
    [[nodiscard]] const PackMap* find(std::string_view name) const;

    /// Returns why packs were left out of the last refresh, one line each.
    ///
    /// The first call reads the folder.
    ///
    /// @return the lines; empty when every pack was read
    [[nodiscard]] std::vector<std::string> problems() const;

    /// Counts each manifest a refresh parses, for a test.
    ///
    /// Hashing a manifest to form a key is not counted. A null function
    /// counts nothing.
    ///
    /// @param watch called with `context` once per manifest parsed
    /// @param context passed to `watch`
    void set_manifest_watch(void (*watch)(void*), void* context);

  private:

    /// Reads the folder when nothing has yet.
    void ensure() const;

    std::filesystem::path maps_folder_{};
    std::vector<PackMap> maps_{};
    std::vector<std::string> problems_{};
    bool ready_{};
    void (*watch_)(void*){};
    void* watch_context_{};

    /// One pack's parsed maps, kept while its key is unchanged.
    struct Cached {
        std::string key{};
        std::vector<PackMap> maps{};
    };

    std::vector<std::pair<std::string, Cached>> cache_{};
};

/// Checks one map in a folder against the base game, with no mod.
///
/// The base store is the game folder's own archives. The map's files are
/// mounted from the folder, and the check uses the base layout. A store that
/// cannot be built is a failure, so the install is refused.
///
/// @param game_dir the base game's folder
/// @param folder the map pack's folder, staged or installed
/// @param map the map's manifest entry
/// @param id the pack's id
/// @return every rule the map broke; a failure when the base game cannot be read
[[nodiscard]] oa::data::map_fit::Fit check_base_game_fit(
    const std::filesystem::path& game_dir,
    const std::filesystem::path& folder,
    const oa::data::map_pack::MapEntry& map,
    std::string_view id
);

} // namespace oa::app
