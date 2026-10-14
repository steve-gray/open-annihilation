// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The window's place remembered across starts: opened where it was left,
// and written again when it changes, so a crash keeps the change.
#include "oa/app/runtime.hpp"

#include "oa/platform/preferences.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace oa::app {

namespace {

/// The place this process remembers, and where a change is written.
struct WindowPlaceMemory {
    std::filesystem::path file;                 ///< the preferences file
    std::map<std::string, std::string>* values; ///< the runtime's preferences; null before it
    bool* dirty;                                ///< the runtime's unsaved-preferences flag
    bool enabled;                               ///< this run may remember a change
    bool has_last;                              ///< last holds a place
    SavedWindow last;                           ///< the place last read or written
    std::string last_written;                   ///< that place's text, so an unchanged one is kept
};

/// The one window the process opens.
WindowPlaceMemory memory{};

/// Holds a coordinate inside the range a remembered place accepts.
///
/// @param coordinate a position the window system reported
/// @return the coordinate, limited to -100000 through 100000
int32_t held_coordinate(int coordinate) {
    if (coordinate > 100000)
        return 100000;
    if (coordinate < -100000)
        return -100000;
    return static_cast<int32_t>(coordinate);
}

/// Holds a size inside the range a remembered place accepts.
///
/// @param size a width or height the window system reported
/// @return the size, at least 1 and at most 16384
int32_t held_size(int size) {
    if (size < 1)
        return 1;
    if (size > 16384)
        return 16384;
    return static_cast<int32_t>(size);
}

/// Reads the place the window has now.
///
/// A minimised or hidden window is skipped, so a close into the dock does
/// not store a zero size. Full screen keeps the window last seen, not the
/// screen's pixel size.
///
/// @param window the game's window
/// @return the place, or nothing when it cannot be read
std::optional<SavedWindow> observe_window(SDL_Window* window) {
    if (window == nullptr)
        return std::nullopt;
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    if ((flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0)
        return std::nullopt;
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSize(window, &width, &height) || width < 1 || height < 1)
        return std::nullopt;
    int x = 0;
    int y = 0;
    if (!SDL_GetWindowPosition(window, &x, &y))
        return std::nullopt;
    const SDL_DisplayID shown = SDL_GetDisplayForWindow(window);
    SDL_Rect bounds{};
    if (shown == 0 || !SDL_GetDisplayBounds(shown, &bounds) || bounds.w < 1 || bounds.h < 1)
        return std::nullopt;
    SavedWindow place;
    place.full_screen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    place.display_x = held_coordinate(bounds.x);
    place.display_y = held_coordinate(bounds.y);
    place.display_width = held_size(bounds.w);
    place.display_height = held_size(bounds.h);
    if (place.full_screen) {
        if (memory.has_last && memory.last.has_window) {
            place.has_window = true;
            place.window = memory.last.window;
        }
        return place;
    }
    place.has_window = true;
    place.window.x = held_coordinate(x);
    place.window.y = held_coordinate(y);
    place.window.width = held_size(width);
    place.window.height = held_size(height);
    return place;
}

/// Writes a place into the preferences.
///
/// The runtime's own map is written whole when the runtime is alive, so a
/// later flush keeps the key. Before the runtime exists the file is loaded,
/// changed and saved. An unchanged text is left where it is.
///
/// @param place the place
/// @return false when the file could not be saved
bool commit_window_place(const SavedWindow& place) {
    const std::string text = encode_window_place(place);
    if (text == memory.last_written)
        return true;
    if (memory.file.empty()) {
        std::cerr << "open-annihilation: the window's place was not saved: no preferences file\n";
        return false;
    }
    try {
        if (memory.values != nullptr) {
            memory.values->insert_or_assign(std::string(window_place_key), text);
            oa::platform::preferences::save(memory.file, *memory.values);
            if (memory.dirty != nullptr)
                *memory.dirty = false;
        } else {
            auto stored = oa::platform::preferences::load(memory.file);
            stored.insert_or_assign(std::string(window_place_key), text);
            oa::platform::preferences::save(memory.file, stored);
        }
    } catch (const std::exception& error) {
        std::cerr << "open-annihilation: the window's place was not saved: " << error.what()
                  << '\n';
        return false;
    }
    memory.last = place;
    memory.has_last = true;
    memory.last_written = text;
    return true;
}

/// The main display, or the first one when none is the main.
///
/// @param displays the displays connected now
/// @return the display, or null when none is connected
const DisplayArea* main_display(const std::vector<DisplayArea>& displays) {
    const DisplayArea* chosen = nullptr;
    for (const DisplayArea& area : displays) {
        if (area.main)
            return &area;
        if (chosen == nullptr)
            chosen = &area;
    }
    return chosen;
}

/// Tells whether a window's contents lie wholly on a display.
///
/// @param x the contents' left
/// @param y the contents' top
/// @param width the contents' width
/// @param height the contents' height
/// @param displays the displays connected now
/// @return true when some display contains the whole window
bool wholly_on_display(
    int x, int y, int width, int height, const std::vector<DisplayArea>& displays
) {
    const int64_t right = static_cast<int64_t>(x) + width;
    const int64_t bottom = static_cast<int64_t>(y) + height;
    for (const DisplayArea& area : displays) {
        if (area.width < 1 || area.height < 1)
            continue;
        const int64_t area_right = static_cast<int64_t>(area.x) + area.width;
        const int64_t area_bottom = static_cast<int64_t>(area.y) + area.height;
        if (x >= area.x && y >= area.y && right <= area_right && bottom <= area_bottom)
            return true;
    }
    return false;
}

} // namespace

SDL_Window* create_game_window(
    const char* title,
    int width,
    int height,
    SDL_WindowFlags flags,
    std::optional<int> x,
    std::optional<int> y
) {
    const SDL_PropertiesID properties = SDL_CreateProperties();
    if (properties == 0)
        return nullptr;
    SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    SDL_SetNumberProperty(
        properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, static_cast<Sint64>(flags)
    );
    if (x.has_value() && y.has_value()) {
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, *x);
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, *y);
    }
    SDL_Window* const opened = SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);
    return opened;
}

std::vector<DisplayArea> connected_displays() {
    int count = 0;
    SDL_DisplayID* const ids = SDL_GetDisplays(&count);
    std::vector<DisplayArea> areas;
    if (ids == nullptr)
        return areas;
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    for (int index = 0; index < count; ++index) {
        SDL_Rect bounds{};
        if (!SDL_GetDisplayBounds(ids[index], &bounds))
            continue;
        SDL_Rect usable{};
        if (!SDL_GetDisplayUsableBounds(ids[index], &usable))
            usable = bounds;
        DisplayArea area;
        area.x = bounds.x;
        area.y = bounds.y;
        area.width = bounds.w;
        area.height = bounds.h;
        area.usable_x = usable.x;
        area.usable_y = usable.y;
        area.usable_width = usable.w;
        area.usable_height = usable.h;
        area.main = ids[index] == primary;
        areas.push_back(area);
    }
    SDL_free(ids);
    return areas;
}

std::optional<SavedWindow> read_window_place_file(const std::filesystem::path& file) {
    return window_place_in(oa::platform::preferences::load(file));
}

void remember_open_window(SDL_Window* window) {
    if (!memory.enabled)
        return;
    if (const std::optional<SavedWindow> place = observe_window(window))
        std::ignore = commit_window_place(*place);
}

bool save_window_place(SDL_Window* window) {
    const std::optional<SavedWindow> place = observe_window(window);
    if (!place)
        return false;
    return commit_window_place(*place);
}

void bind_window_place(const std::filesystem::path& file, bool enabled) {
    memory.file = file;
    memory.enabled = enabled;
    try {
        if (const std::optional<SavedWindow> saved = read_window_place_file(file)) {
            memory.last = *saved;
            memory.has_last = true;
            memory.last_written = encode_window_place(*saved);
        }
    } catch (const std::exception& error) {
        std::cerr << "open-annihilation: the window's place was not read: " << error.what() << '\n';
    }
}

void Runtime::attach_window_place() {
    memory.values = &preference_values_;
    memory.dirty = &preferences_dirty_;
}

void detach_window_place_values() {
    memory.values = nullptr;
    memory.dirty = nullptr;
}

int check_saved_window_place(
    SDL_Window* window, const std::filesystem::path& preferences, bool command_line_wins
) {
    if (window == nullptr) {
        std::cerr << "open-annihilation: window place: no window\n";
        return 1;
    }
    if (command_line_wins) {
        std::cout << "open-annihilation: window place: command line kept\n";
        return 0;
    }
    std::optional<SavedWindow> saved;
    try {
        saved = read_window_place_file(preferences);
    } catch (const std::exception& error) {
        std::cerr << "open-annihilation: window place: " << error.what() << '\n';
        return 1;
    }
    if (!saved) {
        if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0 &&
            !SDL_SetWindowFullscreen(window, false)) {
            std::cerr << "open-annihilation: window place: full screen stayed on: "
                      << SDL_GetError() << '\n';
            return 1;
        }
        std::ignore = SDL_SyncWindow(window);
        const std::vector<DisplayArea> displays = connected_displays();
        const DisplayArea* const chosen = main_display(displays);
        int span_width = 1280;
        int span_height = 720;
        int origin_x = 0;
        int origin_y = 0;
        if (chosen != nullptr) {
            const bool usable = chosen->usable_width > 0 && chosen->usable_height > 0;
            span_width = usable ? chosen->usable_width : chosen->width;
            span_height = usable ? chosen->usable_height : chosen->height;
            origin_x = usable ? chosen->usable_x : chosen->x;
            origin_y = usable ? chosen->usable_y : chosen->y;
        }
        if (span_width < 1)
            span_width = 1;
        if (span_height < 1)
            span_height = 1;
        const int width = std::min(1280, span_width);
        const int height = std::min(720, span_height);
        if (!SDL_SetWindowSize(window, width, height) ||
            !SDL_SetWindowPosition(window, origin_x, origin_y)) {
            std::cerr << "open-annihilation: window place: the window was not placed: "
                      << SDL_GetError() << '\n';
            return 1;
        }
        std::ignore = SDL_SyncWindow(window);
        // Unattended runs do not remember; this is the write the check asks for.
        memory.file = preferences;
        if (!save_window_place(window))
            return 1;
        int saved_width = 0;
        int saved_height = 0;
        if (!SDL_GetWindowSize(window, &saved_width, &saved_height)) {
            std::cerr << "open-annihilation: window place: the window's size was not read: "
                      << SDL_GetError() << '\n';
            return 1;
        }
        std::cout << "open-annihilation: window place: saved " << saved_width << 'x' << saved_height
                  << '\n';
        return 0;
    }
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    if (!SDL_GetWindowSize(window, &width, &height) || !SDL_GetWindowPosition(window, &x, &y)) {
        std::cerr << "open-annihilation: window place: the window was not read: " << SDL_GetError()
                  << '\n';
        return 1;
    }
    const std::vector<DisplayArea> displays = connected_displays();
    const RestoredPlace restored =
        restore_window_place(&*saved, PlaceOverride::none, false, displays);
    if (!restored.sized || width != restored.width || height != restored.height ||
        !wholly_on_display(x, y, width, height, displays)) {
        std::cerr << "open-annihilation: window place: opened " << width << 'x' << height
                  << " off the remembered place\n";
        return 1;
    }
    std::cout << "open-annihilation: window place: opened " << width << 'x' << height << '\n';
    return 0;
}

} // namespace oa::app
