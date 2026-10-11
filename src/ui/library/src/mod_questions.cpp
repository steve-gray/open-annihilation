// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The questions about the mod the game plays, written from the patterns in
// text.hpp.
#include "oa/ui/library/mod_questions.hpp"

#include "oa/ui/library/text.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace oa::ui::library {

oa::ui::kit::Question switch_off_question(const TextHooks& text) {
    oa::ui::kit::Question question;
    question.title = shown(text, words::switch_title);
    const std::string no_mod(shown(text, words::no_mod));
    question.paragraphs.push_back({filled(text, words::switch_ask, {{"title", no_mod}}), false});
    question.buttons.push_back(
        {std::string(shown(text, words::action_cancel)), false, std::string(cancel_button_id)}
    );
    question.buttons.push_back(
        {std::string(shown(text, words::action_switch)), true, std::string(switch_button_id)}
    );
    question.cancel_button = 0;
    question.primary_button = 1;
    question.marked = 0;
    return question;
}

oa::ui::kit::Question switch_refused_note(std::string_view reason, const TextHooks& text) {
    oa::ui::kit::Question question;
    question.title = shown(text, words::switch_title);
    question.paragraphs.push_back({std::string(reason), false});
    question.buttons.push_back(
        {std::string(shown(text, words::action_ok)), true, std::string(ok_button_id)}
    );
    question.cancel_button = 0;
    question.primary_button = 0;
    question.marked = 0;
    return question;
}

oa::ui::kit::Question roll_back_question(
    std::string_view title,
    std::string_view from,
    std::string_view to,
    bool playing,
    const TextHooks& text
) {
    oa::ui::kit::Question question;
    question.title = shown(text, words::roll_back_title);
    std::string asked =
        filled(text, words::roll_back_ask, {{"title", title}, {"from", from}, {"to", to}});
    if (playing)
        asked += " " + std::string(shown(text, words::roll_back_reload));
    question.paragraphs.push_back({std::move(asked), false});
    question.buttons.push_back(
        {std::string(shown(text, words::action_cancel)), false, std::string(cancel_button_id)}
    );
    question.buttons.push_back(
        {std::string(shown(text, words::action_roll_back)), true, std::string(roll_back_button_id)}
    );
    question.cancel_button = 0;
    question.primary_button = 1;
    question.marked = 0;
    return question;
}

} // namespace oa::ui::library
