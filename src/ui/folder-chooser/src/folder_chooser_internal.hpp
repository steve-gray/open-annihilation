// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the chooser's layout and its presses share inside the module: the
// scroll area its rows' controls take, and the part that draws a target by
// the target's place in the order the targets were laid out.
#pragma once

#include "oa/ui/kit/layout.hpp"

#include <stdint.h>

namespace oa::ui::folder_chooser::detail {

/// The scroll area number (kit::Control::group) of the controls in the scrolling region.
inline constexpr int32_t rows_area_group = 0;

/// Returns the part that draws a target, by the target's place in the order the targets were
/// laid out: the parts that draw a control, in painting order.
///
/// @param list the laid-out chooser's display list
/// @param place the target's place, from 0
/// @return the part; null for a place no target has, -1 included
[[nodiscard]] const oa::ui::kit::Item*
target_part(const oa::ui::kit::DisplayList& list, int32_t place) noexcept;

} // namespace oa::ui::folder_chooser::detail
