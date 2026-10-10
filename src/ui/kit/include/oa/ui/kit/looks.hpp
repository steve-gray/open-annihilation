// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Provisional: this header changes whenever OA's screens need it to, until the kit is declared stable (src/ui/kit/README.md).

// How a kit component looks. A look owns its texts, so a display list
// outlives the words it was built from. Nothing here draws, and nothing
// here looks a word up: a caller passes the caption it wants shown.
#pragma once

#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/kit/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace oa::ui::kit {

/// A rectangle in points. One point is one pixel of the 640 by 480 picture.
/// The same rectangle the settings geometry uses, so it passes straight through.
using Rect = oa::ui::frontend_renderer::SourceRect;

/// A one-bit mark the kit draws.
enum class Mark : uint8_t {
    oa_small,     ///< the OA letters, 9 by 7
    oa_large,     ///< the OA letters, 12 by 9
    padlock,      ///< the lock's padlock, 5 by 7
    arrow_closed, ///< an arrow pointing right, 5 by 5
    arrow_open,   ///< an arrow pointing down, 5 by 5
    choice_arrow, ///< a drop-down's arrow, 7 by 4
};

/// Which of the five button looks a button is drawn in.
enum class ButtonStyle : uint8_t {
    accent, ///< the green button: OK, SWITCH, a notice's OK, MANAGE…
    plain,  ///< no face at rest: Restore defaults, Cancel, a notice's other button
    quiet,  ///< no face at rest, and the band while held: Your files, OPEN MODS FOLDER
    inset,  ///< the list's face at rest: a mod row's ROLL BACK
};

/// A button's caption and which of its looks to draw.
struct ButtonLook {
    std::string caption{};                 ///< the words, already looked up
    ButtonStyle style{ButtonStyle::plain}; ///< which look
    bool hovered{};                        ///< the pointer is over it, as the caller counts that
    bool held{};                           ///< a press is held on it with the pointer over it
    bool enabled{true};                    ///< false draws the plain look's disabled face
};

/// An Off/On switch. The captions are already looked up.
struct SwitchLook {
    bool on{};                 ///< the switch is On
    bool hovered{};            ///< the pointer is over it
    bool locked{};             ///< it cannot be changed: On shows without the accent
    std::string off_caption{}; ///< the Off half's words
    std::string on_caption{};  ///< the On half's words
};

/// A strip of levels. captions are already looked up, one per level from the left.
struct LevelsLook {
    std::vector<std::string> captions{}; ///< each level's words
    int32_t level_width{};               ///< each level's columns, inside the 1-pixel border
    std::size_t chosen{};                ///< the chosen level's index
    /// How many levels from the left can be chosen. The rest are faded. The
    /// caller passes the count, never a sentinel.
    std::size_t offered{};
    bool hovered{}; ///< the pointer is over the strip
    bool locked{};  ///< it cannot be changed: the chosen level shows without the accent
};

/// A slider's knob on its track.
struct SliderLook {
    int32_t stop{};  ///< the stop the knob is on
    int32_t stops{}; ///< the stops, 2 or more
    bool locked{};   ///< the knob cannot be moved
    bool hovered{};  ///< the pointer is over it, or holds it
};

/// A drop-down's closed field.
struct ChoiceLook {
    std::string text{}; ///< the choice it shows, already looked up
    bool hovered{};     ///< the pointer is over it, or holds it
    bool open{};        ///< its menu is open
};

/// A drop-down's open menu. shown holds the items on screen, already looked up,
/// the first of them being item first of total.
struct ChoiceMenuLook {
    std::vector<std::string> shown{}; ///< the items on screen, top to bottom
    int32_t first{};                  ///< the index of shown's first item
    int32_t total{};                  ///< how many choices the menu offers
    int32_t chosen{};                 ///< the chosen choice's index
    int32_t marked{};                 ///< the item the pointer or the keys mark
    bool focus_shown{};               ///< the keyboard focus outlines the marked item
};

/// A lock's text, already looked up. The padlock is drawn with it.
struct LockLook {
    std::string text{}; ///< the words
};

/// A scroll bar's thumb, and whether the pointer is on the bar or holds it.
struct ScrollBarLook {
    Rect thumb{}; ///< the thumb
    bool hot{};   ///< the pointer is over the bar, or a press on it is held
};

/// A one-bit mark's picture and colour.
struct MarkLook {
    Mark mark{};     ///< which mark
    Colour colour{}; ///< the colour of its set pixels
};

/// How the OA button looks.
enum class OaButtonState : uint8_t {
    idle,    ///< at rest
    hovered, ///< the pointer is over it
    pressed, ///< a press on it is held
};

/// The OA button's side and how it looks. It is drawn at the item's top left.
struct OaButtonLook {
    int32_t side{};                           ///< the square's side, in points
    OaButtonState state{OaButtonState::idle}; ///< how it looks
};

/// Where the keyboard focus outline is drawn.
struct FocusRingLook {
    bool around{true}; ///< false draws it on the control's own edge
};

/// What a component item draws. Empty for a generic role. Later components
/// append looks.
using Look = std::variant<
    std::monostate,
    ButtonLook,
    SwitchLook,
    LevelsLook,
    SliderLook,
    ChoiceLook,
    ChoiceMenuLook,
    LockLook,
    ScrollBarLook,
    MarkLook,
    OaButtonLook,
    FocusRingLook>;

} // namespace oa::ui::kit
