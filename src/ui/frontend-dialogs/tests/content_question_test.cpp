// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The question shown when a save needs other content: the comparison, CANCEL,
// and TURN ON AND LOAD when the choice can change.

#include "oa/data/saves/content_record.hpp"
#include "oa/ui/frontend_dialogs/content_question.hpp"
#include "oa/test/check.hpp"

#include <string>

namespace {

using oa::data::saves::ContentBlock;
using oa::data::saves::ContentGap;
using oa::ui::frontend_dialogs::ContentColumn;
using oa::ui::frontend_dialogs::content_cancel_button_id;
using oa::ui::frontend_dialogs::content_switch_question;
using oa::ui::frontend_dialogs::turn_on_button_id;

ContentColumn column(std::string base, std::string expansions, std::string mod) {
    return ContentColumn{std::move(base), std::move(expansions), std::move(mod)};
}

void test_turn_on_and_load() {
    ContentGap gap;
    gap.needed = true;
    gap.can_switch = true;
    gap.focus = "Core Contingency";
    const auto question = content_switch_question(
        column("Total Annihilation 3.1c", "Campaign, Core Contingency", "No Mod"),
        column("Total Annihilation 3.1c", "Campaign, Battle Tactics", "No Mod"),
        "Krogoth Encounter",
        gap
    );
    OA_CHECK(question.title == "This save needs other content");
    OA_CHECK(question.paragraphs.size() == 6);
    OA_CHECK(
        question.paragraphs[0].text ==
        "\"Krogoth Encounter\" was saved with Core Contingency, which is off."
    );
    OA_CHECK(question.paragraphs[1].text == "THE SAVE / NOW");
    OA_CHECK(question.paragraphs[2].text.find("Total Annihilation 3.1c") != std::string::npos);
    OA_CHECK(question.paragraphs[3].text.find("Core Contingency") != std::string::npos);
    OA_CHECK(question.paragraphs[3].text.find("Battle Tactics") != std::string::npos);
    OA_CHECK(question.paragraphs[4].text.find("No Mod") != std::string::npos);
    OA_CHECK(
        question.paragraphs[5].text.find("Your other expansions stay as they are.") !=
        std::string::npos
    );
    OA_CHECK(question.buttons.size() == 2);
    OA_CHECK(question.buttons[0].id == content_cancel_button_id);
    OA_CHECK(question.buttons[1].id == turn_on_button_id);
    OA_CHECK(question.buttons[1].accent);
    OA_CHECK(question.cancel_button == 0);
    OA_CHECK(question.primary_button == 1);
}

void test_cannot_switch() {
    ContentGap gap;
    gap.needed = true;
    gap.block = ContentBlock::not_copied;
    gap.focus = "Core Contingency";
    const auto question = content_switch_question(
        column("Total Annihilation 3.1c", "Core Contingency", "No Mod"),
        column("Total Annihilation 3.1c", "Campaign", "No Mod"),
        "mission 3",
        gap
    );
    OA_CHECK(question.buttons.size() == 1);
    OA_CHECK(question.buttons[0].id == content_cancel_button_id);
    OA_CHECK(question.paragraphs.back().text.find("not copied") != std::string::npos);
}

} // namespace

int main() {
    test_turn_on_and_load();
    test_cannot_switch();
    return oa::test::check_exit_status();
}
