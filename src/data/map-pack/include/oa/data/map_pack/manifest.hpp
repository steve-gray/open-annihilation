// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A map pack's manifest, oamap.yaml. The packer, the installer and the game
// read one description of the pack and of each map it lists.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::map_pack {

/// The manifest format this build reads: the manifest's oamap value.
inline constexpr int64_t format_version = 1;

/// The name of a pack's manifest.
inline constexpr std::string_view manifest_file = "oamap.yaml";

/// The most maps one pack lists.
inline constexpr std::size_t most_maps = 256;

/// The most paths one map's file list holds.
inline constexpr std::size_t most_files_per_map = 1024;

/// The most characters of a map's title, the manifest's name key.
inline constexpr std::size_t most_title_characters = 64;

/// The most bytes of a map's description.
///
/// A campaign's short texts are kCampaignShortTextBytes, and the description
/// leaves the last byte of that field for the terminating NUL.
inline constexpr std::size_t most_description_bytes = 127;

/// The most players a map lists.
inline constexpr int32_t most_players = 10;

/// The most cells along one side of a map.
inline constexpr int32_t most_map_side = 128;

/// One map in the pack's index.
///
/// `title` is the manifest's name key, the words players see. `size` is
/// `<width>x<height>` in map cells. `files` is everything that map loads
/// from the pack.
struct MapEntry {
    std::string stem{};               ///< the map's file name without its extension
    std::string title{};              ///< the name players see
    std::string description{};        ///< one line about the map; empty when unwritten
    std::string size{};               ///< the map's width and height, as `<width>x<height>`
    std::string preview{};            ///< the picker's thumbnail; empty when the map has none
    int32_t players{};                ///< how many players the map is for
    std::vector<std::string> files{}; ///< the paths the map loads from the pack
};

/// Who wrote the pack.
struct Author {
    std::string name{};  ///< the author, or "unknown" when nobody is known
    std::string email{}; ///< an e-mail address; empty when unwritten
};

/// Which build of the pack this file is.
struct Packaging {
    int64_t revision{};     ///< the repack of this version, from 1
    std::string date{};     ///< the day it was packed, YYYY-MM-DD
    std::string packager{}; ///< what packed it
};

/// A pack's manifest: the header an author writes and the index the packer writes.
struct Manifest {
    int64_t format{};                ///< oamap: the format, format_version
    std::string id{};                ///< the pack's id, and the suffix of every map name
    std::string name{};              ///< the pack's name, as a player reads it
    std::string version{};           ///< the author's version text, never ordered
    std::string description{};       ///< one line about the pack; empty when unwritten
    std::string homepage{};          ///< an http or https address; empty when unwritten
    Author author{};                 ///< who wrote the pack
    Packaging packaging{};           ///< which build of the pack this file is
    std::vector<std::string> tags{}; ///< how the pack is filed, in the order written
    std::string requires_base{};     ///< the game data the pack builds on; empty when unwritten
    /// The engine requirement, as written. Empty when the manifest has none.
    /// Reading the manifest does not ask whether this build meets it.
    std::string requires_engine{};
    std::vector<MapEntry> maps{}; ///< the maps, in the order written
};

/// Whether the manifest is a source an author is writing or a packed index.
enum class ManifestUse : uint8_t {
    source,  ///< an entry may hold only its stem; anything else is read and later rewritten
    package, ///< every map entry is complete, including its files
};

/// One broken rule, where it is written.
struct Problem {
    uint32_t line{};       ///< the 1-based line of the value
    uint32_t column{};     ///< the 1-based byte column of the value
    std::string key{};     ///< the path of the value, such as maps[0].stem
    std::string message{}; ///< what is wrong
};

/// Reads a manifest.
///
/// Collects every broken rule. A document that is not the strict YAML of a
/// mod profile is one problem. `homepage`, `tags` and `requires.engine`
/// follow the shared package-key rules. An engine requirement this build
/// does not meet is still read.
///
/// @param bytes the manifest's bytes
/// @param use whether a map entry must be a complete packed index
/// @param[out] manifest the manifest; left unchanged when any rule is broken
/// @param[in,out] problems one entry appended for each broken rule
/// @return true when the manifest was read and `problems` gained nothing
[[nodiscard]] bool read_manifest(
    std::span<const uint8_t> bytes,
    ManifestUse use,
    Manifest& manifest,
    std::vector<Problem>& problems
);

/// Writes a manifest as strict YAML.
///
/// Keys are written in a fixed order, with two-space indentation. Every
/// string is double-quoted. A description or a preview that is empty is left
/// out, and so are an empty homepage, an empty tag list and an empty engine
/// requirement. One file path is written per line.
///
/// @param manifest the manifest
/// @return its text, ending in a line break
[[nodiscard]] std::string write_manifest(const Manifest& manifest);

/// Describes a problem the way a manifest's diagnostics read.
///
/// @param problem the problem
/// @return `oamap.yaml:<line>:<column>: <key>: <message>`
[[nodiscard]] std::string describe(const Problem& problem);

} // namespace oa::data::map_pack
