// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-progress: a short job shows no dialog, a long spinner and a long
// bar animate and close with no input, the four languages have the lines,
// and a picture of each state is written at three window sizes.
#include "oa/app/runtime.hpp"

#include "render_state.hpp"
#include "window_pixels.hpp"

#include "oa/base/threads.hpp"
#include "oa/data/languages.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/formats/png.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/paint/painter.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace oa::app {
namespace {

namespace kit = oa::ui::kit;
namespace paint = oa::ui::paint;

/// The size class's word, as the picture's name uses it.
const char* size_class_word(kit::SizeClass size_class) {
    switch (size_class) {
    case kit::SizeClass::regular:
        return "regular";
    case kit::SizeClass::large:
        return "large";
    case kit::SizeClass::compact:
        break;
    }
    return "compact";
}

/// A frame of a pixel size at one canvas pixel a point, with no safe inset.
kit::Frame frame_at(int across, int down) {
    kit::Viewport laid;
    laid.width = static_cast<int32_t>(across);
    laid.height = static_cast<int32_t>(down);
    laid.density = 1.0F;
    laid.scale_percent = 100;
    return kit::frame_of(laid);
}

/// Writes a canvas as an RGB PNG in the working directory.
void write_progress_png(const std::string& path, const paint::Canvas& canvas) {
    if (canvas.width <= 0 || canvas.height <= 0)
        throw std::runtime_error("progress check: the picture is empty");
    const std::size_t count =
        static_cast<std::size_t>(canvas.width) * static_cast<std::size_t>(canvas.height);
    std::vector<uint8_t> rgb(count * 3);
    for (std::size_t at = 0; at < count; ++at) {
        rgb[at * 3] = canvas.rgba[at * 4];
        rgb[at * 3 + 1] = canvas.rgba[at * 4 + 1];
        rgb[at * 3 + 2] = canvas.rgba[at * 4 + 2];
    }
    const oa::formats::png::Header header{
        static_cast<uint32_t>(canvas.width),
        static_cast<uint32_t>(canvas.height),
        8,
        oa::formats::png::ColorType::rgb,
        oa::formats::png::Interlace::none,
    };
    std::vector<uint8_t> file;
    if (!oa::formats::png::write(oa::formats::png::Image{header, {}, rgb}, &file))
        throw std::runtime_error("progress check: cannot encode " + path);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size())
    );
    if (!output)
        throw std::runtime_error("progress check: cannot write " + path);
}

/// Paints one state at a size and writes it. Presents it when the window is that size.
void paint_state(
    SDL_Window* window, SDL_Renderer* renderer, int across, int down, bool spinner, bool* presented
) {
    if (!SDL_SetWindowSize(window, across, down) || !SDL_SyncWindow(window))
        throw std::runtime_error(
            std::string("progress check: the window did not take ") + std::to_string(across) + "x" +
            std::to_string(down)
        );
    oa::ui::game_files::Viewport read;
    std::ignore = read_window_viewport(window, renderer, read);
    const kit::Frame dialog_frame = frame_at(across, down);
    const char* word = size_class_word(dialog_frame.size_class);
    paint::Canvas pixels = paint::make_canvas(across, down);
    const char* line =
        spinner ? "Loading the game's archives" : "Checking your copy of Total Annihilation";
    const int32_t amount = spinner ? -1 : 400;
    const uint32_t turn = spinner ? 2u : 0u;
    paint_progress_frame(pixels, dialog_frame, "Open Annihilation", line, amount, turn, nullptr);
    const std::string path = std::string("G70-progress-") + (spinner ? "spinner-" : "bar-") +
                             std::to_string(across) + "x" + std::to_string(down) + "-" + word +
                             ".png";
    write_progress_png(path, pixels);
    std::cout << "progress check: " << (spinner ? "spinner" : "bar") << " " << across << "x" << down
              << " " << word << "\n";
    if (read.width == across && read.height == down) {
        WindowFrame window_frame(renderer, "the progress dialog");
        const RenderState kept(renderer);
        if (!window_frame.show(pixels, kept, nullptr))
            throw std::runtime_error("progress check: the picture was not shown");
        *presented = true;
    } else {
        std::cout << "progress check: window stayed " << read.width << "x" << read.height << "\n";
    }
}

} // namespace

void Runtime::check_progress() {
    if (sdl_.window == nullptr || sdl_.renderer == nullptr)
        throw std::runtime_error("progress check: the window is missing");

    const auto behind = [](paint::Canvas& canvas) { fill_progress_behind(canvas, nullptr); };
    const LongWorkResult quick = run_long_work(
        sdl_.window,
        sdl_.renderer,
        nullptr,
        "Open Annihilation",
        "Loading the game's archives",
        behind,
        [](LongWork&) { oa::base::threads::sleep_ms(50); }
    );
    if (quick.shown)
        throw std::runtime_error("progress check: a short job showed the dialog");

    const LongWorkResult spinner = run_long_work(
        sdl_.window,
        sdl_.renderer,
        nullptr,
        "Open Annihilation",
        "Loading the game's archives",
        behind,
        [](LongWork&) {
            oa::base::threads::sleep_ms(400);
            SDL_Event click{};
            click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            click.button.button = SDL_BUTTON_LEFT;
            click.button.down = true;
            std::ignore = SDL_PushEvent(&click);
            SDL_Event key{};
            key.type = SDL_EVENT_KEY_DOWN;
            key.key.key = SDLK_RETURN;
            key.key.down = true;
            std::ignore = SDL_PushEvent(&key);
            oa::base::threads::sleep_ms(600);
        }
    );
    if (!spinner.shown)
        throw std::runtime_error("progress check: the spinner was not shown");
    if (spinner.distinct_turns < 2)
        throw std::runtime_error(
            "progress check: the spinner turned " + std::to_string(spinner.distinct_turns) +
            " times"
        );
    if (spinner.bar)
        throw std::runtime_error("progress check: the spinner drew a bar");
    if (!spinner.closed_itself)
        throw std::runtime_error("progress check: the spinner dialog did not close itself");

    const LongWorkResult bar = run_long_work(
        sdl_.window,
        sdl_.renderer,
        nullptr,
        "Open Annihilation",
        "Checking your copy of Total Annihilation",
        behind,
        [](LongWork& work) {
            work.whole.store(1000, std::memory_order_release);
            work.done.store(0, std::memory_order_release);
            oa::base::threads::sleep_ms(500);
            work.done.store(400, std::memory_order_release);
            oa::base::threads::sleep_ms(200);
            work.done.store(1000, std::memory_order_release);
            oa::base::threads::sleep_ms(400);
        }
    );
    if (!bar.shown || !bar.bar || !bar.closed_itself || bar.least_bar < 0 ||
        bar.least_bar >= bar.most_bar)
        throw std::runtime_error(
            "progress check: the bar ran from " + std::to_string(bar.least_bar) + " to " +
            std::to_string(bar.most_bar)
        );

    bool presented = false;
    const std::array<std::pair<int, int>, 3> sizes{{
        {640, 480},
        {1280, 720},
        {1920, 1080},
    }};
    for (const auto& [across, down] : sizes) {
        paint_state(sdl_.window, sdl_.renderer, across, down, true, &presented);
        paint_state(sdl_.window, sdl_.renderer, across, down, false, &presented);
    }
    if (!presented) {
        oa::ui::game_files::Viewport read;
        std::ignore = read_window_viewport(sdl_.window, sdl_.renderer, read);
        if (read.width <= 0 || read.height <= 0)
            throw std::runtime_error("progress check: the window has no size");
        const kit::Frame dialog_frame = frame_at(read.width, read.height);
        paint::Canvas pixels = paint::make_canvas(read.width, read.height);
        paint_progress_frame(
            pixels, dialog_frame, "Open Annihilation", "Loading the game's archives", -1, 2, nullptr
        );
        WindowFrame window_frame(sdl_.renderer, "the progress dialog");
        const RenderState kept(sdl_.renderer);
        if (!window_frame.show(pixels, kept, nullptr))
            throw std::runtime_error("progress check: the picture was not shown");
    }

    int translated_languages = 0;
    const std::array<std::string_view, 3> lines{
        "Loading the game's archives",
        "Checking your copy of Total Annihilation",
        "Applying your changes",
    };
    for (const oa::data::languages::Language* language : oa::data::languages::known_languages()) {
        const std::string_view tag = language->tag;
        if (tag != "de" && tag != "fr" && tag != "es" && tag != "it")
            continue;
        ++translated_languages;
        for (const std::string_view english : lines) {
            const std::string_view translated =
                oa::data::languages::interface_text_in(english, *language);
            if (translated == english)
                throw std::runtime_error(
                    std::string("progress check: ") + std::string(tag) + " has no translation of " +
                    std::string(english)
                );
            std::cout << "progress check: " << tag << ": " << translated << "\n";
        }
    }
    if (translated_languages != 4)
        throw std::runtime_error("progress check: a built-in language is missing");
    std::cout << "progress check: passed\n";
}

} // namespace oa::app
