// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The download check: a kit window that shows a registry's code and page
// address while a download waits for the player to confirm they are a
// person. The words are plain strings. The host opens the page, retries and
// cancels; this window only says which of those a press asked for.
#pragma once

#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/text.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace oa::ui::kit {
struct Canvas;
}

namespace oa::ui::engine_settings {

/// The code, drawn in the kit's largest font with this many extra columns
/// after each glyph but the last.
inline constexpr int32_t challenge_code_tracking = 2;

/// The framed code. A label: an area that is neither enabled nor focusable.
inline constexpr oa::ui::kit::ControlId challenge_code_control = 1;
/// The line under the code. A label, as the code is.
inline constexpr oa::ui::kit::ControlId challenge_status_control = 2;
/// OPEN THE CHECK, shown while the check waits and a browser can be opened.
inline constexpr oa::ui::kit::ControlId challenge_open_control = 3;
/// TRY AGAIN, shown once the check has run out.
inline constexpr oa::ui::kit::ControlId challenge_try_again_control = 4;
/// CANCEL, which is always shown.
inline constexpr oa::ui::kit::ControlId challenge_cancel_control = 5;

/// One challenge, as the host last read it.
struct ChallengeWindow {
    /// What the window is waiting on. Passed is the moment the check has been
    /// accepted, before the host closes the window.
    enum class Status : uint8_t {
        waiting, ///< the registry is waiting for the check
        passed,  ///< the check was accepted
        expired, ///< the check ran out
    };

    std::string registry_name;      ///< the registry's name, as its descriptor gives it
    std::string code;               ///< the code the player enters
    std::string address;            ///< the page without its scheme and query
    Status status{Status::waiting}; ///< which words and buttons show
    bool can_open_browser{true};    ///< false hides OPEN THE CHECK and says to use another device
};

/// One button the window offers.
struct ChallengeButton {
    std::string caption{}; ///< the caption, in capitals
};

/// The words and buttons the window shows for one challenge.
struct ChallengeText {
    std::string title{};   ///< the header's title
    std::string heading{}; ///< the heading under the header
    std::string body{};    ///< the explanation, naming the registry and the page
    std::string code{};    ///< the code, as given
    std::string status{};  ///< the line under the code; empty once the check has passed
    std::vector<ChallengeButton> buttons{}; ///< CANCEL, then OPEN THE CHECK or TRY AGAIN
};

/// The window laid out, and where the pointer and the keys are on it.
struct ChallengeModel {
    ChallengeWindow window{}; ///< what it shows
    /// The size class it is laid out at. Its width and its height limits are
    /// that class's notice width and heights.
    oa::ui::kit::SizeClass size_class{oa::ui::kit::SizeClass::compact};
    oa::ui::kit::Interaction interaction{}; ///< the hover, the press and the focus
};

/// What a press on the window asks of its host.
enum class ChallengeAction : uint8_t {
    none,      ///< nothing
    redraw,    ///< only the look changed: a hover, a press or the focus
    cancel,    ///< CANCEL, or Escape
    open,      ///< OPEN THE CHECK
    try_again, ///< TRY AGAIN
};

/// Returns the words and buttons for a challenge.
///
/// The title is "Downloads" and the heading is "Check that you're a person".
/// With a browser the body says to open the check on this computer or to go
/// to the address on any device; without one it says to go to the address on
/// another device. While the check waits, the status says it updates by
/// itself. Once the check has run out, the status says to try again. Passed,
/// the status is empty. The buttons are CANCEL, and OPEN THE CHECK while the
/// check waits and a browser can be opened, or TRY AGAIN once it has run out.
///
/// @param window the challenge
/// @return the words and the buttons
[[nodiscard]] ChallengeText challenge_text(const ChallengeWindow& window);

/// Returns the button the keys start on: OPEN THE CHECK, or TRY AGAIN, or
/// CANCEL when that is the only button.
///
/// @param window the challenge
/// @return that button's number
[[nodiscard]] oa::ui::kit::ControlId
challenge_focus_control(const ChallengeWindow& window) noexcept;

/// Returns the window's width at a size class: that class's notice width.
///
/// @param size_class the class
/// @return the width, in points
[[nodiscard]] int32_t challenge_width(oa::ui::kit::SizeClass size_class) noexcept;

/// Returns the window's height: its text, the code and the status, from the
/// class's least notice height to its greatest.
///
/// @param window the challenge
/// @param size_class the class
/// @param fonts the fonts the text is measured in; null estimates each character
/// @return the height, in points
[[nodiscard]] int32_t challenge_height(
    const ChallengeWindow& window,
    oa::ui::kit::SizeClass size_class,
    const oa::ui::kit::Fonts* fonts = nullptr
);

/// Returns what the window draws and where its controls are.
///
/// The code and the status are labels (kind area, neither enabled nor
/// focusable) named challenge.code and challenge.status, their text the words
/// shown. The buttons are challenge.open, challenge.try-again and
/// challenge.cancel. The code's item uses the largest font, extra tracking,
/// the accent colour and centred alignment. Focus starts once
/// challenge_settle has run.
///
/// @param model the window and the pointer's place on it
/// @param fonts the fonts the text is measured in; null estimates each character
/// @return the display list
[[nodiscard]] oa::ui::kit::DisplayList
challenge_list(const ChallengeModel& model, const oa::ui::kit::Fonts* fonts = nullptr);

/// Puts the focus on OPEN THE CHECK, or TRY AGAIN, or CANCEL, and shows it.
///
/// @param[in,out] model the window; its focus changes
void challenge_settle(ChallengeModel& model);

/// Takes a key. Enter presses the focused button. Escape is CANCEL. The
/// arrows and Tab move the focus.
///
/// A pad's A and B reach the window as Enter and Escape: the host maps them
/// before this call.
///
/// @param[in,out] model the window; the focus moves when the key moves it
/// @param pressed the key
/// @param fonts the fonts the text is measured in; null estimates each character
/// @return what the host does
[[nodiscard]] ChallengeAction challenge_key(
    ChallengeModel& model, oa::ui::kit::Key pressed, const oa::ui::kit::Fonts* fonts = nullptr
);

/// Moves the pointer: the button under it lights.
///
/// @param[in,out] model the window
/// @param at the pointer, in points from the window's top left corner
/// @param fonts the fonts the text is measured in; null estimates each character
/// @return redraw when the look changed, otherwise none
[[nodiscard]] ChallengeAction challenge_pointer_move(
    ChallengeModel& model, oa::ui::kit::Point at, const oa::ui::kit::Fonts* fonts = nullptr
);

/// Presses the pointer's button.
///
/// @param[in,out] model the window
/// @param at the pointer, in points from the window's top left corner
/// @param fonts the fonts the text is measured in; null estimates each character
/// @return redraw when a button is held, otherwise none
[[nodiscard]] ChallengeAction challenge_pointer_down(
    ChallengeModel& model, oa::ui::kit::Point at, const oa::ui::kit::Fonts* fonts = nullptr
);

/// Releases the pointer. A release over the button it held presses that button.
///
/// @param[in,out] model the window
/// @param at the pointer, in points from the window's top left corner
/// @param fonts the fonts the text is measured in; null estimates each character
/// @return the button's action, redraw when the release missed it, otherwise none
[[nodiscard]] ChallengeAction challenge_pointer_up(
    ChallengeModel& model, oa::ui::kit::Point at, const oa::ui::kit::Fonts* fonts = nullptr
);

/// Presses where a finger landed. A finger within reach of a button holds
/// that button; the later release is moved by the same distance.
///
/// @param[in,out] model the window
/// @param at the finger, in points from the window's top left corner
/// @param reach how far a button may lie from the finger, in points
/// @param fonts the fonts the text is measured in; null estimates each character
/// @return redraw when a button is held, otherwise none
[[nodiscard]] ChallengeAction challenge_finger_down(
    ChallengeModel& model,
    oa::ui::kit::Point at,
    int32_t reach,
    const oa::ui::kit::Fonts* fonts = nullptr
);

/// Draws the window. The code is drawn with its tracking; the kit's painter
/// does not carry a text item's tracking, so the code is drawn after the rest.
///
/// @param canvas where it is drawn
/// @param model the window and the pointer's place on it
void draw_challenge(const oa::ui::kit::Canvas& canvas, const ChallengeModel& model);

} // namespace oa::ui::engine_settings
