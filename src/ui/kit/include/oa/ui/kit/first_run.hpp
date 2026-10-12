// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Provisional: this header changes whenever OA's screens need it to, until the kit is declared stable (src/ui/kit/README.md).

// The first-run pieces: a language card, a carousel of them, a step
// indicator, the setup window's header and footer, a choice card, a coach
// mark and its spotlight, a picture well and a call to action. Words arrive
// already looked up. Reduced motion is the frame's flag, off until a later
// Settings row sets it; nothing here reads a preference.
#pragma once

#include "oa/ui/kit/canvas.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::kit {

/// How far a carousel card turns, in degrees, for each step out from the centre.
inline constexpr int32_t carousel_turn_degrees = 34;

/// How many cards show on each side of the centre. One further step fades out.
inline constexpr int32_t carousel_visible_side = 2;

/// How long a carousel takes to move to the next card, in milliseconds.
inline constexpr uint32_t carousel_move_ms = 500;

/// How often a call to action sends out a ring, in milliseconds.
inline constexpr uint32_t call_to_action_period_ms = 1800;

/// The gap inside a picture well's border, in points.
inline constexpr int32_t picture_well_inset = 6;

/// The gap a coach mark keeps from its target, in points.
inline constexpr int32_t coach_gap = 8;

/// How far a finger must travel, in points, before a carousel press is a swipe.
inline constexpr int32_t carousel_swipe_threshold = 24;

/// The band under a carousel's cards that holds the pager, in points.
inline constexpr int32_t carousel_pager_band = 10;

/// The gap between a language card and the progress bar under it, in points.
inline constexpr int32_t language_progress_gap = 4;

// ---- Carousel motion ----

/// Where the carousel's centre sits while it moves, in thousandths of a card index.
///
/// The move eases out: at 0 it is from_milli, at carousel_move_ms and after
/// it is to_milli, and halfway through the time it has covered three quarters
/// of the way. The same time is the same place however many frames drew it.
///
/// @param from_milli the place the move started, in thousandths of an index
/// @param to_milli the place it moves to, in thousandths of an index
/// @param elapsed_ms how long the move has been running, in milliseconds
/// @return the centre, in thousandths of an index
[[nodiscard]] constexpr int32_t
carousel_centre_milli(int32_t from_milli, int32_t to_milli, uint32_t elapsed_ms) noexcept {
    if (elapsed_ms >= carousel_move_ms || from_milli == to_milli)
        return to_milli;
    const int32_t along = static_cast<int32_t>(elapsed_ms * 1000u / carousel_move_ms);
    const int32_t progress = 2 * along - (along * along) / 1000;
    return from_milli +
           static_cast<int32_t>((static_cast<int64_t>(to_milli - from_milli) * progress) / 1000);
}

/// A carousel's place: which card is selected, and how far a move has come.
struct CarouselModel {
    int32_t count{};        ///< how many cards; 0 is an empty carousel
    int32_t selected{};     ///< the card the move is going to, from 0
    int32_t origin_milli{}; ///< where the centre was when the move started, in thousandths
    uint32_t elapsed_ms{};  ///< how long this move has been running, in milliseconds
};

/// Starts a move to a card. The index is held inside the carousel. The centre
/// stays where it is now and eases to the card over carousel_move_ms.
///
/// @param[in,out] model the carousel
/// @param index the card to move to, from 0
void carousel_go(CarouselModel& model, int32_t index) noexcept;

/// Advances a move by a slice of time. Past carousel_move_ms the centre stays
/// on the selected card. Two slices add as one slice of their sum does.
///
/// @param[in,out] model the carousel
/// @param dt_ms how much time passed, in milliseconds
void carousel_tick(CarouselModel& model, uint32_t dt_ms) noexcept;

/// Returns where the centre sits now, in thousandths of a card index.
///
/// @param model the carousel
/// @return the centre; 0 when the carousel is empty
[[nodiscard]] int32_t carousel_index_milli(const CarouselModel& model) noexcept;

/// A carousel's card size and the step between centres, fitted to a width.
struct CarouselGeometry {
    int32_t card_width{};  ///< each card's width before it shrinks, in points
    int32_t card_height{}; ///< each card's height before it shrinks, in points
    int32_t step{};        ///< the points from one card's centre to the next
};

/// Fits a carousel's cards to a width.
///
/// The metrics' card and step are kept when four steps and a card fit the
/// room. Otherwise the card's width becomes a fifth of the room, its height
/// scales with it, and the step becomes the card's width, so the faces of a
/// resting carousel do not meet.
///
/// @param room the width the cards share, in points
/// @param metrics the size class's metrics
/// @return the card and the step
[[nodiscard]] CarouselGeometry carousel_fit(int32_t room, const Metrics& metrics) noexcept;

/// Returns the band the cards occupy: a carousel's rectangle without the pager.
///
/// @param rect the carousel, cards and pager
/// @return the cards' band
[[nodiscard]] Rect carousel_band(const Rect& rect) noexcept;

/// One card as the carousel places it.
struct CarouselSlot {
    int32_t index{};          ///< the card, from 0
    Rect face{};              ///< where it is drawn, in the area's points
    int32_t degrees{};        ///< the turn; positive shortens the right edge
    int32_t opacity{};        ///< how much of the card shows, in 256ths
    bool centre{};            ///< the card nearest the centre
    int32_t distance_milli{}; ///< how far it sits from the centre, in thousandths of a step
};

/// Places the cards that show.
///
/// The centre card faces the player. Each step out turns carousel_turn_degrees,
/// falls, shrinks and dims. Two steps show on each side and the third has
/// faded out. With reduced motion the cards do not turn, shrink or fall; they
/// still step sideways and dim. Far cards come first, so a nearer card is
/// drawn over them. The place is a pure function of the centre.
///
/// @param area the band the cards share, in points
/// @param geometry the card and the step
/// @param count how many cards
/// @param centre_milli where the centre sits, in thousandths of an index
/// @param reduced_motion true slides the cards flat
/// @param wrap true joins the ring, so the first card's neighbours include the last
/// @return the cards that show, far ones first
[[nodiscard]] std::vector<CarouselSlot> carousel_places(
    const Rect& area,
    const CarouselGeometry& geometry,
    int32_t count,
    int32_t centre_milli,
    bool reduced_motion,
    bool wrap = false
);

/// What a carousel was asked to do. Shoulders and a stick set the flags; the keys do not.
struct CarouselCommand {
    bool previous{}; ///< move to the card before the selected one
    bool next{};     ///< move to the card after the selected one
    bool accept{};   ///< pick the selected card
};

/// What a carousel did with a command or a pointer.
enum class CarouselEvent : uint8_t {
    none,  ///< the carousel did not change
    moved, ///< the selected card changed
    picked ///< the selected card was picked
};

/// Returns the command a key asks of a carousel.
///
/// Left moves to the previous card, Right to the next, and Enter picks the
/// selected card. Any other key asks nothing. A host maps shoulders and a
/// stick onto CarouselCommand itself.
///
/// @param pressed the key
/// @return the command
[[nodiscard]] CarouselCommand carousel_key(Key pressed) noexcept;

/// Applies a command. Accept wins, then next, then previous.
///
/// @param[in,out] model the carousel
/// @param command what was asked
/// @return what the carousel did
CarouselEvent carousel_command(CarouselModel& model, CarouselCommand command) noexcept;

/// Where a finger is in a press on a carousel.
enum class PointerPhase : uint8_t {
    down, ///< the finger went down
    move, ///< the finger moved
    up,   ///< the finger came up
};

/// A press the carousel is following.
struct CarouselDrag {
    bool tracking{};  ///< a press is down
    Point from{};     ///< where it went down, in points
    Point at{};       ///< where it is now, in points
    int32_t card{-1}; ///< the card it went down on; -1 for none
    bool previous{};  ///< it went down on the previous arrow
    bool next{};      ///< it went down on the next arrow
};

/// Follows a finger on a carousel.
///
/// A swipe of carousel_swipe_threshold points or more moves one card: a
/// finger that travels left selects the next card, and right the previous.
/// A short press on a side card centres it. A short press on the centre
/// card, or on the card already selected while it is still on its way there,
/// picks it. A short press on an arrow moves one card.
///
/// @param[in,out] model the carousel
/// @param places the cards as carousel_places laid them, kept alive by the caller
/// @param previous the previous arrow; empty when there is none
/// @param next the next arrow; empty when there is none
/// @param[in,out] drag the press
/// @param phase where the finger is
/// @param at the finger, in the same points as the places
/// @return what the carousel did; none until the finger comes up
CarouselEvent carousel_pointer(
    CarouselModel& model,
    const std::vector<CarouselSlot>& places,
    const Rect& previous,
    const Rect& next,
    CarouselDrag& drag,
    PointerPhase phase,
    Point at
) noexcept;

// ---- Language card ----

/// A language card and, when it has one, the progress bar under it.
struct LanguageCardLayout {
    Rect card{};     ///< the card
    Rect progress{}; ///< the bar under it; empty when the card shows none
};

/// Places a language card at the size class's card, and a progress bar under it.
///
/// The bar is the kit's progress bar. It is not part of the card.
///
/// @param at the card's top left, in points
/// @param metrics the size class's metrics
/// @param progress true leaves room for a bar under the card
/// @return the card and the bar
[[nodiscard]] LanguageCardLayout
language_card_layout(Point at, const Metrics& metrics, bool progress) noexcept;

/// Draws a language card into a rectangle.
///
/// The design's card is 232 by 304 points. Padding, the badge and the gap
/// under it scale from that card to this rectangle. The badge is centred at
/// the top: the pack's picture, or the letters on a grey ground, accent
/// while the card is active. Under it, centred, come the language's own
/// name, its English name and the facts line. The own name is large. Where
/// the modern fonts are open it is drawn at about 27 points on the design
/// card, in the endonym face for a name the sans faces lack, and shrunk
/// until it fits before an ellipsis. In the game's bitmap fonts it uses the
/// regular font, and the same shrink. The pill is centred at the bottom. An
/// active card has an accent border and a one-point accent ring outside it.
/// Offline, and a disabled card, are dimmed into the panel. The rectangle is
/// clipped: a gallery sample may be smaller than the metric card.
///
/// @param canvas where it is drawn
/// @param rect the card
/// @param look its words and state
void draw_language_card(const Canvas& canvas, const Rect& rect, const LanguageCardLook& look);

/// Draws one centred line, large in the modern fonts and shrunk until it fits.
///
/// On the design's card the own name is about 27 points. A title uses 32 and
/// shrinks the same way. In the game's bitmap fonts it uses the regular font.
///
/// @param canvas where it is drawn
/// @param text the line
/// @param box the box the line is centred in
/// @param ink the colour
void draw_prominent_line(const Canvas& canvas, std::string_view text, const Rect& box, Colour ink);

/// Appends a language card: one item and, with a control number, one control
/// of kind button that does not take steps.
///
/// @param[in,out] list the display list
/// @param rect the card
/// @param look its look
/// @param id the control's number, or no_control for the item alone
/// @param name the control's automation name
void add_language_card(
    DisplayList& list,
    const Rect& rect,
    const LanguageCardLook& look,
    ControlId id,
    std::string name
);

// ---- Carousel drawing ----

/// Draws a carousel: each card turned into its place, then the pager.
///
/// Cards are drawn at card_width by card_height and copied into their places,
/// so a card that has fallen back is smaller and turned. The copy is the
/// canvas's perspective blit. The pager is one bar a card, and the bar of the
/// card nearest centre_milli is lit. The arrows are not part of the carousel.
///
/// @param canvas where it is drawn
/// @param rect the band and the pager
/// @param look the cards and where the centre sits
void draw_carousel(const Canvas& canvas, const Rect& rect, const CarouselLook& look);

/// Appends a carousel: one item and no control. The caller adds a control for
/// each card that shows, and for the arrows.
///
/// @param[in,out] list the display list
/// @param rect the band and the pager
/// @param look the cards and where the centre sits
void add_carousel(DisplayList& list, const Rect& rect, const CarouselLook& look);

/// Appends a control over one carousel card or arrow. Left and Right step the
/// carousel instead of moving the focus. The carousel's own item draws the card.
///
/// @param[in,out] list the display list
/// @param rect where a press lands
/// @param id the control's number
/// @param name the control's automation name
/// @param text the card's or the arrow's words, for automation
/// @param enabled false while a press does not reach it
void add_carousel_control(
    DisplayList& list,
    const Rect& rect,
    ControlId id,
    std::string name,
    std::string text,
    bool enabled
);

// ---- Step indicator ----

/// The step line's pattern. A caller looks this up whole, then the kit fills
/// `{step}`, `{index}` and `{count}`. A translation keeps the places and may
/// move them. The English line is the same shape as before, such as
/// "WELCOME · 1 OF 3".
inline constexpr std::string_view step_line_pattern = "{step} · {index} OF {count}";

/// Fills a step indicator's line from its pattern.
///
/// An empty pattern is `step_line_pattern`. The name and the pattern are
/// already looked up; this only writes the places.
///
/// @param look the name, the pattern and the step
/// @return the line, such as "LANGUAGE · 1 OF 3"
[[nodiscard]] std::string step_line(const StepLook& look);

/// Returns a step indicator's width: its letter-spaced line, or its segments,
/// whichever is wider.
///
/// @param fonts the fonts
/// @param look the step's name and how many steps
/// @return the width, in points
[[nodiscard]] int32_t step_indicator_width(const Fonts& fonts, const StepLook& look);

/// Returns a step indicator's height: the small line, a gap and the segments.
///
/// @return the height, in points
[[nodiscard]] constexpr int32_t step_indicator_height() noexcept {
    return compact_metrics.small_line + 2 + 3;
}

/// Draws a step indicator: the filled pattern in the small font,
/// letter-spaced, and one segment a step, lit from the first up to the current.
///
/// @param canvas where it is drawn
/// @param rect the indicator
/// @param look the name, the pattern and the step
void draw_step_indicator(const Canvas& canvas, const Rect& rect, const StepLook& look);

/// Appends a step indicator: one item and no control.
///
/// @param[in,out] list the display list
/// @param rect the indicator
/// @param look the name and the step
void add_step_indicator(DisplayList& list, const Rect& rect, const StepLook& look);

// ---- Setup chrome ----

/// Where the setup footer's three buttons sit, in the window's points.
struct SetupButtons {
    Rect skip{}; ///< the quiet button on the left
    Rect back{}; ///< the plain button
    Rect next{}; ///< the accent button on the right
};

/// Places Skip setup, Back and Next in the footer, each at least button_width
/// and wide enough for its caption.
///
/// @param fonts the fonts; null measures the captions as button_width
/// @param look the window and the buttons' words
/// @return the three buttons
[[nodiscard]] SetupButtons setup_buttons(const Fonts* fonts, const SetupChromeLook& look);

/// Draws the setup window from the canvas's origin: the face, the header
/// (the mark, "Open Annihilation", the page word and the step indicator at
/// the right), the footer, Skip setup, Back and Next, and the edge.
///
/// @param canvas where it is drawn; its origin is the window's top left
/// @param look the window, the page and the buttons
void draw_setup_chrome(const Canvas& canvas, const SetupChromeLook& look);

/// Appends the setup window: one item and no controls. The caller adds the
/// buttons' controls in the window's points.
///
/// @param[in,out] list the display list
/// @param origin the window's top left, in the list's points
/// @param look the window, the page and the buttons
void add_setup_chrome(DisplayList& list, Point origin, const SetupChromeLook& look);

/// Appends the three setup buttons' controls, in the window's points shifted
/// by the origin. They do not take steps. The window's item draws the buttons.
///
/// @param[in,out] list the display list
/// @param origin the window's top left, in the list's points
/// @param buttons where the buttons sit in the window
/// @param look the buttons' words and which of them take a press
/// @param skip_id the quiet button's number
/// @param skip_name its automation name
/// @param back_id the plain button's number
/// @param back_name its automation name
/// @param next_id the accent button's number
/// @param next_name its automation name
void add_setup_controls(
    DisplayList& list,
    Point origin,
    const SetupButtons& buttons,
    const SetupChromeLook& look,
    ControlId skip_id,
    std::string skip_name,
    ControlId back_id,
    std::string back_name,
    ControlId next_id,
    std::string next_name
);

// ---- Choice card ----

/// Returns a choice card's height. It is the same in every size class.
///
/// @return the height, in points
[[nodiscard]] constexpr int32_t choice_card_height() noexcept {
    return 6 + compact_metrics.regular_line + compact_metrics.hint_gap +
           compact_metrics.small_line + 6;
}

/// Returns the row an arrow key moves to in a group of choice cards.
///
/// Up and Left move toward the first, Down and Right toward the last, and
/// the ends stay put. Any other key stays on index.
///
/// @param index the row that has the focus, from 0
/// @param count how many rows
/// @param pressed the key
/// @return the row to move to, held inside the group
[[nodiscard]] int32_t choice_step(int32_t index, int32_t count, Key pressed) noexcept;

/// Draws a choice card: a radio, a title in the regular font and a hint in
/// the small font. Chosen fills the radio and draws the accent border.
/// Disabled draws no accent.
///
/// @param canvas where it is drawn
/// @param rect the card
/// @param look its words and state
void draw_choice_card(const Canvas& canvas, const Rect& rect, const ChoiceCardLook& look);

/// Appends a choice card: one item and, with a control number, one control
/// of kind button that does not take steps, checked while chosen.
///
/// @param[in,out] list the display list
/// @param rect the card
/// @param look its look
/// @param id the control's number, or no_control for the item alone
/// @param name the control's automation name
void add_choice_card(
    DisplayList& list, const Rect& rect, const ChoiceCardLook& look, ControlId id, std::string name
);

// ---- Coach mark and spotlight ----

/// Places a coach card so it does not cover its target.
///
/// The card is tried on the right, then the left, then below, then above,
/// coach_gap points away. A side that would leave the room, or land on the
/// target, is passed over. When none fits, the card is held inside the room.
///
/// @param room the rectangle the card must lie in
/// @param target the rectangle the card must not cover
/// @param card_width the card's width, in points
/// @param card_height the card's height, in points
/// @return where the card sits
[[nodiscard]] Rect place_coach_mark(
    const Rect& room, const Rect& target, int32_t card_width, int32_t card_height
) noexcept;

/// Draws a coach mark: an accent border, the number and the title, the body
/// wrapped in the small font, and a one-point accent line from the card to
/// the target.
///
/// @param canvas where it is drawn
/// @param rect the card
/// @param look the number, the words and the target
void draw_coach_mark(const Canvas& canvas, const Rect& rect, const CoachMarkLook& look);

/// Appends a coach mark: one item and no control. The rectangle is the card.
///
/// @param[in,out] list the display list
/// @param rect the card
/// @param look the number, the words and the target
void add_coach_mark(DisplayList& list, const Rect& rect, const CoachMarkLook& look);

/// Draws a spotlight. The layer dims to spotlight_opacity except the cut-out,
/// which keeps an accent ring.
///
/// @param canvas where it is drawn
/// @param layer the rectangle that dims
/// @param look the cut-out
void draw_spotlight(const Canvas& canvas, const Rect& layer, const SpotlightLook& look);

/// Appends a spotlight: one item and no control. The rectangle is the layer.
///
/// @param[in,out] list the display list
/// @param layer the rectangle that dims
/// @param look the cut-out
void add_spotlight(DisplayList& list, const Rect& layer, const SpotlightLook& look);

// ---- Picture well ----

/// A picture well's border, the rectangle a drawing is scaled into, and its caption.
struct PictureWellLayout {
    Rect border{};  ///< the bordered rectangle
    Rect inset{};   ///< where the caller scales a drawing; empty when it has no room
    Rect caption{}; ///< the line under the border; empty when the look has none
};

/// Places a picture well in a rectangle: a one-point border, an inset of
/// picture_well_inset, and the caption under the border when the look has one.
///
/// @param rect the well and its caption
/// @param caption true leaves a line under the border
/// @return the border, the inset and the caption
[[nodiscard]] PictureWellLayout picture_well_layout(const Rect& rect, bool caption) noexcept;

/// Draws a picture well. The inset is left clear unless the look asks for a
/// stand-in, which fills it with a few rectangles and no picture file. The
/// caller scales a drawing into the inset with scale_blit.
///
/// @param canvas where it is drawn
/// @param rect the well and its caption
/// @param look the caption and whether a stand-in is drawn
void draw_picture_well(const Canvas& canvas, const Rect& rect, const PictureWellLook& look);

/// Appends a picture well: one item and no control.
///
/// @param[in,out] list the display list
/// @param rect the well and its caption
/// @param look the caption and whether a stand-in is drawn
void add_picture_well(DisplayList& list, const Rect& rect, const PictureWellLook& look);

// ---- Call to action ----

/// Draws a call to action.
///
/// Enabled, it is an accent button brightened to accent_light, and a ring
/// that grows and fades over call_to_action_period_ms. Reduced motion holds
/// a steady ring. Disabled, it is the plain button's disabled face and no ring.
/// The ring is drawn a few points outside the button.
///
/// @param canvas where it is drawn
/// @param rect the button
/// @param look its caption, its state and how long the pulse has run
void draw_call_to_action(const Canvas& canvas, const Rect& rect, const CallToActionLook& look);

/// Appends a call to action: one item and, with a control number, one control
/// of kind button that does not take steps.
///
/// @param[in,out] list the display list
/// @param rect the button
/// @param look its look
/// @param id the control's number, or no_control for the item alone
/// @param name the control's automation name
void add_call_to_action(
    DisplayList& list,
    const Rect& rect,
    const CallToActionLook& look,
    ControlId id,
    std::string name
);

} // namespace oa::ui::kit
