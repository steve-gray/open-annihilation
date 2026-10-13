// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The icon the Library draws for Total Annihilation and the demo when the
// game folder's own program cannot be read.
#pragma once

#include "oa/ui/library/library.hpp"

namespace oa::ui::library {

/// Returns the icon the base game's program carries, kept in the game.
///
/// Total Annihilation and the demo share it. The picture is 32 pixels
/// square, 8-bit RGBA, rows from the top.
///
/// @return the icon
[[nodiscard]] Badge bundled_game_icon();

} // namespace oa::ui::library
