// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's layout at Compact, Regular and Large, made of the UI kit's
// components: the window's face, header and footer, the tabs, the search,
// the filters, the list and the details, every control named for
// automation. It reads the model and decides none of its rules.
#include "oa/ui/library/screen.hpp"

#include "oa/ui/kit/chrome.hpp"
#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/components_more.hpp"
#include "oa/ui/kit/controls.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/looks.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/kit/theme.hpp"
#include "oa/ui/library/library.hpp"
#include "oa/ui/library/text.hpp"
#include "screen_controls.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::library {

namespace {

// The Library's own sizes, from the design; every other size is a kit metric.

/// The columns Compact's search row keeps right of the search field, for the filter drop-down.
constexpr int32_t compact_filter_room = 104;
/// The search field's width at Regular and Large.
constexpr int32_t wide_search_width = 240;
/// The list pane's width at Regular.
constexpr int32_t regular_list_width = 420;
/// The list pane's width at Large.
constexpr int32_t large_list_width = 400;
/// The filter pane's width at Large.
constexpr int32_t filter_pane_width = 180;
/// The side of the details' badge at Regular and Large.
constexpr int32_t details_badge_side = 40;
/// The most lines of the summary on Compact's details page.
constexpr std::size_t compact_summary_lines = 3;
/// The most lines of the summary in the details pane.
constexpr std::size_t wide_summary_lines = 6;
/// The most lines of a fact's value on Compact's details page; Rules and From take all they need.
constexpr std::size_t compact_fact_lines = 2;
/// The most lines of a fact's value in the details pane; Rules and From take all they need.
constexpr std::size_t wide_fact_lines = 3;
/// The most lines of the status line beside the details' badge.
constexpr std::size_t wide_status_lines = 2;
/// How many actions Compact's details page shows as buttons, before MORE.
constexpr std::size_t compact_action_buttons = 3;
/// How many action buttons a line of the details pane holds.
constexpr int32_t actions_per_line = 3;
/// The most letters a badge shows.
constexpr std::size_t badge_letter_count = 3;
/// The most bytes one word of an automation name keeps.
constexpr std::size_t most_name_word_bytes = 40;
/// A full progress bar, in the bar's own units.
constexpr int32_t progress_whole = 1000;
/// The prefix of every control's name.
constexpr std::string_view name_prefix = "library.";

/// The tabs, in the order they are shown.
constexpr std::array<Tab, 4> tab_order{Tab::mods, Tab::maps, Tab::languages, Tab::updates};
/// The words the tabs are named by, in that order.
constexpr std::array<std::string_view, 4> tab_words{"mods", "maps", "languages", "updates"};
/// The filters, in the order they are shown.
constexpr std::array<Filter, 3> filter_order{Filter::all, Filter::installed, Filter::updates};
/// The words the filters are named by, in that order.
constexpr std::array<std::string_view, 3> filter_words{"all", "installed", "updates"};

/// Returns the word an action's button is named by.
///
/// @param action the action
/// @return the word, such as "roll-back"
std::string_view action_word(Action action) {
    switch (action) {
    case Action::get:
        return "get";
    case Action::update:
        return "update";
    case Action::update_all:
        return "update-all";
    case Action::cancel:
        return "cancel";
    case Action::retry:
        return "retry";
    case Action::roll_back:
        return "roll-back";
    case Action::play_now:
        return "play-now";
    case Action::open_folder:
        return "open-folder";
    case Action::homepage:
        return "homepage";
    case Action::details:
        return "details";
    case Action::back:
        return "back";
    case Action::settings:
        return "settings";
    case Action::close:
        return "close";
    }
    return "action";
}

/// Returns a text as one word of an automation name: letters a to z and
/// digits kept, capitals made small, every other run of characters one
/// hyphen, none at either end, at most most_name_word_bytes.
///
/// @param text the text, in UTF-8
/// @return the word; "x" when nothing of the text is kept
std::string name_word(std::string_view text) {
    std::string word;
    bool hyphen = false;
    for (const char character : text) {
        const bool small = character >= 'a' && character <= 'z';
        const bool capital = character >= 'A' && character <= 'Z';
        const bool digit = character >= '0' && character <= '9';
        if (small || digit || capital) {
            if (hyphen && !word.empty())
                word += '-';
            hyphen = false;
            word += capital ? static_cast<char>(character - 'A' + 'a') : character;
        } else {
            hyphen = true;
        }
        if (word.size() >= most_name_word_bytes)
            break;
    }
    if (word.empty())
        word = "x";
    return word;
}

/// Returns a name with the prefix every control's name starts with.
///
/// @param words_after the words after it, joined by dots
/// @return the name
std::string named(std::string_view words_after) {
    return std::string(name_prefix) + std::string(words_after);
}

/// The window's rows and columns at a size class.
struct Window {
    int32_t width{};       ///< the window's width, in points
    int32_t height{};      ///< the window's height, in points
    int32_t padding{};     ///< the class's padding
    int32_t left{};        ///< the content's left column
    int32_t right{};       ///< the column after the content's last
    int32_t body_top{};    ///< the first row under the header's rule
    int32_t footer_rule{}; ///< the row of the line over the footer
    int32_t footer_top{};  ///< the footer's first row
    int32_t button_top{};  ///< the footer's buttons' top row
    int32_t gap{};         ///< the rows between stacked parts
};

/// Returns the window's rows and columns for a frame, as the settings
/// dialog's frame places them.
///
/// @param frame the frame
/// @return the window
Window window_of(const kit::Frame& frame) {
    const kit::Metrics& metrics = kit::metrics_of(frame.size_class);
    const kit::Point size = window_size(frame);
    Window window;
    window.width = size.x;
    window.height = size.y;
    window.padding = metrics.padding;
    window.left = metrics.padding;
    window.right = size.x - metrics.edge - metrics.padding;
    window.body_top = metrics.edge + metrics.header_height + 1;
    window.footer_rule = size.y - metrics.edge - metrics.footer_height - 1;
    window.footer_top = window.footer_rule + 1;
    window.button_top = window.footer_top + (metrics.footer_height - metrics.button_height) / 2;
    window.gap = metrics.paragraph_gap;
    return window;
}

/// A drop-down whose menu is open, drawn over everything else.
struct OpenMenu {
    Menu menu{Menu::none};                   ///< which
    kit::Rect field{};                       ///< its field
    std::vector<controls::MenuItem> items{}; ///< its items
    int32_t chosen{-1};                      ///< the item shown as chosen, or -1
    std::string name{};                      ///< the field's control's name
};

/// What a layout is built into: the items in drawing order, and the
/// controls in groups that are tried in a fixed order.
struct Building {
    const Library* library{};      ///< the model
    const ScreenState* state{};    ///< the screen's state
    const kit::Fonts* fonts{};     ///< the fonts texts are measured in
    const kit::Metrics* metrics{}; ///< the class's metrics
    kit::SizeClass size_class{};   ///< the class
    kit::DisplayList list{}; ///< the items, in drawing order; its controls are moved to the groups
    std::vector<kit::Control> menu{};    ///< an open drop-down's items, tried first
    std::vector<kit::Control> top{};     ///< the tabs, the search and the filters
    std::vector<kit::Control> rows{};    ///< the list's rows that show
    std::vector<kit::Control> panes{};   ///< the list pane, tried after its rows
    std::vector<kit::Control> detail{};  ///< the details' controls and labels
    std::vector<kit::Control> footer{};  ///< the footer's buttons and label
    std::vector<kit::ControlId> order{}; ///< Tab's order, built in the order it is declared
    std::vector<std::string> names{};    ///< the names given so far
    std::optional<OpenMenu> open{};      ///< the open drop-down
};

/// Returns a name no control of the layout has yet, the name itself or with
/// "-2", "-3" and so on after it, and records it.
///
/// @param[in,out] building the layout
/// @param name the name wanted
/// @return the name given
std::string unique_name(Building& building, std::string name) {
    std::string given = name;
    for (int32_t count = 2;
         std::find(building.names.begin(), building.names.end(), given) != building.names.end();
         ++count)
        given = name + "-" + std::to_string(count);
    building.names.push_back(given);
    return given;
}

/// Returns a text's width in the layout's fonts.
///
/// @param building the layout
/// @param role the font
/// @param piece the text, in UTF-8
/// @return the width, in points
int32_t width_of(const Building& building, kit::FontRole role, std::string_view piece) {
    return kit::text_width(*building.fonts, role, piece);
}

/// Returns a text cut with "..." to a width in the layout's fonts.
///
/// @param building the layout
/// @param role the font
/// @param piece the text, in UTF-8
/// @param width the room, in points
/// @return the text, whole when it fits
std::string
cut(const Building& building, kit::FontRole role, std::string_view piece, int32_t width) {
    return kit::cut_to_width(*building.fonts, role, piece, std::max(width, int32_t{0}));
}

/// Breaks a text into lines no wider than a width, as the kit wraps.
///
/// @param building the layout
/// @param role the font
/// @param piece the text, in UTF-8
/// @param width the room, in points
/// @param most_lines the most lines kept, the last cut with "..." when more
///     would follow; 0 keeps every line
/// @return the lines
std::vector<std::string> wrapped(
    const Building& building,
    kit::FontRole role,
    std::string_view piece,
    int32_t width,
    std::size_t most_lines
) {
    const kit::Fonts* fonts = building.fonts;
    const kit::Measure measure = [fonts, role](std::string_view piece) {
        return kit::text_width(*fonts, role, piece);
    };
    kit::WrapRules rules;
    rules.newlines = true;
    rules.most_lines = most_lines;
    if (most_lines != 0) {
        const int32_t room = std::max(width, int32_t{0});
        // The line and the words dropped after it are wider than the room,
        // so the cut always ends with the kit's ellipsis.
        rules.shorten_last = [fonts, role, room](std::string_view last) {
            return kit::cut_to_width(*fonts, role, last, room);
        };
    }
    return kit::wrap(piece, std::max(width, int32_t{1}), measure, rules);
}

/// Joins lines with single spaces, as they read.
///
/// @param lines the lines
/// @return the text
std::string joined(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) {
        if (!text.empty())
            text += ' ';
        text += line;
    }
    return text;
}

/// Moves the controls a kit function appended to the list into a group.
///
/// @param[in,out] list the display list
/// @param from how many controls the list held before
/// @param[in,out] group the group they go to
void move_controls(kit::DisplayList& list, std::size_t from, std::vector<kit::Control>& group) {
    for (std::size_t index = from; index < list.controls.size(); ++index)
        group.push_back(std::move(list.controls[index]));
    list.controls.resize(from);
}

/// Appends an item.
///
/// @param[in,out] list the display list
/// @param role what it draws
/// @param rect where
/// @param clip what drawing may touch; empty for the window
/// @return the item, to fill in
kit::Item&
add_item(kit::DisplayList& list, kit::Role role, const kit::Rect& rect, const kit::Rect& clip) {
    kit::Item& added = list.items.emplace_back();
    added.role = role;
    added.rect = rect;
    added.clip = clip;
    return added;
}

/// Appends a text in a box.
///
/// @param[in,out] building the layout
/// @param role the font
/// @param drawn the text, as drawn
/// @param box the box
/// @param align where it sits along the box
/// @param ink its colour
/// @param clip what drawing may touch; empty for the window
void add_text(
    Building& building,
    kit::FontRole role,
    std::string drawn,
    const kit::Rect& box,
    kit::Align align,
    kit::Colour ink,
    const kit::Rect& clip
) {
    if (drawn.empty())
        return;
    kit::Item& added = add_item(building.list, kit::Role::text, box, clip);
    added.text = std::move(drawn);
    added.font = role;
    added.align = align;
    added.colour = ink;
}

/// Appends a label: a control of kind area that takes no press, no finger,
/// no key and no Tab, whose text is the words shown, so automation lists it
/// as a line a journey reads.
///
/// @param group the group it goes to
/// @param id its number
/// @param rect where its words lie
/// @param name its name
/// @param read the words shown
/// @param clip the part of it that shows; empty for all
/// @param scroll_group its scroll area, or -1
void add_label(
    std::vector<kit::Control>& group,
    kit::ControlId id,
    const kit::Rect& rect,
    std::string name,
    std::string read,
    const kit::Rect& clip,
    int32_t scroll_group
) {
    kit::Control label;
    label.id = id;
    label.rect = rect;
    label.clip = clip;
    label.enabled = false;
    label.focusable = false;
    label.name = std::move(name);
    label.kind = kit::ControlKind::area;
    label.group = scroll_group;
    label.text = std::move(read);
    group.push_back(std::move(label));
}

/// Adds a control to Tab's order when it takes the focus.
///
/// @param[in,out] building the layout
/// @param control the control
void tab_stop(Building& building, const kit::Control& control) {
    if (control.enabled && control.focusable)
        building.order.push_back(control.id);
}

/// Tells whether the pointer is over a control.
///
/// @param building the layout
/// @param id the control
/// @return true while it is hovered
bool hovered(const Building& building, kit::ControlId id) {
    return building.state->interaction.hovered == id;
}

/// Tells whether a press is held on a control with the pointer over it.
///
/// @param building the layout
/// @param id the control
/// @return true while it is held
bool held(const Building& building, kit::ControlId id) {
    const kit::Interaction& interaction = building.state->interaction;
    return interaction.pressed == id && interaction.hovered == id;
}

/// Tells whether the keys' focus shows on a control.
///
/// @param building the layout
/// @param id the control
/// @return true while it has the focus
bool focused(const Building& building, kit::ControlId id) {
    const kit::Interaction& interaction = building.state->interaction;
    return interaction.focus_shown && interaction.focused == id;
}

/// Returns a button's width: its caption in the small font and the room a
/// question's button keeps either side, and at least OK's width.
///
/// @param building the layout
/// @param caption the caption
/// @return the width, in points
int32_t button_width(const Building& building, std::string_view caption) {
    const kit::Metrics& metrics = *building.metrics;
    return std::max(
        metrics.button_width,
        width_of(building, kit::FontRole::small, caption) + metrics.prompt_button_padding
    );
}

/// Appends a button.
///
/// @param[in,out] building the layout
/// @param group the group its control goes to
/// @param rect where it lies
/// @param caption its words, looked up
/// @param accent drawn as the main button while it is enabled
/// @param enabled it takes a press
/// @param id its number
/// @param name its name
/// @param clip the part of it that shows; empty for all
/// @param scroll_group its scroll area, or -1
void add_button(
    Building& building,
    std::vector<kit::Control>& group,
    const kit::Rect& rect,
    std::string_view caption,
    bool accent,
    bool enabled,
    kit::ControlId id,
    std::string name,
    const kit::Rect& clip,
    int32_t scroll_group
) {
    kit::ButtonLook look;
    look.caption = cut(building, kit::FontRole::small, caption, rect.width - 2);
    look.style = accent && enabled ? kit::ButtonStyle::accent : kit::ButtonStyle::plain;
    look.hovered = hovered(building, id);
    look.held = held(building, id);
    look.enabled = enabled;
    const std::size_t before = building.list.controls.size();
    kit::add_button(building.list, rect, look, id, unique_name(building, std::move(name)));
    building.list.items.back().clip = clip;
    move_controls(building.list, before, group);
    group.back().clip = clip;
    group.back().group = scroll_group;
    group.back().text = std::string(caption);
    tab_stop(building, group.back());
}

/// Returns the chip a state is shown with.
///
/// @param state the entry's state
/// @return the chip's state
kit::ChipState chip_state_of(State state) {
    switch (state) {
    case State::get:
        return kit::ChipState::get;
    case State::installed:
        return kit::ChipState::installed;
    case State::update:
        return kit::ChipState::update;
    case State::playing:
        return kit::ChipState::playing;
    }
    return kit::ChipState::get;
}

/// Returns an entry's chip: its state's word, drawn disabled when GET or
/// UPDATE cannot act.
///
/// @param library the model
/// @param entry the entry
/// @return the chip
kit::ChipLook entry_chip(const Library& library, const Entry& entry) {
    kit::ChipLook chip;
    chip.text = std::string(state_text(entry.state, library.text));
    chip.state = chip_state_of(entry.state);
    chip.disabled = !entry.can_act;
    return chip;
}

/// Returns an entry's version as its row shows it: the installed copy's,
/// else the listing's.
///
/// @param library the model
/// @param entry the entry
/// @return the version; empty when neither names one
std::string entry_version(const Library& library, const Entry& entry) {
    if (entry.installed != nullptr)
        return version_text(*entry.installed, library.text);
    if (entry.listing != nullptr)
        return version_text(*entry.listing, library.text);
    return {};
}

/// Returns an entry's size as its row shows it: its listing's.
///
/// @param library the model
/// @param entry the entry
/// @return the size; empty without a listing
std::string entry_size(const Library& library, const Entry& entry) {
    if (entry.listing == nullptr)
        return {};
    return size_text(entry.listing->size, library.text);
}

/// Returns an entry's badge picture: its listing's, else its installed copy's.
///
/// @param entry the entry
/// @return the picture; empty without one
oa::ui::frontend_renderer::RgbaPicture badge_picture(const Entry& entry) {
    const Badge* badge = nullptr;
    if (entry.listing != nullptr && entry.listing->badge)
        badge = &*entry.listing->badge;
    else if (entry.installed != nullptr && entry.installed->badge)
        badge = &*entry.installed->badge;
    oa::ui::frontend_renderer::RgbaPicture picture;
    if (badge == nullptr)
        return picture;
    picture.width = badge->width;
    picture.height = badge->height;
    picture.pixels = std::span<const uint8_t>(badge->rgba);
    return picture;
}

/// Returns a row's name: library.row., then its registry's word and its
/// key's, or its key's alone for a package of the player's own.
///
/// @param entry the entry
/// @return the name
std::string row_name(const Entry& entry) {
    std::string name = named("row.");
    if (!entry.id.registry.empty())
        name += name_word(entry.id.registry) + ".";
    name += name_word(entry.id.key);
    return name;
}

/// How a row is shown: Compact's short row, Regular's or Large's.
enum class RowForm : uint8_t {
    compact, ///< the name, then its version and size; short_list_row_height
    regular, ///< the name and byline, then the summary; the version over the size; list_row_height
    large,   ///< as regular, with the tags as chips on a third line
};

/// Returns a row's height in a form.
///
/// @param metrics the class's metrics
/// @param form the form
/// @return the height, in points
int32_t row_height(const kit::Metrics& metrics, RowForm form) {
    switch (form) {
    case RowForm::compact:
        return metrics.short_list_row_height;
    case RowForm::regular:
        return metrics.list_row_height;
    case RowForm::large:
        // The title's line, two lines under it and the rows above the title.
        return metrics.list_row_line_top + 2 * metrics.small_line + metrics.list_row_title_top;
    }
    return metrics.list_row_height;
}

/// Returns what an entry's row shows.
///
/// @param building the layout
/// @param entry the entry
/// @param form the row's form
/// @return the row's look
kit::ListRowLook row_look(const Building& building, const Entry& entry, RowForm form) {
    const Library& library = *building.library;
    kit::ListRowLook look;
    look.badge = badge_picture(entry);
    look.badge_text = badge_letters(entry_name(entry));
    look.badge_colour = kit::colour::list_selected;
    look.title = std::string(entry_name(entry));
    look.chip = entry_chip(library, entry);
    const std::string version = entry_version(library, entry);
    const std::string size = entry_size(library, entry);
    if (form == RowForm::compact) {
        std::string line = version;
        if (!version.empty() && !size.empty())
            line = filled(
                library.text, words::row_version_size, {{"version", version}, {"size", size}}
            );
        else if (version.empty())
            line = size;
        if (entry.not_reviewed && !entry.registry_name.empty())
            line = filled(
                library.text,
                words::row_from_registry,
                {{"line", line}, {"registry", entry.registry_name}}
            );
        look.lines.push_back(std::move(line));
        return look;
    }
    look.by = byline(entry, library.text);
    if (entry.listing != nullptr && !entry.listing->summary.empty())
        look.lines.push_back(entry.listing->summary);
    if (!version.empty())
        look.aside.push_back(version);
    if (!size.empty())
        look.aside.push_back(size);
    if (form == RowForm::large && entry.listing != nullptr) {
        for (const std::string& tag : entry.listing->tags) {
            kit::ChipLook chip;
            chip.text = tag;
            chip.state = kit::ChipState::filter;
            look.tags.push_back(std::move(chip));
        }
    }
    return look;
}

/// Appends the header: the OA mark, the title and its second word, and the
/// words at the right, each kept to the columns it is given.
///
/// @param[in,out] building the layout
/// @param window the window
/// @param title the title, cut to fit when it is an entry's name
/// @param second the word after the title; empty for none
/// @param right the words at the right, cut to fit
void add_header(
    Building& building,
    const Window& window,
    std::string_view title,
    std::string_view second,
    std::string_view right
) {
    const kit::Metrics& compact = kit::compact_metrics;
    const int32_t tracking = building.metrics->heading_tracking;
    // The kit's header keeps Compact's places from the window's edges.
    const int32_t title_left = compact.padding + compact.mark_side + compact.header_gap;
    const int32_t right_edge = window.width - compact.edge - compact.padding;
    const int32_t second_width =
        second.empty()
            ? 0
            : kit::tracked_width(*building.fonts, kit::FontRole::regular, second, tracking);
    const int32_t second_room = second.empty() ? 0 : compact.header_gap + second_width;
    const int32_t right_width = width_of(building, kit::FontRole::small, right);
    const int32_t title_room = std::max(
        right_edge - title_left - second_room - compact.header_gap - right_width, int32_t{0}
    );

    kit::HeaderLook look;
    look.width = window.width;
    look.tracking = tracking;
    look.title = std::string(title);
    if (kit::tracked_width(*building.fonts, kit::FontRole::regular, title, tracking) > title_room)
        look.title =
            cut(building,
                kit::FontRole::regular,
                title,
                title_room - tracking * static_cast<int32_t>(kit::character_count(title)));
    look.title_width =
        kit::tracked_width(*building.fonts, kit::FontRole::regular, look.title, tracking);
    look.second = std::string(second);
    look.second_width = second_width;
    const int32_t version_room = std::max(
        right_edge - (title_left + look.title_width + second_room + compact.header_gap), int32_t{0}
    );
    look.version = cut(building, kit::FontRole::small, right, version_room);
    look.version_width = width_of(building, kit::FontRole::small, look.version);
    const int32_t header_rule = compact.edge + compact.header_height;
    add_item(building.list, kit::Role::header, {0, 0, window.width, header_rule + 1}, {}).look =
        std::move(look);
}

/// Appends the strip of tabs, with the update count on Updates.
///
/// @param[in,out] building the layout
/// @param strip the strip
/// @param compact the tabs take their short names
void add_tabs(Building& building, const kit::Rect& strip, bool compact) {
    const Library& library = *building.library;
    kit::TabsLook look;
    std::array<kit::ControlId, tab_order.size()> ids{};
    std::array<std::string, tab_order.size()> names{};
    for (std::size_t index = 0; index < tab_order.size(); ++index) {
        const Tab tab = tab_order[index];
        look.captions.emplace_back(tab_text(tab, compact, library.text));
        look.counts.push_back(
            tab == Tab::updates ? static_cast<int32_t>(update_count(library)) : 0
        );
        if (tab == library.tab)
            look.selected = index;
        ids[index] = controls::tab_control(tab);
        names[index] = unique_name(building, named("tab." + std::string(tab_words[index])));
        if (hovered(building, ids[index]))
            look.hovered = index;
    }
    const std::size_t before = building.list.controls.size();
    kit::add_tabs(building.list, *building.fonts, strip, look, ids, names);
    move_controls(building.list, before, building.top);
    for (std::size_t index = building.top.size() - tab_order.size(); index < building.top.size();
         ++index)
        tab_stop(building, building.top[index]);
}

/// Returns the search field's words while it is empty, for the open tab.
///
/// @param library the model
/// @return the words, looked up
std::string_view placeholder_of(const Library& library) {
    switch (library.tab) {
    case Tab::mods:
        return shown(library.text, words::search_mods);
    case Tab::maps:
        return shown(library.text, words::search_maps);
    case Tab::languages:
        return shown(library.text, words::search_languages);
    case Tab::updates:
        return shown(library.text, words::search_updates);
    }
    return {};
}

/// Appends the search field.
///
/// @param[in,out] building the layout
/// @param rect the field
void add_search(Building& building, const kit::Rect& rect) {
    kit::SearchLook look;
    look.field = building.state->search;
    look.focused = focused(building, controls::search);
    look.hovered = hovered(building, controls::search);
    look.placeholder =
        cut(building,
            kit::FontRole::regular,
            placeholder_of(*building.library),
            kit::field_text_rect(rect, look).width);
    const std::size_t before = building.list.controls.size();
    kit::add_search(
        building.list, rect, look, controls::search, unique_name(building, named("search"))
    );
    move_controls(building.list, before, building.top);
    tab_stop(building, building.top.back());
}

/// Appends a drop-down's field and, while it is open, keeps its menu to
/// draw over everything else.
///
/// @param[in,out] building the layout
/// @param group the group its control goes to
/// @param rect the field
/// @param menu which drop-down
/// @param caption the words the field shows
/// @param items its items
/// @param id its control's number
/// @param name its control's name
void add_drop_down(
    Building& building,
    std::vector<kit::Control>& group,
    const kit::Rect& rect,
    Menu menu,
    std::string_view caption,
    std::vector<controls::MenuItem> items,
    kit::ControlId id,
    std::string name
) {
    const kit::Metrics& metrics = *building.metrics;
    kit::ChoiceLook look;
    look.text =
        cut(building,
            kit::FontRole::regular,
            caption,
            rect.width - metrics.choice_text_inset - metrics.choice_arrow_room);
    look.hovered = hovered(building, id) || building.state->interaction.pressed == id;
    look.open = building.state->menu == menu;
    const std::string given = unique_name(building, std::move(name));
    const std::size_t before = building.list.controls.size();
    kit::add_choice(building.list, rect, look, id, given);
    move_controls(building.list, before, group);
    kit::Control& added = group.back();
    // The arrows move the focus off a drop-down; Space and Enter open it.
    added.steps = false;
    for (const controls::MenuItem& item : items)
        added.parts.push_back(item.word);
    tab_stop(building, added);
    if (look.open) {
        OpenMenu opened;
        opened.menu = menu;
        opened.field = rect;
        opened.chosen = controls::chosen_item(*building.library, menu, items);
        opened.items = std::move(items);
        opened.name = given;
        building.open = std::move(opened);
    }
}

/// Returns the width a drop-down's field needs for its caption and, open,
/// for its widest item.
///
/// @param building the layout
/// @param caption the field's words
/// @param items its items
/// @return the width, in points
int32_t drop_down_width(
    const Building& building, std::string_view caption, const std::vector<controls::MenuItem>& items
) {
    const kit::Metrics& metrics = *building.metrics;
    int32_t width = width_of(building, kit::FontRole::regular, caption) +
                    metrics.choice_text_inset + metrics.choice_arrow_room;
    for (const controls::MenuItem& item : items)
        width = std::max(
            width,
            width_of(building, kit::FontRole::regular, item.caption) +
                metrics.choice_item_text_inset + metrics.choice_arrow_room
        );
    return width;
}

/// Appends "{n} updates" and UPDATE ALL along a row, the button at its right.
///
/// @param[in,out] building the layout
/// @param row the row
/// @return the column left of the words
int32_t add_update_all(Building& building, const kit::Rect& row) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
    const std::string_view caption = action_text(Action::update_all, library.text);
    const int32_t width = std::min(button_width(building, caption), row.width);
    const kit::Rect button{row.x + row.width - width, row.y, width, metrics.button_height};
    const std::size_t shown_count = library.visible.size();
    const std::string count = filled(
        library.text,
        shown_count == 1 ? words::update_count_one : words::update_count_many,
        {{"count", std::to_string(shown_count)}}
    );
    const int32_t count_width = std::min(
        width_of(building, kit::FontRole::small, count),
        std::max(button.x - metrics.label_gap - row.x, int32_t{0})
    );
    const kit::Rect words_box{
        button.x - metrics.label_gap - count_width, row.y, count_width, metrics.button_height
    };
    add_text(
        building,
        kit::FontRole::small,
        cut(building, kit::FontRole::small, count, count_width),
        words_box,
        kit::Align::right,
        kit::colour::hint,
        {}
    );
    add_button(
        building,
        building.top,
        button,
        caption,
        false,
        update_count(library) > 0,
        controls::update_all,
        named("action.update-all"),
        {},
        -1
    );
    return words_box.x;
}

/// Appends Compact's search row: the search field, and the filter drop-down
/// or, on the Updates tab, the updates' count and UPDATE ALL.
///
/// @param[in,out] building the layout
/// @param row the row
void add_compact_search_row(Building& building, const kit::Rect& row) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
    if (library.tab == Tab::updates) {
        // The filters do not apply on Updates: the field leaves room for the count and the button.
        const std::string_view caption = action_text(Action::update_all, library.text);
        const std::size_t shown_count = library.visible.size();
        const std::string count = filled(
            library.text,
            shown_count == 1 ? words::update_count_one : words::update_count_many,
            {{"count", std::to_string(shown_count)}}
        );
        const int32_t taken = button_width(building, caption) + metrics.label_gap +
                              width_of(building, kit::FontRole::small, count) + metrics.label_gap;
        const int32_t field_width = std::max(row.width - taken, int32_t{0});
        add_search(building, {row.x, row.y, field_width, metrics.field_height});
        add_update_all(
            building,
            {row.x + field_width + metrics.label_gap,
             row.y,
             row.width - field_width - metrics.label_gap,
             row.height}
        );
        return;
    }
    const int32_t field_width = std::max(row.width - compact_filter_room, int32_t{0});
    add_search(building, {row.x, row.y, field_width, metrics.field_height});
    std::vector<controls::MenuItem> items = controls::menu_items(library, Menu::filter, {});
    const int32_t chosen = controls::chosen_item(library, Menu::filter, items);
    const std::string caption =
        chosen >= 0 ? items[static_cast<std::size_t>(chosen)].caption : std::string();
    add_drop_down(
        building,
        building.top,
        {row.x + field_width + metrics.button_gap,
         row.y,
         compact_filter_room - metrics.button_gap,
         metrics.regular_line},
        Menu::filter,
        caption,
        std::move(items),
        controls::filter_menu,
        named("filter")
    );
}

/// Appends Regular's chips: All, Installed and Updates, the tab's tags
/// while they fit, and TAGS ▾ with the rest; or, on the Updates tab, the
/// updates' count and UPDATE ALL.
///
/// @param[in,out] building the layout
/// @param row the row
void add_chip_row(Building& building, const kit::Rect& row) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
    if (library.tab == Tab::updates) {
        static_cast<void>(add_update_all(building, row));
        return;
    }
    const int32_t chip_top = row.y + (row.height - metrics.chip_height) / 2;
    int32_t left = row.x;
    const auto add_chip = [&](const kit::ChipLook& look, kit::ControlId id, std::string name) {
        const int32_t width = kit::chip_width(*building.fonts, look);
        const kit::Rect rect{left, chip_top, width, metrics.chip_height};
        kit::ChipLook drawn = look;
        drawn.hovered = hovered(building, id);
        const std::size_t before = building.list.controls.size();
        kit::add_chip(building.list, rect, drawn, id, unique_name(building, std::move(name)));
        move_controls(building.list, before, building.top);
        tab_stop(building, building.top.back());
        left += width + metrics.chip_gap;
    };
    for (std::size_t index = 0; index < filter_order.size(); ++index) {
        kit::ChipLook look;
        look.text = std::string(filter_text(filter_order[index], library.text));
        look.state = kit::ChipState::filter;
        look.selected = library.filter == filter_order[index];
        add_chip(
            look,
            controls::filter_control(filter_order[index]),
            named("filter." + std::string(filter_words[index]))
        );
    }
    const std::vector<std::pair<std::string, std::size_t>> tags = tags_in_tab(library);
    std::vector<int32_t> widths;
    int32_t total = 0;
    for (const auto& [tag, count] : tags) {
        kit::ChipLook look;
        look.text = tag;
        widths.push_back(kit::chip_width(*building.fonts, look));
        total += widths.back() + metrics.chip_gap;
    }
    const int32_t row_right = row.x + row.width;
    std::vector<bool> tag_shown(tags.size(), true);
    int32_t limit = row_right;
    if (left + total - metrics.chip_gap > row_right) {
        // Not every tag fits: TAGS ▾ takes the row's end and holds the rest.
        std::vector<bool> none(tags.size(), false);
        const std::vector<controls::MenuItem> every =
            controls::menu_items(library, Menu::tags, none);
        limit = row_right -
                drop_down_width(building, shown(library.text, words::tags_menu), every) -
                metrics.chip_gap;
    }
    for (std::size_t index = 0; index < tags.size(); ++index) {
        if (left + widths[index] > limit) {
            // The tags keep their order: once one does not fit, the rest go to TAGS ▾.
            std::fill(
                tag_shown.begin() + static_cast<std::ptrdiff_t>(index), tag_shown.end(), false
            );
            break;
        }
        kit::ChipLook look;
        look.text = tags[index].first;
        look.state = kit::ChipState::filter;
        look.selected = library.tag == tags[index].first;
        add_chip(
            look,
            controls::first_tag + static_cast<kit::ControlId>(index),
            named("tag." + name_word(tags[index].first))
        );
    }
    if (std::find(tag_shown.begin(), tag_shown.end(), false) == tag_shown.end())
        return;
    std::vector<controls::MenuItem> items = controls::menu_items(library, Menu::tags, tag_shown);
    const std::string_view caption = shown(library.text, words::tags_menu);
    const int32_t width = std::min(drop_down_width(building, caption, items), row_right - left);
    add_drop_down(
        building,
        building.top,
        {row_right - width,
         row.y + (row.height - metrics.regular_line) / 2,
         width,
         metrics.regular_line},
        Menu::tags,
        caption,
        std::move(items),
        controls::tags_menu,
        named("tags")
    );
}

/// Appends Large's filter pane: a nav list of All, Installed and Updates,
/// the heading TAGS and a nav list of the tab's tags with their counts,
/// TAGS ▾ holding those that do not fit; or, on the Updates tab, the
/// updates' count and UPDATE ALL.
///
/// @param[in,out] building the layout
/// @param pane the pane
void add_filter_pane(Building& building, const kit::Rect& pane) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
    kit::NavLook nav;
    nav.area = pane;
    nav.rule_column = pane.x + pane.width - 1;
    const int32_t inset = metrics.paragraph_gap;
    const int32_t entry_left = pane.x + inset;
    const int32_t entry_width = pane.width - 2 * inset;
    // The kit's nav draws an entry's caption from 12 columns in, and keeps 4 clear at its right.
    const int32_t caption_room =
        entry_width - kit::compact_metrics.padding - kit::compact_metrics.list_row_inset;
    if (library.tab == Tab::updates) {
        add_item(building.list, kit::Role::nav, pane, {}).look = std::move(nav);
        static_cast<void>(add_update_all(
            building, {entry_left, pane.y + inset, entry_width, metrics.button_height}
        ));
        return;
    }
    const int32_t entry_height = metrics.regular_line + metrics.list_row_inset;
    const int32_t step = entry_height + metrics.hairline;
    int32_t top = pane.y + inset;
    std::vector<kit::ControlId> ids;
    std::vector<std::string> names;
    const auto add_entry =
        [&](std::string caption, bool selected, kit::ControlId id, std::string name) {
            kit::NavEntry entry;
            entry.rect = {entry_left, top, entry_width, entry_height};
            entry.caption = cut(building, kit::FontRole::regular, caption, caption_room);
            entry.selected = selected;
            entry.hovered = hovered(building, id) || building.state->interaction.pressed == id;
            entry.focused = focused(building, id);
            nav.entries.push_back(std::move(entry));
            ids.push_back(id);
            names.push_back(unique_name(building, std::move(name)));
            top += step;
        };
    for (std::size_t index = 0; index < filter_order.size(); ++index)
        add_entry(
            std::string(filter_text(filter_order[index], library.text)),
            library.filter == filter_order[index],
            controls::filter_control(filter_order[index]),
            named("filter." + std::string(filter_words[index]))
        );
    const std::vector<std::pair<std::string, std::size_t>> tags = tags_in_tab(library);
    std::optional<kit::Rect> heading;
    std::vector<bool> tag_shown(tags.size(), false);
    if (!tags.empty()) {
        top += metrics.paragraph_gap;
        heading = kit::Rect{entry_left, top, entry_width, metrics.small_line};
        top += metrics.small_line + metrics.hint_gap;
        const int32_t bottom = pane.y + pane.height - inset;
        for (std::size_t index = 0; index < tags.size(); ++index) {
            const bool last = index + 1 == tags.size();
            // The last place left holds TAGS ▾ when tags remain after it.
            const int32_t needed = last ? entry_height : step + metrics.regular_line;
            if (top + needed > bottom)
                break;
            tag_shown[index] = true;
            add_entry(
                filled(
                    library.text,
                    words::tag_with_count,
                    {{"tag", tags[index].first}, {"count", std::to_string(tags[index].second)}}
                ),
                library.tag == tags[index].first,
                controls::first_tag + static_cast<kit::ControlId>(index),
                named("tag." + name_word(tags[index].first))
            );
        }
    }
    const std::size_t before = building.list.controls.size();
    if (nav.entries.empty())
        add_item(building.list, kit::Role::nav, pane, {}).look = nav;
    else
        kit::add_nav(building.list, nav, ids, names);
    move_controls(building.list, before, building.top);
    for (std::size_t index = building.top.size() - ids.size(); index < building.top.size(); ++index)
        tab_stop(building, building.top[index]);
    if (heading)
        add_item(building.list, kit::Role::heading, *heading, {}).look =
            kit::HeadingLook{std::string(shown(library.text, words::tags_menu))};
    if (std::find(tag_shown.begin(), tag_shown.end(), false) == tag_shown.end())
        return;
    std::vector<controls::MenuItem> items = controls::menu_items(library, Menu::tags, tag_shown);
    const std::string_view caption = shown(library.text, words::tags_menu);
    add_drop_down(
        building,
        building.top,
        {entry_left, top, entry_width, metrics.regular_line},
        Menu::tags,
        caption,
        std::move(items),
        controls::tags_menu,
        named("tags")
    );
}

/// Returns the words an empty list shows.
///
/// @param library the model
/// @return the words, looked up
std::string empty_words(const Library& library) {
    if (library.inputs.registries.empty())
        return std::string(shown(library.text, words::empty_no_registries));
    if (!library.query.empty())
        return filled(library.text, words::empty_matches, {{"query", library.query}});
    if (library.tab == Tab::updates)
        return std::string(shown(library.text, words::empty_updates));
    return std::string(shown(library.text, words::empty_tab));
}

/// Appends the list pane: the note on a list the last fetch could not
/// bring up to date, the rows that show, an empty list's words, the scroll
/// bar and the list's own control.
///
/// @param[in,out] building the layout
/// @param pane the pane
/// @param form how its rows are shown
void add_list(Building& building, const kit::Rect& pane, RowForm form) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
    kit::Rect area = pane;
    if (library.inputs.offline) {
        const std::string note = filled(
            library.text,
            words::offline_list,
            {{"age", age_text(library.inputs.list_age_seconds, library.text)}}
        );
        add_text(
            building,
            kit::FontRole::small,
            cut(building, kit::FontRole::small, note, area.width),
            {area.x, area.y, area.width, metrics.small_line},
            kit::Align::left,
            kit::colour::lock,
            {}
        );
        area.y += metrics.small_line + metrics.hint_gap;
        area.height -= metrics.small_line + metrics.hint_gap;
    }
    const int32_t height = row_height(metrics, form);
    const int32_t view_height = std::max(area.height, int32_t{0}) / height * height;
    const kit::Rect view{area.x, area.y, area.width, view_height};
    const int32_t well_width = metrics.scroll_thumb_width + 2;
    const kit::Rect well{view.x + view.width - well_width, view.y, well_width, view.height};
    const int32_t rows_width = view.width - well_width - metrics.button_gap;
    const auto content = static_cast<int32_t>(library.visible.size()) * height;
    const int32_t limit = kit::ScrollArea::limit_for(content, view.height);
    const int32_t offset = std::clamp(building.state->list_scroll, int32_t{0}, limit);

    if (library.visible.empty())
        add_text(
            building,
            kit::FontRole::small,
            cut(building, kit::FontRole::small, empty_words(library), rows_width),
            {view.x, view.y, rows_width, metrics.small_line},
            kit::Align::left,
            kit::colour::hint,
            {}
        );
    for (std::size_t place = 0; place < library.visible.size(); ++place) {
        const int32_t top = view.y + static_cast<int32_t>(place) * height - offset;
        if (top + height <= view.y || top >= view.y + view.height)
            continue;
        const Entry& entry = library.entries[library.visible[place]];
        const kit::ControlId id = controls::first_row + static_cast<kit::ControlId>(place);
        kit::ListRowLook look = row_look(building, entry, form);
        look.selected = library.selected.has_value() && *library.selected == entry.id;
        look.hovered = hovered(building, id);
        const kit::Rect rect{view.x, top, rows_width, height};
        const std::size_t before = building.list.controls.size();
        kit::add_list_row(building.list, rect, look, id, unique_name(building, row_name(entry)));
        building.list.items.back().clip = view;
        move_controls(building.list, before, building.rows);
        kit::Control& row = building.rows.back();
        row.clip = view;
        row.group = controls::list_group;
        row.focusable = false;
        row.text = std::string(entry_name(entry));
    }
    if (limit > 0) {
        kit::ScrollArea scroll;
        scroll.view = view;
        scroll.well = well;
        scroll.page_step = view.height;
        scroll.content_height = content;
        scroll.limit = limit;
        scroll.offset = offset;
        add_item(building.list, kit::Role::scroll_bar, well, {}).look =
            kit::ScrollBarLook{scroll.thumb(metrics.least_thumb_height), false};
    }
    kit::Control pane_control;
    pane_control.id = controls::list;
    pane_control.rect = view;
    pane_control.name = unique_name(building, named("list"));
    pane_control.kind = kit::ControlKind::list;
    building.panes.push_back(std::move(pane_control));
    tab_stop(building, building.panes.back());
}

/// Appends an entry's badge as the details show it: its picture, or its
/// letters on the badge's colour, as a row's badge.
///
/// @param[in,out] building the layout
/// @param entry the entry
/// @param rect the badge's square
/// @param clip what drawing may touch
void add_badge(
    Building& building, const Entry& entry, const kit::Rect& rect, const kit::Rect& clip
) {
    const oa::ui::frontend_renderer::RgbaPicture picture = badge_picture(entry);
    if (oa::ui::frontend_renderer::picture_drawable(picture)) {
        add_item(building.list, kit::Role::picture, rect, clip).look = kit::PictureLook{picture};
        add_item(building.list, kit::Role::outline, rect, clip).colour =
            kit::colour::control_border;
        return;
    }
    add_item(building.list, kit::Role::fill, rect, clip).colour = kit::colour::list_selected;
    const std::string letters = badge_letters(entry_name(entry));
    add_text(
        building,
        kit::FontRole::small,
        cut(building, kit::FontRole::small, letters, rect.width),
        rect,
        kit::Align::centre,
        kit::colour::on_accent,
        clip
    );
}

/// Tells whether a fact is the Rules fact.
///
/// @param library the model
/// @param fact the fact
/// @return true for Rules
bool rules_fact(const Library& library, const Fact& fact) {
    return fact.long_label == shown(library.text, words::fact_rules);
}

/// Tells whether a fact is the From fact.
///
/// @param library the model
/// @param fact the fact
/// @return true for From
bool from_fact(const Library& library, const Fact& fact) {
    return fact.long_label == shown(library.text, words::fact_from);
}

/// Appends an entry's facts as label and value columns, from a row.
///
/// Rules and From show their whole value; the others take at most a number
/// of lines, the last cut with "...". A value that is met ends with the
/// kit's check in the accent, and a warning is drawn in the lock colour.
///
/// @param[in,out] building the layout
/// @param entry the entry
/// @param area the columns the facts take, from their top
/// @param long_form the long labels and values, else the short
/// @param most_lines the most lines of a value other than Rules and From
/// @param clip what drawing may touch
/// @return the row under the last fact
int32_t add_facts(
    Building& building,
    const Entry& entry,
    const kit::Rect& area,
    bool long_form,
    std::size_t most_lines,
    const kit::Rect& clip
) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
    const std::vector<Fact> list = facts(library, entry);
    int32_t label_width = 0;
    for (const Fact& fact : list)
        label_width = std::max(
            label_width,
            width_of(building, kit::FontRole::small, long_form ? fact.long_label : fact.short_label)
        );
    const int32_t value_left = area.x + label_width + metrics.label_gap;
    const int32_t value_width = std::max(area.x + area.width - value_left, int32_t{0});
    const std::string_view met = shown(library.text, words::fact_met);
    const int32_t met_width = width_of(building, kit::FontRole::small, met);
    const int32_t space_width = width_of(building, kit::FontRole::small, " ");
    int32_t top = area.y;
    for (const Fact& fact : list) {
        const std::string& label = long_form ? fact.long_label : fact.short_label;
        const std::string& value = long_form ? fact.long_value : fact.short_value;
        const bool whole = rules_fact(library, fact) || from_fact(library, fact);
        const bool good = fact.mark == Mark::good;
        const int32_t room = good ? value_width - met_width - space_width : value_width;
        const std::vector<std::string> lines =
            wrapped(building, kit::FontRole::small, value, room, whole ? 0 : most_lines);
        add_text(
            building,
            kit::FontRole::small,
            cut(building, kit::FontRole::small, label, label_width),
            {area.x, top, label_width, metrics.small_line},
            kit::Align::left,
            kit::colour::hint,
            clip
        );
        const kit::Colour ink = fact.mark == Mark::warn ? kit::colour::lock : kit::colour::text;
        int32_t line_top = top;
        for (const std::string& line : lines) {
            add_text(
                building,
                kit::FontRole::small,
                line,
                {value_left, line_top, room, metrics.small_line},
                kit::Align::left,
                ink,
                clip
            );
            line_top += metrics.small_line;
        }
        if (good) {
            const int32_t last_width =
                lines.empty() ? 0 : width_of(building, kit::FontRole::small, lines.back());
            const int32_t met_top = lines.empty() ? top : line_top - metrics.small_line;
            add_text(
                building,
                kit::FontRole::small,
                std::string(met),
                {value_left + last_width + space_width, met_top, met_width, metrics.small_line},
                kit::Align::left,
                kit::colour::accent,
                clip
            );
        }
        const int32_t lines_height =
            std::max(static_cast<int32_t>(lines.size()), int32_t{1}) * metrics.small_line;
        const kit::Rect value_box{value_left, top, value_width, lines_height};
        if (rules_fact(library, fact))
            add_label(
                building.detail,
                controls::rules,
                value_box,
                unique_name(building, named("rules")),
                value,
                clip,
                controls::details_group
            );
        else if (from_fact(library, fact))
            add_label(
                building.detail,
                controls::from,
                value_box,
                unique_name(building, named("from")),
                fact.long_value,
                clip,
                controls::details_group
            );
        top += lines_height;
    }
    return top;
}

/// Appends lines of a text in the small font, one under another.
///
/// @param[in,out] building the layout
/// @param lines the lines
/// @param left their left column
/// @param top the first line's top row
/// @param width their width
/// @param ink their colour
/// @param clip what drawing may touch
/// @return the row under the last line
int32_t add_lines(
    Building& building,
    const std::vector<std::string>& lines,
    int32_t left,
    int32_t top,
    int32_t width,
    kit::Colour ink,
    const kit::Rect& clip
) {
    for (const std::string& line : lines) {
        add_text(
            building,
            kit::FontRole::small,
            line,
            {left, top, width, building.metrics->small_line},
            kit::Align::left,
            ink,
            clip
        );
        top += building.metrics->small_line;
    }
    return top;
}

/// Returns the colour an entry's status line is drawn in: the lock colour
/// while an update waits or GET or UPDATE cannot act, else the hint.
///
/// @param entry the entry
/// @return the colour
kit::Colour status_colour(const Entry& entry) {
    return entry.state == State::update || !entry.can_act ? kit::colour::lock : kit::colour::hint;
}

/// Appends the playing note, when the model has one, under a row.
///
/// @param[in,out] building the layout
/// @param left its left column
/// @param top its top row
/// @param width its width
/// @param clip what drawing may touch
/// @param scroll_group its label's scroll area
/// @return the row under it
int32_t add_note(
    Building& building,
    int32_t left,
    int32_t top,
    int32_t width,
    const kit::Rect& clip,
    int32_t scroll_group
) {
    const std::string note = playing_note(*building.library);
    if (note.empty())
        return top;
    const std::vector<std::string> lines = wrapped(building, kit::FontRole::small, note, width, 0);
    const int32_t bottom = add_lines(building, lines, left, top, width, kit::colour::hint, clip);
    add_label(
        building.detail,
        controls::note,
        {left, top, width, bottom - top},
        unique_name(building, named("note")),
        note,
        clip,
        scroll_group
    );
    return bottom;
}

/// Appends the details pane's parts for an entry, scrolled up by an offset.
///
/// @param[in,out] building the layout
/// @param entry the entry
/// @param view the pane, which clips them
/// @param width the columns they take from the pane's left
/// @param offset how far they are scrolled up, in points
/// @return the content's height, in points
int32_t add_details_content(
    Building& building, const Entry& entry, const kit::Rect& view, int32_t width, int32_t offset
) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
    const int32_t first = view.y - offset;
    const int32_t left = view.x;
    const std::size_t first_item = building.list.items.size();

    add_badge(building, entry, {left, first, details_badge_side, details_badge_side}, view);
    const int32_t text_left = left + details_badge_side + metrics.list_row_gap;
    const int32_t text_width = std::max(left + width - text_left, int32_t{0});
    kit::ChipLook chip = entry_chip(library, entry);
    const int32_t chip_width = kit::chip_width(*building.fonts, chip);
    const std::string name =
        cut(building,
            kit::FontRole::regular,
            entry_name(entry),
            text_width - chip_width - metrics.list_row_gap);
    const int32_t name_width = width_of(building, kit::FontRole::regular, name);
    add_text(
        building,
        kit::FontRole::regular,
        name,
        {text_left, first, name_width, metrics.regular_line},
        kit::Align::left,
        kit::colour::text,
        view
    );
    const kit::Rect chip_rect{
        text_left + name_width + metrics.list_row_gap,
        first + (metrics.regular_line - metrics.chip_height) / 2,
        chip_width,
        metrics.chip_height,
    };
    kit::add_chip(building.list, chip_rect, chip, kit::no_control, {});
    building.list.items.back().clip = view;
    const std::vector<std::string> status = wrapped(
        building, kit::FontRole::small, status_line(library, entry), text_width, wide_status_lines
    );
    const int32_t status_top = first + metrics.regular_line;
    const int32_t status_bottom =
        add_lines(building, status, text_left, status_top, text_width, status_colour(entry), view);
    add_label(
        building.detail,
        controls::status,
        {text_left,
         status_top,
         text_width,
         std::max(status_bottom - status_top, metrics.small_line)},
        unique_name(building, named("status")),
        joined(status),
        view,
        controls::details_group
    );
    int32_t top = first + std::max(details_badge_side, status_bottom - first) +
                  building.metrics->paragraph_gap;

    if (entry.listing != nullptr && !entry.listing->summary.empty()) {
        const std::vector<std::string> summary = wrapped(
            building, kit::FontRole::small, entry.listing->summary, width, wide_summary_lines
        );
        top = add_lines(building, summary, left, top, width, kit::colour::list_text, view) +
              metrics.paragraph_gap;
    }
    top = add_facts(building, entry, {left, top, width, 0}, true, wide_fact_lines, view) +
          metrics.paragraph_gap;

    const std::vector<ActionButton> actions = entry_actions(library, entry);
    const int32_t button_gap = metrics.button_gap;
    const int32_t each =
        std::max((width - (actions_per_line - 1) * button_gap) / actions_per_line, int32_t{0});
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const auto column = static_cast<int32_t>(index) % actions_per_line;
        const auto line = static_cast<int32_t>(index) / actions_per_line;
        const ActionButton& action = actions[index];
        add_button(
            building,
            building.detail,
            {left + column * (each + button_gap),
             top + line * (metrics.button_height + button_gap),
             each,
             metrics.button_height},
            action.text,
            index == 0,
            action.enabled,
            controls::action_control(action.action),
            named("action." + std::string(action_word(action.action))),
            view,
            controls::details_group
        );
    }
    if (!actions.empty()) {
        const auto lines =
            (static_cast<int32_t>(actions.size()) + actions_per_line - 1) / actions_per_line;
        top += lines * metrics.button_height + (lines - 1) * button_gap + metrics.paragraph_gap;
    }
    static_cast<void>(add_note(building, left, top, width, view, controls::details_group));
    const std::optional<controls::Extent> extent =
        controls::clipped_extent(building.list, view, first_item);
    return extent ? extent->bottom - first : 0;
}

/// Appends the details pane of the selected entry. When its parts are
/// taller than the pane they scroll, and the pane keeps a column for the
/// scroll bar at its right.
///
/// @param[in,out] building the layout
/// @param pane the pane
void add_details_pane(Building& building, const kit::Rect& pane) {
    const Entry* entry = selected_entry(*building.library);
    if (entry == nullptr)
        return;
    const kit::Metrics& metrics = *building.metrics;
    // Laid out once at the pane's width to learn its height, then again
    // narrower when that is taller than the pane.
    Building trial = building;
    trial.list = {};
    trial.detail.clear();
    trial.order.clear();
    const int32_t whole_height = add_details_content(trial, *entry, pane, pane.width, 0);
    const int32_t well_width = metrics.scroll_thumb_width + 2;
    const bool scrolls = whole_height > pane.height;
    const int32_t width = scrolls ? pane.width - well_width - metrics.button_gap : pane.width;
    int32_t height = whole_height;
    if (scrolls) {
        trial = building;
        trial.list = {};
        trial.detail.clear();
        trial.order.clear();
        height = add_details_content(trial, *entry, pane, width, 0);
    }
    const int32_t limit = kit::ScrollArea::limit_for(height, pane.height);
    const int32_t offset = std::clamp(building.state->details_scroll, int32_t{0}, limit);
    add_details_content(building, *entry, pane, width, offset);
    if (limit > 0) {
        kit::ScrollArea scroll;
        scroll.view = pane;
        scroll.well = {pane.x + pane.width - well_width, pane.y, well_width, pane.height};
        scroll.content_height = height;
        scroll.limit = limit;
        scroll.offset = offset;
        add_item(building.list, kit::Role::scroll_bar, scroll.well, {}).look =
            kit::ScrollBarLook{scroll.thumb(metrics.least_thumb_height), false};
    }
}

/// Appends the footer's band, the queue's words at its left and its buttons
/// at its right: SETTINGS…, DETAILS at Compact, and CLOSE.
///
/// @param[in,out] building the layout
/// @param window the window
/// @param compact the class is Compact: one queue line with its bar, and DETAILS
void add_list_footer(Building& building, const Window& window, bool compact) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
    add_item(
        building.list,
        kit::Role::footer_band,
        {0, window.footer_rule, window.width, metrics.footer_height + 1},
        {}
    )
        .look = kit::FooterBandLook{window.width, window.footer_rule};

    struct FooterButton {
        Action action{};
        kit::ControlId id{};
        bool enabled{};
    };

    std::vector<FooterButton> buttons{{Action::settings, controls::settings, true}};
    if (compact)
        buttons.push_back({Action::details, controls::details, library.selected.has_value()});
    buttons.push_back({Action::close, controls::close, true});
    int32_t total = 0;
    for (const FooterButton& button : buttons)
        total +=
            button_width(building, action_text(button.action, library.text)) + metrics.button_gap;
    total -= metrics.button_gap;
    int32_t left = window.right - total;
    const int32_t queue_right = left - metrics.label_gap;
    for (const FooterButton& button : buttons) {
        const std::string_view caption = action_text(button.action, library.text);
        const int32_t width = button_width(building, caption);
        add_button(
            building,
            building.footer,
            {left, window.button_top, width, metrics.button_height},
            caption,
            false,
            button.enabled,
            button.id,
            named(std::string(action_word(button.action))),
            {},
            -1
        );
        left += width + metrics.button_gap;
    }

    const QueueLines lines = queue_lines(library);
    const int32_t queue_width = std::max(queue_right - window.left, int32_t{0});
    if (compact) {
        if (lines.compact.empty())
            return;
        // A downloading item's bar under its line: the line, a hint's gap and the bar, centred.
        const QueueItem* downloading = nullptr;
        for (const QueueItem& item : library.inputs.queue)
            if (item.phase == QueueItem::Phase::downloading) {
                downloading = &item;
                break;
            }
        const int32_t bar_height =
            downloading != nullptr ? metrics.hint_gap + metrics.progress_bar_height : 0;
        const int32_t line_top =
            window.footer_top + (metrics.footer_height - metrics.small_line - bar_height) / 2;
        const std::string line = cut(building, kit::FontRole::small, lines.compact, queue_width);
        const kit::Rect box{window.left, line_top, queue_width, metrics.small_line};
        add_text(
            building, kit::FontRole::small, line, box, kit::Align::left, kit::colour::text, {}
        );
        add_label(
            building.footer,
            controls::queue,
            box,
            unique_name(building, named("queue")),
            line,
            {},
            -1
        );
        if (downloading != nullptr) {
            int32_t done = 0;
            if (downloading->total_bytes != 0)
                done = static_cast<int32_t>(
                    std::min(downloading->done_bytes, downloading->total_bytes) *
                    static_cast<uint64_t>(progress_whole) / downloading->total_bytes
                );
            kit::add_progress(
                building.list,
                {window.left,
                 box.y + box.height + metrics.hint_gap,
                 queue_width,
                 metrics.progress_bar_height},
                done,
                progress_whole
            );
        }
        return;
    }
    if (lines.first.empty())
        return;
    const int32_t line_count = lines.second.empty() ? 1 : 2;
    const int32_t line_top =
        window.footer_top + (metrics.footer_height - line_count * metrics.small_line) / 2;
    const std::string first = cut(building, kit::FontRole::small, lines.first, queue_width);
    const kit::Rect box{window.left, line_top, queue_width, metrics.small_line};
    add_text(building, kit::FontRole::small, first, box, kit::Align::left, kit::colour::text, {});
    add_label(
        building.footer, controls::queue, box, unique_name(building, named("queue")), first, {}, -1
    );
    if (line_count == 2)
        add_text(
            building,
            kit::FontRole::small,
            cut(building, kit::FontRole::small, lines.second, queue_width),
            {window.left, line_top + metrics.small_line, queue_width, metrics.small_line},
            kit::Align::left,
            kit::colour::hint,
            {}
        );
}

/// Appends the focus ring round the control that has the focus, unless the
/// control draws its own: a nav entry, or the search field's border.
///
/// @param[in,out] building the layout
void add_focus_ring(Building& building) {
    const kit::Interaction& interaction = building.state->interaction;
    if (!interaction.focus_shown || interaction.focused == kit::no_control ||
        interaction.focused == controls::search)
        return;
    for (const kit::Item& item : building.list.items)
        if (item.control == interaction.focused && item.role == kit::Role::nav)
            return;
    for (const auto* group : {&building.top, &building.panes, &building.detail, &building.footer}) {
        for (const kit::Control& control : *group) {
            if (control.id != interaction.focused || !control.focusable)
                continue;
            const bool on_edge =
                control.kind == kit::ControlKind::tab || control.kind == kit::ControlKind::list;
            add_item(building.list, kit::Role::focus_ring, control.rect, control.clip).look =
                kit::FocusRingLook{!on_edge};
            return;
        }
    }
}

/// Appends the open drop-down's menu over everything, and a control for
/// each item it shows.
///
/// @param[in,out] building the layout
/// @param footer_line the row the menu keeps above unless it opens upward
void add_open_menu(Building& building, int32_t footer_line) {
    if (!building.open)
        return;
    const OpenMenu& open = *building.open;
    const kit::Metrics& metrics = *building.metrics;
    const auto count = static_cast<int32_t>(open.items.size());
    const kit::Rect menu = kit::choice_menu(open.field, open.items.size(), footer_line);
    const int32_t shown_count = kit::shown_choices(open.items.size());
    const int32_t first = std::clamp(
        building.state->menu_first, int32_t{0}, std::max(count - shown_count, int32_t{0})
    );
    kit::ChoiceMenuLook look;
    look.first = first;
    look.total = count;
    look.chosen = open.chosen;
    look.marked = building.state->menu_marked;
    look.focus_shown = building.state->interaction.focus_shown;
    for (int32_t place = 0; place < shown_count && first + place < count; ++place) {
        const controls::MenuItem& item = open.items[static_cast<std::size_t>(first + place)];
        const kit::Rect box = kit::choice_item(menu, place);
        look.shown.push_back(
            cut(building,
                kit::FontRole::regular,
                item.caption,
                box.width - metrics.choice_item_text_inset - metrics.list_row_inset)
        );
        kit::Control control;
        control.id = controls::first_menu_item + first + place;
        control.rect = box;
        control.name = unique_name(building, open.name + "." + item.word);
        control.kind = kit::ControlKind::list_item;
        control.focusable = false;
        control.checked = first + place == open.chosen;
        control.text = look.shown.back();
        building.menu.push_back(std::move(control));
    }
    add_item(building.list, kit::Role::choice_menu, menu, {}).look = std::move(look);
}

/// Starts a layout: the window's face.
///
/// @param library the model
/// @param state the screen's state
/// @param frame the frame
/// @param fonts the fonts
/// @return the layout, with the face drawn
Building start(
    const Library& library,
    const ScreenState& state,
    const kit::Frame& frame,
    const kit::Fonts& fonts
) {
    Building building;
    building.library = &library;
    building.state = &state;
    building.fonts = &fonts;
    building.metrics = &kit::metrics_of(frame.size_class);
    building.size_class = frame.size_class;
    const kit::Point size = window_size(frame);
    add_item(building.list, kit::Role::fill, {0, 0, size.x, size.y}, {}).colour =
        kit::colour::panel;
    return building;
}

/// Finishes a layout: the window's edge, the focus ring, the open menu over
/// everything, and the controls in the order a press tries them.
///
/// @param[in,out] building the layout
/// @param window the window
/// @return the display list
kit::DisplayList finish(Building& building, const Window& window) {
    add_item(building.list, kit::Role::bevel, {0, 0, window.width, window.height}, {}).colour =
        kit::colour::edge_light;
    add_focus_ring(building);
    add_open_menu(building, window.footer_rule);
    kit::DisplayList list = std::move(building.list);
    list.controls.clear();
    for (auto* group :
         {&building.menu,
          &building.top,
          &building.rows,
          &building.panes,
          &building.detail,
          &building.footer})
        for (kit::Control& control : *group)
            list.controls.push_back(std::move(control));
    list.tab_order = std::move(building.order);
    return list;
}

} // namespace

namespace controls {

std::vector<MenuItem>
menu_items(const Library& library, Menu menu, const std::vector<bool>& tag_shown) {
    std::vector<MenuItem> items;
    switch (menu) {
    case Menu::none:
        break;
    case Menu::filter: {
        for (std::size_t index = 0; index < filter_order.size(); ++index) {
            MenuItem item;
            item.caption = std::string(filter_text(filter_order[index], library.text));
            item.word = std::string(filter_words[index]);
            item.filter = filter_order[index];
            items.push_back(std::move(item));
        }
        const std::vector<std::pair<std::string, std::size_t>> tags = tags_in_tab(library);
        for (std::size_t index = 0; index < tags.size(); ++index) {
            MenuItem item;
            item.caption = filled(
                library.text,
                words::filter_tag,
                {{"tag", tags[index].first}, {"count", std::to_string(tags[index].second)}}
            );
            item.word = "tag-" + name_word(tags[index].first);
            item.tag = index;
            items.push_back(std::move(item));
        }
        break;
    }
    case Menu::tags: {
        const std::vector<std::pair<std::string, std::size_t>> tags = tags_in_tab(library);
        for (std::size_t index = 0; index < tags.size(); ++index) {
            if (index < tag_shown.size() && tag_shown[index])
                continue;
            MenuItem item;
            item.caption = filled(
                library.text,
                words::tag_with_count,
                {{"tag", tags[index].first}, {"count", std::to_string(tags[index].second)}}
            );
            item.word = name_word(tags[index].first);
            item.tag = index;
            items.push_back(std::move(item));
        }
        break;
    }
    case Menu::more: {
        const Entry* entry = selected_entry(library);
        if (entry == nullptr)
            break;
        const std::vector<ActionButton> actions = entry_actions(library, *entry);
        for (std::size_t index = compact_action_buttons; index < actions.size(); ++index) {
            MenuItem item;
            item.caption = std::string(actions[index].text);
            item.word = std::string(action_word(actions[index].action));
            item.action = actions[index].action;
            items.push_back(std::move(item));
        }
        break;
    }
    }
    return items;
}

int32_t chosen_item(const Library& library, Menu menu, const std::vector<MenuItem>& items) {
    if (menu == Menu::more)
        return -1;
    const std::vector<std::pair<std::string, std::size_t>> tags = tags_in_tab(library);
    for (std::size_t place = 0; place < items.size(); ++place) {
        const MenuItem& item = items[place];
        const bool tag_chosen = item.tag && *item.tag < tags.size() && !library.tag.empty() &&
                                tags[*item.tag].first == library.tag;
        const bool filter_chosen =
            item.filter && library.tag.empty() && *item.filter == library.filter;
        if (tag_chosen || filter_chosen)
            return static_cast<int32_t>(place);
    }
    return -1;
}

std::optional<Extent>
clipped_extent(const kit::DisplayList& display, const kit::Rect& view, std::size_t from) {
    std::optional<Extent> extent;
    for (std::size_t index = from; index < display.items.size(); ++index) {
        const kit::Item& item = display.items[index];
        const bool same_clip = item.clip.x == view.x && item.clip.y == view.y &&
                               item.clip.width == view.width && item.clip.height == view.height;
        if (!same_clip || item.rect.height <= 0)
            continue;
        const int32_t bottom = item.rect.y + item.rect.height;
        if (!extent)
            extent = Extent{item.rect.y, bottom};
        extent->top = std::min(extent->top, item.rect.y);
        extent->bottom = std::max(extent->bottom, bottom);
    }
    return extent;
}

std::vector<bool> tags_shown(const kit::DisplayList& display, std::size_t tags) {
    std::vector<bool> shown_tags(tags, false);
    for (std::size_t index = 0; index < tags; ++index)
        shown_tags[index] =
            kit::control_of(display, first_tag + static_cast<kit::ControlId>(index)) != nullptr;
    return shown_tags;
}

} // namespace controls

kit::Point window_size(const kit::Frame& frame) noexcept {
    const kit::Metrics& metrics = kit::metrics_of(frame.size_class);
    kit::Point size{metrics.dialog_width, metrics.dialog_height};
    if (frame.area.width > 0)
        size.x = std::min(size.x, frame.area.width);
    if (frame.area.height > 0)
        size.y = std::min(size.y, frame.area.height);
    return size;
}

std::string badge_letters(std::string_view name) {
    std::vector<std::string_view> parts;
    std::size_t at = 0;
    while (at < name.size()) {
        while (at < name.size() && name[at] == ' ')
            ++at;
        const std::size_t word_start = at;
        while (at < name.size() && name[at] != ' ')
            ++at;
        if (at > word_start)
            parts.push_back(name.substr(word_start, at - word_start));
    }
    std::string letters;
    const auto take = [&letters](std::string_view part, std::size_t characters) {
        std::size_t used = 0;
        for (std::size_t count = 0; count < characters && used < part.size(); ++count)
            used += std::max(kit::character_bytes(part.substr(used)), std::size_t{1});
        letters += part.substr(0, used);
    };
    if (parts.size() == 1)
        take(parts.front(), badge_letter_count);
    else
        for (std::size_t index = 0; index < parts.size() && index < badge_letter_count; ++index)
            take(parts[index], 1);
    for (char& letter : letters)
        if (letter >= 'a' && letter <= 'z')
            letter = static_cast<char>(letter - 'a' + 'A');
    return letters;
}

kit::DisplayList library_layout(
    const Library& library,
    const ScreenState& state,
    const kit::Frame& frame,
    const kit::Fonts& fonts
) {
    if (frame.size_class == kit::SizeClass::compact && state.details_page && library.details_open &&
        selected_entry(library) != nullptr)
        return details_page_layout(library, state, frame, fonts);
    Building building = start(library, state, frame, fonts);
    const Window window = window_of(frame);
    const kit::Metrics& metrics = *building.metrics;
    const bool compact = frame.size_class == kit::SizeClass::compact;
    const bool large = frame.size_class == kit::SizeClass::large;
    if (compact) {
        add_header(
            building,
            window,
            shown(library.text, words::title_library),
            {},
            header_age_text(library.inputs, library.text)
        );
    } else {
        add_header(
            building,
            window,
            shown(library.text, words::title_open_annihilation),
            shown(library.text, words::title_library),
            filled(
                library.text,
                words::header_version_list,
                {{"version", library.inputs.version_text},
                 {"list", header_age_text(library.inputs, library.text)}}
            )
        );
    }
    const int32_t content_width = window.right - window.left;
    int32_t top = window.body_top + window.gap;
    if (compact) {
        add_tabs(building, {window.left, top, content_width, metrics.tab_height}, true);
        top += metrics.tab_height + window.gap;
        const int32_t row_height_points = std::max(metrics.field_height, metrics.button_height);
        add_compact_search_row(building, {window.left, top, content_width, row_height_points});
        top += row_height_points + window.gap;
        add_list(
            building,
            {window.left, top, content_width, window.footer_rule - window.gap - top},
            RowForm::compact
        );
    } else {
        add_tabs(
            building,
            {window.left,
             top,
             content_width - wide_search_width - window.padding,
             metrics.tab_height},
            false
        );
        add_search(
            building,
            {window.right - wide_search_width,
             top + (metrics.tab_height - metrics.field_height) / 2,
             wide_search_width,
             metrics.field_height}
        );
        top += metrics.tab_height + window.gap;
        if (!large) {
            add_chip_row(building, {window.left, top, content_width, metrics.button_height});
            top += metrics.button_height + window.gap;
        }
        const int32_t panes_height = window.footer_rule - window.gap - top;
        int32_t left = window.left;
        if (large) {
            add_filter_pane(building, {left, top, filter_pane_width, panes_height});
            left += filter_pane_width + window.padding;
        }
        const int32_t list_width = large ? large_list_width : regular_list_width;
        add_list(
            building,
            {left, top, list_width, panes_height},
            large ? RowForm::large : RowForm::regular
        );
        left += list_width + window.padding;
        add_details_pane(building, {left, top, window.right - left, panes_height});
    }
    add_list_footer(building, window, compact);
    return finish(building, window);
}

kit::DisplayList details_page_layout(
    const Library& library,
    const ScreenState& state,
    const kit::Frame& frame,
    const kit::Fonts& fonts
) {
    Building building = start(library, state, frame, fonts);
    const Window window = window_of(frame);
    const kit::Metrics& metrics = *building.metrics;
    const Entry* entry = selected_entry(library);
    if (entry == nullptr) {
        add_header(building, window, shown(library.text, words::title_library), {}, {});
        return finish(building, window);
    }
    add_header(building, window, entry_name(*entry), {}, entry_version(library, *entry));

    // The body scrolls between the header and the footer.
    const int32_t content_width = window.right - window.left;
    const int32_t body_top = window.body_top + window.gap;
    const kit::Rect view{
        window.left, body_top, content_width, window.footer_rule - window.gap - body_top
    };
    const auto lay_out_body = [&](Building& into, int32_t width, int32_t offset) {
        const int32_t first = view.y - offset;
        const std::size_t first_item = into.list.items.size();
        int32_t top = first;
        kit::LinkLook link;
        link.caption =
            cut(into,
                kit::FontRole::small,
                filled(
                    library.text,
                    words::back_to_tab,
                    {{"tab", std::string(tab_text(library.tab, false, library.text))}}
                ),
                width);
        link.hovered =
            hovered(into, controls::back) || into.state->interaction.pressed == controls::back;
        const kit::Rect link_rect{
            view.x, top, kit::link_width(*into.fonts, link), metrics.small_line
        };
        const std::size_t before = into.list.controls.size();
        kit::add_link(into.list, link_rect, link, controls::back, unique_name(into, named("back")));
        into.list.items.back().clip = view;
        move_controls(into.list, before, into.detail);
        into.detail.back().clip = view;
        into.detail.back().group = controls::details_group;
        tab_stop(into, into.detail.back());
        top += metrics.small_line + metrics.paragraph_gap;

        const std::string status =
            cut(into, kit::FontRole::small, status_line(library, *entry), width);
        const kit::Rect status_box{view.x, top, width, metrics.small_line};
        add_text(
            into,
            kit::FontRole::small,
            status,
            status_box,
            kit::Align::left,
            status_colour(*entry),
            view
        );
        add_label(
            into.detail,
            controls::status,
            status_box,
            unique_name(into, named("status")),
            status,
            view,
            controls::details_group
        );
        top += metrics.small_line;
        const std::string by = byline(*entry, library.text);
        if (!by.empty()) {
            add_text(
                into,
                kit::FontRole::small,
                cut(into, kit::FontRole::small, by, width),
                {view.x, top, width, metrics.small_line},
                kit::Align::left,
                kit::colour::hint,
                view
            );
            top += metrics.small_line;
        }
        top += metrics.paragraph_gap;
        if (entry->listing != nullptr && !entry->listing->summary.empty()) {
            const std::vector<std::string> summary = wrapped(
                into, kit::FontRole::small, entry->listing->summary, width, compact_summary_lines
            );
            top = add_lines(into, summary, view.x, top, width, kit::colour::list_text, view) +
                  metrics.paragraph_gap;
        }
        top = add_facts(into, *entry, {view.x, top, width, 0}, false, compact_fact_lines, view) +
              metrics.paragraph_gap;
        static_cast<void>(add_note(into, view.x, top, width, view, controls::details_group));
        const std::optional<controls::Extent> extent =
            controls::clipped_extent(into.list, view, first_item);
        return extent ? extent->bottom - first : 0;
    };
    Building trial = building;
    const int32_t whole_height = lay_out_body(trial, view.width, 0);
    const int32_t well_width = metrics.scroll_thumb_width + 2;
    const bool scrolls = whole_height > view.height;
    const int32_t width = scrolls ? view.width - well_width - metrics.button_gap : view.width;
    int32_t height = whole_height;
    if (scrolls) {
        trial = building;
        height = lay_out_body(trial, width, 0);
    }
    const int32_t limit = kit::ScrollArea::limit_for(height, view.height);
    const int32_t offset = std::clamp(state.details_scroll, int32_t{0}, limit);
    lay_out_body(building, width, offset);
    if (limit > 0) {
        kit::ScrollArea scroll;
        scroll.view = view;
        scroll.well = {view.x + view.width - well_width, view.y, well_width, view.height};
        scroll.content_height = height;
        scroll.limit = limit;
        scroll.offset = offset;
        add_item(building.list, kit::Role::scroll_bar, scroll.well, {}).look =
            kit::ScrollBarLook{scroll.thumb(metrics.least_thumb_height), false};
    }

    // The footer: the first three actions, MORE ▾ with the rest, and CLOSE.
    add_item(
        building.list,
        kit::Role::footer_band,
        {0, window.footer_rule, window.width, metrics.footer_height + 1},
        {}
    )
        .look = kit::FooterBandLook{window.width, window.footer_rule};
    const std::vector<ActionButton> actions = entry_actions(library, *entry);
    int32_t left = window.left;
    for (std::size_t index = 0; index < actions.size() && index < compact_action_buttons; ++index) {
        const ActionButton& action = actions[index];
        const int32_t width_points = button_width(building, action.text);
        add_button(
            building,
            building.footer,
            {left, window.button_top, width_points, metrics.button_height},
            action.text,
            index == 0,
            action.enabled,
            controls::action_control(action.action),
            named("action." + std::string(action_word(action.action))),
            {},
            -1
        );
        left += width_points + metrics.button_gap;
    }
    if (actions.size() > compact_action_buttons) {
        std::vector<controls::MenuItem> items = controls::menu_items(library, Menu::more, {});
        const std::string_view caption = shown(library.text, words::more_menu);
        const int32_t width_points = drop_down_width(building, caption, items);
        add_drop_down(
            building,
            building.footer,
            {left,
             window.button_top + (metrics.button_height - metrics.regular_line) / 2,
             width_points,
             metrics.regular_line},
            Menu::more,
            caption,
            std::move(items),
            controls::more_menu,
            named("action")
        );
    }
    const std::string_view close_caption = action_text(Action::close, library.text);
    const int32_t close_width = button_width(building, close_caption);
    add_button(
        building,
        building.footer,
        {window.right - close_width, window.button_top, close_width, metrics.button_height},
        close_caption,
        false,
        true,
        controls::close,
        named("close"),
        {},
        -1
    );
    return finish(building, window);
}

} // namespace oa::ui::library
