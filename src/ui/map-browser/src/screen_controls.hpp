// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The map browser's control numbers and words, which its layout and its
// input share. Private to the module.
#pragma once

#include "oa/ui/kit/layout.hpp"
#include "oa/ui/map_browser/model.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <stdint.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::map_browser::controls {

/// The first source chip, All; Base game, Installed packs and Online follow.
inline constexpr kit::ControlId first_source = 1;
/// The first players chip, 2; 3, 4 and 5–10 follow.
inline constexpr kit::ControlId first_players = 10;
/// The first size chip, S; M and L follow.
inline constexpr kit::ControlId first_size = 20;
/// The search field.
inline constexpr kit::ControlId search = 30;
/// Cancel.
inline constexpr kit::ControlId cancel = 40;
/// The primary button.
inline constexpr kit::ControlId primary = 41;
/// The first card, the first shown; the card shown at place p is first_card + p.
inline constexpr kit::ControlId first_card = 1000;

/// The scroll group of the cards.
inline constexpr int32_t grid_group = 0;

/// The sources, in the order their chips show.
inline constexpr std::array<Filters::Source, 4> sources{
    Filters::Source::all, Filters::Source::base, Filters::Source::installed, Filters::Source::online
};
/// The players chips, in the order they show.
inline constexpr std::array<int32_t, 4> players{2, 3, 4, many_players_chip};
/// The size chips, in the order they show.
inline constexpr std::array<char, 3> sizes{'S', 'M', 'L'};

/// Tells whether a control is a card.
///
/// @param id the control
/// @return true for a card's control
[[nodiscard]] constexpr bool is_card(kit::ControlId id) noexcept {
    return id >= first_card;
}

/// Returns a card's place in what shows from its control.
///
/// @param id the card's control
/// @return its place, from 0
[[nodiscard]] constexpr std::size_t card_place(kit::ControlId id) noexcept {
    return static_cast<std::size_t>(id - first_card);
}

/// Returns the control of the card shown at a place.
///
/// @param place its place, from 0
/// @return the control's number
[[nodiscard]] constexpr kit::ControlId card_control(std::size_t place) noexcept {
    return first_card + static_cast<kit::ControlId>(place);
}

/// Returns one of the browser's words in the language the interface shows.
///
/// @param english the word, in English as this module writes it
/// @return its translation, or the English
[[nodiscard]] std::string word(std::string_view english);

/// Returns a pattern's words with each {name} replaced by its value: the
/// pattern is looked up first (word), the values are put in as given.
///
/// @param english the pattern, in English
/// @param values each name and its value
/// @return the text
[[nodiscard]] std::string filled(
    std::string_view english, const std::vector<std::pair<std::string_view, std::string>>& values
);

} // namespace oa::ui::map_browser::controls
