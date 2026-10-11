// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Registered settings pages: the section list that follows the engine's own
// sections, and the row-spec column a registered page shows. The dialog's
// events and its drawing both go through here.

#pragma once

#include "geometry.hpp"

#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/rows.hpp"

#include <optional>
#include <string>
#include <vector>

namespace oa::ui::engine_settings {

/// The scroll group of the section list while registered pages are listed.
/// The engine's own rows, Developer's list and Mods' list keep 0, 1 and 2.
inline constexpr int32_t registered_nav_group = 3;

/// Tells whether the dialog lists registered pages in a scrolling section list.
///
/// @param dialog the dialog
/// @return true for the engine's settings once a page is registered
[[nodiscard]] bool registered_nav(const Dialog& dialog) noexcept;

/// Returns the registered page a nav control names, or -1 when it names none
/// of the pages the dialog lists.
///
/// @param dialog the dialog
/// @param control the control
/// @return the page's index, from 0
[[nodiscard]] int32_t registered_nav_index(const Dialog& dialog, int32_t control) noexcept;

/// One piece of the section list before it scrolls: an entry, the hairline
/// before Developer or an owner, or an owner's name.
struct NavPiece {
    enum class Kind : uint8_t {
        entry, ///< a section's entry
        rule,  ///< a hairline
        owner, ///< an owner's name
    };

    Kind kind{Kind::entry};      ///< what it is
    geometry::SourceRect rect{}; ///< where it lies before the list scrolls
    int32_t control{no_control}; ///< the entry's control; no_control for a rule or a name
    std::string text;            ///< the entry's caption or the owner's name
    bool registered{};           ///< the entry is a registered page
    std::size_t index{};         ///< the registered page's index, when registered
    Page page{Page::mods};       ///< the engine section, when the entry is one
};

/// The section list while registered pages are listed, before it scrolls.
struct NavLayout {
    std::vector<NavPiece> pieces;    ///< top to bottom
    geometry::SourceRect viewport{}; ///< entries listed wholly in here
    geometry::SourceRect area{};     ///< the list's face, what drawing clips to
    int32_t limit{};                 ///< the greatest scroll, in source pixels
};

/// Lays out the section list from the engine's sections, then each owner's
/// pages in registration order.
///
/// @param dialog the dialog
/// @return the pieces, the viewport and the scroll limit
[[nodiscard]] NavLayout layout_registered_nav(const Dialog& dialog);

/// Scrolls the section list the least that shows the focused entry whole.
///
/// @param[in,out] dialog the dialog
void reveal_registered_nav(Dialog& dialog);

/// Scrolls the section list when the pointer is over it and the list scrolls.
///
/// @param[in,out] dialog the dialog
/// @param x the pointer's column, in source pixels
/// @param y the pointer's row, in source pixels
/// @param notches the wheel's turn
/// @return what the list did, or nothing when the wheel is not the list's
[[nodiscard]] std::optional<DialogAction>
wheel_registered_nav(Dialog& dialog, int32_t x, int32_t y, float notches);

/// A registered page's rows, placed and scrolled.
struct RegisteredContent {
    oa::ui::kit::PlacedRows placed{}; ///< the rows, scrolled
    oa::ui::kit::ScrollArea scroll{}; ///< where they scroll
    int32_t offset{};                 ///< how far they are scrolled
};

/// Places the open registered page's rows from their specs.
///
/// @param dialog the dialog
/// @return the rows; empty when the open page is not a registered one
[[nodiscard]] RegisteredContent registered_content(const Dialog& dialog);

/// Returns the open registered page's heading, as it was registered.
///
/// @param dialog the dialog
/// @return the title; empty when the open page is not a registered one
[[nodiscard]] std::string registered_heading(const Dialog& dialog);

/// Appends the section list's pieces that lie wholly in its viewport.
///
/// @param dialog the dialog
/// @param[in,out] parts the parts
void append_registered_nav(const Dialog& dialog, std::vector<LayoutPart>& parts);

/// Appends the open registered page's row parts that lie wholly in its view.
///
/// @param dialog the dialog
/// @param[in,out] parts the parts
void append_registered_content(const Dialog& dialog, std::vector<LayoutPart>& parts);

/// Presses a registered row with Space, or shows the registered page a nav
/// entry names. Other controls are left to the dialog.
///
/// @param[in,out] dialog the dialog
/// @param control the control
/// @return what it did, or nothing when the control is not one of these
[[nodiscard]] std::optional<DialogAction> activate_registered(Dialog& dialog, int32_t control);

/// Presses a registered row where the pointer came up.
///
/// @param[in,out] dialog the dialog
/// @param control the control
/// @param point the pointer, in source pixels
/// @return what it did, or nothing when the control is not a registered row
[[nodiscard]] std::optional<DialogAction>
press_registered_row(Dialog& dialog, int32_t control, oa::ui::kit::Point point);

/// Steps a registered row with Left or Right.
///
/// @param[in,out] dialog the dialog
/// @param control the control
/// @param up true for Right
/// @return what it did, or nothing when the control is not a registered row
[[nodiscard]] std::optional<DialogAction>
step_registered_row(Dialog& dialog, int32_t control, bool up);

/// Scrolls the open registered page when it is taller than its view.
///
/// @param[in,out] dialog the dialog
/// @param notches the wheel's turn
/// @return what the page did, or nothing when it does not scroll
[[nodiscard]] std::optional<DialogAction> wheel_registered_content(Dialog& dialog, float notches);

/// Puts the open registered page's keys back to each row's default text.
///
/// @param[in,out] dialog the dialog
void restore_registered_page(Dialog& dialog);

} // namespace oa::ui::engine_settings
