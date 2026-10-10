// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A pack map's name in the battle room and in a saved game: the map's stem,
// an @, and the pack's id. The pieces are checked here so every reader forms
// and splits the same name.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace oa::data::map_pack {

/// The character between a map's stem and its pack id.
///
/// An id and a stem never contain it, so a name splits at its last one.
inline constexpr char map_name_separator = '@';

/// The most bytes of a pack map's name, the stem and the id joined by
/// map_name_separator.
///
/// The battle room carries that name in PlayerSetupInfo::map_name, and a
/// saved game's summary keeps it in LoadSummary::mission. Both are C strings.
/// The summary's field is the shorter of the two, 64 bytes with the
/// terminating NUL, so the name itself is at most this many bytes. It fits
/// the battle room's field as well.
inline constexpr std::size_t most_map_name_bytes = 63;

/// The most bytes of a pack's id.
inline constexpr std::size_t most_id_bytes = 32;

/// The most bytes of a map's stem.
inline constexpr std::size_t most_stem_bytes = 40;

/// Tells whether a pack id is lower-case kebab-case of 1 to most_id_bytes.
///
/// Words of a-z and 0-9 joined by single hyphens, not starting or ending
/// with a hyphen. An id never contains map_name_separator.
///
/// @param text the id
/// @return true when it is such an id
[[nodiscard]] bool valid_pack_id(std::string_view text);

/// Tells whether a map stem is 1 to most_stem_bytes of the characters a
/// map's file name may use.
///
/// ASCII letters, digits, space, `_`, `-`, `.`, `'`, `(` and `)`. It does
/// not start or end with a space or a dot, and it never contains
/// map_name_separator.
///
/// @param text the stem
/// @return true when it is such a stem
[[nodiscard]] bool valid_stem(std::string_view text);

/// Joins a stem and a pack id into the map's name.
///
/// The result is `stem`, map_name_separator, then `id`, with nothing checked.
///
/// @param stem the map's stem
/// @param id the pack's id
/// @return the joined name
[[nodiscard]] std::string pack_map_name(std::string_view stem, std::string_view id);

/// A pack map's name split into the stem and the pack id it was built from.
///
/// The views refer to the text passed to split_pack_map_name.
struct PackMapName {
    std::string_view stem{}; ///< the map's stem, before the last separator
    std::string_view id{};   ///< the pack's id, after the last separator
};

/// Splits a pack map's name at its last map_name_separator.
///
/// Answers only when the stem is valid and the id is a valid pack id, so a
/// name with no separator, an empty side, or a side that could not have been
/// written does not split.
///
/// @param name the name, as the battle room or a saved game carries it
/// @return the stem and the id, or nothing when the name is not one
[[nodiscard]] std::optional<PackMapName> split_pack_map_name(std::string_view name);

/// Tells whether a map name is short enough to travel.
///
/// @param name the name, in bytes
/// @return true when it is at most most_map_name_bytes
[[nodiscard]] bool map_name_fits(std::string_view name);

} // namespace oa::data::map_pack
