// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The update question: whether an update asks the player first, and what it
// asks, before an update of a mod from a registry the player added that may
// stop saved games loading (its rules hash changes, or cannot be known). A
// built-in registry's update, and one that keeps the rules, never asks. The
// Library's UPDATE and UPDATE ALL share these functions, so the rule lives
// in one place (asks_before_update). Its words are in text.hpp.
#pragma once

#include "oa/ui/kit/components_more.hpp"
#include "oa/ui/library/library.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::library {

/// The update question's NOT NOW button's name to automation, which leaves
/// the update. It is the first button and the one the keys mark at first.
inline constexpr std::string_view not_now_button_id = "not-now";
/// The update question's UPDATE button's name to automation, which starts
/// the update. It has the primary look.
inline constexpr std::string_view update_button_id = "update";

/// How many names UPDATE ALL's question lists before it counts the rest.
inline constexpr std::size_t named_updates = 3;

/// What the update question asks, in the language shown.
struct UpdateQuestion {
    std::string title{}; ///< "Update {name}", or "Update all"
    /// "{name} 4.8 · revision 3 → 4", "{name} 4.8 → 4.9", or "{name} 4.8"
    /// when the version stays and a side names no revision; empty for UPDATE ALL's
    std::string heading{};
    std::string body{};        ///< what the update does to saved games, and ROLL BACK
    std::vector<Fact> facts{}; ///< Rules and Download; none for UPDATE ALL's
};

/// Tells whether an update asks the player first: only a mod's, only from a
/// registry the player added (not Entry::built_in), and only when it changes
/// the rules saved games are tied to or that cannot be known
/// (UpdateNote::Rules::changes or unknown). This is the one place the rule
/// lives.
///
/// @param entry the entry the update is for
/// @param update its update
/// @return true when the update question is asked before it starts
[[nodiscard]] bool asks_before_update(const Entry& entry, const UpdateNote& update) noexcept;

/// Writes the question asked before one update.
///
/// @param entry the entry the update is for, with the package installed
/// @param update its update
/// @param playing the mod is the one being played, so the update finishes
///     when the player leaves it
/// @param text how texts are looked up
/// @return the question; nothing when asks_before_update is false
[[nodiscard]] std::optional<UpdateQuestion> update_question(
    const Entry& entry, const UpdateNote& update, bool playing, const TextHooks& text = {}
);

/// Writes the one question UPDATE ALL asks, over the updates of its entries
/// for which asks_before_update is true: how many change the game's rules,
/// and up to named_updates of their names, then how many more.
///
/// @param entries UPDATE ALL's entries, in list order; one without an update
///     is passed over
/// @param text how texts are looked up
/// @return the question; nothing when none of the updates asks
[[nodiscard]] std::optional<UpdateQuestion>
update_all_question(std::span<const Entry> entries, const TextHooks& text = {});

/// Makes the kit question that asks an update question: its title, the
/// heading, the body and each fact ("Rules: 9c1f 4e0b → 51d2 07aa") as
/// paragraphs, and NOT NOW then UPDATE. NOT NOW is marked at first, and
/// Escape, B and N answer it; UPDATE has the primary look, and Y answers it.
/// Its word is the kit's default, so automation names its buttons
/// prompt.not-now and prompt.update.
///
/// @param asked the question
/// @param text how texts are looked up
/// @return the kit question
[[nodiscard]] oa::ui::kit::Question
update_prompt(const UpdateQuestion& asked, const TextHooks& text = {});

} // namespace oa::ui::library
