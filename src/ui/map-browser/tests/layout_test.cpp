// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The map browser's screen in a synthetic font: 3, 4 and 6 columns at
// Compact, Regular and Large; nothing outside the window; the footer's
// buttons; every automation name, sound and unique, two maps whose names
// give one word told apart; the pictures asked for only for the cards in
// view and the row after; focus by geometry and Tab in the declared order;
// and what a press, a tap, the keys, typing and the wheel do.
#include "oa/formats/fnt.hpp"
#include "oa/test/check.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/kit/canvas.hpp"
#include "oa/ui/kit/controls.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/looks.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/map_browser/model.hpp"
#include "oa/ui/map_browser/screen.hpp"
#include "oa/ui/map_previews/map_previews.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <set>
#include <stdint.h>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

/// Runs the screen's tests; model_test.cpp's main calls it.
void layout_tests();

namespace {

namespace kit = oa::ui::kit;
namespace mb = oa::ui::map_browser;
namespace renderer = oa::ui::frontend_renderer;
namespace previews = oa::ui::map_previews;

/// The synthetic font of the settings pixel test, for both roles.
///
/// @return the fonts
kit::Fonts block_fonts() {
    renderer::TextFont font;
    font.font.nominal_height = 8;
    constexpr uint8_t ink_index = 1;
    for (int byte = 0; byte < 256; ++byte) {
        const auto width = static_cast<uint16_t>(2 + byte % 4);
        constexpr uint16_t height = 8;
        const auto count = static_cast<std::size_t>(width) * height;
        std::vector<uint8_t> pixels(count, ink_index);
        std::vector<uint8_t> coverage(count, 1);
        font.font.glyphs[static_cast<std::size_t>(byte)] =
            oa::formats::fnt::Glyph{width, height, 0, 0, std::move(pixels), std::move(coverage)};
    }
    font.ink[ink_index] = static_cast<uint16_t>(renderer::blend_opaque);
    return {font, font, {}, {}};
}

/// Returns the fonts every layout here is measured in.
///
/// @return the fonts
const kit::Fonts& fonts() {
    static const kit::Fonts loaded = block_fonts();
    return loaded;
}

/// One size class's frame, as the OA layer gives it for a window.
struct ClassFrame {
    kit::Frame frame{};    ///< the frame
    std::size_t columns{}; ///< the grid's columns there
    kit::Point window{};   ///< the browser's window there
};

/// The frames: Compact at 640 by 480, Regular at 960 by 540 points and
/// Large at 1280 by 720.
///
/// @return the frames
std::array<ClassFrame, 3> frames() {
    return {{
        {{{0, 0, 640, 480}, kit::SizeClass::compact, 100}, 3, {480, 324}},
        {{{0, 0, 960, 540}, kit::SizeClass::regular, 200}, 4, {720, 486}},
        {{{0, 0, 1280, 720}, kit::SizeClass::large, 200}, 6, {960, 600}},
    }};
}

/// Returns a browser of many maps: forty base maps, two installed, one of
/// which does not fit, and three online, two of whose names give one word.
///
/// @return the browser, its filters applied
mb::Browser many_maps() {
    std::vector<mb::MapCard> base;
    for (int32_t index = 0; index < 40; ++index) {
        mb::MapCard card;
        card.name = "Base Map " + std::to_string(100 + index);
        card.title = card.name;
        card.players = 2 + index % 9;
        card.size = std::to_string(8 + index % 25) + " x " + std::to_string(8 + index % 20);
        card.preview_key = card.name;
        base.push_back(std::move(card));
    }
    std::vector<mb::MapCard> installed(2);
    installed[0].name = "check_isle@oa-check-isles";
    installed[0].title = "Check Isle";
    installed[0].pack_name = "Map Pack Check Isles";
    installed[0].players = 2;
    installed[0].size = "2x2";
    installed[1].name = "check_clash@oa-check-clash";
    installed[1].title = "Check Clash";
    installed[1].pack_name = "Map Pack Check Clash";
    installed[1].players = 2;
    installed[1].size = "2x2";
    installed[1].base_fit = mb::MapCard::Fit::does_not_fit;
    installed[1].fit_reason = "New names: the feature 'dragonsteeth' is defined already";
    std::vector<mb::MapCard> online(3);
    online[0].name = "isle_of_ashes@archipelago";
    online[0].title = "Isle of Ashes";
    online[0].players = 4;
    online[0].size = "16x16";
    online[0].download_bytes = 2400000;
    online[0].preview_key = "http://127.0.0.1:9/v1/i/one.png";
    online[1].name = "a_b@twins";
    online[1].title = "Twin One";
    online[1].players = 4;
    online[1].size = "16x16";
    online[2].name = "a-b@twins";
    online[2].title = "Twin Two";
    online[2].players = 4;
    online[2].size = "16x16";
    mb::Browser browser;
    browser.cards = mb::merge_cards(std::move(base), std::move(installed), std::move(online));
    browser.mod_name = "Ridge";
    mb::apply_filters(browser);
    return browser;
}

/// Counts the hooks' asks, and which cards they were for.
struct AskCounter {
    std::vector<std::string> asked{}; ///< the cards' names, in order
};

/// The counting hooks.
///
/// @param counter the counter
/// @return the hooks
mb::PreviewHooks counting(AskCounter& counter) {
    mb::PreviewHooks hooks;
    hooks.context = &counter;
    hooks.ask = [](void* context, const mb::MapCard& card) -> const previews::Thumbnail* {
        static_cast<AskCounter*>(context)->asked.push_back(card.name);
        return nullptr;
    };
    return hooks;
}

/// Lays a browser out with hooks that ask for nothing.
///
/// @param browser the browser
/// @param state the screen's state
/// @param frame the frame
/// @return the display list
kit::DisplayList
laid_out(const mb::Browser& browser, const mb::ScreenState& state, const kit::Frame& frame) {
    return mb::browser_layout(browser, state, frame, fonts(), {});
}

/// Returns the control of a name.
///
/// @param list the display list
/// @param name the name
/// @return the control; null when none has it
const kit::Control* named(const kit::DisplayList& list, std::string_view name) {
    const kit::ControlId id = kit::control_named(list, name);
    return id == kit::no_control ? nullptr : kit::control_of(list, id);
}

/// Returns the cards' controls, in list order.
///
/// @param list the display list
/// @return the controls
std::vector<const kit::Control*> card_controls(const kit::DisplayList& list) {
    std::vector<const kit::Control*> cards;
    for (const kit::Control& control : list.controls)
        if (control.name.starts_with(mb::name_card_prefix))
            cards.push_back(&control);
    return cards;
}

/// Returns what of a rectangle a clip leaves; the rectangle when no clip.
///
/// @param rect the rectangle
/// @param clip the clip; empty for none
/// @return the part that can show
kit::Rect visible_part(const kit::Rect& rect, const kit::Rect& clip) {
    if (clip.width <= 0 || clip.height <= 0)
        return rect;
    return kit::intersect(rect, clip);
}

/// Presses and releases the pointer at a control's centre, or taps it.
///
/// @param browser the browser
/// @param state the screen's state
/// @param list the display list
/// @param name the control's name
/// @param reach a finger's reach; 0 for a mouse
/// @return what the release asked for
mb::ScreenResult press(
    mb::Browser& browser,
    mb::ScreenState& state,
    const kit::DisplayList& list,
    std::string_view name,
    int32_t reach = 0
) {
    const kit::Control* control = named(list, name);
    OA_CHECK(control != nullptr);
    if (control == nullptr)
        return {};
    const kit::Rect shown = visible_part(control->rect, control->clip);
    const kit::Point at{shown.x + shown.width / 2, shown.y + shown.height / 2};
    std::ignore = mb::browser_pointer(browser, state, list, mb::PointerKind::move, 0, at, reach);
    std::ignore = mb::browser_pointer(browser, state, list, mb::PointerKind::down, 1, at, reach);
    return mb::browser_pointer(browser, state, list, mb::PointerKind::up, 1, at, reach);
}

void columns_at_each_class() {
    for (const ClassFrame& each : frames()) {
        const mb::Browser browser = many_maps();
        const kit::DisplayList list = laid_out(browser, {}, each.frame);
        OA_CHECK(mb::window_size(each.frame).x == each.window.x);
        OA_CHECK(mb::window_size(each.frame).y == each.window.y);
        OA_CHECK(mb::grid_columns(each.frame.size_class) == each.columns);
        const std::vector<const kit::Control*> cards = card_controls(list);
        OA_CHECK(cards.size() == browser.shown.size());
        std::size_t first_row = 0;
        for (const kit::Control* control : cards)
            if (control->rect.y == cards.front()->rect.y)
                ++first_row;
        OA_CHECK(first_row == each.columns);
    }
}

void nothing_outside_the_window() {
    for (const ClassFrame& each : frames()) {
        mb::Browser browser = many_maps();
        browser.selected = browser.shown.front();
        mb::ScreenState state;
        // The last row, and the first, scrolled to.
        for (const int32_t scroll : {0, 100000}) {
            state.scroll = scroll;
            const kit::DisplayList list = laid_out(browser, state, each.frame);
            const kit::Rect window{0, 0, each.window.x, each.window.y};
            for (const kit::Item& item : list.items) {
                const kit::Rect drawn = visible_part(item.rect, item.clip);
                if (drawn.width <= 0 || drawn.height <= 0)
                    continue;
                if (!kit::wholly_in(drawn, window))
                    std::fprintf(
                        stderr,
                        "an item of role %d lies outside the window at %d,%d %dx%d\n",
                        static_cast<int>(item.role),
                        drawn.x,
                        drawn.y,
                        drawn.width,
                        drawn.height
                    );
                OA_CHECK(kit::wholly_in(drawn, window));
            }
            for (const kit::Control& control : list.controls) {
                const kit::Rect reach = visible_part(control.rect, control.clip);
                if (reach.width > 0 && reach.height > 0)
                    OA_CHECK(kit::wholly_in(reach, window));
            }
        }
        // A download under way, and a failure, keep inside too.
        browser.downloading = browser.cards[browser.shown.front()].name;
        browser.progress = 0.5F;
        kit::DisplayList list = laid_out(browser, {}, each.frame);
        bool bar = false;
        for (const kit::Item& item : list.items)
            if (item.role == kit::Role::progress) {
                bar = true;
                OA_CHECK(kit::wholly_in(item.rect, {0, 0, each.window.x, each.window.y}));
            }
        OA_CHECK(bar);
        browser.downloading.reset();
        browser.error = std::string(300, 'e');
        list = laid_out(browser, {}, each.frame);
        for (const kit::Item& item : list.items) {
            const kit::Rect drawn = visible_part(item.rect, item.clip);
            if (drawn.width > 0 && drawn.height > 0)
                OA_CHECK(kit::wholly_in(drawn, {0, 0, each.window.x, each.window.y}));
        }
    }
}

void names_and_buttons() {
    const std::array<std::string_view, 15> listed{
        mb::name_filter_all,
        mb::name_filter_base,
        mb::name_filter_installed,
        mb::name_filter_online,
        mb::name_players_2,
        mb::name_players_3,
        mb::name_players_4,
        mb::name_players_many,
        mb::name_size_small,
        mb::name_size_medium,
        mb::name_size_large,
        mb::name_search,
        mb::name_cancel,
        mb::name_select,
        "maps-card-isle-of-ashes-archipelago",
    };
    for (const ClassFrame& each : frames()) {
        const mb::Browser browser = many_maps();
        const kit::DisplayList list = laid_out(browser, {}, each.frame);
        OA_CHECK(kit::name_problem(list).empty());
        for (const std::string_view name : listed) {
            if (named(list, name) == nullptr)
                std::fprintf(
                    stderr, "no control is named %.*s\n", static_cast<int>(name.size()), name.data()
                );
            OA_CHECK(named(list, name) != nullptr);
        }
        // Two maps whose names give one word: the second in grid order takes -2.
        const kit::Control* one = named(list, "maps-card-a-b-twins");
        const kit::Control* two = named(list, "maps-card-a-b-twins-2");
        OA_CHECK(one != nullptr && two != nullptr);
        if (one != nullptr && two != nullptr) {
            OA_CHECK(one->text == "a_b@twins");
            OA_CHECK(two->text == "a-b@twins");
        }
        // The card's text is its game name.
        const kit::Control* isle = named(list, "maps-card-check-isle-oa-check-isles");
        OA_CHECK(isle != nullptr && isle->text == "check_isle@oa-check-isles");
        // The footer's buttons: Cancel, and Select disabled with nothing selected.
        const kit::Control* cancel = named(list, mb::name_cancel);
        const kit::Control* select = named(list, mb::name_select);
        OA_CHECK(cancel != nullptr && cancel->enabled && cancel->text == "Cancel");
        OA_CHECK(select != nullptr && !select->enabled && select->text == "Select");
    }
}

void pictures_asked_for_the_view_alone() {
    for (const ClassFrame& each : frames()) {
        const mb::Browser browser = many_maps();
        for (const int32_t scroll : {0, 400}) {
            mb::ScreenState state;
            state.scroll = scroll;
            AskCounter counter;
            const kit::DisplayList list =
                mb::browser_layout(browser, state, each.frame, fonts(), counting(counter));
            const std::vector<std::size_t> in_view =
                mb::cards_in_view(browser, state, each.frame, fonts());
            // Every card in view and the next row with a picture, and no other.
            std::vector<std::string> expected;
            for (const std::size_t index : in_view)
                if (!browser.cards[index].preview_key.empty())
                    expected.push_back(browser.cards[index].name);
            OA_CHECK(counter.asked == expected);
            OA_CHECK(!counter.asked.empty());
            OA_CHECK(counter.asked.size() < browser.shown.size());
            // The cards in view are those whose controls show, and one row more.
            std::size_t showing = 0;
            for (const kit::Control* control : card_controls(list))
                if (visible_part(control->rect, control->clip).height > 0)
                    ++showing;
            OA_CHECK(in_view.size() >= showing && in_view.size() <= showing + each.columns);
            OA_CHECK(in_view.size() > showing);
        }
    }
}

void focus_moves_by_geometry() {
    for (const ClassFrame& each : frames()) {
        mb::Browser browser = many_maps();
        mb::ScreenState state;
        kit::DisplayList list = laid_out(browser, state, each.frame);
        const std::vector<const kit::Control*> cards = card_controls(list);
        OA_CHECK(cards.size() > each.columns);
        if (cards.size() <= each.columns)
            continue;
        const kit::ControlId first = cards[0]->id;
        const kit::ControlId second = cards[1]->id;
        const kit::ControlId below = cards[each.columns]->id;
        state.interaction.focused = first;
        state.interaction.focus_shown = true;
        std::ignore = mb::browser_key(browser, state, list, kit::Key::right);
        OA_CHECK(state.interaction.focused == second);
        list = laid_out(browser, state, each.frame);
        std::ignore = mb::browser_key(browser, state, list, kit::Key::left);
        OA_CHECK(state.interaction.focused == first);
        list = laid_out(browser, state, each.frame);
        std::ignore = mb::browser_key(browser, state, list, kit::Key::down);
        OA_CHECK(state.interaction.focused == below);
        // The grid scrolls to show the focused card.
        list = laid_out(browser, state, each.frame);
        const kit::Control* focused = kit::control_of(list, state.interaction.focused);
        OA_CHECK(focused != nullptr && kit::wholly_in(focused->rect, focused->clip));
        // Up from the first row leaves the grid for the bar above it.
        state.interaction.focused = first;
        state.scroll = 0;
        list = laid_out(browser, state, each.frame);
        std::ignore = mb::browser_key(browser, state, list, kit::Key::up);
        const kit::Control* above = kit::control_of(list, state.interaction.focused);
        OA_CHECK(above != nullptr && !above->name.starts_with(mb::name_card_prefix));
        const std::vector<const kit::Control*> now = card_controls(list);
        OA_CHECK(above != nullptr && !now.empty() && above->rect.y < now[0]->rect.y);
    }
}

void tab_follows_the_declared_order() {
    const std::array<std::string_view, 12> bar_order{
        mb::name_filter_all,
        mb::name_filter_base,
        mb::name_filter_installed,
        mb::name_filter_online,
        mb::name_players_2,
        mb::name_players_3,
        mb::name_players_4,
        mb::name_players_many,
        mb::name_size_small,
        mb::name_size_medium,
        mb::name_size_large,
        mb::name_search,
    };
    for (const ClassFrame& each : frames()) {
        mb::Browser browser = many_maps();
        // The selected card is where Tab enters the grid.
        std::size_t isle = 0;
        for (std::size_t index = 0; index < browser.cards.size(); ++index)
            if (browser.cards[index].name == "isle_of_ashes@archipelago")
                isle = index;
        browser.selected = isle;
        mb::ScreenState state;
        std::vector<std::string> walked;
        for (int32_t step = 0; step < 15; ++step) {
            const kit::DisplayList list = laid_out(browser, state, each.frame);
            std::ignore = mb::browser_key(browser, state, list, kit::Key::tab);
            const kit::DisplayList after = laid_out(browser, state, each.frame);
            const kit::Control* control = kit::control_of(after, state.interaction.focused);
            walked.push_back(control != nullptr ? control->name : std::string());
        }
        for (std::size_t index = 0; index < bar_order.size(); ++index)
            OA_CHECK(walked[index] == bar_order[index]);
        OA_CHECK(walked[12] == "maps-card-isle-of-ashes-archipelago");
        OA_CHECK(walked[13] == mb::name_cancel);
        OA_CHECK(walked[14] == mb::name_select);
        // Shift+Tab from Cancel goes back to the grid at the selected card.
        kit::DisplayList list = laid_out(browser, state, each.frame);
        std::ignore = mb::browser_key(browser, state, list, kit::Key::back_tab);
        list = laid_out(browser, state, each.frame);
        std::ignore = mb::browser_key(browser, state, list, kit::Key::back_tab);
        list = laid_out(browser, state, each.frame);
        const kit::Control* control = kit::control_of(list, state.interaction.focused);
        OA_CHECK(control != nullptr && control->name == "maps-card-isle-of-ashes-archipelago");
        OA_CHECK(control != nullptr && kit::wholly_in(control->rect, control->clip));
    }
}

void pointer_and_keys() {
    const kit::Frame frame = frames()[1].frame;
    mb::Browser browser = many_maps();
    mb::ScreenState state;
    kit::DisplayList list = laid_out(browser, state, frame);

    // A source chip filters; a players chip and a size chip narrow it.
    std::ignore = press(browser, state, list, mb::name_filter_installed);
    OA_CHECK(browser.filters.source == mb::Filters::Source::installed);
    OA_CHECK(browser.shown.size() == 2);
    list = laid_out(browser, state, frame);
    const kit::Control* chip = named(list, mb::name_filter_installed);
    OA_CHECK(chip != nullptr && chip->checked);
    std::ignore = press(browser, state, list, mb::name_filter_all);
    list = laid_out(browser, state, frame);
    std::ignore = press(browser, state, list, mb::name_players_4);
    OA_CHECK(browser.filters.players == std::set<int32_t>{4});
    list = laid_out(browser, state, frame);
    std::ignore = press(browser, state, list, mb::name_size_medium);
    OA_CHECK(browser.filters.sizes == std::set<char>{'M'});
    for (const std::size_t index : browser.shown) {
        OA_CHECK(browser.cards[index].players == 4);
        OA_CHECK(mb::size_letter(browser.cards[index].size) == 'M');
    }
    list = laid_out(browser, state, frame);
    std::ignore = press(browser, state, list, mb::name_players_4);
    list = laid_out(browser, state, frame);
    std::ignore = press(browser, state, list, mb::name_size_medium);
    OA_CHECK(browser.filters.players.empty() && browser.filters.sizes.empty());

    // Typing in the search filters as it goes.
    list = laid_out(browser, state, frame);
    std::ignore = press(browser, state, list, mb::name_search);
    list = laid_out(browser, state, frame);
    OA_CHECK(mb::focused_field(state, list).has_value());
    std::ignore = mb::browser_text(browser, state, list, "is");
    OA_CHECK(browser.filters.query == "is");
    list = laid_out(browser, state, frame);
    std::ignore = mb::browser_text(browser, state, list, "le");
    OA_CHECK(browser.filters.query == "isle");
    OA_CHECK(browser.shown.size() == 2);
    list = laid_out(browser, state, frame);
    std::ignore = mb::browser_key(browser, state, list, kit::Key::backspace);
    OA_CHECK(browser.filters.query == "isl");
    list = laid_out(browser, state, frame);
    std::ignore = mb::browser_text(browser, state, list, "e");

    // A press on a card selects it; a press on the selected card acts.
    list = laid_out(browser, state, frame);
    mb::ScreenResult result = press(browser, state, list, "maps-card-check-isle-oa-check-isles");
    OA_CHECK(result.action == mb::BrowserAction::none);
    OA_CHECK(
        browser.selected && browser.cards[*browser.selected].name == "check_isle@oa-check-isles"
    );
    list = laid_out(browser, state, frame);
    const kit::Control* select = named(list, mb::name_select);
    OA_CHECK(select != nullptr && select->enabled);
    result = press(browser, state, list, "maps-card-check-isle-oa-check-isles");
    OA_CHECK(result.action == mb::BrowserAction::choose);
    result = press(browser, state, list, mb::name_select);
    OA_CHECK(result.action == mb::BrowserAction::choose);

    // A tap on a map that does not fit selects it, and acts on nothing.
    state.search = {};
    browser.filters.query.clear();
    browser.filters.source = mb::Filters::Source::installed;
    mb::apply_filters(browser);
    list = laid_out(browser, state, frame);
    result = press(browser, state, list, "maps-card-check-clash-oa-check-clash", 8);
    OA_CHECK(result.action == mb::BrowserAction::none);
    list = laid_out(browser, state, frame);
    result = press(browser, state, list, "maps-card-check-clash-oa-check-clash", 8);
    OA_CHECK(result.action == mb::BrowserAction::none);
    select = named(list, mb::name_select);
    OA_CHECK(select != nullptr && !select->enabled);
    // Its card is greyed with the problem chip, and the footer gives the reason.
    bool greyed = false;
    bool reason = false;
    for (const kit::Item& item : list.items) {
        if (const auto* card = std::get_if<kit::CardLook>(&item.look);
            card != nullptr && card->title == "Check Clash")
            greyed = card->greyed && card->chips.size() == 2 &&
                     card->chips[1].state == kit::ChipState::problem;
        if (item.role == kit::Role::text && item.text.find("New names") != std::string::npos)
            reason = true;
    }
    OA_CHECK(greyed);
    OA_CHECK(reason);

    // Enter on the focused selected card acts; Escape cancels.
    state.interaction.focused = kit::control_named(list, "maps-card-check-isle-oa-check-isles");
    state.interaction.focus_shown = true;
    result = mb::browser_key(browser, state, list, kit::Key::enter);
    OA_CHECK(result.action == mb::BrowserAction::none);
    OA_CHECK(browser.cards[*browser.selected].name == "check_isle@oa-check-isles");
    list = laid_out(browser, state, frame);
    result = mb::browser_key(browser, state, list, kit::Key::enter);
    OA_CHECK(result.action == mb::BrowserAction::choose);
    result = mb::browser_key(browser, state, list, kit::Key::escape);
    OA_CHECK(result.action == mb::BrowserAction::cancel);
    result = press(browser, state, list, mb::name_cancel);
    OA_CHECK(result.action == mb::BrowserAction::cancel);
}

void wheel_and_drag_scroll() {
    const kit::Frame frame = frames()[0].frame;
    mb::Browser browser = many_maps();
    mb::ScreenState state;
    kit::DisplayList list = laid_out(browser, state, frame);
    std::ignore = mb::browser_wheel(browser, state, list, -1.0F);
    OA_CHECK(state.scroll > 0);
    const int32_t wheeled = state.scroll;
    list = laid_out(browser, state, frame);
    std::ignore = mb::browser_wheel(browser, state, list, 1.0F);
    OA_CHECK(state.scroll < wheeled);
    // A finger's drag up the grid scrolls it, and selects nothing.
    state.scroll = 0;
    list = laid_out(browser, state, frame);
    const std::vector<const kit::Control*> cards = card_controls(list);
    const kit::Rect first = visible_part(cards[0]->rect, cards[0]->clip);
    const kit::Point from{first.x + first.width / 2, first.y + first.height / 2};
    std::ignore = mb::browser_pointer(browser, state, list, mb::PointerKind::down, 1, from, 8);
    std::ignore = mb::browser_pointer(
        browser, state, list, mb::PointerKind::move, 0, {from.x, from.y - 40}, 8
    );
    const mb::ScreenResult result =
        mb::browser_pointer(browser, state, list, mb::PointerKind::up, 1, {from.x, from.y - 40}, 8);
    OA_CHECK(state.scroll == 40);
    OA_CHECK(result.action == mb::BrowserAction::none);
    OA_CHECK(!browser.selected);
    // Page Down and Home scroll it too.
    list = laid_out(browser, state, frame);
    std::ignore = mb::browser_key(browser, state, list, kit::Key::page_down);
    OA_CHECK(state.scroll > 40);
    list = laid_out(browser, state, frame);
    std::ignore = mb::browser_key(browser, state, list, kit::Key::home);
    OA_CHECK(state.scroll == 0);
}

void header_and_footer_words() {
    const kit::Frame frame = frames()[2].frame;
    mb::Browser browser = many_maps();
    for (std::size_t index = 0; index < browser.cards.size(); ++index)
        if (browser.cards[index].name == "isle_of_ashes@archipelago") {
            browser.selected = index;
            browser.cards[index].pack_name = "Archipelago Pack";
            browser.cards[index].pack_version = "1.2";
            browser.cards[index].registry_name = "Example Maps";
            browser.cards[index].mod_fit = mb::MapCard::Fit::unknown;
        }
    const kit::DisplayList list = laid_out(browser, {}, frame);
    bool header = false;
    bool details = false;
    std::vector<std::string> chips;
    for (const kit::Item& item : list.items) {
        if (const auto* look = std::get_if<kit::HeaderLook>(&item.look))
            header = look->title == "Maps" && look->version == "Skirmish · playing Ridge";
        if (item.role == kit::Role::text &&
            item.text == "Archipelago Pack 1.2 · Example Maps · 4 players · 16×16 · 2.3 MB")
            details = true;
        if (const auto* chip = std::get_if<kit::ChipLook>(&item.look);
            chip != nullptr && item.control == kit::no_control)
            chips.push_back(chip->text);
    }
    OA_CHECK(header);
    OA_CHECK(details);
    OA_CHECK(
        std::find(chips.begin(), chips.end(), "Fits TA 3.1c ✓") != chips.end() &&
        std::find(chips.begin(), chips.end(), "Fit with Ridge is checked when it's installed") !=
            chips.end()
    );
    const kit::Control* select = named(list, mb::name_select);
    OA_CHECK(select != nullptr && select->enabled && select->text == "Download and select");
}

} // namespace

void layout_tests() {
    columns_at_each_class();
    nothing_outside_the_window();
    names_and_buttons();
    pictures_asked_for_the_view_alone();
    focus_moves_by_geometry();
    tab_follows_the_declared_order();
    pointer_and_keys();
    wheel_and_drag_scroll();
    header_and_footer_words();
}
