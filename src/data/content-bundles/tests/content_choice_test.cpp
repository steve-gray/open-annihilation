// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The choice of base game and expansions: turning one on or off, choosing a
// base game, a mod that loses an expansion it needs, a player with no stored
// choice, and --content.

#include "oa/data/content_bundles.hpp"
#include "oa/data/content_choice.hpp"
#include "oa/test/check.hpp"

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using oa::data::content_bundles::Bundle;
using oa::data::content_bundles::ContentChangeResult;
using oa::data::content_bundles::ContentChoice;
using oa::data::content_bundles::ContentFile;
using oa::data::content_bundles::ContentRefusal;
using oa::data::content_bundles::ModClearReason;
using oa::data::content_bundles::SourceFile;
using oa::data::content_bundles::StoredContent;
using oa::data::content_bundles::choose_content_base_game;
using oa::data::content_bundles::expansions_text;
using oa::data::content_bundles::find_bundle;
using oa::data::content_bundles::parse_content_argument;
using oa::data::content_bundles::present_bundles;
using oa::data::content_bundles::resolve_content_choice;
using oa::data::content_bundles::turn_content_expansion;

const std::string_view k_all[]{
    "totala",
    "totala-demo",
    "campaign",
    "core-contingency",
    "battle-tactics",
    "music",
    "cavedog-units",
    "cavedog-tilesets",
};

/// Reports whether `got` is `expected`, in that order.
///
/// @param got the ids a function returned
/// @param expected the ids the case wants
/// @return true when the two lists are the same
bool same(const std::vector<std::string>& got, std::initializer_list<const char*> expected) {
    if (got.size() != expected.size())
        return false;
    std::size_t index = 0;
    for (const char* id : expected) {
        if (got[index] != id)
            return false;
        ++index;
    }
    return true;
}

/// Copies views into strings so a check can hold them.
///
/// @param ids views into the table
/// @return the same ids
std::vector<std::string> as_strings(std::span<const std::string_view> ids) {
    std::vector<std::string> owned;
    for (const std::string_view id : ids)
        owned.emplace_back(id);
    return owned;
}

/// Reports whether an expansion is on.
///
/// @param expansions the expansions that are on
/// @param id the expansion
/// @return true when it is on
bool is_on(const std::vector<std::string>& expansions, const char* id) {
    for (const std::string& on : expansions) {
        if (on == id)
            return true;
    }
    return false;
}

/// A choice with those expansions on.
///
/// @param expansions the expansions that are on
/// @param game the base game
/// @return the choice
ContentChoice playing(std::initializer_list<const char*> expansions, const char* game = "totala") {
    ContentChoice choice;
    choice.base_game = game;
    for (const char* id : expansions)
        choice.expansions.emplace_back(id);
    return choice;
}

/// The files of one bundle, by the names the table copies them as.
///
/// @param bundle_id the bundle
/// @return one entry per file; empty when the id is not a bundle
std::vector<SourceFile> files_of(std::string_view bundle_id) {
    std::vector<SourceFile> files;
    const Bundle* bundle = find_bundle(bundle_id);
    if (bundle == nullptr)
        return files;
    for (const ContentFile& file : bundle->files)
        files.push_back(SourceFile{file.copy_name, file.size});
    return files;
}

void test_present_bundles() {
    const std::vector<SourceFile> campaign = files_of("campaign");
    OA_CHECK(same(as_strings(present_bundles(campaign)), {"campaign"}));
    std::vector<SourceFile> short_of = campaign;
    short_of.pop_back();
    OA_CHECK(present_bundles(short_of).empty());
    std::vector<SourceFile> wrong = campaign;
    wrong[0].size += 1;
    OA_CHECK(present_bundles(wrong).empty());

    const Bundle* music = find_bundle("music");
    OA_CHECK(music != nullptr && !music->files.empty());
    const ContentFile& track = music->files[0];
    const SourceFile by_source{track.source_names[0], track.size};
    OA_CHECK(same(as_strings(present_bundles(std::span<const SourceFile>(&by_source, 1))), {}));
    std::vector<SourceFile> every_track = files_of("music");
    every_track[0].name = track.source_names[0];
    OA_CHECK(same(as_strings(present_bundles(every_track)), {"music"}));
}

void test_migration() {
    const ContentChoice fresh = resolve_content_choice({}, k_all, {});
    OA_CHECK(fresh.migration);
    OA_CHECK(fresh.base_game == "totala");
    OA_CHECK(!fresh.command_line);
    OA_CHECK(fresh.error.empty());
    OA_CHECK(same(
        fresh.expansions,
        {"campaign",
         "core-contingency",
         "battle-tactics",
         "music",
         "cavedog-units",
         "cavedog-tilesets"}
    ));

    const std::string_view music_only[]{"music"};
    const ContentChoice music = resolve_content_choice({}, music_only, {});
    OA_CHECK(music.migration);
    OA_CHECK(same(music.expansions, {"music"}));

    // Core Contingency cannot be on without the campaign, so a copy of it
    // alone stays off. Music, which needs no expansion, stays on.
    const std::string_view contingency_and_music[]{"core-contingency", "music"};
    const ContentChoice partial = resolve_content_choice({}, contingency_and_music, {});
    OA_CHECK(same(partial.expansions, {"music"}));

    StoredContent stale;
    const std::string_view only_music = "music";
    stale.expansions = only_music;
    const ContentChoice ignored = resolve_content_choice(stale, k_all, {});
    OA_CHECK(ignored.migration);
    OA_CHECK(ignored.expansions.size() == 6);

    StoredContent corrupt;
    const std::string_view ridge = "ridge";
    corrupt.base_game = ridge;
    const ContentChoice fallback = resolve_content_choice(corrupt, music_only, {});
    OA_CHECK(fallback.migration);
    OA_CHECK(fallback.base_game == "totala");
    OA_CHECK(same(fallback.expansions, {"music"}));
}

void test_stored_set() {
    StoredContent stored;
    const std::string_view totala = "totala";
    const std::string_view none;
    stored.base_game = totala;
    stored.expansions = none;
    const ContentChoice empty = resolve_content_choice(stored, k_all, {});
    OA_CHECK(!empty.migration);
    OA_CHECK(empty.expansions.empty());

    StoredContent absent_list;
    absent_list.base_game = totala;
    const ContentChoice defaults = resolve_content_choice(absent_list, k_all, {});
    OA_CHECK(!defaults.migration);
    OA_CHECK(defaults.expansions.size() == 6);

    const std::string_view tactics = "battle-tactics";
    StoredContent one;
    one.base_game = totala;
    one.expansions = tactics;
    const ContentChoice closed = resolve_content_choice(one, k_all, {});
    OA_CHECK(same(closed.expansions, {"campaign", "battle-tactics"}));

    const std::string_view spaced = "music, campaign";
    StoredContent spaces;
    spaces.base_game = totala;
    spaces.expansions = spaced;
    OA_CHECK(same(resolve_content_choice(spaces, k_all, {}).expansions, {"campaign", "music"}));

    const std::string_view demo = "totala-demo";
    const std::string_view demo_music = "music";
    StoredContent demo_choice;
    demo_choice.base_game = demo;
    demo_choice.expansions = demo_music;
    const ContentChoice demo_on = resolve_content_choice(demo_choice, k_all, {});
    OA_CHECK(demo_on.base_game == "totala-demo");
    OA_CHECK(demo_on.expansions.empty());

    OA_CHECK(expansions_text(closed.expansions) == "campaign,battle-tactics");
    OA_CHECK(expansions_text({}).empty());
}

void test_turning() {
    const ContentChoice base = playing({});
    const ContentChangeResult campaign =
        turn_content_expansion(base, "campaign", true, k_all, false, false, {});
    OA_CHECK(campaign.applied);
    OA_CHECK(campaign.refusal == ContentRefusal::none);
    OA_CHECK(campaign.mod_cleared == ModClearReason::none);
    OA_CHECK(same(campaign.choice.expansions, {"campaign", "core-contingency", "battle-tactics"}));

    const ContentChangeResult units =
        turn_content_expansion(base, "cavedog-units", true, k_all, false, false, {});
    OA_CHECK(same(units.choice.expansions, {"campaign", "core-contingency", "cavedog-units"}));

    const std::string_view copied_without_tactics[]{
        "totala", "campaign", "core-contingency", "music", "cavedog-units", "cavedog-tilesets"
    };
    const ContentChangeResult no_tactics =
        turn_content_expansion(base, "campaign", true, copied_without_tactics, false, false, {});
    OA_CHECK(same(no_tactics.choice.expansions, {"campaign", "core-contingency"}));

    const ContentChoice all = playing(
        {"campaign",
         "core-contingency",
         "battle-tactics",
         "music",
         "cavedog-units",
         "cavedog-tilesets"}
    );
    const ContentChangeResult campaign_off =
        turn_content_expansion(all, "campaign", false, k_all, false, false, {});
    OA_CHECK(same(campaign_off.choice.expansions, {"music", "cavedog-tilesets"}));
    const ContentChangeResult contingency_off =
        turn_content_expansion(all, "core-contingency", false, k_all, false, false, {});
    OA_CHECK(same(
        contingency_off.choice.expansions,
        {"campaign", "battle-tactics", "music", "cavedog-tilesets"}
    ));

    const ContentChangeResult again =
        turn_content_expansion(campaign.choice, "campaign", true, k_all, false, false, {});
    OA_CHECK(!again.applied);
    OA_CHECK(again.refusal == ContentRefusal::none);

    const ContentChangeResult already_off =
        turn_content_expansion(base, "music", false, k_all, false, false, {});
    OA_CHECK(!already_off.applied);
    OA_CHECK(already_off.choice.expansions.empty());

    const ContentChangeResult during =
        turn_content_expansion(all, "music", false, k_all, true, true, {});
    OA_CHECK(!during.applied);
    OA_CHECK(during.refusal == ContentRefusal::in_match);
    OA_CHECK(during.mod_cleared == ModClearReason::none);
    OA_CHECK(during.choice.expansions.size() == all.expansions.size());

    ContentChoice commanded = all;
    commanded.command_line = true;
    const ContentChangeResult locked =
        turn_content_expansion(commanded, "music", false, k_all, false, false, {});
    OA_CHECK(!locked.applied);
    OA_CHECK(locked.refusal == ContentRefusal::command_line);

    const ContentChangeResult missing = turn_content_expansion(
        base, "music", true, std::span<const std::string_view>{}, false, false, {}
    );
    OA_CHECK(!missing.applied);
    OA_CHECK(missing.refusal == ContentRefusal::not_copied);

    const std::string_view music_only[]{"music"};
    const ContentChangeResult needs_campaign =
        turn_content_expansion(base, "core-contingency", true, music_only, false, false, {});
    OA_CHECK(needs_campaign.refusal == ContentRefusal::not_copied);

    OA_CHECK(
        turn_content_expansion(base, "ridge", true, k_all, false, false, {}).refusal ==
        ContentRefusal::unknown
    );
    OA_CHECK(
        turn_content_expansion(base, "totala", true, k_all, false, false, {}).refusal ==
        ContentRefusal::not_for_game
    );
    const ContentChoice demo = playing({}, "totala-demo");
    OA_CHECK(
        turn_content_expansion(demo, "music", true, k_all, false, false, {}).refusal ==
        ContentRefusal::not_for_game
    );
}

void test_mod_cleared_when_a_need_goes_off() {
    const ContentChoice all = playing(
        {"campaign",
         "core-contingency",
         "battle-tactics",
         "music",
         "cavedog-units",
         "cavedog-tilesets"}
    );
    const std::string_view units[]{"cavedog-units"};
    const ContentChangeResult lost =
        turn_content_expansion(all, "core-contingency", false, k_all, true, true, units);
    OA_CHECK(!lost.applied);
    OA_CHECK(lost.refusal == ContentRefusal::in_match);

    const ContentChangeResult cleared =
        turn_content_expansion(all, "core-contingency", false, k_all, false, true, units);
    OA_CHECK(cleared.applied);
    OA_CHECK(cleared.mod_cleared == ModClearReason::required_expansion_off);
    OA_CHECK(!is_on(cleared.choice.expansions, "cavedog-units"));

    const std::string_view tactics[]{"battle-tactics"};
    const ContentChangeResult kept =
        turn_content_expansion(all, "core-contingency", false, k_all, false, true, tactics);
    OA_CHECK(kept.applied);
    OA_CHECK(kept.mod_cleared == ModClearReason::none);

    const std::string_view music[]{"music"};
    const ContentChangeResult music_stays =
        turn_content_expansion(all, "core-contingency", false, k_all, false, true, music);
    OA_CHECK(music_stays.mod_cleared == ModClearReason::none);

    const ContentChangeResult no_mod =
        turn_content_expansion(all, "music", false, k_all, false, false, music);
    OA_CHECK(no_mod.mod_cleared == ModClearReason::none);
}

void test_choose_base_game() {
    const ContentChoice with_mod = playing({"music"});
    const ContentChangeResult chosen =
        choose_content_base_game(with_mod, "totala", k_all, false, true);
    OA_CHECK(chosen.applied);
    OA_CHECK(chosen.mod_cleared == ModClearReason::base_game_chosen);
    OA_CHECK(chosen.choice.base_game == "totala");
    OA_CHECK(chosen.choice.expansions.size() == 6);

    const std::string_view campaign_only[]{"campaign", "music"};
    const ContentChangeResult some =
        choose_content_base_game(with_mod, "totala", campaign_only, false, false);
    OA_CHECK(some.mod_cleared == ModClearReason::none);
    OA_CHECK(same(some.choice.expansions, {"campaign", "music"}));

    const ContentChangeResult demo =
        choose_content_base_game(with_mod, "totala-demo", k_all, false, true);
    OA_CHECK(demo.applied);
    OA_CHECK(demo.choice.base_game == "totala-demo");
    OA_CHECK(demo.choice.expansions.empty());
    OA_CHECK(demo.mod_cleared == ModClearReason::base_game_chosen);

    const ContentChangeResult during =
        choose_content_base_game(with_mod, "totala-demo", k_all, true, true);
    OA_CHECK(!during.applied);
    OA_CHECK(during.refusal == ContentRefusal::in_match);
    OA_CHECK(during.mod_cleared == ModClearReason::none);
    OA_CHECK(during.choice.base_game == "totala");

    ContentChoice commanded = with_mod;
    commanded.command_line = true;
    OA_CHECK(
        choose_content_base_game(commanded, "totala-demo", k_all, false, true).refusal ==
        ContentRefusal::command_line
    );
    OA_CHECK(
        choose_content_base_game(with_mod, "ridge", k_all, false, false).refusal ==
        ContentRefusal::unknown
    );
    OA_CHECK(
        choose_content_base_game(with_mod, "campaign", k_all, false, false).refusal ==
        ContentRefusal::not_for_game
    );

    const ContentChoice already = playing(
        {"campaign",
         "core-contingency",
         "battle-tactics",
         "music",
         "cavedog-units",
         "cavedog-tilesets"}
    );
    const ContentChangeResult same_choice =
        choose_content_base_game(already, "totala", k_all, false, false);
    OA_CHECK(!same_choice.applied);
}

void test_content_option() {
    const ContentChoice alone = parse_content_argument("totala");
    OA_CHECK(alone.error.empty());
    OA_CHECK(alone.command_line);
    OA_CHECK(alone.base_game == "totala");
    OA_CHECK(alone.expansions.empty());
    OA_CHECK(parse_content_argument("totala:").expansions.empty());

    const ContentChoice named = parse_content_argument("totala:campaign,core-contingency");
    OA_CHECK(named.error.empty());
    OA_CHECK(same(named.expansions, {"campaign", "core-contingency"}));

    const ContentChoice needs = parse_content_argument("totala:core-contingency");
    OA_CHECK(same(needs.expansions, {"campaign", "core-contingency"}));
    const ContentChoice tactics = parse_content_argument("totala:battle-tactics");
    OA_CHECK(same(tactics.expansions, {"campaign", "battle-tactics"}));
    const ContentChoice units = parse_content_argument("totala:cavedog-units");
    OA_CHECK(same(units.expansions, {"campaign", "core-contingency", "cavedog-units"}));
    OA_CHECK(same(parse_content_argument("totala:music,music").expansions, {"music"}));

    const ContentChoice demo = parse_content_argument("totala-demo");
    OA_CHECK(demo.error.empty());
    OA_CHECK(demo.base_game == "totala-demo");
    OA_CHECK(demo.expansions.empty());

    OA_CHECK(
        parse_content_argument("ridge").error == "--content names an unknown base game: ridge"
    );
    OA_CHECK(
        parse_content_argument("campaign").error ==
        "--content names an expansion as the base game: campaign"
    );
    OA_CHECK(
        parse_content_argument("totala:ridge").error ==
        "--content names an unknown expansion: ridge"
    );
    OA_CHECK(
        parse_content_argument("totala:totala").error ==
        "--content names a base game as an expansion: totala"
    );
    OA_CHECK(
        parse_content_argument("totala-demo:music").error ==
        "--content: music does not extend totala-demo"
    );
    OA_CHECK(
        parse_content_argument("totala:campaign,").error == "--content has an empty expansion"
    );
    OA_CHECK(
        parse_content_argument("totala:campaign,,music").error == "--content has an empty expansion"
    );
    OA_CHECK(
        parse_content_argument("totala:campaign, music").error ==
        "--content has a space in GAME[:EXPANSION,...]"
    );
    OA_CHECK(parse_content_argument("").error == "--content requires a value");

    StoredContent stored;
    const std::string_view totala = "totala";
    const std::string_view music = "music";
    stored.base_game = totala;
    stored.expansions = music;
    const ContentChoice run = resolve_content_choice(stored, k_all, "totala:campaign");
    OA_CHECK(run.command_line);
    OA_CHECK(!run.migration);
    OA_CHECK(same(run.expansions, {"campaign"}));
    OA_CHECK(
        resolve_content_choice(stored, k_all, "totala:ridge").error ==
        "--content names an unknown expansion: ridge"
    );
}

} // namespace

int main() {
    test_present_bundles();
    test_migration();
    test_stored_set();
    test_turning();
    test_mod_cleared_when_a_need_goes_off();
    test_choose_base_game();
    test_content_option();
    return oa::test::check_exit_status();
}
