// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A 1000 by 628 point window at one pixel per point and at two. The main
// menu's 640 by 480 picture is the same letterbox, scaled, centred and as
// tall as the drawing surface. The setup's game step and the Settings
// dialog keep one arrangement, and the drawing at two pixels per point is
// that arrangement with sharper edges. The dummy driver has no display
// scale; OA_PIXEL_DENSITY grows its window.
#include "oa/app/frame_coordinates.hpp"
#include "oa/app/window_surface.hpp"

#include "oa/platform/text_font.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/first_run/first_run.hpp"
#include "oa/ui/kit/canvas.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/theme.hpp"
#include "oa/ui/paint/painter.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

namespace kit = oa::ui::kit;
namespace paint = oa::ui::paint;
namespace renderer = oa::ui::frontend_renderer;
namespace settings = oa::ui::engine_settings;
namespace text_font = oa::platform::text_font;

constexpr int kPointsWidth = 1000;
constexpr int kPointsHeight = 628;
constexpr int kFrameWidth = 640;
constexpr int kFrameHeight = 480;
/// A pixel this bright is text, an accent or a button, not the panel.
constexpr int kInk = 100;

/// Tells whether two places are the same rectangle, within a twentieth of a pixel.
bool same_place(const SDL_FRect& got, const SDL_FRect& want) noexcept {
    return std::fabs(got.x - want.x) <= 0.05F && std::fabs(got.y - want.y) <= 0.05F &&
           std::fabs(got.w - want.w) <= 0.05F && std::fabs(got.h - want.h) <= 0.05F;
}

/// Tells whether one place is another scaled by two.
bool doubled(const SDL_FRect& once, const SDL_FRect& twice) noexcept {
    return same_place(twice, SDL_FRect{once.x * 2.0F, once.y * 2.0F, once.w * 2.0F, once.h * 2.0F});
}

/// The drawing surface the menu is presented on, without a window.
struct Output {
    SDL_Surface* surface{};
    SDL_Renderer* renderer{};

    Output(int width, int height) {
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_XRGB8888);
        if (surface != nullptr)
            renderer = SDL_CreateSoftwareRenderer(surface);
    }

    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    ~Output() {
        if (renderer != nullptr)
            SDL_DestroyRenderer(renderer);
        if (surface != nullptr)
            SDL_DestroySurface(surface);
    }
};

/// RGB bytes of a surface, tightly packed.
struct Rgb {
    int width{};
    int height{};
    std::vector<uint8_t> bytes{};
};

/// Reads a surface as RGB.
Rgb read_rgb(SDL_Surface* surface) {
    Rgb rgb;
    if (surface == nullptr)
        return rgb;
    SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGB24);
    if (converted == nullptr)
        return rgb;
    rgb.width = converted->w;
    rgb.height = converted->h;
    rgb.bytes.resize(
        static_cast<std::size_t>(rgb.width) * static_cast<std::size_t>(rgb.height) * 3U
    );
    const auto* rows = static_cast<const uint8_t*>(converted->pixels);
    for (int y = 0; y < rgb.height; ++y) {
        const auto* from =
            rows + static_cast<std::size_t>(y) * static_cast<std::size_t>(converted->pitch);
        auto* to = rgb.bytes.data() +
                   static_cast<std::size_t>(y) * static_cast<std::size_t>(rgb.width) * 3U;
        std::copy_n(from, static_cast<std::size_t>(rgb.width) * 3U, to);
    }
    SDL_DestroySurface(converted);
    return rgb;
}

/// The rectangle of pixels that are not black.
SDL_Rect lit_rectangle(const Rgb& rgb) {
    int left = rgb.width;
    int top = rgb.height;
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            const std::size_t at =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(rgb.width) +
                 static_cast<std::size_t>(x)) *
                3U;
            if ((rgb.bytes[at] | rgb.bytes[at + 1] | rgb.bytes[at + 2]) == 0)
                continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    if (right < 0)
        return {};
    return {left, top, right - left + 1, bottom - top + 1};
}

/// Writes an RGB picture when OA_G87_PICTURES names a folder.
void write_ppm(const Rgb& rgb, const char* name) {
    const char* directory = SDL_GetHint("OA_G87_PICTURES");
    if (directory == nullptr || directory[0] == '\0' || rgb.width <= 0)
        return;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path path = std::filesystem::path(directory) / name;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "P6\n" << rgb.width << ' ' << rgb.height << "\n255\n";
    output.write(
        reinterpret_cast<const char*>(rgb.bytes.data()),
        static_cast<std::streamsize>(rgb.bytes.size())
    );
}

/// Writes a painted canvas over a dark ground, so a clear pixel keeps that ground.
void write_canvas(const paint::Canvas& canvas, const char* name) {
    Rgb rgb;
    rgb.width = canvas.width;
    rgb.height = canvas.height;
    rgb.bytes.resize(
        static_cast<std::size_t>(rgb.width) * static_cast<std::size_t>(rgb.height) * 3U
    );
    constexpr uint8_t ground_r = 0x2a;
    constexpr uint8_t ground_g = 0x30;
    constexpr uint8_t ground_b = 0x24;
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            const paint::Rgba pixel = paint::pixel_at(canvas, x, y);
            const uint32_t cover = pixel.a;
            const uint32_t keep = 255U - cover;
            const std::size_t at =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(rgb.width) +
                 static_cast<std::size_t>(x)) *
                3U;
            rgb.bytes[at] = static_cast<uint8_t>((pixel.r * cover + ground_r * keep) / 255U);
            rgb.bytes[at + 1] = static_cast<uint8_t>((pixel.g * cover + ground_g * keep) / 255U);
            rgb.bytes[at + 2] = static_cast<uint8_t>((pixel.b * cover + ground_b * keep) / 255U);
        }
    }
    write_ppm(rgb, name);
}

/// A pixel bright enough to be text or an accent.
bool ink_pixel(paint::Rgba pixel) noexcept {
    return std::max(pixel.r, std::max(pixel.g, pixel.b)) >= kInk && pixel.a >= 128;
}

/// Where the bright pixels of a canvas sit.
struct InkBox {
    int left{};
    int top{};
    int right{};
    int bottom{};
    bool any{false};
};

InkBox ink_box(const paint::Canvas& canvas) {
    InkBox box;
    box.left = canvas.width;
    box.top = canvas.height;
    for (int y = 0; y < canvas.height; ++y) {
        for (int x = 0; x < canvas.width; ++x) {
            if (!ink_pixel(paint::pixel_at(canvas, x, y)))
                continue;
            box.any = true;
            box.left = std::min(box.left, x);
            box.top = std::min(box.top, y);
            box.right = std::max(box.right, x);
            box.bottom = std::max(box.bottom, y);
        }
    }
    return box;
}

/// How many rows of a point rectangle hold bright pixels, at a scale.
int ink_rows(const paint::Canvas& canvas, const kit::Rect& points, int32_t scale) {
    const kit::Rect pixels = kit::to_canvas(points, scale);
    int rows = 0;
    for (int32_t y = 0; y < pixels.height; ++y) {
        bool row = false;
        for (int32_t x = 0; x < pixels.width; ++x) {
            if (ink_pixel(paint::pixel_at(canvas, pixels.x + x, pixels.y + y))) {
                row = true;
                break;
            }
        }
        if (row)
            ++rows;
    }
    return rows;
}

/// The rightmost bright pixel of a point rectangle, at a scale; -1 when none.
int ink_right(const paint::Canvas& canvas, const kit::Rect& points, int32_t scale) {
    const kit::Rect pixels = kit::to_canvas(points, scale);
    int right = -1;
    for (int32_t y = 0; y < pixels.height; ++y) {
        for (int32_t x = 0; x < pixels.width; ++x) {
            if (ink_pixel(paint::pixel_at(canvas, pixels.x + x, pixels.y + y)))
                right = std::max(right, pixels.x + x);
        }
    }
    return right;
}

/// Draws a white frame into a black output and returns the lit rectangle.
SDL_Rect draw_white_frame(Output& output, const SDL_FRect* destination) {
    std::vector<uint8_t> white(static_cast<std::size_t>(kFrameWidth) * kFrameHeight * 3U, 255);
    SDL_Texture* frame = SDL_CreateTexture(
        output.renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC, kFrameWidth, kFrameHeight
    );
    CHECK(frame != nullptr);
    if (frame == nullptr)
        return {};
    CHECK(SDL_UpdateTexture(frame, nullptr, white.data(), kFrameWidth * 3));
    CHECK(SDL_SetTextureScaleMode(frame, SDL_SCALEMODE_NEAREST));
    CHECK(SDL_SetRenderDrawColor(output.renderer, 0, 0, 0, 255));
    CHECK(SDL_RenderClear(output.renderer));
    CHECK(SDL_RenderTexture(output.renderer, frame, nullptr, destination));
    CHECK(SDL_FlushRenderer(output.renderer));
    SDL_DestroyTexture(frame);
    return lit_rectangle(read_rgb(output.surface));
}

/// The hint's words, including the ones it refuses.
void test_hint_words() {
    CHECK(SDL_SetHint(oa::app::pixel_density_hint, "2"));
    CHECK(oa::app::asked_pixel_density() == 2);
    CHECK(SDL_SetHint(oa::app::pixel_density_hint, "8"));
    CHECK(oa::app::asked_pixel_density() == 8);
    CHECK(SDL_SetHint(oa::app::pixel_density_hint, "9"));
    CHECK(oa::app::asked_pixel_density() == 8);
    CHECK(SDL_SetHint(oa::app::pixel_density_hint, "0"));
    CHECK(oa::app::asked_pixel_density() == 1);
    CHECK(SDL_SetHint(oa::app::pixel_density_hint, "2x"));
    CHECK(oa::app::asked_pixel_density() == 1);
    CHECK(SDL_SetHint(oa::app::pixel_density_hint, ""));
    CHECK(oa::app::asked_pixel_density() == 1);
    SDL_ResetHint(oa::app::pixel_density_hint);
}

/// A distance SDL reports, in canvas pixels, for a real density and a hinted one.
void test_canvas_distance() {
    oa::app::WindowSurface real;
    real.points_width = kPointsWidth;
    real.points_height = kPointsHeight;
    real.sdl_width = kPointsWidth;
    real.sdl_height = kPointsHeight;
    real.pixel_width = kPointsWidth * 2;
    real.pixel_height = kPointsHeight * 2;
    real.density = 2.0F;
    CHECK(oa::app::surface_canvas_pixels(real, 10, true) == 20);
    CHECK(oa::app::surface_canvas_pixels(real, 10, false) == 20);

    oa::app::WindowSurface hinted = real;
    hinted.sdl_width = kPointsWidth * 2;
    hinted.sdl_height = kPointsHeight * 2;
    CHECK(oa::app::surface_canvas_pixels(hinted, 10, true) == 10);
    CHECK(oa::app::surface_canvas_pixels(hinted, 10, false) == 10);
}

/// The menu's frame on a drawing surface, through SDL and through the letterbox.
void test_menu_on_surface(int width, int height, const SDL_FRect& expect_letterbox) {
    Output output(width, height);
    CHECK(output.renderer != nullptr);
    if (output.renderer == nullptr)
        return;
    CHECK(SDL_SetRenderLogicalPresentation(
        output.renderer, kFrameWidth, kFrameHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX
    ));
    CHECK(SDL_FlushRenderer(output.renderer));
    const SDL_FRect wanted =
        oa::app::frame_letterbox(width, height, kFrameWidth, kFrameHeight, false);
    CHECK(same_place(wanted, expect_letterbox));
    SDL_FRect reckoned{};
    CHECK(SDL_GetRenderLogicalPresentationRect(output.renderer, &reckoned));
    CHECK(same_place(reckoned, wanted));
    CHECK(reckoned.y >= -0.05F && reckoned.y <= 0.05F);
    CHECK(std::fabs(reckoned.h - static_cast<float>(height)) <= 0.05F);
    CHECK(reckoned.x > 1.0F);
    const float spare = static_cast<float>(width) - (reckoned.x + reckoned.w);
    CHECK(std::fabs(reckoned.x - spare) <= 0.6F);

    const SDL_Rect drawn = oa::app::drawn_frame_rect(reckoned);
    const SDL_Rect lit = draw_white_frame(output, nullptr);
    CHECK(lit.x == drawn.x && lit.y == drawn.y && lit.w == drawn.w && lit.h == drawn.h);
    if (width == kPointsWidth)
        write_ppm(read_rgb(output.surface), "menu-1x.ppm");
    else
        write_ppm(read_rgb(output.surface), "menu-2x.ppm");

    // The same place with the logical presentation off, drawn into the rect.
    CHECK(
        SDL_SetRenderLogicalPresentation(output.renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED)
    );
    const SDL_Rect explicit_lit = draw_white_frame(output, &wanted);
    CHECK(
        explicit_lit.x == drawn.x && explicit_lit.y == drawn.y && explicit_lit.w == drawn.w &&
        explicit_lit.h == drawn.h
    );

    CHECK(SDL_SetRenderLogicalPresentation(
        output.renderer, kFrameWidth, kFrameHeight, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
    ));
    CHECK(SDL_FlushRenderer(output.renderer));
    const SDL_FRect steps =
        oa::app::frame_letterbox(width, height, kFrameWidth, kFrameHeight, true);
    SDL_FRect step_rect{};
    CHECK(SDL_GetRenderLogicalPresentationRect(output.renderer, &step_rect));
    CHECK(same_place(step_rect, steps));
}

/// The 2× letterbox is the 1× letterbox times two, and both fill the height.
void test_menu_scales() {
    const SDL_FRect once =
        oa::app::frame_letterbox(kPointsWidth, kPointsHeight, kFrameWidth, kFrameHeight, false);
    const SDL_FRect twice = oa::app::frame_letterbox(
        kPointsWidth * 2, kPointsHeight * 2, kFrameWidth, kFrameHeight, false
    );
    CHECK(doubled(once, twice));
    test_menu_on_surface(kPointsWidth, kPointsHeight, once);
    test_menu_on_surface(kPointsWidth * 2, kPointsHeight * 2, twice);

    const SDL_FRect once_steps =
        oa::app::frame_letterbox(kPointsWidth, kPointsHeight, kFrameWidth, kFrameHeight, true);
    const SDL_FRect twice_steps = oa::app::frame_letterbox(
        kPointsWidth * 2, kPointsHeight * 2, kFrameWidth, kFrameHeight, true
    );
    CHECK(doubled(once_steps, twice_steps));
}

/// OA_PIXEL_DENSITY grows a dummy window, once, and the surface keeps the points.
void test_hint_grows_window() {
    CHECK(SDL_SetHint(oa::app::pixel_density_hint, "1"));
    SDL_Window* window = SDL_CreateWindow("Open Annihilation", kPointsWidth, kPointsHeight, 0);
    CHECK(window != nullptr);
    if (window == nullptr)
        return;
    SDL_Renderer* renderer = SDL_CreateRenderer(window, "software");
    CHECK(renderer != nullptr);
    oa::app::apply_asked_pixel_density(window);
    int width = 0;
    int height = 0;
    CHECK(SDL_GetWindowSize(window, &width, &height));
    CHECK(width == kPointsWidth && height == kPointsHeight);

    CHECK(SDL_SetHint(oa::app::pixel_density_hint, "2"));
    oa::app::apply_asked_pixel_density(window);
    SDL_PumpEvents();
    CHECK(SDL_GetWindowSize(window, &width, &height));
    CHECK(width == kPointsWidth * 2 && height == kPointsHeight * 2);
    // A second call for the same window and density does not grow it again.
    oa::app::apply_asked_pixel_density(window);
    CHECK(SDL_GetWindowSize(window, &width, &height));
    CHECK(width == kPointsWidth * 2 && height == kPointsHeight * 2);

    const oa::app::WindowSurface surface = oa::app::window_surface(window, renderer);
    CHECK(surface.points_width == kPointsWidth);
    CHECK(surface.points_height == kPointsHeight);
    CHECK(surface.pixel_width == kPointsWidth * 2);
    CHECK(surface.pixel_height == kPointsHeight * 2);
    CHECK(std::fabs(surface.density - 2.0F) <= 0.01F);

    int output_w = 0;
    int output_h = 0;
    CHECK(SDL_GetRenderOutputSize(renderer, &output_w, &output_h));
    CHECK(output_w == kPointsWidth * 2 && output_h == kPointsHeight * 2);
    CHECK(SDL_SetRenderLogicalPresentation(
        renderer, kFrameWidth, kFrameHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX
    ));
    const oa::app::PlacedFrame placed =
        oa::app::place_frame(window, renderer, kFrameWidth, kFrameHeight, false);
    const SDL_FRect wanted = oa::app::frame_letterbox(
        kPointsWidth * 2, kPointsHeight * 2, kFrameWidth, kFrameHeight, false
    );
    CHECK(placed.sdl_matches);
    CHECK(same_place(placed.drawn, wanted));
    CHECK(placed.drawn.x > 1.0F);
    CHECK(std::fabs(placed.drawn.h - static_cast<float>(kPointsHeight * 2)) <= 0.05F);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    oa::app::apply_asked_pixel_density(nullptr);
    CHECK(SDL_SetHint(oa::app::pixel_density_hint, "1"));
}

/// The room OA's screens lay out in, from a canvas at one density.
kit::Frame room_of(int pixels_w, int pixels_h, float density) {
    kit::Viewport canvas;
    canvas.width = pixels_w;
    canvas.height = pixels_h;
    canvas.density = density;
    return kit::frame_of(kit::layer_viewport(canvas, 0));
}

/// The game step, with one installer chosen so Copy the game can be pressed.
oa::ui::first_run::Session game_session() {
    oa::ui::first_run::Session session;
    session.model.leave = oa::ui::first_run::Leave::quit;
    session.model.walk = oa::ui::first_run::Walk::setup;
    session.model.step = oa::ui::first_run::Step::game;
    oa::ui::first_run::GameSource source;
    source.title = "setup_total_annihilation.exe";
    source.detail = "~/Downloads";
    source.mark = oa::ui::first_run::SourceMark::gog;
    source.footnote = "1.0 GB";
    source.chips = {
        {"Total Annihilation 3.1c", true},
        {"Campaign", true},
        {"Core Contingency", true},
        {"Battle Tactics", true},
        {"Music", true},
    };
    session.model.sources.push_back(std::move(source));
    session.model.source = 0;
    return session;
}

/// A smooth canvas of a point size at a scale.
struct SmoothPicture {
    int32_t width{};
    int32_t height{};
    paint::Canvas pixels;
    paint::Painter painter;
    kit::Canvas canvas{};

    SmoothPicture(
        text_font::FontStack* fonts, int32_t point_width, int32_t point_height, int32_t scale
    )
        : width(point_width * scale / 100), height(point_height * scale / 100),
          pixels(paint::make_canvas(width, height)), painter(pixels) {
        canvas.smooth.painter = &painter;
        canvas.smooth.fonts = fonts;
        canvas.smooth.scale_percent = scale;
    }
};

/// Cells of a canvas that hold bright pixels, so two scales can be compared.
std::vector<char> occupancy(const paint::Canvas& canvas, int columns, int rows) {
    std::vector<char> cells(static_cast<std::size_t>(columns * rows), 0);
    if (canvas.width <= 0 || canvas.height <= 0)
        return cells;
    for (int y = 0; y < canvas.height; ++y) {
        const int row = std::min(y * rows / canvas.height, rows - 1);
        for (int x = 0; x < canvas.width; ++x) {
            if (!ink_pixel(paint::pixel_at(canvas, x, y)))
                continue;
            const int column = std::min(x * columns / canvas.width, columns - 1);
            cells[static_cast<std::size_t>(row * columns + column)] = 1;
        }
    }
    return cells;
}

/// The setup's game step at 1000 by 628 points, drawn at 1× and at 2×.
void test_setup(text_font::FontStack* fonts, const kit::Frame& once, const kit::Frame& twice) {
    CHECK(once.area.width == twice.area.width);
    CHECK(once.area.height == twice.area.height);
    CHECK(once.size_class == kit::SizeClass::regular);
    CHECK(twice.size_class == kit::SizeClass::regular);
    CHECK(once.area.width == kPointsWidth);
    CHECK(once.area.height == kPointsHeight);
    CHECK(once.scale_percent == 100);
    CHECK(twice.scale_percent == 200);

    const oa::ui::first_run::Session session = game_session();
    const auto scene_once = oa::ui::first_run::lay_out(session, once, nullptr);
    const auto scene_twice = oa::ui::first_run::lay_out(session, twice, nullptr);
    CHECK(scene_once.list.controls.size() == scene_twice.list.controls.size());
    const std::size_t controls =
        std::min(scene_once.list.controls.size(), scene_twice.list.controls.size());
    for (std::size_t index = 0; index < controls; ++index) {
        const kit::Rect& left = scene_once.list.controls[index].rect;
        const kit::Rect& right = scene_twice.list.controls[index].rect;
        CHECK(left.x == right.x && left.y == right.y);
        CHECK(left.width == right.width && left.height == right.height);
    }
    const kit::Control* next = kit::control_of(scene_once.list, oa::ui::first_run::next_id);
    const kit::Control* locate = kit::control_of(scene_once.list, oa::ui::first_run::locate_id);
    CHECK(next != nullptr && locate != nullptr);
    if (next == nullptr || locate == nullptr)
        return;
    CHECK(next->rect.width > kit::compact_metrics.button_width);
    CHECK(locate->rect.width == kit::compact_metrics.button_width);
    CHECK(next->rect.height == kit::compact_metrics.button_height);
    const int32_t next_right = next->rect.x + next->rect.width;
    CHECK(next_right <= once.area.width - kit::compact_metrics.edge);
    CHECK(next->rect.x >= 0 && next->rect.y >= 0);
    CHECK(next->rect.y + next->rect.height <= once.area.height);

    SmoothPicture low(fonts, once.area.width, once.area.height, 100);
    SmoothPicture high(fonts, twice.area.width, twice.area.height, 200);
    oa::ui::first_run::draw(low.canvas, scene_once, session);
    oa::ui::first_run::draw(high.canvas, scene_twice, session);
    write_canvas(low.pixels, "setup-1x.ppm");
    write_canvas(high.pixels, "setup-2x.ppm");

    const InkBox low_ink = ink_box(low.pixels);
    const InkBox high_ink = ink_box(high.pixels);
    CHECK(low_ink.any && high_ink.any);
    if (low_ink.any && high_ink.any) {
        const float low_left = static_cast<float>(low_ink.left) / static_cast<float>(low.width);
        const float high_left = static_cast<float>(high_ink.left) / static_cast<float>(high.width);
        const float low_top = static_cast<float>(low_ink.top) / static_cast<float>(low.height);
        const float high_top = static_cast<float>(high_ink.top) / static_cast<float>(high.height);
        const float low_right = static_cast<float>(low_ink.right) / static_cast<float>(low.width);
        const float high_right =
            static_cast<float>(high_ink.right) / static_cast<float>(high.width);
        const float low_bottom =
            static_cast<float>(low_ink.bottom) / static_cast<float>(low.height);
        const float high_bottom =
            static_cast<float>(high_ink.bottom) / static_cast<float>(high.height);
        CHECK(std::fabs(low_left - high_left) <= 0.03F);
        CHECK(std::fabs(low_top - high_top) <= 0.03F);
        CHECK(std::fabs(low_right - high_right) <= 0.03F);
        CHECK(std::fabs(low_bottom - high_bottom) <= 0.03F);
    }
    constexpr int kColumns = 20;
    constexpr int kRows = 12;
    const std::vector<char> low_cells = occupancy(low.pixels, kColumns, kRows);
    const std::vector<char> high_cells = occupancy(high.pixels, kColumns, kRows);
    int differ = 0;
    for (std::size_t index = 0; index < low_cells.size(); ++index)
        differ += low_cells[index] == high_cells[index] ? 0 : 1;
    CHECK(differ <= 12);

    const int low_title = ink_rows(low.pixels, scene_once.title, 100);
    const int high_title = ink_rows(high.pixels, scene_twice.title, 200);
    CHECK(low_title >= 16);
    CHECK(high_title * 10 >= low_title * 16);
    CHECK(high_title * 10 <= low_title * 24);

    const kit::Rect footer{
        0,
        once.area.height - kit::compact_metrics.footer_height - kit::compact_metrics.edge,
        once.area.width,
        kit::compact_metrics.footer_height + kit::compact_metrics.edge
    };
    const int low_right = ink_right(low.pixels, footer, 100);
    const int high_right = ink_right(high.pixels, footer, 200);
    const kit::Rect next_low = kit::to_canvas(next->rect, 100);
    const kit::Rect next_high = kit::to_canvas(next->rect, 200);
    CHECK(low_right >= next_low.x + next_low.width - 2);
    CHECK(low_right <= next_low.x + next_low.width + 2);
    CHECK(high_right >= next_high.x + next_high.width - 3);
    CHECK(high_right <= next_high.x + next_high.width + 3);
    CHECK(low_right <= low.width - 8);
    CHECK(high_right <= high.width - 16);
}

/// Where the Settings dialog sits in a window, centred at the room's scale.
struct DialogPlace {
    int32_t x{};
    int32_t y{};
    int32_t width{};
    int32_t height{};
    int32_t scale{};
};

DialogPlace dialog_place(int pixels_w, int pixels_h, int32_t scale) {
    const kit::Metrics& sized = kit::metrics_of(kit::SizeClass::regular);
    const kit::Rect drawn = kit::to_canvas({0, 0, sized.dialog_width, sized.dialog_height}, scale);
    return {
        (pixels_w - drawn.width) / 2,
        (pixels_h - drawn.height) / 2,
        drawn.width,
        drawn.height,
        scale
    };
}

/// A black surface.
renderer::Surface black_surface(int32_t width, int32_t height) {
    renderer::Surface surface;
    surface.width = static_cast<uint32_t>(width);
    surface.height = static_cast<uint32_t>(height);
    surface.rgb.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U, 0);
    return surface;
}

/// The bounding box of a surface's pixels that are not black.
SDL_Rect surface_lit(const renderer::Surface& surface) {
    Rgb rgb;
    rgb.width = static_cast<int>(surface.width);
    rgb.height = static_cast<int>(surface.height);
    rgb.bytes = surface.rgb;
    return lit_rectangle(rgb);
}

/// The Settings dialog at Regular, the same arrangement at 1× and at 2×.
void test_settings(const kit::Frame& once, const kit::Frame& twice) {
    const kit::Metrics& sized = kit::metrics_of(kit::SizeClass::regular);
    CHECK(sized.dialog_width == 720);
    CHECK(sized.dialog_height == 486);
    const DialogPlace low = dialog_place(kPointsWidth, kPointsHeight, once.scale_percent);
    const DialogPlace high = dialog_place(kPointsWidth * 2, kPointsHeight * 2, twice.scale_percent);
    CHECK(low.scale == 100 && high.scale == 200);
    CHECK(high.x == low.x * 2 && high.y == low.y * 2);
    CHECK(high.width == low.width * 2 && high.height == low.height * 2);
    CHECK(low.x > 0 && low.y > 0);
    CHECK(low.x + low.width < kPointsWidth);
    CHECK(low.y + low.height < kPointsHeight);

    settings::Dialog dialog;
    settings::open_dialog(dialog, {}, {}, {}, "0.8", settings::Page::graphics);
    dialog.size_class = kit::SizeClass::regular;
    const settings::DialogFonts fonts{};
    const renderer::RgbaPicture icon{};
    renderer::Surface low_surface = black_surface(low.width, low.height);
    renderer::Surface high_surface = black_surface(high.width, high.height);
    settings::draw_dialog(low_surface, {0, 0, 1}, dialog, fonts, icon);
    settings::draw_dialog(high_surface, {0, 0, 2}, dialog, fonts, icon);
    write_ppm(Rgb{low.width, low.height, low_surface.rgb}, "settings-1x.ppm");
    write_ppm(Rgb{high.width, high.height, high_surface.rgb}, "settings-2x.ppm");

    const SDL_Rect low_lit = surface_lit(low_surface);
    const SDL_Rect high_lit = surface_lit(high_surface);
    CHECK(low_lit.w > low.width * 9 / 10 && low_lit.h > low.height * 9 / 10);
    CHECK(high_lit.w > high.width * 9 / 10 && high_lit.h > high.height * 9 / 10);
    // Each source pixel is a 2 by 2 block. A handful may blend at an edge.
    int differ = 0;
    int seen = 0;
    for (int y = 0; y < low.height; ++y) {
        for (int x = 0; x < low.width; ++x) {
            const std::size_t low_at =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(low.width) +
                 static_cast<std::size_t>(x)) *
                3U;
            const std::size_t high_at =
                (static_cast<std::size_t>(y * 2) * static_cast<std::size_t>(high.width) +
                 static_cast<std::size_t>(x * 2)) *
                3U;
            const bool same = low_surface.rgb[low_at] == high_surface.rgb[high_at] &&
                              low_surface.rgb[low_at + 1] == high_surface.rgb[high_at + 1] &&
                              low_surface.rgb[low_at + 2] == high_surface.rgb[high_at + 2];
            differ += same ? 0 : 1;
            ++seen;
        }
    }
    CHECK(seen > 0 && differ * 100 <= seen);
}

} // namespace

int main() {
    // The environment may already force these, at a priority SetHint cannot
    // replace. Either way the check runs headless on the software renderer.
    const auto want_hint = [](const char* name, const char* value) {
        const char* now = SDL_GetHint(name);
        if (now != nullptr && std::strcmp(now, value) == 0)
            return;
        CHECK(SDL_SetHintWithPriority(name, value, SDL_HINT_OVERRIDE));
    };
    want_hint(SDL_HINT_VIDEO_DRIVER, "dummy");
    want_hint(SDL_HINT_RENDER_DRIVER, "software");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    test_hint_words();
    test_canvas_distance();
    test_menu_scales();
    test_hint_grows_window();

    const kit::Frame once = room_of(kPointsWidth, kPointsHeight, 1.0F);
    const kit::Frame twice = room_of(kPointsWidth * 2, kPointsHeight * 2, 2.0F);
    auto fonts = text_font::FontStack::open(text_font::bundled_font_directory());
    CHECK(fonts != nullptr);
    if (fonts != nullptr)
        test_setup(fonts.get(), once, twice);
    test_settings(once, twice);

    SDL_Quit();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
