// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Provisional: this header changes whenever OA's screens need it to, until the kit is declared stable (src/ui/kit/README.md).

// The kit's controls, drawn through the canvas's primitives (canvas.hpp).
// One button, five looks. Captions come already looked up. paint draws a
// display list's items in order.
#pragma once

#include "oa/ui/kit/canvas.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/looks.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace oa::ui::kit {

/// Draws a one-bit mark.
///
/// @param canvas where it is drawn
/// @param mark which mark
/// @param at the mark's top left, in points
/// @param ink the colour of its set pixels
void draw_mark(const Canvas& canvas, Mark mark, Point at, Colour ink);

/// Draws a state icon in one colour: a circled check, a circled up-arrow or
/// a download arrow, filling a square of the rectangle's shorter side
/// centred in it.
///
/// The icon is worked out from its rings and strokes at the canvas's own
/// pixels (covered_pixels), its edges smoothed by how much of each pixel
/// they cover, and drawn through blend_coverage, so it is as crisp at 2x,
/// 3x and 150% as at 1x. Its design is a square of 24 units: the ring is 2
/// units wide with its outer edge a unit in, and every stroke is 2.5 units
/// wide with round ends. Pixels it does not cover are left as they are.
///
/// @param canvas where it is drawn
/// @param rect the rectangle, in points; state_icon_side square in a chip
/// @param icon which icon
/// @param ink its colour
void draw_state_icon(const Canvas& canvas, const Rect& rect, StateIcon icon, Colour ink);

/// Draws the keyboard focus outline: the accent, grown by the focus inset
/// when around is set, and on the control's own edge otherwise.
///
/// @param canvas where it is drawn
/// @param control the control
/// @param around true draws it outside the control; false on the control's edge
void draw_focus_ring(const Canvas& canvas, const Rect& control, bool around);

/// Draws a button in one of the five looks. The caption is centred in the
/// small font. A plain button that is not enabled draws the disabled look,
/// whatever the pointer is doing. A refresh mark draws a circular arrow in
/// place of the caption: the quiet look's face while the pointer is over it,
/// the plain look's disabled face while it cannot be pressed, and the arrow
/// in the text colour while it is turning.
///
/// @param canvas where it is drawn
/// @param area the button
/// @param look its caption, style and state
void draw_button(const Canvas& canvas, const Rect& area, const ButtonLook& look);

/// Draws an Off/On switch.
///
/// @param canvas where it is drawn
/// @param area the switch
/// @param look its state and its two captions
void draw_switch(const Canvas& canvas, const Rect& area, const SwitchLook& look);

/// Draws a strip of levels. Levels from offered on are faded into the panel.
///
/// @param canvas where it is drawn
/// @param area the strip
/// @param look its captions, the chosen level and its state
void draw_levels(const Canvas& canvas, const Rect& area, const LevelsLook& look);

/// Returns the level of a strip under a column.
///
/// @param area the strip
/// @param levels how many levels it holds
/// @param level_width one level's columns, inside the border
/// @param column the column, in points
/// @return the level's index, from 0; the nearest end for a column outside it
[[nodiscard]] std::size_t
level_at(const Rect& area, std::size_t levels, int32_t level_width, int32_t column) noexcept;

/// Draws a slider: its track filled up to the knob, its stops and its knob.
///
/// @param canvas where it is drawn
/// @param track_area the slider's area, the track within it
/// @param look the stop, the stops and its state
void draw_slider(const Canvas& canvas, const Rect& track_area, const SliderLook& look);

/// Returns the column a slider's knob is centred on.
///
/// @param track the slider's area
/// @param stop the stop
/// @param stops the slider's stops
/// @return the column, in points
[[nodiscard]] int32_t knob_column(const Rect& track, int32_t stop, int32_t stops) noexcept;

/// Returns the stop nearest a column on a slider.
///
/// @param track the slider's area
/// @param column the column, in points
/// @param stops the slider's stops
/// @return the stop, 0 to stops - 1
[[nodiscard]] int32_t stop_at(const Rect& track, int32_t column, int32_t stops) noexcept;

/// Draws a drop-down's field: the choice at its left and the arrow at its right.
///
/// @param canvas where it is drawn
/// @param field the field
/// @param look the choice it shows, and whether it is open
void draw_choice(const Canvas& canvas, const Rect& field, const ChoiceLook& look);

/// Draws a drop-down's open menu over what is under it.
///
/// @param canvas where it is drawn
/// @param menu the menu
/// @param look the items on screen, the chosen one and the marked one
void draw_choice_menu(const Canvas& canvas, const Rect& menu, const ChoiceMenuLook& look);

/// Returns how many items an open menu shows at once.
///
/// @param choices the choices it offers
/// @return at most the compact most_shown_choices
[[nodiscard]] int32_t shown_choices(std::size_t choices) noexcept;

/// The settings dialog's footer line at Compact: the row choice_menu keeps
/// a menu above unless it is given another.
inline constexpr int32_t compact_footer_line =
    compact_metrics.dialog_height - compact_metrics.edge - compact_metrics.footer_height - 1;

/// Returns where a drop-down's open menu lies: under its field, as wide as
/// the field and as tall as the items it shows, and over the field when that
/// would pass the screen's footer line.
///
/// @param field the drop-down's field
/// @param choices the choices it offers
/// @param footer_line the row of the screen's footer line: the settings
///     dialog's at its size class, Compact's when not given
/// @return the menu's rectangle
[[nodiscard]] Rect choice_menu(
    const Rect& field, std::size_t choices, int32_t footer_line = compact_footer_line
) noexcept;

/// Returns one shown item's rectangle in an open menu.
///
/// @param menu the menu
/// @param place the item's place among those shown, from 0
/// @return the item's rectangle, inside the menu's border
[[nodiscard]] Rect choice_item(const Rect& menu, int32_t place) noexcept;

/// Returns the shown item under a point.
///
/// @param menu the menu
/// @param x the point's column, in points
/// @param y the point's row, in points
/// @return the item's place among those shown, from 0; -1 when the point is
///     on the border or outside the menu
[[nodiscard]] int32_t choice_item_at(const Rect& menu, int32_t x, int32_t y) noexcept;

/// Moves a marked item by a number of choices, skipping a divider it would
/// land on, one further in the same direction.
///
/// Home and End are a move to the ends, which are not dividers. A step of 0
/// stays. A landing that cannot leave a divider stays where it was.
///
/// @param look the menu, whose items name the dividers
/// @param marked the item marked now, from 0
/// @param step how many choices to move, negative towards the first
/// @return the item to mark
[[nodiscard]] int32_t
choice_mark_toward(const ChoiceMenuLook& look, int32_t marked, int32_t step) noexcept;

/// Draws the padlock and a lock's text, ending at the area's right.
///
/// @param canvas where it is drawn
/// @param area the lock's area
/// @param look the text
void draw_lock(const Canvas& canvas, const Rect& area, const LockLook& look);

/// Draws a scroll bar: a well, and its thumb in the well's inner columns.
///
/// @param canvas where it is drawn
/// @param well the well
/// @param thumb the thumb
/// @param hot the pointer is over the bar, or a press on it is held
void draw_scroll_bar(const Canvas& canvas, const Rect& well, const Rect& thumb, bool hot);

/// The side of the attention dot the OA button wears, ring included, in points.
inline constexpr int32_t oa_button_dot_side = 7;

/// The dot's gap to the OA button's top and right edges, in points.
inline constexpr int32_t oa_button_dot_inset = 2;

/// Returns where the OA button's attention dot lies: oa_button_dot_side
/// points square, oa_button_dot_inset points in from the button's top and
/// right edges.
///
/// @param side the button's side, in points
/// @return the dot's square, from the button's top left corner, in points
[[nodiscard]] constexpr Rect oa_button_dot(int32_t side) noexcept {
    return {
        side - oa_button_dot_inset - oa_button_dot_side,
        oa_button_dot_inset,
        oa_button_dot_side,
        oa_button_dot_side
    };
}

/// Draws the OA button at the canvas's origin: a bevelled square, the icon
/// inset when the canvas has one, and otherwise the OA mark. With the dot,
/// the kit's attention dot is drawn over its top right corner
/// (oa_button_dot): a disc oa_button_dot_side points across in the warning
/// token (colour::lock) whose outer point is a ring in the outline token
/// (colour::control_border). Without it, nothing else is drawn.
///
/// @param canvas where it is drawn; its icon is the button's
/// @param side the square's side, in points
/// @param state how it looks
/// @param dot it wears the attention dot, as the main menu's does while
///     updates are waiting
void draw_oa_button(const Canvas& canvas, int32_t side, OaButtonState state, bool dot = false);

/// Draws the OA mark alone at the canvas's origin: the icon filling the
/// square, or without one the mark the OA button shows at rest. A side of
/// 0 or less draws nothing.
///
/// @param canvas where it is drawn; its icon is the mark's
/// @param side the square's side, in points
void draw_oa_mark(const Canvas& canvas, int32_t side);

/// Draws the header's mark: the icon filling the place, or without one the
/// small OA mark in its outlined square.
///
/// @param canvas where it is drawn; its icon is the mark's
/// @param place the place the icon fills
void draw_header_mark(const Canvas& canvas, const Rect& place);

/// Appends a button: one item and one control.
///
/// @param[in,out] list the display list
/// @param rect the button
/// @param look its look
/// @param id the control's number
/// @param name the control's automation name
void add_button(
    DisplayList& list, const Rect& rect, const ButtonLook& look, ControlId id, std::string name
);

/// Appends a switch: one item and one control. steps is set, and checked is
/// the switch's state.
///
/// @param[in,out] list the display list
/// @param rect the switch
/// @param look its look
/// @param id the control's number
/// @param name the control's automation name
void add_switch(
    DisplayList& list, const Rect& rect, const SwitchLook& look, ControlId id, std::string name
);

/// Appends a level strip: one item and one control. steps is set.
///
/// @param[in,out] list the display list
/// @param rect the strip
/// @param look its look
/// @param id the control's number
/// @param name the control's automation name
void add_levels(
    DisplayList& list, const Rect& rect, const LevelsLook& look, ControlId id, std::string name
);

/// Appends a slider: one item and one control. steps is set.
///
/// @param[in,out] list the display list
/// @param rect the slider's area
/// @param look its look
/// @param id the control's number
/// @param name the control's automation name
void add_slider(
    DisplayList& list, const Rect& rect, const SliderLook& look, ControlId id, std::string name
);

/// Appends a drop-down field: one item and one control. steps is set.
///
/// @param[in,out] list the display list
/// @param rect the field
/// @param look its look
/// @param id the control's number
/// @param name the control's automation name
void add_choice(
    DisplayList& list, const Rect& rect, const ChoiceLook& look, ControlId id, std::string name
);

/// Appends a focus ring. It adds no control.
///
/// @param[in,out] list the display list
/// @param control the control the ring outlines
/// @param around true draws it outside the control
void add_focus_ring(DisplayList& list, const Rect& control, bool around);

/// Appends a lock. It adds no control.
///
/// @param[in,out] list the display list
/// @param area the lock's area
/// @param look its text
void add_lock(DisplayList& list, const Rect& area, const LockLook& look);

/// Appends a mark. It adds no control.
///
/// @param[in,out] list the display list
/// @param at the mark's top left
/// @param mark which mark
/// @param ink the colour of its set pixels
void add_mark(DisplayList& list, Point at, Mark mark, Colour ink);

/// Draws a display list's items in list order. An item with a clip draws
/// only inside it. A generic role is drawn by the canvas's primitive of its
/// name; a component by its draw function.
///
/// @param canvas where the list is drawn
/// @param list the display list
void paint(const Canvas& canvas, const DisplayList& list);

} // namespace oa::ui::kit
