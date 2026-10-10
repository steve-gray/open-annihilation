// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Provisional: this header changes whenever OA's screens need it to, until the kit is declared stable (src/ui/kit/README.md).

// The OA UI kit's layout: points, the Auto scale and the three size classes,
// frames, arrangements and scroll areas, and one display list. Nothing here
// draws. Hit testing reads the list's controls. Drawing reads its items.
#pragma once

#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/kit/theme.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <stdint.h>
#include <string>
#include <vector>

namespace oa::ui::kit {

/// A rectangle in points. One point is one pixel of the 640 by 480 picture.
/// The same rectangle the settings geometry uses, so it passes straight through.
using Rect = oa::ui::frontend_renderer::SourceRect;

/// A place in a layout, in points.
struct Point {
    int32_t x{}; ///< column
    int32_t y{}; ///< row
};

/// The points kept clear on each side of a rectangle.
struct Insets {
    int32_t left{};   ///< columns clear on the left
    int32_t top{};    ///< rows clear on the top
    int32_t right{};  ///< columns clear on the right
    int32_t bottom{}; ///< rows clear on the bottom
};

/// Tells whether a point lies in a rectangle.
///
/// The rectangle's right and bottom edges are outside it. An empty rectangle
/// holds nothing.
///
/// @param rect the rectangle, in points
/// @param point the point, in points
/// @return true when the point lies inside the rectangle
[[nodiscard]] constexpr bool contains(const Rect& rect, Point point) noexcept {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.width &&
           point.y < rect.y + rect.height;
}

/// Returns the rectangle two rectangles share.
///
/// @param a a rectangle, in points
/// @param b another rectangle, in points
/// @return the shared rectangle; zero wide or high when they do not meet
[[nodiscard]] constexpr Rect intersect(const Rect& a, const Rect& b) noexcept {
    const int32_t left = std::max(a.x, b.x);
    const int32_t top = std::max(a.y, b.y);
    const int32_t right = std::min(a.x + a.width, b.x + b.width);
    const int32_t bottom = std::min(a.y + a.height, b.y + b.height);
    return {left, top, std::max(right - left, int32_t{0}), std::max(bottom - top, int32_t{0})};
}

/// Tells whether a rectangle lies wholly in another.
///
/// @param inner the rectangle
/// @param outer the rectangle it must lie in
/// @return true when no part of the inner rectangle lies outside the outer
[[nodiscard]] constexpr bool wholly_in(const Rect& inner, const Rect& outer) noexcept {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

/// Returns a rectangle grown by the same number of points on every side.
///
/// A negative growth shrinks it. The result may be empty.
///
/// @param rect the rectangle, in points
/// @param by the points added on each side
/// @return the grown rectangle
[[nodiscard]] constexpr Rect grown(const Rect& rect, int32_t by) noexcept {
    return {rect.x - by, rect.y - by, rect.width + 2 * by, rect.height + 2 * by};
}

/// Returns a rectangle brought in by insets.
///
/// @param rect the rectangle, in points
/// @param insets the points taken off each side
/// @return the inset rectangle, empty when the insets use it up
[[nodiscard]] constexpr Rect inset(const Rect& rect, const Insets& insets) noexcept {
    return {
        rect.x + insets.left,
        rect.y + insets.top,
        rect.width - insets.left - insets.right,
        rect.height - insets.top - insets.bottom,
    };
}

/// Returns the rectangle's point nearest another point.
///
/// Inside the rectangle the point is unchanged. Outside, it is the nearest
/// point of the rectangle's own area, its right-most column and bottom-most
/// row included. An empty rectangle returns its origin.
///
/// @param rect the rectangle, in points
/// @param point the point, in points
/// @return the nearest point of the rectangle
[[nodiscard]] constexpr Point nearest_point(const Rect& rect, Point point) noexcept {
    if (rect.width <= 0 || rect.height <= 0)
        return {rect.x, rect.y};
    return {
        std::clamp(point.x, rect.x, rect.x + rect.width - 1),
        std::clamp(point.y, rect.y, rect.y + rect.height - 1),
    };
}

/// The window height, in canvas pixels, that the Auto scale counts as one step.
inline constexpr int32_t auto_scale_height = 720;

/// Returns the Auto scale of a window: the canvas height over 720, rounded to
/// the nearest whole number and never below 1, and never below the display's
/// density, so a point is never smaller than a window point.
///
/// A half rounds away from zero. The height is the canvas, in pixels, not the
/// window's points.
///
/// @param canvas_height the canvas height, in pixels
/// @param density the canvas pixels per window point
/// @return the scale, a whole number of canvas pixels a point
[[nodiscard]] int32_t auto_scale(int32_t canvas_height, float density) noexcept;

/// How much room a layout has, from the points left after the scale.
enum class SizeClass : uint8_t {
    compact, ///< below 960 by 540 points
    regular, ///< from 960 by 540 points
    large,   ///< from 1280 by 720 points
};

/// The points at which a layout becomes Regular. Both sides must reach them.
inline constexpr Point regular_from{960, 540};
/// The points at which a layout becomes Large. Both sides must reach them.
inline constexpr Point large_from{1280, 720};

/// Returns the size class of a layout's room.
///
/// Large from large_from, Regular from regular_from, and Compact below that.
/// One side reaching a class does not: both the width and the height must.
///
/// @param width_points the room's width, in points
/// @param height_points the room's height, in points
/// @return the size class
[[nodiscard]] SizeClass size_class_of(int32_t width_points, int32_t height_points) noexcept;

/// The canvas a screen is laid out on.
struct Viewport {
    int32_t width{};            ///< canvas pixels
    int32_t height{};           ///< canvas pixels
    float density{1.0F};        ///< canvas pixels per window point
    int32_t scale_percent{100}; ///< the scale, as a percentage; 150 is one and a half
    Insets safe{};              ///< canvas pixels kept clear on each side
};

/// The room a screen lays out into, in points, and the scale it is drawn at.
struct Frame {
    Rect area{};                              ///< the canvas less its safe insets, in points
    SizeClass size_class{SizeClass::compact}; ///< the class of area
    int32_t scale_percent{100};               ///< the viewport's scale, as a percentage
};

/// Returns the frame of a viewport.
///
/// The area is the canvas less its safe insets, divided by the scale and
/// rounded down, placed at the insets divided the same way. Its class comes
/// from that size. A scale of 0 or less counts as 1% while dividing and is
/// kept as given on the frame.
///
/// @param viewport the canvas, its density, its scale and its insets
/// @return the frame
[[nodiscard]] Frame frame_of(const Viewport& viewport) noexcept;

/// Returns a rectangle of points as canvas pixels.
///
/// Each edge is that edge in points times the percentage, divided by 100 and
/// rounded down, so two rectangles that share an edge in points share it in
/// pixels, with no gap and no overlap. A scale of 0 or less counts as 1%.
///
/// @param points the rectangle, in points
/// @param scale_percent the scale, as a percentage
/// @return the rectangle, in canvas pixels
[[nodiscard]] Rect to_canvas(const Rect& points, int32_t scale_percent) noexcept;

/// Returns a canvas pixel as a point.
///
/// Each coordinate is the pixel times 100, divided by the percentage and
/// rounded down. A scale of 0 or less counts as 1%.
///
/// @param canvas the pixel
/// @param scale_percent the scale, as a percentage
/// @return the point
[[nodiscard]] Point to_points(Point canvas, int32_t scale_percent) noexcept;

/// Places rectangles down an area, each as wide as the area.
///
/// The heights are as given. The gap lies between them, not after the last.
/// The column starts at the area's top and may run past its bottom.
///
/// @param area the area, in points
/// @param heights each rectangle's height, in points, top to bottom
/// @param gap the points between two rectangles
/// @return one rectangle per height
[[nodiscard]] std::vector<Rect>
column(const Rect& area, std::span<const int32_t> heights, int32_t gap);

/// Places rectangles across an area, each as tall as the area.
///
/// The widths and the gaps between them are one group, set to the left, the
/// centre or the right of the area. At the centre, spare points go to the
/// right of the group. The group may be wider than the area.
///
/// @param area the area, in points
/// @param widths each rectangle's width, in points, left to right
/// @param gap the points between two rectangles
/// @param align where the group sits in the area
/// @return one rectangle per width
[[nodiscard]] std::vector<Rect>
row(const Rect& area, std::span<const int32_t> widths, int32_t gap, Align align);

/// Places cells in rows across an area.
///
/// As many columns as fit at the least width, and at least one when there is
/// a cell to place. The cells of a full row widen to fill the row; spare
/// points go to the cells on the left, one each. A short last row keeps those
/// widths and starts at the left. A least width of 0 or less is one column.
///
/// @param area the area, in points
/// @param least_cell_width the narrowest a cell may be and still add a column, in points
/// @param cell_height each cell's height, in points
/// @param gap the points between cells
/// @param count the cells
/// @return the cells, row by row
[[nodiscard]] std::vector<Rect> grid(
    const Rect& area, int32_t least_cell_width, int32_t cell_height, int32_t gap, std::size_t count
);

/// Two rectangles a split makes of one area.
struct Split {
    Rect first{};  ///< the first part
    Rect second{}; ///< the rest, after the gap
};

/// Splits an area across, the first part a given width.
///
/// The second starts the gap after the first and ends at the area's right
/// edge. When that leaves it nothing, its width is 0.
///
/// @param area the area, in points
/// @param first_width the first part's width, in points
/// @param gap the points between the parts
/// @return the two parts, each as tall as the area
[[nodiscard]] Split split_across(const Rect& area, int32_t first_width, int32_t gap);

/// Splits an area down, the first part a given height.
///
/// The second starts the gap under the first and ends at the area's bottom
/// edge. When that leaves it nothing, its height is 0.
///
/// @param area the area, in points
/// @param first_height the first part's height, in points
/// @param gap the points between the parts
/// @return the two parts, each as wide as the area
[[nodiscard]] Split split_down(const Rect& area, int32_t first_height, int32_t gap);

/// Where a list scrolls: the view, the bar, and how far the content has moved.
///
/// The limit is the caller's. It is at least the content's height less the
/// view's, and more while the content scrolls on so a line is not cut by the
/// view's edge. It is not derived again here.
struct ScrollArea {
    Rect view{};              ///< what the content is seen through
    Rect well{};              ///< the scroll bar's well, as high as the view
    Rect hit{};               ///< where a press holds the scroll bar
    int32_t page_step{};      ///< how far a page step moves the offset, in points
    int32_t content_height{}; ///< the content's height, in points
    int32_t limit{};          ///< the greatest offset, in points
    int32_t offset{};         ///< how far the content is moved up, in points

    /// Returns the greatest offset that shows the end of a content in a view.
    ///
    /// @param content_height the content's height, in points
    /// @param view_height the view's height, in points
    /// @return the content's height less the view's, or 0 when the content fits
    [[nodiscard]] static int32_t limit_for(int32_t content_height, int32_t view_height) noexcept;

    /// Returns the offset nearest the open one that shows an item whole.
    ///
    /// The item's top, before scrolling, may come up to the view's first row.
    /// The row under the item, or the limit when the item is the last, may
    /// come down to the view's last row. An item taller than the view shows
    /// its top.
    ///
    /// @param content_top the item's top before scrolling, in points
    /// @param height the item's height, in points
    /// @param last true when the item is the last
    /// @return the offset, from 0 to the limit
    [[nodiscard]] int32_t
    offset_showing(int32_t content_top, int32_t height, bool last) const noexcept;

    /// Returns the scroll bar's thumb.
    ///
    /// The thumb sits inside the well's border. It is as tall as the view's
    /// share of the content, and never under the least height, and as far
    /// down its travel as the offset is down the limit, to the nearest row.
    ///
    /// @param least_height the least height of the thumb, in points
    /// @return the thumb, in points
    [[nodiscard]] Rect thumb(int32_t least_height) const noexcept;

    /// Returns the offset that puts the thumb's top on a row.
    ///
    /// @param thumb_top the thumb's top row, clamped to its travel
    /// @param least_height the least height of the thumb, in points
    /// @return the offset, from 0 to the limit
    [[nodiscard]] int32_t offset_at(int32_t thumb_top, int32_t least_height) const noexcept;

    /// Returns a content rectangle moved up by the offset.
    ///
    /// An empty rectangle is returned as it was given.
    ///
    /// @param content the rectangle before scrolling, in points
    /// @return the rectangle as placed, in points
    [[nodiscard]] Rect placed(const Rect& content) const noexcept;
};

/// A control's number. no_control is none.
using ControlId = int32_t;

/// No control.
inline constexpr ControlId no_control = -1;

/// What an item of a display list is. Later components add roles after mark.
enum class Role : uint8_t {
    fill,    ///< a flat rectangle
    blend,   ///< a rectangle blended by the item's opacity
    outline, ///< a one-pixel outline
    bevel,   ///< a raised edge
    rule,    ///< a hairline
    text,    ///< a text
    picture, ///< a picture
    mark,    ///< a one-bit mark
};

/// How a control looks, for the item that draws it.
struct State {
    bool hovered{};  ///< the pointer is over it
    bool pressed{};  ///< it is held
    bool focused{};  ///< the keyboard focus is on it
    bool selected{}; ///< it is the chosen one of its group
    bool on{};       ///< a switch is on
    bool locked{};   ///< it shows and takes no change
    bool disabled{}; ///< it takes nothing
    friend constexpr bool operator==(State, State) = default;
};

/// One thing a screen draws, in the order it is drawn.
struct Item {
    Role role{};                   ///< what is drawn
    Rect rect{};                   ///< where, in points
    Rect clip{};                   ///< what drawing may touch; empty means none
    std::string text{};            ///< a text item's words
    FontRole font{};               ///< which font a text is drawn in
    int32_t tracking{};            ///< extra columns after each glyph but the last
    Align align{};                 ///< where a text sits in its rectangle
    Colour colour{};               ///< the colour
    uint32_t opacity{};            ///< a blend's share of a pixel, in 256ths; 256 is opaque
    ControlId control{no_control}; ///< the control this draws, or none
    State state{};                 ///< how that control looks
};

/// What a control is. A screen maps these when it lists controls to automation.
enum class ControlKind : uint8_t {
    button,     ///< a button
    toggle,     ///< a switch
    choice,     ///< a drop-down
    slider,     ///< a slider
    levels,     ///< a strip of levels
    buttons,    ///< a row of buttons
    list,       ///< a list
    list_item,  ///< a row of a list
    text_field, ///< a field the player types in
    scroll_bar, ///< a scroll bar
    tab,        ///< a tab
    link,       ///< a link
    area,       ///< a place that takes a press and is not one of the others
};

/// A control a press can reach. The rectangle is where it lies, scrolled out
/// of view or not. What a press reaches is the rectangle clipped, when a clip
/// is set. Its name is how automation finds it.
struct Control {
    ControlId id{no_control}; ///< its number
    Rect rect{};              ///< where it lies, in points
    Rect clip{};              ///< the part a press can reach; empty means the whole rectangle
    bool enabled{true};       ///< false while a press does not reach it
    /// Its automation name: words of a-z, 0-9 and hyphens, joined by dots, at
    /// most 100 bytes, and unique on the screen. The automation endpoint adds
    /// its own prefix; the name here has none.
    std::string name{};
    ControlKind kind{};   ///< what it is
    bool focusable{true}; ///< the arrows and Tab can stop on it
    /// Left and Right act on it, as a switch or a slider takes them, instead
    /// of moving the focus.
    bool steps{};
    int32_t group{-1};  ///< its scroll area's number; -1 when it is in none
    bool checked{};     ///< a switch that is on, or a chosen row, for automation
    std::string text{}; ///< its caption or its value, for automation
};

/// What a screen drew, and where its controls are.
///
/// Items are in drawing order. Controls are in the order a point is tested,
/// and the two lists are not one list: drawing never reads a control, and a
/// hit never reads an item. Tab follows tab_order.
struct DisplayList {
    std::vector<Item> items{};       ///< what is drawn, first to last
    std::vector<Control> controls{}; ///< what can be pressed, first tested first
    /// The declared order Tab follows. Only these controls take the focus.
    std::vector<ControlId> tab_order{};
};

/// Returns the first enabled control a point lies in.
///
/// Controls are tried in list order. A control with a clip is reached only
/// in the part its rectangle shares with the clip; with no clip, in its
/// rectangle. A disabled control is skipped.
///
/// @param list the display list
/// @param point the point, in the same points as the controls
/// @return that control's number, or no_control when none holds the point
[[nodiscard]] ControlId hit(const DisplayList& list, Point point) noexcept;

/// Returns a list's control of a number.
///
/// @param list the display list
/// @param id the control's number
/// @return the control, or null when the list has none of that number; the first when it has several
[[nodiscard]] const Control* control_of(const DisplayList& list, ControlId id) noexcept;

} // namespace oa::ui::kit
