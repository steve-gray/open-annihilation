// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The OA layer (OaLayer): one host for Open Annihilation's own screens on
// every screen of the game. It keeps a stack of screens (LayerScreen) above
// the game's picture, maps the input to each by its placement, tells a
// finger from a mouse, maps the keys to the kit's, latches the key that
// closed a screen, lets the top modal screen take every input, darkens what
// lies under it, and draws the screens in the window's own pixels before the
// software cursor. oa_layer.cpp holds it and the settings' screen
// (Runtime::SettingsScreen).
#pragma once

#include "oa/app/runtime.hpp"
#include "oa/ui/display_layout.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/screen_registry.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace oa::app {

/// What a screen of the OA layer needs to place itself.
struct LayerView {
    /// The layer shows over a match, its screens in the window's pixels;
    /// otherwise over the front end's picture, in the picture's pixels.
    bool match{};
    /// The match's layout; null on the front end.
    const oa::ui::display_layout::MatchLayout* match_layout{};
    /// In a match, the screens fit the window's safe area rather than the
    /// side column's scale: the touch controls are on and the window is
    /// phone class.
    bool fit_safe_area{};
    /// The room the screens have, in points, with its size class and scale:
    /// the picture on the front end, the safe area in a match; Compact at
    /// 100% while every screen is drawn at 1×.
    oa::ui::kit::Frame frame{};
};

/// Where a screen of the OA layer shows.
struct LayerPlacement {
    /// Where the screen shows, in the coordinates input arrives in: the
    /// picture's pixels on the front end, the window's in a match. Empty
    /// while the screen does not show: it then neither draws nor takes input.
    oa::ui::display_layout::Rect shown{};
    int32_t points_width{};  ///< the screen's width in its own points, as draw draws it
    int32_t points_height{}; ///< the screen's height in its own points
};

/// What a screen of the OA layer did with an input, or with its frame's tick.
enum class LayerAnswer : uint8_t {
    /// The screen does not take it. Under a modal screen it counts as none;
    /// a screen that is not modal lets it go on to the screens below it and
    /// then to what lies under the layer.
    pass,
    none,   ///< taken; nothing the screen draws changed
    redraw, ///< taken; what the screen draws changed (its revision changed)
    close,  ///< taken, and the screen closes: the layer takes it off
};

/// One screen of the OA layer: a modal dialog such as Settings, a notice, a
/// card, a button. Every place and point is in the screen's own points, from
/// its top left corner; the layer maps them to where it shows
/// (LayerPlacement). No call of it names an SDL type.
class LayerScreen {
  public:

    LayerScreen() = default;
    LayerScreen(const LayerScreen&) = delete;
    LayerScreen& operator=(const LayerScreen&) = delete;

    /// Lets the screen go.
    virtual ~LayerScreen() = default;

    /// Returns the screen's name, by which the layer finds it.
    ///
    /// @return a short lower-case word, such as "settings"
    [[nodiscard]] virtual std::string_view name() const = 0;

    /// Returns where the screen shows.
    ///
    /// @param view what the screen is placed on
    /// @return its place; an empty shown rectangle while it does not show
    [[nodiscard]] virtual LayerPlacement placement(const LayerView& view) const = 0;

    /// Tells whether the screen takes every input while it shows, nothing
    /// under it seeing any.
    ///
    /// @return true for a modal screen
    [[nodiscard]] virtual bool modal() const = 0;

    /// Tells whether what lies under the screen darkens while it shows.
    ///
    /// @return true for a backdrop
    [[nodiscard]] virtual bool backdrop() const = 0;

    /// Draws the screen at 1× from its own top left corner, every pixel of
    /// its points_width × points_height opaque.
    ///
    /// @param canvas where it draws: a picture of the screen's size, the
    ///     dialog's fonts and the Open Annihilation icon
    virtual void draw(const oa::ui::kit::Canvas& canvas) const = 0;

    /// Takes a pointer's move, press or release.
    ///
    /// @param kind pointer_move, pointer_down or pointer_up
    /// @param button the pointer's button, 1 for the left; 0 for a move
    /// @param at the pointer, in the screen's points; it may lie outside the screen
    /// @param finger_reach how far a finger's press may lie from a control,
    ///     in the screen's points; 0 for a mouse
    /// @return what the screen did
    virtual LayerAnswer
    pointer(ScreenInputKind kind, uint8_t button, oa::ui::kit::Point at, int32_t finger_reach) = 0;

    /// Takes a key's press.
    ///
    /// @param pressed the key, as the kit names it (layer_key)
    /// @param sdl_key the key's SDL keycode
    /// @return what the screen did
    virtual LayerAnswer key(oa::ui::kit::Key pressed, uint32_t sdl_key) = 0;

    /// Takes a turn of the wheel.
    ///
    /// @param at the pointer, in the screen's points
    /// @param notches how far the wheel turned; positive away from the player
    /// @return what the screen did
    virtual LayerAnswer wheel(oa::ui::kit::Point at, float notches) = 0;

    /// Takes text the player typed into the focused text field.
    ///
    /// @return what the screen did; pass unless the screen has a text field
    virtual LayerAnswer text(std::string_view /*utf8*/) { return LayerAnswer::pass; }

    /// Returns the focused text field, over which the system's text input
    /// stays started while the screen takes input.
    ///
    /// @return the field's rectangle, in the screen's points; none without one
    [[nodiscard]] virtual std::optional<oa::ui::kit::Rect> text_field() const {
        return std::nullopt;
    }

    /// Returns what the screen draws and where its controls are, at its own
    /// points, as automation lists them.
    ///
    /// @return the display list
    [[nodiscard]] virtual oa::ui::kit::DisplayList display_list() const = 0;

    /// Returns the screen's hover, press and focus, as automation lists them.
    ///
    /// @return the interaction
    [[nodiscard]] virtual oa::ui::kit::Interaction interaction() const = 0;

    /// Brings the screen up to date once a frame.
    ///
    /// @return close when the screen closes; otherwise none or redraw
    virtual LayerAnswer tick() = 0;

    /// Returns a number that changes whenever what the screen draws changes.
    ///
    /// @return the revision
    [[nodiscard]] virtual uint64_t revision() const = 0;

    /// Tells the screen it leaves the layer, whatever took it off. A screen
    /// whose own model is still open closes it here, as its Cancel would.
    ///
    /// @param by_key a key's press closed it; that key is latched
    virtual void close(bool by_key) = 0;
};

/// Maps a point to a screen's own points by its placement:
/// floor((x - shown.x) × points_width / shown.width), and the same down.
///
/// @param placement where the screen shows
/// @param x the point's column, in the coordinates input arrives in
/// @param y the point's row
/// @return the point in the screen's points; it may lie outside the screen
[[nodiscard]] oa::ui::kit::Point
layer_point(const LayerPlacement& placement, float x, float y) noexcept;

/// Returns the key a press means to a screen of the OA layer: Enter, Escape,
/// the arrows, Space, Tab and Shift+Tab, Page Up and Page Down, Home and End,
/// Y and N, Backspace and Delete.
///
/// @param sdl_key the SDL keycode
/// @param modifiers SDL_Keymod bits
/// @return the kit's key; nothing for a key no screen answers to
[[nodiscard]] std::optional<oa::ui::kit::Key>
layer_key(uint32_t sdl_key, uint16_t modifiers) noexcept;

/// The host of Open Annihilation's own screens over every screen of the
/// game (Runtime::oa_layer).
///
/// Input reaches it through one overlay at z 100 on every screen
/// (Runtime::register_oa_layer): the latched key first, then the top modal
/// screen that shows takes every event; with none, each screen that shows
/// sees it from the top, and the first that answers other than pass takes
/// it; in a match the in-game OA button then takes its own; what nobody
/// takes goes on to the overlays and screens under the layer.
///
/// On the front end, a modal screen with a backdrop darkens the frame itself
/// (darken_front_end), and the screens are drawn at 1× and stamped into the
/// window over the picture's rectangle (present), before the software
/// cursor; without a renderer, and for frame_without_cursor, they are
/// composed into the frame instead (compose_front_end). In a match the layer
/// is one picture of the window's size: the in-game OA button, the backdrop
/// and the screens, drawn again when what it shows changes (refresh_match).
class OaLayer {
  public:

    /// The in-game OA button's left column in the in-game menu's column, in
    /// source pixels: under Resume, clear of its frame.
    static constexpr int32_t match_button_source_x = 100;
    /// The in-game OA button's top row in the in-game menu's column, in source pixels.
    static constexpr int32_t match_button_source_y = 455;

    /// Makes an empty layer.
    ///
    /// @param runtime the runtime it hosts screens for
    explicit OaLayer(Runtime& runtime) noexcept;

    /// Lets the screens go without closing them, and destroys the textures.
    ~OaLayer();

    OaLayer(const OaLayer&) = delete;
    OaLayer& operator=(const OaLayer&) = delete;

    /// Puts a screen on top of the stack.
    ///
    /// @param screen the screen; null is ignored
    void push(std::unique_ptr<LayerScreen> screen);

    /// Takes the top screen off, telling it it closes.
    void close_top();

    /// Takes off, from the top, every screen above the one of a name; with no
    /// such screen, nothing.
    ///
    /// @param name the screen's name
    void close_above(std::string_view name);

    /// Returns the top screen.
    ///
    /// @return the screen; null while the stack is empty
    [[nodiscard]] LayerScreen* top() const noexcept;

    /// Returns the topmost screen of a name.
    ///
    /// @param name the screen's name
    /// @return the screen; null when none has the name
    [[nodiscard]] LayerScreen* find(std::string_view name) const noexcept;

    /// Returns what the screens are placed on now: the front end's picture
    /// or the match's canvas.
    ///
    /// @return the view
    [[nodiscard]] LayerView view() const;

    /// Tells whether a screen shows on the front end or in a match.
    ///
    /// @param match true for a match, false for the front end
    /// @return true while a screen shows there
    [[nodiscard]] bool shows(bool match) const;

    /// Tells whether a modal screen shows on the front end or in a match, so
    /// that nothing under the layer sees input.
    ///
    /// @param match true for a match, false for the front end
    /// @return true while a modal screen shows there
    [[nodiscard]] bool modal_shown(bool match) const;

    /// Takes one input, as the layer's overlay does.
    ///
    /// @param input the input, in the picture's pixels on the front end and
    ///     the window's in a match
    /// @return true when the layer took it
    bool take_input(const ScreenInput& input);

    /// Carries out a screen's answer: close takes it off, latching the key
    /// whose press closed it. take_input does this for its own events; a
    /// check that hands a screen an event of its own does it here.
    ///
    /// @param screen the screen, on the layer
    /// @param answer what it answered
    /// @param key_down the key whose press it answered; 0 for anything else
    void take_answer(LayerScreen& screen, LayerAnswer answer, uint32_t key_down);

    /// Ticks every screen once a frame, taking off those that close, and
    /// keeps the system's text input over the focused text field.
    void tick();

    /// Returns the key that closed a screen until it is released.
    ///
    /// @return the SDL keycode; 0 for none
    [[nodiscard]] uint32_t latched_key() const noexcept { return latched_key_; }

    /// Forgets the latched key, as its release would.
    void forget_latched_key() noexcept { latched_key_ = 0; }

    /// Draws the match's layer again at its next refresh: for a change made
    /// to a screen's model from outside its events.
    void redraw() noexcept { ++revision_; }

    /// Darkens the front end's frame under a modal screen with a backdrop:
    /// the frame blended with the backdrop's colour at the menu's opacity.
    ///
    /// @param[in,out] frame the composed frame
    void darken_front_end(renderer::Surface& frame) const;

    /// Draws each screen that shows on the front end into the frame at 1×,
    /// at its place: the picture the player sees, for checks and runs
    /// without a renderer.
    ///
    /// @param[in,out] frame the composed frame
    void compose_front_end(renderer::Surface& frame) const;

    /// Draws the match's layer again when what it shows changed: the
    /// in-game OA button while the in-game menu's column shows, the backdrop
    /// under a modal screen, and the screens that show.
    ///
    /// @return true when the layer shows anything
    bool refresh_match();

    /// Draws the match's layer over a composed match frame, at the display gamma.
    ///
    /// @param[in,out] frame the composed frame, at the window's size
    void compose_match(renderer::Surface& frame);

    /// Draws the layer into the window, in the window's own pixels
    /// (RenderState::use_window_pixels), before the software cursor: on the
    /// front end each screen stamped from its 1× drawing to its place
    /// mapped through the picture's rectangle, each window pixel taking the
    /// screen's pixel under its centre; in a match the match's layer, as the
    /// match's other layers are drawn.
    ///
    /// Throws PresentError when SDL cannot make the layer's texture.
    ///
    /// @param picture_area where the front end's picture lies in the window
    ///     (SDL_GetRenderLogicalPresentationRect); null in a match
    void present(const SDL_FRect* picture_area);

    /// Returns the layer's texture, in tiles beyond the renderer's limit.
    ///
    /// @return the texture; none before the first present
    [[nodiscard]] const TiledTexture& texture() const noexcept { return texture_; }

    /// Destroys the layer's texture; the next present makes it again.
    void destroy_textures() noexcept;

    /// The layer's overlay's input callback (OverlayDesc::event): take_input.
    ///
    /// @param context the screen context, with its input and the runtime as its host
    /// @return 1 when the layer took the input
    static int overlay_event(ScreenContext* context, void* /*state*/);

    /// The layer's overlay's frame callback (OverlayDesc::tick): tick.
    ///
    /// @param context the screen context, with the runtime as its host
    static void overlay_tick(ScreenContext* context, void* /*state*/);

    /// The layer's overlay's drawing (OverlayDesc::draw), on the front end
    /// alone: darken_front_end, then, without a renderer or while the frame
    /// is drawn without the cursor (Runtime::frame_without_cursor),
    /// compose_front_end.
    ///
    /// @param context the screen context, with the frame and the runtime as its host
    static void overlay_draw(ScreenContext* context, void* /*state*/);

    /// Tells whether the in-game OA button shows: the in-game menu's column
    /// shows and the dialog's fonts are present.
    ///
    /// @param runtime the runtime
    /// @return true while the button shows
    [[nodiscard]] static bool match_button_shown(Runtime& runtime);

    /// Tells whether the match's screens and the OA button fit the window's
    /// safe area rather than the side column's scale: the touch controls are
    /// on and the window is phone class, whose side column is no longer
    /// drawn at a scale of its own.
    ///
    /// @param runtime the runtime
    /// @return true on a phone with touch controls
    [[nodiscard]] static bool match_fits_safe_area(const Runtime& runtime);

    /// Returns the window's safe area on the match's canvas.
    ///
    /// @param match_layout the match's layout
    /// @return the canvas less its safe-area insets, in window pixels
    [[nodiscard]] static oa::ui::display_layout::Rect
    match_safe_area(const oa::ui::display_layout::MatchLayout& match_layout) noexcept;

    /// Returns where the OA button shows on the match's canvas: under Resume
    /// in the side column at the column's own scale, which a side-column page
    /// taller than the window makes smaller than the bars' (so the button
    /// stays in the in-game menu at every window size); fitted to the safe
    /// area, where a placed region showing that part of the column puts it,
    /// else in the column as the 640x480 frame fitted to the safe area from
    /// its top left corner places it.
    ///
    /// @param match_layout the match's layout
    /// @param fit the button fits the safe area (match_fits_safe_area)
    /// @return the button's rectangle, in window pixels
    [[nodiscard]] static oa::ui::display_layout::Rect match_button_rect(
        const oa::ui::display_layout::MatchLayout& match_layout, bool fit = false
    ) noexcept;

    /// Draws the OA button's face as the match's layer shows it: at the
    /// whole-number scale at or above the side column's, so that its icon
    /// keeps its detail when the face is fitted to match_button_rect, and
    /// darkened as the screen is under a modal screen.
    ///
    /// @param match_layout the match's layout
    /// @param look how the button looks
    /// @param darkened a modal screen shows over the screen
    /// @param fonts the dialog's fonts
    /// @param icon the Open Annihilation icon; empty draws the OA mark
    /// @param fit the button fits the safe area (match_fits_safe_area): the
    ///     face takes the whole-number scale at or above the button's own
    /// @return the face, ingame_button_side source pixels a side at that scale
    [[nodiscard]] static renderer::Surface match_button_face(
        const oa::ui::display_layout::MatchLayout& match_layout,
        oa::ui::engine_settings::ButtonLook look,
        bool darkened,
        const oa::ui::engine_settings::DialogFonts& fonts,
        const oa::ui::frontend_renderer::RgbaPicture& icon,
        bool fit = false
    );

    /// Returns where the settings dialog shows on the match's canvas: at the
    /// side column's scale, centred in the area right of the column; fitted
    /// to the safe area, at the largest scale that shows it whole there
    /// (min(safe width / dialog_width, safe height / dialog_height)), centred
    /// in it.
    ///
    /// @param match_layout the match's layout
    /// @param fit the dialog fits the safe area (match_fits_safe_area)
    /// @return the dialog's rectangle, in window pixels
    [[nodiscard]] static oa::ui::display_layout::Rect match_dialog_rect(
        const oa::ui::display_layout::MatchLayout& match_layout, bool fit = false
    ) noexcept;

    /// Copies a surface into a rectangle of an opaque-or-clear layer, each
    /// layer pixel taking the surface pixel it lands on (nearest), fully
    /// opaque.
    ///
    /// @param[in,out] rgba the layer, 4 bytes a pixel
    /// @param width the layer's width
    /// @param height the layer's height
    /// @param source the surface
    /// @param rect where the surface lands, in layer pixels; clipped to the layer
    static void stamp(
        std::vector<uint8_t>& rgba,
        int32_t width,
        int32_t height,
        const renderer::Surface& source,
        const oa::ui::display_layout::Rect& rect
    );

  private:

    /// What the match's layer shows, at which size: it is drawn again only
    /// when this changes.
    struct MatchLook {
        int32_t width{};  ///< the layer's width, in window pixels
        int32_t height{}; ///< the layer's height, in window pixels
        double scale{};   ///< the side column's scale
        /// The OA button's rectangle, x, y, width and height, in window
        /// pixels: it moves with the safe area on a phone (fit_safe_area).
        std::array<int32_t, 4> button{};
        /// Each screen's rectangle, x, y, width and height, in window pixels.
        std::vector<int32_t> placed{};
        /// Each screen's revision, in the stack's order.
        std::vector<uint64_t> revisions{};
        bool button_shown{};   ///< the OA button shows
        bool darkened{};       ///< a modal screen with a backdrop shows over the darkened screen
        uint8_t button_look{}; ///< the button's oa::ui::engine_settings::ButtonLook
        uint64_t revision{};   ///< the layer's own revision when it was drawn
        bool operator==(const MatchLook&) const = default;
    };

    /// One screen of the front end as the window shows it.
    struct FrontPiece {
        oa::ui::display_layout::Rect window{}; ///< where it lands, in window pixels
        oa::ui::display_layout::Rect shown{};  ///< where it shows, in the picture's pixels
        renderer::Surface drawn{};             ///< its 1× drawing
    };

    /// What the front end's part of the layer shows in the window.
    struct FrontLook {
        int32_t picture_width{};          ///< the picture's width, in its pixels
        int32_t picture_height{};         ///< the picture's height, in its pixels
        std::array<float, 4> area{};      ///< the picture's rectangle in the window
        std::vector<FrontPiece> pieces{}; ///< the screens that show, bottom first
    };

    /// Returns the top modal screen that shows.
    ///
    /// @param seen what the screens are placed on
    /// @return the screen; null when none shows
    [[nodiscard]] LayerScreen* top_modal(const LayerView& seen) const;

    /// Tells whether a screen is still on the layer.
    ///
    /// @param screen the screen
    /// @return true while it is
    [[nodiscard]] bool holds(const LayerScreen* screen) const noexcept;

    /// Hands one input to a screen at its place.
    ///
    /// @param screen the screen
    /// @param seen what the screen is placed on
    /// @param input the input
    /// @return what the screen answered
    LayerAnswer deliver(LayerScreen& screen, const LayerView& seen, const ScreenInput& input);

    /// Takes a screen off, telling it it closes.
    ///
    /// @param screen the screen
    /// @param by_key a key's press closed it
    void remove(const LayerScreen* screen, bool by_key);

    /// Takes the input the in-game OA button answers to: the shortcut,
    /// Ctrl+F2 while the profile offers the mod options, and the pointer
    /// over the button.
    ///
    /// @param input the input, in window pixels
    /// @return true when it was taken
    bool take_match_button(const ScreenInput& input);

    /// Keeps the system's text input started over the focused text field of
    /// the screen taking input, mapped to the coordinates input arrives in,
    /// and stops it once no screen has one.
    void sync_text_input();

    /// Draws a screen at 1× from its own top left corner.
    ///
    /// @param screen the screen
    /// @param placed where it shows
    /// @return its picture, points_width × points_height
    [[nodiscard]] renderer::Surface
    draw_screen(const LayerScreen& screen, const LayerPlacement& placed) const;

    /// Draws the front end's screens into the window.
    ///
    /// @param area where the picture lies in the window, in window pixels
    void present_front_end(const SDL_FRect& area);

    /// Draws the match's layer into the window, as the match's other layers
    /// laid out 1:1 are drawn (one_to_one_scale_mode).
    void present_match();

    Runtime& runtime_; ///< the runtime it hosts screens for
    /// The screens, bottom first.
    std::vector<std::unique_ptr<LayerScreen>> screens_{};
    /// The key whose press closed a screen, until it is released; 0 for
    /// none. Its presses do nothing until then, so that a held key never
    /// reaches what lies under the layer.
    uint32_t latched_key_{};
    /// The field the system's text input was started over, in the
    /// coordinates input arrives in; none while the layer has not started it.
    std::optional<oa::ui::display_layout::Rect> text_field_{};
    bool button_hovered_{}; ///< the pointer is over the in-game OA button
    bool button_pressed_{}; ///< a press on the in-game OA button is held
    /// Counts the changes to what the match's layer shows that MatchLook
    /// cannot see: the button's hover and press, screens coming and going.
    uint64_t revision_{};
    std::optional<MatchLook> drawn_{}; ///< what rgba_ holds; nothing before the first draw
    /// The match's layer at the window's size, 4 bytes a pixel (red, green,
    /// blue, opacity), before the display gamma.
    std::vector<uint8_t> rgba_{};
    oa::ui::display_layout::Rect bounds_{}; ///< the part of the match's layer that is not clear
    std::optional<MatchLook> uploaded_{};   ///< the match's layer the texture holds
    /// The front end's screens the texture holds, at their places; none
    /// while it holds the match's layer or nothing.
    std::optional<FrontLook> front_uploaded_{};
    oa::ui::display_layout::Rect
        front_bounds_{}; ///< the window pixels the front end's texture covers
    std::array<uint8_t, 256> uploaded_gamma_{}; ///< the gamma table the texture was uploaded at
    /// The layer's texture, in tiles beyond the renderer's limit; none
    /// before the first present.
    TiledTexture texture_;
};

} // namespace oa::app
