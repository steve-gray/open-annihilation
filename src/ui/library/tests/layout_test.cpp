// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's screens over the fixture of fifteen packages, laid out with
// a synthetic font at Compact, Regular and Large, and at the windows of
// 640x480 (1x), 1920x1080 (2x) and 2560x1440 (2x). At each frame: every
// part lies inside the window; no two controls meet, nor their press areas;
// every action of the selected entry is a control (Compact: on its details
// page, MORE's open); UPDATE ALL is one on the Updates tab, SETTINGS... and
// CLOSE always are; Tab follows the declared order and the arrows reach
// every control that takes the focus; the names are step 8's and unique;
// every text fits, and the Rules fact is shown whole. Then the keys, the
// controller's buttons, the pointer, a finger, the wheel and typing, each
// changing the model as the screen promises, and no press on a row
// getting anything.
//
// The synthetic font's glyph for byte b is 2 + b % 4 columns by 8 rows.

#include "oa/formats/fnt.hpp"
#include "oa/test/check.hpp"
#include "oa/ui/frontend_renderer.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/library/library.hpp"
#include "oa/ui/library/screen.hpp"
#include "oa/ui/library/text.hpp"
#include "screen_fixture.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace kit = oa::ui::kit;
namespace lib = oa::ui::library;
namespace renderer = oa::ui::frontend_renderer;
namespace fixture = screen_fixture;

/// The synthetic font of the kit's tests, for both roles.
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

/// The fonts every layout here is measured in.
///
/// @return the fonts
const kit::Fonts& fonts() {
    static const kit::Fonts loaded = block_fonts();
    return loaded;
}

/// Prints each problem a check found, and fails the check when there are any.
///
/// @param problems the problems
/// @param where the frame or case they belong to
/// @param file the check's file
/// @param line the check's line
void expect_none(
    const std::vector<std::string>& problems, const std::string& where, const char* file, int line
) {
    for (const std::string& problem : problems)
        std::fprintf(stderr, "%s:%d: %s: %s\n", file, line, where.c_str(), problem.c_str());
    if (!problems.empty())
        ++oa::test::failed_checks();
}

#define OA_EXPECT_NONE(problems, where) expect_none((problems), (where), __FILE__, __LINE__)

/// Returns the fixture's Library with Ridge, the update that changes the rules, selected.
///
/// @return the Library
lib::Library with_ridge() {
    lib::Library library = fixture::fixture_library();
    lib::select(library, fixture::ridge_id());
    return library;
}

/// Lays the Library out in the synthetic font.
///
/// @param library the model
/// @param state the screen's state
/// @param frame the frame
/// @return the display list
kit::DisplayList
laid_out(const lib::Library& library, const lib::ScreenState& state, const kit::Frame& frame) {
    return lib::library_layout(library, state, frame, fonts());
}

/// Returns a control's number by its name, or no_control.
///
/// @param list the display list
/// @param name the name
/// @return the number
kit::ControlId id_of(const kit::DisplayList& list, std::string_view name) {
    return kit::control_named(list, name);
}

/// Returns a control's centre.
///
/// @param control the control
/// @return the point
kit::Point centre_of(const kit::Control& control) {
    const kit::Rect area = fixture::press_area(control);
    return {area.x + area.width / 2, area.y + area.height / 2};
}

/// Tells whether a name starts with a prefix.
///
/// @param name the name
/// @param prefix the prefix
/// @return true when it does
bool starts(std::string_view name, std::string_view prefix) {
    return name.substr(0, prefix.size()) == prefix;
}

/// Tells whether a control is the list's or one of its rows: the rows lie
/// in the list that holds them, the one pair of controls that meet.
///
/// @param a a control
/// @param b another
/// @return true for the list and a row
bool list_and_row(const kit::Control& a, const kit::Control& b) {
    const auto pair = [](const kit::Control& list, const kit::Control& row) {
        return list.name == "library.list" && starts(row.name, "library.row.");
    };
    return pair(a, b) || pair(b, a);
}

/// Lists the pairs of controls that meet, by their rectangles and by their press areas.
///
/// @param list the display list
/// @return a line for each pair
std::vector<std::string> meeting_controls(const kit::DisplayList& list) {
    std::vector<std::string> problems;
    for (std::size_t a = 0; a < list.controls.size(); ++a) {
        for (std::size_t b = a + 1; b < list.controls.size(); ++b) {
            const kit::Control& first = list.controls[a];
            const kit::Control& second = list.controls[b];
            if (list_and_row(first, second))
                continue;
            if (fixture::has_room(kit::intersect(first.rect, second.rect)))
                problems.push_back(first.name + " meets " + second.name);
            if (fixture::has_room(
                    kit::intersect(fixture::press_area(first), fixture::press_area(second))
                ))
                problems.push_back(first.name + "'s press area meets " + second.name + "'s");
        }
    }
    return problems;
}

/// Returns the place a control's name gives it in the declared order: the
/// tabs, the search, the filters, the list, the details' actions, then the
/// footer's buttons.
///
/// @param name the name
/// @return the place, or -1 for a name of none
int declared_place(std::string_view name) {
    if (starts(name, "library.tab."))
        return 0;
    if (name == "library.search")
        return 1;
    if (name == "library.filter" || starts(name, "library.filter.") ||
        starts(name, "library.tag.") || name == "library.tags" ||
        name == "library.action.update-all")
        return 2;
    if (name == "library.list")
        return 3;
    if (name == "library.action" || starts(name, "library.action.") || name == "library.back")
        return 4;
    if (name == "library.settings" || name == "library.details" || name == "library.close")
        return 5;
    return -1;
}

/// Lists how Tab's order differs from the declared one: every control
/// that takes the focus in it, by place, and within a place top to bottom
/// and left to right; and how the arrows, searched from the first stop,
/// fail to reach every one of them.
///
/// @param list the display list
/// @return a line for each difference
std::vector<std::string> order_problems(const kit::DisplayList& list) {
    std::vector<std::string> problems;
    std::set<kit::ControlId> takes_focus;
    for (const kit::Control& control : list.controls)
        if (control.enabled && control.focusable)
            takes_focus.insert(control.id);
    const std::set<kit::ControlId> ordered(list.tab_order.begin(), list.tab_order.end());
    if (ordered != takes_focus || ordered.size() != list.tab_order.size())
        problems.emplace_back("Tab's order is not every control that takes the focus, once");
    const kit::Control* previous = nullptr;
    for (const kit::ControlId id : list.tab_order) {
        const kit::Control* control = kit::control_of(list, id);
        if (control == nullptr) {
            problems.emplace_back("Tab stops on a control the list lacks");
            continue;
        }
        const int place = declared_place(control->name);
        if (place < 0)
            problems.push_back(control->name + " is in no declared place");
        if (previous != nullptr) {
            const int before = declared_place(previous->name);
            const bool later_place = place > before;
            const bool same_place_after =
                place == before &&
                (control->rect.y > previous->rect.y ||
                 (control->rect.y == previous->rect.y && control->rect.x > previous->rect.x));
            if (!later_place && !same_place_after)
                problems.push_back(control->name + " follows " + previous->name);
        }
        previous = control;
    }
    if (list.tab_order.empty())
        return problems;
    std::set<kit::ControlId> reached{list.tab_order.front()};
    std::deque<kit::ControlId> waiting{list.tab_order.front()};
    while (!waiting.empty()) {
        const kit::ControlId from = waiting.front();
        waiting.pop_front();
        for (const kit::Direction direction :
             {kit::Direction::up,
              kit::Direction::down,
              kit::Direction::left,
              kit::Direction::right}) {
            const kit::ControlId next = kit::focus_toward(list, from, direction);
            if (next != kit::no_control && reached.insert(next).second)
                waiting.push_back(next);
        }
    }
    for (const kit::ControlId id : takes_focus)
        if (reached.count(id) == 0)
            problems.push_back("the arrows do not reach " + kit::control_of(list, id)->name);
    return problems;
}

/// Tells whether a name is one step 8 of the screen's design names.
///
/// @param name the name
/// @return true when it is
bool designed_name(const std::string& name) {
    static const std::regex pattern(
        "library\\.(tab\\.(mods|maps|languages|updates)|search|filter|"
        "filter\\.(all|installed|updates|tag-[a-z0-9-]+)|tag\\.[a-z0-9-]+|tags|tags\\.[a-z0-9-]+|"
        "list|row\\.[a-z0-9-]+(\\.[a-z0-9-]+)?|action|"
        "action\\.(get|update|update-all|cancel|retry|roll-back|play-now|open-folder|homepage)|"
        "details|back|settings|close|status|queue|note|from|rules)"
    );
    return std::regex_match(name, pattern);
}

/// The labels: lines a journey reads, which take nothing.
constexpr std::array<std::string_view, 5> label_names{
    "library.status", "library.queue", "library.note", "library.from", "library.rules"
};

/// Lists what is wrong with the controls' names: the kit's rules, step 8's
/// names, and the labels' kind.
///
/// @param list the display list
/// @return a line for each fault
std::vector<std::string> name_problems(const kit::DisplayList& list) {
    std::vector<std::string> problems;
    const std::string kit_problem = kit::name_problem(list);
    if (!kit_problem.empty())
        problems.push_back(kit_problem);
    for (const kit::Control& control : list.controls)
        if (!designed_name(control.name))
            problems.push_back(control.name + " is not one of the screen's names");
    std::set<std::string> listed;
    for (const kit::AutomationEntry& entry : kit::automation_parts(list, {}))
        if (!listed.insert(entry.name).second)
            problems.push_back(entry.name + " is listed twice to automation");
    for (const std::string_view name : label_names) {
        const kit::Control* label = fixture::named(list, name);
        if (label == nullptr)
            continue;
        if (label->kind != kit::ControlKind::area || label->enabled || label->focusable)
            problems.push_back(std::string(name) + " takes a press, a key or Tab");
    }
    return problems;
}

/// Returns the fact of a label.
///
/// @param library the model
/// @param entry the entry
/// @param label the long label, in English
/// @return the fact
std::optional<lib::Fact>
fact_of(const lib::Library& library, const lib::Entry& entry, std::string_view label) {
    for (const lib::Fact& fact : lib::facts(library, entry))
        if (fact.long_label == label)
            return fact;
    return std::nullopt;
}

/// Returns the words of the text items that lie in a rectangle, top to
/// bottom, joined by spaces.
///
/// @param list the display list
/// @param box the rectangle
/// @return the words
std::string words_in(const kit::DisplayList& list, const kit::Rect& box) {
    std::string words;
    for (const kit::Item& item : list.items) {
        if (item.role != kit::Role::text || !kit::wholly_in(item.rect, box))
            continue;
        if (!words.empty())
            words += ' ';
        words += item.text;
    }
    return words;
}

/// Lists how the selected entry's Rules and From facts are not shown whole.
///
/// @param library the model, with an entry selected
/// @param list the display list showing its details
/// @param long_form the details show the long values
/// @return a line for each fault
std::vector<std::string>
whole_fact_problems(const lib::Library& library, const kit::DisplayList& list, bool long_form) {
    std::vector<std::string> problems;
    const lib::Entry* entry = lib::selected_entry(library);
    if (entry == nullptr)
        return {"nothing is selected"};
    const std::optional<lib::Fact> rules = fact_of(library, *entry, "Rules");
    const kit::Control* rules_label = fixture::named(list, "library.rules");
    if (!rules || rules_label == nullptr) {
        problems.emplace_back("the Rules fact is not shown");
    } else {
        const std::string& value = long_form ? rules->long_value : rules->short_value;
        if (rules_label->text != value)
            problems.push_back(
                "library.rules reads \"" + rules_label->text + "\", not \"" + value + "\""
            );
        if (words_in(list, rules_label->rect) != value)
            problems.push_back(
                "the Rules fact shows \"" + words_in(list, rules_label->rect) + "\""
            );
    }
    const std::optional<lib::Fact> from = fact_of(library, *entry, "From");
    const kit::Control* from_label = fixture::named(list, "library.from");
    if (!from || from_label == nullptr)
        problems.emplace_back("the From fact is not shown");
    else if (from_label->text != from->long_value)
        problems.push_back("library.from reads \"" + from_label->text + "\"");
    return problems;
}

/// Counts the rows a list shows.
///
/// @param list the display list
/// @return how many
std::size_t row_count(const kit::DisplayList& list) {
    return static_cast<std::size_t>(
        std::count_if(list.controls.begin(), list.controls.end(), [](const kit::Control& control) {
            return starts(control.name, "library.row.");
        })
    );
}

/// Returns the word an action's button is named by.
///
/// @param action the action
/// @return the word
std::string_view word_of(lib::Action action) {
    switch (action) {
    case lib::Action::get:
        return "get";
    case lib::Action::update:
        return "update";
    case lib::Action::cancel:
        return "cancel";
    case lib::Action::retry:
        return "retry";
    case lib::Action::roll_back:
        return "roll-back";
    case lib::Action::play_now:
        return "play-now";
    case lib::Action::open_folder:
        return "open-folder";
    case lib::Action::homepage:
        return "homepage";
    default:
        return "other";
    }
}

/// Checks a frame's layouts: the list page, the details at Regular and
/// Large or the details page at Compact, and the Updates tab.
///
/// @param named the frame
void check_frame(const fixture::NamedFrame& named) {
    const kit::Frame& frame = named.frame;
    const bool compact = frame.size_class == kit::SizeClass::compact;
    const kit::Point window = lib::window_size(frame);
    OA_CHECK(window.x <= frame.area.width && window.y <= frame.area.height);

    lib::Library library = with_ridge();
    lib::ScreenState state;
    const kit::DisplayList list = laid_out(library, state, frame);
    OA_EXPECT_NONE(fixture::outside_window(list, window), named.name + " list");
    OA_EXPECT_NONE(meeting_controls(list), named.name + " list");
    OA_EXPECT_NONE(order_problems(list), named.name + " list");
    OA_EXPECT_NONE(name_problems(list), named.name + " list");
    OA_EXPECT_NONE(fixture::texts_not_fitting(list, fonts()), named.name + " list");
    OA_CHECK(fixture::named(list, "library.settings") != nullptr);
    OA_CHECK(fixture::named(list, "library.close") != nullptr);
    OA_CHECK(fixture::named(list, "library.list") != nullptr);
    OA_CHECK(fixture::named(list, "library.queue") != nullptr);

    const lib::Entry* ridge = lib::find_entry(library, fixture::ridge_id());
    OA_CHECK(ridge != nullptr);
    if (ridge == nullptr)
        return;
    const std::vector<lib::ActionButton> actions = lib::entry_actions(library, *ridge);
    OA_CHECK(actions.size() == 5);
    if (!compact) {
        for (const lib::ActionButton& action : actions)
            OA_CHECK(
                fixture::named(list, "library.action." + std::string(word_of(action.action))) !=
                nullptr
            );
        OA_EXPECT_NONE(whole_fact_problems(library, list, true), named.name + " details");
        OA_CHECK(fixture::named(list, "library.status") != nullptr);
        OA_CHECK(fixture::named(list, "library.note") != nullptr);
    } else {
        // Compact's details are a page of their own: the first three actions,
        // and MORE ▾ holding the rest.
        lib::open_details(library);
        lib::ScreenState page_state;
        page_state.details_page = true;
        const kit::DisplayList page = laid_out(library, page_state, frame);
        OA_CHECK(fixture::named(page, "library.back") != nullptr);
        OA_EXPECT_NONE(fixture::outside_window(page, window), named.name + " details page");
        OA_EXPECT_NONE(meeting_controls(page), named.name + " details page");
        OA_EXPECT_NONE(order_problems(page), named.name + " details page");
        OA_EXPECT_NONE(name_problems(page), named.name + " details page");
        OA_EXPECT_NONE(fixture::texts_not_fitting(page, fonts()), named.name + " details page");
        OA_EXPECT_NONE(whole_fact_problems(library, page, false), named.name + " details page");
        OA_CHECK(fixture::named(page, "library.close") != nullptr);
        OA_CHECK(fixture::named(page, "library.status") != nullptr);
        OA_CHECK(fixture::named(page, "library.note") != nullptr);
        for (std::size_t index = 0; index < 3; ++index)
            OA_CHECK(
                fixture::named(
                    page, "library.action." + std::string(word_of(actions[index].action))
                ) != nullptr
            );
        OA_CHECK(fixture::named(page, "library.action") != nullptr);
        page_state.menu = lib::MenuKind::more;
        const kit::DisplayList more = laid_out(library, page_state, frame);
        for (const lib::ActionButton& action : actions)
            OA_CHECK(
                fixture::named(more, "library.action." + std::string(word_of(action.action))) !=
                nullptr
            );
        OA_EXPECT_NONE(name_problems(more), named.name + " details page, MORE open");
        OA_EXPECT_NONE(
            fixture::outside_window(more, window), named.name + " details page, MORE open"
        );
        OA_EXPECT_NONE(
            fixture::texts_not_fitting(more, fonts()), named.name + " details page, MORE open"
        );
    }

    lib::Library updates = with_ridge();
    lib::set_tab(updates, lib::Tab::updates);
    const kit::DisplayList updates_list = laid_out(updates, state, frame);
    const kit::Control* update_all = fixture::named(updates_list, "library.action.update-all");
    OA_CHECK(update_all != nullptr && update_all->enabled);
    OA_CHECK(fixture::named(updates_list, "library.settings") != nullptr);
    OA_CHECK(fixture::named(updates_list, "library.close") != nullptr);
    OA_EXPECT_NONE(fixture::outside_window(updates_list, window), named.name + " updates");
    OA_EXPECT_NONE(meeting_controls(updates_list), named.name + " updates");
    OA_EXPECT_NONE(order_problems(updates_list), named.name + " updates");
    OA_EXPECT_NONE(name_problems(updates_list), named.name + " updates");
    OA_EXPECT_NONE(fixture::texts_not_fitting(updates_list, fonts()), named.name + " updates");
    const kit::Control* updates_tab = fixture::named(updates_list, "library.tab.updates");
    OA_CHECK(updates_tab != nullptr && updates_tab->text == (compact ? "Upd 2" : "Updates 2"));
}

/// Checks every frame.
void every_frame_holds_its_invariants() {
    for (const fixture::NamedFrame& named : fixture::frames())
        check_frame(named);
}

/// Regular shows more rows than Compact, and Large has the filter pane.
void larger_classes_show_more() {
    const lib::Library library = with_ridge();
    const lib::ScreenState state;
    const std::vector<fixture::NamedFrame> all = fixture::frames();
    const kit::DisplayList compact = laid_out(library, state, all[0].frame);
    const kit::DisplayList regular = laid_out(library, state, all[1].frame);
    const kit::DisplayList large = laid_out(library, state, all[2].frame);
    OA_CHECK(row_count(regular) > row_count(compact));
    OA_CHECK(row_count(compact) >= 1);
    const auto has_nav = [](const kit::DisplayList& list) {
        return std::any_of(list.items.begin(), list.items.end(), [](const kit::Item& item) {
            return item.role == kit::Role::nav;
        });
    };
    OA_CHECK(has_nav(large));
    OA_CHECK(!has_nav(regular) && !has_nav(compact));
    const kit::Control* all_entry = fixture::named(large, "library.filter.all");
    OA_CHECK(all_entry != nullptr && all_entry->kind == kit::ControlKind::tab);
    OA_CHECK(fixture::named(regular, "library.filter.all") != nullptr);
    OA_CHECK(fixture::named(compact, "library.filter") != nullptr);
    // Compact's tabs take their short names.
    const kit::Control* languages = fixture::named(compact, "library.tab.languages");
    OA_CHECK(languages != nullptr && languages->text == "Lang");
    const kit::Control* updates = fixture::named(compact, "library.tab.updates");
    OA_CHECK(updates != nullptr && updates->text == "Upd 2");
}

/// A row reads its entry's name, and a row's chip says its state.
void rows_name_their_entries() {
    const lib::Library library = with_ridge();
    const kit::DisplayList list = laid_out(library, {}, fixture::frames()[1].frame);
    const kit::Control* ridge = fixture::named(list, "library.row.fixture-core.ridge");
    OA_CHECK(
        ridge != nullptr && ridge->text == "Ridge" && ridge->kind == kit::ControlKind::list_item
    );
    OA_CHECK(ridge != nullptr && ridge->checked);
    const kit::Control* frontier = fixture::named(list, "library.row.example-mods.frontier");
    OA_CHECK(frontier != nullptr && !frontier->checked);
    const kit::Control* pane = fixture::named(list, "library.list");
    OA_CHECK(pane != nullptr && pane->kind == kit::ControlKind::list && pane->text.empty());
    OA_CHECK(lib::badge_letters("Ridge") == "RID");
    OA_CHECK(lib::badge_letters("TA Zero") == "TZ");
    OA_CHECK(lib::badge_letters("Star of the Sea Pack") == "SOT");
    OA_CHECK(lib::badge_letters("ab") == "AB");
}

/// An empty list says why, and a list the last fetch could not refresh says so above it.
void empty_and_offline_lists_say_why() {
    lib::Library library = with_ridge();
    lib::set_query(library, "zzz");
    const kit::DisplayList list = laid_out(library, {}, fixture::frames()[0].frame);
    bool found = false;
    for (const kit::Item& item : list.items)
        found = found || (item.role == kit::Role::text && item.text == "Nothing matches \"zzz\".");
    OA_CHECK(found);
    OA_CHECK(row_count(list) == 0);

    lib::Inputs inputs = fixture::fixture_inputs();
    inputs.offline = true;
    lib::Library offline;
    lib::refresh(offline, std::move(inputs));
    const kit::DisplayList offline_list = laid_out(offline, {}, fixture::frames()[0].frame);
    bool noted = false;
    for (const kit::Item& item : offline_list.items)
        noted = noted ||
                (item.role == kit::Role::text &&
                 item.text == "Showing the list from 4 min ago. OA couldn't reach the registries.");
    OA_CHECK(noted);
    OA_EXPECT_NONE(fixture::outside_window(offline_list, {480, 324}), "offline");
    OA_EXPECT_NONE(meeting_controls(offline_list), "offline");

    lib::Library none;
    lib::refresh(none, {});
    const kit::DisplayList none_list = laid_out(none, {}, fixture::frames()[0].frame);
    bool told = false;
    for (const kit::Item& item : none_list.items)
        told =
            told || (item.role == kit::Role::text &&
                     item.text == "Nothing here yet. Turn a registry on in Settings › Downloads.");
    OA_CHECK(told);
}

/// Returns a state whose focus shows on a named control.
///
/// @param list the display list
/// @param name the control's name
/// @return the state
lib::ScreenState focused_on(const kit::DisplayList& list, std::string_view name) {
    lib::ScreenState state;
    state.interaction.focused = id_of(list, name);
    state.interaction.focus_shown = true;
    return state;
}

/// Enter on the list opens the details page at Compact, and moves the focus
/// to the first enabled action at Regular.
void enter_opens_details_or_reaches_the_actions() {
    const std::vector<fixture::NamedFrame> all = fixture::frames();
    lib::Library library = with_ridge();
    kit::DisplayList list = laid_out(library, {}, all[0].frame);
    lib::ScreenState state = focused_on(list, "library.list");
    lib::ScreenResult result = lib::library_key(library, state, list, kit::Key::enter);
    OA_CHECK(result.redraw && !result.action);
    OA_CHECK(library.details_open && state.details_page);
    list = laid_out(library, state, all[0].frame);
    OA_CHECK(fixture::named(list, "library.back") != nullptr);
    OA_CHECK(state.interaction.focused == id_of(list, "library.action.update"));

    lib::Library wide = with_ridge();
    kit::DisplayList wide_list = laid_out(wide, {}, all[1].frame);
    lib::ScreenState wide_state = focused_on(wide_list, "library.list");
    result = lib::library_key(wide, wide_state, wide_list, kit::Key::enter);
    OA_CHECK(result.redraw && !result.action);
    OA_CHECK(!wide.details_open);
    OA_CHECK(wide_state.interaction.focused == id_of(wide_list, "library.action.update"));
}

/// Escape clears a search that has the focus, else leaves the details page,
/// else closes.
void escape_clears_leaves_or_closes() {
    const kit::Frame compact = fixture::frames()[0].frame;
    lib::Library library = with_ridge();
    kit::DisplayList list = laid_out(library, {}, compact);
    lib::ScreenState state = focused_on(list, "library.search");
    OA_CHECK(lib::library_text(library, state, list, "ri").redraw);
    OA_CHECK(library.query == "ri" && state.search.text == "ri");
    list = laid_out(library, state, compact);
    lib::ScreenResult result = lib::library_key(library, state, list, kit::Key::escape);
    OA_CHECK(!result.action && library.query.empty() && state.search.text.empty());

    lib::select(library, fixture::ridge_id());
    lib::open_details(library);
    state = {};
    state.details_page = true;
    list = laid_out(library, state, compact);
    result = lib::library_key(library, state, list, kit::Key::escape);
    OA_CHECK(!result.action && !library.details_open && !state.details_page);

    list = laid_out(library, state, compact);
    result = lib::library_key(library, state, list, kit::Key::escape);
    OA_CHECK(result.action == lib::Action::close);
}

/// Ctrl+Tab and Ctrl+Shift+Tab, and the controller's RB and LB, change tabs
/// both ways round.
void tab_commands_change_tabs() {
    lib::Library library = with_ridge();
    lib::ScreenState state;
    const kit::Frame regular = fixture::frames()[1].frame;
    kit::DisplayList list = laid_out(library, state, regular);
    OA_CHECK(lib::library_command(library, state, list, lib::ScreenCommand::next_tab).redraw);
    OA_CHECK(library.tab == lib::Tab::maps);
    list = laid_out(library, state, regular);
    static_cast<void>(lib::library_command(library, state, list, lib::ScreenCommand::previous_tab));
    OA_CHECK(library.tab == lib::Tab::mods);
    list = laid_out(library, state, regular);
    static_cast<void>(lib::library_command(library, state, list, lib::ScreenCommand::previous_tab));
    OA_CHECK(library.tab == lib::Tab::updates);
    list = laid_out(library, state, regular);
    static_cast<void>(lib::library_command(library, state, list, lib::ScreenCommand::next_tab));
    OA_CHECK(library.tab == lib::Tab::mods);
}

/// The controller's X asks for the selected entry's first action when it is
/// GET, UPDATE, CANCEL or RETRY and enabled; Y gives the search the focus;
/// B leaves the details page, else closes.
void controller_buttons() {
    const kit::Frame compact = fixture::frames()[0].frame;
    lib::Library library = with_ridge();
    lib::ScreenState state;
    kit::DisplayList list = laid_out(library, state, compact);
    lib::ScreenResult result =
        lib::library_command(library, state, list, lib::ScreenCommand::first_action);
    OA_CHECK(result.action == lib::Action::update && result.entry == fixture::ridge_id());

    lib::select(library, {std::string(fixture::added), lib::Kind::mod, "hollow"});
    result = lib::library_command(library, state, list, lib::ScreenCommand::first_action);
    OA_CHECK(!result.action);
    lib::select(library, {std::string(fixture::core), lib::Kind::mod, "bastion"});
    result = lib::library_command(library, state, list, lib::ScreenCommand::first_action);
    OA_CHECK(!result.action);
    lib::select(library, {std::string(fixture::core), lib::Kind::mod, "ta-zero"});
    result = lib::library_command(library, state, list, lib::ScreenCommand::first_action);
    OA_CHECK(result.action == lib::Action::cancel);

    result = lib::library_command(library, state, list, lib::ScreenCommand::find);
    list = laid_out(library, state, compact);
    OA_CHECK(
        state.interaction.focus_shown && state.interaction.focused == id_of(list, "library.search")
    );
    OA_CHECK(lib::focused_field(state, list).has_value());

    lib::select(library, fixture::ridge_id());
    lib::open_details(library);
    state = {};
    state.details_page = true;
    list = laid_out(library, state, compact);
    result = lib::library_command(library, state, list, lib::ScreenCommand::back);
    OA_CHECK(!result.action && !library.details_open && !state.details_page);
    list = laid_out(library, state, compact);
    result = lib::library_command(library, state, list, lib::ScreenCommand::back);
    OA_CHECK(result.action == lib::Action::close);
}

/// On the list, Up and Down move the selection and keep the focus; past the
/// last row the focus leaves the list. Page Down moves by the rows shown,
/// Home and End go to the ends, and the list scrolls to keep the selection.
void list_keys_move_the_selection() {
    const kit::Frame compact = fixture::frames()[0].frame;
    lib::Library library = fixture::fixture_library();
    kit::DisplayList list = laid_out(library, {}, compact);
    lib::ScreenState state = focused_on(list, "library.list");
    const kit::ControlId pane = state.interaction.focused;
    const auto selected_place = [&library]() {
        for (std::size_t place = 0; place < library.visible.size(); ++place)
            if (library.entries[library.visible[place]].id == *library.selected)
                return place;
        return library.visible.size();
    };
    OA_CHECK(selected_place() == 0);
    OA_CHECK(lib::library_key(library, state, list, kit::Key::down).redraw);
    OA_CHECK(selected_place() == 1 && state.interaction.focused == pane);
    const std::size_t shown_rows = row_count(list);
    list = laid_out(library, state, compact);
    static_cast<void>(lib::library_key(library, state, list, kit::Key::page_down));
    OA_CHECK(selected_place() == 1 + shown_rows);
    OA_CHECK(state.list_scroll > 0);
    list = laid_out(library, state, compact);
    static_cast<void>(lib::library_key(library, state, list, kit::Key::home));
    OA_CHECK(selected_place() == 0 && state.list_scroll == 0);
    list = laid_out(library, state, compact);
    static_cast<void>(lib::library_key(library, state, list, kit::Key::end));
    OA_CHECK(selected_place() + 1 == library.visible.size());
    list = laid_out(library, state, compact);
    OA_CHECK(
        fixture::named(list, "library.row.fixture-core.quarry") != nullptr || row_count(list) > 0
    );
    static_cast<void>(lib::library_key(library, state, list, kit::Key::down));
    OA_CHECK(selected_place() + 1 == library.visible.size());
    OA_CHECK(state.interaction.focused != pane);
    // Left and Right move by where controls sit, never along Tab's order.
    lib::select(library, fixture::ridge_id());
    list = laid_out(library, state, fixture::frames()[1].frame);
    state = focused_on(list, "library.list");
    static_cast<void>(lib::library_key(library, state, list, kit::Key::right));
    const kit::Control* focused = kit::control_of(list, state.interaction.focused);
    OA_CHECK(focused != nullptr && starts(focused->name, "library.action."));
}

/// Tab and Shift+Tab follow the declared order.
void tab_follows_the_declared_order() {
    lib::Library library = with_ridge();
    const kit::Frame large = fixture::frames()[2].frame;
    kit::DisplayList list = laid_out(library, {}, large);
    lib::ScreenState state;
    std::vector<kit::ControlId> visited;
    for (std::size_t step = 0; step < list.tab_order.size(); ++step) {
        static_cast<void>(lib::library_key(library, state, list, kit::Key::tab));
        visited.push_back(state.interaction.focused);
        list = laid_out(library, state, large);
    }
    OA_CHECK(visited == list.tab_order);
    static_cast<void>(lib::library_key(library, state, list, kit::Key::back_tab));
    OA_CHECK(state.interaction.focused == list.tab_order[list.tab_order.size() - 2]);

    // The orders, by name: Compact's list page and its details page, and
    // Regular's ends.
    const auto names_of = [](const kit::DisplayList& laid) {
        std::vector<std::string> names;
        for (const kit::ControlId id : laid.tab_order)
            names.push_back(kit::control_of(laid, id)->name);
        return names;
    };
    const kit::Frame compact = fixture::frames()[0].frame;
    const std::vector<std::string> compact_order{
        "library.tab.mods",
        "library.tab.maps",
        "library.tab.languages",
        "library.tab.updates",
        "library.search",
        "library.filter",
        "library.list",
        "library.settings",
        "library.details",
        "library.close",
    };
    OA_CHECK(names_of(laid_out(library, {}, compact)) == compact_order);
    lib::open_details(library);
    lib::ScreenState page;
    page.details_page = true;
    const std::vector<std::string> page_order{
        "library.back",
        "library.action.update",
        "library.action.play-now",
        "library.action.roll-back",
        "library.action",
        "library.close",
    };
    OA_CHECK(names_of(laid_out(library, page, compact)) == page_order);
    lib::close_details(library);
    const std::vector<std::string> regular =
        names_of(laid_out(library, {}, fixture::frames()[1].frame));
    const std::vector<std::string> regular_end{
        "library.list",
        "library.action.update",
        "library.action.play-now",
        "library.action.roll-back",
        "library.action.open-folder",
        "library.action.homepage",
        "library.settings",
        "library.close",
    };
    OA_CHECK(regular.size() > regular_end.size() + 5);
    OA_CHECK(std::equal(regular_end.rbegin(), regular_end.rend(), regular.rbegin()));
    OA_CHECK(
        regular.size() > 7 && regular[4] == "library.search" && regular[5] == "library.filter.all"
    );
}

/// Presses a control with the pointer: a press and a release at its centre.
///
/// @param library the model
/// @param state the screen's state
/// @param list the display list
/// @param name the control's name
/// @param finger_reach 0 for a mouse, else a finger's reach
/// @return what the release asked of the host
lib::ScreenResult click(
    lib::Library& library,
    lib::ScreenState& state,
    const kit::DisplayList& list,
    std::string_view name,
    int32_t finger_reach = 0
) {
    const kit::Control* control = fixture::named(list, name);
    OA_CHECK(control != nullptr);
    if (control == nullptr)
        return {};
    const kit::Point at = centre_of(*control);
    static_cast<void>(
        lib::library_pointer(library, state, list, lib::PointerKind::down, 1, at, finger_reach)
    );
    return lib::library_pointer(library, state, list, lib::PointerKind::up, 1, at, finger_reach);
}

/// A click, a double click or a tap on a row selects it and gets nothing; at
/// Compact it opens the row's details page. An action's button asks for its
/// action, and a label takes no press.
void pressing_rows_selects_and_never_gets() {
    const std::vector<fixture::NamedFrame> all = fixture::frames();
    lib::Library library = with_ridge();
    lib::ScreenState state;
    kit::DisplayList list = laid_out(library, state, all[1].frame);
    const lib::EntryId frontier{std::string(fixture::added), lib::Kind::mod, "frontier"};
    lib::ScreenResult result = click(library, state, list, "library.row.example-mods.frontier");
    OA_CHECK(!result.action && library.selected == frontier);
    list = laid_out(library, state, all[1].frame);
    result = click(library, state, list, "library.row.example-mods.frontier");
    OA_CHECK(!result.action && library.selected == frontier);
    result = click(library, state, list, "library.row.example-mods.frontier", 12);
    OA_CHECK(!result.action && library.selected == frontier && !library.details_open);

    list = laid_out(library, state, all[1].frame);
    result = click(library, state, list, "library.action.cancel");
    OA_CHECK(result.action == lib::Action::cancel && result.entry == frontier);
    const kit::Control* status = fixture::named(list, "library.status");
    OA_CHECK(status != nullptr && kit::hit(list, centre_of(*status)) != status->id);

    lib::Library small = with_ridge();
    lib::ScreenState small_state;
    kit::DisplayList small_list = laid_out(small, small_state, all[0].frame);
    const lib::EntryId cinder{std::string(fixture::core), lib::Kind::mod, "cinder"};
    result = click(small, small_state, small_list, "library.row.fixture-core.cinder");
    OA_CHECK(!result.action && small.selected == cinder);
    OA_CHECK(small.details_open && small_state.details_page);
    small_list = laid_out(small, small_state, all[0].frame);
    OA_CHECK(fixture::named(small_list, "library.back") != nullptr);
    result = click(small, small_state, small_list, "library.back");
    OA_CHECK(!result.action && !small.details_open);
    small_list = laid_out(small, small_state, all[0].frame);
    result = click(small, small_state, small_list, "library.settings");
    OA_CHECK(result.action == lib::Action::settings);
    result = click(small, small_state, small_list, "library.close");
    OA_CHECK(result.action == lib::Action::close);
}

/// The wheel scrolls the pane under the pointer; a finger's drag in the
/// list scrolls it and selects nothing.
void wheel_and_drag_scroll() {
    const kit::Frame compact = fixture::frames()[0].frame;
    lib::Library library = with_ridge();
    lib::ScreenState state;
    kit::DisplayList list = laid_out(library, state, compact);
    const kit::Control* pane = fixture::named(list, "library.list");
    OA_CHECK(pane != nullptr);
    if (pane == nullptr)
        return;
    const kit::Point inside = centre_of(*pane);
    OA_CHECK(lib::library_wheel(library, state, list, inside, -1.0F).redraw);
    OA_CHECK(state.list_scroll > 0);
    const int32_t scrolled = state.list_scroll;
    OA_CHECK(!lib::library_wheel(library, state, list, {2, 2}, -1.0F).redraw);
    OA_CHECK(state.list_scroll == scrolled);

    state = {};
    list = laid_out(library, state, compact);
    const std::optional<lib::EntryId> before = library.selected;
    static_cast<void>(
        lib::library_pointer(library, state, list, lib::PointerKind::down, 1, inside, 12)
    );
    static_cast<void>(lib::library_pointer(
        library, state, list, lib::PointerKind::move, 0, {inside.x, inside.y - 40}, 12
    ));
    OA_CHECK(state.list_scroll > 0);
    const lib::ScreenResult result = lib::library_pointer(
        library, state, list, lib::PointerKind::up, 1, {inside.x, inside.y - 40}, 12
    );
    OA_CHECK(!result.action && library.selected == before && !library.details_open);
}

/// Typing searches as it is typed, and Backspace takes a character back.
void typing_searches() {
    const kit::Frame regular = fixture::frames()[1].frame;
    lib::Library library = with_ridge();
    kit::DisplayList list = laid_out(library, {}, regular);
    lib::ScreenState state = focused_on(list, "library.search");
    OA_CHECK(lib::library_text(library, state, list, "f").redraw);
    OA_CHECK(library.query == "f");
    OA_CHECK(lib::library_text(library, state, list, "r").redraw);
    OA_CHECK(library.query == "fr");
    OA_CHECK(
        !library.visible.empty() && library.entries[library.visible.front()].id.key == "frontier"
    );
    OA_CHECK(lib::library_key(library, state, list, kit::Key::backspace).redraw);
    OA_CHECK(library.query == "f");
    lib::ScreenState elsewhere;
    OA_CHECK(!lib::library_text(library, elsewhere, list, "x").redraw);
    OA_CHECK(library.query == "f");
}

/// Compact's filter drop-down opens, lists the filters and the tab's tags,
/// and its items show them; Regular's tag chips turn a tag on and off.
void filters_and_tags() {
    const std::vector<fixture::NamedFrame> all = fixture::frames();
    lib::Library library = with_ridge();
    lib::ScreenState state;
    kit::DisplayList list = laid_out(library, state, all[0].frame);
    static_cast<void>(click(library, state, list, "library.filter"));
    OA_CHECK(state.menu == lib::MenuKind::filter);
    list = laid_out(library, state, all[0].frame);
    OA_CHECK(fixture::named(list, "library.filter.installed") != nullptr);
    OA_CHECK(fixture::named(list, "library.filter.tag-balance") != nullptr);
    OA_EXPECT_NONE(name_problems(list), "filter menu open");
    OA_EXPECT_NONE(fixture::texts_not_fitting(list, fonts()), "filter menu open");
    static_cast<void>(click(library, state, list, "library.filter.installed"));
    OA_CHECK(library.filter == lib::Filter::installed && state.menu == lib::MenuKind::none);
    list = laid_out(library, state, all[0].frame);
    static_cast<void>(click(library, state, list, "library.filter"));
    list = laid_out(library, state, all[0].frame);
    // A press outside an open drop-down closes it and does nothing else.
    const lib::ScreenResult outside = click(library, state, list, "library.close");
    OA_CHECK(!outside.action && state.menu == lib::MenuKind::none);

    lib::Library wide = with_ridge();
    lib::ScreenState wide_state;
    kit::DisplayList wide_list = laid_out(wide, wide_state, all[1].frame);
    static_cast<void>(click(wide, wide_state, wide_list, "library.tag.balance"));
    OA_CHECK(wide.tag == "balance");
    wide_list = laid_out(wide, wide_state, all[1].frame);
    static_cast<void>(click(wide, wide_state, wide_list, "library.tag.balance"));
    OA_CHECK(wide.tag.empty());
    wide_list = laid_out(wide, wide_state, all[1].frame);
    static_cast<void>(click(wide, wide_state, wide_list, "library.filter.updates"));
    OA_CHECK(wide.filter == lib::Filter::updates);
}

/// On Compact's details page the page keys and the arrows never change the
/// entry it shows.
void details_page_keeps_its_entry() {
    const kit::Frame compact = fixture::frames()[0].frame;
    lib::Library library = with_ridge();
    lib::open_details(library);
    lib::ScreenState state;
    state.details_page = true;
    for (const kit::Key pressed :
         {kit::Key::page_down,
          kit::Key::end,
          kit::Key::page_up,
          kit::Key::home,
          kit::Key::down,
          kit::Key::down,
          kit::Key::up}) {
        const kit::DisplayList list = laid_out(library, state, compact);
        static_cast<void>(lib::library_key(library, state, list, pressed));
        OA_CHECK(library.selected == fixture::ridge_id());
        OA_CHECK(library.details_open && state.details_page);
    }
}

/// MORE ▾ on Compact's details page holds the actions after the first
/// three, and choosing one asks for it.
void more_holds_the_rest() {
    const kit::Frame compact = fixture::frames()[0].frame;
    lib::Library library = with_ridge();
    lib::open_details(library);
    lib::ScreenState state;
    state.details_page = true;
    kit::DisplayList list = laid_out(library, state, compact);
    static_cast<void>(click(library, state, list, "library.action"));
    OA_CHECK(state.menu == lib::MenuKind::more);
    list = laid_out(library, state, compact);
    const lib::ScreenResult result = click(library, state, list, "library.action.homepage");
    OA_CHECK(result.action == lib::Action::homepage && result.entry == fixture::ridge_id());
    OA_CHECK(state.menu == lib::MenuKind::none);
}

} // namespace

int main() {
    every_frame_holds_its_invariants();
    larger_classes_show_more();
    rows_name_their_entries();
    empty_and_offline_lists_say_why();
    enter_opens_details_or_reaches_the_actions();
    escape_clears_leaves_or_closes();
    tab_commands_change_tabs();
    controller_buttons();
    list_keys_move_the_selection();
    tab_follows_the_declared_order();
    pressing_rows_selects_and_never_gets();
    wheel_and_drag_scroll();
    typing_searches();
    filters_and_tags();
    more_holds_the_rest();
    details_page_keeps_its_entry();
    return oa::test::check_exit_status();
}
