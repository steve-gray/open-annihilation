// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The map browser's input: the pointer and a finger, the kit's keys, typed
// text and the wheel. It reads where the controls lie from the display list
// it is given, and changes the model's filters and selection; what the host
// is to do comes back as a ScreenResult.
#include "screen_controls.hpp"

#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/theme.hpp"
#include "oa/ui/map_browser/model.hpp"
#include "oa/ui/map_browser/screen.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <stdint.h>
#include <string_view>
#include <tuple>
#include <vector>

namespace oa::ui::map_browser {
namespace {

namespace ids = controls;

/// How far a mouse's press in the grid moves before it scrolls the grid
/// instead of pressing, in points; a finger's reach stands in for it.
constexpr int32_t mouse_drag_points = 4;
/// How far one notch of the wheel scrolls the grid, in points: three lines.
constexpr int32_t wheel_points = 3 * kit::compact_metrics.regular_line;

/// Returns a result that only asks for a redraw.
///
/// @return the result
ScreenResult redrawn() {
    ScreenResult result;
    result.redraw = true;
    return result;
}

/// Returns a result that asks the host for an action.
///
/// @param action the action
/// @return the result
ScreenResult asking(BrowserAction action) {
    ScreenResult result;
    result.action = action;
    result.redraw = true;
    return result;
}

/// The grid's view and how far it scrolls, as the cards' controls place it.
struct GridScroll {
    kit::Rect view{}; ///< what the cards are seen through
    int32_t offset{}; ///< how far it is scrolled
    int32_t limit{};  ///< the greatest offset
};

/// Reads the grid's view and scroll from the cards' controls: their clip is
/// the view, the first card lies at the view's top less the offset, and the
/// last card's foot is the content's.
///
/// @param list the display list
/// @return the scroll; none while no card shows
std::optional<GridScroll> grid_scroll_of(const kit::DisplayList& list) {
    const kit::Control* first = nullptr;
    int32_t bottom = 0;
    for (const kit::Control& control : list.controls) {
        if (!ids::is_card(control.id))
            continue;
        if (first == nullptr)
            first = &control;
        bottom = std::max(bottom, control.rect.y + control.rect.height);
    }
    if (first == nullptr)
        return std::nullopt;
    GridScroll scroll;
    scroll.view = first->clip;
    scroll.offset = std::max(scroll.view.y - first->rect.y, int32_t{0});
    const int32_t content = bottom + scroll.offset - scroll.view.y;
    scroll.limit = kit::ScrollArea::limit_for(content, scroll.view.height);
    return scroll;
}

/// Scrolls the grid so that a card's control shows whole, as near the
/// offset it has as that allows.
///
/// @param[in,out] state the screen's state
/// @param list the display list
/// @param id the control; nothing for one that is not a card
void reveal(ScreenState& state, const kit::DisplayList& list, kit::ControlId id) {
    if (!ids::is_card(id))
        return;
    const kit::Control* control = kit::control_of(list, id);
    const std::optional<GridScroll> scroll = grid_scroll_of(list);
    if (control == nullptr || !scroll)
        return;
    const int32_t top = control->rect.y + scroll->offset - scroll->view.y;
    int32_t offset = scroll->offset;
    if (top < offset)
        offset = top;
    else if (top + control->rect.height > offset + scroll->view.height)
        offset = top + control->rect.height - scroll->view.height;
    state.scroll = std::clamp(offset, int32_t{0}, scroll->limit);
}

/// Shows the cards the filters leave, from the top, and forgets a hover, a
/// press or a focus on a card, whose numbers now name other cards.
///
/// @param[in,out] browser the browser
/// @param[in,out] state the screen's state
void filters_changed(Browser& browser, ScreenState& state) {
    apply_filters(browser);
    state.scroll = 0;
    state.wheel = {};
    state.drag = {};
    kit::Interaction& interaction = state.interaction;
    for (kit::ControlId* id : {&interaction.hovered, &interaction.pressed, &interaction.focused})
        if (ids::is_card(*id))
            *id = kit::no_control;
}

/// Follows the search field's text: the query becomes it, and the cards are
/// filtered again.
///
/// @param[in,out] browser the browser
/// @param[in,out] state the screen's state
void search_changed(Browser& browser, ScreenState& state) {
    if (browser.filters.query == state.search.text)
        return;
    browser.filters.query = state.search.text;
    filters_changed(browser, state);
}

/// Returns the card Tab enters the grid at: the selected card when it
/// shows, else the first card.
///
/// @param browser the browser
/// @param list the display list
/// @return its control; no_control while no card shows
kit::ControlId grid_entry(const Browser& browser, const kit::DisplayList& list) {
    kit::ControlId first = kit::no_control;
    for (const kit::ControlId id : list.tab_order) {
        if (!ids::is_card(id))
            continue;
        if (first == kit::no_control)
            first = id;
        const std::size_t place = ids::card_place(id);
        if (browser.selected && place < browser.shown.size() &&
            browser.shown[place] == *browser.selected)
            return id;
    }
    return first;
}

/// Returns the control Tab or Shift+Tab moves the focus to: the declared
/// order with the grid one stop, entered at grid_entry.
///
/// @param browser the browser
/// @param list the display list
/// @param from the control the focus is on
/// @param shown the focus shows
/// @param forward Tab rather than Shift+Tab
/// @return the control; no_control when nothing takes the focus
kit::ControlId tab_target(
    const Browser& browser,
    const kit::DisplayList& list,
    kit::ControlId from,
    bool shown,
    bool forward
) {
    const kit::ControlId entry = grid_entry(browser, list);
    std::vector<kit::ControlId> stops;
    for (const kit::ControlId id : list.tab_order)
        if (!ids::is_card(id) || id == entry)
            stops.push_back(id);
    if (stops.empty())
        return kit::no_control;
    const kit::ControlId at = ids::is_card(from) ? entry : from;
    const auto found = std::find(stops.begin(), stops.end(), at);
    if (!shown || found == stops.end())
        return forward ? stops.front() : stops.back();
    const auto place = static_cast<std::size_t>(found - stops.begin());
    const std::size_t next =
        forward ? (place + 1) % stops.size() : (place + stops.size() - 1) % stops.size();
    return stops[next];
}

/// Carries out a control's press: a chip turns on or off, the search takes
/// the focus, a card is selected or, already selected, acts as the primary
/// button, and Cancel and the primary button ask the host.
///
/// @param[in,out] browser the browser
/// @param[in,out] state the screen's state
/// @param list the display list
/// @param id the control pressed
/// @return what the host is to do
ScreenResult
act(Browser& browser, ScreenState& state, const kit::DisplayList& list, kit::ControlId id) {
    Filters& filters = browser.filters;
    if (id >= ids::first_source &&
        id < ids::first_source + static_cast<kit::ControlId>(ids::sources.size())) {
        filters.source = ids::sources[static_cast<std::size_t>(id - ids::first_source)];
        filters_changed(browser, state);
        return redrawn();
    }
    if (id >= ids::first_players &&
        id < ids::first_players + static_cast<kit::ControlId>(ids::players.size())) {
        const int32_t chip = ids::players[static_cast<std::size_t>(id - ids::first_players)];
        if (filters.players.erase(chip) == 0)
            filters.players.insert(chip);
        filters_changed(browser, state);
        return redrawn();
    }
    if (id >= ids::first_size &&
        id < ids::first_size + static_cast<kit::ControlId>(ids::sizes.size())) {
        const char letter = ids::sizes[static_cast<std::size_t>(id - ids::first_size)];
        if (filters.sizes.erase(letter) == 0)
            filters.sizes.insert(letter);
        filters_changed(browser, state);
        return redrawn();
    }
    if (id == ids::search) {
        state.interaction.focused = ids::search;
        state.interaction.focus_shown = true;
        return redrawn();
    }
    if (id == ids::cancel)
        return asking(BrowserAction::cancel);
    if (id == ids::primary)
        return primary_action(browser).enabled ? asking(BrowserAction::choose) : ScreenResult{};
    if (ids::is_card(id)) {
        const std::size_t place = ids::card_place(id);
        if (place >= browser.shown.size())
            return {};
        const std::size_t index = browser.shown[place];
        if (browser.selected && *browser.selected == index)
            return primary_action(browser).enabled ? asking(BrowserAction::choose) : ScreenResult{};
        browser.selected = index;
        if (!browser.downloading)
            browser.error.clear();
        reveal(state, list, id);
        return redrawn();
    }
    return {};
}

/// Scrolls the grid to an offset.
///
/// @param[in,out] state the screen's state
/// @param list the display list
/// @param offset the offset wanted
/// @return redraw when it moved; nothing otherwise
ScreenResult scroll_to(ScreenState& state, const kit::DisplayList& list, int32_t offset) {
    const std::optional<GridScroll> scroll = grid_scroll_of(list);
    if (!scroll)
        return {};
    const int32_t next = std::clamp(offset, int32_t{0}, scroll->limit);
    if (next == scroll->offset)
        return {};
    state.scroll = next;
    return redrawn();
}

} // namespace

ScreenResult browser_find(ScreenState& state, const kit::DisplayList& list) {
    if (kit::control_of(list, ids::search) == nullptr)
        return {};
    state.interaction.focused = ids::search;
    state.interaction.focus_shown = true;
    return redrawn();
}

std::optional<kit::Rect>
focused_field(const ScreenState& state, const kit::DisplayList& list) noexcept {
    if (!state.interaction.focus_shown || state.interaction.focused != ids::search)
        return std::nullopt;
    const kit::Control* control = kit::control_of(list, ids::search);
    if (control == nullptr)
        return std::nullopt;
    return control->rect;
}

ScreenResult browser_pointer(
    Browser& browser,
    ScreenState& state,
    const kit::DisplayList& list,
    PointerKind kind,
    uint8_t button,
    kit::Point at,
    int32_t finger_reach
) {
    kit::Interaction& interaction = state.interaction;
    switch (kind) {
    case PointerKind::move: {
        if (state.drag.held) {
            const int32_t reach = finger_reach > 0 ? finger_reach : mouse_drag_points;
            if (!state.drag.scrolling && (std::abs(at.y - state.drag.from.y) > reach ||
                                          std::abs(at.x - state.drag.from.x) > reach)) {
                // The press becomes a drag: it scrolls, and presses nothing.
                state.drag.scrolling = true;
                interaction.pressed = kit::no_control;
                interaction.finger_shift = {};
            }
            if (state.drag.scrolling) {
                std::ignore = scroll_to(state, list, state.drag.offset + state.drag.from.y - at.y);
                return redrawn();
            }
        }
        const kit::PointerOutcome outcome = kit::pointer_move(interaction, list, at);
        return outcome.result == kit::PointerResult::none ? ScreenResult{} : redrawn();
    }
    case PointerKind::down: {
        if (button != 1)
            return {};
        const kit::PointerOutcome outcome =
            finger_reach > 0 ? kit::finger_down(interaction, list, at, finger_reach)
                             : kit::pointer_down(interaction, list, at);
        state.drag = {};
        if (const std::optional<GridScroll> scroll = grid_scroll_of(list);
            scroll && kit::contains(scroll->view, at)) {
            state.drag.held = true;
            state.drag.from = at;
            state.drag.offset = scroll->offset;
        }
        return outcome.result == kit::PointerResult::none ? ScreenResult{} : redrawn();
    }
    case PointerKind::up: {
        if (button != 1)
            return {};
        const bool dragged = state.drag.scrolling;
        state.drag = {};
        if (dragged) {
            interaction.pressed = kit::no_control;
            interaction.finger_shift = {};
            return redrawn();
        }
        const kit::PointerOutcome outcome = kit::pointer_up(interaction, list, at);
        if (outcome.result == kit::PointerResult::activated)
            return act(browser, state, list, outcome.control);
        return outcome.result == kit::PointerResult::none ? ScreenResult{} : redrawn();
    }
    }
    return {};
}

ScreenResult
browser_key(Browser& browser, ScreenState& state, const kit::DisplayList& list, kit::Key pressed) {
    kit::Interaction& interaction = state.interaction;
    const bool on_search = interaction.focus_shown && interaction.focused == ids::search;
    switch (pressed) {
    case kit::Key::escape:
        return asking(BrowserAction::cancel);
    case kit::Key::tab:
    case kit::Key::back_tab: {
        const kit::ControlId next = tab_target(
            browser, list, interaction.focused, interaction.focus_shown, pressed == kit::Key::tab
        );
        if (next == kit::no_control)
            return {};
        interaction.focused = next;
        interaction.focus_shown = true;
        reveal(state, list, next);
        return redrawn();
    }
    case kit::Key::space:
        // A space typed into the search arrives as text.
        if (on_search)
            return {};
        break;
    case kit::Key::enter:
        if (on_search) {
            const kit::ControlId entry = grid_entry(browser, list);
            if (entry == kit::no_control)
                return {};
            interaction.focused = entry;
            reveal(state, list, entry);
            return redrawn();
        }
        break;
    case kit::Key::page_up:
    case kit::Key::page_down: {
        const std::optional<GridScroll> scroll = grid_scroll_of(list);
        if (!scroll)
            return {};
        const int32_t step =
            pressed == kit::Key::page_up ? -scroll->view.height : scroll->view.height;
        return scroll_to(state, list, scroll->offset + step);
    }
    case kit::Key::home:
    case kit::Key::end:
        if (!on_search) {
            const std::optional<GridScroll> scroll = grid_scroll_of(list);
            if (!scroll)
                return {};
            return scroll_to(state, list, pressed == kit::Key::home ? 0 : scroll->limit);
        }
        break;
    default:
        break;
    }
    const kit::KeyOutcome outcome = kit::key(interaction, list, pressed);
    switch (outcome.result) {
    case kit::KeyResult::none:
        return {};
    case kit::KeyResult::redraw:
        reveal(state, list, interaction.focused);
        return redrawn();
    case kit::KeyResult::to_control:
        if (outcome.control == ids::search) {
            if (!kit::edit_text(state.search, pressed))
                return {};
            search_changed(browser, state);
            return redrawn();
        }
        return act(browser, state, list, outcome.control);
    case kit::KeyResult::scroll:
        return {};
    case kit::KeyResult::accept:
        return primary_action(browser).enabled ? asking(BrowserAction::choose) : ScreenResult{};
    case kit::KeyResult::cancel:
        return asking(BrowserAction::cancel);
    }
    return {};
}

ScreenResult browser_text(
    Browser& browser, ScreenState& state, const kit::DisplayList& list, std::string_view utf8
) {
    if (!focused_field(state, list) || !kit::insert_text(state.search, utf8))
        return {};
    search_changed(browser, state);
    return redrawn();
}

ScreenResult browser_wheel(
    Browser& /*browser*/, ScreenState& state, const kit::DisplayList& list, float notches
) {
    const std::optional<GridScroll> scroll = grid_scroll_of(list);
    if (!scroll)
        return {};
    const int32_t next =
        kit::wheel_offset(state.wheel, notches, wheel_points, scroll->offset, scroll->limit);
    if (next == scroll->offset)
        return {};
    state.scroll = next;
    return redrawn();
}

} // namespace oa::ui::map_browser
