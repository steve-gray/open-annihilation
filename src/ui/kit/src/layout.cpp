// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Points, scale, size classes, arrangements, scroll areas and the display list.

#include "oa/ui/kit/layout.hpp"

#include <algorithm>
#include <cmath>
#include <stdint.h>

namespace oa::ui::kit {

namespace {

/// Returns floor(value * numerator / denominator). A denominator of 0 or less counts as 1.
///
/// @param value the value
/// @param numerator the multiplier
/// @param denominator the divisor
/// @return the rounded-down product
[[nodiscard]] int32_t
floor_mul_div(int32_t value, int32_t numerator, int32_t denominator) noexcept {
    const int32_t divisor = denominator > 0 ? denominator : 1;
    const int64_t product = static_cast<int64_t>(value) * numerator;
    int64_t quotient = product / divisor;
    if (product % divisor < 0)
        --quotient;
    return static_cast<int32_t>(quotient);
}

/// Returns the percentage a scale divides by. 0 or less counts as 1.
///
/// @param scale_percent the scale, as a percentage
/// @return the percentage, at least 1
[[nodiscard]] int32_t percent_of(int32_t scale_percent) noexcept {
    return scale_percent > 0 ? scale_percent : 1;
}

/// Returns how many columns fit in a width at a least cell width.
///
/// @param width the row's width, in points
/// @param least the least cell width, in points
/// @param gap the points between cells
/// @param count the cells to place
/// @return the columns, at least one when count is not 0
[[nodiscard]] int32_t
columns_across(int32_t width, int32_t least, int32_t gap, std::size_t count) noexcept {
    if (count == 0)
        return 0;
    int32_t columns = 1;
    if (least > 0) {
        const int64_t stride = static_cast<int64_t>(least) + gap;
        if (stride > 0 && width >= least) {
            const int64_t fit = (static_cast<int64_t>(width) + gap) / stride;
            if (fit > columns)
                columns = static_cast<int32_t>(fit);
        }
    }
    if (static_cast<std::size_t>(columns) > count)
        columns = static_cast<int32_t>(count);
    return columns;
}

/// Shares a length across parts. The first extra parts are one longer.
///
/// @param total the length, in points
/// @param parts how many parts
/// @param[out] base each part's length before the spare
/// @param[out] extra how many parts take one more point
void share(int32_t total, int32_t parts, int32_t& base, int32_t& extra) noexcept {
    if (parts <= 0) {
        base = 0;
        extra = 0;
        return;
    }
    int64_t quotient = static_cast<int64_t>(total) / parts;
    int64_t remainder = static_cast<int64_t>(total) % parts;
    if (remainder < 0) {
        --quotient;
        remainder += parts;
    }
    base = static_cast<int32_t>(quotient);
    extra = static_cast<int32_t>(remainder);
}

/// Returns the second part of a split, empty when it would have no room.
///
/// @param origin the area's origin on this axis
/// @param extent the area's extent on this axis
/// @param first the first part's extent
/// @param gap the points between the parts
/// @param[out] start the second part's origin
/// @return the second part's extent, 0 when none is left
[[nodiscard]] int32_t
rest_of(int32_t origin, int32_t extent, int32_t first, int32_t gap, int32_t& start) noexcept {
    start = origin + first + gap;
    const int32_t room = origin + extent - start;
    return room > 0 ? room : 0;
}

} // namespace

int32_t auto_scale(int32_t canvas_height, float density) noexcept {
    const long scale = std::max(
        {long{1},
         std::lround(static_cast<double>(canvas_height) / static_cast<double>(auto_scale_height)),
         std::lround(static_cast<double>(density))}
    );
    return static_cast<int32_t>(scale);
}

SizeClass size_class_of(int32_t width_points, int32_t height_points) noexcept {
    if (width_points >= large_from.x && height_points >= large_from.y)
        return SizeClass::large;
    if (width_points >= regular_from.x && height_points >= regular_from.y)
        return SizeClass::regular;
    return SizeClass::compact;
}

Frame frame_of(const Viewport& viewport) noexcept {
    const int32_t percent = percent_of(viewport.scale_percent);
    const int32_t inner_width = viewport.width - viewport.safe.left - viewport.safe.right;
    const int32_t inner_height = viewport.height - viewport.safe.top - viewport.safe.bottom;
    Frame frame;
    frame.area = {
        floor_mul_div(viewport.safe.left, 100, percent),
        floor_mul_div(viewport.safe.top, 100, percent),
        floor_mul_div(inner_width, 100, percent),
        floor_mul_div(inner_height, 100, percent),
    };
    frame.size_class = size_class_of(frame.area.width, frame.area.height);
    frame.scale_percent = viewport.scale_percent;
    return frame;
}

Viewport layer_viewport(const Viewport& canvas, int32_t chosen_percent) noexcept {
    const int32_t chosen =
        chosen_percent > 0 ? chosen_percent / 100 : auto_scale(canvas.height, canvas.density);
    // The largest whole step at which Compact's dialog fits the room.
    const int32_t room_width = canvas.width - canvas.safe.left - canvas.safe.right;
    const int32_t room_height = canvas.height - canvas.safe.top - canvas.safe.bottom;
    const int32_t fits = std::min(
        room_width / compact_metrics.dialog_width, room_height / compact_metrics.dialog_height
    );
    Viewport viewport = canvas;
    viewport.scale_percent = std::max(int32_t{1}, std::min(chosen, fits)) * 100;
    return viewport;
}

Rect to_canvas(const Rect& points, int32_t scale_percent) noexcept {
    const int32_t percent = percent_of(scale_percent);
    const int32_t left = floor_mul_div(points.x, percent, 100);
    const int32_t top = floor_mul_div(points.y, percent, 100);
    const int32_t right = floor_mul_div(points.x + points.width, percent, 100);
    const int32_t bottom = floor_mul_div(points.y + points.height, percent, 100);
    return {left, top, right - left, bottom - top};
}

Point to_points(Point canvas, int32_t scale_percent) noexcept {
    const int32_t percent = percent_of(scale_percent);
    return {floor_mul_div(canvas.x, 100, percent), floor_mul_div(canvas.y, 100, percent)};
}

std::vector<Rect> column(const Rect& area, std::span<const int32_t> heights, int32_t gap) {
    std::vector<Rect> placed;
    placed.reserve(heights.size());
    int32_t y = area.y;
    for (std::size_t index = 0; index < heights.size(); ++index) {
        placed.push_back(Rect{area.x, y, area.width, heights[index]});
        y += heights[index];
        if (index + 1 < heights.size())
            y += gap;
    }
    return placed;
}

std::vector<Rect> row(const Rect& area, std::span<const int32_t> widths, int32_t gap, Align align) {
    int64_t total = 0;
    for (const int32_t width : widths)
        total += width;
    if (widths.size() > 1)
        total += static_cast<int64_t>(gap) * static_cast<int64_t>(widths.size() - 1);
    int32_t x = area.x;
    const int64_t spare = static_cast<int64_t>(area.width) - total;
    if (align == Align::right)
        x += static_cast<int32_t>(spare);
    else if (align == Align::centre)
        x += static_cast<int32_t>(spare / 2);
    std::vector<Rect> placed;
    placed.reserve(widths.size());
    for (std::size_t index = 0; index < widths.size(); ++index) {
        placed.push_back(Rect{x, area.y, widths[index], area.height});
        x += widths[index];
        if (index + 1 < widths.size())
            x += gap;
    }
    return placed;
}

std::vector<Rect> grid(
    const Rect& area, int32_t least_cell_width, int32_t cell_height, int32_t gap, std::size_t count
) {
    std::vector<Rect> cells;
    const int32_t columns = columns_across(area.width, least_cell_width, gap, count);
    if (columns <= 0)
        return cells;
    int32_t base = 0;
    int32_t extra = 0;
    share(area.width - gap * (columns - 1), columns, base, extra);
    cells.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const int32_t column_index =
            static_cast<int32_t>(index % static_cast<std::size_t>(columns));
        const int32_t row_index = static_cast<int32_t>(index / static_cast<std::size_t>(columns));
        int32_t x = area.x;
        for (int32_t column = 0; column < column_index; ++column)
            x += base + (column < extra ? 1 : 0) + gap;
        const int32_t width = base + (column_index < extra ? 1 : 0);
        const int32_t y = area.y + row_index * (cell_height + gap);
        cells.push_back(Rect{x, y, width, cell_height});
    }
    return cells;
}

Split split_across(const Rect& area, int32_t first_width, int32_t gap) {
    int32_t x = 0;
    const int32_t width = rest_of(area.x, area.width, first_width, gap, x);
    return Split{
        Rect{area.x, area.y, first_width, area.height}, Rect{x, area.y, width, area.height}
    };
}

Split split_down(const Rect& area, int32_t first_height, int32_t gap) {
    int32_t y = 0;
    const int32_t height = rest_of(area.y, area.height, first_height, gap, y);
    return Split{
        Rect{area.x, area.y, area.width, first_height}, Rect{area.x, y, area.width, height}
    };
}

int32_t ScrollArea::limit_for(int32_t content_height, int32_t view_height) noexcept {
    return std::max(content_height - view_height, int32_t{0});
}

int32_t ScrollArea::offset_showing(int32_t content_top, int32_t height, bool last) const noexcept {
    const int32_t highest = content_top - view.y;
    const int32_t lowest = last ? limit : content_top + height - (view.y + view.height - 1);
    const int32_t scroll = std::min(std::max(offset, lowest), highest);
    if (limit < 0)
        return 0;
    return std::clamp(scroll, int32_t{0}, limit);
}

Rect ScrollArea::thumb(int32_t least_height) const noexcept {
    const Rect inside{well.x + 1, well.y + 1, well.width - 2, well.height - 2};
    int32_t height = inside.height;
    if (content_height > view.height && content_height != 0) {
        const int32_t share_height = static_cast<int32_t>(
            static_cast<int64_t>(inside.height) * view.height / content_height
        );
        height = std::max(least_height, share_height);
    }
    const int32_t travel = inside.height - height;
    int32_t top = inside.y;
    if (limit > 0) {
        const int32_t scroll = std::clamp(offset, int32_t{0}, limit);
        top += static_cast<int32_t>((static_cast<int64_t>(travel) * scroll + limit / 2) / limit);
    }
    return {inside.x, top, inside.width, height};
}

int32_t ScrollArea::offset_at(int32_t thumb_top, int32_t least_height) const noexcept {
    ScrollArea resting = *this;
    resting.offset = 0;
    const Rect at_rest = resting.thumb(least_height);
    const int32_t travel = well.height - 2 - at_rest.height;
    if (travel <= 0 || limit <= 0)
        return 0;
    const int32_t along = std::clamp(thumb_top - at_rest.y, int32_t{0}, travel);
    return static_cast<int32_t>((static_cast<int64_t>(limit) * along + travel / 2) / travel);
}

Rect ScrollArea::placed(const Rect& content) const noexcept {
    if (content.width <= 0 || content.height <= 0)
        return content;
    return {content.x, content.y - offset, content.width, content.height};
}

ControlId hit(const DisplayList& list, Point point) noexcept {
    for (const Control& control : list.controls) {
        if (!control.enabled)
            continue;
        const bool clipped = control.clip.width > 0 && control.clip.height > 0;
        const Rect reach = clipped ? intersect(control.rect, control.clip) : control.rect;
        if (contains(reach, point))
            return control.id;
    }
    return no_control;
}

const Control* control_of(const DisplayList& list, ControlId id) noexcept {
    for (const Control& control : list.controls)
        if (control.id == id)
            return &control;
    return nullptr;
}

} // namespace oa::ui::kit
