// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Where the two coach marks sit so they follow the OA buttons and never cover them.

#include "oa/ui/first_run/first_run.hpp"

#include <algorithm>

namespace oa::ui::first_run {
namespace {

/// Tells whether a rectangle has an area.
///
/// @param rect the rectangle
/// @return true when it is wider and taller than nothing
bool live(const kit::Rect& rect) noexcept {
    return rect.width > 0 && rect.height > 0;
}

/// Tells whether two rectangles share any area.
///
/// @param left a rectangle
/// @param right another rectangle
/// @return true when they meet
bool meets(const kit::Rect& left, const kit::Rect& right) noexcept {
    if (!live(left) || !live(right))
        return false;
    const kit::Rect shared = kit::intersect(left, right);
    return shared.width > 0 && shared.height > 0;
}

/// Places one card where it lies wholly in the room and misses the target and the extras.
///
/// A place that would have to be dragged onto a button is refused. The width
/// shrinks only when nothing at the asked width fits.
///
/// @param room the rectangle the card must lie in
/// @param target the button the card points at
/// @param card_width the card's width
/// @param card_height the card's height
/// @param extra rectangles the card must also miss
/// @param extra_count how many extras
/// @return the card; empty when the room has nowhere clear
kit::Rect place_one(
    const kit::Rect& room,
    const kit::Rect& target,
    int32_t card_width,
    int32_t card_height,
    const kit::Rect* extra,
    int32_t extra_count
) noexcept {
    const auto fits = [&](const kit::Rect& card) {
        if (!live(card) || !kit::wholly_in(card, room))
            return false;
        if (meets(card, target))
            return false;
        for (int32_t index = 0; index < extra_count; ++index) {
            if (meets(card, extra[index]))
                return false;
        }
        return true;
    };
    const auto search = [&](int32_t card_w, int32_t card_h) -> kit::Rect {
        const int32_t gap = kit::coach_gap;
        const kit::Rect beside[] = {
            {target.x - gap - card_w, target.y, card_w, card_h},
            {target.x + target.width + gap, target.y, card_w, card_h},
            {target.x, target.y + target.height + gap, card_w, card_h},
            {target.x + target.width - card_w, target.y + target.height + gap, card_w, card_h},
            {room.x + room.width - card_w, target.y + target.height + gap, card_w, card_h},
            {room.x, target.y + target.height + gap, card_w, card_h},
            {target.x, target.y - gap - card_h, card_w, card_h},
            {target.x + target.width - card_w, target.y - gap - card_h, card_w, card_h},
            {room.x + room.width - card_w, target.y - gap - card_h, card_w, card_h},
        };
        for (const kit::Rect& candidate : beside) {
            if (fits(candidate))
                return candidate;
        }
        for (int32_t index = 0; index < extra_count; ++index) {
            const kit::Rect& other = extra[index];
            if (!live(other))
                continue;
            const int32_t below = other.y + other.height + gap;
            const kit::Rect stacked[] = {
                {room.x + room.width - card_w, below, card_w, card_h},
                {other.x + other.width - card_w, below, card_w, card_h},
                {target.x + target.width - card_w, below, card_w, card_h},
                {other.x, below, card_w, card_h},
                {room.x, below, card_w, card_h},
            };
            for (const kit::Rect& candidate : stacked) {
                if (fits(candidate))
                    return candidate;
            }
        }
        for (int32_t row = 1; row <= 6; ++row) {
            const int32_t below = target.y + target.height + gap + row * (card_h + gap);
            const kit::Rect lowered[] = {
                {room.x + room.width - card_w, below, card_w, card_h},
                {target.x + target.width - card_w, below, card_w, card_h},
                {room.x, below, card_w, card_h},
            };
            for (const kit::Rect& candidate : lowered) {
                if (fits(candidate))
                    return candidate;
            }
        }
        return {};
    };

    const kit::Rect full = search(card_width, card_height);
    if (live(full))
        return full;
    int32_t narrower = card_width;
    while (narrower > 80) {
        narrower = std::max(narrower * 3 / 4, int32_t{80});
        const kit::Rect shrunk = search(narrower, card_height);
        if (live(shrunk))
            return shrunk;
        if (narrower <= 80)
            break;
    }
    return {};
}

} // namespace

int32_t coach_card_width(kit::SizeClass size_class) noexcept {
    switch (size_class) {
    case kit::SizeClass::compact:
        return 220;
    case kit::SizeClass::regular:
        return 260;
    case kit::SizeClass::large:
        return 300;
    }
    return 220;
}

CoachCards place_coaches(
    const kit::Rect& room,
    const kit::Rect& blocked,
    const kit::Rect& library_button,
    const kit::Rect& settings_button,
    int32_t card_width,
    int32_t library_height,
    int32_t settings_height
) noexcept {
    CoachCards cards;
    const kit::Rect library_avoid[] = {blocked, settings_button};
    cards.library = place_one(room, library_button, card_width, library_height, library_avoid, 2);
    const kit::Rect settings_avoid[] = {blocked, library_button, cards.library};
    cards.settings =
        place_one(room, settings_button, card_width, settings_height, settings_avoid, 3);
    return cards;
}

} // namespace oa::ui::first_run
