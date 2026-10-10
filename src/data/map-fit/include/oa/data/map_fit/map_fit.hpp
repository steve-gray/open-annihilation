// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Whether one map may be played with a game and a mod. The map fits when it
// only adds files and feature names the game does not already have, and every
// feature and unit it places can be loaded.
#pragma once

#include "oa/data/defs/layout.hpp"
#include "oa/data/map_pack/manifest.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/formats/tdf.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::map_fit {

/// A rule a map must meet to be played with a game.
///
/// A later rule is added at the end, so these values stay put.
enum class Rule : uint8_t {
    isolated,  ///< the mounted layer is this map's files and no others
    adds_only, ///< the map adds no file the game already provides
    new_names, ///< the map defines no feature section the game already names
    complete,  ///< every feature and unit the map places can be loaded
};

/// Names the rule a failure broke, as a player reads it.
///
/// @param rule the rule
/// @return "Isolated", "Adds only", "New names" or "Complete"
[[nodiscard]] std::string_view rule_name(Rule rule) noexcept;

/// One rule a map broke, the path or name at fault, and why.
struct Failure {
    Rule rule{};           ///< the rule the map broke
    std::string subject{}; ///< the path or the name at fault
    std::string detail{};  ///< what it clashes with, or what is missing, and where
};

/// Every rule one map broke against one game.
struct Fit {
    std::vector<Failure> failures{}; ///< every broken rule, sorted by rule, subject and detail

    /// Tells whether the map broke no rule.
    ///
    /// @return true when failures is empty
    [[nodiscard]] bool fits() const noexcept { return failures.empty(); }
};

/// The feature, weapon and unit names a game provides, folded to lower-case ASCII.
///
/// A feature section's value is the resource of the first feature TDF that
/// defines it. A unit name is the stem of a unit file. `unreadable` lists the
/// game's TDFs that did not parse; they name nothing.
struct GameNames {
    std::map<std::string, std::string> feature_sections{}; ///< folded section, then its resource
    std::set<std::string> weapons{};                       ///< folded weapon section names
    std::set<std::string> units{};                         ///< folded unit-file stems
    std::vector<std::string> unreadable{};                 ///< game TDFs that did not parse
};

/// Collects the feature, weapon and unit names the game provides.
///
/// Reads only what the store provides from loose files and archives. The pack
/// layer is left out of every listing. Feature sections come from every
/// feature TDF in listing order, and the first file to name a section keeps
/// it. Weapon names are the top-level sections of the weapon TDFs. Unit names
/// are the stems of the unit files. A TDF that does not parse is listed in
/// `unreadable` and skipped. The caller keeps the result while the store's
/// mounts stay as they were.
///
/// @param store the game and the mod
/// @param layout the directories and the unit extension to read
/// @return the names, and the TDFs that did not parse
[[nodiscard]] GameNames
collect_game_names(const oa::AssetStore& store, const oa::data::defs::DataLayout& layout);

/// The files of one map, as the asset store would show them.
///
/// A folder of an installed map and the store's mounted pack layer are the
/// two sources. Either one answers `paths` and `read` for those paths only.
class MapFiles {
  public:

    /// Releases the map's files.
    virtual ~MapFiles() = default;

    /// Map files are not copied.
    MapFiles(const MapFiles&) = delete;

    /// Map files are not copied.
    MapFiles& operator=(const MapFiles&) = delete;

    /// Returns the paths the store would show for this map.
    ///
    /// @return the paths, in the map's order
    [[nodiscard]] virtual std::vector<std::string> paths() const = 0;

    /// Reads one of the map's files.
    ///
    /// A path the map does not list reads as nothing, and so does a listed
    /// path whose file cannot be read.
    ///
    /// @param path a resource path, matched ignoring ASCII case
    /// @return the bytes, or nothing when the map has no such file
    [[nodiscard]] virtual std::optional<std::vector<uint8_t>> read(std::string_view path) const = 0;

    /// Tells whether the files are the store's mounted pack layer.
    ///
    /// @return true when read takes the mounted layer
    [[nodiscard]] virtual bool from_layer() const noexcept = 0;

  protected:

    /// A derived class supplies the files.
    MapFiles() = default;
};

/// Reads one map's files from its installed folder.
///
/// A source path is resolved in the folder ignoring ASCII case. The paths
/// are the ones the store would show, including the map's suffixed OTA and
/// TNT. Whatever pack layer the store has mounted is ignored.
///
/// @param folder the map's folder
/// @param map the map's manifest entry
/// @param id the pack's id, the suffix of the map's name
/// @return the map's files; a path that is not in the folder reads as nothing
[[nodiscard]] std::unique_ptr<MapFiles> folder_map_files(
    const std::filesystem::path& folder,
    const oa::data::map_pack::MapEntry& map,
    std::string_view id
);

/// Reads the files of the store's mounted pack layer.
///
/// `store` must outlive the result. The paths are the layer's own paths.
/// `from_layer` is true even when no layer is mounted, and then no path reads.
///
/// @param store the store whose mounted layer is read
/// @return the mounted layer's files
[[nodiscard]] std::unique_ptr<MapFiles> mounted_map_files(const oa::AssetStore& store);

/// Lists the files a map shows, and the file in the pack each is read from.
///
/// The OTA and the TNT are shown at `maps/<stem>@<id>.ota` and `.tnt`, read
/// from `maps/<stem>.ota` and `.tnt`. Every other file is shown at its own
/// path. The comparison that finds the OTA and the TNT ignores ASCII case.
///
/// @param map the map's manifest entry
/// @param id the pack's id, the suffix of the map's name
/// @return the files, in the manifest's order
[[nodiscard]] std::vector<oa::PackLayerFile>
layer_files(const oa::data::map_pack::MapEntry& map, std::string_view id);

/// The features and units one map places, and where each name was written.
///
/// `features_from` is parallel to `features`: "the TNT" or "Schema 2's
/// features". `units_from` is parallel to `units`: "Schema 2's units". Names
/// keep the spelling they were written with.
struct MapUses {
    std::vector<std::string> features{};      ///< feature names, in the order placed
    std::vector<std::string> features_from{}; ///< where each feature name was written
    std::vector<std::string> units{};         ///< unit names, in the order placed
    std::vector<std::string> units_from{};    ///< where each unit name was written
};

/// Names the features and units one map places.
///
/// Feature names come from the TNT, then from every schema's features, Schema
/// 0 onward until the first missing schema number. A feature whose XPos or
/// ZPos is below zero is not placed. Unit names come from each of those
/// schemas' unit section, the layout's `map_units_section`, by Unitname.
/// Every schema up to the gap is read, whatever its type. On failure `uses`
/// is left unchanged and the names are not appended.
///
/// @param ota the map's OTA text
/// @param tnt the map's TNT bytes
/// @param layout the data layout, for the schema section that places units
/// @param[out] uses receives the names, appended; left unchanged on failure
/// @param[out] error receives the reason when a file cannot be read, if not null
/// @return true when both files were read
[[nodiscard]] bool uses_of(
    std::span<const uint8_t> ota,
    std::span<const uint8_t> tnt,
    const oa::data::defs::DataLayout& layout,
    MapUses& uses,
    std::string* error
);

/// What one feature section uses: a sprite, a model, a burn weapon and links.
///
/// A sprite's `gaf` is `anims/<filename>.gaf`. A model's `model` is
/// `objects3d/<object>.3do`. An empty string is a use the section does not
/// have. Names keep their spelling.
struct SectionReferences {
    std::string gaf{};         ///< the sprite's GAF, or empty when the section is not a sprite
    std::string model{};       ///< the model, or empty when the section is a sprite
    std::string burn_weapon{}; ///< the burn weapon's name, or empty when it names none
    std::vector<std::string> features{}; ///< linked section names, in link order
};

/// Names what one feature section uses.
///
/// An empty object is a sprite, and `filename` names `anims/<filename>.gaf`,
/// or nothing when `filename` is empty too. Otherwise `object` names
/// `objects3d/<object>.3do`. `burnweapon` names a weapon. `featuredead`,
/// `featurereclamate` and `featureburnt` name further sections, in that
/// order. An empty name is left out.
///
/// @param section the feature section
/// @return the sprite, the model, the burn weapon and the linked sections
[[nodiscard]] SectionReferences references_of(const oa::formats::tdf::Block& section);

/// Checks one map against the game and reports every rule it breaks.
///
/// The game is what `store` provides from loose files and archives. The pack
/// layer is never part of it. `game` is the caller's, collected for this
/// store; the check does not collect it again. A check through folder files
/// ignores whatever layer is mounted. Failures are sorted by rule, then
/// subject, then detail, and the same triple is reported once.
///
/// @param store the game and the mod
/// @param game the game's feature, weapon and unit names
/// @param files the map's own files
/// @param map the map's manifest entry
/// @param id the pack's id, the suffix of the map's name
/// @param layout the data layout `game` was collected with
/// @return every failure
[[nodiscard]] Fit check_map(
    const oa::AssetStore& store,
    const GameNames& game,
    const MapFiles& files,
    const oa::data::map_pack::MapEntry& map,
    std::string_view id,
    const oa::data::defs::DataLayout& layout
);

/// Describes one failure in a sentence a log can print.
///
/// @param failure the failure
/// @return the rule, the subject and the reason
[[nodiscard]] std::string describe(const Failure& failure);

} // namespace oa::data::map_fit
