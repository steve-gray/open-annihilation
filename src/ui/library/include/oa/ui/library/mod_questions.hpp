// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the Library asks and tells about the mod the game plays, as kit
// questions a host shows over it: SWITCH OFF's Switch Mod question, the note
// that PLAY NOW could not switch to a mod, and ROLL BACK's Roll Back Mod
// question. Their words are in text.hpp.
#pragma once

#include "oa/ui/kit/components_more.hpp"
#include "oa/ui/library/library.hpp"

#include <string_view>

namespace oa::ui::library {

/// The questions' CANCEL's name to automation: the first button, the one
/// the keys mark at first, and the one Escape, B and N answer.
inline constexpr std::string_view cancel_button_id = "cancel";
/// The Switch Mod question's SWITCH's name to automation.
inline constexpr std::string_view switch_button_id = "switch";
/// The Roll Back Mod question's ROLL BACK's name to automation.
inline constexpr std::string_view roll_back_button_id = "roll-back";
/// The note's OK's name to automation.
inline constexpr std::string_view ok_button_id = "ok";

/// Makes SWITCH OFF's question: the Switch Mod question of No Mod, the game
/// played with no mod. Its title, what it asks of No Mod, and CANCEL then
/// SWITCH. CANCEL is marked at first; SWITCH has the primary look, and Y
/// answers it. Its word is the kit's default, so automation names its
/// buttons prompt.cancel and prompt.switch.
///
/// @param text how texts are looked up
/// @return the question
[[nodiscard]] oa::ui::kit::Question switch_off_question(const TextHooks& text = {});

/// Makes the note that PLAY NOW could not switch to a mod, under the Switch
/// Mod question's title: why, then OK (prompt.ok), which Enter, Escape and Y
/// answer.
///
/// @param reason why, in the language shown, as the host was told it
/// @param text how texts are looked up
/// @return the note, as a question of one button
[[nodiscard]] oa::ui::kit::Question
switch_refused_note(std::string_view reason, const TextHooks& text = {});

/// Makes ROLL BACK's question: the Roll Back Mod question. Its title, what
/// it asks of the mod, with the reload line for the mod the game plays, and
/// CANCEL then ROLL BACK (prompt.cancel and prompt.roll-back). CANCEL is
/// marked at first; Escape and N answer it, Y ROLL BACK.
///
/// @param title the mod's title
/// @param from the version the folder holds, such as "4.8 revision 3"
/// @param to the version its .backup keeps, named the same way
/// @param playing the folder is the mod the game plays
/// @param text how texts are looked up
/// @return the question
[[nodiscard]] oa::ui::kit::Question roll_back_question(
    std::string_view title,
    std::string_view from,
    std::string_view to,
    bool playing,
    const TextHooks& text = {}
);

} // namespace oa::ui::library
