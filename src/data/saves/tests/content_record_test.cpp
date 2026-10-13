// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The content note: music is left out, a file from before the note needs
// nothing, and a note that names an expansion which is off asks to turn it
// on without turning the others off.

#include "oa/data/content_bundles/content_choice.hpp"
#include "oa/data/saves/content_record.hpp"
#include "oa/test/check.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace {

using oa::data::content_bundles::ContentChoice;
using oa::data::saves::ContentBlock;
using oa::data::saves::ContentGap;
using oa::data::saves::ContentRecord;
using oa::data::saves::content_gap;
using oa::data::saves::content_record_from;
using oa::data::saves::content_record_from_text;
using oa::data::saves::content_record_text;

const std::string_view kCopied[]{
    "totala",
    "totala-demo",
    "campaign",
    "core-contingency",
    "battle-tactics",
    "music",
};

ContentChoice choice_of(std::string base, std::vector<std::string> expansions) {
    ContentChoice choice;
    choice.base_game = std::move(base);
    choice.expansions = std::move(expansions);
    return choice;
}

bool has_expansion(const ContentChoice& choice, std::string_view id) {
    for (const std::string& one : choice.expansions) {
        if (one == id)
            return true;
    }
    return false;
}

void test_record_drops_music_and_orders_expansions() {
    const std::string on[]{"battle-tactics", "music", "campaign", "totala"};
    const ContentRecord record = content_record_from("totala", on, "Ridge");
    OA_CHECK(record.present);
    OA_CHECK(record.base_game == "totala");
    OA_CHECK(record.mod == "Ridge");
    OA_CHECK(record.expansions.size() == 2);
    OA_CHECK(record.expansions[0] == "campaign");
    OA_CHECK(record.expansions[1] == "battle-tactics");
    OA_CHECK(!content_record_from("music", on, {}).present);
}

void test_text_round_trip_and_old_file() {
    const std::string on[]{"core-contingency", "campaign"};
    const ContentRecord record = content_record_from("totala", on, "");
    const ContentRecord again = content_record_from_text(content_record_text(record));
    OA_CHECK(again.present);
    OA_CHECK(again.base_game == "totala");
    OA_CHECK(again.mod.empty());
    OA_CHECK(again.expansions.size() == 2);
    OA_CHECK(again.expansions[0] == "campaign");
    OA_CHECK(again.expansions[1] == "core-contingency");

    const ContentRecord old = content_record_from_text("");
    OA_CHECK(!old.present);
    const ContentGap untouched = content_gap(old, choice_of("totala", {"campaign"}), kCopied);
    OA_CHECK(!untouched.needed);

    const ContentRecord nameless = content_record_from_text("mod: Ridge\n");
    OA_CHECK(!nameless.present);
    OA_CHECK(!content_gap(nameless, choice_of("totala", {}), kCopied).needed);
}

void test_matching_and_extra_now_need_nothing() {
    const std::string on[]{"campaign", "core-contingency"};
    const ContentRecord saved = content_record_from("totala", on, "");
    ContentChoice now = choice_of("totala", {"campaign", "core-contingency", "music"});
    OA_CHECK(!content_gap(saved, now, kCopied).needed);
    now.expansions.push_back("battle-tactics");
    OA_CHECK(!content_gap(saved, now, kCopied).needed);
}

void test_missing_expansion_turns_that_one_on() {
    const std::string saved_on[]{"campaign", "core-contingency"};
    const ContentRecord saved = content_record_from("totala", saved_on, "");
    const ContentChoice now = choice_of("totala", {"campaign", "battle-tactics", "music"});
    const auto gap = content_gap(saved, now, kCopied);
    OA_CHECK(gap.needed);
    OA_CHECK(gap.can_switch);
    OA_CHECK(!gap.base_differs);
    OA_CHECK(!gap.clears_mod);
    OA_CHECK(gap.block == ContentBlock::none);
    OA_CHECK(gap.focus == "Core Contingency");
    OA_CHECK(gap.next.base_game == "totala");
    OA_CHECK(has_expansion(gap.next, "campaign"));
    OA_CHECK(has_expansion(gap.next, "core-contingency"));
    OA_CHECK(has_expansion(gap.next, "battle-tactics"));
    OA_CHECK(has_expansion(gap.next, "music"));
}

void test_other_base_game() {
    const ContentRecord saved =
        content_record_from("totala-demo", std::span<const std::string>{}, "Ridge");
    const ContentChoice now = choice_of("totala", {"campaign", "music"});
    const auto gap = content_gap(saved, now, kCopied);
    OA_CHECK(gap.needed);
    OA_CHECK(gap.can_switch);
    OA_CHECK(gap.base_differs);
    OA_CHECK(gap.clears_mod);
    OA_CHECK(gap.focus == "Total Annihilation demo");
    OA_CHECK(gap.next.base_game == "totala-demo");
    OA_CHECK(gap.next.expansions.empty());
}

void test_not_copied_and_command_line() {
    const std::string saved_on[]{"core-contingency", "campaign"};
    const ContentRecord saved = content_record_from("totala", saved_on, "");
    const ContentChoice now = choice_of("totala", {"campaign"});
    const std::string_view campaign_only[]{"totala", "campaign"};
    const auto missing = content_gap(saved, now, campaign_only);
    OA_CHECK(missing.needed);
    OA_CHECK(!missing.can_switch);
    OA_CHECK(missing.block == ContentBlock::not_copied);

    ContentChoice named = now;
    named.command_line = true;
    const auto held = content_gap(saved, named, kCopied);
    OA_CHECK(held.needed);
    OA_CHECK(!held.can_switch);
    OA_CHECK(held.block == ContentBlock::command_line);
}

} // namespace

int main() {
    test_record_drops_music_and_orders_expansions();
    test_text_round_trip_and_old_file();
    test_matching_and_extra_now_need_nothing();
    test_missing_expansion_turns_that_one_on();
    test_other_base_game();
    test_not_copied_and_command_line();
    return oa::test::check_exit_status();
}
