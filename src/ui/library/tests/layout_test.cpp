// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's screens over the fixture of fifteen packages, laid out with
// a synthetic font at Compact, Regular and Large, and at the windows of
// 640x480 (1x), 1920x1080 (2x) and 2560x1440 (2x). At each frame: every
// part lies inside the window; no two controls meet, nor their press areas;
// every action of the selected entry is a control (Compact: on its details
// page, MORE's open); UPDATE ALL is one on the Updates tab, over its list
// beside the same filters as Mods, and CLOSE always is; the header's
// refresh icon is present and SETTINGS is not a footer button; Tab
// follows the declared order and the arrows reach every control that takes
// the focus; the names are step 8's and unique; every text fits, and the
// Rules fact is shown whole. The Active tab is first, has no search and no
// filters, and its lines take nothing. Then the keys, the controller's
// buttons, the pointer, a finger, the wheel and typing, each changing the
// model as the screen promises, no press on a row getting anything, and a
// row's download arrow and up-arrow pressed as GET and UPDATE.
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
#include <variant>
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

/// Tells whether two controls are the list and one of its rows or a row's
/// state icon, or a row and its own state icon: the rows lie in the list
/// that holds them and each icon in its row, the only controls that meet.
///
/// @param a a control
/// @param b another
/// @return true for the list and a row or an icon, and a row and its icon
bool list_and_row(const kit::Control& a, const kit::Control& b) {
    const auto pair = [](const kit::Control& outer, const kit::Control& inner) {
        if (outer.name == "library.list" && starts(inner.name, "library.row."))
            return true;
        return starts(outer.name, "library.row.") &&
               (inner.name == outer.name + ".get" || inner.name == outer.name + ".update");
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
/// tabs, the search, the filters, UPDATE ALL over the list and the list,
/// the details' actions, the Registries tab's rows, then the footer's buttons.
///
/// @param name the name
/// @return the place, or -1 for a name of none
int declared_place(std::string_view name) {
    if (starts(name, "library.tab."))
        return 0;
    if (name == "library.search")
        return 1;
    if (name == "library.filter" || starts(name, "library.filter.") ||
        starts(name, "library.tag.") || name == "library.tags")
        return 2;
    if (name == "library.action.update-all" || name == "library.list")
        return 3;
    if (name == "library.action" || starts(name, "library.action.") || name == "library.back")
        return 4;
    if (starts(name, "library.registry."))
        return 5;
    if (name == "library.refresh")
        return 0;
    if (name == "library.add-registry" || name == "library.details" || name == "library.close")
        return 6;
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
        "library\\.(tab\\.(active|mods|maps|languages|updates|registries)|search|filter|"
        "filter\\.(all|new|installed|updates|tag-[a-z0-9-]+)|tag\\.[a-z0-9-]+|tags|tags\\.[a-z0-9-]"
        "+|"
        "list|row\\.[a-z0-9-]+(\\.[a-z0-9-]+)?(\\.(get|update))?|action|"
        "action\\.(get|update|update-all|cancel|retry|roll-back|play-now|switch-off|uninstall|"
        "open-folder|homepage)|"
        "details|back|refresh|close|status|queue|note|from|rules|"
        "active\\.(mod|language|map\\.[1-9][0-9]*)|"
        "add-registry|registries\\.(note|notice)|"
        "registry\\.[a-z0-9-]+(\\.(remove|add-mirror))?)"
    );
    return std::regex_match(name, pattern);
}

/// The labels: lines a journey reads, which take nothing.
constexpr std::array<std::string_view, 9> label_names{
    "library.status",
    "library.queue",
    "library.note",
    "library.from",
    "library.rules",
    "library.active.mod",
    "library.active.language",
    "library.registries.note",
    "library.registries.notice",
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
    for (const kit::Control& control : list.controls)
        if (starts(control.name, "library.active.") &&
            (control.kind != kit::ControlKind::area || control.enabled || control.focusable))
            problems.push_back(control.name + " takes a press, a key or Tab");
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

/// Counts the rows a list shows, leaving their state icons out.
///
/// @param list the display list
/// @return how many
std::size_t row_count(const kit::DisplayList& list) {
    return static_cast<std::size_t>(
        std::count_if(list.controls.begin(), list.controls.end(), [](const kit::Control& control) {
            return starts(control.name, "library.row.") &&
                   control.kind == kit::ControlKind::list_item;
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
    case lib::Action::uninstall:
        return "uninstall";
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
    OA_CHECK(fixture::named(list, "library.refresh") != nullptr);
    OA_CHECK(fixture::named(list, "library.settings") == nullptr);
    OA_CHECK(fixture::named(list, "library.close") != nullptr);
    OA_CHECK(fixture::named(list, "library.list") != nullptr);
    OA_CHECK(fixture::named(list, "library.queue") != nullptr);

    const lib::Entry* ridge = lib::find_entry(library, fixture::ridge_id());
    OA_CHECK(ridge != nullptr);
    if (ridge == nullptr)
        return;
    const std::vector<lib::ActionButton> actions = lib::entry_actions(library, *ridge);
    OA_CHECK(actions.size() == 6);
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
    // UPDATE ALL sits over the list, and the filters are the Mods tab's.
    const kit::Control* updates_pane = fixture::named(updates_list, "library.list");
    OA_CHECK(
        updates_pane != nullptr && update_all != nullptr &&
        update_all->rect.y + update_all->rect.height <= updates_pane->rect.y
    );
    if (compact) {
        OA_CHECK(fixture::named(updates_list, "library.filter") != nullptr);
    } else {
        for (const std::string_view word : {"all", "new", "installed", "updates"})
            OA_CHECK(
                fixture::named(updates_list, "library.filter." + std::string(word)) != nullptr
            );
        const kit::Control* chosen = fixture::named(updates_list, "library.filter.updates");
        OA_CHECK(chosen != nullptr && chosen->checked && chosen->text == "Updates (3)");
    }
    OA_CHECK(fixture::named(updates_list, "library.refresh") != nullptr);
    OA_CHECK(fixture::named(updates_list, "library.settings") == nullptr);
    OA_CHECK(fixture::named(updates_list, "library.close") != nullptr);
    OA_EXPECT_NONE(fixture::outside_window(updates_list, window), named.name + " updates");
    OA_EXPECT_NONE(meeting_controls(updates_list), named.name + " updates");
    OA_EXPECT_NONE(order_problems(updates_list), named.name + " updates");
    OA_EXPECT_NONE(name_problems(updates_list), named.name + " updates");
    OA_EXPECT_NONE(fixture::texts_not_fitting(updates_list, fonts()), named.name + " updates");
    const kit::Control* updates_tab = fixture::named(updates_list, "library.tab.updates");
    // Updates counts the updates that can be installed.
    const std::string updates_count = std::to_string(lib::update_count(library));
    OA_CHECK(
        updates_tab != nullptr &&
        updates_tab->text == (compact ? "Upd " : "Updates ") + updates_count
    );

    // The Active tab: first, no search, no filters and no list; its lines
    // are labels, and Tab reaches only the tabs and the footer's buttons.
    lib::Library active = with_ridge();
    lib::set_tab(active, lib::Tab::active);
    const kit::DisplayList active_list = laid_out(active, state, frame);
    OA_EXPECT_NONE(fixture::outside_window(active_list, window), named.name + " active");
    OA_EXPECT_NONE(meeting_controls(active_list), named.name + " active");
    OA_EXPECT_NONE(order_problems(active_list), named.name + " active");
    OA_EXPECT_NONE(name_problems(active_list), named.name + " active");
    OA_EXPECT_NONE(fixture::texts_not_fitting(active_list, fonts()), named.name + " active");
    const kit::Control* active_tab = fixture::named(active_list, "library.tab.active");
    const kit::Control* mods_tab = fixture::named(active_list, "library.tab.mods");
    OA_CHECK(active_tab != nullptr && active_tab->checked && active_tab->text == "Active");
    OA_CHECK(mods_tab != nullptr && active_tab != nullptr && active_tab->rect.x < mods_tab->rect.x);
    for (const std::string_view absent :
         {"library.search",
          "library.filter",
          "library.filter.all",
          "library.list",
          "library.details",
          "library.action.update-all"})
        OA_CHECK(fixture::named(active_list, absent) == nullptr);
    for (const kit::ControlId id : active_list.tab_order) {
        const kit::Control* stop = kit::control_of(active_list, id);
        OA_CHECK(
            stop != nullptr && (starts(stop->name, "library.tab.") || stop->name == "library.close")
        );
    }
    const kit::Control* mod_line = fixture::named(active_list, "library.active.mod");
    OA_CHECK(mod_line != nullptr && mod_line->text == "Highland 0.4.0");
    const kit::Control* language_line = fixture::named(active_list, "library.active.language");
    OA_CHECK(language_line != nullptr && language_line->text == "Polski 1.0");
    const kit::Control* first_map = fixture::named(active_list, "library.active.map.1");
    OA_CHECK(first_map != nullptr && first_map->text == "Ridgeline · 4 players · 16x16");
    const kit::Control* last_map = fixture::named(active_list, "library.active.map.3");
    OA_CHECK(last_map != nullptr && last_map->text == "Atoll · 6 players · 20x20");
    OA_CHECK(fixture::named(active_list, "library.active.map.4") == nullptr);
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
    // Compact's tabs take their short names; Active is the first.
    const kit::Control* first_tab = kit::control_of(compact, compact.tab_order.front());
    OA_CHECK(first_tab != nullptr && first_tab->name == "library.tab.active");
    const kit::Control* languages = fixture::named(compact, "library.tab.languages");
    OA_CHECK(languages != nullptr && languages->text == "Lang 2");
    const kit::Control* updates = fixture::named(compact, "library.tab.updates");
    OA_CHECK(
        updates != nullptr && updates->text == "Upd " + std::to_string(lib::update_count(library))
    );
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

/// A mod of the player's own, which no listing describes, shows its
/// manifest's description in the details.
void an_own_mods_details_show_its_description() {
    lib::Inputs inputs = fixture::fixture_inputs();
    lib::Installed own;
    own.kind = lib::Kind::mod;
    own.key = "own-description";
    own.folder = "own-description";
    own.name = "Own Description";
    own.version = "1.0";
    own.description = "A mod of the player's own.";
    inputs.installed.push_back(own);
    lib::Library library;
    lib::refresh(library, std::move(inputs));
    lib::set_tab(library, lib::Tab::mods);
    lib::select(library, {"", lib::Kind::mod, "own-description"});
    OA_CHECK(library.selected.has_value());
    for (const fixture::NamedFrame& named : fixture::frames()) {
        if (named.frame.size_class == kit::SizeClass::compact)
            continue;
        const kit::DisplayList list = laid_out(library, {}, named.frame);
        bool shown = false;
        for (const kit::Item& item : list.items)
            shown = shown ||
                    (item.role == kit::Role::text && item.text == "A mod of the player's own.");
        if (!shown)
            std::fprintf(stderr, "%s: the description is not in the details\n", named.name.c_str());
        OA_CHECK(shown);
    }
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
    lib::set_tab(offline, lib::Tab::mods);
    const kit::DisplayList offline_list = laid_out(offline, {}, fixture::frames()[0].frame);
    bool noted = false;
    for (const kit::Item& item : offline_list.items)
        noted = noted ||
                (item.role == kit::Role::text &&
                 item.text == "Showing the list from 4 min ago. OA couldn't reach the registries.");
    OA_CHECK(noted);
    bool marked = false;
    for (const kit::Item& item : offline_list.items)
        marked = marked || (item.role == kit::Role::text && item.text == "offline" &&
                            item.colour == kit::colour::lock);
    OA_CHECK(marked);
    OA_EXPECT_NONE(fixture::outside_window(offline_list, {480, 324}), "offline");
    OA_EXPECT_NONE(meeting_controls(offline_list), "offline");

    lib::Library none;
    lib::refresh(none, {});
    lib::set_tab(none, lib::Tab::mods);
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
    OA_CHECK(library.tab == lib::Tab::active);
    list = laid_out(library, state, regular);
    static_cast<void>(lib::library_command(library, state, list, lib::ScreenCommand::previous_tab));
    OA_CHECK(library.tab == lib::Tab::registries);
    list = laid_out(library, state, regular);
    static_cast<void>(lib::library_command(library, state, list, lib::ScreenCommand::next_tab));
    OA_CHECK(library.tab == lib::Tab::active);
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
        "library.tab.active",
        "library.tab.mods",
        "library.tab.maps",
        "library.tab.languages",
        "library.tab.updates",
        "library.tab.registries",
        "library.search",
        "library.filter",
        "library.list",
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
        "library.action.uninstall",
        "library.action.open-folder",
        "library.action.homepage",
        "library.close",
    };
    OA_CHECK(regular.size() > regular_end.size() + 5);
    OA_CHECK(std::equal(regular_end.rbegin(), regular_end.rend(), regular.rbegin()));
    OA_CHECK(
        regular.size() > 9 && regular[6] == "library.search" &&
        regular[7] == "library.filter.all" && regular[8] == "library.filter.new"
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
    result = click(small, small_state, small_list, "library.close");
    OA_CHECK(result.action == lib::Action::close);
    // The header's refresh icon asks for nothing until a registry can be
    // asked, and nothing while the registries are being asked.
    const kit::Control* refresh_off = fixture::named(small_list, "library.refresh");
    OA_CHECK(refresh_off != nullptr && !refresh_off->enabled && refresh_off->text == "Refresh");
    result = click(small, small_state, small_list, "library.refresh");
    OA_CHECK(!result.action);
    OA_CHECK(
        lib::library_command(small, small_state, small_list, lib::ScreenCommand::refresh).action ==
        std::nullopt
    );
    small.inputs.can_check = true;
    small_list = laid_out(small, small_state, all[0].frame);
    const kit::Control* refresh_on = fixture::named(small_list, "library.refresh");
    OA_CHECK(refresh_on != nullptr && refresh_on->enabled && refresh_on->text == "Refresh");
    result = click(small, small_state, small_list, "library.refresh");
    OA_CHECK(result.action == lib::Action::check_now);
    OA_CHECK(
        lib::library_command(small, small_state, small_list, lib::ScreenCommand::refresh).action ==
        lib::Action::check_now
    );
    small.inputs.checking = true;
    small_state.refresh_turn = 2;
    small_list = laid_out(small, small_state, all[0].frame);
    const kit::Control* refreshing = fixture::named(small_list, "library.refresh");
    OA_CHECK(refreshing != nullptr && !refreshing->enabled && refreshing->text == "Refreshing");
    result = click(small, small_state, small_list, "library.refresh");
    OA_CHECK(!result.action);
    bool turning = false;
    for (const kit::Item& item : small_list.items) {
        if (item.role != kit::Role::button || refreshing == nullptr ||
            item.control != refreshing->id)
            continue;
        const auto* look = std::get_if<kit::ButtonLook>(&item.look);
        turning = look != nullptr && look->mark == kit::ButtonMark::refresh && look->turning &&
                  look->turn == 2;
    }
    OA_CHECK(turning);
}

/// A row's state icon: a circled up-arrow pressed as UPDATE and a download
/// arrow pressed as GET, each a button named after its row with the word it
/// replaces as its text, enabled only while that button is, at every size
/// class. A press on one asks for its action for its row's entry and
/// selects it; a blocked or queued one takes no press, which reaches the
/// row instead. An installed row's circled check is no control.
void row_icons_are_get_and_update() {
    for (const fixture::NamedFrame& named : fixture::frames()) {
        lib::Library library = with_ridge();
        lib::ScreenState state;
        kit::DisplayList list = laid_out(library, state, named.frame);
        const kit::Control* row = fixture::named(list, "library.row.fixture-core.ridge");
        const kit::Control* update = fixture::named(list, "library.row.fixture-core.ridge.update");
        OA_CHECK(row != nullptr && update != nullptr);
        if (row == nullptr || update == nullptr)
            continue;
        OA_CHECK(update->kind == kit::ControlKind::button && update->enabled);
        OA_CHECK(update->text == "UPDATE" && !update->focusable);
        OA_CHECK(kit::wholly_in(update->rect, row->rect));
        OA_CHECK(update->rect.width == kit::compact_metrics.state_icon_side);
        OA_CHECK(kit::hit(list, centre_of(*update)) == update->id);
        OA_CHECK(fixture::named(list, "library.row.fixture-core.bastion.get") == nullptr);
        OA_CHECK(fixture::named(list, "library.row.fixture-core.bastion.update") == nullptr);
    }

    const kit::Frame regular = fixture::frames()[1].frame;
    lib::Library library = fixture::fixture_library();
    lib::ScreenState state;
    kit::DisplayList list = laid_out(library, state, regular);
    lib::ScreenResult result = click(library, state, list, "library.row.fixture-core.ridge.update");
    OA_CHECK(result.action == lib::Action::update && result.entry == fixture::ridge_id());
    OA_CHECK(library.selected == fixture::ridge_id() && !library.details_open);

    // A blocked update: dimmed, listed, and no press; a press there selects the row.
    const lib::EntryId ironclad{std::string(fixture::core), lib::Kind::mod, "ironclad"};
    list = laid_out(library, state, regular);
    const kit::Control* blocked = fixture::named(list, "library.row.fixture-core.ironclad.update");
    OA_CHECK(blocked != nullptr && !blocked->enabled && blocked->text == "UPDATE");
    if (blocked != nullptr) {
        const kit::Point at = centre_of(*blocked);
        static_cast<void>(
            lib::library_pointer(library, state, list, lib::PointerKind::down, 1, at, 0)
        );
        result = lib::library_pointer(library, state, list, lib::PointerKind::up, 1, at, 0);
        OA_CHECK(!result.action && library.selected == ironclad);
    }

    // GET: one that can act asks for it; one blocked by missing hacks, and
    // one already queued, take no press.
    lib::set_query(library, "quarry");
    list = laid_out(library, state, regular);
    const kit::Control* get = fixture::named(list, "library.row.fixture-core.quarry.get");
    OA_CHECK(get != nullptr && get->enabled && get->text == "GET");
    const lib::EntryId quarry{std::string(fixture::core), lib::Kind::mod, "quarry"};
    result = click(library, state, list, "library.row.fixture-core.quarry.get");
    OA_CHECK(result.action == lib::Action::get && result.entry == quarry);
    for (const std::string_view key : {"hollow", "frontier"}) {
        lib::set_query(library, key);
        list = laid_out(library, state, regular);
        const std::string name = "library.row.example-mods." + std::string(key) + ".get";
        const kit::Control* off = fixture::named(list, name);
        OA_CHECK(off != nullptr && !off->enabled);
        if (off != nullptr)
            OA_CHECK(kit::hit(list, centre_of(*off)) != off->id);
    }
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
    // The drop-down's filters carry their counts, as the chips and the column do.
    const kit::Control* all_item = fixture::named(list, "library.filter.all");
    OA_CHECK(all_item != nullptr && all_item->text == "All (11)");
    const kit::Control* new_item = fixture::named(list, "library.filter.new");
    OA_CHECK(new_item != nullptr && new_item->text == "New (3)");
    OA_EXPECT_NONE(name_problems(list), "filter menu open");
    OA_EXPECT_NONE(fixture::texts_not_fitting(list, fonts()), "filter menu open");
    static_cast<void>(click(library, state, list, "library.filter.installed"));
    OA_CHECK(
        lib::shown_filter(library) == lib::Filter::installed && state.menu == lib::MenuKind::none
    );
    // Each tab keeps its own: Updates still opens on Updates, and Mods keeps Installed.
    lib::set_tab(library, lib::Tab::updates);
    OA_CHECK(lib::shown_filter(library) == lib::Filter::updates);
    lib::set_tab(library, lib::Tab::mods);
    OA_CHECK(lib::shown_filter(library) == lib::Filter::installed);
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
    OA_CHECK(lib::shown_filter(wide) == lib::Filter::updates);
    wide_list = laid_out(wide, wide_state, all[1].frame);
    static_cast<void>(click(wide, wide_state, wide_list, "library.filter.new"));
    OA_CHECK(lib::shown_filter(wide) == lib::Filter::newest);
    OA_CHECK(
        !wide.visible.empty() && wide.entries[wide.visible.front()].id.key == "dunes" &&
        wide.visible.size() == 3
    );
}

/// The Active tab's lines take no press, and with more maps than fit they
/// scroll by the wheel, a finger, the page keys and the right stick.
void active_tab_takes_nothing_and_scrolls() {
    const kit::Frame compact = fixture::frames()[0].frame;
    lib::Inputs inputs = fixture::fixture_inputs();
    lib::InstalledMaps many;
    many.name = "Many Maps";
    for (int32_t index = 1; index <= 40; ++index)
        many.maps.push_back({"Map " + std::to_string(index), 2, "16x16"});
    inputs.map_packs.push_back(std::move(many));
    lib::Library library;
    lib::refresh(library, std::move(inputs));
    OA_CHECK(library.tab == lib::Tab::active);
    lib::ScreenState state;
    kit::DisplayList list = laid_out(library, state, compact);
    OA_EXPECT_NONE(fixture::outside_window(list, {480, 324}), "active, many maps");
    OA_EXPECT_NONE(meeting_controls(list), "active, many maps");
    OA_EXPECT_NONE(name_problems(list), "active, many maps");
    OA_EXPECT_NONE(fixture::texts_not_fitting(list, fonts()), "active, many maps");
    OA_CHECK(fixture::named(list, "library.active.map.43") != nullptr);
    const bool has_bar =
        std::any_of(list.items.begin(), list.items.end(), [](const kit::Item& item) {
            return item.role == kit::Role::scroll_bar;
        });
    OA_CHECK(has_bar);

    // A press on a line does nothing.
    const kit::Control* mod_line = fixture::named(list, "library.active.mod");
    OA_CHECK(mod_line != nullptr);
    if (mod_line == nullptr)
        return;
    const kit::Point on_line = centre_of(*mod_line);
    OA_CHECK(kit::hit(list, on_line) != mod_line->id);
    static_cast<void>(
        lib::library_pointer(library, state, list, lib::PointerKind::down, 1, on_line, 0)
    );
    const lib::ScreenResult pressed =
        lib::library_pointer(library, state, list, lib::PointerKind::up, 1, on_line, 0);
    OA_CHECK(!pressed.action && !library.selected && library.tab == lib::Tab::active);

    // The wheel scrolls the lines; so do the page keys, End and Home.
    OA_CHECK(lib::library_wheel(library, state, list, on_line, -1.0F).redraw);
    OA_CHECK(state.details_scroll > 0);
    list = laid_out(library, state, compact);
    static_cast<void>(lib::library_key(library, state, list, kit::Key::home));
    OA_CHECK(state.details_scroll == 0);
    list = laid_out(library, state, compact);
    static_cast<void>(lib::library_key(library, state, list, kit::Key::page_down));
    OA_CHECK(state.details_scroll > 0);
    list = laid_out(library, state, compact);
    static_cast<void>(lib::library_key(library, state, list, kit::Key::end));
    const int32_t bottom = state.details_scroll;
    list = laid_out(library, state, compact);
    OA_CHECK(!lib::library_wheel(library, state, list, on_line, -1.0F).redraw);
    OA_CHECK(state.details_scroll == bottom);
    OA_EXPECT_NONE(fixture::outside_window(list, {480, 324}), "active, scrolled");
    // The right stick scrolls them too, and another tab starts at its top.
    OA_CHECK(lib::library_stick(library, state, list, 1.0F).redraw);
    OA_CHECK(state.details_scroll < bottom);
    list = laid_out(library, state, compact);
    static_cast<void>(click(library, state, list, "library.tab.mods"));
    OA_CHECK(library.tab == lib::Tab::mods && state.details_scroll == 0);
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

/// The Registries tab lists every registry, with REMOVE and ADD MIRROR… only
/// for one the player added, and ADD REGISTRY… in the footer. It has no search.
void registries_tab_lists_every_registry() {
    lib::Library library = with_ridge();
    lib::set_tab(library, lib::Tab::registries);
    for (const fixture::NamedFrame& named : fixture::frames()) {
        const kit::DisplayList list = laid_out(library, {}, named.frame);
        const kit::Point window = lib::window_size(named.frame);
        OA_EXPECT_NONE(fixture::outside_window(list, window), named.name + " registries");
        OA_EXPECT_NONE(name_problems(list), named.name + " registries");
        OA_EXPECT_NONE(order_problems(list), named.name + " registries");
        OA_CHECK(fixture::named(list, "library.tab.registries") != nullptr);
        OA_CHECK(fixture::named(list, "library.search") == nullptr);
        OA_CHECK(fixture::named(list, "library.registry.fixture-core") != nullptr);
        OA_CHECK(fixture::named(list, "library.registry.fixture-core.remove") == nullptr);
        OA_CHECK(fixture::named(list, "library.registry.fixture-core.add-mirror") == nullptr);
        OA_CHECK(fixture::named(list, "library.registry.example-mods") != nullptr);
        OA_CHECK(fixture::named(list, "library.registry.example-mods.remove") != nullptr);
        OA_CHECK(fixture::named(list, "library.registry.example-mods.add-mirror") != nullptr);
        OA_CHECK(fixture::named(list, "library.add-registry") != nullptr);
        OA_CHECK(fixture::named(list, "library.registries.note") != nullptr);
        OA_CHECK(fixture::named(list, "library.details") == nullptr);
    }

    const kit::Frame regular = fixture::frames()[1].frame;
    lib::ScreenState state;
    kit::DisplayList list = laid_out(library, state, regular);
    const lib::ScreenResult removed =
        click(library, state, list, "library.registry.example-mods.remove");
    OA_CHECK(removed.action == lib::Action::remove_registry && removed.registry == fixture::added);
    list = laid_out(library, state, regular);
    const lib::ScreenResult mirror =
        click(library, state, list, "library.registry.example-mods.add-mirror");
    OA_CHECK(mirror.action == lib::Action::add_mirror && mirror.registry == fixture::added);
    list = laid_out(library, state, regular);
    const lib::ScreenResult add = click(library, state, list, "library.add-registry");
    OA_CHECK(add.action == lib::Action::add_registry && add.registry.empty());

    // The switch's right half turns it on, and its left half turns it off.
    // Space toggles. A half that is already chosen asks for nothing.
    const kit::Control* core_switch = fixture::named(list, "library.registry.fixture-core");
    OA_CHECK(core_switch != nullptr);
    if (core_switch != nullptr) {
        const kit::Point on_half{
            core_switch->rect.x + core_switch->rect.width * 3 / 4, core_switch->rect.y + 1
        };
        static_cast<void>(
            lib::library_pointer(library, state, list, lib::PointerKind::down, 1, on_half, 0)
        );
        const lib::ScreenResult already =
            lib::library_pointer(library, state, list, lib::PointerKind::up, 1, on_half, 0);
        OA_CHECK(!already.action);
        const kit::Point off_half{
            core_switch->rect.x + core_switch->rect.width / 4, core_switch->rect.y + 1
        };
        static_cast<void>(
            lib::library_pointer(library, state, list, lib::PointerKind::down, 1, off_half, 0)
        );
        const lib::ScreenResult turned =
            lib::library_pointer(library, state, list, lib::PointerKind::up, 1, off_half, 0);
        OA_CHECK(
            turned.action == lib::Action::set_registry && turned.registry == fixture::core &&
            !turned.registry_on
        );
    }
    state = focused_on(list, "library.registry.fixture-core");
    const lib::ScreenResult keyed = lib::library_key(library, state, list, kit::Key::space);
    OA_CHECK(
        keyed.action == lib::Action::set_registry && keyed.registry == fixture::core &&
        !keyed.registry_on
    );

    library.inputs.in_match = true;
    list = laid_out(library, state, regular);
    OA_CHECK(fixture::named(list, "library.add-registry") == nullptr);
    OA_CHECK(fixture::named(list, "library.registry.example-mods.add-mirror") == nullptr);
    OA_CHECK(fixture::named(list, "library.registry.example-mods.remove") != nullptr);
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
    an_own_mods_details_show_its_description();
    enter_opens_details_or_reaches_the_actions();
    escape_clears_leaves_or_closes();
    tab_commands_change_tabs();
    controller_buttons();
    list_keys_move_the_selection();
    tab_follows_the_declared_order();
    pressing_rows_selects_and_never_gets();
    row_icons_are_get_and_update();
    wheel_and_drag_scroll();
    typing_searches();
    filters_and_tags();
    active_tab_takes_nothing_and_scrolls();
    registries_tab_lists_every_registry();
    more_holds_the_rest();
    details_page_keeps_its_entry();
    return oa::test::check_exit_status();
}
