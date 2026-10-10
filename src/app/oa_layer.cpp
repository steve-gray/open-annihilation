// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The OA layer (oa_layer.hpp): its stack of screens and the queue of those
// waiting for it, their input and ticks, its drawing over the front end's
// picture and over a match, the overlay that hands it the game's input and
// frames, the settings dialog as one of its screens (Runtime::SettingsScreen),
// on the main menu and in a match, and the screens of a notice and a
// question (NoticeScreen, QuestionScreen). On the front end the screens are
// laid out at the size class of the window's canvas and drawn at its whole
// scale in the window's own pixels.

#include "oa_layer.hpp"

#include "engine_settings_menu_host.hpp"
#include "engine_settings_state.hpp"
#include "render_state.hpp"

#include "oa/app/automation_host.hpp"
#include "oa/app/frame_coordinates.hpp"
#include "oa/app/runtime.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/ui/display_layout.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/engine_settings/notice.hpp"
#include "oa/ui/engine_settings/prompt.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/components_more.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/theme.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace layout = oa::ui::display_layout;
namespace kit = oa::ui::kit;
namespace artless = oa::ui::frontend_renderer;

namespace {

/// The layer's overlay's z: over the extensions' overlays, under the
/// frontend's message boxes.
constexpr int16_t kLayerOverlayZ = 100;

/// The settings' screen's name.
constexpr std::string_view kSettingsName = "settings";

/// The match's layer's opacity where it darkens the screen, in 255ths.
constexpr uint32_t kBackdropAlpha = (kit::ingame_backdrop_opacity * 255U + 128U) / 256U;

/// The sound opening the settings and OK play.
constexpr std::string_view kOpenSound = "Options";
/// The sound Cancel plays.
constexpr std::string_view kCancelSound = "Previous";

/// Scales a source length by the side column's scale.
///
/// @param value source pixels
/// @param scale the side column's scale
/// @return window pixels, at least 1
int32_t scaled_length(int32_t value, double scale) noexcept {
    return std::max(1, static_cast<int32_t>(std::lround(static_cast<double>(value) * scale)));
}

/// Returns the scale the side column is drawn at: the chrome's, or the
/// smaller one a side-column page taller than the window gives it
/// (display_layout::fit_side_column).
///
/// @param laid_out the match's layout
/// @return canvas pixels per source pixel of the side column
double column_scale(const layout::MatchLayout& laid_out) noexcept {
    return laid_out.column_narrowed() ? laid_out.column_scale : laid_out.scale;
}

/// Tells whether a point lies in a rectangle.
///
/// @param rect the rectangle
/// @param x the point's column
/// @param y the point's row
/// @return true inside
bool rect_contains(const layout::Rect& rect, float x, float y) noexcept {
    return x >= static_cast<float>(rect.x) && y >= static_cast<float>(rect.y) &&
           x < static_cast<float>(rect.x + rect.width) &&
           y < static_cast<float>(rect.y + rect.height);
}

/// Tells whether a source rectangle lies wholly in another.
///
/// @param inner the rectangle
/// @param outer the other
/// @return true when no part of it lies outside
bool source_holds(const layout::Rect& outer, const layout::Rect& inner) noexcept {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

/// Tells whether a rectangle holds no pixel.
///
/// @param rect the rectangle
/// @return true when it is not wide or not high
bool empty_rect(const layout::Rect& rect) noexcept {
    return rect.width <= 0 || rect.height <= 0;
}

/// Tells whether two rectangles are the same.
///
/// @param first a rectangle
/// @param second another
/// @return true when every edge is the same
bool same_rect(const layout::Rect& first, const layout::Rect& second) noexcept {
    return first.x == second.x && first.y == second.y && first.width == second.width &&
           first.height == second.height;
}

/// Returns the smallest rectangle holding two.
///
/// @param first a rectangle; empty for none
/// @param second another; empty for none
/// @return the rectangle holding both
layout::Rect union_rect(const layout::Rect& first, const layout::Rect& second) noexcept {
    if (empty_rect(first))
        return second;
    if (empty_rect(second))
        return first;
    const int32_t left = std::min(first.x, second.x);
    const int32_t top = std::min(first.y, second.y);
    const int32_t right = std::max(first.x + first.width, second.x + second.width);
    const int32_t bottom = std::max(first.y + first.height, second.y + second.height);
    return {left, top, right - left, bottom - top};
}

/// Returns the rectangle two rectangles share.
///
/// @param first a rectangle
/// @param second another
/// @return the shared rectangle; empty when they do not meet
layout::Rect intersection(const layout::Rect& first, const layout::Rect& second) noexcept {
    const int32_t left = std::max(first.x, second.x);
    const int32_t top = std::max(first.y, second.y);
    const int32_t right = std::min(first.x + first.width, second.x + second.width);
    const int32_t bottom = std::min(first.y + first.height, second.y + second.height);
    if (right <= left || bottom <= top)
        return {};
    return {left, top, right - left, bottom - top};
}

/// Copies a screen drawn in window pixels onto the front end's picture: each
/// picture pixel the screen covers takes the screen's pixel at the picture
/// pixel's centre in the window. Where the picture fills the window, pixel
/// for pixel, that is the drawing copied at its place.
///
/// @param[in,out] frame the picture
/// @param drawn the screen's drawing, as large as its place
/// @param shown where the screen shows, in window pixels
/// @param view where the picture lies in the window
void stamp_onto_picture(
    renderer::Surface& frame,
    const renderer::Surface& drawn,
    const layout::Rect& shown,
    const LayerView& view
) {
    if (drawn.width == 0 || drawn.height == 0 || empty_rect(shown))
        return;
    const auto width = static_cast<int32_t>(frame.width);
    const auto height = static_cast<int32_t>(frame.height);
    // The window pixel under a picture pixel's centre, along one axis.
    const auto window_pixel = [](int32_t pixel, int32_t start, int32_t length, int32_t size) {
        return start + static_cast<int32_t>(
                           (static_cast<int64_t>(2 * pixel + 1) * length) / (2 * int64_t{size})
                       );
    };
    for (int32_t row = 0; row < height; ++row) {
        const int32_t source_row =
            window_pixel(row, view.picture.y, view.picture.height, view.picture_size.y) - shown.y;
        if (source_row < 0 || source_row >= static_cast<int32_t>(drawn.height))
            continue;
        for (int32_t column = 0; column < width; ++column) {
            const int32_t source_column =
                window_pixel(column, view.picture.x, view.picture.width, view.picture_size.x) -
                shown.x;
            if (source_column < 0 || source_column >= static_cast<int32_t>(drawn.width))
                continue;
            const auto* from =
                drawn.rgb.data() + (static_cast<std::size_t>(source_row) * drawn.width +
                                    static_cast<std::size_t>(source_column)) *
                                       3U;
            auto* to = frame.rgb.data() + (static_cast<std::size_t>(row) * frame.width +
                                           static_cast<std::size_t>(column)) *
                                              3U;
            to[0] = from[0];
            to[1] = from[1];
            to[2] = from[2];
        }
    }
}

/// Tells whether two pictures hold the same pixels.
///
/// @param first a picture
/// @param second another
/// @return true when their sizes and pixels are the same
bool same_picture(const renderer::Surface& first, const renderer::Surface& second) {
    return first.width == second.width && first.height == second.height && first.rgb == second.rgb;
}

/// Darkens a picture once for each modal screen with a backdrop over it:
/// each pass blends the whole picture with the backdrop's colour.
///
/// @param[in,out] picture the picture
/// @param passes how many times it is darkened
/// @param opacity the backdrop's share of each pixel, in 256ths
void darken(renderer::Surface& picture, uint32_t passes, uint32_t opacity) {
    for (uint32_t pass = 0; pass < passes; ++pass)
        artless::blend_source_rect(
            picture,
            {0, 0, 1},
            {0, 0, static_cast<int32_t>(picture.width), static_cast<int32_t>(picture.height)},
            kit::rgb(kit::colour::backdrop),
            opacity
        );
}

/// How the window shows the canvas, for the rectangles automation lists the
/// layer's controls at.
struct WindowPlace {
    SDL_Renderer* renderer{}; ///< the renderer; null shows the canvas one to one
    float density{1.0F};      ///< the window's pixels to a window coordinate
    /// The front end's picture is drawn in the window, the screens over it
    /// in the window's own pixels (LayerPlacement::shown); otherwise the
    /// screens lie on the match's canvas, or the frame holds them.
    bool front_end{};
    SDL_FRect picture{};     ///< where the picture lies in the window, in its pixels
    double scale_x{};        ///< window pixels per picture pixel, across
    double scale_y{};        ///< window pixels per picture pixel, down
    int32_t output_width{};  ///< the window's width, in its pixels
    int32_t output_height{}; ///< the window's height, in its pixels
};

/// Returns how the window shows the canvas now, as present places the layer.
///
/// @param renderer the renderer; may be null
/// @param match a match shows
/// @param surface_width the front end's picture's width, without a logical presentation
/// @param surface_height its height
/// @return the placement
WindowPlace
window_place(SDL_Renderer* renderer, bool match, int32_t surface_width, int32_t surface_height) {
    WindowPlace place{};
    place.renderer = renderer;
    if (renderer == nullptr)
        return place;
    if (SDL_Window* window = SDL_GetRenderWindow(renderer)) {
        const float density = SDL_GetWindowPixelDensity(window);
        if (density > 0.0F)
            place.density = density;
    }
    SDL_FRect area{};
    int output_width = 0;
    int output_height = 0;
    if (match || !SDL_GetRenderLogicalPresentationRect(renderer, &area) || !(area.w > 0.0F) ||
        !(area.h > 0.0F) || !SDL_GetRenderOutputSize(renderer, &output_width, &output_height))
        return place;
    // The picture's own size, which the logical presentation scales into area.
    int picture_width = 0;
    int picture_height = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    if (!SDL_GetRenderLogicalPresentation(renderer, &picture_width, &picture_height, &mode) ||
        mode == SDL_LOGICAL_PRESENTATION_DISABLED || picture_width <= 0 || picture_height <= 0) {
        picture_width = surface_width;
        picture_height = surface_height;
    }
    if (picture_width <= 0 || picture_height <= 0)
        return place;
    place.front_end = true;
    place.picture = area;
    place.scale_x = static_cast<double>(area.w) / picture_width;
    place.scale_y = static_cast<double>(area.h) / picture_height;
    place.output_width = output_width;
    place.output_height = output_height;
    return place;
}

/// A rectangle's edges on the canvas, not yet whole pixels.
struct Edges {
    double left{};   ///< its left edge
    double top{};    ///< its top edge
    double right{};  ///< the edge right of it
    double bottom{}; ///< the edge under it
};

/// Returns where a rectangle of a screen's points lies where the screen is
/// placed: the whole pixels the nearest pixel's rule stamps it on
/// (OaLayer::stamp). On the front end, where a screen is drawn at the view's
/// scale as large as its place, those are the window pixels it is drawn on.
///
/// @param placed where the screen shows
/// @param points the rectangle, in the screen's points
/// @param scale_percent the scale the screen is drawn at
/// @return the edges; all 0 when the screen has no size
Edges screen_edges(const LayerPlacement& placed, const kit::Rect& points, int32_t scale_percent) {
    const kit::Rect drawn = kit::to_canvas(points, scale_percent);
    const kit::Rect whole =
        kit::to_canvas({0, 0, placed.points_width, placed.points_height}, scale_percent);
    if (whole.width <= 0 || whole.height <= 0)
        return {};
    const auto along = [](int32_t start, int32_t shown_length, int32_t length, int32_t pixel) {
        // The first pixel whose nearest drawn pixel is at or past this one.
        const int64_t scaled = static_cast<int64_t>(pixel) * shown_length;
        const int64_t first = scaled >= 0 ? (scaled + length - 1) / length : scaled / length;
        return static_cast<double>(start + first);
    };
    const auto& shown = placed.shown;
    return {
        along(shown.x, shown.width, whole.width, drawn.x),
        along(shown.y, shown.height, whole.height, drawn.y),
        along(shown.x, shown.width, whole.width, drawn.x + drawn.width),
        along(shown.y, shown.height, whole.height, drawn.y + drawn.height),
    };
}

/// Returns the window's pixels a canvas rectangle is drawn on: over the
/// front end's picture those whose centres it holds, clipped to the window,
/// as the picture's own pixels are scaled there; in a match its corners
/// through the renderer; without one, the canvas's own.
///
/// @param place how the window shows the canvas
/// @param edges the rectangle's edges on the canvas
/// @return the rectangle, in the window's pixels
layout::Rect window_rect_of(const WindowPlace& place, const Edges& edges) {
    if (place.renderer == nullptr) {
        const auto whole = [](double edge) { return static_cast<int32_t>(std::lround(edge)); };
        return {
            whole(edges.left),
            whole(edges.top),
            whole(edges.right) - whole(edges.left),
            whole(edges.bottom) - whole(edges.top)
        };
    }
    if (place.front_end) {
        const auto pixel = [](double area_start, double scale, double edge) {
            return static_cast<int32_t>(std::ceil(area_start + edge * scale - 0.5));
        };
        const int32_t left =
            std::clamp(pixel(place.picture.x, place.scale_x, edges.left), 0, place.output_width);
        const int32_t first_row =
            std::clamp(pixel(place.picture.y, place.scale_y, edges.top), 0, place.output_height);
        const int32_t right = std::clamp(
            pixel(place.picture.x, place.scale_x, edges.right), left, place.output_width
        );
        const int32_t bottom = std::clamp(
            pixel(place.picture.y, place.scale_y, edges.bottom), first_row, place.output_height
        );
        return {left, first_row, right - left, bottom - first_row};
    }
    float left = 0.0F;
    float first_row = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
    if (!frame_to_window(
            place.renderer,
            static_cast<float>(edges.left),
            static_cast<float>(edges.top),
            &left,
            &first_row
        ) ||
        !frame_to_window(
            place.renderer,
            static_cast<float>(edges.right),
            static_cast<float>(edges.bottom),
            &right,
            &bottom
        ))
        return {};
    const auto pixel = [&place](float coordinate) {
        return static_cast<int32_t>(std::lround(coordinate * place.density));
    };
    return {
        pixel(left), pixel(first_row), pixel(right) - pixel(left), pixel(bottom) - pixel(first_row)
    };
}

/// Returns the window's pixels a rectangle of a front-end screen is drawn on:
/// its edges, which are window pixels already, clipped to the window.
///
/// @param place how the window shows the canvas
/// @param edges the rectangle's edges, in the window's pixels
/// @return the rectangle, in the window's pixels
layout::Rect window_rect_at(const WindowPlace& place, const Edges& edges) {
    const auto whole = [](double edge) { return static_cast<int32_t>(std::lround(edge)); };
    const int32_t left = std::clamp(whole(edges.left), 0, place.output_width);
    const int32_t first_row = std::clamp(whole(edges.top), 0, place.output_height);
    const int32_t right = std::clamp(whole(edges.right), left, place.output_width);
    const int32_t bottom = std::clamp(whole(edges.bottom), first_row, place.output_height);
    return {left, first_row, right - left, bottom - first_row};
}

/// Returns the canvas rectangle a window rectangle covers: its corners
/// mapped back through the canvas's placement, as the game maps the
/// pointer, the top left rounded down and the bottom right up.
///
/// @param place how the window shows the canvas
/// @param window the rectangle, in the window's pixels
/// @return the rectangle, in canvas pixels; the window's own without a renderer
layout::Rect canvas_rect_of(const WindowPlace& place, const layout::Rect& window) {
    if (place.renderer == nullptr)
        return window;
    const FrameShift shift = renderer_frame_shift(place.renderer);
    const auto to_canvas = [&place,
                            &shift](int32_t x, int32_t y, float& canvas_x, float& canvas_y) {
        if (!SDL_RenderCoordinatesFromWindow(
                place.renderer,
                static_cast<float>(x) / place.density,
                static_cast<float>(y) / place.density,
                &canvas_x,
                &canvas_y
            ))
            return false;
        canvas_x += shift.x;
        canvas_y += shift.y;
        return true;
    };
    float left = 0.0F;
    float first_row = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
    if (!to_canvas(window.x, window.y, left, first_row) ||
        !to_canvas(window.x + window.width, window.y + window.height, right, bottom))
        return window;
    const auto down = [](float edge) { return static_cast<int32_t>(std::floor(edge)); };
    const auto up = [](float edge) { return static_cast<int32_t>(std::ceil(edge)); };
    return {down(left), down(first_row), up(right) - down(left), up(bottom) - down(first_row)};
}

/// Sets a control's rectangles: in the window's pixels, and on the canvas.
///
/// @param[out] control the control
/// @param place how the window shows the canvas
/// @param window where it lies, in the window's pixels (window_rect_of, window_rect_at)
void place_control(
    AutomationControl& control, const WindowPlace& place, const layout::Rect& window
) {
    const layout::Rect canvas = canvas_rect_of(place, window);
    control.x = canvas.x;
    control.y = canvas.y;
    control.width = canvas.width;
    control.height = canvas.height;
    control.window_pixels = true;
    control.window_x = window.x;
    control.window_y = window.y;
    control.window_width = window.width;
    control.window_height = window.height;
}

/// The prefix of every name and dialog the layer lists to automation.
constexpr std::string_view kAutomationPrefix = "oa.";

/// Returns a quotient rounded down, for a numerator below 0 too.
///
/// @param numerator the numerator
/// @param denominator the denominator, above 0
/// @return floor(numerator / denominator)
int32_t floor_div(int32_t numerator, int32_t denominator) noexcept {
    const int32_t quotient = numerator / denominator;
    return numerator % denominator < 0 ? quotient - 1 : quotient;
}

/// Returns the size class a screen of the front end is laid out at.
///
/// @param view what the screen is placed on
/// @return the view's class; Compact in a match
kit::SizeClass front_class(const LayerView& view) noexcept {
    return view.match ? kit::SizeClass::compact : view.frame.size_class;
}

/// Returns where a notice or a question of a height shows: centred in the
/// view's room at its scale, its class's notice width wide.
///
/// @param view what it is placed on
/// @param box_height its height, in points
/// @return its place
LayerPlacement centred_box(const LayerView& view, int32_t box_height) noexcept {
    return centred_placement(view, kit::metrics_of(front_class(view)).notice_width, box_height);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The settings dialog as a screen of the layer

/// The settings dialog as a screen of the OA layer: modal, over a backdrop,
/// centred on the main menu's picture or beside the in-game menu's column.
struct Runtime::SettingsScreen final : LayerScreen {
    /// Makes the screen of the dialog open now.
    ///
    /// @param runtime the runtime
    /// @param in_match the dialog was opened in a match; otherwise on the main menu
    SettingsScreen(Runtime& runtime, bool in_match) noexcept
        : runtime_(runtime), in_match_(in_match) {}

    /// Returns "settings".
    ///
    /// @return the name
    [[nodiscard]] std::string_view name() const override { return kSettingsName; }

    /// Returns where the dialog shows: on the main menu at the view's size
    /// class, centred in the window at its scale; in a match at Compact where
    /// match_dialog_rect puts it.
    ///
    /// @param view what the screen is placed on
    /// @return its place; empty once the dialog closed, or off its screen
    [[nodiscard]] LayerPlacement placement(const LayerView& view) const override {
        if (runtime_.engine_settings_dialog() == nullptr)
            return {};
        if (!in_match_) {
            if (view.match || runtime_.screen_ != Screen::main_menu)
                return {};
            // At the view's class (lay_out), centred at its scale.
            const kit::Metrics& sized = kit::metrics_of(front_class(view));
            return centred_placement(view, sized.dialog_width, sized.dialog_height);
        }
        if (!view.match || view.match_layout == nullptr)
            return {};
        return {
            OaLayer::match_dialog_rect(*view.match_layout, view.fit_safe_area),
            settings::dialog_width,
            settings::dialog_height
        };
    }

    /// Tells that the dialog takes every input.
    ///
    /// @return true
    [[nodiscard]] bool modal() const override { return true; }

    /// Tells that what lies under the dialog darkens.
    ///
    /// @return true
    [[nodiscard]] bool backdrop() const override { return true; }

    /// Lays the dialog out at the view's size class on the main menu, and at
    /// Compact in a match (Dialog::size_class).
    ///
    /// @param view what the screen is placed on
    void lay_out(const LayerView& view) override {
        auto* dialog = runtime_.engine_settings_dialog();
        if (dialog == nullptr)
            return;
        const kit::SizeClass size_class = in_match_ ? kit::SizeClass::compact : front_class(view);
        if (dialog->size_class == size_class)
            return;
        dialog->size_class = size_class;
        ++revision_;
    }

    /// Draws the dialog with the dialog's fonts and icon, at the canvas's scale.
    ///
    /// @param canvas where it draws
    void draw(const kit::Canvas& canvas) const override {
        const auto* dialog = runtime_.engine_settings_dialog();
        if (dialog == nullptr || canvas.surface == nullptr || canvas.fonts == nullptr)
            return;
        settings::draw_dialog(
            *canvas.surface, canvas.placement, *dialog, *canvas.fonts, canvas.icon
        );
    }

    /// Takes a pointer's move, press or release: a finger's press takes the
    /// nearest control within reach.
    ///
    /// @param kind pointer_move, pointer_down or pointer_up
    /// @param button the pointer's button
    /// @param at the pointer, in the dialog's pixels
    /// @param finger_reach a finger's reach in the dialog's pixels; 0 for a mouse
    /// @return what the dialog did
    LayerAnswer
    pointer(ScreenInputKind kind, uint8_t button, kit::Point at, int32_t finger_reach) override {
        auto* dialog = runtime_.engine_settings_dialog();
        if (dialog == nullptr)
            return LayerAnswer::close;
        auto action = settings::DialogAction::none;
        switch (kind) {
        case ScreenInputKind::pointer_move:
            action = settings::dialog_pointer_move(*dialog, at.x, at.y);
            break;
        case ScreenInputKind::pointer_down:
            if (button == SDL_BUTTON_LEFT)
                action = finger_reach > 0
                             ? settings::dialog_finger_down(*dialog, at.x, at.y, finger_reach)
                             : settings::dialog_pointer_down(*dialog, at.x, at.y);
            break;
        case ScreenInputKind::pointer_up:
            if (button == SDL_BUTTON_LEFT)
                action = settings::dialog_pointer_up(*dialog, at.x, at.y);
            break;
        default:
            break;
        }
        return take_action(action);
    }

    /// Takes a key's press.
    ///
    /// @param pressed the key
    /// @return what the dialog did
    LayerAnswer key(kit::Key pressed, uint32_t /*sdl_key*/) override {
        auto* dialog = runtime_.engine_settings_dialog();
        if (dialog == nullptr)
            return LayerAnswer::close;
        return take_action(settings::dialog_key(*dialog, pressed));
    }

    /// Takes a turn of the wheel, which scrolls the open section; nothing
    /// under the dialog sees it.
    ///
    /// @param at the pointer, in the dialog's pixels
    /// @param notches how far the wheel turned
    /// @return what the dialog did
    LayerAnswer wheel(kit::Point at, float notches) override {
        auto* dialog = runtime_.engine_settings_dialog();
        if (dialog == nullptr)
            return LayerAnswer::close;
        return take_action(settings::dialog_wheel(*dialog, at.x, at.y, notches));
    }

    /// Returns the dialog's display list.
    ///
    /// @return the list; empty once the dialog closed
    [[nodiscard]] kit::DisplayList display_list() const override {
        const auto* dialog = runtime_.engine_settings_dialog();
        if (dialog == nullptr)
            return {};
        return settings::geometry::dialog_list(*dialog, runtime_.engine_settings_fonts());
    }

    /// Returns the dialog's hover, press and focus.
    ///
    /// @return the interaction; none once the dialog closed
    [[nodiscard]] kit::Interaction interaction() const override {
        const auto* dialog = runtime_.engine_settings_dialog();
        if (dialog == nullptr)
            return {};
        kit::Interaction shown{};
        shown.hovered = dialog->hovered;
        shown.pressed = dialog->pressed;
        shown.focused = dialog->focused;
        shown.focus_shown = dialog->focused != settings::no_control;
        shown.finger_shift = {dialog->finger_shift_x, dialog->finger_shift_y};
        return shown;
    }

    /// Brings the dialog up to date: Hardware acceleration's status follows
    /// the renderer, Touch shows once a finger turns the touch controls on,
    /// and Controller once a gamepad sends input, with its Steam Input
    /// notice while that applies. Off the main menu the main menu's dialog
    /// closes as Cancel; once the in-game menu's column no longer shows under
    /// it, the match's closes as OK.
    ///
    /// @return close when the dialog closed
    LayerAnswer tick() override {
        auto* dialog = runtime_.engine_settings_dialog();
        if (dialog == nullptr)
            return LayerAnswer::close;
        if (in_match_) {
            if (runtime_.screen_ != Screen::match || !runtime_.ingame_menu_column_shown()) {
                // OK always closes the dialog; a failed save is reported
                // where it happens.
                ++revision_;
                std::ignore =
                    runtime_.take_engine_settings_action(settings::DialogAction::accepted);
                return LayerAnswer::close;
            }
        } else if (runtime_.screen_ != Screen::main_menu) {
            // Cancel always closes an open dialog.
            std::ignore = runtime_.take_engine_settings_action(settings::DialogAction::cancelled);
            return LayerAnswer::close;
        }
        bool changed =
            settings::set_acceleration_status(*dialog, runtime_.acceleration_report().status) ==
            settings::DialogAction::redraw;
        changed = settings::set_touch_controls(*dialog, runtime_.touch_controls_active()) ==
                      settings::DialogAction::redraw ||
                  changed;
        changed = settings::set_controller_section(
                      *dialog, runtime_.pad_used(), runtime_.pad_steam_input()
                  ) == settings::DialogAction::redraw ||
                  changed;
        if (!changed)
            return LayerAnswer::none;
        ++revision_;
        return LayerAnswer::redraw;
    }

    /// Returns the dialog's revision, which counts the actions it took.
    ///
    /// @return the revision
    [[nodiscard]] uint64_t revision() const override { return revision_; }

    /// Closes a dialog still open as Cancel closes it.
    void close(bool /*by_key*/) override {
        if (runtime_.engine_settings_dialog() != nullptr)
            std::ignore = runtime_.take_engine_settings_action(settings::DialogAction::cancelled);
    }

    /// Puts an action of the dialog in effect and plays its sound: OK and
    /// SWITCH play the opening sound, Cancel its own.
    ///
    /// @param action what the dialog's event asked
    /// @return close when the dialog closed; none when it asked nothing
    LayerAnswer take_action(settings::DialogAction action) {
        if (action == settings::DialogAction::accepted ||
            action == settings::DialogAction::switch_mod)
            runtime_.play_ui_sound(kOpenSound, 0);
        else if (action == settings::DialogAction::cancelled)
            runtime_.play_ui_sound(kCancelSound, 0);
        if (action == settings::DialogAction::none)
            return LayerAnswer::none;
        ++revision_;
        return runtime_.take_engine_settings_action(action) ? LayerAnswer::close
                                                            : LayerAnswer::redraw;
    }

  private:

    Runtime& runtime_;    ///< the runtime whose dialog it shows
    bool in_match_{};     ///< opened in a match; otherwise on the main menu
    uint64_t revision_{}; ///< counts the actions the dialog took and the changes its tick made
};

void Runtime::push_settings_screen(bool in_match) {
    oa_layer().push(std::make_unique<SettingsScreen>(*this, in_match));
}

void Runtime::take_settings_screen_action(settings::DialogAction action) {
    auto& layer = oa_layer();
    auto* screen = dynamic_cast<SettingsScreen*>(layer.find(kSettingsName));
    if (screen == nullptr) {
        std::ignore = take_engine_settings_action(action);
        return;
    }
    layer.take_answer(*screen, screen->take_action(action), 0);
}

// ---------------------------------------------------------------------------------------------
// A notice as a screen of the layer

NoticeScreen::NoticeScreen(OaLayer& layer, Screen shown_over, kit::Notice told, Host answers)
    : layer_(layer), over_(shown_over), notice_(std::move(told)), host_(std::move(answers)) {
}

int32_t NoticeScreen::shown_height() const {
    return settings::notice_height(notice_, layer_.screen_fonts());
}

LayerPlacement NoticeScreen::placement(const LayerView& view) const {
    if (view.match || view.game_screen != over_ || layer_.screen_fonts() == nullptr)
        return {};
    // As tall as its text makes it at the view's class.
    kit::Notice sized = notice_;
    sized.size_class = front_class(view);
    return centred_box(view, settings::notice_height(sized, layer_.screen_fonts()));
}

void NoticeScreen::lay_out(const LayerView& view) {
    const kit::SizeClass size_class = front_class(view);
    if (notice_.size_class == size_class)
        return;
    notice_.size_class = size_class;
    ++revision_;
}

void NoticeScreen::draw(const kit::Canvas& canvas) const {
    if (canvas.surface == nullptr || canvas.fonts == nullptr)
        return;
    settings::draw_notice(*canvas.surface, canvas.placement, notice_, *canvas.fonts, canvas.icon);
}

LayerAnswer
NoticeScreen::pointer(ScreenInputKind kind, uint8_t button, kit::Point at, int32_t finger_reach) {
    const int32_t tall = shown_height();
    auto action = kit::NoticeAction::none;
    switch (kind) {
    case ScreenInputKind::pointer_move:
        action = kit::notice_pointer_move(notice_, at, tall);
        break;
    case ScreenInputKind::pointer_down:
        // A finger's press takes the nearer button within reach.
        if (button == SDL_BUTTON_LEFT)
            action = finger_reach > 0 ? kit::notice_finger_down(notice_, at, tall, finger_reach)
                                      : kit::notice_pointer_down(notice_, at, tall);
        break;
    case ScreenInputKind::pointer_up:
        if (button == SDL_BUTTON_LEFT)
            action = kit::notice_pointer_up(notice_, at, tall);
        break;
    default:
        break;
    }
    return take(action);
}

LayerAnswer NoticeScreen::key(kit::Key pressed, uint32_t /*sdl_key*/) {
    return take(kit::notice_key(notice_, pressed));
}

kit::DisplayList NoticeScreen::display_list() const {
    return kit::notice_list(notice_, layer_.screen_fonts(), oa::data::languages::interface_text);
}

kit::Interaction NoticeScreen::interaction() const {
    kit::Interaction shown{};
    shown.hovered = notice_.hovered;
    shown.pressed = notice_.pressed;
    shown.focused = notice_.marked;
    shown.focus_shown = notice_.marked != kit::no_control;
    shown.finger_shift = {notice_.finger_shift_x, notice_.finger_shift_y};
    return shown;
}

LayerAnswer NoticeScreen::tick() {
    return host_.tick && host_.tick() ? LayerAnswer::close : LayerAnswer::none;
}

LayerAnswer NoticeScreen::take(kit::NoticeAction action) {
    switch (action) {
    case kit::NoticeAction::open_folder:
        // The notice stays, saying why the folder could not be shown.
        notice_.failure = host_.open ? host_.open() : std::string();
        ++revision_;
        return LayerAnswer::redraw;
    case kit::NoticeAction::closed:
        if (host_.ok)
            host_.ok();
        ++revision_;
        return LayerAnswer::close;
    case kit::NoticeAction::redraw:
        ++revision_;
        return LayerAnswer::redraw;
    case kit::NoticeAction::none:
        break;
    }
    return LayerAnswer::none;
}

// ---------------------------------------------------------------------------------------------
// A question as a screen of the layer

QuestionScreen::QuestionScreen(OaLayer& layer, Screen shown_over, kit::Question asked, Host answers)
    : layer_(layer), over_(shown_over), question_(std::move(asked)), host_(std::move(answers)) {
}

int32_t QuestionScreen::shown_height() const {
    return settings::prompt_height(question_, layer_.screen_fonts());
}

LayerPlacement QuestionScreen::placement(const LayerView& view) const {
    if (view.match || view.game_screen != over_ || layer_.screen_fonts() == nullptr)
        return {};
    // As tall as its text makes it at the view's class.
    kit::Question sized = question_;
    sized.size_class = front_class(view);
    return centred_box(view, settings::prompt_height(sized, layer_.screen_fonts()));
}

void QuestionScreen::lay_out(const LayerView& view) {
    size_class_ = front_class(view);
    if (question_.size_class == size_class_)
        return;
    question_.size_class = size_class_;
    ++revision_;
}

void QuestionScreen::draw(const kit::Canvas& canvas) const {
    if (canvas.surface == nullptr || canvas.fonts == nullptr)
        return;
    settings::draw_prompt(*canvas.surface, canvas.placement, question_, *canvas.fonts, canvas.icon);
}

LayerAnswer
QuestionScreen::pointer(ScreenInputKind kind, uint8_t button, kit::Point at, int32_t finger_reach) {
    const int32_t tall = shown_height();
    kit::QuestionAnswer outcome{};
    switch (kind) {
    case ScreenInputKind::pointer_move:
        outcome = kit::question_pointer_move(question_, at, tall);
        break;
    case ScreenInputKind::pointer_down:
        // A finger's press takes the nearest button within reach.
        if (button == SDL_BUTTON_LEFT)
            outcome = finger_reach > 0
                          ? kit::question_finger_down(question_, at, tall, finger_reach)
                          : kit::question_pointer_down(question_, at, tall);
        break;
    case ScreenInputKind::pointer_up:
        if (button == SDL_BUTTON_LEFT)
            outcome = kit::question_pointer_up(question_, at, tall);
        break;
    default:
        break;
    }
    return take(outcome, 0);
}

LayerAnswer QuestionScreen::key(kit::Key pressed, uint32_t sdl_key) {
    return take(kit::question_key(question_, pressed), sdl_key);
}

kit::DisplayList QuestionScreen::display_list() const {
    return kit::question_list(question_, layer_.screen_fonts());
}

kit::Interaction QuestionScreen::interaction() const {
    kit::Interaction shown{};
    shown.hovered = question_.hovered;
    shown.pressed = question_.pressed;
    shown.focused = question_.marked;
    shown.focus_shown = question_.marked != kit::no_control;
    shown.finger_shift = {question_.finger_shift_x, question_.finger_shift_y};
    return shown;
}

LayerAnswer QuestionScreen::tick() {
    return host_.tick && host_.tick() ? LayerAnswer::close : LayerAnswer::none;
}

void QuestionScreen::close(bool /*by_key*/) {
    if (host_.closed)
        host_.closed(*this);
}

LayerAnswer QuestionScreen::take(kit::QuestionAnswer outcome, uint32_t sdl_key) {
    switch (outcome.action) {
    case kit::QuestionAction::answered: {
        // The key that answered does nothing more until it is released,
        // whether the question closes or stays with what its host shows next.
        if (sdl_key != 0)
            layer_.latch_key(sdl_key);
        ++revision_;
        const bool last = !host_.answer || host_.answer(outcome.button);
        return last ? LayerAnswer::close : LayerAnswer::redraw;
    }
    case kit::QuestionAction::redraw:
        ++revision_;
        return LayerAnswer::redraw;
    case kit::QuestionAction::none:
        break;
    }
    return LayerAnswer::none;
}

// ---------------------------------------------------------------------------------------------
// The layer

kit::Point layer_point(const LayerPlacement& placement, float x, float y) noexcept {
    const auto& shown = placement.shown;
    const auto map = [](float offset, int32_t length, int32_t points) {
        if (length <= 0)
            return 0;
        return static_cast<int32_t>(
            std::floor(static_cast<double>(offset) * static_cast<double>(points) / length)
        );
    };
    return {
        map(x - static_cast<float>(shown.x), shown.width, placement.points_width),
        map(y - static_cast<float>(shown.y), shown.height, placement.points_height)
    };
}

LayerPlacement
centred_placement(const LayerView& view, int32_t width_points, int32_t height_points) noexcept {
    const int32_t scale = std::max(view.scale, 1);
    const int32_t width = width_points * scale;
    const int32_t height = height_points * scale;
    return {
        {view.room.x + floor_div(view.room.width - width, 2),
         view.room.y + floor_div(view.room.height - height, 2),
         width,
         height},
        width_points,
        height_points
    };
}

WindowPosition window_position(const LayerView& view, float x, float y) noexcept {
    if (view.match || view.picture_size.x <= 0 || view.picture_size.y <= 0)
        return {static_cast<double>(x), static_cast<double>(y)};
    // The picture's pixels as the window draws them; a pixel's edge mapped
    // back from the picture lands on it to the nearest 64th.
    const auto along = [](float at, int32_t start, int32_t shown, int32_t size) {
        const double exact = static_cast<double>(start) + static_cast<double>(at) *
                                                              static_cast<double>(shown) /
                                                              static_cast<double>(size);
        return std::round(exact * 64.0) / 64.0;
    };
    return {
        along(x, view.picture.x, view.picture.width, view.picture_size.x),
        along(y, view.picture.y, view.picture.height, view.picture_size.y)
    };
}

std::optional<kit::Key> layer_key(uint32_t sdl_key, uint16_t modifiers) noexcept {
    switch (sdl_key) {
    case SDLK_RETURN:
        return kit::Key::enter;
    case SDLK_ESCAPE:
        return kit::Key::escape;
    case SDLK_UP:
        return kit::Key::up;
    case SDLK_DOWN:
        return kit::Key::down;
    case SDLK_LEFT:
        return kit::Key::left;
    case SDLK_RIGHT:
        return kit::Key::right;
    case SDLK_SPACE:
        return kit::Key::space;
    case SDLK_TAB:
        return (modifiers & SDL_KMOD_SHIFT) != 0 ? kit::Key::back_tab : kit::Key::tab;
    case SDLK_PAGEUP:
        return kit::Key::page_up;
    case SDLK_PAGEDOWN:
        return kit::Key::page_down;
    case SDLK_HOME:
        return kit::Key::home;
    case SDLK_END:
        return kit::Key::end;
    case SDLK_Y:
        return kit::Key::yes;
    case SDLK_N:
        return kit::Key::no;
    case SDLK_BACKSPACE:
        return kit::Key::backspace;
    case SDLK_DELETE:
        return kit::Key::delete_forward;
    default:
        return std::nullopt;
    }
}

void Runtime::destroy_oa_layer(OaLayer* layer) noexcept {
    delete layer;
}

OaLayer& Runtime::oa_layer() {
    if (!oa_layer_)
        oa_layer_.reset(new OaLayer(*this));
    return *oa_layer_;
}

void Runtime::register_oa_layer() {
    OverlayDesc layer{};
    layer.name = "oa_layer";
    // On every screen: a screen closes in its tick when the screen it was
    // opened on goes, and the key that closed one is latched wherever it is
    // released.
    layer.screen = kScreenAny;
    layer.z = kLayerOverlayZ;
    layer.event = OaLayer::overlay_event;
    layer.tick = OaLayer::overlay_tick;
    layer.draw = OaLayer::overlay_draw;
    // A refused overlay is recorded in the registry, and register_screens
    // reports it once every screen and overlay is in.
    overlay_register(&screens_, &layer);
}

OaLayer::OaLayer(Runtime& runtime) noexcept : runtime_(runtime) {
}

OaLayer::~OaLayer() = default;

int OaLayer::overlay_event(ScreenContext* context, void*) {
    if (context == nullptr || context->input == nullptr || context->host == nullptr)
        return 0;
    auto& runtime = *static_cast<Runtime*>(context->host);
    return runtime.oa_layer().take_input(*context->input) ? 1 : 0;
}

void OaLayer::overlay_tick(ScreenContext* context, void*) {
    if (context == nullptr || context->host == nullptr)
        return;
    static_cast<Runtime*>(context->host)->oa_layer().tick();
}

void OaLayer::overlay_draw(ScreenContext* context, void*) {
    if (context == nullptr || context->host == nullptr || context->surface == nullptr)
        return;
    auto& runtime = *static_cast<Runtime*>(context->host);
    // A match draws its layer over its composed frame and its other layers.
    if (runtime.screen_ == Screen::match)
        return;
    auto& layer = runtime.oa_layer();
    layer.lay_out_screens();
    layer.darken_front_end(*context->surface);
    // With a renderer the screens go into the window over the picture
    // (present); the frame holds them without one, and for the picture
    // frame_without_cursor returns.
    if (runtime.sdl_.renderer == nullptr || runtime.frame_without_cursor_)
        layer.compose_front_end(*context->surface);
}

void OaLayer::push(std::unique_ptr<LayerScreen> screen) {
    if (!screen)
        return;
    screen->lay_out(view());
    screens_.push_back(std::move(screen));
    button_hovered_ = false;
    button_pressed_ = false;
    ++revision_;
    sync_text_input();
}

void OaLayer::show_when_free(std::unique_ptr<LayerScreen> screen) {
    if (!screen)
        return;
    queue_.push_back(std::move(screen));
    show_waiting();
}

void OaLayer::close(const LayerScreen* screen) {
    if (screen == nullptr)
        return;
    // A screen that is taking an input or its tick leaves once that is done.
    if (screen == calling_) {
        close_asked_ = true;
        return;
    }
    if (holds(screen))
        remove(screen, false);
    else
        drop_waiting(screen);
    show_waiting();
}

void OaLayer::close_top() {
    if (!screens_.empty())
        remove(screens_.back().get(), false);
}

void OaLayer::close_above(std::string_view name) {
    for (;;) {
        const LayerScreen* named = find(name);
        if (named == nullptr || screens_.back().get() == named)
            return;
        remove(screens_.back().get(), false);
    }
}

LayerScreen* OaLayer::top() const noexcept {
    return screens_.empty() ? nullptr : screens_.back().get();
}

LayerScreen* OaLayer::find(std::string_view name) const noexcept {
    for (auto it = screens_.rbegin(); it != screens_.rend(); ++it)
        if ((*it)->name() == name)
            return it->get();
    return nullptr;
}

LayerScreen* OaLayer::waiting(std::string_view name) const noexcept {
    for (const auto& screen : queue_)
        if (screen->name() == name)
            return screen.get();
    return nullptr;
}

const kit::Fonts* OaLayer::screen_fonts() const {
    return runtime_.engine_settings_fonts();
}

LayerView OaLayer::view() const {
    LayerView seen{};
    seen.match = runtime_.screen_ == Screen::match;
    seen.game_screen = runtime_.screen_;
    if (seen.match) {
        // The in-match Settings keeps its own placement, at Compact.
        seen.match_layout = &runtime_.match_layout_;
        seen.fit_safe_area = match_fits_safe_area(runtime_);
        const auto safe = match_safe_area(runtime_.match_layout_);
        seen.frame.area = {safe.x, safe.y, safe.width, safe.height};
        seen.frame.size_class = kit::SizeClass::compact;
        seen.frame.scale_percent = 100;
        seen.room = safe;
        seen.picture = {0, 0, runtime_.match_layout_.width, runtime_.match_layout_.height};
        seen.picture_size = {runtime_.match_layout_.width, runtime_.match_layout_.height};
        return seen;
    }
    // The front end's picture, which without a renderer is the canvas too.
    const auto picture_width = static_cast<int32_t>(runtime_.surface_.width);
    const auto picture_height = static_cast<int32_t>(runtime_.surface_.height);
    seen.picture_size = {picture_width, picture_height};
    seen.picture = {0, 0, picture_width, picture_height};
    kit::Viewport viewport;
    viewport.width = picture_width;
    viewport.height = picture_height;
    SDL_Renderer* sdl_renderer = runtime_.sdl_.renderer;
    SDL_Window* window = runtime_.sdl_.window;
    int output_width = 0;
    int output_height = 0;
    if (sdl_renderer != nullptr &&
        SDL_GetRenderOutputSize(sdl_renderer, &output_width, &output_height) && output_width > 0 &&
        output_height > 0) {
        // The canvas in pixels and the window points it holds, as the Game
        // files screen fills its viewport.
        viewport.width = output_width;
        viewport.height = output_height;
        int window_width = 0;
        int window_height = 0;
        if (window != nullptr && SDL_GetWindowSize(window, &window_width, &window_height) &&
            window_width > 0)
            viewport.density = static_cast<float>(output_width) / static_cast<float>(window_width);
        // The window's safe area, kept clear while the game has touch controls.
        SDL_Rect safe{};
        if (window != nullptr && runtime_.touch_controls_active() &&
            SDL_GetWindowSafeArea(window, &safe) && safe.w > 0 && safe.h > 0) {
            const auto pixels = [&viewport](int points) {
                return static_cast<int32_t>(
                    std::lround(static_cast<float>(points) * viewport.density)
                );
            };
            viewport.safe = {
                pixels(safe.x),
                pixels(safe.y),
                pixels(window_width - safe.x - safe.w),
                pixels(window_height - safe.y - safe.h)
            };
        }
        // Where the picture is drawn, from the whole pixel SDL starts it at.
        int logical_width = 0;
        int logical_height = 0;
        SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
        SDL_FRect reckoned{};
        if (SDL_GetRenderLogicalPresentation(
                sdl_renderer, &logical_width, &logical_height, &mode
            ) &&
            mode != SDL_LOGICAL_PRESENTATION_DISABLED && logical_width > 0 && logical_height > 0 &&
            SDL_GetRenderLogicalPresentationRect(sdl_renderer, &reckoned) && reckoned.w > 0.0F &&
            reckoned.h > 0.0F) {
            const SDL_Rect drawn = drawn_frame_rect(reckoned);
            seen.picture = {drawn.x, drawn.y, drawn.w, drawn.h};
            seen.picture_size = {logical_width, logical_height};
        } else {
            seen.picture = {0, 0, output_width, output_height};
        }
    }
    seen.room = {
        viewport.safe.left,
        viewport.safe.top,
        viewport.width - viewport.safe.left - viewport.safe.right,
        viewport.height - viewport.safe.top - viewport.safe.bottom
    };
    // The Auto scale, no larger than lets Compact's dialog fit the room, and
    // at least 1; the class from the points left over.
    const kit::Metrics& compact = kit::compact_metrics;
    const int32_t fits =
        std::min(seen.room.width / compact.dialog_width, seen.room.height / compact.dialog_height);
    seen.scale =
        std::max(int32_t{1}, std::min(kit::auto_scale(viewport.height, viewport.density), fits));
    viewport.scale_percent = seen.scale * 100;
    seen.frame = kit::frame_of(viewport);
    return seen;
}

LayerPlacement OaLayer::placement_of(std::string_view name) const {
    const LayerScreen* named = find(name);
    if (named == nullptr)
        return {};
    return named->placement(view());
}

void OaLayer::lay_out_screens() {
    const LayerView seen = view();
    for (const auto& screen : screens_)
        screen->lay_out(seen);
    for (const auto& screen : queue_)
        screen->lay_out(seen);
}

bool OaLayer::shows(bool match) const {
    const LayerView seen = view();
    if (seen.match != match)
        return false;
    return std::any_of(screens_.begin(), screens_.end(), [&seen](const auto& screen) {
        return !empty_rect(screen->placement(seen).shown);
    });
}

bool OaLayer::modal_shown(bool match) const {
    const LayerView seen = view();
    return seen.match == match && top_modal(seen) != nullptr;
}

LayerScreen* OaLayer::top_modal(const LayerView& seen) const {
    for (auto it = screens_.rbegin(); it != screens_.rend(); ++it)
        if ((*it)->modal() && !empty_rect((*it)->placement(seen).shown))
            return it->get();
    return nullptr;
}

bool OaLayer::holds(const LayerScreen* screen) const noexcept {
    return std::any_of(screens_.begin(), screens_.end(), [screen](const auto& held) {
        return held.get() == screen;
    });
}

bool OaLayer::queued(const LayerScreen* screen) const noexcept {
    return std::any_of(queue_.begin(), queue_.end(), [screen](const auto& waiting_screen) {
        return waiting_screen.get() == screen;
    });
}

std::vector<uint32_t> OaLayer::backdrops_above(const LayerView& seen) const {
    std::vector<uint32_t> above(screens_.size(), 0U);
    uint32_t darkening = 0;
    for (std::size_t index = screens_.size(); index > 0; --index) {
        above[index - 1] = darkening;
        const auto& screen = *screens_[index - 1];
        if (screen.modal() && screen.backdrop() && !empty_rect(screen.placement(seen).shown))
            ++darkening;
    }
    return above;
}

bool OaLayer::take_input(const ScreenInput& input) {
    // The key that closed a screen does nothing more until it is released,
    // so that a held key never reaches what lies under the layer.
    if (latched_key_ != 0 && input.key == latched_key_ &&
        (input.kind == ScreenInputKind::key_down || input.kind == ScreenInputKind::key_up)) {
        if (input.kind == ScreenInputKind::key_up)
            latched_key_ = 0;
        return true;
    }
    lay_out_screens();
    const LayerView seen = view();
    const uint32_t key_down = input.kind == ScreenInputKind::key_down ? input.key : 0U;
    bool taken = false;
    if (LayerScreen* modal = top_modal(seen)) {
        // A modal screen takes every input: nothing under it sees any.
        take_answer(*modal, deliver(*modal, seen, input), key_down);
        taken = true;
    } else {
        // Each screen that shows sees it from the top; the first that answers
        // other than pass takes it. A move that only changes a screen's own
        // hover is answered pass, and goes on.
        std::vector<LayerScreen*> shown_screens;
        for (auto it = screens_.rbegin(); it != screens_.rend(); ++it)
            if (!empty_rect((*it)->placement(seen).shown))
                shown_screens.push_back(it->get());
        for (LayerScreen* screen : shown_screens) {
            if (!holds(screen))
                continue;
            const LayerAnswer answer = deliver(*screen, seen, input);
            if (answer == LayerAnswer::pass)
                continue;
            take_answer(*screen, answer, key_down);
            taken = true;
            break;
        }
        if (!taken && seen.match)
            taken = take_match_button(input);
    }
    // A screen that closed may free the layer for the next one waiting.
    show_waiting();
    sync_text_input();
    return taken;
}

LayerAnswer OaLayer::deliver(LayerScreen& screen, const LayerView& seen, const ScreenInput& input) {
    calling_ = &screen;
    close_asked_ = false;
    const LayerAnswer answer = hand_input(screen, seen, input);
    calling_ = nullptr;
    // A screen whose host closed it while it took the input leaves now.
    return std::exchange(close_asked_, false) ? LayerAnswer::close : answer;
}

LayerAnswer
OaLayer::hand_input(LayerScreen& screen, const LayerView& seen, const ScreenInput& input) {
    const LayerPlacement placed = screen.placement(seen);
    // The pointer in the window's pixels, mapped to the screen's points by
    // its placement.
    const WindowPosition at = window_position(seen, input.x, input.y);
    const kit::Point point =
        layer_point(placed, static_cast<float>(at.x), static_cast<float>(at.y));
    switch (input.kind) {
    case ScreenInputKind::pointer_move:
    case ScreenInputKind::pointer_down:
    case ScreenInputKind::pointer_up: {
        // A finger's reach, in the screen's points as it shows: the pick
        // distance in the canvas the touch controls measure it in (the
        // picture's pixels on the front end), over the canvas pixels a
        // point takes.
        int32_t reach = 0;
        if (runtime_.engine_settings_state().finger_pointer && placed.points_width > 0 &&
            seen.picture.width > 0) {
            const double window_per_point =
                static_cast<double>(placed.shown.width) / static_cast<double>(placed.points_width);
            const double canvas_per_window =
                static_cast<double>(seen.picture_size.x) / static_cast<double>(seen.picture.width);
            reach = Runtime::EngineSettingsState::finger_reach(
                runtime_, window_per_point * canvas_per_window
            );
        }
        return screen.pointer(input.kind, input.button, point, reach);
    }
    case ScreenInputKind::wheel:
        return screen.wheel(point, input.wheel_y);
    case ScreenInputKind::key_down:
        if (const auto pressed = layer_key(input.key, input.modifiers))
            return screen.key(*pressed, input.key);
        return LayerAnswer::pass;
    case ScreenInputKind::text:
        return screen.text(
            input.text != nullptr ? std::string_view(input.text) : std::string_view()
        );
    case ScreenInputKind::key_up:
        return LayerAnswer::pass;
    }
    return LayerAnswer::pass;
}

void OaLayer::take_answer(LayerScreen& screen, LayerAnswer answer, uint32_t key_down) {
    if (answer != LayerAnswer::close || !holds(&screen))
        return;
    // The key that closed the screen does nothing more until it is released.
    if (key_down != 0)
        latched_key_ = key_down;
    remove(&screen, key_down != 0);
    show_waiting();
}

void OaLayer::remove(const LayerScreen* screen, bool by_key) {
    const auto it = std::find_if(screens_.begin(), screens_.end(), [screen](const auto& held) {
        return held.get() == screen;
    });
    if (it == screens_.end())
        return;
    std::unique_ptr<LayerScreen> leaving = std::move(*it);
    screens_.erase(it);
    button_hovered_ = false;
    button_pressed_ = false;
    ++revision_;
    leaving->close(by_key);
    sync_text_input();
}

void OaLayer::drop_waiting(const LayerScreen* screen) {
    const auto it = std::find_if(queue_.begin(), queue_.end(), [screen](const auto& held) {
        return held.get() == screen;
    });
    if (it == queue_.end())
        return;
    std::unique_ptr<LayerScreen> leaving = std::move(*it);
    queue_.erase(it);
    leaving->close(false);
}

void OaLayer::show_waiting() {
    // One at a time: the first waiting is pushed while no modal screen
    // shows, and the next waits while it shows.
    while (!queue_.empty() && top_modal(view()) == nullptr) {
        std::unique_ptr<LayerScreen> next = std::move(queue_.front());
        queue_.erase(queue_.begin());
        push(std::move(next));
    }
}

void OaLayer::tick() {
    // The window's size, and with it the screens' class, may have changed.
    lay_out_screens();
    // A tick may close a screen, or open another. The screens waiting are
    // ticked too, so that one whose screen of the game goes leaves the queue.
    std::vector<LayerScreen*> ticking;
    ticking.reserve(screens_.size() + queue_.size());
    for (const auto& screen : screens_)
        ticking.push_back(screen.get());
    for (const auto& screen : queue_)
        ticking.push_back(screen.get());
    for (LayerScreen* screen : ticking) {
        const bool on_stack = holds(screen);
        if (!on_stack && !queued(screen))
            continue;
        calling_ = screen;
        close_asked_ = false;
        const LayerAnswer answer = screen->tick();
        calling_ = nullptr;
        if (answer != LayerAnswer::close && !std::exchange(close_asked_, false))
            continue;
        if (holds(screen))
            remove(screen, false);
        else
            drop_waiting(screen);
    }
    show_waiting();
    sync_text_input();
}

bool OaLayer::take_match_button(const ScreenInput& input) {
    if (input.kind == ScreenInputKind::key_down &&
        Runtime::engine_settings_shortcut(input.key, input.modifiers)) {
        runtime_.open_engine_settings_in_match();
        return true;
    }
    // Ctrl+F2 opens the mod options while the profile offers them.
    if (input.kind == ScreenInputKind::key_down && input.key == SDLK_F2 &&
        (input.modifiers & SDL_KMOD_CTRL) != 0 && runtime_.ui_rules().options_dialog.enabled) {
        runtime_.open_engine_settings_in_match(settings::DialogKind::mod_options);
        return true;
    }
    const bool pointer = input.kind == ScreenInputKind::pointer_move ||
                         input.kind == ScreenInputKind::pointer_down ||
                         input.kind == ScreenInputKind::pointer_up;
    if (!pointer)
        return false;
    if (!match_button_shown(runtime_)) {
        if (button_hovered_ || button_pressed_)
            ++revision_;
        button_hovered_ = false;
        button_pressed_ = false;
        return false;
    }
    const bool over = rect_contains(
        match_button_rect(runtime_.match_layout_, match_fits_safe_area(runtime_)), input.x, input.y
    );
    if (over != button_hovered_)
        ++revision_;
    button_hovered_ = over;
    if (input.kind == ScreenInputKind::pointer_down && input.button == SDL_BUTTON_LEFT && over) {
        button_pressed_ = true;
        ++revision_;
        return true;
    }
    if (input.kind == ScreenInputKind::pointer_up && input.button == SDL_BUTTON_LEFT &&
        button_pressed_) {
        button_pressed_ = false;
        ++revision_;
        if (over)
            runtime_.open_engine_settings_in_match();
        return true;
    }
    return false;
}

void OaLayer::sync_text_input() {
    const LayerView seen = view();
    // The field of a screen, in the coordinates input arrives in: through
    // its placement into the window, and on the front end through the
    // picture's rectangle into the picture.
    const auto field_of = [&seen](const LayerScreen& screen) -> std::optional<layout::Rect> {
        const auto field = screen.text_field();
        if (!field)
            return std::nullopt;
        const LayerPlacement placed = screen.placement(seen);
        if (empty_rect(placed.shown) || placed.points_width <= 0 || placed.points_height <= 0 ||
            empty_rect(seen.picture))
            return std::nullopt;
        const auto start =
            [](int32_t shown_start, int32_t shown_length, int32_t points, int32_t at) {
                return shown_start +
                       static_cast<int32_t>(static_cast<int64_t>(at) * shown_length / points);
            };
        const auto in_x = [&seen](int32_t window) {
            return static_cast<int32_t>(
                static_cast<int64_t>(window - seen.picture.x) * seen.picture_size.x /
                seen.picture.width
            );
        };
        const auto in_y = [&seen](int32_t window) {
            return static_cast<int32_t>(
                static_cast<int64_t>(window - seen.picture.y) * seen.picture_size.y /
                seen.picture.height
            );
        };
        const int32_t left =
            in_x(start(placed.shown.x, placed.shown.width, placed.points_width, field->x));
        const int32_t first_row =
            in_y(start(placed.shown.y, placed.shown.height, placed.points_height, field->y));
        const int32_t right = in_x(
            start(placed.shown.x, placed.shown.width, placed.points_width, field->x + field->width)
        );
        const int32_t bottom = in_y(start(
            placed.shown.y, placed.shown.height, placed.points_height, field->y + field->height
        ));
        return layout::Rect{left, first_row, right - left, bottom - first_row};
    };
    std::optional<layout::Rect> wanted;
    if (const LayerScreen* modal = top_modal(seen))
        wanted = field_of(*modal);
    else
        for (auto it = screens_.rbegin(); it != screens_.rend() && !wanted; ++it)
            if (!empty_rect((*it)->placement(seen).shown))
                wanted = field_of(**it);
    if (wanted) {
        if (!text_field_ || !same_rect(*text_field_, *wanted)) {
            runtime_.start_text_input(*wanted);
            text_field_ = wanted;
        }
        return;
    }
    // Only the text input the layer started stops with its field.
    if (text_field_) {
        runtime_.stop_text_input();
        text_field_.reset();
    }
}

renderer::Surface
OaLayer::draw_screen(const LayerScreen& screen, const LayerPlacement& placed, int32_t scale) const {
    // At the scale itself: never drawn at 1× and enlarged.
    const int32_t whole = std::max(scale, int32_t{1});
    renderer::Surface drawn;
    drawn.width = static_cast<uint32_t>(std::max(placed.points_width, 0) * whole);
    drawn.height = static_cast<uint32_t>(std::max(placed.points_height, 0) * whole);
    drawn.rgb.assign(static_cast<std::size_t>(drawn.width) * drawn.height * 3U, 0);
    if (drawn.rgb.empty())
        return drawn;
    kit::Canvas canvas{};
    canvas.surface = &drawn;
    canvas.placement = {0, 0, whole};
    canvas.fonts = runtime_.engine_settings_fonts();
    canvas.icon = runtime_.engine_settings_icon();
    screen.draw(canvas);
    return drawn;
}

void OaLayer::darken_front_end(renderer::Surface& frame) const {
    const LayerView seen = view();
    if (seen.match)
        return;
    const auto darkenings =
        std::count_if(screens_.begin(), screens_.end(), [&seen](const auto& screen) {
            return screen->modal() && screen->backdrop() &&
                   !empty_rect(screen->placement(seen).shown);
        });
    // A blend of the frame itself, in 256ths, for each: the same picture the
    // window shows at every size.
    darken(frame, static_cast<uint32_t>(darkenings), kit::menu_backdrop_opacity);
}

void OaLayer::compose_front_end(renderer::Surface& frame) const {
    const LayerView seen = view();
    if (seen.match || empty_rect(seen.picture) || seen.picture_size.x <= 0 ||
        seen.picture_size.y <= 0)
        return;
    const std::vector<uint32_t> above = backdrops_above(seen);
    for (std::size_t index = 0; index < screens_.size(); ++index) {
        const auto& screen = *screens_[index];
        const LayerPlacement placed = screen.placement(seen);
        if (empty_rect(placed.shown))
            continue;
        // A screen under a modal screen with a backdrop darkens with what
        // lies under it.
        renderer::Surface drawn = draw_screen(screen, placed, seen.scale);
        darken(drawn, above[index], kit::menu_backdrop_opacity);
        stamp_onto_picture(frame, drawn, placed.shown, seen);
    }
}

void OaLayer::present(const SDL_FRect* picture_area) {
    if (runtime_.sdl_.renderer == nullptr)
        return;
    // A match presented as one frame shows no layer, as its composed frame
    // is the picture of its layers.
    if (runtime_.screen_ == Screen::match) {
        if (picture_area == nullptr)
            present_match();
        return;
    }
    if (picture_area != nullptr)
        present_front_end(*picture_area);
}

void OaLayer::present_front_end(const SDL_FRect& area) {
    SDL_Renderer* sdl_renderer = runtime_.sdl_.renderer;
    lay_out_screens();
    const LayerView seen = view();
    if (seen.match || !(area.w > 0.0F) || !(area.h > 0.0F))
        return;
    int output_width = 0;
    int output_height = 0;
    if (!SDL_GetRenderOutputSize(sdl_renderer, &output_width, &output_height) ||
        output_width <= 0 || output_height <= 0)
        return;
    const layout::Rect output{0, 0, output_width, output_height};
    FrontLook look{};
    look.picture_width = seen.picture_size.x;
    look.picture_height = seen.picture_size.y;
    look.area = {area.x, area.y, area.w, area.h};
    layout::Rect bounds{};
    const std::vector<uint32_t> above = backdrops_above(seen);
    uint32_t darkenings = 0;
    for (std::size_t index = 0; index < screens_.size(); ++index) {
        const auto& screen = *screens_[index];
        const LayerPlacement placed = screen.placement(seen);
        if (empty_rect(placed.shown) || placed.points_width <= 0 || placed.points_height <= 0)
            continue;
        if (screen.modal() && screen.backdrop())
            ++darkenings;
        // The part of its place the window shows.
        const layout::Rect window = intersection(placed.shown, output);
        if (empty_rect(window))
            continue;
        FrontPiece piece{};
        piece.window = window;
        piece.shown = placed.shown;
        // Drawn at the view's scale, as large as its place.
        piece.drawn = draw_screen(screen, placed, seen.scale);
        darken(piece.drawn, above[index], kit::menu_backdrop_opacity);
        bounds = union_rect(bounds, piece.window);
        look.pieces.push_back(std::move(piece));
    }
    // In the window's own pixels, not through the picture's logical
    // presentation; everything is put back for the cursor and the next frame.
    const RenderState kept(sdl_renderer);
    kept.use_window_pixels();
    // Under a modal screen with a backdrop the whole window darkens: the
    // picture is darkened in the frame (darken_front_end), and the window
    // outside it, where SDL clears to black, takes black darkened as often.
    if (darkenings > 0) {
        renderer::Surface black;
        black.width = 1;
        black.height = 1;
        black.rgb.assign(3U, 0);
        darken(black, darkenings, kit::menu_backdrop_opacity);
        if (!runtime_.gamma_identity_)
            runtime_.apply_gamma_rgb(black.rgb.data(), 1U, 3);
        std::vector<SDL_FRect> bars;
        const auto bar = [&bars](int32_t x, int32_t y, int32_t width, int32_t height) {
            if (width > 0 && height > 0)
                bars.push_back(
                    {static_cast<float>(x),
                     static_cast<float>(y),
                     static_cast<float>(width),
                     static_cast<float>(height)}
                );
        };
        const layout::Rect& picture = seen.picture;
        bar(0, 0, output_width, picture.y);
        bar(
            0, picture.y + picture.height, output_width, output_height - picture.y - picture.height
        );
        bar(0, picture.y, picture.x, picture.height);
        bar(picture.x + picture.width,
            picture.y,
            output_width - picture.x - picture.width,
            picture.height);
        if (!bars.empty()) {
            SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(
                sdl_renderer, black.rgb[0], black.rgb[1], black.rgb[2], SDL_ALPHA_OPAQUE
            );
            SDL_RenderFillRects(sdl_renderer, bars.data(), static_cast<int>(bars.size()));
        }
    }
    if (look.pieces.empty())
        return;
    const auto limit = runtime_.render_texture_limit();
    const bool made = texture_.ensure(
        sdl_renderer,
        SDL_PIXELFORMAT_RGBA32,
        bounds.width,
        bounds.height,
        limit,
        SDL_BLENDMODE_BLEND
    );
    const auto same_look = [&look](const FrontLook& held) {
        if (held.picture_width != look.picture_width ||
            held.picture_height != look.picture_height || held.area != look.area ||
            held.pieces.size() != look.pieces.size())
            return false;
        for (std::size_t index = 0; index < held.pieces.size(); ++index) {
            const auto& was = held.pieces[index];
            const auto& now = look.pieces[index];
            if (!same_rect(was.window, now.window) || !same_rect(was.shown, now.shown) ||
                !same_picture(was.drawn, now.drawn))
                return false;
        }
        return true;
    };
    if (made || !front_uploaded_ || !same_look(*front_uploaded_) ||
        !same_rect(front_bounds_, bounds) || uploaded_gamma_ != runtime_.gamma_table_) {
        std::vector<uint8_t> rgba(
            static_cast<std::size_t>(bounds.width) * static_cast<std::size_t>(bounds.height) * 4U, 0
        );
        // Each piece's pixels as drawn, one window pixel each.
        for (const auto& piece : look.pieces) {
            const int32_t first_column = piece.window.x - piece.shown.x;
            for (int32_t row = 0; row < piece.window.height; ++row) {
                const auto source_row =
                    static_cast<std::size_t>(piece.window.y + row - piece.shown.y);
                const auto* from =
                    piece.drawn.rgb.data() +
                    (source_row * piece.drawn.width + static_cast<std::size_t>(first_column)) * 3U;
                auto* to =
                    rgba.data() + (static_cast<std::size_t>(piece.window.y + row - bounds.y) *
                                       static_cast<std::size_t>(bounds.width) +
                                   static_cast<std::size_t>(piece.window.x - bounds.x)) *
                                      4U;
                for (int32_t column = 0; column < piece.window.width; ++column) {
                    to[0] = from[0];
                    to[1] = from[1];
                    to[2] = from[2];
                    to[3] = 255U;
                    from += 3;
                    to += 4;
                }
            }
        }
        // At the display gamma, as the picture under it is uploaded.
        if (!runtime_.gamma_identity_)
            runtime_.apply_gamma_rgb(rgba.data(), rgba.size() / 4U, 4);
        texture_.update(rgba.data(), bounds.width * 4, 4);
        front_uploaded_ = std::move(look);
        front_bounds_ = bounds;
        uploaded_.reset();
        uploaded_gamma_ = runtime_.gamma_table_;
    }
    const SDL_FRect at{
        static_cast<float>(bounds.x),
        static_cast<float>(bounds.y),
        static_cast<float>(bounds.width),
        static_cast<float>(bounds.height)
    };
    texture_.draw(sdl_renderer, nullptr, &at);
}

bool OaLayer::match_button_shown(Runtime& runtime) {
    return runtime.ingame_menu_column_shown() && runtime.engine_settings_fonts() != nullptr;
}

bool OaLayer::match_fits_safe_area(const Runtime& runtime) {
    return runtime.touch_controls_active() && runtime.touch_phone_class();
}

layout::Rect OaLayer::match_safe_area(const layout::MatchLayout& match_layout) noexcept {
    const auto& safe = match_layout.safe;
    return {
        safe.left,
        safe.top,
        std::max(match_layout.width - safe.left - safe.right, 0),
        std::max(match_layout.height - safe.top - safe.bottom, 0)
    };
}

layout::Rect
OaLayer::match_button_rect(const layout::MatchLayout& match_layout, bool fit) noexcept {
    const layout::Rect source{
        match_button_source_x,
        match_button_source_y,
        settings::ingame_button_side,
        settings::ingame_button_side
    };
    const auto safe = match_safe_area(match_layout);
    if (fit && safe.width > 0 && safe.height > 0) {
        // Where a placed region shows that part of the side column, the
        // button stands on it.
        const std::size_t count =
            std::min<std::size_t>(match_layout.placed_count, layout::kMaxPlacedRegions);
        for (std::size_t index = count; index > 0; --index)
            if (source_holds(match_layout.placed[index - 1].source, source))
                return layout::source_rect_to_canvas(
                    match_layout, source.x, source.y, source.width, source.height
                );
        // Else the column as the 640x480 frame fitted to the safe area
        // places it, from the safe area's top left corner.
        const double scale = std::min(
            static_cast<double>(safe.width) / layout::kSourceWidth,
            static_cast<double>(safe.height) / layout::kSourceHeight
        );
        return {
            safe.x + static_cast<int>(std::lround(static_cast<double>(source.x) * scale)),
            safe.y + static_cast<int>(std::lround(static_cast<double>(source.y) * scale)),
            scaled_length(source.width, scale),
            scaled_length(source.height, scale)
        };
    }
    // The side column hangs from the window's top left corner at its own
    // scale, and the in-game menu in it with the button.
    const double scale = column_scale(match_layout);
    return {
        static_cast<int>(std::lround(static_cast<double>(match_button_source_x) * scale)),
        static_cast<int>(std::lround(static_cast<double>(match_button_source_y) * scale)),
        scaled_length(settings::ingame_button_side, scale),
        scaled_length(settings::ingame_button_side, scale)
    };
}

renderer::Surface OaLayer::match_button_face(
    const layout::MatchLayout& match_layout,
    settings::ButtonLook look,
    bool darkened,
    const settings::DialogFonts& fonts,
    const artless::RgbaPicture& icon,
    bool fit
) {
    // Fitted, the face takes the button's own scale; else the side column's.
    const double shown_scale =
        fit ? static_cast<double>(match_button_rect(match_layout, true).width) /
                  settings::ingame_button_side
            : column_scale(match_layout);
    const int32_t scale = std::max(1, static_cast<int32_t>(std::ceil(shown_scale)));
    const artless::Placement placement{0, 0, scale};
    renderer::Surface face;
    face.width = static_cast<uint32_t>(settings::ingame_button_side * scale);
    face.height = face.width;
    face.rgb.assign(static_cast<std::size_t>(face.width) * face.height * 3U, 0);
    settings::draw_oa_button(face, placement, settings::ingame_button_side, look, fonts, icon);
    if (darkened)
        artless::blend_source_rect(
            face,
            placement,
            {0, 0, settings::ingame_button_side, settings::ingame_button_side},
            settings::backdrop_color,
            settings::ingame_backdrop_opacity
        );
    return face;
}

layout::Rect
OaLayer::match_dialog_rect(const layout::MatchLayout& match_layout, bool fit) noexcept {
    const auto safe = match_safe_area(match_layout);
    if (fit && safe.width > 0 && safe.height > 0) {
        // The largest scale that shows the whole dialog in the safe area.
        const double scale = std::min(
            static_cast<double>(safe.width) / settings::dialog_width,
            static_cast<double>(safe.height) / settings::dialog_height
        );
        const int32_t width = std::min(scaled_length(settings::dialog_width, scale), safe.width);
        const int32_t height = std::min(scaled_length(settings::dialog_height, scale), safe.height);
        return {
            safe.x + (safe.width - width) / 2, safe.y + (safe.height - height) / 2, width, height
        };
    }
    const int32_t width = scaled_length(settings::dialog_width, match_layout.scale);
    const int32_t height = scaled_length(settings::dialog_height, match_layout.scale);
    return {
        match_layout.left + (match_layout.width - match_layout.left - width) / 2,
        (match_layout.height - height) / 2,
        width,
        height
    };
}

void OaLayer::stamp(
    std::vector<uint8_t>& rgba,
    int32_t width,
    int32_t height,
    const renderer::Surface& source,
    const layout::Rect& rect
) {
    if (source.width == 0 || source.height == 0 || rect.width <= 0 || rect.height <= 0)
        return;
    const int32_t first_row = std::max(rect.y, 0);
    const int32_t bottom = std::min(rect.y + rect.height, height);
    const int32_t left = std::max(rect.x, 0);
    const int32_t right = std::min(rect.x + rect.width, width);
    for (int32_t row = first_row; row < bottom; ++row) {
        const auto source_row = static_cast<std::size_t>(
            static_cast<int64_t>(row - rect.y) * source.height / rect.height
        );
        for (int32_t column = left; column < right; ++column) {
            const auto source_column = static_cast<std::size_t>(
                static_cast<int64_t>(column - rect.x) * source.width / rect.width
            );
            const auto* from = source.rgb.data() + (source_row * source.width + source_column) * 3U;
            auto* to =
                rgba.data() + (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(column)) *
                                  4U;
            to[0] = from[0];
            to[1] = from[1];
            to[2] = from[2];
            to[3] = 255U;
        }
    }
}

bool OaLayer::refresh_match() {
    const auto* fonts = runtime_.engine_settings_fonts();
    const auto& laid_out = runtime_.match_layout_;
    const bool fit = match_fits_safe_area(runtime_);
    const auto button_at = match_button_rect(laid_out, fit);
    const LayerView seen = view();
    MatchLook look{};
    look.width = laid_out.width;
    look.height = laid_out.height;
    look.scale = laid_out.scale;
    look.button = {button_at.x, button_at.y, button_at.width, button_at.height};
    // The layer shows while the in-game menu's column shows, the button in it.
    look.button_shown = fonts != nullptr && runtime_.screen_ == Screen::match &&
                        runtime_.ingame_menu_column_shown();
    look.button_look = static_cast<uint8_t>(
        button_pressed_ && button_hovered_ ? settings::ButtonLook::pressed
        : button_hovered_                  ? settings::ButtonLook::hovered
                                           : settings::ButtonLook::idle
    );
    look.revision = revision_;
    if (!look.button_shown || look.width <= 0 || look.height <= 0 || !seen.match)
        return false;

    // Each screen that shows, where, and how many modal screens with a
    // backdrop show above it.
    struct ShownScreen {
        const LayerScreen* screen{};
        LayerPlacement placed{};
        uint32_t darkenings{};
    };

    std::vector<ShownScreen> shown_screens;
    const std::vector<uint32_t> above = backdrops_above(seen);
    for (std::size_t index = 0; index < screens_.size(); ++index) {
        const auto& screen = *screens_[index];
        const LayerPlacement placed = screen.placement(seen);
        if (empty_rect(placed.shown))
            continue;
        shown_screens.push_back({&screen, placed, above[index]});
        look.placed.insert(
            look.placed.end(),
            {placed.shown.x, placed.shown.y, placed.shown.width, placed.shown.height}
        );
        look.revisions.push_back(screen.revision());
        look.darkened = look.darkened || (screen.modal() && screen.backdrop());
    }
    if (drawn_ == look)
        return true;
    const auto pixels =
        static_cast<std::size_t>(look.width) * static_cast<std::size_t>(look.height);
    rgba_.assign(pixels * 4U, 0);
    const renderer::Surface button = match_button_face(
        laid_out,
        static_cast<settings::ButtonLook>(look.button_look),
        look.darkened,
        *fonts,
        runtime_.engine_settings_icon(),
        fit
    );
    bounds_ = button_at;
    if (look.darkened) {
        // The whole screen darkens, the button with it; the screens go over.
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            auto* to = rgba_.data() + pixel * 4U;
            to[0] = settings::backdrop_color[0];
            to[1] = settings::backdrop_color[1];
            to[2] = settings::backdrop_color[2];
            to[3] = static_cast<uint8_t>(kBackdropAlpha);
        }
        bounds_ = {0, 0, look.width, look.height};
    }
    stamp(rgba_, look.width, look.height, button, button_at);
    for (const auto& shown : shown_screens) {
        // A screen under a modal screen with a backdrop darkens as the
        // button does.
        renderer::Surface drawn = draw_screen(*shown.screen, shown.placed, 1);
        darken(drawn, shown.darkenings, settings::ingame_backdrop_opacity);
        stamp(rgba_, look.width, look.height, drawn, shown.placed.shown);
        bounds_ = union_rect(bounds_, shown.placed.shown);
    }
    drawn_ = look;
    uploaded_.reset();
    return true;
}

void OaLayer::compose_match(renderer::Surface& frame) {
    if (!refresh_match())
        return;
    const auto width = static_cast<int32_t>(frame.width);
    const auto height = static_cast<int32_t>(frame.height);
    if (!drawn_ || drawn_->width != width || drawn_->height != height)
        return;
    const int32_t first_row = std::max(bounds_.y, 0);
    const int32_t bottom = std::min(bounds_.y + bounds_.height, height);
    const int32_t left = std::max(bounds_.x, 0);
    const int32_t right = std::min(bounds_.x + bounds_.width, width);
    const auto& gamma = runtime_.gamma_table_;
    const bool identity = runtime_.gamma_identity_;
    // As the frontend dialogs' layer goes over the composed frame.
    for (int32_t row = first_row; row < bottom; ++row)
        for (int32_t column = left; column < right; ++column) {
            const auto pixel = static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(column);
            const auto* source = rgba_.data() + pixel * 4U;
            const unsigned alpha = source[3];
            if (alpha == 0)
                continue;
            auto* target = frame.rgb.data() + pixel * 3U;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const unsigned shown = identity ? source[channel] : gamma[source[channel]];
                target[channel] =
                    static_cast<uint8_t>((shown * alpha + target[channel] * (255U - alpha)) / 255U);
            }
        }
}

void OaLayer::present_match() {
    SDL_Renderer* sdl_renderer = runtime_.sdl_.renderer;
    if (sdl_renderer == nullptr || !refresh_match())
        return;
    const int width = drawn_->width;
    const int height = drawn_->height;
    if (texture_.ensure(
            sdl_renderer,
            SDL_PIXELFORMAT_RGBA32,
            width,
            height,
            runtime_.render_texture_limit(),
            SDL_BLENDMODE_BLEND
        ))
        uploaded_.reset();
    // A texture that held the front end's screens holds nothing of the match's.
    if (front_uploaded_) {
        front_uploaded_.reset();
        uploaded_.reset();
    }
    if (uploaded_ != drawn_ || uploaded_gamma_ != runtime_.gamma_table_) {
        const uint8_t* pixels = rgba_.data();
        std::vector<uint8_t> corrected;
        if (!runtime_.gamma_identity_) {
            corrected = rgba_;
            runtime_.apply_gamma_rgb(corrected.data(), corrected.size() / 4U, 4);
            pixels = corrected.data();
        }
        texture_.update(pixels, width * 4, 4);
        uploaded_ = drawn_;
        uploaded_gamma_ = runtime_.gamma_table_;
    }
    const SDL_FRect bounds{
        static_cast<float>(bounds_.x),
        static_cast<float>(bounds_.y),
        static_cast<float>(bounds_.width),
        static_cast<float>(bounds_.height)
    };
    // At the display's pixels with the match's other layers laid out 1:1.
    draw_one_to_one(sdl_renderer, texture_, &bounds, &bounds, runtime_.one_to_one_scale_mode());
}

void OaLayer::destroy_textures() noexcept {
    if (texture_.tile_count() == 0)
        return;
    texture_.reset();
    uploaded_.reset();
    front_uploaded_.reset();
}

// ---------------------------------------------------------------------------------------------
// What automation lists of the layer

AutomationControlKind automation_kind(const kit::AutomationEntry& entry) noexcept {
    if (entry.part)
        return entry.kind == kit::ControlKind::buttons ? AutomationControlKind::button
                                                       : AutomationControlKind::check_box;
    switch (entry.kind) {
    case kit::ControlKind::button:
    case kit::ControlKind::link:
    case kit::ControlKind::choice:
        return AutomationControlKind::button;
    case kit::ControlKind::toggle:
    case kit::ControlKind::levels:
    case kit::ControlKind::buttons:
        return AutomationControlKind::label;
    case kit::ControlKind::slider:
    case kit::ControlKind::scroll_bar:
        return AutomationControlKind::slider;
    case kit::ControlKind::tab:
    case kit::ControlKind::list_item:
        return AutomationControlKind::check_box;
    case kit::ControlKind::text_field:
        return AutomationControlKind::text_field;
    case kit::ControlKind::list:
        return AutomationControlKind::list;
    case kit::ControlKind::area:
        // An area that takes nothing is a line a journey reads.
        return entry.enabled || entry.focusable ? AutomationControlKind::area
                                                : AutomationControlKind::label;
    }
    return AutomationControlKind::area;
}

void OaLayer::automation_controls(std::vector<AutomationControl>& controls) const {
    const LayerView seen = view();
    const LayerScreen* modal = top_modal(seen);
    const WindowPlace place = window_place(
        runtime_.sdl_.renderer,
        seen.match,
        static_cast<int32_t>(runtime_.surface_.width),
        static_cast<int32_t>(runtime_.surface_.height)
    );
    // The OA button, on the main menu's picture or in the in-game menu's column.
    std::optional<layout::Rect> button_at;
    if (seen.match) {
        if (match_button_shown(runtime_))
            button_at = match_button_rect(runtime_.match_layout_, match_fits_safe_area(runtime_));
    } else if (Runtime::EngineSettingsMenuHost::button_shown(runtime_)) {
        const auto square = Runtime::EngineSettingsMenuHost::button_rect(runtime_);
        button_at = layout::Rect{square.x, square.y, square.width, square.height};
    }
    if (button_at) {
        AutomationControl button;
        button.name = std::string(kAutomationPrefix) + "button";
        button.kind = AutomationControlKind::button;
        button.visible = true;
        // Under a modal screen it takes no press.
        button.enabled = modal == nullptr;
        place_control(
            button,
            place,
            window_rect_of(
                place,
                {static_cast<double>(button_at->x),
                 static_cast<double>(button_at->y),
                 static_cast<double>(button_at->x + button_at->width),
                 static_cast<double>(button_at->y + button_at->height)}
            )
        );
        controls.push_back(std::move(button));
    }
    // The screens that show, from the top, down to the first that takes
    // every input: nothing under it takes any.
    for (auto it = screens_.rbegin(); it != screens_.rend(); ++it) {
        const LayerScreen& screen = **it;
        const LayerPlacement placed = screen.placement(seen);
        if (empty_rect(placed.shown) || placed.points_width <= 0 || placed.points_height <= 0)
            continue;
        const std::string dialog = std::string(kAutomationPrefix) + std::string(screen.name());
        const kit::DisplayList list = screen.display_list();
        for (const kit::AutomationEntry& entry :
             kit::automation_parts(list, screen.interaction())) {
            AutomationControl control;
            control.name = std::string(kAutomationPrefix) + entry.name;
            control.kind = automation_kind(entry);
            control.dialog = dialog;
            control.visible = entry.shown;
            control.enabled = entry.enabled;
            control.focused = entry.focused;
            control.checked = entry.checked;
            control.text = entry.text;
            // A front-end screen is drawn in the window's own pixels at its
            // place; a match's on its canvas.
            const Edges edges = screen_edges(placed, entry.rect, seen.frame.scale_percent);
            place_control(
                control,
                place,
                place.front_end ? window_rect_at(place, edges) : window_rect_of(place, edges)
            );
            controls.push_back(std::move(control));
        }
        if (screen.modal())
            break;
    }
}

} // namespace oa::app
