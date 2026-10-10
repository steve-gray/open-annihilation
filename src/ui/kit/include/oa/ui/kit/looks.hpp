// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Provisional: this header changes whenever OA's screens need it to, until the kit is declared stable (src/ui/kit/README.md).

// How a kit component looks. A look owns its texts, so a display list
// outlives the words it was built from. Nothing here draws, and nothing
// here looks a word up: a caller passes the caption it wants shown.
#pragma once

#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/kit/chrome.hpp"
#include "oa/ui/kit/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
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
    magnifier,    ///< a search field's magnifier, 7 by 7
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

/// The text a player types into a field, and where the caret stands.
///
/// The text is UTF-8. The caret counts bytes from the text's start and stands
/// on a character's first byte or at the text's end. insert_text and
/// edit_text (input.hpp) keep both so.
struct TextField {
    std::string text{};  ///< the player's own text, in UTF-8; never looked up
    std::size_t caret{}; ///< the caret, in bytes from the start, on a character boundary
};

/// What a chip says, which chooses its colours.
enum class ChipState : uint8_t {
    get,       ///< can be fetched: the hint on the hover border
    installed, ///< is in place: the accent
    update,    ///< has an update: the lock colour
    playing,   ///< is in use: filled with the accent
    online,    ///< is on the network: the online colour
    problem,   ///< something is wrong: the danger colour
    filter,    ///< a filter the player turns on and off: the list's selected face while on
};

/// A chip: a short text in a box, in the colours of its state. The text is
/// drawn as given, already looked up and in the case the caller wants.
struct ChipLook {
    std::string text{};              ///< the words
    ChipState state{ChipState::get}; ///< which colours
    bool selected{};                 ///< a filter that is on
    bool hovered{};                  ///< the pointer is over it, or holds it
    bool disabled{};                 ///< it cannot act: drawn in the idle colour whatever its state
};

/// A row of a list, in its full or its short form, which its rectangle's
/// height chooses. Every text is already looked up.
struct ListRowLook {
    /// The badge's letters, drawn on badge_colour when there is no picture.
    std::string badge_text{};
    Colour badge_colour{}; ///< the badge's face behind its letters
    /// The badge's picture. It refers to pixels the screen keeps, which must
    /// outlive the display list. Empty, the letters or a blank badge show.
    oa::ui::frontend_renderer::RgbaPicture badge{};
    std::string title{};              ///< the title, in the regular font
    std::string by{};                 ///< the byline after the title, in the small font
    std::vector<std::string> lines{}; ///< the lines under the title, top to bottom
    std::vector<std::string> aside{}; ///< the entries at the right, one a line from the top
    std::optional<ChipLook> chip{};   ///< the chip at the right; none when unset
    std::vector<ChipLook> tags{};     ///< chips on the line after the last line
    bool selected{};                  ///< the row is the chosen one: the accent tint and outline
    bool hovered{};                   ///< the pointer is over it, or holds it
};

/// A text field: the plain one, or with the magnifier the search field.
struct SearchLook {
    /// Shown in the hint colour while the text is empty, already looked up.
    std::string placeholder{};
    TextField field{};    ///< the text and the caret
    bool focused{};       ///< the field takes the keys: its caret shows
    bool hovered{};       ///< the pointer is over it
    bool magnifier{true}; ///< the magnifier before the text, as the search field has
};

/// A strip of tabs, left to right. Captions are already looked up.
struct TabsLook {
    std::vector<std::string> captions{};  ///< each tab's words
    std::vector<int32_t> counts{};        ///< each tab's count; 0, or none given, draws no count
    std::size_t selected{};               ///< the open tab's index
    std::optional<std::size_t> hovered{}; ///< the tab under the pointer; none when unset
};

/// A card of a grid: a square preview, its chips, a title and a subtitle.
struct CardLook {
    /// The preview's picture. It refers to pixels the screen keeps, which
    /// must outlive the display list. Empty, the placeholder shows.
    oa::ui::frontend_renderer::RgbaPicture picture{};
    std::string placeholder{};     ///< the words on the empty preview, already looked up
    std::string title{};           ///< the title, already looked up
    std::string subtitle{};        ///< the line under it, already looked up
    std::vector<ChipLook> chips{}; ///< chips at the preview's bottom left, left to right
    bool selected{};               ///< the chosen card: an accent outline
    bool hovered{};                ///< the pointer is over it: a lighter outline
    /// Faded into the panel, as a locked row is. It still takes a press.
    bool greyed{};
};

/// An edge of a rectangle.
enum class Side : uint8_t {
    none,   ///< no edge
    left,   ///< the left edge
    right,  ///< the right edge
    top,    ///< the top edge
    bottom, ///< the bottom edge
};

/// One line of a hover card: a label and its value.
struct HoverCardRow {
    /// The label, already looked up. Empty, the value is drawn from the left.
    std::string label{};
    std::string value{};               ///< the value, already looked up
    Colour value_colour{colour::text}; ///< the value's colour
};

/// A hover card: a header, label and value rows, a block of lines and an
/// arrow pointing at what it describes. Every text is already looked up.
struct HoverCardLook {
    int32_t width{};                  ///< the card's width, in points; 0 fits its widest line
    bool mark{};                      ///< the small OA mark starts the header
    std::string title{};              ///< the header's title
    std::string aside{};              ///< the header's words at the right
    std::vector<HoverCardRow> rows{}; ///< the rows under the header
    std::vector<std::string> block{}; ///< the quiet lines under the rows
    Side arrow{Side::none};           ///< the edge the arrow leaves from; none draws no arrow
    /// The arrow's point, in points along that edge from its start.
    int32_t arrow_at{};
};

/// A link: a caption that acts when pressed.
struct LinkLook {
    std::string caption{}; ///< the words, already looked up
    bool hovered{};        ///< the pointer is over it, or holds it
    bool enabled{true};    ///< false draws it in the idle colour, and it takes no press
};

/// What a component item draws. Empty for a generic role. Later components
/// append looks. A locked fade has no look of its own: its rectangle is the
/// item's.
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
    FocusRingLook,
    HeaderLook,
    FooterBandLook,
    NavLook,
    HeadingLook,
    RowFrame,
    ListRowLook,
    ChipLook,
    SearchLook,
    TabsLook,
    CardLook,
    HoverCardLook,
    LinkLook>;

} // namespace oa::ui::kit
