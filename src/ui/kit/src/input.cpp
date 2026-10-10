// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Pointer, finger reach, keys, two-dimensional focus and the wheel, over a
// kit screen's display list.

#include "oa/ui/kit/input.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdint.h>
#include <string>
#include <string_view>

namespace oa::ui::kit {

namespace {

/// Returns the part of a control a press can reach.
///
/// An empty clip is no clip: the whole rectangle. Otherwise the part the
/// rectangle shares with the clip.
///
/// @param control the control
/// @return the press area, empty when the clip hides it all
[[nodiscard]] Rect press_area(const Control& control) noexcept {
    const bool clipped = control.clip.width > 0 && control.clip.height > 0;
    if (!clipped)
        return control.rect;
    return intersect(control.rect, control.clip);
}

/// Tells whether a scroll area shows any of a control.
///
/// @param control the control
/// @return true when its press area has room
[[nodiscard]] bool in_view(const Control& control) noexcept {
    const Rect area = press_area(control);
    return area.width > 0 && area.height > 0;
}

/// Returns the square of the distance between two points.
///
/// @param from one point
/// @param to the other
/// @return the distance squared, in points squared
[[nodiscard]] int64_t distance_squared(Point from, Point to) noexcept {
    const int64_t across = int64_t{from.x} - to.x;
    const int64_t down = int64_t{from.y} - to.y;
    return across * across + down * down;
}

/// Returns a point moved by a finger's shift.
///
/// @param point the pointer
/// @param shift how far the press was moved
/// @return the point the held press uses
[[nodiscard]] Point shifted(Point point, Point shift) noexcept {
    return {point.x + shift.x, point.y + shift.y};
}

/// Tells whether the focus may stop on a control.
///
/// @param list the display list
/// @param id the control
/// @return true when it is focusable and in the declared order
[[nodiscard]] bool takes_focus(const DisplayList& list, ControlId id) noexcept {
    const Control* control = control_of(list, id);
    if (control == nullptr || !control->focusable)
        return false;
    for (const ControlId ordered : list.tab_order)
        if (ordered == id)
            return true;
    return false;
}

/// Returns a rectangle's centre on one axis, doubled so a half point is exact.
///
/// @param origin the rectangle's start on the axis
/// @param extent the rectangle's length on the axis
/// @return twice the centre
[[nodiscard]] int64_t centre_of(int32_t origin, int32_t extent) noexcept {
    return static_cast<int64_t>(origin) * 2 + extent;
}

/// Tells whether two rectangles share any point of their area.
///
/// @param a a rectangle
/// @param b another
/// @return true when they overlap
[[nodiscard]] bool overlaps(const Rect& a, const Rect& b) noexcept {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}

/// A span on one axis, its end outside it.
struct Span {
    int32_t begin{}; ///< the first point
    int32_t end{};   ///< the point after the last
};

/// Returns a rectangle's span across a direction: the axis the direction does not move on.
///
/// @param rect the rectangle
/// @param direction the way the focus moves
/// @return the span
[[nodiscard]] Span across_span(const Rect& rect, Direction direction) noexcept {
    if (direction == Direction::up || direction == Direction::down)
        return {rect.x, rect.x + rect.width};
    return {rect.y, rect.y + rect.height};
}

/// Tells whether two spans share any point.
///
/// @param a a span
/// @param b another
/// @return true when they overlap
[[nodiscard]] bool spans_overlap(Span a, Span b) noexcept {
    return a.begin < b.end && b.begin < a.end;
}

/// Returns the gap between two spans, 0 when they overlap or touch.
///
/// @param a a span
/// @param b another
/// @return the points between them
[[nodiscard]] int64_t span_gap(Span a, Span b) noexcept {
    if (a.end <= b.begin)
        return static_cast<int64_t>(b.begin) - a.end;
    if (b.end <= a.begin)
        return static_cast<int64_t>(a.begin) - b.end;
    return 0;
}

/// Tells whether a candidate's centre lies strictly past another's in a direction.
///
/// @param from the focused control's rectangle
/// @param candidate the candidate's rectangle
/// @param direction the way the focus moves
/// @return true when the candidate's centre is strictly that way
[[nodiscard]] bool
centre_past(const Rect& from, const Rect& candidate, Direction direction) noexcept {
    const bool vertical = direction == Direction::up || direction == Direction::down;
    const bool forward = direction == Direction::down || direction == Direction::right;
    const int64_t from_centre =
        vertical ? centre_of(from.y, from.height) : centre_of(from.x, from.width);
    const int64_t candidate_centre = vertical ? centre_of(candidate.y, candidate.height)
                                              : centre_of(candidate.x, candidate.width);
    return forward ? candidate_centre > from_centre : candidate_centre < from_centre;
}

/// Tells whether a candidate lies wholly past a rectangle's facing edge.
///
/// Touching the edge counts as past.
///
/// @param from the focused control's rectangle
/// @param candidate the candidate's rectangle
/// @param direction the way the focus moves
/// @return true when the candidate is wholly that way
[[nodiscard]] bool
wholly_past(const Rect& from, const Rect& candidate, Direction direction) noexcept {
    switch (direction) {
    case Direction::right:
        return int64_t{candidate.x} >= int64_t{from.x} + from.width;
    case Direction::left:
        return int64_t{candidate.x} + candidate.width <= from.x;
    case Direction::down:
        return int64_t{candidate.y} >= int64_t{from.y} + from.height;
    case Direction::up:
        return int64_t{candidate.y} + candidate.height <= from.y;
    }
    return false;
}

/// Tells whether a candidate lies beyond the focused control in a direction.
///
/// @param from the focused control's rectangle
/// @param candidate the candidate's rectangle
/// @param direction the way the focus moves
/// @return true when the focus can move to it that way
[[nodiscard]] bool beyond(const Rect& from, const Rect& candidate, Direction direction) noexcept {
    if (wholly_in(candidate, from) || wholly_in(from, candidate))
        return centre_past(from, candidate, direction);
    if (overlaps(from, candidate))
        return false;
    return wholly_past(from, candidate, direction);
}

/// Returns the gap from a rectangle's facing edge to a candidate's near edge.
///
/// Negative when the candidate starts before that edge, as a control inside
/// another does.
///
/// @param from the focused control's rectangle
/// @param candidate the candidate's rectangle
/// @param direction the way the focus moves
/// @return the gap, in points
[[nodiscard]] int64_t
gap_along(const Rect& from, const Rect& candidate, Direction direction) noexcept {
    switch (direction) {
    case Direction::right:
        return static_cast<int64_t>(candidate.x) - (static_cast<int64_t>(from.x) + from.width);
    case Direction::left:
        return static_cast<int64_t>(from.x) - (static_cast<int64_t>(candidate.x) + candidate.width);
    case Direction::down:
        return static_cast<int64_t>(candidate.y) - (static_cast<int64_t>(from.y) + from.height);
    case Direction::up:
        return static_cast<int64_t>(from.y) -
               (static_cast<int64_t>(candidate.y) + candidate.height);
    }
    return 0;
}

/// Returns the distance between two rectangles' centres across a direction.
///
/// @param from the focused control's rectangle
/// @param candidate the candidate's rectangle
/// @param direction the way the focus moves
/// @return the distance, in half-points
[[nodiscard]] int64_t
centre_across(const Rect& from, const Rect& candidate, Direction direction) noexcept {
    const bool vertical = direction == Direction::up || direction == Direction::down;
    const int64_t from_centre =
        vertical ? centre_of(from.x, from.width) : centre_of(from.y, from.height);
    const int64_t candidate_centre = vertical ? centre_of(candidate.x, candidate.width)
                                              : centre_of(candidate.y, candidate.height);
    const int64_t difference = candidate_centre - from_centre;
    return difference < 0 ? -difference : difference;
}

/// Tells whether a name is words of a-z, 0-9 and hyphens joined by dots.
///
/// @param name the name
/// @return true when it matches
[[nodiscard]] bool name_words(std::string_view name) noexcept {
    if (name.empty())
        return false;
    bool word = false;
    for (const char character : name) {
        const bool letter = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        if (letter || digit || character == '-') {
            word = true;
            continue;
        }
        if (character == '.' && word) {
            word = false;
            continue;
        }
        return false;
    }
    return word;
}

} // namespace

Reached reach(const DisplayList& list, Point finger, int32_t reach_px) noexcept {
    const ControlId under = hit(list, finger);
    if (under != no_control || reach_px <= 0)
        return {under, finger};

    const int64_t within = int64_t{reach_px} * reach_px;
    bool found = false;
    int64_t best_distance = 0;
    Reached best{no_control, finger};
    for (const Control& control : list.controls) {
        if (!control.enabled)
            continue;
        const Rect area = press_area(control);
        if (area.width <= 0 || area.height <= 0)
            continue;
        const Point candidate = nearest_point(area, finger);
        // A point another control covers is passed over: the press would land there.
        if (hit(list, candidate) != control.id)
            continue;
        const int64_t distance = distance_squared(candidate, finger);
        if (distance <= within && (!found || distance < best_distance)) {
            found = true;
            best_distance = distance;
            best = {control.id, candidate};
        }
    }
    return found ? best : Reached{no_control, finger};
}

PointerOutcome pointer_move(Interaction& interaction, const DisplayList& list, Point point) {
    if (interaction.pressed != no_control)
        point = shifted(point, interaction.finger_shift);
    const ControlId hovered = hit(list, point);
    if (hovered == interaction.hovered)
        return {PointerResult::none, hovered, point};
    interaction.hovered = hovered;
    return {PointerResult::redraw, hovered, point};
}

PointerOutcome pointer_down(Interaction& interaction, const DisplayList& list, Point point) {
    interaction.finger_shift = {};
    const ControlId control = hit(list, point);
    interaction.hovered = control;
    interaction.pressed = control;
    if (control == no_control)
        return {PointerResult::none, no_control, point};
    if (interaction.focus_shown && takes_focus(list, control))
        interaction.focused = control;
    return {PointerResult::redraw, control, point};
}

PointerOutcome
finger_down(Interaction& interaction, const DisplayList& list, Point finger, int32_t reach_px) {
    const Reached landed = reach(list, finger, reach_px);
    const PointerOutcome outcome = pointer_down(interaction, list, landed.at);
    if (interaction.pressed != no_control) {
        interaction.finger_shift = {landed.at.x - finger.x, landed.at.y - finger.y};
    }
    return outcome;
}

PointerOutcome pointer_up(Interaction& interaction, const DisplayList& list, Point point) {
    if (interaction.pressed != no_control)
        point = shifted(point, interaction.finger_shift);
    interaction.finger_shift = {};
    const ControlId hovered = hit(list, point);
    interaction.hovered = hovered;
    const ControlId held = interaction.pressed;
    interaction.pressed = no_control;
    if (held == no_control)
        return {PointerResult::none, no_control, point};
    if (hovered != held)
        return {PointerResult::redraw, hovered, point};
    return {PointerResult::activated, held, point};
}

ControlId next_in_tab_order(const DisplayList& list, ControlId from, bool forward) noexcept {
    const std::vector<ControlId>& order = list.tab_order;
    if (order.empty())
        return no_control;
    const auto found = std::find(order.begin(), order.end(), from);
    if (found == order.end())
        return forward ? order.front() : order.back();
    const auto count = static_cast<std::ptrdiff_t>(order.size());
    const std::ptrdiff_t at = found - order.begin();
    const std::ptrdiff_t step = forward ? 1 : count - 1;
    return order[static_cast<std::size_t>((at + step) % count)];
}

ControlId focus_toward(const DisplayList& list, ControlId from, Direction direction) noexcept {
    // How many gaps along the direction one gap across it costs, when nothing
    // lies in line with the focused control. The whole rule is this function,
    // so a playtest can tune it here.
    constexpr int32_t kAcrossWeight = 2;

    const Control* origin = control_of(list, from);
    if (origin == nullptr)
        return no_control;

    const auto search = [&](bool same_group) noexcept -> ControlId {
        bool any = false;
        bool best_in_beam = false;
        int64_t best_rank = 0;
        int64_t best_centres = 0;
        std::size_t best_tab = 0;
        ControlId best = no_control;
        for (std::size_t index = 0; index < list.tab_order.size(); ++index) {
            const ControlId id = list.tab_order[index];
            if (id == from)
                continue;
            const Control* control = control_of(list, id);
            if (control == nullptr || !control->focusable || !control->enabled)
                continue;
            if (same_group) {
                if (control->group != origin->group)
                    continue;
            } else if (control->group == origin->group || !in_view(*control)) {
                continue;
            }
            if (!beyond(origin->rect, control->rect, direction))
                continue;

            const bool in_beam = spans_overlap(
                across_span(origin->rect, direction), across_span(control->rect, direction)
            );
            const int64_t along = gap_along(origin->rect, control->rect, direction);
            const int64_t across = span_gap(
                across_span(origin->rect, direction), across_span(control->rect, direction)
            );
            const int64_t rank =
                in_beam ? along : along + static_cast<int64_t>(kAcrossWeight) * across;
            const int64_t centres = centre_across(origin->rect, control->rect, direction);

            bool better = !any;
            if (any) {
                if (in_beam != best_in_beam)
                    better = in_beam;
                else if (rank != best_rank)
                    better = rank < best_rank;
                else if (centres != best_centres)
                    better = centres < best_centres;
                else
                    better = index < best_tab;
            }
            if (!better)
                continue;
            any = true;
            best_in_beam = in_beam;
            best_rank = rank;
            best_centres = centres;
            best_tab = index;
            best = id;
        }
        return best;
    };

    const ControlId inside = search(true);
    if (inside != no_control)
        return inside;
    return search(false);
}

namespace {

/// Builds what a key did.
///
/// @param result what the screen does
/// @param control the control it names
/// @param pressed the key
/// @return the outcome
[[nodiscard]] KeyOutcome outcome_of(KeyResult result, ControlId control, Key pressed) noexcept {
    return {result, control, pressed};
}

/// Shows the focus on one end of the declared order.
///
/// @param[in,out] interaction the focus
/// @param list the display list
/// @param last true for the last control, false for the first
/// @param pressed the key
/// @return redraw, or none when the order is empty
[[nodiscard]] KeyOutcome
show_end(Interaction& interaction, const DisplayList& list, bool last, Key pressed) {
    if (list.tab_order.empty())
        return outcome_of(KeyResult::none, no_control, pressed);
    interaction.focused = last ? list.tab_order.back() : list.tab_order.front();
    interaction.focus_shown = true;
    return outcome_of(KeyResult::redraw, interaction.focused, pressed);
}

} // namespace

KeyOutcome key(Interaction& interaction, const DisplayList& list, Key pressed) {
    const bool shown = interaction.focus_shown && interaction.focused != no_control;
    const Control* focused = shown ? control_of(list, interaction.focused) : nullptr;

    switch (pressed) {
    case Key::escape:
        return outcome_of(KeyResult::cancel, no_control, pressed);
    case Key::yes:
    case Key::no:
        return outcome_of(KeyResult::none, no_control, pressed);
    case Key::page_up:
    case Key::page_down:
    case Key::home:
    case Key::end:
        return outcome_of(KeyResult::scroll, shown ? interaction.focused : no_control, pressed);
    case Key::enter:
        if (focused != nullptr &&
            (focused->kind == ControlKind::button || focused->kind == ControlKind::link ||
             focused->kind == ControlKind::list_item))
            return outcome_of(KeyResult::to_control, interaction.focused, pressed);
        return outcome_of(KeyResult::accept, no_control, pressed);
    case Key::up:
    case Key::back_tab:
        if (!shown)
            return show_end(interaction, list, true, pressed);
        break;
    case Key::tab:
    case Key::down:
    case Key::left:
    case Key::right:
    case Key::space:
        if (!shown)
            return show_end(interaction, list, false, pressed);
        break;
    }

    if (pressed == Key::space)
        return outcome_of(KeyResult::to_control, interaction.focused, pressed);

    if ((pressed == Key::left || pressed == Key::right) && focused != nullptr && focused->steps)
        return outcome_of(KeyResult::to_control, interaction.focused, pressed);

    if (pressed == Key::tab || pressed == Key::back_tab) {
        const ControlId next = next_in_tab_order(list, interaction.focused, pressed == Key::tab);
        if (next == no_control || next == interaction.focused)
            return outcome_of(KeyResult::none, interaction.focused, pressed);
        interaction.focused = next;
        interaction.focus_shown = true;
        return outcome_of(KeyResult::redraw, next, pressed);
    }

    Direction direction = Direction::up;
    if (pressed == Key::down)
        direction = Direction::down;
    else if (pressed == Key::left)
        direction = Direction::left;
    else if (pressed == Key::right)
        direction = Direction::right;
    else if (pressed != Key::up)
        return outcome_of(KeyResult::none, no_control, pressed);

    const ControlId next = focus_toward(list, interaction.focused, direction);
    if (next == no_control)
        return outcome_of(KeyResult::none, interaction.focused, pressed);
    interaction.focused = next;
    interaction.focus_shown = true;
    return outcome_of(KeyResult::redraw, next, pressed);
}

int32_t wheel_offset(
    WheelCarry& carry, float notches, int32_t step, int32_t offset, int32_t limit
) noexcept {
    if (!std::isfinite(notches))
        return offset;
    if (limit == 0) {
        carry.rows = 0.0F;
        return offset;
    }
    // A turn larger than the area reaches its end. The extra point lets a
    // fraction past the last row still arrive there.
    const float room = static_cast<float>(limit) + 1.0F;
    const float rows = std::clamp(carry.rows - notches * static_cast<float>(step), -room, room);
    const auto whole = static_cast<int32_t>(rows);
    carry.rows = rows - static_cast<float>(whole);
    const int32_t next = std::clamp(offset + whole, int32_t{0}, limit);
    // What is carried towards an end already reached is dropped.
    if ((next == 0 && carry.rows < 0.0F) || (next == limit && carry.rows > 0.0F))
        carry.rows = 0.0F;
    return next;
}

std::vector<AutomationEntry>
automation_entries(const DisplayList& list, const Interaction& interaction) {
    std::vector<AutomationEntry> entries;
    for (const Control& control : list.controls) {
        if (control.name.empty())
            continue;
        AutomationEntry entry;
        entry.name = control.name;
        entry.kind = control.kind;
        entry.rect = control.rect;
        entry.shown = in_view(control);
        entry.enabled = control.enabled;
        entry.focused = interaction.focus_shown && interaction.focused == control.id;
        entry.checked = control.checked;
        entry.text = control.text;
        entries.push_back(std::move(entry));
    }
    return entries;
}

ControlId control_named(const DisplayList& list, std::string_view name) noexcept {
    for (const Control& control : list.controls)
        if (std::string_view{control.name} == name)
            return control.id;
    return no_control;
}

std::string name_problem(const DisplayList& list) {
    std::vector<std::string_view> seen;
    seen.reserve(list.controls.size());
    for (const Control& control : list.controls) {
        const std::string& control_name = control.name;
        if (control_name.empty())
            return "Control " + std::to_string(control.id) + " has no name.";
        if (control_name.size() > 100)
            return "The name of control " + std::to_string(control.id) + " is " +
                   std::to_string(control_name.size()) + " bytes, and a name is at most 100.";
        if (!name_words(control_name))
            return "The name \"" + control_name +
                   "\" is not words of a-z, 0-9 and hyphens joined by dots.";
        for (const std::string_view earlier : seen) {
            if (earlier == control_name)
                return "The name \"" + control_name + "\" is used more than once.";
        }
        seen.push_back(control_name);
    }
    return {};
}

} // namespace oa::ui::kit
