// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What layout.cpp and input.cpp share: the numbers of the screen's controls,
// its two scroll areas, and the items each drop-down holds.
#pragma once

#include "oa/ui/kit/layout.hpp"
#include "oa/ui/library/library.hpp"
#include "oa/ui/library/screen.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oa::ui::library::controls {

/// The first tab's control; the others follow in Tab's order.
inline constexpr kit::ControlId first_tab = 1;
/// The search field.
inline constexpr kit::ControlId search = 10;
/// Compact's filter drop-down (library.filter).
inline constexpr kit::ControlId filter_menu = 11;
/// The drop-down of the tags that do not fit (library.tags).
inline constexpr kit::ControlId tags_menu = 12;
/// The Compact details page's MORE ▾ (library.action).
inline constexpr kit::ControlId more_menu = 13;
/// The All chip or nav entry; New, Installed and Updates follow in Filter's order.
inline constexpr kit::ControlId first_filter = 20;
/// UPDATE ALL.
inline constexpr kit::ControlId update_all = 30;
/// The list pane.
inline constexpr kit::ControlId list = 40;
/// Compact's DETAILS.
inline constexpr kit::ControlId details = 50;
/// The Compact details page's way back to the list.
inline constexpr kit::ControlId back = 51;
/// SETTINGS….
inline constexpr kit::ControlId settings = 52;
/// CLOSE.
inline constexpr kit::ControlId close = 53;
/// CHECK NOW.
inline constexpr kit::ControlId check_now = 54;
/// The status line's label.
inline constexpr kit::ControlId status = 60;
/// The first queue line's label.
inline constexpr kit::ControlId queue = 61;
/// The playing note's label.
inline constexpr kit::ControlId note = 62;
/// The From fact's label.
inline constexpr kit::ControlId from = 63;
/// The Rules fact's label.
inline constexpr kit::ControlId rules = 64;
/// The Active tab's line on the mod being played.
inline constexpr kit::ControlId active_mod = 65;
/// The Active tab's line on the language in use.
inline constexpr kit::ControlId active_language = 66;
/// An entry's action button: this and the Action's value.
inline constexpr kit::ControlId first_action = 100;
/// A tag's chip or nav entry: this and the tag's place in tags_in_tab.
inline constexpr kit::ControlId first_tag = 1000;
/// An open drop-down's item: this and the item's place among its items,
/// below first_active_map.
inline constexpr kit::ControlId first_menu_item = 3000;
/// The Active tab's line on a map: this and the map's place among the
/// lines, from 0, below first_row.
inline constexpr kit::ControlId first_active_map = 6000;
/// The most maps whose lines are labels; the lines after them are drawn
/// but not named.
inline constexpr std::size_t most_active_maps = 4000;
/// A row: this and the entry's place in the model's visible entries, below first_row_icon.
inline constexpr kit::ControlId first_row = 10000;
/// A row's state icon that is pressed as GET or UPDATE: this and the
/// entry's place in the model's visible entries.
inline constexpr kit::ControlId first_row_icon = 1000000;

/// The list's scroll area, the group of its rows.
inline constexpr int32_t list_group = 0;
/// The details' scroll area, the group of the controls in them.
inline constexpr int32_t details_group = 1;

/// Returns an action's button's control.
///
/// @param action the action
/// @return its number
[[nodiscard]] constexpr kit::ControlId action_control(Action action) noexcept {
    return first_action + static_cast<kit::ControlId>(action);
}

/// Tells whether a control is a row's.
///
/// @param id the control
/// @return true from first_row up to first_row_icon
[[nodiscard]] constexpr bool row_control(kit::ControlId id) noexcept {
    return id >= first_row && id < first_row_icon;
}

/// Tells whether a control is a row's state icon.
///
/// @param id the control
/// @return true from first_row_icon on
[[nodiscard]] constexpr bool row_icon_control(kit::ControlId id) noexcept {
    return id >= first_row_icon;
}

/// Returns a filter's chip's or nav entry's control.
///
/// @param filter the filter
/// @return its number
[[nodiscard]] constexpr kit::ControlId filter_control(Filter filter) noexcept {
    return first_filter + static_cast<kit::ControlId>(filter);
}

/// Returns a tab's control.
///
/// @param tab the tab
/// @return its number
[[nodiscard]] constexpr kit::ControlId tab_control(Tab tab) noexcept {
    return first_tab + static_cast<kit::ControlId>(tab);
}

/// One item of a drop-down: its caption, its automation word and what
/// choosing it does, one of a filter, a tag or an action.
struct MenuItem {
    std::string caption{};            ///< the words shown, looked up
    std::string word{};               ///< the word automation names it by
    std::optional<Filter> filter{};   ///< the filter it shows, with every tag
    std::optional<std::size_t> tag{}; ///< the tag it shows, its place in tags_in_tab
    std::optional<Action> action{};   ///< the selected entry's action it asks for
};

/// Lists a drop-down's items.
///
/// Compact's filter holds All, New, Installed and Updates with their counts,
/// then every tag of the tab; the tags drop-down the tags without a chip or
/// nav entry of their own; MORE the selected entry's actions after its
/// first three.
///
/// @param library the model
/// @param menu the drop-down
/// @param tag_shown for each tag of tags_in_tab, whether it has its own chip or nav entry
/// @return the items, top to bottom
[[nodiscard]] std::vector<MenuItem>
menu_items(const Library& library, MenuKind menu, const std::vector<bool>& tag_shown);

/// Returns what a row's state icon is pressed as: GET or UPDATE, as the
/// entry's first button asks for it, and only while that button is
/// enabled, so the icon keeps that button's rules and blocked state. A row
/// that is queued (CANCEL), that failed (RETRY), or that is installed and
/// up to date has none.
///
/// @param library the model
/// @param entry one of its entries
/// @return the action; none while the icon takes no press
[[nodiscard]] std::optional<Action> row_icon_action(const Library& library, const Entry& entry);

/// Returns the item a drop-down shows as chosen.
///
/// @param library the model
/// @param menu the drop-down
/// @param items its items
/// @return the item's place, or -1 for none
[[nodiscard]] int32_t
chosen_item(const Library& library, MenuKind menu, const std::vector<MenuItem>& items);

/// The rows a pane's parts take.
struct Extent {
    int32_t top{};    ///< the first part's top row
    int32_t bottom{}; ///< the row under the last part
};

/// Returns the rows taken by the items of a display list that a view clips,
/// from one item on: the parts of a pane that scrolls.
///
/// @param display the display list
/// @param view the pane's view, which clips its parts
/// @param first_item the first item to look at
/// @return the rows; none when no item from there is clipped by the view
[[nodiscard]] std::optional<Extent>
clipped_extent(const kit::DisplayList& display, const kit::Rect& view, std::size_t first_item);

/// Tells, for each tag of the tab, whether a display list holds its own chip
/// or nav entry.
///
/// @param display the display list
/// @param tags how many tags the tab has
/// @return one flag per tag
[[nodiscard]] std::vector<bool> tags_shown(const kit::DisplayList& display, std::size_t tags);

} // namespace oa::ui::library::controls
