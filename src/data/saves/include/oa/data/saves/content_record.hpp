// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The base game and the rule-changing expansions a save, a recording or a
// film was made with. Music is not among them. A file from before this note
// has none, and loads as it does today.
#pragma once

#include "oa/data/content_bundles/content_choice.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::saves {

/// Why TURN ON AND LOAD cannot change the choice.
enum class ContentBlock : uint8_t {
    none,         ///< the choice can change
    not_copied,   ///< the save's base game, or an expansion it needs, is not copied
    command_line, ///< this run's content was named for the run
};

/// What a save, a recording or a film notes about the content it was made with.
struct ContentRecord {
    /// The note was present. A file from before this note leaves it false.
    bool present{};
    /// The base game's id. Empty when `present` is false.
    std::string base_game{};
    /// Rule-changing expansions that were on, in the built-in order. Music is
    /// never among them.
    std::vector<std::string> expansions{};
    /// The mod's name as the player saw it. Empty for no mod.
    std::string mod{};
};

/// What loading a noted file asks, against the choice this run plays.
struct ContentGap {
    /// The note names a base game or a rule-changing expansion this run does
    /// not have on. A missing note, and a note this run already matches, leave
    /// it false.
    bool needed{};
    /// TURN ON AND LOAD can store `next`.
    bool can_switch{};
    /// Why it cannot, when `needed` is set and `can_switch` is false.
    ContentBlock block{ContentBlock::none};
    /// The base game in the note is not the one this run plays.
    bool base_differs{};
    /// The names the sentence names: the other base game, or the expansions
    /// that are off, in English.
    std::string focus{};
    /// The choice TURN ON AND LOAD stores. Expansions already on stay on.
    content_bundles::ContentChoice next{};
    /// Choosing the note's base game clears the remembered mod.
    bool clears_mod{};
};

/// The note beside a save or a recording: the file's name with this suffix.
inline constexpr std::string_view content_note_suffix = ".oa-content";

/// The note in a film's folder.
inline constexpr std::string_view film_content_name = "oa-content.txt";

/// Preference that names the save the next start loads, after TURN ON AND LOAD.
inline constexpr std::string_view pending_load_key = "open-annihilation.pending-load";

/// Preference that names the recording the next start plays.
inline constexpr std::string_view pending_demo_key = "open-annihilation.pending-demo";

/// Builds the note for a choice and the mod name the player saw.
///
/// Music is left out. Expansions are kept in the built-in order. An id that
/// is not a rule-changing expansion is left out.
///
/// @param base_game the base game's id
/// @param expansions the expansions that are on
/// @param mod the mod's name; empty for no mod
/// @return the note, present when `base_game` names a base game
[[nodiscard]] ContentRecord content_record_from(
    std::string_view base_game, std::span<const std::string> expansions, std::string_view mod
);

/// Writes the note as text: one `base-game` line, one `expansions` line and
/// one `mod` line.
///
/// @param record the note
/// @return the text, or empty when the note is not present
[[nodiscard]] std::string content_record_text(const ContentRecord& record);

/// Reads a note from the text a save, a recording or a film keeps beside it.
///
/// Text with no `base-game` line, including empty text, is a file from before
/// this note.
///
/// @param text the note's text
/// @return the note
[[nodiscard]] ContentRecord content_record_from_text(std::string_view text);

/// Compares a note with the choice this run plays.
///
/// A missing note needs nothing. Music does not count. An expansion the note
/// has that is already on does not count, and one this run has that the note
/// lacks stays on. `next` turns the missing ones on, with what they need,
/// and keeps every expansion already on that still extends the note's base
/// game.
///
/// @param saved the note; a missing one needs nothing
/// @param now the choice this run plays
/// @param copied the bundle ids that are copied
/// @return what loading asks
[[nodiscard]] ContentGap content_gap(
    const ContentRecord& saved,
    const content_bundles::ContentChoice& now,
    std::span<const std::string_view> copied
);

/// The English name a bundle shows. An unknown id is returned as it was given.
///
/// @param id the bundle id
/// @return the name
[[nodiscard]] std::string bundle_english_name(std::string_view id);

} // namespace oa::data::saves
