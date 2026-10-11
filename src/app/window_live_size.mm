// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The macOS window's live content size (window_live_size.hpp). The size is
// read from the system's window, so a resize that was not delivered can
// still be seen.
#include "window_live_size.hpp"

#import <AppKit/AppKit.h>

#include <cmath>

namespace oa::app {

bool platform_window_content_size(SDL_Window* window, int& width, int& height) {
    if (window == nullptr)
        return false;
    @autoreleasepool {
        void* pointer = SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr
        );
        if (pointer == nullptr)
            return false;
        NSWindow* cocoa = (__bridge NSWindow*)pointer;
        const NSRect content = [cocoa contentRectForFrameRect:[cocoa frame]];
        width = static_cast<int>(std::lround(content.size.width));
        height = static_cast<int>(std::lround(content.size.height));
    }
    return width > 0 && height > 0;
}

} // namespace oa::app
