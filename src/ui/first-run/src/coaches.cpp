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

/// Holds a card inside a room, keeping its size when the room is large enough.
///
/// @param card the card
/// @param room the room
/// @return the card, moved inside the room
kit::Rect held_inside(kit::Rect card, const kit::Rect& room) noexcept {
    if (card.width > room.width)
        card.width = std::max(room.width, int32_t{0});
    if (card.height > room.height)
        card.height = std::max(room.height, int32_t{0});
    if (card.x < room.x)
        card.x = room.x;
    if (card.y < room.y)
        card.y = room.y;
    if (card.x + card.width > room.x + room.width)
        card.x = room.x + room.width - card.width;
    if (card.y + card.height > room.y + room.height)
        card.y = room.y + room.height - card.height;
    return card;
}

/// Places one card, trying the right, the left, below and above.
///
/// @param room the rectangle the card must lie in
/// @param target the button the card points at
/// @param card_width the card's width
/// @param card_height the card's height
/// @param extra rectangles the card must also miss
/// @param extra_count how many extras
/// @return the card
kit::Rect place_one(
    const kit::Rect& room,
    const kit::Rect& target,
    int32_t card_width,
    int32_t card_height,
    const kit::Rect* extra,
    int32_t extra_count
) noexcept {
    const kit::Rect right{
        target.x + target.width + kit::coach_gap, target.y, card_width, card_height
    };
    const kit::Rect left{target.x - kit::coach_gap - card_width, target.y, card_width, card_height};
    const kit::Rect below{
        target.x, target.y + target.height + kit::coach_gap, card_width, card_height
    };
    const kit::Rect above{
        target.x, target.y - kit::coach_gap - card_height, card_width, card_height
    };
    const kit::Rect sides[] = {right, left, below, above};
    for (const kit::Rect& side : sides) {
        const kit::Rect card = held_inside(side, room);
        if (!live(card) || !kit::wholly_in(card, room))
            continue;
        if (meets(card, target))
            continue;
        bool blocked = false;
        for (int32_t place = 0; place < extra_count; ++place) {
            if (meets(card, extra[place]))
                blocked = true;
        }
        if (!blocked)
            return card;
    }
    return held_inside(right, room);
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
