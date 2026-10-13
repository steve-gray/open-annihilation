// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The question a save, a recording or a film asks when it was made with
// other content: the comparison, and TURN ON AND LOAD when the choice can
// change.
#pragma once

#include "oa/data/saves/content_record.hpp"
#include "oa/ui/kit/components_more.hpp"

#include <string>
#include <string_view>

namespace oa::ui::frontend_dialogs {

/// TURN ON AND LOAD's name to automation.
inline constexpr std::string_view turn_on_button_id = "turn-on";

/// CANCEL's name to automation.
inline constexpr std::string_view content_cancel_button_id = "cancel";

/// The question's name to automation.
inline constexpr std::string_view content_question_word = "load-save";

/// One side of the comparison, in the language shown.
struct ContentColumn {
    /// The base game's name.
    std::string base_game{};
    /// The expansions' names, or None when there are none.
    std::string expansions{};
    /// The mod's name, or No Mod.
    std::string mod{};
};

/// Makes the question the comparison shows.
///
/// Its title, the sentence that names what is off or which base game the
/// file was made with, one line each for the base game, the expansions and
/// the mod, and the line that says what TURN ON AND LOAD does. CANCEL is
/// marked at first. TURN ON AND LOAD is shown when the choice can change,
/// and it has the primary look.
///
/// @param saved the file's column
/// @param now the column for this run
/// @param title the file's name, as the player knows it
/// @param gap what loading asks
/// @return the question
[[nodiscard]] kit::Question content_switch_question(
    const ContentColumn& saved,
    const ContentColumn& now,
    std::string_view title,
    const data::saves::ContentGap& gap
);

} // namespace oa::ui::frontend_dialogs
