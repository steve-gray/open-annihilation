// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The player's choice of base game and expansions. Turning one on or off
// follows the built-in bundles, and the result is what the next mount plays.
// The preferences and the one-run option are stored by the caller.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::content_bundles {

/// The base game a player with no stored choice plays.
inline constexpr std::string_view default_base_game_id = "totala";

/// Preference value set when a mod was cleared because an expansion it needs
/// went off. The sentence the player reads is chosen with that note.
inline constexpr std::string_view required_expansion_note = "required-expansion";

/// A file seen in a folder, by the path it was found under and its size.
struct SourceFile {
    /// Path or file name, matched as match_file matches a source.
    std::string_view name{};
    /// Size in bytes.
    uint64_t size{};
};

/// The base game and the expansions that are on.
struct ContentChoice {
    /// A base game id. Empty when `error` is set.
    std::string base_game{};
    /// Expansion ids that are on, in the built-in order. The base game is not
    /// among them.
    std::vector<std::string> expansions{};
    /// The base-game preference was absent or named no base game, so this is
    /// the default: totala and every copied expansion of it that can be on.
    bool migration{};
    /// `--content` named this choice for the run. It is not a stored choice,
    /// and it cannot be changed during the run.
    bool command_line{};
    /// Why `--content` was refused. Empty when the choice is usable.
    std::string error{};
};

/// The preference text the choice is read from. An absent optional is a
/// missing key. An empty expansions value means none are on.
struct StoredContent {
    /// `open-annihilation.base-game`, when the key is present.
    std::optional<std::string_view> base_game{};
    /// `open-annihilation.expansions.<game>` for that base game, when the key
    /// is present.
    std::optional<std::string_view> expansions{};
};

/// Why a change was refused.
enum class ContentRefusal : uint8_t {
    none,         ///< the choice was kept because it already said this, or the change was made
    in_match,     ///< a match is running
    command_line, ///< `--content` named the choice for this run
    unknown,      ///< the id is not a bundle
    not_copied,   ///< the expansion, or one it needs, is not copied
    not_for_game, ///< the id is not an expansion of this base game
};

/// Why the remembered mod was cleared.
enum class ModClearReason : uint8_t {
    none,                   ///< the mod stays
    base_game_chosen,       ///< choosing a base game clears the mod
    required_expansion_off, ///< an expansion the mod needs went off; a note is owed
};

/// What a change did.
struct ContentChangeResult {
    /// The choice after the change, or the choice that was given when it was refused.
    ContentChoice choice{};
    /// The stored choice would change. A refusal leaves this false.
    bool applied{};
    /// Why it was refused. `none` when it was applied or the choice already matched.
    ContentRefusal refusal{};
    /// Set when `applied` cleared the remembered mod.
    ModClearReason mod_cleared{};
};

/// Returns the bundles whose every file is among `files`.
///
/// A file matches by match_file: the path or its last name, without regard
/// to ASCII case, and the size. The ids are in the built-in order and address
/// the table.
///
/// @param files the files found, by path and size
/// @return the bundles that are wholly present
[[nodiscard]] std::vector<std::string_view> present_bundles(std::span<const SourceFile> files);

/// Reads `--content GAME[:EXPANSION,...]`.
///
/// `GAME` is a base game. Each expansion has to extend it. An expansion the
/// list needs is turned on with it; an expansion that was only waiting on
/// one of them is not. The expansions are in the built-in order. A list with
/// a space, an empty name or a trailing comma is refused.
///
/// @param text the option's value
/// @return the choice, with `command_line` set; `error` names a refusal
[[nodiscard]] ContentChoice parse_content_argument(std::string_view text);

/// Resolves the choice for a run.
///
/// A non-empty `command_line` is `--content` and replaces the stored choice.
/// With no usable base game stored, the choice is totala and every copied
/// expansion of it that can be on (`migration`). An expansions value that is
/// absent means the same for the stored base game; one that is present is
/// that list, with each expansion's needs added when they are copied, and
/// an expansion dropped when a need is not copied.
///
/// @param stored the preference text
/// @param copied the bundle ids that are copied
/// @param command_line the `--content` value; empty when the run names none
/// @return the choice the next mount plays
[[nodiscard]] ContentChoice resolve_content_choice(
    const StoredContent& stored,
    std::span<const std::string_view> copied,
    std::string_view command_line
);

/// Joins expansion ids with commas, in the order given.
///
/// @param expansions the ids
/// @return `campaign,music`, or empty when there are none
[[nodiscard]] std::string expansions_text(std::span<const std::string> expansions);

/// Returns the choice after turning one expansion on or off.
///
/// Off turns off every expansion that needs it. On turns on what it needs
/// and every copied expansion that was waiting only on it. Either is refused
/// during a match, when `--content` named the run, or when the id is not an
/// expansion of this base game. On is also refused when the expansion or a
/// need of it is not copied. A mod whose required expansion goes off is
/// cleared, and `mod_cleared` says a note is owed.
///
/// @param choice the choice now
/// @param bundle_id the expansion
/// @param on true to turn it on, false to turn it off
/// @param copied the bundle ids that are copied
/// @param in_match true while a match is running
/// @param mod_active true while a mod is remembered
/// @param mod_required_expansions the expansions that mod needs
/// @return the choice afterwards, and whether the mod was cleared
[[nodiscard]] ContentChangeResult turn_content_expansion(
    const ContentChoice& choice,
    std::string_view bundle_id,
    bool on,
    std::span<const std::string_view> copied,
    bool in_match,
    bool mod_active,
    std::span<const std::string_view> mod_required_expansions
);

/// Returns the choice after choosing a base game.
///
/// Every copied expansion of that game that can be on is turned on, and a
/// remembered mod is cleared. Refused during a match, when `--content` named
/// the run, or when the id is not a base game.
///
/// @param choice the choice now
/// @param bundle_id the base game
/// @param copied the bundle ids that are copied
/// @param in_match true while a match is running
/// @param mod_active true while a mod is remembered
/// @return the choice afterwards, and whether the mod was cleared
[[nodiscard]] ContentChangeResult choose_content_base_game(
    const ContentChoice& choice,
    std::string_view bundle_id,
    std::span<const std::string_view> copied,
    bool in_match,
    bool mod_active
);

} // namespace oa::data::content_bundles
