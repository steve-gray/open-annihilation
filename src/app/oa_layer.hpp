// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The OA layer (OaLayer): one host for Open Annihilation's own screens on
// every screen of the game. It keeps a stack of screens (LayerScreen) above
// the game's picture, and a queue of those nobody asked for at that moment
// that wait for it to be free; maps the input to each by its placement,
// tells a finger from a mouse, maps the keys to the kit's, latches the key
// that closed a screen, lets the top modal screen take every input, darkens
// what lies under each modal screen, and draws the screens in the window's
// own pixels before the software cursor. On the front end it draws them at a
// whole scale (kit::auto_scale) and lays them out at the size class of the
// points left over. oa_layer.cpp holds it, the settings' screen
// (Runtime::SettingsScreen) and the screens of a notice (NoticeScreen) and a
// question (QuestionScreen).
#pragma once

#include "oa/app/runtime.hpp"
#include "oa/ui/display_layout.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/components_more.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/screen_registry.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {

class OaLayer;

/// What a screen of the OA layer needs to place itself.
struct LayerView {
    /// The layer shows over a match; otherwise over the front end's picture.
    /// Either way its screens are placed in the window's own pixels.
    bool match{};
    /// The screen of the game the layer shows over now.
    Screen game_screen{};
    /// The match's layout; null on the front end.
    const oa::ui::display_layout::MatchLayout* match_layout{};
    /// In a match, the screens fit the window's safe area rather than the
    /// side column's scale: the touch controls are on and the window is
    /// phone class.
    bool fit_safe_area{};
    /// The room the screens have, in points, with its size class and scale.
    /// On the front end: the window's canvas less its safe insets while the
    /// game has touch controls, divided by the scale (kit::frame_of). In a
    /// match: the safe area, Compact at 100%, as the in-match Settings keeps
    /// its own placement.
    oa::ui::kit::Frame frame{};
    /// The whole number of window pixels a point takes: the Auto scale of the
    /// window's canvas (kit::auto_scale), no larger than lets Compact's
    /// settings dialog fit the room, and at least 1. 1 in a match.
    int32_t scale{1};
    /// The room in window pixels: the canvas less its safe insets on the
    /// front end, the safe area in a match. A centred screen centres in it.
    oa::ui::display_layout::Rect room{};
    /// Where the front end's picture, the 640 by 480 frame, is drawn in the
    /// window, in window pixels: what a screen anchored to a part of a 3.1c
    /// screen places itself by. The match's canvas in a match.
    oa::ui::display_layout::Rect picture{};
    /// The picture's own size, in its pixels: what input on the front end
    /// arrives in, and the picture's rectangle holds.
    oa::ui::display_layout::Point picture_size{};
};

/// Where a screen of the OA layer shows.
struct LayerPlacement {
    /// Where the screen shows, in window pixels: points_width times the
    /// scale wide on the front end, and wherever the in-match Settings is
    /// stamped in a match. Empty while the screen does not show: it then
    /// neither draws nor takes input.
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

    /// Draws the screen from its own top left corner at the canvas's whole
    /// scale, every pixel of its points_width × points_height points opaque.
    ///
    /// @param canvas where it draws: a picture of the screen's size times
    ///     the scale, the scale, the dialog's fonts and the Open Annihilation
    ///     icon
    virtual void draw(const oa::ui::kit::Canvas& canvas) const = 0;

    /// Lays the screen out for a view: Settings, a notice and a question take
    /// the view's size class on the front end. The layer calls it before it
    /// places, draws or hands input to the screen, and whenever the view's
    /// class may have changed. A screen of one layout ignores it.
    ///
    /// @param view what the screen is placed on
    virtual void lay_out(const LayerView& /*view*/) {}

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

    /// Brings the screen up to date once a frame. A screen waiting in the
    /// layer's queue (OaLayer::show_when_free) is ticked too, and leaves the
    /// queue when it answers close.
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
/// floor((x - shown.x) × points_width / shown.width), and the same down. On
/// the front end that is floor((x - left) / scale).
///
/// @param placement where the screen shows
/// @param x the point's column, in window pixels
/// @param y the point's row
/// @return the point in the screen's points; it may lie outside the screen
[[nodiscard]] oa::ui::kit::Point
layer_point(const LayerPlacement& placement, float x, float y) noexcept;

/// Returns where a screen of a size shows centred in a view's room at the
/// view's scale: left = room's left + (room's width - width × scale) / 2,
/// rounded down, and the same down.
///
/// @param view what the screen is placed on
/// @param width_points the screen's width, in points
/// @param height_points the screen's height, in points
/// @return its place, in window pixels
[[nodiscard]] LayerPlacement
centred_placement(const LayerView& view, int32_t width_points, int32_t height_points) noexcept;

/// A place in the window, in window pixels and their fractions.
struct WindowPosition {
    double x{}; ///< column
    double y{}; ///< row
};

/// Returns where an input's position lies in the window: on the front end,
/// where input arrives in the picture's pixels, through the picture's
/// rectangle (to the nearest 64th of a pixel, as the mapping back from the
/// picture leaves it); in a match as it is.
///
/// @param view what the screens are placed on
/// @param x the input's column, in the coordinates input arrives in
/// @param y the input's row
/// @return the position, in window pixels
[[nodiscard]] WindowPosition window_position(const LayerView& view, float x, float y) noexcept;

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
/// A screen somebody asked for joins at once (push); one nobody asked for
/// at that moment, such as a notice or an install's question, waits in a
/// queue, first in, first out, until no modal screen shows
/// (show_when_free), so that two never show at once.
///
/// On the front end the screens are laid out at the size class of the
/// window's canvas and drawn at its whole scale (view), centred in the
/// window; each modal screen with a backdrop darkens the frame itself, the
/// window round it and every screen under it (darken_front_end, present),
/// and the screens are copied into the window in its own pixels (present),
/// before the software cursor; without a renderer, and for
/// frame_without_cursor, they are composed into the frame instead
/// (compose_front_end). In a match the layer is one picture of the window's
/// size: the in-game OA button, the backdrop and the screens, drawn again
/// when what it shows changes (refresh_match).
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

    /// Lets the screens, and those waiting, go without closing them, and
    /// destroys the textures.
    ~OaLayer();

    OaLayer(const OaLayer&) = delete;
    OaLayer& operator=(const OaLayer&) = delete;

    /// Puts a screen on top of the stack at once: a screen somebody asked
    /// for, such as Settings, or a question a screen asks of its own over
    /// that screen.
    ///
    /// @param screen the screen; null is ignored
    void push(std::unique_ptr<LayerScreen> screen);

    /// Shows a screen nobody asked for at this moment, such as a notice or an
    /// install's question, once the layer is free: the screens wait first in,
    /// first out, and the first is pushed while no modal screen shows, at
    /// once, after an input or a tick, or when a screen is closed
    /// (close); one at a time while each shows.
    ///
    /// @param screen the screen; null is ignored
    void show_when_free(std::unique_ptr<LayerScreen> screen);

    /// Takes a screen off the stack, or out of the queue, telling it it
    /// closes, and pushes the next waiting screen if the layer is then free.
    /// A screen asked while it takes an input or its tick leaves once that
    /// is done, as if it had answered close.
    ///
    /// @param screen the screen; one the layer does not hold is ignored
    void close(const LayerScreen* screen);

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

    /// Returns the topmost screen of a name on the stack.
    ///
    /// @param name the screen's name
    /// @return the screen; null when none has the name
    [[nodiscard]] LayerScreen* find(std::string_view name) const noexcept;

    /// Returns the first screen of a name that waits in the queue.
    ///
    /// @param name the screen's name
    /// @return the screen; null when none waits
    [[nodiscard]] LayerScreen* waiting(std::string_view name) const noexcept;

    /// Tells whether a screen is on the stack.
    ///
    /// @param screen the screen
    /// @return true while it is
    [[nodiscard]] bool holds(const LayerScreen* screen) const noexcept;

    /// Returns the fonts the layer's screens are drawn in: the settings
    /// dialog's (Runtime::engine_settings_fonts).
    ///
    /// @return the fonts; null when they cannot be loaded
    [[nodiscard]] const oa::ui::kit::Fonts* screen_fonts() const;

    /// Returns what the screens are placed on now: the window over the front
    /// end's picture, with the scale and size class of the window's canvas
    /// (its size in pixels, the window points it holds and, while the game
    /// has touch controls, its safe insets), or the match's canvas. Without a
    /// renderer the canvas is the picture itself.
    ///
    /// @return the view
    [[nodiscard]] LayerView view() const;

    /// Returns the whole scale the front end's screens are drawn at now.
    ///
    /// @return window pixels a point; 1 in a match
    [[nodiscard]] int32_t scale() const { return view().scale; }

    /// Returns the size class the front end's screens are laid out at now.
    ///
    /// @return the class; Compact in a match
    [[nodiscard]] oa::ui::kit::SizeClass size_class() const { return view().frame.size_class; }

    /// Returns where the topmost screen of a name shows now.
    ///
    /// @param name the screen's name
    /// @return its place, in window pixels; empty when no such screen shows
    [[nodiscard]] LayerPlacement placement_of(std::string_view name) const;

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

    /// Latches a key as closing a screen does: its presses do nothing until
    /// it is released. A question whose answer to a key leaves it on the
    /// layer latches that key, so that the key held answers nothing more.
    ///
    /// @param sdl_key the SDL keycode
    void latch_key(uint32_t sdl_key) noexcept { latched_key_ = sdl_key; }

    /// Forgets the latched key, as its release would.
    void forget_latched_key() noexcept { latched_key_ = 0; }

    /// Draws the match's layer again at its next refresh: for a change made
    /// to a screen's model from outside its events.
    void redraw() noexcept { ++revision_; }

    /// Asks for the window's read-back (the frame a check captures as it is
    /// presented) to hold the whole window on the front end, the letterbox
    /// round the picture with it, rather than the picture's area alone: what
    /// a check compares the screens and their backdrop with, as they are
    /// drawn in the window's own pixels. Off, the read-back is the picture's.
    ///
    /// @param whole true for the whole window
    void read_whole_window(bool whole) noexcept { whole_window_ = whole; }

    /// Tells whether the window's read-back holds the whole window on the
    /// front end (read_whole_window).
    ///
    /// @return true for the whole window
    [[nodiscard]] bool reads_whole_window() const noexcept { return whole_window_; }

    /// Darkens the front end's frame under the modal screens with a
    /// backdrop: the frame blended with the backdrop's colour at the menu's
    /// opacity once for each that shows.
    ///
    /// @param[in,out] frame the composed frame
    void darken_front_end(renderer::Surface& frame) const;

    /// Draws each screen that shows on the front end into the frame, at its
    /// place mapped through the picture's rectangle, darkened once for each
    /// modal screen with a backdrop that shows above it: the picture the
    /// player sees, for checks and runs without a renderer. Each pixel of the
    /// frame takes the screen's pixel at its centre in the window; where the
    /// picture fills the window, as without a renderer and in a 640 by 480
    /// window, that is the screen drawn at 1× at its place.
    ///
    /// @param[in,out] frame the composed frame
    void compose_front_end(renderer::Surface& frame) const;

    /// Draws the match's layer again when what it shows changed: the
    /// in-game OA button while the in-game menu's column shows, the backdrop
    /// under a modal screen, and the screens that show, each darkened as
    /// the button is once for each modal screen with a backdrop above it.
    ///
    /// @return true when the layer shows anything
    bool refresh_match();

    /// Draws the match's layer over a composed match frame, at the display gamma.
    ///
    /// @param[in,out] frame the composed frame, at the window's size
    void compose_match(renderer::Surface& frame);

    /// Draws the layer into the window, in the window's own pixels
    /// (RenderState::use_window_pixels), before the software cursor: on the
    /// front end each screen drawn at the view's scale and copied to its
    /// place pixel for pixel, and, under a modal screen with a backdrop, the
    /// window outside the picture darkened as the picture is; in a match the
    /// match's layer, as the match's other layers are drawn.
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
        oa::ui::display_layout::Rect
            window{};                         ///< the part of it the window shows, in window pixels
        oa::ui::display_layout::Rect shown{}; ///< where it shows, in window pixels
        renderer::Surface drawn{};            ///< its drawing at the view's scale
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

    /// Tells whether a screen waits in the queue.
    ///
    /// @param screen the screen
    /// @return true while it does
    [[nodiscard]] bool queued(const LayerScreen* screen) const noexcept;

    /// Returns, for each screen of the stack, bottom first, how many modal
    /// screens with a backdrop show above it: each darkens it once.
    ///
    /// @param seen what the screens are placed on
    /// @return the counts, one for each screen of the stack
    [[nodiscard]] std::vector<uint32_t> backdrops_above(const LayerView& seen) const;

    /// Hands one input to a screen at its place; a screen that asked to be
    /// closed while it took the input (close) answers close.
    ///
    /// @param screen the screen
    /// @param seen what the screen is placed on
    /// @param input the input
    /// @return what the screen answered
    LayerAnswer deliver(LayerScreen& screen, const LayerView& seen, const ScreenInput& input);

    /// Hands one input to a screen at its place, as deliver does, without
    /// looking for a close asked meanwhile.
    ///
    /// @param screen the screen
    /// @param seen what the screen is placed on
    /// @param input the input
    /// @return what the screen answered
    LayerAnswer hand_input(LayerScreen& screen, const LayerView& seen, const ScreenInput& input);

    /// Takes a screen off, telling it it closes.
    ///
    /// @param screen the screen
    /// @param by_key a key's press closed it
    void remove(const LayerScreen* screen, bool by_key);

    /// Takes a screen out of the queue, telling it it closes.
    ///
    /// @param screen the screen
    void drop_waiting(const LayerScreen* screen);

    /// Pushes the waiting screens, first in first, while no modal screen
    /// shows.
    void show_waiting();

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

    /// Lays every screen out for the view now, and every one waiting
    /// (LayerScreen::lay_out).
    void lay_out_screens();

    /// Draws a screen from its own top left corner at a whole scale.
    ///
    /// @param screen the screen
    /// @param placed where it shows
    /// @param scale window pixels a point
    /// @return its picture, points_width × points_height times the scale
    [[nodiscard]] renderer::Surface
    draw_screen(const LayerScreen& screen, const LayerPlacement& placed, int32_t scale) const;

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
    /// The screens waiting for the layer to be free, first in first.
    std::vector<std::unique_ptr<LayerScreen>> queue_{};
    /// The screen an input or its tick is handed to now; null between.
    const LayerScreen* calling_{};
    /// close asked for calling_ while it was called: it leaves once the
    /// call is done.
    bool close_asked_{};
    /// The key whose press closed a screen, until it is released; 0 for
    /// none. Its presses do nothing until then, so that a held key never
    /// reaches what lies under the layer.
    uint32_t latched_key_{};
    /// The field the system's text input was started over, in the
    /// coordinates input arrives in; none while the layer has not started it.
    std::optional<oa::ui::display_layout::Rect> text_field_{};
    bool whole_window_{}; ///< the front end's read-back holds the whole window (read_whole_window)
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

/// A notice of Open Annihilation's own (kit::Notice) as a screen of the OA
/// layer: modal over a backdrop, laid out at the view's size class and
/// centred in the window at its scale over one screen of the game, its open
/// button and OK answered as its host says. A host that tells the player something nobody asked for at
/// that moment shows it with OaLayer::show_when_free.
class NoticeScreen final : public LayerScreen {
  public:

    /// The name every notice has on the layer.
    static constexpr std::string_view screen_name = "notice";

    /// What the notice's host does.
    struct Host {
        /// Opens the notice's folder when its open button is pressed, and
        /// returns why it could not, which the notice shows in amber under
        /// its text; empty when it opened. Unset, the button opens nothing.
        std::function<std::string()> open{};
        /// What OK, Enter or Escape closing the notice does besides, such as
        /// its sound; may be unset.
        std::function<void()> ok{};
        /// Tells, once a frame, whether the notice must close, as when the
        /// screen it shows over goes; unset, it never must.
        std::function<bool()> tick{};
    };

    /// Makes the screen of a notice.
    ///
    /// @param layer the layer it shows on, whose fonts it is drawn in
    /// @param shown_over the screen of the game it shows over; it shows on no other
    /// @param told the notice
    /// @param answers what its host does
    NoticeScreen(OaLayer& layer, Screen shown_over, oa::ui::kit::Notice told, Host answers);

    /// Returns the notice as it shows.
    ///
    /// @return the notice
    [[nodiscard]] const oa::ui::kit::Notice& notice() const noexcept { return notice_; }

    /// Returns the screen of the game the notice shows over.
    ///
    /// @return the screen
    [[nodiscard]] Screen over() const noexcept { return over_; }

    /// Returns "notice".
    ///
    /// @return the name
    [[nodiscard]] std::string_view name() const override { return screen_name; }

    /// Returns where the notice shows: centred in the view's room at its
    /// scale, its size class's notice width wide and as tall as its text
    /// makes it at that width, over its own screen of the game.
    ///
    /// @param view what the screen is placed on
    /// @return its place; empty in a match, over another screen, or without fonts
    [[nodiscard]] LayerPlacement placement(const LayerView& view) const override;

    /// Lays the notice out at the view's size class (kit::Notice::size_class).
    ///
    /// @param view what the screen is placed on
    void lay_out(const LayerView& view) override;

    /// Tells that the notice takes every input.
    ///
    /// @return true
    [[nodiscard]] bool modal() const override { return true; }

    /// Tells that what lies under the notice darkens.
    ///
    /// @return true
    [[nodiscard]] bool backdrop() const override { return true; }

    /// Draws the notice in the settings dialog's look, its texts in the
    /// language shown (oa::ui::engine_settings::draw_notice).
    ///
    /// @param canvas where it draws
    void draw(const oa::ui::kit::Canvas& canvas) const override;

    /// Takes a pointer's move, press or release: a finger's press takes the
    /// nearer button within reach.
    ///
    /// @param kind pointer_move, pointer_down or pointer_up
    /// @param button the pointer's button
    /// @param at the pointer, in the notice's points
    /// @param finger_reach a finger's reach in the notice's points; 0 for a mouse
    /// @return close when OK closed it; otherwise none or redraw
    LayerAnswer pointer(
        ScreenInputKind kind, uint8_t button, oa::ui::kit::Point at, int32_t finger_reach
    ) override;

    /// Takes a key's press: Enter and Escape close the notice, Space presses
    /// the marked button, and the arrows and Tab move the mark.
    ///
    /// @param pressed the key
    /// @return close when the key closed it; otherwise none or redraw
    LayerAnswer key(oa::ui::kit::Key pressed, uint32_t /*sdl_key*/) override;

    /// Takes a turn of the wheel, which does nothing under the notice.
    ///
    /// @return none
    LayerAnswer wheel(oa::ui::kit::Point /*at*/, float /*notches*/) override {
        return LayerAnswer::none;
    }

    /// Returns the notice's display list (kit::notice_list), its texts in
    /// the language shown.
    ///
    /// @return the list
    [[nodiscard]] oa::ui::kit::DisplayList display_list() const override;

    /// Returns the notice's hover, press and mark.
    ///
    /// @return the interaction
    [[nodiscard]] oa::ui::kit::Interaction interaction() const override;

    /// Asks the host whether the notice must close.
    ///
    /// @return close when it must; otherwise none
    LayerAnswer tick() override;

    /// Returns the notice's revision, which counts the changes to its look.
    ///
    /// @return the revision
    [[nodiscard]] uint64_t revision() const override { return revision_; }

    /// Has nothing of its own to close.
    void close(bool /*by_key*/) override {}

  private:

    /// Returns the notice's height in its fonts.
    ///
    /// @return the height, in points
    [[nodiscard]] int32_t shown_height() const;

    /// Carries out what an event on the notice asks: the open button's
    /// folder, OK's closing.
    ///
    /// @param action what the event asked
    /// @return what the screen answers
    LayerAnswer take(oa::ui::kit::NoticeAction action);

    OaLayer& layer_;             ///< the layer it shows on
    Screen over_{};              ///< the screen of the game it shows over
    oa::ui::kit::Notice notice_; ///< the notice
    Host host_;                  ///< what its host does
    uint64_t revision_{};        ///< counts the changes to its look
};

/// A question of Open Annihilation's own (kit::Question) as a screen of the
/// OA layer: modal over a backdrop, laid out at the view's size class and
/// centred in the window at its scale over one screen of the game, its
/// answers handed to its host. A question
/// nobody asked for at that moment, such as an install's, is shown with
/// OaLayer::show_when_free; one a screen asks of its own is pushed over it at
/// once (OaLayer::push). Its host may change the question while it shows
/// (question, changed).
class QuestionScreen final : public LayerScreen {
  public:

    /// The name every question has on the layer.
    static constexpr std::string_view screen_name = "prompt";

    /// What the question's host does.
    struct Host {
        /// Takes an answer: the button pressed, from 0. The host may change
        /// the question, or close it (OaLayer::close), meanwhile. Unset, every
        /// answer closes the question.
        ///
        /// Returns true when the answer is the question's last and it
        /// closes; false keeps it, as the host left it.
        std::function<bool(int32_t button)> answer{};
        /// Tells, once a frame, whether the question must close or be set
        /// aside, as when the screen it shows over goes; unset, it never must.
        std::function<bool()> tick{};
        /// What the question's leaving the layer, or its queue, does,
        /// whatever took it off; may be unset.
        std::function<void(const QuestionScreen& leaving)> closed{};
    };

    /// Makes the screen of a question.
    ///
    /// @param layer the layer it shows on, whose fonts it is drawn in, and
    ///     which latches the key that answered it
    /// @param shown_over the screen of the game it shows over; it shows on no other
    /// @param asked the question
    /// @param answers what its host does
    QuestionScreen(OaLayer& layer, Screen shown_over, oa::ui::kit::Question asked, Host answers);

    /// Returns the question, which its host may change; it then calls changed.
    ///
    /// @return the question
    [[nodiscard]] oa::ui::kit::Question& question() noexcept { return question_; }

    /// Returns the question as it shows.
    ///
    /// @return the question
    [[nodiscard]] const oa::ui::kit::Question& question() const noexcept { return question_; }

    /// Tells the screen its host changed the question: it keeps the size
    /// class the layer lays it out at, whatever the host put in its place.
    void changed() noexcept {
        question_.size_class = size_class_;
        ++revision_;
    }

    /// Returns the screen of the game the question shows over.
    ///
    /// @return the screen
    [[nodiscard]] Screen over() const noexcept { return over_; }

    /// Returns "prompt".
    ///
    /// @return the name
    [[nodiscard]] std::string_view name() const override { return screen_name; }

    /// Returns where the question shows: centred in the view's room at its
    /// scale, its size class's notice width wide and as tall as its text
    /// makes it at that width, over its own screen of the game.
    ///
    /// @param view what the screen is placed on
    /// @return its place; empty in a match, over another screen, or without fonts
    [[nodiscard]] LayerPlacement placement(const LayerView& view) const override;

    /// Lays the question out at the view's size class
    /// (kit::Question::size_class).
    ///
    /// @param view what the screen is placed on
    void lay_out(const LayerView& view) override;

    /// Tells that the question takes every input.
    ///
    /// @return true
    [[nodiscard]] bool modal() const override { return true; }

    /// Tells that what lies under the question darkens.
    ///
    /// @return true
    [[nodiscard]] bool backdrop() const override { return true; }

    /// Draws the question in a notice's look
    /// (oa::ui::engine_settings::draw_prompt).
    ///
    /// @param canvas where it draws
    void draw(const oa::ui::kit::Canvas& canvas) const override;

    /// Takes a pointer's move, press or release: a finger's press takes the
    /// nearest button within reach, and a release over the button pressed
    /// answers it.
    ///
    /// @param kind pointer_move, pointer_down or pointer_up
    /// @param button the pointer's button
    /// @param at the pointer, in the question's points
    /// @param finger_reach a finger's reach in the question's points; 0 for a mouse
    /// @return close when an answer closed it; otherwise none or redraw
    LayerAnswer pointer(
        ScreenInputKind kind, uint8_t button, oa::ui::kit::Point at, int32_t finger_reach
    ) override;

    /// Takes a key's press: Enter and Space answer the marked button, Escape
    /// and N the cancel button, Y the primary one; the arrows and Tab move
    /// the mark. The key that answered is latched (OaLayer::latch_key).
    ///
    /// @param pressed the key
    /// @param sdl_key the key's SDL keycode
    /// @return close when the answer closed it; otherwise none or redraw
    LayerAnswer key(oa::ui::kit::Key pressed, uint32_t sdl_key) override;

    /// Takes a turn of the wheel, which does nothing under the question.
    ///
    /// @return none
    LayerAnswer wheel(oa::ui::kit::Point /*at*/, float /*notches*/) override {
        return LayerAnswer::none;
    }

    /// Returns the question's display list (kit::question_list).
    ///
    /// @return the list
    [[nodiscard]] oa::ui::kit::DisplayList display_list() const override;

    /// Returns the question's hover, press and mark.
    ///
    /// @return the interaction
    [[nodiscard]] oa::ui::kit::Interaction interaction() const override;

    /// Asks the host whether the question must close or be set aside.
    ///
    /// @return close when it must; otherwise none
    LayerAnswer tick() override;

    /// Returns the question's revision, which counts the changes to its look.
    ///
    /// @return the revision
    [[nodiscard]] uint64_t revision() const override { return revision_; }

    /// Tells the host the question leaves (Host::closed).
    void close(bool /*by_key*/) override;

  private:

    /// Returns the question's height in its fonts.
    ///
    /// @return the height, in points
    [[nodiscard]] int32_t shown_height() const;

    /// Carries out what an event on the question asks: an answer goes to
    /// the host, and the key that gave it is latched.
    ///
    /// @param outcome what the event did
    /// @param sdl_key the key whose press it was; 0 for a pointer
    /// @return what the screen answers
    LayerAnswer take(oa::ui::kit::QuestionAnswer outcome, uint32_t sdl_key);

    OaLayer& layer_;                 ///< the layer it shows on
    Screen over_{};                  ///< the screen of the game it shows over
    oa::ui::kit::Question question_; ///< the question
    Host host_;                      ///< what its host does
    uint64_t revision_{};            ///< counts the changes to its look
    /// The size class the layer lays the question out at (lay_out).
    oa::ui::kit::SizeClass size_class_{oa::ui::kit::SizeClass::compact};
};

} // namespace oa::app
