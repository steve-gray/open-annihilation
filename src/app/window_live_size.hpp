// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The size of the window the player sees, which can differ from the size
// last delivered to the game when a resize was not reported.
#pragma once

#include <SDL3/SDL.h>

namespace oa::app {

/// The window's live content size in points, from the system's window.
///
/// On macOS this is the content rectangle of the window on screen. It can
/// differ from the size last recorded when a resize was not delivered.
/// Elsewhere, or when that window cannot be read, it returns false and
/// leaves the sizes unchanged.
///
/// @param window the window
/// @param[out] width its content width, in points
/// @param[out] height its content height, in points
/// @return true when the size was read
#if defined(SDL_PLATFORM_MACOS)
[[nodiscard]] bool platform_window_content_size(SDL_Window* window, int& width, int& height);
#else
[[nodiscard]] inline bool
platform_window_content_size(SDL_Window* /*window*/, int& /*width*/, int& /*height*/) {
    return false;
}
#endif

} // namespace oa::app
