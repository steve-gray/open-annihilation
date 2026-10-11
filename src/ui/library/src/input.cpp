// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's input: the pointer and a finger, the keys through the kit's
// abstract keys, typed text, the wheel, and the Library's own keys and
// controller buttons. What it knows of the screen it reads from the display
// list the screen shows, and every rule it applies is the model's.
#include "oa/ui/library/screen.hpp"

#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/looks.hpp"
#include "oa/ui/kit/theme.hpp"
#include "oa/ui/library/library.hpp"
#include "screen_controls.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace oa::ui::library {

namespace {

/// How many tabs there are, in Tab's order.
constexpr int32_t tab_count = 4;
/// How many filters there are, in Filter's order.
constexpr int32_t filter_count = 3;
/// How far a notch of the wheel scrolls the details, in points: a full list row.
constexpr int32_t details_step = kit::compact_metrics.list_row_height;

/// A pane's scroll, as the display list shows it.
struct PaneScroll {
    kit::Rect view{}; ///< what the pane is seen through
    int32_t offset{}; ///< how far it is scrolled, in points
    int32_t limit{};  ///< the greatest offset, in points
    int32_t step{};   ///< how far a notch of the wheel scrolls it, in points
};

/// Returns a result that only redraws.
///
/// @return the result
ScreenResult redrawn() {
    ScreenResult result;
    result.redraw = true;
    return result;
}

/// Returns the height of the list's rows, from a row the list shows.
///
/// @param list the display list
/// @return the height, in points; none while no row shows
std::optional<int32_t> row_height_of(const kit::DisplayList& list) {
    for (const kit::Control& control : list.controls)
        if (control.id >= controls::first_row && control.rect.height > 0)
            return control.rect.height;
    return std::nullopt;
}

/// Returns the list's scroll as the display list shows it.
///
/// @param library the model
/// @param state the screen's state
/// @param list the display list
/// @return the scroll; none while the list shows no row
std::optional<PaneScroll>
list_scroll_of(const Library& library, const ScreenState& state, const kit::DisplayList& list) {
    const kit::Control* pane = kit::control_of(list, controls::list);
    const std::optional<int32_t> height = row_height_of(list);
    if (pane == nullptr || !height)
        return std::nullopt;
    PaneScroll scroll;
    scroll.view = pane->rect;
    scroll.step = *height;
    const auto content = static_cast<int32_t>(library.visible.size()) * *height;
    scroll.limit = kit::ScrollArea::limit_for(content, scroll.view.height);
    scroll.offset = std::clamp(state.list_scroll, int32_t{0}, scroll.limit);
    return scroll;
}

/// Returns the details' scroll as the display list shows it: their view
/// clips every control in them, and their parts run from the view's top
/// less the offset.
///
/// @param list the display list
/// @return the scroll; none while no details show
std::optional<PaneScroll> details_scroll_of(const kit::DisplayList& list) {
    const kit::Control* inside = nullptr;
    for (const kit::Control& control : list.controls)
        if (control.group == controls::details_group && control.clip.width > 0 &&
            control.clip.height > 0) {
            inside = &control;
            break;
        }
    if (inside == nullptr)
        return std::nullopt;
    const kit::Rect view = inside->clip;
    const std::optional<controls::Extent> extent = controls::clipped_extent(list, view, 0);
    if (!extent)
        return std::nullopt;
    PaneScroll scroll;
    scroll.view = view;
    scroll.step = details_step;
    scroll.offset = std::max(view.y - extent->top, int32_t{0});
    scroll.limit = kit::ScrollArea::limit_for(extent->bottom - extent->top, view.height);
    return scroll;
}

/// Returns the offset nearest another that shows a part of a pane whole,
/// or its top when it is taller than the view.
///
/// @param offset the offset now
/// @param top the part's top when the pane is not scrolled, from the view's top
/// @param height the part's height
/// @param view_height the view's height
/// @param limit the greatest offset
/// @return the offset
int32_t
offset_showing(int32_t offset, int32_t top, int32_t height, int32_t view_height, int32_t limit) {
    const int32_t lowest = top + height - view_height;
    return std::clamp(
        std::min(std::max(offset, lowest), top), int32_t{0}, std::max(limit, int32_t{0})
    );
}

/// Returns the selected entry's place among the visible entries.
///
/// @param library the model
/// @return the place; none with nothing selected
std::optional<std::size_t> selected_place(const Library& library) {
    if (!library.selected)
        return std::nullopt;
    for (std::size_t place = 0; place < library.visible.size(); ++place)
        if (library.entries[library.visible[place]].id == *library.selected)
            return place;
    return std::nullopt;
}

/// Scrolls the list so the selected row shows whole.
///
/// @param library the model
/// @param[in,out] state the screen's state; its list scroll changes
/// @param list the display list, which says the rows' height and the view
void show_selection(const Library& library, ScreenState& state, const kit::DisplayList& list) {
    const std::optional<PaneScroll> scroll = list_scroll_of(library, state, list);
    const std::optional<std::size_t> place = selected_place(library);
    if (!scroll || !place)
        return;
    state.list_scroll = offset_showing(
        scroll->offset,
        static_cast<int32_t>(*place) * scroll->step,
        scroll->step,
        scroll->view.height,
        scroll->limit
    );
}

/// Scrolls the details so a control in them shows whole.
///
/// @param[in,out] state the screen's state; its details scroll changes
/// @param list the display list
/// @param id the control
void show_in_details(ScreenState& state, const kit::DisplayList& list, kit::ControlId id) {
    const kit::Control* control = kit::control_of(list, id);
    if (control == nullptr || control->group != controls::details_group)
        return;
    const std::optional<PaneScroll> scroll = details_scroll_of(list);
    if (!scroll)
        return;
    const int32_t top = control->rect.y + scroll->offset - scroll->view.y;
    state.details_scroll = offset_showing(
        scroll->offset, top, control->rect.height, scroll->view.height, scroll->limit
    );
}

/// Tells whether the Compact details page shows: its way back is in the list.
///
/// @param list the display list
/// @return true while it shows
bool page_shows(const kit::DisplayList& list) {
    return kit::control_of(list, controls::back) != nullptr;
}

/// Tells whether the Compact list page shows: its DETAILS is in the list.
///
/// @param list the display list
/// @return true while it shows
bool compact_list_shows(const kit::DisplayList& list) {
    return kit::control_of(list, controls::details) != nullptr;
}

/// Returns the drop-down field a menu opens from.
///
/// @param menu the menu
/// @return its control
kit::ControlId field_of(MenuKind menu) {
    switch (menu) {
    case MenuKind::filter:
        return controls::filter_menu;
    case MenuKind::tags:
        return controls::tags_menu;
    case MenuKind::more:
        return controls::more_menu;
    case MenuKind::none:
        break;
    }
    return kit::no_control;
}

/// Tells whether a control is an open menu's item.
///
/// @param id the control
/// @return true for an item
bool menu_item_control(kit::ControlId id) {
    return id >= controls::first_menu_item && id < controls::first_row;
}

/// Returns the open drop-down's items.
///
/// @param library the model
/// @param state the screen's state
/// @param list the display list, which says which tags have their own chip
/// @return the items
std::vector<controls::MenuItem>
open_items(const Library& library, const ScreenState& state, const kit::DisplayList& list) {
    const std::size_t tags = tags_in_tab(library).size();
    return controls::menu_items(library, state.menu, controls::tags_shown(list, tags));
}

/// Moves the drop-down's mark, keeping it among the items shown.
///
/// @param[in,out] state the screen's state
/// @param count how many items the drop-down holds
/// @param marked the item to mark
void mark_item(ScreenState& state, int32_t count, int32_t marked) {
    if (count <= 0)
        return;
    const int32_t shown_count = kit::shown_choices(static_cast<std::size_t>(count));
    state.menu_marked = std::clamp(marked, int32_t{0}, count - 1);
    state.menu_first =
        std::clamp(state.menu_first, state.menu_marked - shown_count + 1, state.menu_marked);
    state.menu_first =
        std::clamp(state.menu_first, int32_t{0}, std::max(count - shown_count, int32_t{0}));
}

/// Brings the screen up to date after the model shows other entries: the
/// drop-down closes, the list shows the selection and the details their top.
///
/// @param library the model
/// @param[in,out] state the screen's state
/// @param list the display list
void after_browsing(const Library& library, ScreenState& state, const kit::DisplayList& list) {
    state.menu = MenuKind::none;
    state.list_scroll = 0;
    state.details_scroll = 0;
    show_selection(library, state, list);
}

/// Searches for the search field's text, keeping the field's text the
/// model's query, at most max_query_bytes.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @param list the display list
void search_for_field(Library& library, ScreenState& state, const kit::DisplayList& list) {
    set_query(library, state.search.text);
    if (state.search.text != library.query) {
        state.search.text = library.query;
        state.search.caret = std::min(state.search.caret, state.search.text.size());
    }
    after_browsing(library, state, list);
}

/// Gives the search field the focus.
///
/// @param[in,out] state the screen's state
void focus_search(ScreenState& state) {
    state.menu = MenuKind::none;
    state.interaction.focused = controls::search;
    state.interaction.focus_shown = true;
}

/// Returns the first enabled action of the selected entry.
///
/// @param library the model
/// @return its button's control; none without one
std::optional<kit::ControlId> first_enabled_action(const Library& library) {
    const Entry* entry = selected_entry(library);
    if (entry == nullptr)
        return std::nullopt;
    for (const ActionButton& button : entry_actions(library, *entry))
        if (button.enabled)
            return controls::action_control(button.action);
    return std::nullopt;
}

/// Opens the selected entry's details page at Compact.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @return what the host is to do
ScreenResult open_page(Library& library, ScreenState& state) {
    if (selected_entry(library) == nullptr)
        return {};
    open_details(library);
    state.details_page = library.details_open;
    state.details_scroll = 0;
    state.menu = MenuKind::none;
    if (state.interaction.focus_shown)
        state.interaction.focused = first_enabled_action(library).value_or(controls::back);
    return redrawn();
}

/// Leaves the details page for the list.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @return what the host is to do
ScreenResult close_page(Library& library, ScreenState& state) {
    close_details(library);
    state.details_page = false;
    state.menu = MenuKind::none;
    if (state.interaction.focus_shown)
        state.interaction.focused = controls::list;
    return redrawn();
}

/// Does what Enter does on the list: opens the details page at Compact,
/// and at Regular and Large moves the focus to the first enabled action.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @param list the display list
/// @return what the host is to do
ScreenResult enter_list(Library& library, ScreenState& state, const kit::DisplayList& list) {
    if (compact_list_shows(list))
        return open_page(library, state);
    const std::optional<kit::ControlId> action = first_enabled_action(library);
    if (!action || kit::control_of(list, *action) == nullptr)
        return {};
    state.interaction.focused = *action;
    state.interaction.focus_shown = true;
    show_in_details(state, list, *action);
    return redrawn();
}

/// Moves the selection through the list and keeps it in view, the details
/// of the entry it reaches from their top.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @param list the display list
/// @param delta how many rows; negative moves up
/// @return true when the selection moved
bool move_in_list(Library& library, ScreenState& state, const kit::DisplayList& list, int delta) {
    const std::optional<std::size_t> before = selected_place(library);
    move_selection(library, delta);
    show_selection(library, state, list);
    if (selected_place(library) == before)
        return false;
    // Another entry's details start at their top.
    state.details_scroll = 0;
    return true;
}

/// Returns how many rows the list shows.
///
/// @param library the model
/// @param state the screen's state
/// @param list the display list
/// @return the rows, at least 1
int rows_shown(const Library& library, const ScreenState& state, const kit::DisplayList& list) {
    const std::optional<PaneScroll> scroll = list_scroll_of(library, state, list);
    if (!scroll || scroll->step <= 0)
        return 1;
    return std::max(scroll->view.height / scroll->step, int32_t{1});
}

/// Does what choosing a drop-down's item asks: shows a filter or a tag, or
/// asks for the selected entry's action.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @param list the display list
/// @param place the item's place among the drop-down's items
/// @return what the host is to do
ScreenResult
choose_item(Library& library, ScreenState& state, const kit::DisplayList& list, int32_t place) {
    const std::vector<controls::MenuItem> items = open_items(library, state, list);
    state.menu = MenuKind::none;
    if (place < 0 || static_cast<std::size_t>(place) >= items.size())
        return redrawn();
    const controls::MenuItem& item = items[static_cast<std::size_t>(place)];
    if (item.action) {
        ScreenResult result = redrawn();
        result.action = *item.action;
        if (library.selected)
            result.entry = *library.selected;
        return result;
    }
    if (item.filter) {
        set_tag(library, {});
        set_filter(library, *item.filter);
    } else if (item.tag) {
        const std::vector<std::pair<std::string, std::size_t>> tags = tags_in_tab(library);
        if (*item.tag < tags.size())
            set_tag(library, tags[*item.tag].first);
    }
    after_browsing(library, state, list);
    return redrawn();
}

/// Opens a drop-down, or closes it when it is open, marking its chosen item.
///
/// @param library the model
/// @param[in,out] state the screen's state
/// @param list the display list
/// @param menu the drop-down
/// @return what the host is to do
ScreenResult toggle_menu(
    const Library& library, ScreenState& state, const kit::DisplayList& list, MenuKind menu
) {
    if (state.menu == menu) {
        state.menu = MenuKind::none;
        return redrawn();
    }
    state.menu = menu;
    state.menu_first = 0;
    const std::vector<controls::MenuItem> items = open_items(library, state, list);
    const int32_t chosen = controls::chosen_item(library, menu, items);
    mark_item(state, static_cast<int32_t>(items.size()), std::max(chosen, int32_t{0}));
    return redrawn();
}

/// Shows a tab.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @param list the display list
/// @param tab the tab
/// @return what the host is to do
ScreenResult show_tab(Library& library, ScreenState& state, const kit::DisplayList& list, Tab tab) {
    const bool tab_focused = state.interaction.focused >= controls::first_tab &&
                             state.interaction.focused < controls::first_tab + tab_count;
    if (state.details_page) {
        close_details(library);
        state.details_page = false;
    }
    set_tab(library, tab);
    after_browsing(library, state, list);
    if (tab_focused)
        state.interaction.focused = controls::tab_control(tab);
    return redrawn();
}

/// Does what a control does when it is pressed, or takes Space or Enter.
///
/// No press on a row gets, updates or installs anything: it selects the
/// row, and at Compact opens its details page.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @param list the display list
/// @param id the control
/// @return what the host is to do
ScreenResult
activate(Library& library, ScreenState& state, const kit::DisplayList& list, kit::ControlId id) {
    const kit::Control* control = kit::control_of(list, id);
    if (control == nullptr || !control->enabled)
        return {};
    if (menu_item_control(id))
        return choose_item(library, state, list, id - controls::first_menu_item);
    state.menu = id == field_of(state.menu) ? state.menu : MenuKind::none;
    if (id >= controls::first_tab && id < controls::first_tab + tab_count)
        return show_tab(library, state, list, static_cast<Tab>(id - controls::first_tab));
    if (id >= controls::first_filter && id < controls::first_filter + filter_count) {
        set_filter(library, static_cast<Filter>(id - controls::first_filter));
        after_browsing(library, state, list);
        return redrawn();
    }
    if (id >= controls::first_action && id < controls::first_tag) {
        ScreenResult result = redrawn();
        result.action = static_cast<Action>(id - controls::first_action);
        if (library.selected)
            result.entry = *library.selected;
        return result;
    }
    if (id >= controls::first_tag && id < controls::first_menu_item) {
        const std::vector<std::pair<std::string, std::size_t>> tags = tags_in_tab(library);
        const auto index = static_cast<std::size_t>(id - controls::first_tag);
        if (index < tags.size())
            set_tag(library, library.tag == tags[index].first ? std::string() : tags[index].first);
        after_browsing(library, state, list);
        return redrawn();
    }
    if (id >= controls::first_row) {
        const auto place = static_cast<std::size_t>(id - controls::first_row);
        if (place >= library.visible.size())
            return {};
        select(library, library.entries[library.visible[place]].id);
        state.details_scroll = 0;
        if (compact_list_shows(list))
            return open_page(library, state);
        return redrawn();
    }
    ScreenResult result = redrawn();
    switch (id) {
    case controls::search:
        focus_search(state);
        return result;
    case controls::filter_menu:
        return toggle_menu(library, state, list, MenuKind::filter);
    case controls::tags_menu:
        return toggle_menu(library, state, list, MenuKind::tags);
    case controls::more_menu:
        return toggle_menu(library, state, list, MenuKind::more);
    case controls::update_all:
        result.action = Action::update_all;
        return result;
    case controls::details:
        return open_page(library, state);
    case controls::back:
        return close_page(library, state);
    case controls::settings:
        result.action = Action::settings;
        return result;
    case controls::close:
        result.action = Action::close;
        return result;
    default:
        break;
    }
    return {};
}

/// Takes a key while a drop-down is open: Up, Down, Page Up, Page Down,
/// Home and End move its mark, Enter and Space choose the marked item and
/// Escape closes it. Tab and Shift+Tab close it and move on.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @param list the display list
/// @param pressed the key
/// @return what the host is to do; none when the key goes on to the screen
std::optional<ScreenResult>
menu_key(Library& library, ScreenState& state, const kit::DisplayList& list, kit::Key pressed) {
    const auto count = static_cast<int32_t>(open_items(library, state, list).size());
    const int32_t page = kit::shown_choices(static_cast<std::size_t>(std::max(count, int32_t{0})));
    switch (pressed) {
    case kit::Key::escape:
        state.menu = MenuKind::none;
        return redrawn();
    case kit::Key::up:
        mark_item(state, count, state.menu_marked - 1);
        return redrawn();
    case kit::Key::down:
        mark_item(state, count, state.menu_marked + 1);
        return redrawn();
    case kit::Key::page_up:
        mark_item(state, count, state.menu_marked - page);
        return redrawn();
    case kit::Key::page_down:
        mark_item(state, count, state.menu_marked + page);
        return redrawn();
    case kit::Key::home:
        mark_item(state, count, 0);
        return redrawn();
    case kit::Key::end:
        mark_item(state, count, count - 1);
        return redrawn();
    case kit::Key::enter:
    case kit::Key::space:
        return choose_item(library, state, list, state.menu_marked);
    case kit::Key::tab:
    case kit::Key::back_tab:
        state.menu = MenuKind::none;
        return std::nullopt;
    default:
        return ScreenResult{};
    }
}

/// Returns the pane a point lies in.
///
/// @param library the model
/// @param state the screen's state
/// @param list the display list
/// @param at the point
/// @return the list, the details or none
Pane pane_at(
    const Library& library, const ScreenState& state, const kit::DisplayList& list, kit::Point at
) {
    if (const std::optional<PaneScroll> scroll = list_scroll_of(library, state, list);
        scroll && kit::contains(scroll->view, at))
        return Pane::list;
    if (const std::optional<PaneScroll> scroll = details_scroll_of(list);
        scroll && kit::contains(scroll->view, at))
        return Pane::details;
    return Pane::none;
}

/// Returns a pane's scroll.
///
/// @param library the model
/// @param state the screen's state
/// @param list the display list
/// @param pane the pane
/// @return its scroll; none while it does not show
std::optional<PaneScroll> scroll_of(
    const Library& library, const ScreenState& state, const kit::DisplayList& list, Pane pane
) {
    if (pane == Pane::list)
        return list_scroll_of(library, state, list);
    if (pane == Pane::details)
        return details_scroll_of(list);
    return std::nullopt;
}

/// Scrolls a pane to an offset.
///
/// @param[in,out] state the screen's state
/// @param pane the pane
/// @param offset the offset, in points
void scroll_to(ScreenState& state, Pane pane, int32_t offset) {
    if (pane == Pane::list)
        state.list_scroll = offset;
    else if (pane == Pane::details)
        state.details_scroll = offset;
}

/// Turns a pane by the wheel's notches.
///
/// @param library the model
/// @param[in,out] state the screen's state
/// @param list the display list
/// @param pane the pane
/// @param notches the turn; positive towards the top
/// @return what the host is to do
ScreenResult turn_pane(
    const Library& library,
    ScreenState& state,
    const kit::DisplayList& list,
    Pane pane,
    float notches
) {
    const std::optional<PaneScroll> scroll = scroll_of(library, state, list, pane);
    if (!scroll)
        return {};
    kit::WheelCarry& carry = pane == Pane::list ? state.list_wheel : state.details_wheel;
    const int32_t next =
        kit::wheel_offset(carry, notches, scroll->step, scroll->offset, scroll->limit);
    if (next == scroll->offset)
        return {};
    scroll_to(state, pane, next);
    return redrawn();
}

/// Returns the pane that holds the focus: the details while a control of
/// theirs has it, else the list.
///
/// @param state the screen's state
/// @param list the display list
/// @return the pane
Pane focused_pane(const ScreenState& state, const kit::DisplayList& list) {
    const kit::Control* control = kit::control_of(list, state.interaction.focused);
    if (state.interaction.focus_shown && control != nullptr &&
        control->group == controls::details_group)
        return Pane::details;
    return page_shows(list) ? Pane::details : Pane::list;
}

} // namespace

std::optional<kit::Rect>
focused_field(const ScreenState& state, const kit::DisplayList& list) noexcept {
    if (!state.interaction.focus_shown || state.interaction.focused != controls::search)
        return std::nullopt;
    const kit::Control* control = kit::control_of(list, controls::search);
    if (control == nullptr)
        return std::nullopt;
    return control->rect;
}

ScreenResult library_pointer(
    Library& library,
    ScreenState& state,
    const kit::DisplayList& list,
    PointerKind kind,
    uint8_t button,
    kit::Point at,
    int32_t finger_reach
) {
    kit::Interaction& interaction = state.interaction;
    switch (kind) {
    case PointerKind::move: {
        if (state.drag.pane != Pane::none) {
            const int32_t across = at.x - state.drag.from.x;
            const int32_t along = at.y - state.drag.from.y;
            if (!state.drag.scrolling &&
                (std::abs(along) > finger_reach || std::abs(across) > finger_reach)) {
                // The press becomes a drag: it scrolls, and presses nothing.
                state.drag.scrolling = true;
                interaction.pressed = kit::no_control;
                interaction.finger_shift = {};
            }
            if (state.drag.scrolling) {
                const std::optional<PaneScroll> scroll =
                    scroll_of(library, state, list, state.drag.pane);
                if (!scroll)
                    return {};
                const int32_t next =
                    std::clamp(state.drag.offset - along, int32_t{0}, scroll->limit);
                if (next == scroll->offset)
                    return {};
                scroll_to(state, state.drag.pane, next);
                return redrawn();
            }
        }
        const kit::PointerOutcome outcome = kit::pointer_move(interaction, list, at);
        ScreenResult result;
        result.redraw = outcome.result != kit::PointerResult::none;
        if (state.menu != MenuKind::none && menu_item_control(outcome.control)) {
            const int32_t marked = outcome.control - controls::first_menu_item;
            if (marked != state.menu_marked) {
                state.menu_marked = marked;
                result.redraw = true;
            }
        }
        return result;
    }
    case PointerKind::down: {
        if (button != 1)
            return {};
        state.drag = {};
        if (state.menu != MenuKind::none) {
            const kit::ControlId under =
                finger_reach > 0 ? kit::reach(list, at, finger_reach).control : kit::hit(list, at);
            if (!menu_item_control(under) && under != field_of(state.menu)) {
                // A press outside an open drop-down only closes it.
                state.menu = MenuKind::none;
                return redrawn();
            }
        }
        const kit::PointerOutcome outcome =
            finger_reach > 0 ? kit::finger_down(interaction, list, at, finger_reach)
                             : kit::pointer_down(interaction, list, at);
        if (finger_reach > 0) {
            const Pane pane = pane_at(library, state, list, at);
            if (const std::optional<PaneScroll> scroll = scroll_of(library, state, list, pane)) {
                state.drag.pane = pane;
                state.drag.from = at;
                state.drag.offset = scroll->offset;
            }
        }
        ScreenResult result;
        result.redraw = outcome.result != kit::PointerResult::none;
        return result;
    }
    case PointerKind::up: {
        if (button != 1)
            return {};
        const bool dragged = state.drag.scrolling;
        state.drag = {};
        if (dragged) {
            interaction.pressed = kit::no_control;
            interaction.finger_shift = {};
            return redrawn();
        }
        const kit::PointerOutcome outcome = kit::pointer_up(interaction, list, at);
        if (outcome.result == kit::PointerResult::activated)
            return activate(library, state, list, outcome.control);
        ScreenResult result;
        result.redraw = outcome.result != kit::PointerResult::none;
        return result;
    }
    }
    return {};
}

ScreenResult
library_key(Library& library, ScreenState& state, const kit::DisplayList& list, kit::Key pressed) {
    kit::Interaction& interaction = state.interaction;
    // A focus the screen no longer shows moves to the list, or to the first stop.
    if (interaction.focus_shown && kit::control_of(list, interaction.focused) == nullptr) {
        if (kit::control_of(list, controls::list) != nullptr)
            interaction.focused = controls::list;
        else
            interaction.focused = list.tab_order.empty() ? kit::no_control : list.tab_order.front();
    }
    if (state.menu != MenuKind::none) {
        if (const std::optional<ScreenResult> taken = menu_key(library, state, list, pressed))
            return *taken;
    }
    const kit::ControlId focus = interaction.focus_shown ? interaction.focused : kit::no_control;
    const bool on_search = focus == controls::search;
    const bool on_list = focus == controls::list;
    const bool on_drop_down = focus == controls::filter_menu || focus == controls::tags_menu ||
                              focus == controls::more_menu;
    // On the details page the page keys scroll its body, never the list it hides.
    const bool in_details = page_shows(list) || (focus != kit::no_control && !on_list &&
                                                 focused_pane(state, list) == Pane::details);

    switch (pressed) {
    case kit::Key::escape:
        if (on_search && !state.search.text.empty()) {
            state.search = {};
            search_for_field(library, state, list);
            return redrawn();
        }
        if (page_shows(list))
            return close_page(library, state);
        {
            ScreenResult result;
            result.action = Action::close;
            return result;
        }
    case kit::Key::enter:
        if (on_search) {
            // The search is done: the focus goes to what it found.
            if (kit::control_of(list, controls::list) != nullptr)
                interaction.focused = controls::list;
            return redrawn();
        }
        if (on_list || focus == kit::no_control)
            return enter_list(library, state, list);
        if (on_drop_down)
            return activate(library, state, list, focus);
        break;
    case kit::Key::space:
        if (on_list)
            return enter_list(library, state, list);
        if (on_drop_down)
            return activate(library, state, list, focus);
        break;
    case kit::Key::up:
    case kit::Key::down:
        // On the list the arrows move the selection; past either end the focus leaves it.
        if (on_list && move_in_list(library, state, list, pressed == kit::Key::up ? -1 : 1))
            return redrawn();
        break;
    case kit::Key::left:
    case kit::Key::right:
        if (on_search) {
            if (kit::edit_text(state.search, pressed))
                return redrawn();
            // At the text's end the arrow moves the focus on.
            const kit::ControlId next = kit::focus_toward(
                list,
                focus,
                pressed == kit::Key::left ? kit::Direction::left : kit::Direction::right
            );
            if (next == kit::no_control)
                return {};
            interaction.focused = next;
            show_in_details(state, list, next);
            return redrawn();
        }
        break;
    case kit::Key::backspace:
    case kit::Key::delete_forward:
        if (on_search && kit::edit_text(state.search, pressed)) {
            search_for_field(library, state, list);
            return redrawn();
        }
        return {};
    case kit::Key::home:
    case kit::Key::end:
        if (on_search)
            return kit::edit_text(state.search, pressed) ? redrawn() : ScreenResult{};
        if (in_details) {
            const std::optional<PaneScroll> scroll = details_scroll_of(list);
            if (!scroll)
                return {};
            const int32_t next = pressed == kit::Key::home ? 0 : scroll->limit;
            if (next == scroll->offset)
                return {};
            state.details_scroll = next;
            return redrawn();
        }
        if (move_in_list(
                library,
                state,
                list,
                pressed == kit::Key::home ? -static_cast<int>(library.visible.size())
                                          : static_cast<int>(library.visible.size())
            ))
            return redrawn();
        return {};
    case kit::Key::page_up:
    case kit::Key::page_down: {
        if (in_details) {
            const std::optional<PaneScroll> scroll = details_scroll_of(list);
            if (!scroll)
                return {};
            const int32_t page =
                std::max(scroll->view.height - kit::compact_metrics.small_line, int32_t{1});
            const int32_t next = std::clamp(
                scroll->offset + (pressed == kit::Key::page_up ? -page : page),
                int32_t{0},
                scroll->limit
            );
            if (next == scroll->offset)
                return {};
            state.details_scroll = next;
            return redrawn();
        }
        const int rows = rows_shown(library, state, list);
        if (move_in_list(library, state, list, pressed == kit::Key::page_up ? -rows : rows))
            return redrawn();
        return {};
    }
    case kit::Key::yes:
    case kit::Key::no:
    case kit::Key::info:
        return {};
    case kit::Key::tab:
    case kit::Key::back_tab:
        break;
    }

    // The kit's keys: Tab's declared order, the arrows by where controls
    // sit, and Space or Enter on a focused control.
    const kit::KeyOutcome outcome = kit::key(interaction, list, pressed);
    switch (outcome.result) {
    case kit::KeyResult::none:
        return {};
    case kit::KeyResult::redraw:
        show_in_details(state, list, interaction.focused);
        return redrawn();
    case kit::KeyResult::to_control:
        return activate(library, state, list, outcome.control);
    case kit::KeyResult::accept:
        return enter_list(library, state, list);
    case kit::KeyResult::scroll:
    case kit::KeyResult::cancel:
        return {};
    }
    return {};
}

ScreenResult library_text(
    Library& library, ScreenState& state, const kit::DisplayList& list, std::string_view utf8
) {
    if (!focused_field(state, list) || !kit::insert_text(state.search, utf8))
        return {};
    search_for_field(library, state, list);
    return redrawn();
}

ScreenResult library_wheel(
    Library& library, ScreenState& state, const kit::DisplayList& list, kit::Point at, float notches
) {
    if (state.menu != MenuKind::none) {
        for (const kit::Item& item : list.items) {
            const auto* look = std::get_if<kit::ChoiceMenuLook>(&item.look);
            if (item.role != kit::Role::choice_menu || look == nullptr ||
                !kit::contains(item.rect, at))
                continue;
            if (!std::isfinite(notches))
                return {};
            const auto count = static_cast<int32_t>(open_items(library, state, list).size());
            const int32_t shown_count =
                kit::shown_choices(static_cast<std::size_t>(std::max(count, int32_t{0})));
            const int32_t first = std::clamp(
                state.menu_first - static_cast<int32_t>(std::lround(notches)),
                int32_t{0},
                std::max(count - shown_count, int32_t{0})
            );
            if (first == state.menu_first)
                return {};
            state.menu_first = first;
            return redrawn();
        }
    }
    return turn_pane(library, state, list, pane_at(library, state, list, at), notches);
}

ScreenResult library_command(
    Library& library, ScreenState& state, const kit::DisplayList& list, ScreenCommand command
) {
    switch (command) {
    case ScreenCommand::find:
        if (page_shows(list)) {
            close_details(library);
            state.details_page = false;
        }
        focus_search(state);
        return redrawn();
    case ScreenCommand::next_tab:
    case ScreenCommand::previous_tab: {
        const int32_t step = command == ScreenCommand::next_tab ? 1 : tab_count - 1;
        const auto tab = static_cast<Tab>((static_cast<int32_t>(library.tab) + step) % tab_count);
        return show_tab(library, state, list, tab);
    }
    case ScreenCommand::back:
        if (state.menu != MenuKind::none) {
            state.menu = MenuKind::none;
            return redrawn();
        }
        if (page_shows(list))
            return close_page(library, state);
        {
            ScreenResult result;
            result.action = Action::close;
            return result;
        }
    case ScreenCommand::first_action: {
        const Entry* entry = selected_entry(library);
        if (entry == nullptr)
            return {};
        const std::vector<ActionButton> actions = entry_actions(library, *entry);
        if (actions.empty() || !actions.front().enabled)
            return {};
        const Action action = actions.front().action;
        if (action != Action::get && action != Action::update && action != Action::cancel &&
            action != Action::retry)
            return {};
        ScreenResult result;
        result.action = action;
        result.entry = entry->id;
        return result;
    }
    }
    return {};
}

ScreenResult
library_stick(Library& library, ScreenState& state, const kit::DisplayList& list, float notches) {
    return turn_pane(library, state, list, focused_pane(state, list), notches);
}

} // namespace oa::ui::library
