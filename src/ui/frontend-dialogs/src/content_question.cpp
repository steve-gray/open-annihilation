// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_dialogs/content_question.hpp"

#include "oa/data/languages/interface_text.hpp"
#include "oa/ui/frontend_dialogs/interface_words.hpp"
#include "oa/ui/kit/layout.hpp"

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace oa::ui::frontend_dialogs {
namespace {

/// One place in a pattern.
struct Place {
    std::string_view name{};
    std::string_view value{};
};

/// Looks a word up and fills its {places}.
///
/// @param english the word, in English
/// @param places the places it names
/// @return the line in the language shown
std::string filled(std::string_view english, std::initializer_list<Place> places) {
    const std::string_view source = oa::data::languages::interface_text(english);
    std::string result;
    std::size_t at = 0;
    while (at < source.size()) {
        const std::size_t open = source.find('{', at);
        const std::size_t close = open == std::string_view::npos ? open : source.find('}', open);
        if (close == std::string_view::npos) {
            result.append(source.substr(at));
            break;
        }
        result.append(source.substr(at, open - at));
        const std::string_view name = source.substr(open + 1, close - open - 1);
        bool found = false;
        for (const Place& place : places) {
            if (place.name == name) {
                result.append(place.value);
                found = true;
                break;
            }
        }
        if (!found)
            result.append(source.substr(open, close - open + 1));
        at = close + 1;
    }
    return result;
}

/// One comparison line.
///
/// @param row the row's name
/// @param saved the file's value
/// @param now this run's value
/// @return the line
std::string row_line(std::string_view row, std::string_view saved, std::string_view now) {
    return filled("{row}: {saved} / {now}", {{"row", row}, {"saved", saved}, {"now", now}});
}

} // namespace

kit::Question content_switch_question(
    const ContentColumn& saved,
    const ContentColumn& now,
    std::string_view title,
    const data::saves::ContentGap& gap
) {
    kit::Question question;
    question.word = std::string(content_question_word);
    question.size_class = kit::SizeClass::regular;
    question.title =
        std::string(oa::data::languages::interface_text("This save needs other content"));
    const std::string_view sentence = gap.base_differs
                                          ? "\"{title}\" was saved with {name}."
                                          : "\"{title}\" was saved with {name}, which is off.";
    question.paragraphs.push_back(
        {filled(sentence, {{"title", title}, {"name", gap.focus}}), false}
    );
    question.paragraphs.push_back(
        {filled(
             "{save_heading} / {now_heading}",
             {{"save_heading", oa::data::languages::interface_text("THE SAVE")},
              {"now_heading", oa::data::languages::interface_text("NOW")}}
         ),
         false}
    );
    question.paragraphs.push_back(
        {row_line(oa::data::languages::interface_text("Base game"), saved.base_game, now.base_game),
         false}
    );
    question.paragraphs.push_back(
        {row_line(
             oa::data::languages::interface_text("Expansions"), saved.expansions, now.expansions
         ),
         false}
    );
    question.paragraphs.push_back(
        {row_line(oa::data::languages::interface_text("Mod"), saved.mod, now.mod), false}
    );
    std::string footnote;
    if (gap.block == data::saves::ContentBlock::not_copied) {
        footnote = filled(
            "{name} is not copied, so this save cannot be loaded with it.", {{"name", gap.focus}}
        );
    } else if (gap.block == data::saves::ContentBlock::command_line) {
        footnote = std::string(
            oa::data::languages::interface_text("This run names its content, so it cannot switch.")
        );
    } else if (gap.base_differs) {
        footnote = filled(
            "Switching to {name} restarts the game's files, then loads this save.",
            {{"name", gap.focus}}
        );
    } else {
        footnote = filled(
            "Turning {name} on restarts the game's files, then loads this save. Your other "
            "expansions stay as they are.",
            {{"name", gap.focus}}
        );
    }
    question.paragraphs.push_back({std::move(footnote), false});
    question.buttons.push_back(
        {std::string(oa::data::languages::interface_text("Cancel")),
         false,
         std::string(content_cancel_button_id)}
    );
    if (gap.can_switch) {
        question.buttons.push_back(
            {std::string(oa::data::languages::interface_text("Turn on and load")),
             true,
             std::string(turn_on_button_id)}
        );
        question.primary_button = 1;
    } else {
        question.primary_button = 0;
    }
    question.cancel_button = 0;
    question.marked = 0;
    return question;
}

} // namespace oa::ui::frontend_dialogs
