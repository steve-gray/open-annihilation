// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's layout at Compact, Regular and Large, made of the UI kit's
// components: the window's face, header and footer, the tabs, the search,
// the filters with their counts, the list and the details, the Active tab's
// setup, and the Registries tab's cards, every control named for automation.
// It reads the model and decides none of its rules.
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
#include "oa/ui/library/uninstall.hpp"
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

/// The columns Compact's search row keeps right of the search field, for the
/// filter drop-down and its count, such as "Installed (12)".
constexpr int32_t compact_filter_room = 128;
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
/// The face behind a badge's letters when its package has no picture. The
/// kit's row draws the letters in the ink for the accent (colour::on_accent),
/// which reads on this light grey at every size class.
constexpr kit::Colour badge_face = kit::colour::hint;
/// The most bytes one word of an automation name keeps.
constexpr std::size_t most_name_word_bytes = 40;
/// The letters on the base game's badge: the 3.1c mark.
constexpr std::string_view base_badge_name = "3.1c";
/// A full progress bar, in the bar's own units.
constexpr int32_t progress_whole = 1000;
/// The prefix of every control's name.
constexpr std::string_view name_prefix = "library.";

/// The tabs, in the order they are shown.
constexpr std::array<Tab, tab_total> tab_order{
    Tab::active, Tab::mods, Tab::maps, Tab::languages, Tab::updates, Tab::registries
};
/// The words the tabs are named by, in that order.
constexpr std::array<std::string_view, tab_total> tab_words{
    "active", "mods", "maps", "languages", "updates", "registries"
};
/// The filters, in the order they are shown.
constexpr std::array<Filter, filter_total> filter_order{
    Filter::all, Filter::newest, Filter::installed, Filter::updates
};
/// The words the filters are named by, in that order.
constexpr std::array<std::string_view, filter_total> filter_words{
    "all", "new", "installed", "updates"
};

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
    case Action::switch_off:
        return "switch-off";
    case Action::open_folder:
        return "open-folder";
    case Action::homepage:
        return "homepage";
    case Action::details:
        return "details";
    case Action::back:
        return "back";
    case Action::check_now:
        return "refresh";
    case Action::settings:
        return "settings";
    case Action::close:
        return "close";
    case Action::add_registry:
        return "add-registry";
    case Action::add_mirror:
        return "add-mirror";
    case Action::remove_registry:
        return "remove";
    case Action::set_registry:
        return "registry";
    case Action::refresh_registry:
        return "refresh-registry";
    case Action::uninstall:
        return "uninstall";
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

/// Returns the word a registry's controls are named by.
///
/// @param id the registry's id
/// @return the word; "registry" when the id keeps none
std::string registry_word(std::string_view id) {
    std::string word = name_word(id);
    if (word == "x")
        return "registry";
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
    MenuKind menu{MenuKind::none};           ///< which
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
    int32_t scale_percent{100};    ///< the frame's scale, in percent; the base badge is sized to it
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
    const kit::Measure measure = [fonts, role](std::string_view measured) {
        return kit::text_width(*fonts, role, measured);
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
/// @param in_tab_order false draws it and leaves Tab's order for the caller
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
    int32_t scroll_group,
    bool in_tab_order = true
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
    if (in_tab_order)
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

/// Returns the kit mark a maker shows.
///
/// @param maker the maker
/// @return the mark; none when the maker is none
std::optional<kit::MakerMark> shown_maker(PackageMaker maker) noexcept {
    switch (maker) {
    case PackageMaker::open_annihilation:
        return kit::MakerMark::open_annihilation;
    case PackageMaker::cavedog:
        return kit::MakerMark::bone;
    case PackageMaker::none:
        return std::nullopt;
    }
    return std::nullopt;
}

/// Returns a row's read text: its name, and the maker when a mark shows.
///
/// @param name the package's name
/// @param maker the maker the row shows; none adds nothing
/// @return the text
std::string maker_reading(std::string name, PackageMaker maker) {
    if (maker == PackageMaker::open_annihilation)
        name += " · Open Annihilation";
    else if (maker == PackageMaker::cavedog)
        name += " · Cavedog Entertainment";
    return name;
}

/// Returns the state icon a state is shown with: a download arrow for GET,
/// a circled up-arrow for UPDATE, and a circled check for INSTALLED and
/// PLAYING.
///
/// @param state the entry's state
/// @return the icon
kit::StateIcon icon_of(State state) {
    switch (state) {
    case State::get:
        return kit::StateIcon::download_arrow;
    case State::update:
        return kit::StateIcon::circled_up_arrow;
    case State::installed:
    case State::playing:
        return kit::StateIcon::circled_check;
    }
    return kit::StateIcon::circled_check;
}

/// Returns an entry's chip: its state's icon, dimmed when GET or UPDATE
/// cannot act, its state's word kept as its text for automation.
///
/// @param library the model
/// @param entry the entry
/// @return the chip
kit::ChipLook entry_chip(const Library& library, const Entry& entry) {
    kit::ChipLook chip;
    chip.text = std::string(state_text(entry.state, library.text));
    chip.state = chip_state_of(entry.state);
    chip.disabled = !entry.can_act;
    chip.icon = icon_of(entry.state);
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

/// Returns a badge's picture.
///
/// @param badge the badge; may be null
/// @return the picture, which points into the badge; empty without one
oa::ui::frontend_renderer::RgbaPicture picture_of(const Badge* badge) {
    oa::ui::frontend_renderer::RgbaPicture picture;
    if (badge == nullptr)
        return picture;
    picture.width = badge->width;
    picture.height = badge->height;
    picture.pixels = std::span<const uint8_t>(badge->rgba);
    return picture;
}

/// Returns an entry's badge picture: its own, else its listing's, else its
/// installed copy's.
///
/// @param entry the entry
/// @return the picture; empty without one
oa::ui::frontend_renderer::RgbaPicture badge_picture(const Entry& entry) {
    const Badge* badge = entry.badge;
    if (badge == nullptr && entry.listing != nullptr && entry.listing->badge)
        badge = &*entry.listing->badge;
    else if (badge == nullptr && entry.installed != nullptr && entry.installed->badge)
        badge = &*entry.installed->badge;
    return picture_of(badge);
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
    look.badge_colour = badge_face;
    look.title = std::string(entry_name(entry));
    look.chip = entry_chip(library, entry);
    look.maker = shown_maker(entry_maker(entry));
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
    else if (is_base_game(entry.id)) {
        const std::string_view about = entry_summary(entry);
        if (!about.empty())
            look.lines.push_back(std::string(about));
    }
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
/// version at the right. right_inset keeps columns clear inside the padding
/// for the refresh icon and, when the last refresh failed, the offline mark.
///
/// @param[in,out] building the layout
/// @param window the window
/// @param title the title, cut to fit when it is an entry's name
/// @param second the word after the title; empty for none
/// @param right the version at the right, cut to fit
/// @param right_inset the columns kept clear inside the padding
void add_header(
    Building& building,
    const Window& window,
    std::string_view title,
    std::string_view second,
    std::string_view right,
    int32_t right_inset
) {
    const kit::Metrics& compact = kit::compact_metrics;
    const int32_t tracking = building.metrics->heading_tracking;
    // The kit's header keeps Compact's places from the window's edges.
    const int32_t title_left = compact.padding + compact.mark_side + compact.header_gap;
    const int32_t right_edge = window.width - compact.edge - compact.padding - right_inset;
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
    look.right_inset = right_inset;
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

/// Returns the columns the header keeps clear for the refresh icon, and for
/// the offline mark when the last refresh could not reach a registry.
///
/// @param building the layout
/// @return the columns, inside the header's padding
int32_t header_reserve(const Building& building) {
    const kit::Metrics& compact = kit::compact_metrics;
    int32_t reserve = compact.button_height + compact.header_gap;
    if (building.library->inputs.offline) {
        const std::string_view mark = shown(building.library->text, words::offline_mark);
        reserve += width_of(building, kit::FontRole::small, mark) + compact.header_gap;
    }
    return reserve;
}

/// Returns how long a registry's refresh mark has been showing its check.
///
/// @param state the marks
/// @param id the registry
/// @return the milliseconds; 0 while it shows the arrow or is turning
uint16_t registry_check_ms(const ScreenState& state, std::string_view id) noexcept {
    for (const ScreenState::RegistryMark& mark : state.registry_marks)
        if (mark.id == id)
            return mark.check_ms;
    return 0;
}

/// Appends the header's refresh icon, at the top right, the same square at
/// every size class. Its words are "Refresh", or "Refreshing" while the
/// registries are being asked. It can be pressed only when a registry can
/// be asked, including while the check shows.
///
/// @param[in,out] building the layout
/// @param window the window
void add_refresh_button(Building& building, const Window& window) {
    const Library& library = *building.library;
    const kit::Metrics& compact = kit::compact_metrics;
    const int32_t side = compact.button_height;
    const kit::Rect rect{
        window.width - compact.edge - compact.padding - side,
        compact.edge + (compact.header_height - side) / 2,
        side,
        side,
    };
    const bool turning = library.inputs.checking;
    kit::ButtonLook look;
    look.style = kit::ButtonStyle::quiet;
    look.hovered = hovered(building, controls::refresh);
    look.held = held(building, controls::refresh);
    look.enabled = can_check_now(library);
    look.mark = kit::ButtonMark::refresh;
    look.turn = building.state->refresh_turn;
    look.turning = turning;
    look.check_ms = turning ? uint16_t{0} : building.state->refresh_check_ms;
    const std::size_t before = building.list.controls.size();
    kit::add_button(
        building.list, rect, look, controls::refresh, unique_name(building, named("refresh"))
    );
    move_controls(building.list, before, building.top);
    building.top.back().text = std::string(
        shown(library.text, turning ? words::action_refreshing : words::action_refresh)
    );
    tab_stop(building, building.top.back());
}

/// Appends the word "offline" in the lock colour, immediately left of the
/// refresh icon, when the last refresh could not reach a registry.
///
/// @param[in,out] building the layout
/// @param window the window
void add_offline_mark(Building& building, const Window& window) {
    const Library& library = *building.library;
    if (!library.inputs.offline)
        return;
    const kit::Metrics& compact = kit::compact_metrics;
    const std::string mark(shown(library.text, words::offline_mark));
    const int32_t mark_width = width_of(building, kit::FontRole::small, mark);
    const int32_t side = compact.button_height;
    const int32_t icon_left = window.width - compact.edge - compact.padding - side;
    const kit::Rect box{
        icon_left - compact.header_gap - mark_width,
        compact.edge,
        std::max(mark_width, int32_t{0}),
        compact.header_height,
    };
    add_text(building, kit::FontRole::small, mark, box, kit::Align::right, kit::colour::lock, {});
}

/// Appends the header, the offline mark and the refresh icon.
///
/// @param[in,out] building the layout
/// @param window the window
/// @param title the title
/// @param second the word after the title; empty for none
/// @param version the version at the right
void add_library_header(
    Building& building,
    const Window& window,
    std::string_view title,
    std::string_view second,
    std::string_view version
) {
    add_header(building, window, title, second, version, header_reserve(building));
    add_offline_mark(building, window);
    add_refresh_button(building, window);
}

/// Returns a tab's title, with its count in brackets after every tab but Active.
///
/// @param library the model
/// @param tab the tab
/// @param short_name the short title
/// @return the title, such as "Mods (13)" or "Active"
std::string tab_caption(const Library& library, Tab tab, bool short_name) {
    const std::string title(tab_text(tab, short_name, library.text));
    if (tab == Tab::active)
        return title;
    return filled(
        library.text,
        words::tab_with_count,
        {{"tab", title}, {"count", std::to_string(tab_count(library, tab))}}
    );
}

/// Appends the strip of tabs. Each tab after Active shows its count in
/// brackets after its title, in that title's font and colour. Active has
/// no count. A strip too narrow for the full titles takes the short names,
/// with the counts the same way. The counts the kit would draw beside a
/// caption stay unset, so the brackets are the only digits.
///
/// @param[in,out] building the layout
/// @param strip the strip
/// @param compact the tabs take their short names, as they also do when the
///     full names and counts do not fit the strip
void add_tabs(Building& building, const kit::Rect& strip, bool compact) {
    const Library& library = *building.library;
    // A strip too narrow for the full names and their counts takes the short
    // names, as Compact's does.
    if (!compact && building.fonts != nullptr) {
        kit::TabsLook full;
        full.count_style = kit::TabCountStyle::words;
        for (const Tab tab : tab_order) {
            full.captions.push_back(tab_caption(library, tab, false));
            full.counts.push_back(-1);
        }
        const std::vector<kit::Rect> rects = kit::tab_rects(*building.fonts, strip, full);
        compact = !rects.empty() && rects.back().x + rects.back().width > strip.x + strip.width;
    }
    kit::TabsLook look;
    look.count_style = kit::TabCountStyle::words;
    std::array<kit::ControlId, tab_order.size()> ids{};
    std::array<std::string, tab_order.size()> names{};
    for (std::size_t index = 0; index < tab_order.size(); ++index) {
        const Tab tab = tab_order[index];
        const std::string caption = tab_caption(library, tab, compact);
        look.captions.push_back(caption);
        look.counts.push_back(-1);
        look.count_amber.push_back(0);
        if (tab == Tab::active)
            look.readings.emplace_back();
        else
            look.readings.push_back(caption);
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
/// @return the words, looked up; none on Active and Registries, which have no search
std::string_view placeholder_of(const Library& library) {
    switch (library.tab) {
    case Tab::active:
    case Tab::registries:
        return {};
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
    MenuKind menu,
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

/// Appends the Updates tab's line over its list: "{n} updates" at its
/// left, counting the updates the list shows, and UPDATE ALL at its right.
///
/// @param[in,out] building the layout
/// @param row the row
void add_updates_line(Building& building, const kit::Rect& row) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
    const std::string_view caption = action_text(Action::update_all, library.text);
    const int32_t width = std::min(button_width(building, caption), row.width);
    const kit::Rect button{row.x + row.width - width, row.y, width, metrics.button_height};
    const std::size_t shown_count = updates_shown(library);
    const std::string count = filled(
        library.text,
        shown_count == 1 ? words::update_count_one : words::update_count_many,
        {{"count", std::to_string(shown_count)}}
    );
    const int32_t count_room = std::max(button.x - metrics.label_gap - row.x, int32_t{0});
    const kit::Rect words_box{row.x, row.y, count_room, metrics.button_height};
    add_text(
        building,
        kit::FontRole::small,
        cut(building, kit::FontRole::small, count, count_room),
        words_box,
        kit::Align::left,
        kit::colour::hint,
        {}
    );
    add_button(
        building,
        building.top,
        button,
        caption,
        true,
        update_count(library) > 0,
        controls::update_all,
        named("action.update-all"),
        {},
        -1
    );
}

/// Appends Compact's search row: the search field and the filter drop-down.
///
/// @param[in,out] building the layout
/// @param row the row
void add_compact_search_row(Building& building, const kit::Rect& row) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
    const int32_t field_width = std::max(row.width - compact_filter_room, int32_t{0});
    add_search(building, {row.x, row.y, field_width, metrics.field_height});
    std::vector<controls::MenuItem> items = controls::menu_items(library, MenuKind::filter, {});
    const int32_t chosen = controls::chosen_item(library, MenuKind::filter, items);
    const std::string caption =
        chosen >= 0 ? items[static_cast<std::size_t>(chosen)].caption : std::string();
    add_drop_down(
        building,
        building.top,
        {row.x + field_width + metrics.button_gap,
         row.y,
         compact_filter_room - metrics.button_gap,
         metrics.regular_line},
        MenuKind::filter,
        caption,
        std::move(items),
        controls::filter_menu,
        named("filter")
    );
}

/// Appends Regular's chips: All, New, Installed and Updates with their
/// counts, the tab's tags while they fit, and TAGS ▾ with the rest.
///
/// @param[in,out] building the layout
/// @param row the row
void add_chip_row(Building& building, const kit::Rect& row) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
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
        look.text = filter_caption(library, filter_order[index]);
        look.state = kit::ChipState::filter;
        look.selected = shown_filter(library) == filter_order[index];
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
            controls::menu_items(library, MenuKind::tags, none);
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
    std::vector<controls::MenuItem> items =
        controls::menu_items(library, MenuKind::tags, tag_shown);
    const std::string_view caption = shown(library.text, words::tags_menu);
    const int32_t width = std::min(drop_down_width(building, caption, items), row_right - left);
    add_drop_down(
        building,
        building.top,
        {row_right - width,
         row.y + (row.height - metrics.regular_line) / 2,
         width,
         metrics.regular_line},
        MenuKind::tags,
        caption,
        std::move(items),
        controls::tags_menu,
        named("tags")
    );
}

/// Appends Large's filter pane: a nav list of All, New, Installed and
/// Updates with their counts, the heading TAGS and a nav list of the tab's
/// tags with their counts, TAGS ▾ holding those that do not fit.
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
            filter_caption(library, filter_order[index]),
            shown_filter(library) == filter_order[index],
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
    std::vector<controls::MenuItem> items =
        controls::menu_items(library, MenuKind::tags, tag_shown);
    const std::string_view caption = shown(library.text, words::tags_menu);
    add_drop_down(
        building,
        building.top,
        {entry_left, top, entry_width, metrics.regular_line},
        MenuKind::tags,
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
    if (shown_filter(library) == Filter::updates)
        return std::string(shown(library.text, words::empty_updates));
    return std::string(shown(library.text, words::empty_tab));
}

/// The most bytes one scaled base-game badge may take. A larger square keeps
/// the icon as it was read, and the kit scales that.
constexpr uint64_t most_scaled_badge_bytes = 8U * 1024U * 1024U;

/// Returns the square a list row's badge is drawn in. The kit's row uses
/// Compact's inset and badge side at every size class.
///
/// @param row the row
/// @return the square, inside the row
kit::Rect row_badge_square(const kit::Rect& row) noexcept {
    const kit::Metrics& metrics = kit::compact_metrics;
    const int32_t inset = metrics.list_row_inset;
    const int32_t side =
        std::max(std::min(row.height - 2 * inset, metrics.list_row_badge), int32_t{0});
    return {row.x + inset, row.y + (row.height - side) / 2, side, side};
}

/// Tells whether a badge is the base game's icon.
///
/// @param library the model
/// @param badge the badge; may be null
/// @return true when it is Inputs::base_game_badge
bool base_badge(const Library& library, const Badge* badge) noexcept {
    return library.inputs.base_game_badge.has_value() && badge == &*library.inputs.base_game_badge;
}

/// Tells whether a picture is the base game's icon, still at the size it was read.
///
/// @param library the model
/// @param picture the picture
/// @return true when its pixels are Inputs::base_game_badge
bool base_picture(
    const Library& library, const oa::ui::frontend_renderer::RgbaPicture& picture
) noexcept {
    if (!library.inputs.base_game_badge)
        return false;
    const Badge& badge = *library.inputs.base_game_badge;
    return picture.width == badge.width && picture.height == badge.height &&
           picture.pixels.data() == badge.rgba.data();
}

/// Returns how many surface pixels a badge square covers at this layout's scale.
/// A whole scale is that many times the square; any other scale rounds each
/// edge on its own, as the canvas does. The Library's canvas starts at the origin.
///
/// @param building the layout
/// @param rect the badge's square, in points
/// @return the pixels; empty when the square covers none
kit::Point badge_pixel_size(const Building& building, const kit::Rect& rect) noexcept {
    if (rect.width <= 0 || rect.height <= 0)
        return {};
    const kit::Rect drawn = kit::to_canvas(rect, building.scale_percent);
    if (drawn.width <= 0 || drawn.height <= 0)
        return {};
    return {drawn.width, drawn.height};
}

/// Scales an icon to a pixel square, nearest-neighbour, whole pixels, and
/// centres it. A scale of one or more copies each source pixel into a block;
/// a smaller scale keeps the icon's shape and samples one source pixel for
/// each drawn pixel. The square around it is clear.
///
/// @param picture the icon
/// @param dest_columns the square's columns
/// @param dest_rows the square's rows
/// @return the square, dest_columns by dest_rows, or empty when it would be too large
std::vector<uint8_t> scale_nearest(
    const oa::ui::frontend_renderer::RgbaPicture& picture, uint32_t dest_columns, uint32_t dest_rows
) {
    if (dest_columns == 0 || dest_rows == 0 || dest_rows > most_scaled_badge_bytes / 4U ||
        uint64_t{dest_columns} > most_scaled_badge_bytes / 4U / dest_rows)
        return {};
    std::vector<uint8_t> scaled(static_cast<std::size_t>(dest_columns) * dest_rows * 4U, 0);
    const uint32_t source_columns = picture.width;
    const uint32_t source_rows = picture.height;
    if (source_columns == 0 || source_rows == 0)
        return scaled;
    uint32_t drawn_columns = dest_columns;
    uint32_t drawn_rows = dest_rows;
    uint32_t block = 0;
    if (dest_columns / source_columns >= 1U && dest_rows / source_rows >= 1U) {
        block = std::min(dest_columns / source_columns, dest_rows / source_rows);
        drawn_columns = source_columns * block;
        drawn_rows = source_rows * block;
    } else if (uint64_t{source_columns} * dest_rows >= uint64_t{source_rows} * dest_columns) {
        drawn_columns = dest_columns;
        drawn_rows = static_cast<uint32_t>(uint64_t{dest_columns} * source_rows / source_columns);
    } else {
        drawn_rows = dest_rows;
        drawn_columns = static_cast<uint32_t>(uint64_t{dest_rows} * source_columns / source_rows);
    }
    if (drawn_columns == 0)
        drawn_columns = 1;
    if (drawn_rows == 0)
        drawn_rows = 1;
    if (drawn_columns > dest_columns)
        drawn_columns = dest_columns;
    if (drawn_rows > dest_rows)
        drawn_rows = dest_rows;
    const uint32_t origin_column = (dest_columns - drawn_columns) / 2U;
    const uint32_t origin_row = (dest_rows - drawn_rows) / 2U;
    const uint8_t* source = picture.pixels.data();
    for (uint32_t row = 0; row < drawn_rows; ++row) {
        uint32_t source_row = block >= 1U ? row / block : row * source_rows / drawn_rows;
        if (source_row >= source_rows)
            source_row = source_rows - 1U;
        for (uint32_t column = 0; column < drawn_columns; ++column) {
            uint32_t source_column =
                block >= 1U ? column / block : column * source_columns / drawn_columns;
            if (source_column >= source_columns)
                source_column = source_columns - 1U;
            const uint8_t* from =
                source + (static_cast<std::size_t>(source_row) * source_columns + source_column) *
                             oa::ui::frontend_renderer::picture_pixel_bytes;
            uint8_t* to =
                scaled.data() + (static_cast<std::size_t>(origin_row + row) * dest_columns +
                                 origin_column + column) *
                                    oa::ui::frontend_renderer::picture_pixel_bytes;
            to[0] = from[0];
            to[1] = from[1];
            to[2] = from[2];
            to[3] = from[3];
        }
    }
    return scaled;
}

/// Returns the base game's icon scaled to the pixels of a badge square.
/// The pixels live on the Library until the next layout.
///
/// @param building the layout
/// @param picture the icon, at the size it was read
/// @param rect the badge's square, in points
/// @return the scaled picture, or `picture` when this square is not drawn
oa::ui::frontend_renderer::RgbaPicture fitted_base_picture(
    Building& building, const oa::ui::frontend_renderer::RgbaPicture& picture, const kit::Rect& rect
) {
    const kit::Point pixels = badge_pixel_size(building, rect);
    if (pixels.x <= 0 || pixels.y <= 0)
        return picture;
    const auto columns = static_cast<uint32_t>(pixels.x);
    const auto rows = static_cast<uint32_t>(pixels.y);
    std::vector<uint8_t> scaled = scale_nearest(picture, columns, rows);
    if (scaled.empty())
        return picture;
    std::vector<uint8_t>& kept =
        building.library->scaled_badge_pixels.emplace_back(std::move(scaled));
    oa::ui::frontend_renderer::RgbaPicture fitted;
    fitted.width = columns;
    fitted.height = rows;
    fitted.pixels = kept;
    return fitted;
}

/// Returns a map picture as the painter takes one.
///
/// @param picture the minimap
/// @return the picture, which points into it
oa::ui::frontend_renderer::RgbaPicture picture_of_map(const MapPicture& picture) {
    oa::ui::frontend_renderer::RgbaPicture drawn;
    drawn.width = picture.width;
    drawn.height = picture.height;
    drawn.pixels = std::span<const uint8_t>(picture.rgba);
    return drawn;
}

/// Fits a minimap into a box, nearest-neighbour, and fills the letterbox and
/// every transparent pixel with the list's well colour. The result is the
/// box's pixel size, so drawing it into that box does not stretch it. The
/// pixels live on the Library until the next layout.
///
/// @param building the layout
/// @param picture the minimap
/// @param rect the box, in points
/// @return the fitted picture; empty when the box covers no pixels
oa::ui::frontend_renderer::RgbaPicture fitted_well_picture(
    Building& building, const oa::ui::frontend_renderer::RgbaPicture& picture, const kit::Rect& rect
) {
    const kit::Point pixels = badge_pixel_size(building, rect);
    if (pixels.x <= 0 || pixels.y <= 0)
        return {};
    const auto columns = static_cast<uint32_t>(pixels.x);
    const auto rows = static_cast<uint32_t>(pixels.y);
    std::vector<uint8_t> scaled = scale_nearest(picture, columns, rows);
    if (scaled.empty())
        return {};
    const kit::Colour well = kit::colour::well;
    for (std::size_t index = 0; index + 3 < scaled.size(); index += 4) {
        if (scaled[index + 3] != 0)
            continue;
        scaled[index] = well.r;
        scaled[index + 1] = well.g;
        scaled[index + 2] = well.b;
        scaled[index + 3] = well.a;
    }
    std::vector<uint8_t>& kept =
        building.library->scaled_badge_pixels.emplace_back(std::move(scaled));
    oa::ui::frontend_renderer::RgbaPicture fitted;
    fitted.width = columns;
    fitted.height = rows;
    fitted.pixels = kept;
    return fitted;
}

/// Returns a map line's minimap when its pixels are there.
///
/// @param line the map
/// @return the minimap; null without one
const MapPicture* map_pixels(const MapLine& line) noexcept {
    if (!line.preview || line.preview->rgba.empty() || line.preview->width == 0 ||
        line.preview->height == 0)
        return nullptr;
    return &*line.preview;
}

/// Returns the installed pack an entry is, by its key and registry, or by
/// its name when the pack names no key.
///
/// @param library the model
/// @param entry the entry
/// @return the pack; null when the entry is not an installed map pack
const InstalledMaps* installed_pack(const Library& library, const Entry& entry) {
    if (entry.id.kind != Kind::map_pack)
        return nullptr;
    const InstalledMaps* by_name = nullptr;
    for (const InstalledMaps& pack : library.inputs.map_packs) {
        if (!pack.key.empty() && pack.key == entry.id.key && pack.registry == entry.id.registry)
            return &pack;
        if (pack.key.empty() && pack.name == entry_name(entry))
            by_name = &pack;
    }
    return by_name;
}

/// Returns the installed map that is the same as a listed one: the stem
/// when both name one, else the title.
///
/// @param pack the installed pack
/// @param listed the catalogue's map
/// @return the installed map; null when none matches
const MapLine* installed_map(const InstalledMaps& pack, const MapLine& listed) {
    if (!listed.stem.empty()) {
        for (const MapLine& map : pack.maps)
            if (map.stem == listed.stem)
                return &map;
    }
    for (const MapLine& map : pack.maps)
        if (map.name == listed.name)
            return &map;
    return nullptr;
}

/// One map as the details and a row show it. The words point into the
/// Library's inputs until the next refresh.
struct ShownMap {
    std::string_view name{};
    int32_t players{};
    std::string_view size{};
    std::string_view description{};
    const MapPicture* preview{}; ///< null while it has not been decoded
};

/// Returns the maps an entry shows. A listing's maps come first, and an
/// installed map's minimap and description fill in where the listing has
/// none. An installed minimap wins when both have pixels. With no listing
/// the installed pack's maps are the ones shown.
///
/// @param library the model
/// @param entry the entry
/// @return the maps; empty when the entry is not a map pack
std::vector<ShownMap> maps_shown(const Library& library, const Entry& entry) {
    std::vector<ShownMap> result;
    if (entry.id.kind != Kind::map_pack)
        return result;
    const InstalledMaps* pack = installed_pack(library, entry);
    if (entry.listing != nullptr && !entry.listing->maps.empty()) {
        for (const MapLine& listed : entry.listing->maps) {
            const MapLine* installed = pack != nullptr ? installed_map(*pack, listed) : nullptr;
            ShownMap map;
            map.name = listed.name;
            map.players = listed.players;
            map.size = listed.size.empty() && installed != nullptr
                           ? std::string_view{installed->size}
                           : std::string_view{listed.size};
            map.description = listed.description.empty() && installed != nullptr
                                  ? std::string_view{installed->description}
                                  : std::string_view{listed.description};
            const MapPicture* from_installed =
                installed != nullptr ? map_pixels(*installed) : nullptr;
            const MapPicture* from_listed = map_pixels(listed);
            map.preview = from_installed != nullptr ? from_installed : from_listed;
            result.push_back(map);
        }
        return result;
    }
    if (pack == nullptr)
        return result;
    for (const MapLine& map_line : pack->maps) {
        ShownMap map;
        map.name = map_line.name;
        map.players = map_line.players;
        map.size = map_line.size;
        map.description = map_line.description;
        map.preview = map_pixels(map_line);
        result.push_back(map);
    }
    return result;
}

/// What a map pack's row shows in place of its badge.
struct RowPreview {
    const MapPicture* picture{}; ///< the minimap to fit; null to keep the badge
    bool placeholder{};          ///< a dashed square: the minimap is not here yet
};

/// Returns the minimap a map pack's row shows.
///
/// One map always shows its minimap, or a placeholder while it is not
/// decoded. Several maps on the Maps tab show the pack's badge when it has
/// a picture, and otherwise the first map's minimap. Everywhere else a pack
/// of several keeps its badge or its letters.
///
/// @param library the model
/// @param entry the entry
/// @return what the row shows; both empty keeps the badge the row already has
RowPreview row_preview(const Library& library, const Entry& entry) {
    RowPreview choice;
    if (entry.id.kind != Kind::map_pack)
        return choice;
    const std::vector<ShownMap> maps = maps_shown(library, entry);
    if (maps.size() == 1) {
        choice.picture = maps.front().preview;
        choice.placeholder = choice.picture == nullptr;
        return choice;
    }
    if (maps.size() > 1 && library.tab == Tab::maps &&
        !oa::ui::frontend_renderer::picture_drawable(badge_picture(entry))) {
        choice.picture = maps.front().preview;
        choice.placeholder = choice.picture == nullptr;
    }
    return choice;
}

/// The side of one map's large preview, and of one cell in a grid of several.
constexpr int32_t map_preview_large = 160;
constexpr int32_t map_preview_cell = 72;
/// How many lines of a map's description a grid cell keeps.
constexpr std::size_t map_preview_lines = 3;

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
    // A From link names the row to put at the top of the list. The write is
    // through the state's mutable scroll: the layout is handed a const state,
    // and a temporary state discards it.
    if (building.state->reveal_row >= 0 && !library.visible.empty()) {
        const int32_t last = static_cast<int32_t>(library.visible.size()) - 1;
        const int32_t place = std::clamp(building.state->reveal_row, int32_t{0}, last);
        const int64_t raw = static_cast<int64_t>(place) * static_cast<int64_t>(height);
        const int64_t furthest = static_cast<int64_t>(std::max(limit, int32_t{0}));
        building.state->list_scroll = static_cast<int32_t>(std::clamp(raw, int64_t{0}, furthest));
        building.state->reveal_row = -1;
    }
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
        const RowPreview preview = row_preview(library, entry);
        if (preview.picture != nullptr || preview.placeholder) {
            look.badge = {};
            look.badge_text.clear();
            if (preview.picture != nullptr) {
                const oa::ui::frontend_renderer::RgbaPicture fitted = fitted_well_picture(
                    building, picture_of_map(*preview.picture), row_badge_square(rect)
                );
                if (oa::ui::frontend_renderer::picture_drawable(fitted))
                    look.badge = fitted;
            }
        } else if (
            base_badge(library, entry.badge) &&
            oa::ui::frontend_renderer::picture_drawable(look.badge)
        ) {
            look.badge = fitted_base_picture(building, look.badge, row_badge_square(rect));
        }
        if (look.chip && (entry.state == State::get || entry.state == State::update)) {
            // The download arrow and the up-arrow are GET and UPDATE: a
            // control of their own, tried before the row, named after it.
            const kit::ControlId icon_id =
                controls::first_row_icon + static_cast<kit::ControlId>(place);
            const Action action = entry.state == State::get ? Action::get : Action::update;
            kit::Control icon;
            icon.id = icon_id;
            icon.rect = kit::list_row_icon_rect(*building.fonts, rect, look);
            icon.clip = view;
            icon.enabled = controls::row_icon_action(library, entry).has_value();
            icon.focusable = false;
            icon.name =
                unique_name(building, row_name(entry) + "." + std::string(action_word(action)));
            icon.kind = kit::ControlKind::button;
            icon.group = controls::list_group;
            icon.text = look.chip->text;
            look.chip->hovered = icon.enabled && hovered(building, icon_id);
            building.rows.push_back(std::move(icon));
        }
        const std::size_t before = building.list.controls.size();
        kit::add_list_row(building.list, rect, look, id, unique_name(building, row_name(entry)));
        building.list.items.back().clip = view;
        move_controls(building.list, before, building.rows);
        kit::Control& row = building.rows.back();
        row.clip = view;
        row.group = controls::list_group;
        row.focusable = false;
        row.text = maker_reading(std::string(entry_name(entry)), entry_maker(entry));
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

/// Appends a badge as the details show one: its picture, or a name's
/// letters on the badge's colour, as a row's badge.
///
/// @param[in,out] building the layout
/// @param picture the picture; empty for the letters
/// @param name the name whose letters show without a picture
/// @param rect the badge's square
/// @param clip what drawing may touch
void add_badge_of(
    Building& building,
    const oa::ui::frontend_renderer::RgbaPicture& picture,
    std::string_view name,
    const kit::Rect& rect,
    const kit::Rect& clip
) {
    oa::ui::frontend_renderer::RgbaPicture drawn = picture;
    if (base_picture(*building.library, picture))
        drawn = fitted_base_picture(building, picture, rect);
    if (oa::ui::frontend_renderer::picture_drawable(drawn)) {
        add_item(building.list, kit::Role::picture, rect, clip).look = kit::PictureLook{drawn};
        add_item(building.list, kit::Role::outline, rect, clip).colour =
            kit::colour::control_border;
        return;
    }
    add_item(building.list, kit::Role::fill, rect, clip).colour = badge_face;
    const std::string letters = badge_letters(name);
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

/// Appends a square filled with the list's well colour and outlined, the
/// place a minimap takes while it has not been decoded.
///
/// @param[in,out] building the layout
/// @param rect the square
/// @param clip what drawing may touch
void add_well_square(Building& building, const kit::Rect& rect, const kit::Rect& clip) {
    add_item(building.list, kit::Role::fill, rect, clip).colour = kit::colour::well;
    add_item(building.list, kit::Role::outline, rect, clip).colour = kit::colour::control_border;
}

/// Appends a minimap fitted into a square, or the well while it is not decoded.
///
/// @param[in,out] building the layout
/// @param picture the minimap; null for the well
/// @param rect the square
/// @param clip what drawing may touch
void add_fitted_preview(
    Building& building, const MapPicture* picture, const kit::Rect& rect, const kit::Rect& clip
) {
    if (picture != nullptr) {
        const oa::ui::frontend_renderer::RgbaPicture fitted =
            fitted_well_picture(building, picture_of_map(*picture), rect);
        if (oa::ui::frontend_renderer::picture_drawable(fitted)) {
            add_item(building.list, kit::Role::picture, rect, clip).look = kit::PictureLook{fitted};
            add_item(building.list, kit::Role::outline, rect, clip).colour =
                kit::colour::control_border;
            return;
        }
    }
    add_well_square(building, rect, clip);
}

/// Appends an entry's badge as the details show it. A pack of one map wears
/// that map's minimap. A pack of several keeps its picture or its letters.
///
/// @param[in,out] building the layout
/// @param entry the entry
/// @param rect the badge's square
/// @param clip what drawing may touch
void add_badge(
    Building& building, const Entry& entry, const kit::Rect& rect, const kit::Rect& clip
) {
    // A pack of one map wears that map's minimap everywhere, including the
    // details. A pack of several keeps its badge or its letters.
    if (entry.id.kind == Kind::map_pack) {
        const std::vector<ShownMap> maps = maps_shown(*building.library, entry);
        if (maps.size() == 1) {
            add_fitted_preview(building, maps.front().preview, rect, clip);
            return;
        }
    }
    add_badge_of(building, badge_picture(entry), entry_name(entry), rect, clip);
}

/// Returns a map's players and size as one line: "4 players · 16x16".
///
/// @param building the layout
/// @param players how many players it is for
/// @param size its size, as written
/// @return the line; the size alone when the count is empty
std::string players_and_size(const Building& building, int32_t players, std::string_view size) {
    const std::string count = filled(
        building.library->text,
        players == 1 ? words::active_players_one : words::active_players_many,
        {{"count", std::to_string(players)}}
    );
    if (size.empty())
        return count;
    if (count.empty())
        return std::string(size);
    return filled(
        building.library->text,
        words::map_players_size,
        {{"players", count}, {"size", std::string(size)}}
    );
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
);

/// Appends a map pack's previews: one large picture for one map, a grid for
/// several, each with its name, players, size and description.
///
/// @param[in,out] building the layout
/// @param entry the entry
/// @param left the column
/// @param top the row the first picture starts on
/// @param width the columns they take
/// @param clip what drawing may touch
/// @return the row under the last map, or `top` when the entry is not a map pack
int32_t add_map_previews(
    Building& building,
    const Entry& entry,
    int32_t left,
    int32_t top,
    int32_t width,
    const kit::Rect& clip
) {
    if (entry.id.kind != Kind::map_pack || width <= 0)
        return top;
    const std::vector<ShownMap> maps = maps_shown(*building.library, entry);
    if (maps.empty())
        return top;
    const kit::Metrics& metrics = *building.metrics;
    const bool one = maps.size() == 1;
    const int32_t gap = metrics.card_gap;
    const int32_t columns = one || width < map_preview_cell * 2 + gap ? int32_t{1} : int32_t{2};
    const int32_t cell = one ? std::min(map_preview_large, width) : map_preview_cell;
    const int32_t across =
        columns == 1 ? cell : std::min(cell, std::max((width - gap) / 2, int32_t{0}));
    if (across <= 0)
        return top;
    int32_t row_top = top;
    for (std::size_t index = 0; index < maps.size();) {
        const std::size_t row_count =
            std::min(static_cast<std::size_t>(columns), maps.size() - index);
        int32_t block_height = across;
        std::vector<std::vector<std::string>> descriptions(row_count);
        for (std::size_t place = 0; place < row_count; ++place) {
            const ShownMap& map = maps[index + place];
            int32_t lines = metrics.small_line;
            if (!map.size.empty() || map.players > 0)
                lines += metrics.small_line;
            if (!map.description.empty()) {
                descriptions[place] = wrapped(
                    building,
                    kit::FontRole::small,
                    map.description,
                    across,
                    one ? wide_summary_lines : map_preview_lines
                );
                lines += static_cast<int32_t>(descriptions[place].size()) * metrics.small_line;
            }
            block_height = std::max(block_height, across + metrics.hint_gap + lines);
        }
        for (std::size_t place = 0; place < row_count; ++place) {
            const ShownMap& map = maps[index + place];
            const int32_t column = left + static_cast<int32_t>(place) * (across + gap);
            add_fitted_preview(building, map.preview, {column, row_top, across, across}, clip);
            int32_t text_top = row_top + across + metrics.hint_gap;
            const std::string title = cut(building, kit::FontRole::small, map.name, across);
            if (!title.empty() && width_of(building, kit::FontRole::small, title) <= across) {
                add_text(
                    building,
                    kit::FontRole::small,
                    title,
                    {column, text_top, across, metrics.small_line},
                    kit::Align::left,
                    kit::colour::text,
                    clip
                );
            }
            text_top += metrics.small_line;
            if (!map.size.empty() || map.players > 0) {
                const std::string meta = players_and_size(building, map.players, map.size);
                const std::string meta_drawn = cut(building, kit::FontRole::small, meta, across);
                if (!meta_drawn.empty() &&
                    width_of(building, kit::FontRole::small, meta_drawn) <= across)
                    add_text(
                        building,
                        kit::FontRole::small,
                        meta_drawn,
                        {column, text_top, across, metrics.small_line},
                        kit::Align::left,
                        kit::colour::hint,
                        clip
                    );
                text_top += metrics.small_line;
            }
            if (!descriptions[place].empty())
                add_lines(
                    building,
                    descriptions[place],
                    column,
                    text_top,
                    across,
                    kit::colour::list_text,
                    clip
                );
        }
        index += row_count;
        row_top += block_height;
        if (index < maps.size())
            row_top += gap;
    }
    return row_top + metrics.paragraph_gap;
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

/// Appends the details' note (details_note), when the model has one, under a row.
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
    const std::string note = details_note(*building.library);
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
    const int32_t chip_height = chip.icon ? metrics.state_icon_side : metrics.chip_height;
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
        first + (metrics.regular_line - chip_height) / 2,
        chip_width,
        chip_height,
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

    // The listing's summary, or a mod of the player's own's description.
    if (const std::string_view about = entry_summary(entry); !about.empty()) {
        const std::vector<std::string> summary =
            wrapped(building, kit::FontRole::small, about, width, wide_summary_lines);
        top = add_lines(building, summary, left, top, width, kit::colour::list_text, view) +
              metrics.paragraph_gap;
    }
    top = add_map_previews(building, entry, left, top, width, view);
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
            // UNINSTALL is never drawn as the main button.
            index == 0 && action.action != Action::uninstall,
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

/// The side of an Active tile's badge. A list row's badge plus the card's
/// inset on either side, so the badge is larger than a row's and the size
/// is still taken from the kit's metrics.
///
/// @param metrics the class's metrics
/// @return the side, in points
int32_t active_badge_side(const kit::Metrics& metrics) {
    return metrics.list_row_badge + 2 * metrics.card_inset;
}

/// Appends a tile's face: the kit card's fill and its outline. Nothing
/// here takes a press.
///
/// @param[in,out] building the layout
/// @param rect the tile
/// @param clip what drawing may touch
void add_tile_face(Building& building, const kit::Rect& rect, const kit::Rect& clip) {
    add_item(building.list, kit::Role::fill, rect, clip).colour = kit::colour::list_selected;
    add_item(building.list, kit::Role::outline, rect, clip).colour = kit::colour::control_border;
}

/// One line of text on a tile.
struct TileLine {
    std::string text{};
    kit::FontRole font{kit::FontRole::small};
    kit::Colour ink{kit::colour::text};
};

/// Returns a tile line's height.
///
/// @param metrics the class's metrics
/// @param font the line's font
/// @return the height, in points
int32_t tile_line_height(const kit::Metrics& metrics, kit::FontRole font) {
    return font == kit::FontRole::regular ? metrics.regular_line : metrics.small_line;
}

/// Returns the height of a tile's lines, stacked.
///
/// @param metrics the class's metrics
/// @param lines the lines
/// @return the height, in points
int32_t tile_lines_height(const kit::Metrics& metrics, const std::vector<TileLine>& lines) {
    int32_t height = 0;
    for (const TileLine& line : lines)
        height += tile_line_height(metrics, line.font);
    return height;
}

/// Returns an identity tile's height. The badge and the lines start at the
/// top inset. A tile with UNLOAD MOD keeps the button under both, with the
/// button gap between and the card's inset below the button. One without a
/// button keeps that inset below the badge or the last line.
///
/// @param metrics the class's metrics
/// @param badge the badge's side
/// @param lines the lines beside the badge
/// @param unload the tile holds UNLOAD MOD
/// @return the height, in points
int32_t identity_tile_height(
    const kit::Metrics& metrics, int32_t badge, const std::vector<TileLine>& lines, bool unload
) {
    int32_t stack = std::max(badge, tile_lines_height(metrics, lines));
    if (unload)
        stack += metrics.button_gap + metrics.button_height;
    return stack + 2 * metrics.card_inset;
}

/// Appends a tile's lines. The first line keeps room at its right for a
/// chip; a line that cannot fit is left out, and its row is kept.
///
/// @param[in,out] building the layout
/// @param lines the lines
/// @param left their left column
/// @param top the first line's top row
/// @param width their width
/// @param first_room the first line's width
/// @param clip what drawing may touch
void place_tile_lines(
    Building& building,
    const std::vector<TileLine>& lines,
    int32_t left,
    int32_t top,
    int32_t width,
    int32_t first_room,
    const kit::Rect& clip
) {
    const kit::Metrics& metrics = *building.metrics;
    int32_t row = top;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const TileLine& line = lines[index];
        const int32_t room = index == 0 ? first_room : width;
        const int32_t height = tile_line_height(metrics, line.font);
        if (room > 0 && !line.text.empty()) {
            const std::string drawn = cut(building, line.font, line.text, room);
            if (width_of(building, line.font, drawn) <= room)
                add_text(
                    building,
                    line.font,
                    drawn,
                    {left, row, room, height},
                    kit::Align::left,
                    line.ink,
                    clip
                );
        }
        row += height;
    }
}

/// Appends a pill that takes no press, at the right of a line, when it fits.
///
/// @param[in,out] building the layout
/// @param line the line it sits on
/// @param words_shown the pill's words
/// @param state the pill's colours
/// @param clip what drawing may touch
/// @return the pill's width, or 0 when it does not fit
int32_t add_idle_pill(
    Building& building,
    const kit::Rect& line,
    const std::string& words_shown,
    kit::PillState state,
    const kit::Rect& clip
) {
    if (words_shown.empty() || line.width <= 0 || line.height <= 0)
        return 0;
    kit::PillLook look;
    look.text = words_shown;
    look.state = state;
    const int32_t width = kit::pill_width(*building.fonts, look);
    const int32_t height = kit::pill_height();
    if (width > line.width || height > line.height)
        return 0;
    const kit::Rect pill{
        line.x + line.width - width,
        line.y + (line.height - height) / 2,
        width,
        height,
    };
    kit::add_pill(building.list, pill, look, kit::no_control, {});
    building.list.items.back().clip = clip;
    return width;
}

/// Appends an identity tile: a badge and lines from the top inset, a pill
/// at the right of the title's line, and UNLOAD MOD at the bottom right
/// while a mod is played. The label is the tile above that button, so the
/// two do not meet, and the label takes no press.
///
/// @param[in,out] building the layout
/// @param tile the tile
/// @param badge the badge; null for the letters
/// @param letters the name the letters are taken from
/// @param lines the lines beside the badge
/// @param pill the pill's words; empty for none
/// @param pill_state the pill's colours
/// @param unload the tile holds UNLOAD MOD
/// @param id the label's number
/// @param name the label's name
/// @param read the label's text
/// @param clip what drawing may touch
void add_identity_tile(
    Building& building,
    const kit::Rect& tile,
    const Badge* badge,
    std::string_view letters,
    const std::vector<TileLine>& lines,
    const std::string& pill,
    kit::PillState pill_state,
    bool unload,
    kit::ControlId id,
    std::string name,
    std::string read,
    const kit::Rect& clip
) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
    const int32_t inset = metrics.card_inset;
    const int32_t side =
        std::min(active_badge_side(metrics), std::max(tile.width - 2 * inset, int32_t{0}));
    add_tile_face(building, tile, clip);
    const int32_t inner_left = tile.x + inset;
    const int32_t inner_top = tile.y + inset;
    if (side > 0)
        add_badge_of(
            building, picture_of(badge), letters, {inner_left, inner_top, side, side}, clip
        );
    const int32_t text_left = inner_left + side + metrics.list_row_gap;
    const int32_t text_width = std::max(tile.x + tile.width - inset - text_left, int32_t{0});
    const int32_t text_top = inner_top;
    const int32_t title_height =
        lines.empty() ? metrics.regular_line : tile_line_height(metrics, lines.front().font);
    const int32_t pill_width = add_idle_pill(
        building, {text_left, text_top, text_width, title_height}, pill, pill_state, clip
    );
    const int32_t first_room =
        pill_width > 0 ? std::max(text_width - pill_width - metrics.list_row_gap, int32_t{0})
                       : text_width;
    place_tile_lines(building, lines, text_left, text_top, text_width, first_room, clip);
    kit::Rect label = tile;
    if (unload) {
        const std::string caption(shown(library.text, words::action_unload_mod));
        const int32_t across =
            std::min(button_width(building, caption), std::max(tile.width - 2 * inset, int32_t{0}));
        const kit::Rect button{
            tile.x + tile.width - inset - across,
            tile.y + tile.height - inset - metrics.button_height,
            across,
            metrics.button_height,
        };
        if (across > 0 && button.y >= tile.y) {
            add_button(
                building,
                building.detail,
                button,
                caption,
                true,
                can_switch(library),
                controls::active_unload,
                named("active.unload"),
                clip,
                controls::details_group
            );
            label.height = std::max(button.y - tile.y, int32_t{0});
        }
    }
    add_label(
        building.detail,
        id,
        kit::intersect(label, clip),
        std::move(name),
        std::move(read),
        clip,
        controls::details_group
    );
}

/// Returns one map tile's height: the minimap or the lines beside it,
/// whichever is taller, with the card's padding above and below. A pack
/// of several maps keeps a From line; a pack of one does not.
///
/// @param metrics the class's metrics
/// @param side the minimap's side
/// @param described the map has a description to show
/// @param from_line the tile draws "From <pack>"
/// @return the height, in points
int32_t map_tile_height(const kit::Metrics& metrics, int32_t side, bool described, bool from_line) {
    int32_t lines = metrics.regular_line + metrics.small_line;
    if (described)
        lines += metrics.small_line;
    if (from_line)
        lines += metrics.small_line;
    return metrics.card_inset + std::max(side, lines) + metrics.card_inset;
}

/// What an identity tile shows. The layout paints it; the model chose the words.
struct IdentityTile {
    const Badge* badge{};
    std::string letters{};
    std::vector<TileLine> lines{};
    std::string pill{};
    kit::PillState pill_state{kit::PillState::neutral};
    bool unload{}; ///< UNLOAD MOD, on the mod tile while a mod is played
    kit::ControlId id{};
    std::string name{};
    std::string read{};
};

/// Returns the mod tile: the mod being played, or the base game.
///
/// @param[in,out] building the layout, which records the label's name
/// @param setup what the tab shows
/// @return the tile
IdentityTile mod_tile(Building& building, const ActiveSetup& setup) {
    IdentityTile tile;
    tile.badge = setup.mod_badge;
    tile.id = controls::active_mod;
    tile.name = unique_name(building, named("active.mod"));
    tile.read = setup.mod_line;
    if (setup.mod_name.empty()) {
        tile.letters = std::string(base_badge_name);
        if (!setup.base_title.empty())
            tile.lines.push_back(
                TileLine{setup.base_title, kit::FontRole::regular, kit::colour::text}
            );
        if (!setup.base_look.empty())
            tile.lines.push_back(
                TileLine{setup.base_look, kit::FontRole::small, kit::colour::hint}
            );
        if (!setup.mod_line.empty())
            tile.lines.push_back(TileLine{setup.mod_line, kit::FontRole::small, kit::colour::hint});
    } else {
        tile.letters = setup.mod_name;
        tile.lines.push_back(TileLine{setup.mod_name, kit::FontRole::regular, kit::colour::text});
        const std::string& version =
            setup.mod_release.empty() ? setup.mod_version : setup.mod_release;
        if (!version.empty())
            tile.lines.push_back(TileLine{version, kit::FontRole::small, kit::colour::hint});
        if (!setup.mod_author.empty())
            tile.lines.push_back(
                TileLine{setup.mod_author, kit::FontRole::small, kit::colour::list_text}
            );
        tile.pill = setup.mod_state;
        tile.pill_state = kit::PillState::playing;
        tile.unload = true;
    }
    return tile;
}

/// Returns the language tile.
///
/// @param[in,out] building the layout, which records the label's name
/// @param setup what the tab shows
/// @return the tile
IdentityTile language_tile(Building& building, const ActiveSetup& setup) {
    IdentityTile tile;
    tile.badge = setup.language_badge;
    tile.letters = setup.language_name;
    tile.id = controls::active_language;
    tile.name = unique_name(building, named("active.language"));
    tile.read = setup.language_line;
    tile.pill = setup.language_note;
    tile.pill_state = kit::PillState::neutral;
    if (!setup.language_name.empty())
        tile.lines.push_back(
            TileLine{setup.language_name, kit::FontRole::regular, kit::colour::text}
        );
    if (!setup.language_english.empty())
        tile.lines.push_back(
            TileLine{setup.language_english, kit::FontRole::small, kit::colour::hint}
        );
    if (!setup.language_pack.empty())
        tile.lines.push_back(
            TileLine{setup.language_pack, kit::FontRole::small, kit::colour::list_text}
        );
    if (!setup.language_version.empty())
        tile.lines.push_back(
            TileLine{setup.language_version, kit::FontRole::small, kit::colour::hint}
        );
    if (!setup.language_coverage.empty())
        tile.lines.push_back(
            TileLine{setup.language_coverage, kit::FontRole::small, kit::colour::list_text}
        );
    return tile;
}

/// Returns an identity tile's height at this class.
///
/// @param metrics the class's metrics
/// @param tile the tile
/// @return the height, in points
int32_t identity_height(const kit::Metrics& metrics, const IdentityTile& tile) {
    return identity_tile_height(metrics, active_badge_side(metrics), tile.lines, tile.unload);
}

/// Appends an identity tile.
///
/// @param[in,out] building the layout
/// @param tile the tile
/// @param rect where it sits
/// @param clip what drawing may touch
void place_identity(
    Building& building, const IdentityTile& tile, const kit::Rect& rect, const kit::Rect& clip
) {
    add_identity_tile(
        building,
        rect,
        tile.badge,
        tile.letters,
        tile.lines,
        tile.pill,
        tile.pill_state,
        tile.unload,
        tile.id,
        tile.name,
        tile.read,
        clip
    );
}

/// Appends one installed map's tile: its minimap, title, players and size,
/// its description cut to one line, and the link to its pack. A pack of
/// several maps keeps that link on a From line. A pack of one map leaves
/// the line out and makes the title the link, in the same accent.
///
/// @param[in,out] building the layout
/// @param map the map
/// @param tile the tile
/// @param number the map's place among the tiles, from 1
/// @param clip what drawing may touch
void add_map_tile(
    Building& building,
    const ActiveMap& map,
    const kit::Rect& tile,
    std::size_t number,
    const kit::Rect& clip
) {
    const kit::Metrics& metrics = *building.metrics;
    const int32_t inset = metrics.card_inset;
    const int32_t side =
        std::min(active_badge_side(metrics), std::max(tile.width - 2 * inset, int32_t{0}));
    add_tile_face(building, tile, clip);
    const int32_t inner_left = tile.x + inset;
    const int32_t inner_top = tile.y + inset;
    if (side > 0)
        add_fitted_preview(building, map.preview, {inner_left, inner_top, side, side}, clip);
    const int32_t text_left = inner_left + side + metrics.list_row_gap;
    const int32_t text_width = std::max(tile.x + tile.width - inset - text_left, int32_t{0});
    if (text_width <= 0)
        return;
    int32_t row = inner_top;
    const bool title_links = map.from.empty();
    const std::string title = cut(building, kit::FontRole::regular, map.name, text_width);
    const bool title_fits =
        !title.empty() && width_of(building, kit::FontRole::regular, title) <= text_width;
    const kit::ControlId link_id =
        controls::first_active_from + static_cast<kit::ControlId>(number - 1);
    // The title of a one-map pack is the pack's link: the accent, lighter
    // while the pointer is over it, with the focus ring when it is focused.
    const bool title_is_link = title_links && number <= controls::most_active_maps && title_fits;
    kit::Colour title_ink = kit::colour::text;
    if (title_is_link) {
        const bool over =
            hovered(building, link_id) || building.state->interaction.pressed == link_id;
        title_ink = over ? kit::colour::accent_light : kit::colour::accent;
    }
    if (title_fits)
        add_text(
            building,
            kit::FontRole::regular,
            title,
            {text_left, row, text_width, metrics.regular_line},
            kit::Align::left,
            title_ink,
            clip
        );
    if (title_is_link) {
        const int32_t link_across =
            std::min(width_of(building, kit::FontRole::regular, title), text_width);
        const kit::Rect link_rect =
            kit::intersect({text_left, row, link_across, metrics.regular_line}, clip);
        if (link_rect.width > 0 && link_rect.height > 0) {
            kit::LinkLook link;
            link.caption = title;
            link.hovered =
                hovered(building, link_id) || building.state->interaction.pressed == link_id;
            const std::size_t items_before = building.list.items.size();
            const std::size_t controls_before = building.list.controls.size();
            kit::add_link(
                building.list,
                link_rect,
                link,
                link_id,
                unique_name(building, named("active.from." + std::to_string(number)))
            );
            // The kit draws a link in the small font. The title stays in the
            // regular font, already drawn above, so the link item is dropped
            // and only its control remains.
            building.list.items.resize(items_before);
            move_controls(building.list, controls_before, building.detail);
            kit::Control& control = building.detail.back();
            control.clip = clip;
            control.group = controls::details_group;
            control.focusable = true;
            control.enabled = true;
            tab_stop(building, control);
        }
    }
    if (number <= controls::most_active_maps) {
        // The composed line stays a label. When the title is the link, the
        // label sits on the players-and-size line so the two do not meet.
        const int32_t label_top = title_is_link ? row + metrics.regular_line : row;
        const int32_t label_height = title_is_link ? metrics.small_line : metrics.regular_line;
        add_label(
            building.detail,
            controls::first_active_map + static_cast<kit::ControlId>(number - 1),
            kit::intersect({text_left, label_top, text_width, label_height}, clip),
            unique_name(building, named("active.map." + std::to_string(number))),
            map.line,
            clip,
            controls::details_group
        );
    }
    row += metrics.regular_line;
    const std::string meta = filled(
        building.library->text,
        words::map_players_size,
        {{"players", map.players}, {"size", map.size}}
    );
    const std::string meta_drawn = cut(building, kit::FontRole::small, meta, text_width);
    if (!meta_drawn.empty() && width_of(building, kit::FontRole::small, meta_drawn) <= text_width)
        add_text(
            building,
            kit::FontRole::small,
            meta_drawn,
            {text_left, row, text_width, metrics.small_line},
            kit::Align::left,
            kit::colour::hint,
            clip
        );
    row += metrics.small_line;
    if (!map.description.empty()) {
        const std::string described =
            cut(building, kit::FontRole::small, map.description, text_width);
        if (!described.empty() && width_of(building, kit::FontRole::small, described) <= text_width)
            add_text(
                building,
                kit::FontRole::small,
                described,
                {text_left, row, text_width, metrics.small_line},
                kit::Align::left,
                kit::colour::list_text,
                clip
            );
        row += metrics.small_line;
    }
    if (number > controls::most_active_maps || map.from.empty())
        return;
    kit::LinkLook link;
    link.caption = cut(building, kit::FontRole::small, map.from, text_width);
    if (link.caption.empty() || width_of(building, kit::FontRole::small, link.caption) > text_width)
        return;
    const kit::ControlId id = controls::first_active_from + static_cast<kit::ControlId>(number - 1);
    link.hovered = hovered(building, id) || building.state->interaction.pressed == id;
    const int32_t link_width = std::min(kit::link_width(*building.fonts, link), text_width);
    const kit::Rect link_rect =
        kit::intersect({text_left, row, link_width, metrics.small_line}, clip);
    // A link scrolled out of the area is left out: a zero-wide caption would
    // not fit its box, and an off-screen stop would fail the arrow check.
    if (link_rect.width <= 0 || link_rect.height <= 0)
        return;
    const std::size_t before = building.list.controls.size();
    kit::add_link(
        building.list,
        link_rect,
        link,
        id,
        unique_name(building, named("active.from." + std::to_string(number)))
    );
    building.list.items.back().clip = clip;
    move_controls(building.list, before, building.detail);
    kit::Control& control = building.detail.back();
    control.clip = clip;
    control.group = controls::details_group;
    control.focusable = true;
    control.enabled = true;
    tab_stop(building, control);
}

int32_t add_active_heading(
    Building& building,
    std::string_view words_shown,
    int32_t left,
    int32_t top,
    int32_t width,
    const kit::Rect& clip
);

/// Appends the map tiles: one column, or two at Large when more than one
/// map is installed. A window with none shows that in one tile.
///
/// @param[in,out] building the layout
/// @param setup what the tab shows
/// @param left the section's left column
/// @param top the section's top row
/// @param width the section's width
/// @param clip what drawing may touch
/// @return the row under the section
int32_t add_map_tiles(
    Building& building,
    const ActiveSetup& setup,
    int32_t left,
    int32_t top,
    int32_t width,
    const kit::Rect& clip
) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
    top = add_active_heading(
        building, shown(library.text, words::active_heading_maps), left, top, width, clip
    );
    if (setup.maps.empty()) {
        const int32_t inset = metrics.card_inset;
        const int32_t inner = std::max(width - 2 * inset, int32_t{0});
        const int32_t height = metrics.small_line + 2 * inset;
        const kit::Rect tile{left, top, width, height};
        add_tile_face(building, tile, clip);
        const std::string words_shown = std::string(shown(library.text, words::active_no_maps));
        if (inner > 0) {
            const std::string drawn = cut(building, kit::FontRole::small, words_shown, inner);
            if (width_of(building, kit::FontRole::small, drawn) <= inner)
                add_text(
                    building,
                    kit::FontRole::small,
                    drawn,
                    {left + inset, top + inset, inner, metrics.small_line},
                    kit::Align::left,
                    kit::colour::hint,
                    clip
                );
        }
        return top + height;
    }
    const int32_t columns =
        building.size_class == kit::SizeClass::large && setup.maps.size() > 1 ? 2 : 1;
    const int32_t gap = metrics.card_gap;
    const int32_t left_width = columns == 1 ? width : std::max((width - gap) / 2, int32_t{0});
    const auto tile_width = [&](std::size_t place) {
        if (columns == 1 || place % 2 == 0)
            return left_width;
        return std::max(width - left_width - gap, int32_t{0});
    };
    const auto tile_left = [&](std::size_t place) {
        if (columns == 1 || place % 2 == 0)
            return left;
        return left + left_width + gap;
    };
    std::size_t number = 1;
    std::size_t index = 0;
    while (index < setup.maps.size()) {
        const std::size_t row_count =
            std::min(static_cast<std::size_t>(columns), setup.maps.size() - index);
        int32_t tiles_height = 0;
        for (std::size_t place = 0; place < row_count; ++place) {
            const ActiveMap& map = setup.maps[index + place];
            const int32_t across = tile_width(index + place);
            const int32_t side = std::min(
                active_badge_side(metrics), std::max(across - 2 * metrics.card_inset, int32_t{0})
            );
            tiles_height = std::max(
                tiles_height,
                map_tile_height(metrics, side, !map.description.empty(), !map.from.empty())
            );
        }
        for (std::size_t place = 0; place < row_count; ++place) {
            const ActiveMap& map = setup.maps[index + place];
            const int32_t across = tile_width(index + place);
            add_map_tile(
                building, map, {tile_left(index + place), top, across, tiles_height}, number, clip
            );
            ++number;
        }
        index += row_count;
        top += tiles_height;
        if (index < setup.maps.size())
            top += gap;
    }
    return top;
}

/// Appends a heading of the Active tab, and returns the row under it.
///
/// @param[in,out] building the layout
/// @param words_shown the heading, looked up
/// @param left its left column
/// @param top its top row
/// @param width its width
/// @param clip what drawing may touch
/// @return the row under it and the gap after it
int32_t add_active_heading(
    Building& building,
    std::string_view words_shown,
    int32_t left,
    int32_t top,
    int32_t width,
    const kit::Rect& clip
) {
    const kit::Metrics& metrics = *building.metrics;
    kit::Item& heading =
        add_item(building.list, kit::Role::heading, {left, top, width, metrics.small_line}, clip);
    heading.text = std::string(words_shown);
    heading.look = kit::HeadingLook{std::string(words_shown)};
    return top + metrics.small_line + metrics.hint_gap;
}

/// Appends the Active tab's tiles, scrolled up by an offset. Under a
/// heading each: the mod being played, or the base game, and the language
/// in use, side by side from Regular on; then one tile per installed map,
/// in two columns at Large when more than one map is installed. The mod's
/// and the language's labels are the part of the tile that shows, the
/// mod's ending above UNLOAD MOD, and each map's is the part of its line
/// that shows, empty while that part is scrolled out of the area. The
/// labels take no press. UNLOAD MOD does, while a mod is played, and so
/// does a map's link to its pack: the From line while that line shows, or
/// the title when the pack holds that map alone.
///
/// @param[in,out] building the layout
/// @param setup what the tab shows
/// @param view the area, which clips them
/// @param width the columns they take from the area's left
/// @param offset how far they are scrolled up, in points
/// @return the parts' height, in points
int32_t add_active_content(
    Building& building,
    const ActiveSetup& setup,
    const kit::Rect& view,
    int32_t width,
    int32_t offset
) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
    const int32_t first = view.y - offset;
    const int32_t left = view.x;
    const std::size_t first_item = building.list.items.size();
    const IdentityTile mod = mod_tile(building, setup);
    const IdentityTile language = language_tile(building, setup);
    const int32_t mod_height = identity_height(metrics, mod);
    const int32_t language_height = identity_height(metrics, language);
    int32_t top = first;
    if (building.size_class == kit::SizeClass::compact) {
        top = add_active_heading(
            building, shown(library.text, words::active_heading_mod), left, top, width, view
        );
        place_identity(building, mod, {left, top, width, mod_height}, view);
        top += mod_height + metrics.paragraph_gap;
        top = add_active_heading(
            building, shown(library.text, words::active_heading_language), left, top, width, view
        );
        place_identity(building, language, {left, top, width, language_height}, view);
        top += language_height + metrics.paragraph_gap;
    } else {
        const int32_t gap = metrics.card_gap;
        const int32_t left_width = std::max((width - gap) / 2, int32_t{0});
        const int32_t right = left + left_width + gap;
        const int32_t right_width = std::max(left + width - right, int32_t{0});
        const int32_t tiles = add_active_heading(
            building, shown(library.text, words::active_heading_mod), left, top, left_width, view
        );
        static_cast<void>(add_active_heading(
            building,
            shown(library.text, words::active_heading_language),
            right,
            top,
            right_width,
            view
        ));
        const int32_t tile_height = std::max(mod_height, language_height);
        place_identity(building, mod, {left, tiles, left_width, tile_height}, view);
        place_identity(building, language, {right, tiles, right_width, tile_height}, view);
        top = tiles + tile_height + metrics.paragraph_gap;
    }
    static_cast<void>(add_map_tiles(building, setup, left, top, width, view));
    const std::optional<controls::Extent> extent =
        controls::clipped_extent(building.list, view, first_item);
    return extent ? extent->bottom - first : 0;
}

/// Appends the Active tab: its parts in an area that scrolls when they are
/// taller than it, with a column for the scroll bar at its right.
///
/// @param[in,out] building the layout
/// @param area the area
void add_active(Building& building, const kit::Rect& area) {
    const kit::Metrics& metrics = *building.metrics;
    const ActiveSetup setup = active_setup(*building.library);
    // Laid out once at the area's width to learn its height, then again
    // narrower when that is taller than the area.
    Building trial = building;
    trial.list = {};
    trial.detail.clear();
    trial.order.clear();
    const int32_t whole_height = add_active_content(trial, setup, area, area.width, 0);
    const int32_t well_width = metrics.scroll_thumb_width + 2;
    const bool scrolls = whole_height > area.height;
    const int32_t width = scrolls ? area.width - well_width - metrics.button_gap : area.width;
    int32_t height = whole_height;
    if (scrolls) {
        trial = building;
        trial.list = {};
        trial.detail.clear();
        trial.order.clear();
        height = add_active_content(trial, setup, area, width, 0);
    }
    const int32_t limit = kit::ScrollArea::limit_for(height, area.height);
    const int32_t offset = std::clamp(building.state->details_scroll, int32_t{0}, limit);
    add_active_content(building, setup, area, width, offset);
    if (limit > 0) {
        kit::ScrollArea scroll;
        scroll.view = area;
        scroll.well = {area.x + area.width - well_width, area.y, well_width, area.height};
        scroll.content_height = height;
        scroll.limit = limit;
        scroll.offset = offset;
        add_item(building.list, kit::Role::scroll_bar, scroll.well, {}).look =
            kit::ScrollBarLook{scroll.thumb(metrics.least_thumb_height), false};
    }
}

/// Appends a registry's Off/On switch, in the details' scroll.
///
/// @param[in,out] building the layout
/// @param rect the switch
/// @param on the switch is On
/// @param id its control's number
/// @param name its control's name
/// @param clip the part of it that shows
void add_registry_switch(
    Building& building,
    const kit::Rect& rect,
    bool on,
    kit::ControlId id,
    std::string name,
    const kit::Rect& clip
) {
    const Library& library = *building.library;
    kit::SwitchLook look;
    look.on = on;
    look.hovered = hovered(building, id);
    look.off_caption = std::string(shown(library.text, words::registry_off));
    look.on_caption = std::string(shown(library.text, words::registry_on));
    const std::size_t before = building.list.controls.size();
    kit::add_switch(building.list, rect, look, id, unique_name(building, std::move(name)));
    building.list.items.back().clip = clip;
    move_controls(building.list, before, building.detail);
    building.detail.back().clip = clip;
    building.detail.back().group = controls::details_group;
    tab_stop(building, building.detail.back());
}

/// Appends the key icon: the kit's key glyph in the icon square, so the
/// pointer and the keys can rest on it. Its text is the fingerprints, one a
/// line. Pressing it asks for nothing.
///
/// @param[in,out] building the layout
/// @param rect the square, state_icon_side on each side
/// @param id its control's number
/// @param name its control's name
/// @param fingerprints the fingerprints, one a line
/// @param clip the part of it that shows
void add_key_button(
    Building& building,
    const kit::Rect& rect,
    kit::ControlId id,
    std::string name,
    std::string fingerprints,
    const kit::Rect& clip
) {
    kit::ChipLook look;
    look.text = std::move(fingerprints);
    look.state = kit::ChipState::get;
    look.hovered = hovered(building, id);
    look.icon = kit::StateIcon::key;
    const std::size_t before = building.list.controls.size();
    kit::add_chip(building.list, rect, look, id, unique_name(building, std::move(name)));
    building.list.items.back().clip = clip;
    move_controls(building.list, before, building.detail);
    building.detail.back().clip = clip;
    building.detail.back().group = controls::details_group;
    tab_stop(building, building.detail.back());
}

/// Appends the Registries tab's parts, scrolled up by an offset: one card
/// for each registry (its avatar, heading, address, refresh time, description,
/// subtitle, key icon, REFRESH and switch; REMOVE and ADD MIRROR… for one the
/// player added, and a Built in pill for one that ships with the game), then
/// the refusal in amber and the note under the last card. Each control is in
/// the details' scroll area. The heading is the registry's title, or its
/// short name when it has no title.
///
/// @param[in,out] building the layout
/// @param view the area, which clips them
/// @param width the columns they take from the area's left
/// @param offset how far they are scrolled up, in points
/// @return the parts' height, in points
int32_t
add_registries_content(Building& building, const kit::Rect& view, int32_t width, int32_t offset) {
    const Library& library = *building.library;
    const kit::Metrics& metrics = *building.metrics;
    const int32_t first = view.y - offset;
    const int32_t left = view.x;
    const std::size_t first_item = building.list.items.size();
    int32_t cursor = first;
    const std::vector<Registry>& registries = library.inputs.registries;
    for (std::size_t index = 0; index < registries.size(); ++index) {
        const Registry& registry = registries[index];
        const std::string word = registry_word(registry.id);
        if (index > 0)
            cursor += metrics.card_gap;
        const int32_t card_top = cursor;
        const int32_t inset = metrics.card_inset;
        const int32_t inner_left = left + inset;
        const int32_t inner_right = left + width - inset;
        const int32_t band_top = card_top + inset;
        const int32_t side = metrics.list_row_badge;
        const int32_t title_band = std::max(metrics.regular_line, metrics.button_height);
        // The right cluster, from the switch inward: REFRESH, the refresh
        // mark while this registry is being asked or its check is showing,
        // and the key icon. The key, REFRESH and the switch share the band's
        // top, so Tab reads them left to right and then the buttons below.
        // A wrapped heading grows under that band.
        const uint16_t check_ms =
            registry.refreshing ? uint16_t{0} : registry_check_ms(*building.state, registry.id);
        const bool show_mark = registry.refreshing || check_ms > 0;
        int32_t cluster = inner_right;
        cluster -= metrics.switch_width;
        const kit::Rect toggle{
            cluster,
            band_top,
            metrics.switch_width,
            metrics.regular_line,
        };
        cluster -= metrics.button_gap;
        const std::string_view refresh_caption = shown(library.text, words::registry_refresh);
        const int32_t refresh_width = button_width(building, refresh_caption);
        cluster -= refresh_width;
        const kit::Rect refresh_rect{cluster, band_top, refresh_width, metrics.button_height};
        cluster -= metrics.button_gap;
        kit::Rect turning_rect{};
        if (show_mark) {
            cluster -= metrics.button_height;
            turning_rect = {cluster, band_top, metrics.button_height, metrics.button_height};
            cluster -= metrics.button_gap;
        }
        cluster -= metrics.state_icon_side;
        const kit::Rect key_rect{
            cluster, band_top, metrics.state_icon_side, metrics.state_icon_side
        };
        const int32_t text_left = inner_left + side + metrics.list_row_gap;
        const int32_t title_right = cluster - metrics.label_gap;
        const int32_t title_width = title_right - text_left;
        const std::string_view heading = registry.title.empty() ? std::string_view{registry.name}
                                                                : std::string_view{registry.title};
        std::vector<std::string> heading_lines;
        if (title_width > 0)
            heading_lines = wrapped(
                building, kit::FontRole::regular, heading, std::max(title_width, int32_t{1}), 0
            );
        const int32_t heading_height =
            static_cast<int32_t>(std::max(heading_lines.size(), std::size_t{1})) *
            metrics.regular_line;
        const int32_t header_height = std::max(title_band, std::max(heading_height, side));
        const int32_t lines_width = std::max(inner_right - text_left, int32_t{0});
        std::vector<std::string> description_lines;
        if (!registry.description.empty() && lines_width > 0)
            description_lines = wrapped(
                building,
                kit::FontRole::small,
                registry.description,
                std::max(lines_width, int32_t{1}),
                0
            );

        const std::size_t card_at = building.list.items.size();
        const kit::Rect avatar{inner_left, band_top, side, side};
        add_item(building.list, kit::Role::fill, avatar, view).colour =
            registry.built_in ? kit::colour::accent : kit::colour::switch_idle;
        add_text(
            building,
            kit::FontRole::small,
            cut(building, kit::FontRole::small, registry_initials(registry.name), side),
            avatar,
            kit::Align::centre,
            kit::colour::on_accent,
            view
        );
        int32_t heading_top = band_top;
        for (const std::string& heading_line : heading_lines) {
            add_text(
                building,
                kit::FontRole::regular,
                heading_line,
                {text_left, heading_top, std::max(title_width, int32_t{0}), metrics.regular_line},
                kit::Align::left,
                kit::colour::text,
                view
            );
            heading_top += metrics.regular_line;
        }
        std::string fingerprints;
        for (const SigningKey& signing_key : registry.keys) {
            if (!fingerprints.empty())
                fingerprints.push_back('\n');
            fingerprints += signing_key.fingerprint;
        }
        add_key_button(
            building,
            key_rect,
            controls::first_registry_key + static_cast<kit::ControlId>(index),
            named("registry." + word + ".key"),
            std::move(fingerprints),
            view
        );
        // A built-in registry's mark sits under the key, inside the header
        // band (the avatar is two icons tall). It is not a control.
        if (const std::optional<kit::MakerMark> mark = shown_maker(
                registry.built_in ? package_maker(registry.author) : PackageMaker::none
            )) {
            kit::ChipLook mark_look;
            mark_look.icon = *mark == kit::MakerMark::open_annihilation ? kit::StateIcon::oa
                                                                        : kit::StateIcon::bone;
            const kit::Rect mark_rect{
                key_rect.x,
                key_rect.y + metrics.state_icon_side,
                metrics.state_icon_side,
                metrics.state_icon_side,
            };
            kit::add_chip(building.list, mark_rect, mark_look, kit::no_control, {});
            building.list.items.back().clip = view;
        }
        if (show_mark) {
            kit::ButtonLook spinning;
            spinning.style = kit::ButtonStyle::quiet;
            spinning.enabled = false;
            spinning.mark = kit::ButtonMark::refresh;
            spinning.turn = building.state->refresh_turn;
            spinning.turning = registry.refreshing;
            spinning.check_ms = check_ms;
            kit::Item& spun = add_item(building.list, kit::Role::button, turning_rect, view);
            spun.look = spinning;
        }
        add_button(
            building,
            building.detail,
            refresh_rect,
            refresh_caption,
            false,
            registry.on && !registry.refreshing,
            controls::first_registry_refresh + static_cast<kit::ControlId>(index),
            named("registry." + word + ".refresh"),
            view,
            controls::details_group
        );
        add_registry_switch(
            building,
            toggle,
            registry.on,
            controls::first_registry + static_cast<kit::ControlId>(index),
            named("registry." + word),
            view
        );

        int32_t facts_top = band_top + header_height + metrics.hint_gap;
        int32_t bottom = band_top + header_height;
        const auto place_line = [&](std::string full, kit::ControlId id, std::string control_name) {
            if (full.empty() || lines_width <= 0)
                return;
            const kit::Rect box{text_left, facts_top, lines_width, metrics.small_line};
            add_text(
                building,
                kit::FontRole::small,
                cut(building, kit::FontRole::small, full, lines_width),
                box,
                kit::Align::left,
                kit::colour::hint,
                view
            );
            add_label(
                building.detail,
                id,
                box,
                unique_name(building, std::move(control_name)),
                full,
                view,
                controls::details_group
            );
            bottom = facts_top + metrics.small_line;
            facts_top = bottom + metrics.hint_gap;
        };
        place_line(
            registry.address,
            controls::first_registry_address + static_cast<kit::ControlId>(index),
            named("registry." + word + ".address")
        );
        place_line(
            registry_refresh_line(registry, library.text),
            controls::first_registry_refreshed + static_cast<kit::ControlId>(index),
            named("registry." + word + ".refreshed")
        );
        if (!description_lines.empty()) {
            for (const std::string& description_line : description_lines) {
                add_text(
                    building,
                    kit::FontRole::small,
                    description_line,
                    {text_left, facts_top, lines_width, metrics.small_line},
                    kit::Align::left,
                    kit::colour::text,
                    view
                );
                facts_top += metrics.small_line;
            }
            bottom = facts_top;
            facts_top += metrics.hint_gap;
        }
        const std::string subtitle = registry_subtitle(registry, library.text);
        if (!subtitle.empty() && lines_width > 0) {
            add_text(
                building,
                kit::FontRole::small,
                cut(building, kit::FontRole::small, subtitle, lines_width),
                {text_left, facts_top, lines_width, metrics.small_line},
                kit::Align::left,
                kit::colour::hint,
                view
            );
            bottom = facts_top + metrics.small_line;
        }
        if (!registry.built_in) {
            const int32_t buttons_top = bottom + metrics.folder_button_gap;
            const std::string_view remove_caption = shown(library.text, words::registry_remove);
            const int32_t remove_width = button_width(building, remove_caption);
            add_button(
                building,
                building.detail,
                {text_left, buttons_top, remove_width, metrics.button_height},
                remove_caption,
                false,
                true,
                controls::first_registry_remove + static_cast<kit::ControlId>(index),
                named("registry." + word + ".remove"),
                view,
                controls::details_group
            );
            const int32_t mirror_left = text_left + remove_width + metrics.button_gap;
            if (library.inputs.in_match) {
                const int32_t lock_width = std::max(inner_right - mirror_left, int32_t{0});
                if (lock_width > 0) {
                    kit::LockLook look;
                    look.text = std::string(shown(library.text, words::registry_game_lock));
                    kit::add_lock(
                        building.list,
                        {mirror_left, buttons_top, lock_width, metrics.button_height},
                        look
                    );
                    building.list.items.back().clip = view;
                }
            } else {
                const std::string_view mirror_caption =
                    shown(library.text, words::registry_add_mirror);
                add_button(
                    building,
                    building.detail,
                    {mirror_left,
                     buttons_top,
                     button_width(building, mirror_caption),
                     metrics.button_height},
                    mirror_caption,
                    false,
                    true,
                    controls::first_registry_mirror + static_cast<kit::ControlId>(index),
                    named("registry." + word + ".add-mirror"),
                    view,
                    controls::details_group
                );
            }
            bottom = buttons_top + metrics.button_height;
        } else {
            kit::PillLook pill;
            pill.text = std::string(shown(library.text, words::registry_built_in));
            pill.state = kit::PillState::neutral;
            const int32_t pill_w = kit::pill_width(*building.fonts, pill);
            const int32_t pill_top = bottom + metrics.hint_gap;
            const int32_t pill_right = toggle.x + toggle.width;
            kit::add_pill(
                building.list,
                {pill_right - pill_w, pill_top, pill_w, kit::pill_height()},
                pill,
                kit::no_control,
                {}
            );
            building.list.items.back().clip = view;
            bottom = pill_top + kit::pill_height();
        }
        const kit::Rect card{left, card_top, width, bottom + inset - card_top};
        const std::size_t drawn = building.list.items.size();
        add_tile_face(building, card, view);
        std::rotate(
            building.list.items.begin() + static_cast<std::ptrdiff_t>(card_at),
            building.list.items.begin() + static_cast<std::ptrdiff_t>(drawn),
            building.list.items.end()
        );
        cursor = card.y + card.height;
    }

    cursor += metrics.label_gap;
    if (!library.inputs.registries_notice.empty()) {
        const std::vector<std::string> notice_lines =
            wrapped(building, kit::FontRole::small, library.inputs.registries_notice, width, 0);
        const int32_t notice_top = cursor;
        for (const std::string& notice_line : notice_lines) {
            add_text(
                building,
                kit::FontRole::small,
                notice_line,
                {left, cursor, width, metrics.small_line},
                kit::Align::left,
                kit::colour::lock,
                view
            );
            cursor += metrics.small_line;
        }
        add_label(
            building.detail,
            controls::registries_notice,
            {left, notice_top, width, std::max(cursor - notice_top, metrics.small_line)},
            unique_name(building, named("registries.notice")),
            joined(notice_lines),
            view,
            controls::details_group
        );
        cursor += metrics.hint_gap;
    }

    const std::string note = registry_note(registries, library.text);
    const std::vector<std::string> note_lines =
        wrapped(building, kit::FontRole::small, note, width, 0);
    const int32_t note_top = cursor;
    for (const std::string& note_line : note_lines) {
        add_text(
            building,
            kit::FontRole::small,
            note_line,
            {left, cursor, width, metrics.small_line},
            kit::Align::left,
            kit::colour::hint,
            view
        );
        cursor += metrics.small_line;
    }
    if (note_lines.empty())
        cursor += metrics.small_line;
    add_label(
        building.detail,
        controls::registries_note,
        {left, note_top, width, std::max(cursor - note_top, metrics.small_line)},
        unique_name(building, named("registries.note")),
        note,
        view,
        controls::details_group
    );

    const std::optional<controls::Extent> extent =
        controls::clipped_extent(building.list, view, first_item);
    return extent ? extent->bottom - first : 0;
}

/// Appends the Registries tab: its cards in an area that scrolls when they
/// are taller than it, with a column for the scroll bar at its right.
///
/// @param[in,out] building the layout
/// @param area the area
void add_registries(Building& building, const kit::Rect& area) {
    const kit::Metrics& metrics = *building.metrics;
    Building trial = building;
    trial.list = {};
    trial.detail.clear();
    trial.order.clear();
    const int32_t whole_height = add_registries_content(trial, area, area.width, 0);
    const int32_t well_width = metrics.scroll_thumb_width + 2;
    const bool scrolls = whole_height > area.height;
    const int32_t width = scrolls ? area.width - well_width - metrics.button_gap : area.width;
    int32_t height = whole_height;
    if (scrolls) {
        trial = building;
        trial.list = {};
        trial.detail.clear();
        trial.order.clear();
        height = add_registries_content(trial, area, width, 0);
    }
    const int32_t limit = kit::ScrollArea::limit_for(height, area.height);
    const int32_t scroll_offset = std::clamp(building.state->details_scroll, int32_t{0}, limit);
    add_registries_content(building, area, width, scroll_offset);
    if (limit > 0) {
        kit::ScrollArea scroll;
        scroll.view = area;
        scroll.well = {area.x + area.width - well_width, area.y, well_width, area.height};
        scroll.content_height = height;
        scroll.limit = limit;
        scroll.offset = scroll_offset;
        add_item(building.list, kit::Role::scroll_bar, scroll.well, {}).look =
            kit::ScrollBarLook{scroll.thumb(metrics.least_thumb_height), false};
    }
}

/// Appends ADD REGISTRY… at the top right of the Registries tab, above the
/// list, and moves the body down past it. During a match the padlock and
/// "Choose from the main menu." stand there, and the button is absent. The
/// button joins Tab's order later, after the rows, so it follows them.
///
/// @param[in,out] building the layout
/// @param window the window
/// @param[in,out] top the body's first row; moved down past the button
void add_registry_bar(Building& building, const Window& window, int32_t& top) {
    const Library& library = *building.library;
    if (library.tab != Tab::registries)
        return;
    const kit::Metrics& metrics = *building.metrics;
    const int32_t height = metrics.button_height;
    const int32_t room = std::max(window.right - window.left, int32_t{0});
    if (library.inputs.in_match) {
        const int32_t lock_width = std::min(
            width_of(
                building, kit::FontRole::small, shown(library.text, words::registry_game_lock)
            ) + metrics.padlock_width +
                metrics.padlock_gap,
            room
        );
        if (lock_width > 0) {
            kit::LockLook look;
            look.text = std::string(shown(library.text, words::registry_game_lock));
            kit::add_lock(
                building.list, {window.right - lock_width, top, lock_width, height}, look
            );
        }
    } else {
        const std::string_view caption = shown(library.text, words::registry_add);
        const int32_t button = std::min(button_width(building, caption), room);
        if (button > 0) {
            add_button(
                building,
                building.top,
                {window.right - button, top, button, height},
                caption,
                false,
                true,
                controls::add_registry,
                named("add-registry"),
                {},
                -1,
                false
            );
        }
    }
    top += height + window.gap;
}

/// Appends the footer's band, the queue's words at its left and its buttons
/// at its right: DETAILS at Compact but not on the Active or Registries
/// tabs, and CLOSE, the main action. On the Registries tab, ADD REGISTRY…
/// was drawn above the list; it joins Tab's order here, before CLOSE.
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

    std::vector<FooterButton> buttons;
    if (compact && library.tab != Tab::active && library.tab != Tab::registries)
        buttons.push_back({Action::details, controls::details, library.selected.has_value()});
    buttons.push_back({Action::close, controls::close, true});
    int32_t total = 0;
    for (const FooterButton& button : buttons)
        total +=
            button_width(building, action_text(button.action, library.text)) + metrics.button_gap;
    total -= metrics.button_gap;
    int32_t left = window.right - total;
    const int32_t queue_right = left - metrics.label_gap;
    if (library.tab == Tab::registries) {
        for (const kit::Control& control : building.top)
            if (control.id == controls::add_registry)
                tab_stop(building, control);
    }
    for (const FooterButton& button : buttons) {
        const std::string_view caption = action_text(button.action, library.text);
        const int32_t width = button_width(building, caption);
        add_button(
            building,
            building.footer,
            {left, window.button_top, width, metrics.button_height},
            caption,
            button.action == Action::close,
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
    const OpenMenu& menu_open = *building.open;
    const kit::Metrics& metrics = *building.metrics;
    const auto count = static_cast<int32_t>(menu_open.items.size());
    const kit::Rect menu = kit::choice_menu(menu_open.field, menu_open.items.size(), footer_line);
    const int32_t shown_count = kit::shown_choices(menu_open.items.size());
    const int32_t first = std::clamp(
        building.state->menu_first, int32_t{0}, std::max(count - shown_count, int32_t{0})
    );
    kit::ChoiceMenuLook look;
    look.first = first;
    look.total = count;
    look.chosen = menu_open.chosen;
    look.marked = building.state->menu_marked;
    look.focus_shown = building.state->interaction.focus_shown;
    for (int32_t place = 0; place < shown_count && first + place < count; ++place) {
        const controls::MenuItem& item = menu_open.items[static_cast<std::size_t>(first + place)];
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
        control.name = unique_name(building, menu_open.name + "." + item.word);
        control.kind = kit::ControlKind::list_item;
        control.focusable = false;
        control.checked = first + place == menu_open.chosen;
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
    building.scale_percent = frame.scale_percent > 0 ? frame.scale_percent : 100;
    library.scaled_badge_pixels.clear();
    const kit::Point size = window_size(frame);
    add_item(building.list, kit::Role::fill, {0, 0, size.x, size.y}, {}).colour =
        kit::colour::panel;
    return building;
}

/// Appends one line of a key tooltip, splitting it so each piece fits the
/// card's text width. A public key that is wider than the window is split
/// across consecutive lines, so the card shows every character.
///
/// @param building the layout
/// @param[in,out] lines the tooltip's block
/// @param text the line
/// @param inner the text width, in points
void append_fitted(
    const Building& building, std::vector<std::string>& lines, std::string_view text, int32_t inner
) {
    if (text.empty())
        return;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t count = text.size() - start;
        while (count > 1 &&
               width_of(building, kit::FontRole::small, text.substr(start, count)) > inner)
            --count;
        lines.emplace_back(text.substr(start, count));
        start += count;
    }
}

/// Returns the key icon whose tooltip shows: the one the keys or the pad
/// focus, or the one under the pointer.
///
/// @param state the screen's state
/// @return the icon's control; none when neither rests on a key icon
kit::ControlId key_tip_id(const ScreenState& state) noexcept {
    const kit::Interaction& interaction = state.interaction;
    if (interaction.focus_shown && controls::registry_key_control(interaction.focused))
        return interaction.focused;
    if (controls::registry_key_control(interaction.hovered))
        return interaction.hovered;
    return kit::no_control;
}

/// Appends the key tooltip over the window when a key icon is hovered or
/// focused and still in view. The card names each signing key's fingerprint
/// and its full public key, and whether the registry is built in or was
/// added by the player. It is drawn last and is not clipped to the list.
///
/// @param[in,out] building the layout
/// @param window the window
void add_key_tooltip(Building& building, const Window& window) {
    const kit::ControlId tip = key_tip_id(*building.state);
    if (tip == kit::no_control)
        return;
    const kit::Control* icon = nullptr;
    for (const kit::Control& control : building.detail)
        if (control.id == tip)
            icon = &control;
    if (icon == nullptr)
        return;
    const kit::Rect seen = icon->clip.width > 0 && icon->clip.height > 0
                               ? kit::intersect(icon->rect, icon->clip)
                               : icon->rect;
    if (seen.width <= 0 || seen.height <= 0)
        return;
    const std::size_t index = controls::registry_index(tip);
    const std::vector<Registry>& registries = building.library->inputs.registries;
    if (index >= registries.size())
        return;
    const Registry& registry = registries[index];
    const TextHooks& text = building.library->text;
    const int32_t padding = kit::compact_metrics.hover_card_padding;
    const int32_t inner = std::max(window.width - 2 * padding, int32_t{1});
    kit::HoverCardLook look;
    look.title = std::string(
        shown(text, registry.keys.size() > 1 ? words::registry_key_many : words::registry_key_one)
    );
    look.aside = std::string(
        shown(text, registry.built_in ? words::registry_key_built_in : words::registry_key_added)
    );
    if (registry.keys.empty()) {
        append_fitted(building, look.block, shown(text, words::registry_no_key), inner);
    } else {
        for (const SigningKey& signing_key : registry.keys) {
            append_fitted(building, look.block, signing_key.fingerprint, inner);
            append_fitted(building, look.block, signing_key.public_key, inner);
        }
    }
    const kit::Point size = kit::hover_card_size(*building.fonts, look);
    const kit::Rect frame{0, 0, window.width, window.height};
    const kit::Rect card = kit::hover_card_rect(seen, frame, size);
    const int32_t anchor_mid = seen.x + seen.width / 2;
    look.arrow_at = std::clamp(anchor_mid - card.x, int32_t{0}, std::max(card.width, int32_t{0}));
    look.arrow = card.y >= seen.y + seen.height ? kit::Side::top : kit::Side::bottom;
    kit::add_hover_card(building.list, card, look);
}

/// Appends UNLOAD MOD's hint when the pointer or the keys rest on it.
/// The card is playing_note for the mod being played: the same words
/// SWITCH OFF shows under that mod. The Active tab selects nothing, so
/// the selected entry's note is empty here.
///
/// @param[in,out] building the layout
/// @param window the window
void add_unload_note(Building& building, const Window& window) {
    const kit::ControlId tip = controls::active_unload;
    if (!hovered(building, tip) && !focused(building, tip))
        return;
    const Entry* played = nullptr;
    for (const Entry& candidate : building.library->entries) {
        if (candidate.id.kind == Kind::mod && candidate.installed != nullptr &&
            candidate.installed->playing) {
            played = &candidate;
            break;
        }
    }
    if (played == nullptr)
        return;
    const std::string note = playing_note(*building.library, *played);
    if (note.empty())
        return;
    const kit::Control* button = nullptr;
    for (const kit::Control& control : building.detail)
        if (control.id == tip)
            button = &control;
    if (button == nullptr)
        return;
    const kit::Rect seen = button->clip.width > 0 && button->clip.height > 0
                               ? kit::intersect(button->rect, button->clip)
                               : button->rect;
    if (seen.width <= 0 || seen.height <= 0)
        return;
    const int32_t padding = kit::compact_metrics.hover_card_padding;
    const int32_t inner = std::max(window.width - 2 * padding, int32_t{1});
    kit::HoverCardLook look;
    append_fitted(building, look.block, note, inner);
    const kit::Point size = kit::hover_card_size(*building.fonts, look);
    const kit::Rect frame{0, 0, window.width, window.height};
    const kit::Rect card = kit::hover_card_rect(seen, frame, size);
    const int32_t anchor_mid = seen.x + seen.width / 2;
    look.arrow_at = std::clamp(anchor_mid - card.x, int32_t{0}, std::max(card.width, int32_t{0}));
    look.arrow = card.y >= seen.y + seen.height ? kit::Side::top : kit::Side::bottom;
    kit::add_hover_card(building.list, card, look);
}

/// Finishes a layout: the window's edge, the focus ring, the open menu over
/// everything, the key tooltip, and the controls in the order a press tries them.
///
/// @param[in,out] building the layout
/// @param window the window
/// @return the display list
kit::DisplayList finish(Building& building, const Window& window) {
    add_item(building.list, kit::Role::bevel, {0, 0, window.width, window.height}, {}).colour =
        kit::colour::edge_light;
    add_focus_ring(building);
    add_open_menu(building, window.footer_rule);
    add_unload_note(building, window);
    add_key_tooltip(building, window);
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

std::optional<Action> row_icon_action(const Library& library, const Entry& entry) {
    if (entry.state != State::get && entry.state != State::update)
        return std::nullopt;
    const std::vector<ActionButton> actions = entry_actions(library, entry);
    if (actions.empty() || !actions.front().enabled)
        return std::nullopt;
    const Action first = actions.front().action;
    if (first != Action::get && first != Action::update)
        return std::nullopt;
    return first;
}

std::vector<MenuItem>
menu_items(const Library& library, MenuKind menu, const std::vector<bool>& tag_shown) {
    std::vector<MenuItem> items;
    switch (menu) {
    case MenuKind::none:
        break;
    case MenuKind::filter: {
        for (std::size_t index = 0; index < filter_order.size(); ++index) {
            MenuItem item;
            item.caption = filter_caption(library, filter_order[index]);
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
    case MenuKind::tags: {
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
    case MenuKind::more: {
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

int32_t chosen_item(const Library& library, MenuKind menu, const std::vector<MenuItem>& items) {
    if (menu == MenuKind::more)
        return -1;
    const std::vector<std::pair<std::string, std::size_t>> tags = tags_in_tab(library);
    for (std::size_t place = 0; place < items.size(); ++place) {
        const MenuItem& item = items[place];
        const bool tag_chosen = item.tag && *item.tag < tags.size() && !library.tag.empty() &&
                                tags[*item.tag].first == library.tag;
        const bool filter_chosen =
            item.filter && library.tag.empty() && *item.filter == shown_filter(library);
        if (tag_chosen || filter_chosen)
            return static_cast<int32_t>(place);
    }
    return -1;
}

std::optional<Extent>
clipped_extent(const kit::DisplayList& display, const kit::Rect& view, std::size_t first_item) {
    std::optional<Extent> extent;
    for (std::size_t index = first_item; index < display.items.size(); ++index) {
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
        add_library_header(
            building,
            window,
            shown(library.text, words::title_library),
            {},
            library.inputs.version_text
        );
    } else {
        add_library_header(
            building,
            window,
            shown(library.text, words::title_open_annihilation),
            shown(library.text, words::title_library),
            library.inputs.version_text
        );
    }
    const int32_t content_width = window.right - window.left;
    int32_t top = window.body_top + window.gap;
    const bool active = library.tab == Tab::active;
    const bool registries = library.tab == Tab::registries;
    const bool catalogue = !active && !registries;
    // The Updates tab's line, count and UPDATE ALL, takes a row over its list.
    const bool updates_line = library.tab == Tab::updates;
    const int32_t updates_row = metrics.button_height + window.gap;
    const auto body_of = [&](int32_t body_top) {
        return kit::Rect{
            window.left, body_top, content_width, window.footer_rule - window.gap - body_top
        };
    };
    if (compact) {
        add_tabs(building, {window.left, top, content_width, metrics.tab_height}, true);
        top += metrics.tab_height + window.gap;
        if (active)
            add_active(building, body_of(top));
        else if (registries) {
            add_registry_bar(building, window, top);
            add_registries(building, body_of(top));
        } else {
            const int32_t row_height_points = std::max(metrics.field_height, metrics.button_height);
            add_compact_search_row(building, {window.left, top, content_width, row_height_points});
            top += row_height_points + window.gap;
            if (updates_line) {
                add_updates_line(
                    building, {window.left, top, content_width, metrics.button_height}
                );
                top += updates_row;
            }
            add_list(
                building,
                {window.left, top, content_width, window.footer_rule - window.gap - top},
                RowForm::compact
            );
        }
    } else {
        add_tabs(
            building,
            {window.left,
             top,
             content_width - wide_search_width - window.padding,
             metrics.tab_height},
            false
        );
        if (catalogue)
            add_search(
                building,
                {window.right - wide_search_width,
                 top + (metrics.tab_height - metrics.field_height) / 2,
                 wide_search_width,
                 metrics.field_height}
            );
        top += metrics.tab_height + window.gap;
        if (active)
            add_active(building, body_of(top));
        else if (registries) {
            add_registry_bar(building, window, top);
            add_registries(building, body_of(top));
        } else {
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
            int32_t list_top = top;
            if (updates_line) {
                add_updates_line(building, {left, top, list_width, metrics.button_height});
                list_top += updates_row;
            }
            add_list(
                building,
                {left, list_top, list_width, window.footer_rule - window.gap - list_top},
                large ? RowForm::large : RowForm::regular
            );
            left += list_width + window.padding;
            add_details_pane(building, {left, top, window.right - left, panes_height});
        }
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
        add_library_header(building, window, shown(library.text, words::title_library), {}, {});
        return finish(building, window);
    }
    add_library_header(building, window, entry_name(*entry), {}, entry_version(library, *entry));

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
        if (const std::string_view about = entry_summary(*entry); !about.empty()) {
            const std::vector<std::string> summary =
                wrapped(into, kit::FontRole::small, about, width, compact_summary_lines);
            top = add_lines(into, summary, view.x, top, width, kit::colour::list_text, view) +
                  metrics.paragraph_gap;
        }
        top = add_map_previews(into, *entry, view.x, top, width, view);
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
            index == 0 && action.action != Action::uninstall,
            action.enabled,
            controls::action_control(action.action),
            named("action." + std::string(action_word(action.action))),
            {},
            -1
        );
        left += width_points + metrics.button_gap;
    }
    if (actions.size() > compact_action_buttons) {
        std::vector<controls::MenuItem> items = controls::menu_items(library, MenuKind::more, {});
        const std::string_view caption = shown(library.text, words::more_menu);
        const int32_t width_points = drop_down_width(building, caption, items);
        add_drop_down(
            building,
            building.footer,
            {left,
             window.button_top + (metrics.button_height - metrics.regular_line) / 2,
             width_points,
             metrics.regular_line},
            MenuKind::more,
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
        true,
        true,
        controls::close,
        named("close"),
        {},
        -1
    );
    return finish(building, window);
}

namespace {

/// What one refresh mark draws, enough to tell a later step from this one.
struct MarkShot {
    bool turning{};
    uint8_t turn{};
    uint8_t ease{};
    bool operator==(const MarkShot&) const = default;
};

/// Returns the picture a mark draws: the turning arrow, or the check's ease.
///
/// @param turning the arrow is turning
/// @param turn the shared eighth, used while it turns
/// @param check_ms how long the check has shown; 0 while it has not
/// @return the picture
MarkShot mark_shot(bool turning, uint8_t turn, uint16_t check_ms) noexcept {
    MarkShot shot;
    shot.turning = turning;
    shot.turn = turning ? turn : uint8_t{0};
    shot.ease =
        turning ? uint8_t{0} : (check_ms == 0 ? uint8_t{0} : kit::refresh_check_ease(check_ms));
    return shot;
}

/// Returns a registry's mark, or none when this step has not kept one.
///
/// @param state the marks
/// @param id the registry
/// @return the mark; none when the registry has no row
const ScreenState::RegistryMark* mark_of(const ScreenState& state, std::string_view id) noexcept {
    for (const ScreenState::RegistryMark& mark : state.registry_marks)
        if (mark.id == id)
            return &mark;
    return nullptr;
}

/// Moves a check forward, or starts one.
///
/// A start with no time yet still counts as begun. A check that has already
/// lasted kit::refresh_check_ms is over, and the arrow shows.
///
/// @param shown how long it has shown; 0 while it has not
/// @param begin true starts it from nothing
/// @param elapsed_ms the milliseconds to add
/// @return the milliseconds still to show; 0 when the arrow is back
uint16_t advance_check(uint16_t shown, bool begin, uint32_t elapsed_ms) noexcept {
    if (!begin && shown == 0)
        return 0;
    const uint32_t next =
        begin ? (elapsed_ms == 0 ? 1u : elapsed_ms) : static_cast<uint32_t>(shown) + elapsed_ms;
    if (next >= kit::refresh_check_ms)
        return 0;
    return static_cast<uint16_t>(next);
}

} // namespace

bool step_refresh_marks(
    ScreenState& state,
    bool checking,
    std::span<const RegistryPulse> registries,
    uint32_t elapsed_ms
) {
    const MarkShot header_before =
        mark_shot(state.refresh_was_checking, state.refresh_turn, state.refresh_check_ms);
    std::vector<MarkShot> rows_before;
    rows_before.reserve(registries.size());
    for (const RegistryPulse& pulse : registries) {
        const ScreenState::RegistryMark* held = mark_of(state, pulse.id);
        const bool turning = held != nullptr && held->was_refreshing;
        const uint16_t shown = held != nullptr ? held->check_ms : uint16_t{0};
        rows_before.push_back(mark_shot(turning, state.refresh_turn, shown));
    }

    bool any_refreshing = checking;
    for (const RegistryPulse& pulse : registries)
        any_refreshing = any_refreshing || pulse.refreshing;
    if (any_refreshing) {
        if (!state.refresh_spinning)
            state.refresh_spin_ms = 0;
        state.refresh_spin_ms += elapsed_ms;
        state.refresh_turn = kit::refresh_eighth(state.refresh_spin_ms);
    } else {
        state.refresh_spin_ms = 0;
        state.refresh_turn = 0;
    }
    state.refresh_spinning = any_refreshing;

    if (checking && !state.refresh_was_checking)
        state.refresh_read_list = false;
    for (const RegistryPulse& pulse : registries) {
        const ScreenState::RegistryMark* held = mark_of(state, pulse.id);
        const bool was_refreshing = held != nullptr && held->was_refreshing;
        if (was_refreshing && !pulse.refreshing && !pulse.failed &&
            (checking || state.refresh_was_checking))
            state.refresh_read_list = true;
    }

    if (checking)
        state.refresh_check_ms = 0;
    else if (state.refresh_was_checking) {
        state.refresh_check_ms =
            state.refresh_read_list ? advance_check(0, true, elapsed_ms) : uint16_t{0};
        state.refresh_read_list = false;
    } else {
        state.refresh_check_ms = advance_check(state.refresh_check_ms, false, elapsed_ms);
    }
    state.refresh_was_checking = checking;

    std::vector<ScreenState::RegistryMark> next_marks;
    next_marks.reserve(registries.size());
    for (const RegistryPulse& pulse : registries) {
        const ScreenState::RegistryMark* held = mark_of(state, pulse.id);
        const bool was_refreshing = held != nullptr && held->was_refreshing;
        const uint16_t previous = held != nullptr ? held->check_ms : uint16_t{0};
        ScreenState::RegistryMark row;
        row.id = std::string(pulse.id);
        row.was_refreshing = pulse.refreshing;
        if (pulse.refreshing)
            row.check_ms = 0;
        else if (was_refreshing)
            row.check_ms = pulse.failed ? uint16_t{0} : advance_check(0, true, elapsed_ms);
        else
            row.check_ms = advance_check(previous, false, elapsed_ms);
        if (row.was_refreshing || row.check_ms > 0)
            next_marks.push_back(std::move(row));
    }
    state.registry_marks = std::move(next_marks);

    if (mark_shot(checking, state.refresh_turn, state.refresh_check_ms) != header_before)
        return true;
    for (std::size_t row = 0; row < registries.size(); ++row) {
        const ScreenState::RegistryMark* held = mark_of(state, registries[row].id);
        const bool turning = held != nullptr && held->was_refreshing;
        const uint16_t shown = held != nullptr ? held->check_ms : uint16_t{0};
        if (mark_shot(turning, state.refresh_turn, shown) != rows_before[row])
            return true;
    }
    return false;
}

} // namespace oa::ui::library
