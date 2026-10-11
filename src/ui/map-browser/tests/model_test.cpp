// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// ui-map-browser: the map browser's model (merging the three sources, every
// filter and their combination, the search, the order, the primary button
// and the cards' words), then its screen (layout_test.cpp).
#include "oa/test/check.hpp"
#include "oa/ui/map_browser/model.hpp"

#include <cstddef>
#include <optional>
#include <stdint.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// Runs the screen's tests, in layout_test.cpp.
void layout_tests();

namespace {

namespace mb = oa::ui::map_browser;
using Origin = mb::MapCard::Origin;
using Fit = mb::MapCard::Fit;

/// Returns a card.
///
/// @param name the game name
/// @param title the title
/// @param players the most players
/// @param size the size text
/// @return the card
mb::MapCard
card(std::string_view name, std::string_view title, int32_t players, std::string_view size) {
    mb::MapCard made;
    made.name = std::string(name);
    made.title = std::string(title);
    made.players = players;
    made.size = std::string(size);
    return made;
}

/// Returns the shown cards' names, in order.
///
/// @param browser the browser
/// @return the names
std::vector<std::string> shown_names(const mb::Browser& browser) {
    std::vector<std::string> names;
    for (const std::size_t index : browser.shown)
        names.push_back(browser.cards[index].name);
    return names;
}

/// A browser of three base maps, two installed and three online, one online
/// map the same as an installed one.
///
/// @return the browser, its filters applied
mb::Browser sample() {
    std::vector<mb::MapCard> base{
        card("Painted Desert", "Painted Desert", 4, "16 x 16"),
        card("Coast To Coast", "Coast to Coast", 2, "20 x 20"),
        card("Big Wars", "Big Wars", 8, "32 x 32"),
    };
    std::vector<mb::MapCard> installed{
        card("tidewater@lowlands", "Tidewater", 4, "24x24"),
        card("fen_crossing@lowlands", "Fen Crossing", 3, "18x18"),
    };
    installed[0].pack_name = "Lowlands";
    installed[1].pack_name = "Lowlands";
    std::vector<mb::MapCard> online{
        card("isle_of_ashes@archipelago", "Isle of Ashes", 4, "16x16"),
        card("tidewater@lowlands", "Tidewater (online)", 4, "24x24"),
        card("black_shoals@archipelago", "Black Shoals", 6, "12x12"),
    };
    online[0].pack_name = "Archipelago Pack";
    online[2].pack_name = "Archipelago Pack";
    mb::Browser browser;
    browser.cards = mb::merge_cards(std::move(base), std::move(installed), std::move(online));
    mb::apply_filters(browser);
    return browser;
}

void merging() {
    const mb::Browser browser = sample();
    // The online copy of an installed map shows once, as installed.
    OA_CHECK(browser.cards.size() == 7);
    std::size_t tidewater = 0;
    for (const mb::MapCard& made : browser.cards)
        if (made.name == "tidewater@lowlands") {
            ++tidewater;
            OA_CHECK(made.origin == Origin::installed);
            OA_CHECK(made.title == "Tidewater");
        }
    OA_CHECK(tidewater == 1);
    // Also when the installed pack came from another registry, and a second
    // registry's online copy shows once, the first's.
    mb::MapCard from_one = card("isle_of_ashes@archipelago", "Isle of Ashes", 4, "16x16");
    from_one.registry_name = "Registry One";
    mb::MapCard from_two = from_one;
    from_two.registry_name = "Registry Two";
    mb::MapCard installed = from_one;
    installed.registry_name = "Registry Three";
    std::vector<mb::MapCard> merged = mb::merge_cards({}, {installed}, {from_one, from_two});
    OA_CHECK(merged.size() == 1);
    OA_CHECK(merged[0].origin == Origin::installed && merged[0].registry_name == "Registry Three");
    merged = mb::merge_cards({}, {}, {from_one, from_two});
    OA_CHECK(merged.size() == 1);
    OA_CHECK(merged[0].origin == Origin::online && merged[0].registry_name == "Registry One");
    // Each card's origin is its list's, whatever it said.
    mb::MapCard wrong = card("x", "X", 2, "8x8");
    wrong.origin = Origin::online;
    merged = mb::merge_cards({wrong}, {}, {});
    OA_CHECK(merged.size() == 1 && merged[0].origin == Origin::base);
}

void order() {
    const mb::Browser browser = sample();
    // Base maps, then installed, then online, each by title ignoring case.
    const std::vector<std::string> expected{
        "Big Wars",
        "Coast To Coast",
        "Painted Desert",
        "fen_crossing@lowlands",
        "tidewater@lowlands",
        "black_shoals@archipelago",
        "isle_of_ashes@archipelago",
    };
    OA_CHECK(shown_names(browser) == expected);
}

void source_filter() {
    mb::Browser browser = sample();
    browser.filters.source = mb::Filters::Source::base;
    mb::apply_filters(browser);
    OA_CHECK(shown_names(browser).size() == 3);
    browser.filters.source = mb::Filters::Source::installed;
    mb::apply_filters(browser);
    OA_CHECK(
        shown_names(browser) ==
        (std::vector<std::string>{"fen_crossing@lowlands", "tidewater@lowlands"})
    );
    browser.filters.source = mb::Filters::Source::online;
    mb::apply_filters(browser);
    OA_CHECK(
        shown_names(browser) ==
        (std::vector<std::string>{"black_shoals@archipelago", "isle_of_ashes@archipelago"})
    );
    browser.filters.source = mb::Filters::Source::all;
    mb::apply_filters(browser);
    OA_CHECK(browser.shown.size() == 7);
}

void players_filter() {
    OA_CHECK(mb::players_match(2, 2) && !mb::players_match(2, 3));
    OA_CHECK(mb::players_match(4, 4) && !mb::players_match(4, 5));
    OA_CHECK(mb::players_match(mb::many_players_chip, 5));
    OA_CHECK(mb::players_match(mb::many_players_chip, 10));
    OA_CHECK(!mb::players_match(mb::many_players_chip, 4));
    OA_CHECK(!mb::players_match(mb::many_players_chip, 11));

    mb::Browser browser = sample();
    browser.filters.players = {4};
    mb::apply_filters(browser);
    OA_CHECK(
        shown_names(browser) ==
        (std::vector<std::string>{
            "Painted Desert", "tidewater@lowlands", "isle_of_ashes@archipelago"
        })
    );
    // Two chips on: either matches.
    browser.filters.players = {2, mb::many_players_chip};
    mb::apply_filters(browser);
    OA_CHECK(
        shown_names(browser) ==
        (std::vector<std::string>{"Big Wars", "Coast To Coast", "black_shoals@archipelago"})
    );
    browser.filters.players = {3};
    mb::apply_filters(browser);
    OA_CHECK(shown_names(browser) == (std::vector<std::string>{"fen_crossing@lowlands"}));
}

void size_filter() {
    OA_CHECK(mb::map_sides("16 x 16") == (std::pair<int32_t, int32_t>{16, 16}));
    OA_CHECK(mb::map_sides("24x12") == (std::pair<int32_t, int32_t>{24, 12}));
    OA_CHECK(!mb::map_sides("huge"));
    OA_CHECK(!mb::map_sides("16"));
    OA_CHECK(mb::size_letter("10x2") == 'S');
    OA_CHECK(mb::size_letter("11 x 4") == 'M');
    OA_CHECK(mb::size_letter("8x20") == 'M');
    OA_CHECK(mb::size_letter("21x21") == 'L');
    OA_CHECK(!mb::size_letter(""));

    mb::Browser browser = sample();
    browser.filters.sizes = {'S'};
    mb::apply_filters(browser);
    OA_CHECK(shown_names(browser).empty());
    browser.filters.sizes = {'M'};
    mb::apply_filters(browser);
    OA_CHECK(
        shown_names(browser) == (std::vector<std::string>{
                                    "Coast To Coast",
                                    "Painted Desert",
                                    "fen_crossing@lowlands",
                                    "black_shoals@archipelago",
                                    "isle_of_ashes@archipelago",
                                })
    );
    browser.filters.sizes = {'L', 'S'};
    mb::apply_filters(browser);
    OA_CHECK(shown_names(browser) == (std::vector<std::string>{"Big Wars", "tidewater@lowlands"}));
}

void search() {
    mb::Browser browser = sample();
    // Title, ignoring case.
    browser.filters.query = "ISLE";
    mb::apply_filters(browser);
    OA_CHECK(shown_names(browser) == (std::vector<std::string>{"isle_of_ashes@archipelago"}));
    // The name.
    browser.filters.query = "fen_cross";
    mb::apply_filters(browser);
    OA_CHECK(shown_names(browser) == (std::vector<std::string>{"fen_crossing@lowlands"}));
    // The pack's name.
    browser.filters.query = "lowlands";
    mb::apply_filters(browser);
    OA_CHECK(shown_names(browser).size() == 2);
    browser.filters.query = "nothing like it";
    mb::apply_filters(browser);
    OA_CHECK(browser.shown.empty());
}

void combined_filters() {
    mb::Browser browser = sample();
    browser.filters.source = mb::Filters::Source::online;
    browser.filters.players = {4};
    browser.filters.sizes = {'M'};
    browser.filters.query = "ashes";
    mb::apply_filters(browser);
    OA_CHECK(shown_names(browser) == (std::vector<std::string>{"isle_of_ashes@archipelago"}));
    browser.filters.source = mb::Filters::Source::installed;
    mb::apply_filters(browser);
    OA_CHECK(browser.shown.empty());
    // A selection the filters hide is cleared.
    browser = sample();
    browser.selected = browser.shown.front();
    browser.filters.source = mb::Filters::Source::online;
    mb::apply_filters(browser);
    OA_CHECK(!browser.selected);
}

void primary() {
    mb::Browser browser = sample();
    // No selection: Select, disabled.
    mb::PrimaryAction action = mb::primary_action(browser);
    OA_CHECK(action.kind == mb::Primary::select && !action.enabled);
    const auto select_named = [&](std::string_view name) {
        for (std::size_t index = 0; index < browser.cards.size(); ++index)
            if (browser.cards[index].name == name)
                browser.selected = index;
    };
    // A base map, and an installed one that fits: Select.
    select_named("Painted Desert");
    action = mb::primary_action(browser);
    OA_CHECK(action.kind == mb::Primary::select && action.enabled && action.reason.empty());
    select_named("tidewater@lowlands");
    OA_CHECK(mb::primary_action(browser).enabled);
    // An installed map that does not fit: disabled, with its reason.
    browser.cards[*browser.selected].mod_fit = Fit::does_not_fit;
    browser.cards[*browser.selected].fit_reason = "New names: the feature 'x' is defined already";
    action = mb::primary_action(browser);
    OA_CHECK(action.kind == mb::Primary::select && !action.enabled);
    OA_CHECK(action.reason.find("New names") == 0);
    // An online map that fits, or whose fit is unknown: Download and select.
    select_named("isle_of_ashes@archipelago");
    action = mb::primary_action(browser);
    OA_CHECK(action.kind == mb::Primary::download_and_select && action.enabled);
    browser.cards[*browser.selected].mod_fit = Fit::unknown;
    OA_CHECK(mb::primary_action(browser).enabled);
    // One that does not: disabled.
    browser.cards[*browser.selected].base_fit = Fit::does_not_fit;
    action = mb::primary_action(browser);
    OA_CHECK(action.kind == mb::Primary::download_and_select && !action.enabled);
    // While a download runs: Downloading…, disabled.
    browser.cards[*browser.selected].base_fit = Fit::fits;
    browser.downloading = "isle_of_ashes@archipelago";
    action = mb::primary_action(browser);
    OA_CHECK(action.kind == mb::Primary::downloading && !action.enabled);
}

void words() {
    OA_CHECK(mb::card_word("isle_of_ashes@archipelago") == "isle-of-ashes-archipelago");
    OA_CHECK(mb::card_word("Painted Desert") == "painted-desert");
    OA_CHECK(mb::card_word("a-b") == "a-b");
    OA_CHECK(mb::card_word("A_B") == "a-b");
    // A character of several bytes is one hyphen.
    OA_CHECK(mb::card_word("caf\xc3\xa9") == "caf-");
    OA_CHECK(mb::card_word("") == "map");
    OA_CHECK(mb::card_word(std::string(200, 'x')).size() == mb::most_word_bytes);
}

} // namespace

int main() {
    merging();
    order();
    source_filter();
    players_filter();
    size_filter();
    search();
    combined_filters();
    primary();
    words();
    layout_tests();
    return oa::test::check_exit_status();
}
