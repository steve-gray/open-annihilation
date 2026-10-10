// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The kit's points, Auto scale, size classes, arrangements, scroll areas and
// display list. The scroll numbers were computed with geometry::scroll_thumb,
// geometry::scroll_at and geometry::scroll_showing at c5e005df, the start of
// this branch, by a throwaway program that was not committed.

#include "oa/test/check.hpp"
#include "oa/ui/kit/layout.hpp"

#include <cstddef>
#include <stdint.h>
#include <vector>

namespace {

namespace kit = oa::ui::kit;

/// Tells whether a rectangle is the one named by its edges' place and size.
///
/// @param rect the rectangle
/// @param x its left column
/// @param y its top row
/// @param width its columns
/// @param height its rows
/// @return true when every field matches
bool rect_is(kit::Rect rect, int32_t x, int32_t y, int32_t width, int32_t height) {
    return rect.x == x && rect.y == y && rect.width == width && rect.height == height;
}

/// One row of the design's window table, and the frame it makes at density 1.
struct Window {
    int32_t width{};         ///< canvas pixels
    int32_t height{};        ///< canvas pixels
    int32_t scale{};         ///< the Auto scale
    int32_t points_width{};  ///< the frame's width, in points
    int32_t points_height{}; ///< the frame's height, in points
    kit::SizeClass size_class{kit::SizeClass::compact};
};

/// The design's six windows, at one canvas pixel a window point.
constexpr Window windows[] = {
    {640, 480, 1, 640, 480, kit::SizeClass::compact},
    {1024, 768, 1, 1024, 768, kit::SizeClass::regular},
    {1280, 720, 1, 1280, 720, kit::SizeClass::large},
    {1920, 1080, 2, 960, 540, kit::SizeClass::regular},
    {2560, 1440, 2, 1280, 720, kit::SizeClass::large},
    {3840, 2160, 3, 1280, 720, kit::SizeClass::large},
};

/// Checks a point against a rectangle's inside and its edges.
void a_point_lies_inside_and_not_on_the_far_edges() {
    const kit::Rect rect{0, 0, 10, 10};
    OA_CHECK(kit::contains(rect, {0, 0}));
    OA_CHECK(kit::contains(rect, {9, 9}));
    OA_CHECK(!kit::contains(rect, {10, 5}));
    OA_CHECK(!kit::contains(rect, {5, 10}));
    OA_CHECK(!kit::contains(rect, {-1, 0}));
    OA_CHECK(rect_is(kit::intersect(rect, {8, 8, 10, 10}), 8, 8, 2, 2));
    OA_CHECK(rect_is(kit::intersect(rect, {20, 20, 4, 4}), 20, 20, 0, 0));
    OA_CHECK(kit::wholly_in({2, 2, 3, 3}, rect));
    OA_CHECK(kit::wholly_in(rect, rect));
    OA_CHECK(!kit::wholly_in({0, 0, 11, 10}, rect));
    OA_CHECK(rect_is(kit::grown({5, 5, 4, 4}, 2), 3, 3, 8, 8));
    OA_CHECK(rect_is(kit::inset({0, 0, 20, 20}, {1, 2, 3, 4}), 1, 2, 16, 14));
    const kit::Point inside = kit::nearest_point(rect, {3, 4});
    OA_CHECK(inside.x == 3 && inside.y == 4);
    const kit::Point outside = kit::nearest_point(rect, {-2, 20});
    OA_CHECK(outside.x == 0 && outside.y == 9);
    const kit::Point empty = kit::nearest_point({5, 6, 0, 4}, {100, 100});
    OA_CHECK(empty.x == 5 && empty.y == 6);
}

/// Checks the Auto scale at density 1, and where the density raises it.
void the_scale_follows_the_canvas_and_the_density() {
    constexpr int32_t heights[] = {480, 720, 768, 1079, 1080, 1440, 1800, 2160};
    constexpr int32_t scales[] = {1, 1, 1, 1, 2, 2, 3, 3};
    for (std::size_t index = 0; index < 8; ++index)
        OA_CHECK(kit::auto_scale(heights[index], 1.0F) == scales[index]);
    // A 786-row canvas rounds to 1 from its height, and a 1179-row canvas to 2.
    OA_CHECK(kit::auto_scale(786, 2.0F) == 2);
    OA_CHECK(kit::auto_scale(786, 3.0F) == 3);
    OA_CHECK(kit::auto_scale(1179, 2.0F) == 2);
    OA_CHECK(kit::auto_scale(1179, 3.0F) == 3);
}

/// Checks each window of the design's table, and a phone whose insets come off first.
void the_frame_matches_the_window_table() {
    for (const Window& window : windows) {
        OA_CHECK(kit::auto_scale(window.height, 1.0F) == window.scale);
        kit::Viewport viewport;
        viewport.width = window.width;
        viewport.height = window.height;
        viewport.density = 1.0F;
        viewport.scale_percent = window.scale * 100;
        const kit::Frame frame = kit::frame_of(viewport);
        OA_CHECK(frame.scale_percent == window.scale * 100);
        OA_CHECK(frame.size_class == window.size_class);
        OA_CHECK(rect_is(frame.area, 0, 0, window.points_width, window.points_height));
    }
    // One side reaching a class does not.
    OA_CHECK(kit::size_class_of(1279, 720) == kit::SizeClass::regular);
    OA_CHECK(kit::size_class_of(1280, 719) == kit::SizeClass::regular);
    OA_CHECK(kit::size_class_of(959, 540) == kit::SizeClass::compact);
    OA_CHECK(kit::size_class_of(960, 539) == kit::SizeClass::compact);

    // 2880 by 1800 at density 2: the height rounds to 3, above the density.
    OA_CHECK(kit::auto_scale(1800, 2.0F) == 3);
    kit::Viewport hidpi;
    hidpi.width = 2880;
    hidpi.height = 1800;
    hidpi.density = 2.0F;
    hidpi.scale_percent = 300;
    const kit::Frame wide = kit::frame_of(hidpi);
    OA_CHECK(wide.size_class == kit::SizeClass::regular);
    OA_CHECK(rect_is(wide.area, 0, 0, 960, 600));

    // A 1704 by 786 canvas at 2 canvas pixels a window point. The insets are
    // 118, 0, 118 and 42 canvas pixels: 59 points at each side and 21 at the
    // bottom, times 2.
    OA_CHECK(kit::auto_scale(786, 2.0F) == 2);
    kit::Viewport phone;
    phone.width = 1704;
    phone.height = 786;
    phone.density = 2.0F;
    phone.scale_percent = 200;
    phone.safe = {118, 0, 118, 42};
    const kit::Frame frame = kit::frame_of(phone);
    OA_CHECK(frame.scale_percent == 200);
    OA_CHECK(frame.size_class == kit::SizeClass::compact);
    OA_CHECK(rect_is(frame.area, 59, 0, 734, 372));
}

/// Checks that neighbouring rectangles share an edge at each scale, and that a
/// whole scale maps a rectangle back onto itself.
void canvas_pixels_tile_and_whole_scales_round_trip() {
    const kit::Rect left{0, 0, 10, 10};
    const kit::Rect right{10, 0, 10, 10};
    const kit::Rect below{0, 10, 10, 8};
    constexpr int32_t percentages[] = {100, 150, 200, 300};
    for (const int32_t percent : percentages) {
        const kit::Rect across = kit::to_canvas(left, percent);
        const kit::Rect beside = kit::to_canvas(right, percent);
        const kit::Rect under = kit::to_canvas(below, percent);
        OA_CHECK(across.x + across.width == beside.x);
        OA_CHECK(across.y == beside.y);
        OA_CHECK(across.width > 0 && beside.width > 0);
        OA_CHECK(across.y + across.height == under.y);
        OA_CHECK(across.x == under.x);
    }
    OA_CHECK(rect_is(kit::to_canvas(left, 150), 0, 0, 15, 15));
    OA_CHECK(rect_is(kit::to_canvas(right, 150), 15, 0, 15, 15));

    const kit::Rect samples[] = {{0, 0, 1, 1}, {3, 5, 7, 11}, {10, 20, 30, 40}};
    constexpr int32_t whole[] = {100, 200, 300};
    for (const kit::Rect& sample : samples) {
        for (const int32_t percent : whole) {
            const kit::Rect canvas = kit::to_canvas(sample, percent);
            const kit::Point origin = kit::to_points({canvas.x, canvas.y}, percent);
            const kit::Point far =
                kit::to_points({canvas.x + canvas.width, canvas.y + canvas.height}, percent);
            OA_CHECK(origin.x == sample.x && origin.y == sample.y);
            OA_CHECK(far.x - origin.x == sample.width);
            OA_CHECK(far.y - origin.y == sample.height);
        }
    }
}

/// Checks a column, a row at each alignment, a grid and the two splits.
void arrangements_place_the_rectangles() {
    const kit::Rect area{10, 20, 100, 80};
    const int32_t heights[] = {15, 25, 5};
    const std::vector<kit::Rect> down = kit::column(area, heights, 4);
    OA_CHECK(down.size() == 3);
    OA_CHECK(rect_is(down[0], 10, 20, 100, 15));
    OA_CHECK(rect_is(down[1], 10, 39, 100, 25));
    OA_CHECK(rect_is(down[2], 10, 68, 100, 5));
    OA_CHECK(kit::column(area, {}, 4).empty());

    const kit::Rect band{0, 0, 100, 16};
    const int32_t widths[] = {10, 20, 30};
    const std::vector<kit::Rect> at_left = kit::row(band, widths, 5, kit::Align::left);
    const std::vector<kit::Rect> at_centre = kit::row(band, widths, 5, kit::Align::centre);
    const std::vector<kit::Rect> at_right = kit::row(band, widths, 5, kit::Align::right);
    OA_CHECK(at_left.size() == 3 && at_centre.size() == 3 && at_right.size() == 3);
    OA_CHECK(rect_is(at_left[0], 0, 0, 10, 16));
    OA_CHECK(rect_is(at_left[1], 15, 0, 20, 16));
    OA_CHECK(rect_is(at_left[2], 40, 0, 30, 16));
    OA_CHECK(rect_is(at_centre[0], 15, 0, 10, 16));
    OA_CHECK(rect_is(at_centre[1], 30, 0, 20, 16));
    OA_CHECK(rect_is(at_centre[2], 55, 0, 30, 16));
    OA_CHECK(rect_is(at_right[0], 30, 0, 10, 16));
    OA_CHECK(rect_is(at_right[1], 45, 0, 20, 16));
    OA_CHECK(rect_is(at_right[2], 70, 0, 30, 16));

    // Three cells in a row whose width is exactly three least widths and two gaps.
    const std::vector<kit::Rect> exact = kit::grid({0, 0, 70, 40}, 20, 10, 5, 3);
    OA_CHECK(exact.size() == 3);
    OA_CHECK(rect_is(exact[0], 0, 0, 20, 10));
    OA_CHECK(rect_is(exact[1], 25, 0, 20, 10));
    OA_CHECK(rect_is(exact[2], 50, 0, 20, 10));

    // Seven cells: three columns, the spare two points on the left-hand cells.
    const std::vector<kit::Rect> seven = kit::grid({8, 4, 100, 80}, 30, 12, 4, 7);
    OA_CHECK(seven.size() == 7);
    OA_CHECK(rect_is(seven[0], 8, 4, 31, 12));
    OA_CHECK(rect_is(seven[1], 43, 4, 31, 12));
    OA_CHECK(rect_is(seven[2], 78, 4, 30, 12));
    OA_CHECK(rect_is(seven[3], 8, 20, 31, 12));
    OA_CHECK(rect_is(seven[4], 43, 20, 31, 12));
    OA_CHECK(rect_is(seven[5], 78, 20, 30, 12));
    OA_CHECK(rect_is(seven[6], 8, 36, 31, 12));
    OA_CHECK(seven[2].x + seven[2].width == 108);
    OA_CHECK(kit::grid(area, 20, 10, 4, 0).empty());

    const kit::Split across = kit::split_across(area, 40, 6);
    OA_CHECK(rect_is(across.first, 10, 20, 40, 80));
    OA_CHECK(rect_is(across.second, 56, 20, 54, 80));
    const kit::Split down_split = kit::split_down(area, 30, 6);
    OA_CHECK(rect_is(down_split.first, 10, 20, 100, 30));
    OA_CHECK(rect_is(down_split.second, 10, 56, 100, 44));
    const kit::Split none_left = kit::split_across(area, 100, 10);
    OA_CHECK(none_left.second.width == 0 && none_left.second.height == 80);
}

/// Graphics' scroll area, and the values computed for it with the settings
/// dialog's scroll functions.
///
/// view {158, 54, 309, 236}, well {470, 54, 7, 236}, content height 749,
/// limit 513, least thumb height 16. The half offset is 256, half of 513
/// rounded down. The well's top row is 54 and its bottom row is 289.
void the_scroll_matches_the_settings_dialog() {
    kit::ScrollArea area;
    area.view = {158, 54, 309, 236};
    area.well = {470, 54, 7, 236};
    area.content_height = 749;
    area.limit = 513;
    OA_CHECK(kit::ScrollArea::limit_for(749, 236) == 513);
    OA_CHECK(kit::ScrollArea::limit_for(100, 236) == 0);
    OA_CHECK(kit::ScrollArea::limit_for(236, 236) == 0);

    area.offset = 0;
    OA_CHECK(rect_is(area.thumb(16), 471, 55, 5, 73));
    area.offset = 1;
    OA_CHECK(rect_is(area.thumb(16), 471, 55, 5, 73));
    area.offset = 256;
    OA_CHECK(rect_is(area.thumb(16), 471, 135, 5, 73));
    // The thumb's top at offset 256 reads back as 255.
    OA_CHECK(area.offset_at(135, 16) == 255);
    area.offset = 513;
    OA_CHECK(rect_is(area.thumb(16), 471, 216, 5, 73));
    OA_CHECK(area.offset_at(216, 16) == 513);
    OA_CHECK(area.offset_at(54, 16) == 0);
    OA_CHECK(area.offset_at(289, 16) == 513);

    // The limit is the area's own, here 600 rather than 749 - 236.
    area.limit = 600;
    area.offset = 300;
    OA_CHECK(rect_is(area.thumb(16), 471, 136, 5, 73));
    OA_CHECK(area.offset_at(136, 16) == 302);
    area.limit = 513;

    area.offset = 0;
    OA_CHECK(area.offset_showing(54, 28, false) == 0);
    OA_CHECK(area.offset_showing(400, 36, false) == 147);
    area.offset = 400;
    OA_CHECK(area.offset_showing(80, 20, false) == 26);
    area.offset = 100;
    OA_CHECK(area.offset_showing(700, 49, true) == 513);
    area.offset = 1;
    OA_CHECK(area.offset_showing(400, 36, false) == 147);
    area.offset = 256;
    OA_CHECK(area.offset_showing(400, 36, false) == 256);
    area.offset = 513;
    OA_CHECK(area.offset_showing(400, 36, false) == 346);
    area.offset = 200;
    OA_CHECK(area.offset_showing(400, 36, false) == 200);

    area.offset = 40;
    OA_CHECK(rect_is(area.placed({158, 400, 40, 16}), 158, 360, 40, 16));
    OA_CHECK(rect_is(area.placed({158, 400, 0, 16}), 158, 400, 0, 16));
    OA_CHECK(rect_is(area.placed({158, 400, 40, 0}), 158, 400, 40, 0));
}

/// Checks hit testing: list order, a clip, a disabled control and an empty list.
void a_point_hits_the_first_enabled_control() {
    OA_CHECK(kit::hit({}, {0, 0}) == kit::no_control);

    kit::DisplayList list;
    list.controls.push_back(kit::Control{1, {0, 0, 20, 20}, {}, true});
    list.controls.push_back(kit::Control{2, {10, 10, 20, 20}, {}, true});
    OA_CHECK(kit::hit(list, {12, 12}) == 1);
    OA_CHECK(kit::hit(list, {25, 25}) == 2);
    OA_CHECK(kit::hit(list, {30, 30}) == kit::no_control);

    kit::DisplayList clipped;
    clipped.controls.push_back(kit::Control{7, {0, 0, 40, 40}, {10, 10, 10, 10}, true});
    OA_CHECK(kit::hit(clipped, {5, 5}) == kit::no_control);
    OA_CHECK(kit::hit(clipped, {15, 15}) == 7);
    OA_CHECK(kit::hit(clipped, {19, 19}) == 7);
    OA_CHECK(kit::hit(clipped, {20, 20}) == kit::no_control);

    kit::DisplayList disabled;
    disabled.controls.push_back(kit::Control{3, {0, 0, 10, 10}, {}, false});
    disabled.controls.push_back(kit::Control{4, {0, 0, 10, 10}, {}, true});
    OA_CHECK(kit::hit(disabled, {1, 1}) == 4);
    disabled.controls.pop_back();
    OA_CHECK(kit::hit(disabled, {1, 1}) == kit::no_control);

    // An empty clip is no clip. An item is not a control, so a point on one misses.
    kit::DisplayList drawn;
    kit::Item item;
    item.role = kit::Role::fill;
    item.rect = {0, 0, 50, 50};
    item.control = 9;
    drawn.items.push_back(item);
    drawn.controls.push_back(kit::Control{5, {0, 0, 10, 10}, {100, 100, 0, 0}, true});
    OA_CHECK(kit::hit(drawn, {1, 1}) == 5);
    OA_CHECK(kit::hit(drawn, {30, 30}) == kit::no_control);
    const kit::Control* found = kit::control_of(drawn, 5);
    OA_CHECK(found != nullptr && found->id == 5);
    OA_CHECK(kit::control_of(drawn, 9) == nullptr);
}

} // namespace

int main() {
    a_point_lies_inside_and_not_on_the_far_edges();
    the_scale_follows_the_canvas_and_the_density();
    the_frame_matches_the_window_table();
    canvas_pixels_tile_and_whole_scales_round_trip();
    arrangements_place_the_rectangles();
    the_scroll_matches_the_settings_dialog();
    a_point_hits_the_first_enabled_control();
    return oa::test::check_exit_status();
}
