// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's screens, built only from the UI kit's components: the list
// and details laid out at the three size classes (a list page and a details
// page of its own at Compact, list and details side by side at Regular, and
// filters, list and details at Large), their drawing, and the pointer,
// finger, keys, typed text, wheel and controller that drive them. Nothing
// here hosts the screen, fetches or installs: a host lays it out for its
// frame, draws it, passes its input in and acts on the actions it returns.
// Its words are in text.hpp.
#pragma once

#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/looks.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/library/library.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace oa::ui::library {

/// One of the Library's drop-downs, which can be open.
enum class Menu : uint8_t {
    none,   ///< no drop-down is open
    filter, ///< Compact's filter: All, Installed, Updates, then each tag of the tab (library.filter)
    tags,   ///< the tags that do not fit as chips or in the filter pane (library.tags)
    more,   ///< the Compact details page's actions after its first three (library.action)
};

/// One of the Library's two panes that scroll.
enum class Pane : uint8_t {
    none, ///< neither
    list, ///< the list of entries
    details, ///< the selected entry's details: the pane at Regular and Large, the page's body at Compact
};

/// A finger's press in a pane, which becomes a drag that scrolls the pane
/// once the finger moves farther than its reach.
struct Drag {
    Pane pane{Pane::none}; ///< the pane the finger pressed in; none while no finger presses one
    kit::Point from{};     ///< where the finger pressed, in points
    int32_t offset{};      ///< how far the pane was scrolled when it pressed, in points
    bool scrolling{};      ///< it moved past the finger's reach: it scrolls and presses nothing
};

/// What the Library's screen keeps between its inputs, beside the model.
struct ScreenState {
    kit::Interaction interaction{};  ///< the hover, the press and the focus
    int32_t list_scroll{};           ///< how far the list is scrolled, in points
    int32_t details_scroll{};        ///< how far the details are scrolled, in points
    kit::WheelCarry list_wheel{};    ///< the wheel's turn the list has not scrolled yet
    kit::WheelCarry details_wheel{}; ///< the wheel's turn the details have not scrolled yet
    /// The search field's text and caret. Its text is the model's query: the
    /// input keeps them the same.
    kit::TextField search{};
    Menu menu{Menu::none}; ///< the open drop-down
    int32_t menu_first{};  ///< the open drop-down's first item shown, from 0
    int32_t menu_marked{}; ///< the open drop-down's item the keys or the pointer mark, from 0
    /// Compact shows the selected entry's details page. It shows while this
    /// and the model's details_open are both set: a refresh that closes the
    /// model's details closes the page too.
    bool details_page{};
    Drag drag{}; ///< a finger's press in a pane
};

/// What one input asked of the host.
struct ScreenResult {
    /// What the host is to do: an entry's action, UPDATE ALL, SETTINGS… or
    /// CLOSE. None when the screen did all of it itself, as it does for
    /// browsing, DETAILS and the way back from the details page.
    std::optional<Action> action{};
    EntryId entry{}; ///< the entry an entry's action is for; empty for the others
    bool redraw{};   ///< what the screen shows changed: lay it out and draw it again
};

/// What a pointer did, as the OA layer passes it to a screen.
enum class PointerEvent : uint8_t {
    move, ///< it moved
    down, ///< a button or a finger pressed
    up,   ///< a button or a finger let go
};

/// The Library's own keys and controller buttons, beyond the kit's keys. A
/// host maps the platform's keys and buttons to these. The controller's
/// D-pad and left stick are the kit's arrows, its A is Space, and its right
/// stick is library_stick.
enum class Command : uint8_t {
    find,         ///< Ctrl+F (Cmd+F on macOS), `/` outside the search, and the controller's Y
    next_tab,     ///< Ctrl+Tab and the controller's RB
    previous_tab, ///< Ctrl+Shift+Tab and the controller's LB
    back,         ///< the controller's B: from the details page back to the list, else close
    /// The controller's X: the selected entry's first action when it is GET,
    /// UPDATE, CANCEL or RETRY and enabled.
    first_action,
};

/// Returns the Library's window size for a frame: its size class's dialog
/// size (480 by 324, 720 by 486 or 960 by 600 points), no larger than the
/// frame's area. A host places the window by it.
///
/// @param frame the room the screen lays out in
/// @return the width and height, in points
[[nodiscard]] kit::Point window_size(const kit::Frame& frame) noexcept;

/// Lays the Library out for a frame, from the window's top left corner.
///
/// Compact shows the list page, or the selected entry's details page
/// (details_page_layout) while the state and the model both say it shows.
/// Regular shows the list and the details side by side, and Large the
/// filters, the list and the details. Every control carries its automation
/// name (library.…). The layout reads the model's visible entries, its
/// entries' actions, facts, status lines and bylines, the playing note and
/// the queue lines, and decides no rule of its own.
///
/// @param library the model; the display list refers to its badges' pixels
///     until its next refresh
/// @param state the hover, press, focus, scroll, search and open drop-down
/// @param frame the room, its size class and its scale
/// @param fonts the fonts its texts are measured in
/// @return what the screen draws and where its controls are
[[nodiscard]] kit::DisplayList library_layout(
    const Library& library,
    const ScreenState& state,
    const kit::Frame& frame,
    const kit::Fonts& fonts
);

/// Lays out Compact's details page of the selected entry: its name and
/// version in the header; the way back to the list, the status line, the
/// byline, the summary, the short facts and the playing note; and the first
/// three actions, MORE ▾ with the rest and CLOSE in the footer.
///
/// @param library the model, with an entry selected
/// @param state the hover, press, focus, scroll and open drop-down
/// @param frame the room; the page takes its class's window size
/// @param fonts the fonts its texts are measured in
/// @return what the page draws and where its controls are
[[nodiscard]] kit::DisplayList details_page_layout(
    const Library& library,
    const ScreenState& state,
    const kit::Frame& frame,
    const kit::Fonts& fonts
);

/// Draws a laid-out Library through the kit's painter (kit::paint).
///
/// @param canvas where it is drawn, from the window's top left corner
/// @param list the display list library_layout made
void draw_library(const kit::Canvas& canvas, const kit::DisplayList& list);

/// Returns the letters a badge shows when its package has no picture: a
/// name of one word gives its first three characters, a name of more its
/// words' first characters, at most three; letters a to z in capitals.
///
/// @param name the name, in UTF-8
/// @return the letters, such as "RID" for "Ridge" and "TZ" for "TA Zero"
[[nodiscard]] std::string badge_letters(std::string_view name);

/// Returns the search field while it has the focus, over which a host keeps
/// the system's text input started.
///
/// @param state the focus
/// @param list the display list
/// @return the field's rectangle, in points; none while it has not the focus
[[nodiscard]] std::optional<kit::Rect>
focused_field(const ScreenState& state, const kit::DisplayList& list) noexcept;

/// Takes a pointer's move, press or release, in the window's points.
///
/// A press on a row selects it, and at Compact opens its details page; no
/// press on a row gets, updates or installs anything. A finger's press
/// reaches the nearest control within its reach; one in the list or the
/// details that moves farther than its reach scrolls that pane instead. A
/// press outside an open drop-down closes it.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @param list the display list the screen shows now
/// @param kind a move, a press or a release
/// @param button the pointer's button, 1 for the left; 0 for a move
/// @param at the pointer, in the window's points; it may lie outside the window
/// @param finger_reach how far a finger's press may lie from a control, in
///     points; 0 for a mouse
/// @return what the host is to do
ScreenResult library_pointer(
    Library& library,
    ScreenState& state,
    const kit::DisplayList& list,
    PointerEvent kind,
    uint8_t button,
    kit::Point at,
    int32_t finger_reach
);

/// Takes a key, as the kit names it.
///
/// Tab and Shift+Tab follow the declared order: the tabs, the search, the
/// filters, the list, the details' actions and the footer's buttons. The
/// arrows move by where controls sit. On the list, Up and Down move the
/// selection and keep the focus there, but at its first or last entry the
/// focus leaves it; Page Up and Page Down move by the rows shown, and Home
/// and End go to its ends. Enter on the list opens the details page at
/// Compact, and moves the focus to the first enabled action at Regular and
/// Large. Escape clears a search that has the focus and holds words, else
/// leaves the details page, else closes. An open drop-down takes Up, Down,
/// Home, End, Enter, Space and Escape.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @param list the display list the screen shows now
/// @param pressed the key
/// @return what the host is to do
ScreenResult
library_key(Library& library, ScreenState& state, const kit::DisplayList& list, kit::Key pressed);

/// Takes typed text into the search while it has the focus, and searches
/// for it as it is typed.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @param list the display list the screen shows now
/// @param utf8 the typed text, in UTF-8
/// @return what the host is to do
ScreenResult library_text(
    Library& library, ScreenState& state, const kit::DisplayList& list, std::string_view utf8
);

/// Takes a turn of the wheel: it scrolls the pane under the pointer, or the
/// open drop-down's items.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @param list the display list the screen shows now
/// @param at the pointer, in the window's points
/// @param notches the turn; positive away from the player, towards the top
/// @return what the host is to do
ScreenResult library_wheel(
    Library& library, ScreenState& state, const kit::DisplayList& list, kit::Point at, float notches
);

/// Takes one of the Library's own keys or controller buttons.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @param list the display list the screen shows now
/// @param command what was pressed
/// @return what the host is to do
ScreenResult library_command(
    Library& library, ScreenState& state, const kit::DisplayList& list, Command command
);

/// Takes the controller's right stick: it scrolls the pane that holds the
/// focus, the details while the focus is on one of their controls and the
/// list otherwise.
///
/// @param[in,out] library the model
/// @param[in,out] state the screen's state
/// @param list the display list the screen shows now
/// @param notches the stick's turn, as notches of the wheel; positive towards the top
/// @return what the host is to do
ScreenResult
library_stick(Library& library, ScreenState& state, const kit::DisplayList& list, float notches);

} // namespace oa::ui::library
