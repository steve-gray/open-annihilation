// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Registries part of Downloads: a second view of the section that lists
// every registry OA gets content from. Each row shows the registry's
// avatar, its name (with a Built in chip for one that ships with the game),
// a subtitle and an Off/On switch; a registry the player added also has
// REMOVE and ADD MIRROR…. Switches and removals stay on the dialog until OK;
// ADD REGISTRY… and ADD MIRROR… ask the host, whose window adds at once.
// Every control's place comes from fixed widths, never from a font, so a
// press lands where the part draws the control.

#include "registries_page.hpp"

#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/looks.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/kit/theme.hpp"

#include "settings_rows.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::engine_settings::registries {

namespace {

namespace kit = oa::ui::kit;
using geometry::SourceRect;

/// The ‹ DOWNLOADS link's width, which the heading's words follow.
constexpr int32_t kBackWidth = 80;
/// The columns between the link and the heading's words.
constexpr int32_t kHeadingGap = 12;
/// An avatar's side.
constexpr int32_t kAvatarSide = 28;
/// The columns between an avatar and the row's words.
constexpr int32_t kAvatarGap = 6;
/// REMOVE's and UNDO's width.
constexpr int32_t kRemoveWidth = 60;
/// ADD MIRROR…'s width.
constexpr int32_t kMirrorWidth = 96;
/// The columns between REMOVE and ADD MIRROR….
constexpr int32_t kButtonGap = 5;
/// The rows between a subtitle and the row's buttons.
constexpr int32_t kButtonLineGap = 4;
/// The rows between the last row and the note under the list.
constexpr int32_t kNoteGap = 8;

/// Captions of the rows' buttons, in English.
constexpr std::string_view kRemoveText = "REMOVE";
constexpr std::string_view kUndoText = "UNDO";
constexpr std::string_view kMirrorText = "ADD MIRROR…";

/// The word each control's name starts with.
constexpr std::string_view kNamePrefix = "settings";

/// One registry's row, placed.
struct PlacedRow {
    std::size_t index{};   ///< its place in the offer
    int32_t top{};         ///< the row of its line
    int32_t height{};      ///< rows from its line to the next row's
    SourceRect avatar{};   ///< the avatar's square
    SourceRect title{};    ///< the room for its name and chip
    SourceRect subtitle{}; ///< its subtitle, or the removal's words
    SourceRect toggle{};   ///< its switch, at the name's line's right
    SourceRect remove{};   ///< REMOVE or UNDO; empty for a built-in registry
    SourceRect mirror{};   ///< ADD MIRROR…; empty for a built-in registry
    SourceRect lock{};     ///< the lock in ADD MIRROR…'s place during a game
};

/// The part's content, placed.
struct Placed {
    std::vector<PlacedRow> rows;          ///< one for each registry
    std::vector<std::string> notice;      ///< the last OK's failure, broken into lines
    std::vector<SourceRect> notice_lines; ///< where those lines lie
    std::vector<std::string> note;        ///< the note, broken into lines
    std::vector<SourceRect> note_lines;   ///< where those lines lie
    int32_t content_height{}; ///< from the first row's line to the end gap under the note
};

/// Returns how wide a text is in a font, or at the estimated width without one.
///
/// @param fonts the fonts; null to estimate
/// @param font which of them
/// @return the measure
geometry::TextWidth width_in(const DialogFonts* fonts, DialogFont font) {
    return [fonts, font](std::string_view text) {
        if (fonts != nullptr)
            return dialog_text_width(*fonts, font, text);
        return static_cast<int32_t>(kit::character_count(text)) * estimated_character_width;
    };
}

/// Returns the player's choice for a registry, or null past the choices.
///
/// @param dialog the dialog
/// @param index the registry's place
/// @return the choice
const RegistryChoice* choice_of(const Dialog& dialog, std::size_t index) noexcept {
    return index < dialog.registry_chosen.size() ? &dialog.registry_chosen[index] : nullptr;
}

/// Tells whether a registry's switch is on, as the player has it.
///
/// @param dialog the dialog
/// @param index the registry's place
/// @return the switch
bool switch_on(const Dialog& dialog, std::size_t index) noexcept {
    if (const RegistryChoice* choice = choice_of(dialog, index))
        return choice->on;
    return index < dialog.registries.registries.size() ? dialog.registries.registries[index].on
                                                       : true;
}

/// Tells whether a registry is marked for removal.
///
/// @param dialog the dialog
/// @param index the registry's place
/// @return true once REMOVE was pressed and not undone
bool removed(const Dialog& dialog, std::size_t index) noexcept {
    const RegistryChoice* choice = choice_of(dialog, index);
    return choice != nullptr && choice->removed;
}

/// Returns the word a registry's controls are named by: its id in name form.
///
/// @param row the registry
/// @return the word; "registry" for an id with no word in it
std::string word_of(const RegistryRow& row) {
    std::string word = geometry::word_form(row.id, false);
    return word.empty() ? std::string("registry") : word;
}

/// Returns a count of things with its noun, one or many.
///
/// @param count how many
/// @param one the noun for one, in English
/// @param many the noun for any other count, in English
/// @return such as "1 package" or "31 packages"
std::string counted(std::size_t count, std::string_view one, std::string_view many) {
    const std::string number = std::to_string(count);
    return geometry::filled(count == 1 ? one : many, {{"n", number}});
}

/// Joins names as a sentence lists them: "A", "A and B", "A, B and C".
///
/// @param names the names
/// @return the list
std::string listed(const std::vector<std::string>& names) {
    std::string text;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index > 0)
            text += index + 1 == names.size() ? " and " : ", ";
        text += names[index];
    }
    return text;
}

/// Breaks a text into the lines the part shows, at the section's hint width.
///
/// @param dialog the dialog
/// @param text the text, as shown
/// @return at most most_notice_lines lines
std::vector<std::string> wrapped(const Dialog& dialog, std::string_view text) {
    const std::size_t columns =
        std::max(geometry::sizes_of(dialog).hint_line_characters, std::size_t{1});
    return geometry::break_lines(text, columns, geometry::most_notice_lines);
}

/// Places the part's rows and note at an offset.
///
/// @param dialog the dialog
/// @param scroll how far the content is moved up
/// @return the placed content
Placed place(const Dialog& dialog, int32_t scroll) {
    const geometry::Sizes& sized = geometry::sizes_of(dialog);
    const bool locked = adding_locked(dialog);
    Placed placed;
    int32_t cursor = geometry::first_row_top;
    const std::vector<RegistryRow>& rows = dialog.registries.registries;
    placed.rows.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        PlacedRow row;
        row.index = index;
        row.top = cursor;
        const int32_t line = cursor + geometry::row_padding;
        row.avatar = {sized.content_left, line + 1, kAvatarSide, kAvatarSide};
        const int32_t text_left = row.avatar.x + kAvatarSide + kAvatarGap;
        row.toggle = {
            sized.content_right - geometry::switch_width,
            line,
            geometry::switch_width,
            geometry::label_line_height,
        };
        row.title = {
            text_left,
            line,
            std::max(row.toggle.x - geometry::label_gap - text_left, int32_t{1}),
            geometry::label_line_height,
        };
        const int32_t subtitle_top = line + geometry::label_line_height + geometry::hint_gap;
        row.subtitle = {
            text_left,
            subtitle_top,
            sized.content_right - text_left,
            geometry::hint_line_height,
        };
        int32_t bottom = subtitle_top + geometry::hint_line_height;
        if (!rows[index].built_in) {
            const int32_t buttons_top = bottom + kButtonLineGap;
            row.remove = {text_left, buttons_top, kRemoveWidth, geometry::button_height};
            const int32_t mirror_left = text_left + kRemoveWidth + kButtonGap;
            if (locked)
                row.lock = {
                    mirror_left,
                    buttons_top,
                    sized.content_right - mirror_left,
                    geometry::button_height,
                };
            else
                row.mirror = {mirror_left, buttons_top, kMirrorWidth, geometry::button_height};
            bottom = buttons_top + geometry::button_height;
        }
        row.height = bottom + geometry::row_padding - cursor;
        cursor += row.height;
        placed.rows.push_back(row);
    }
    cursor += kNoteGap;
    if (!dialog.registries.notice.empty()) {
        placed.notice = wrapped(dialog, dialog.registries.notice);
        for (std::size_t index = 0; index < placed.notice.size(); ++index) {
            placed.notice_lines.push_back(
                {sized.content_left, cursor, sized.content_width, geometry::hint_line_height}
            );
            cursor += geometry::hint_line_height;
        }
        cursor += geometry::hint_gap;
    }
    placed.note = wrapped(dialog, note_text(dialog));
    for (std::size_t index = 0; index < placed.note.size(); ++index) {
        placed.note_lines.push_back(
            {sized.content_left, cursor, sized.content_width, geometry::hint_line_height}
        );
        cursor += geometry::hint_line_height;
    }
    placed.content_height = cursor + geometry::end_gap - geometry::first_row_top;

    const auto lift = [scroll](SourceRect& rect) {
        if (rect.width > 0 && rect.height > 0)
            rect.y -= scroll;
    };
    for (PlacedRow& row : placed.rows) {
        row.top -= scroll;
        for (SourceRect* rect :
             {&row.avatar,
              &row.title,
              &row.subtitle,
              &row.toggle,
              &row.remove,
              &row.mirror,
              &row.lock})
            lift(*rect);
    }
    for (SourceRect& rect : placed.notice_lines)
        lift(rect);
    for (SourceRect& rect : placed.note_lines)
        lift(rect);
    return placed;
}

/// Returns the part's scroll offset, held to its limit.
///
/// @param dialog the dialog
/// @param content_height the content's height
/// @return the offset
int32_t offset_of(const Dialog& dialog, int32_t content_height) noexcept {
    const geometry::Sizes& sized = geometry::sizes_of(dialog);
    const int32_t limit = kit::ScrollArea::limit_for(content_height, sized.view.height);
    const auto slot = static_cast<std::size_t>(Page::downloads);
    return std::clamp(dialog.scroll[slot], int32_t{0}, limit);
}

/// Returns the content placed at the part's own offset.
///
/// @param dialog the dialog
/// @return the placed content
Placed placed_now(const Dialog& dialog) {
    const Placed at_top = place(dialog, 0);
    const int32_t offset = offset_of(dialog, at_top.content_height);
    return offset == 0 ? at_top : place(dialog, offset);
}

/// Returns the heading's words' place, right of the link.
///
/// @param dialog the dialog
/// @return the rectangle
SourceRect heading_rect(const Dialog& dialog) noexcept {
    const SourceRect& heading = geometry::sizes_of(dialog).heading;
    return {
        heading.x + kBackWidth + kHeadingGap,
        heading.y,
        heading.width - kBackWidth - kHeadingGap,
        heading.height,
    };
}

/// Returns the link's place, at the heading's left.
///
/// @param dialog the dialog
/// @return the rectangle
SourceRect back_rect(const Dialog& dialog) noexcept {
    const SourceRect& heading = geometry::sizes_of(dialog).heading;
    return {heading.x, heading.y, kBackWidth, heading.height};
}

/// Returns a chip's width for its words.
///
/// @param measure the small font's measure
/// @return the width
int32_t chip_width(const geometry::TextWidth& measure) {
    return measure(geometry::shown_text(built_in_text)) + 2 * kit::compact_metrics.chip_padding;
}

/// Where a row's name and its chip are drawn.
struct TitleParts {
    std::string title; ///< the name, cut to its room
    SourceRect text{}; ///< the name's own width
    SourceRect chip{}; ///< the chip after it; empty for an added registry
};

/// Places a row's name and, for a built-in registry, the chip after it.
///
/// @param row the placed row
/// @param registry the registry
/// @param regular the regular font's measure
/// @param small the small font's measure
/// @return the parts
TitleParts title_parts(
    const PlacedRow& row,
    const RegistryRow& registry,
    const geometry::TextWidth& regular,
    const geometry::TextWidth& small
) {
    TitleParts parts;
    const int32_t chip = registry.built_in ? chip_width(small) : 0;
    const int32_t chip_room = registry.built_in ? chip + kit::compact_metrics.list_row_gap : 0;
    const int32_t room = std::max(row.title.width - chip_room, int32_t{1});
    parts.title = geometry::cut_text(registry.name, room, regular);
    const int32_t width = std::clamp(regular(parts.title), int32_t{1}, room);
    parts.text = {row.title.x, row.title.y, width, row.title.height};
    if (registry.built_in)
        parts.chip = {
            row.title.x + width + kit::compact_metrics.list_row_gap,
            row.title.y + (row.title.height - kit::compact_metrics.chip_height) / 2,
            chip,
            kit::compact_metrics.chip_height,
        };
    return parts;
}

/// Returns the words a row shows under its name.
///
/// @param dialog the dialog
/// @param index the registry's place
/// @return its subtitle, or the removal's words
std::string subtitle_of(const Dialog& dialog, std::size_t index) {
    if (removed(dialog, index))
        return std::string(geometry::shown_text(removed_text));
    return subtitle(dialog.registries.registries[index]);
}

/// Returns a control of the part, named and in the section's scroll group.
///
/// @param id its number
/// @param rect where it lies
/// @param name its name, after the dialog's word
/// @param kind what it is
/// @param view what a press reaches it through
/// @return the control
kit::Control part_control(
    int32_t id,
    const SourceRect& rect,
    const std::string& name,
    kit::ControlKind kind,
    const SourceRect& view
) {
    kit::Control control;
    control.id = id;
    control.rect = rect;
    control.clip = view;
    control.name = std::string(kNamePrefix) + "." + name;
    control.kind = kind;
    control.group = geometry::section_group;
    return control;
}

/// Appends a text item.
///
/// @param[in,out] list the list
/// @param rect where
/// @param words the words
/// @param font which font
/// @param colour the colour
/// @param align where the words sit
/// @param clip what it is drawn clipped to
void add_text(
    kit::DisplayList& list,
    const SourceRect& rect,
    std::string words,
    DialogFont font,
    kit::Colour colour,
    kit::Align align,
    const SourceRect& clip
) {
    kit::Item item;
    item.role = kit::Role::text;
    item.rect = rect;
    item.clip = clip;
    item.text = std::move(words);
    item.font = font;
    item.colour = colour;
    item.align = align;
    list.items.push_back(std::move(item));
}

/// Appends a button item and, when the keys mark it, its focus ring.
///
/// @param[in,out] list the list
/// @param dialog the dialog
/// @param rect where
/// @param caption the caption, as shown
/// @param control its control
/// @param clip what it is drawn clipped to
void add_button(
    kit::DisplayList& list,
    const Dialog& dialog,
    const SourceRect& rect,
    std::string caption,
    int32_t control,
    const SourceRect& clip
) {
    const bool hovered = dialog.hovered == control || dialog.pressed == control;
    const bool held = dialog.pressed == control && dialog.hovered == control;
    kit::Item item;
    item.role = kit::Role::button;
    item.rect = rect;
    item.clip = clip;
    item.text = caption;
    item.control = control;
    item.state.hovered = hovered;
    item.state.pressed = held;
    item.look = kit::ButtonLook{std::move(caption), kit::ButtonStyle::quiet, hovered, held, true};
    list.items.push_back(std::move(item));
    if (dialog.focused == control) {
        kit::Item ring;
        ring.role = kit::Role::focus_ring;
        ring.rect = rect;
        ring.clip = clip;
        ring.look = kit::FocusRingLook{true};
        list.items.push_back(std::move(ring));
    }
}

/// Appends a padlock and a lock's words in an area.
///
/// @param[in,out] list the list
/// @param area the lock's area
/// @param clip what it is drawn clipped to
void add_lock(kit::DisplayList& list, const SourceRect& area, const SourceRect& clip) {
    kit::Item padlock;
    padlock.role = kit::Role::mark;
    padlock.rect = {area.x, area.y + (area.height - geometry::padlock_height) / 2, 0, 0};
    padlock.clip = clip;
    padlock.colour = kit::colour::lock;
    padlock.look = kit::MarkLook{kit::Mark::padlock, kit::colour::lock};
    list.items.push_back(std::move(padlock));
    const int32_t text_left = area.x + geometry::padlock_width + geometry::padlock_gap;
    add_text(
        list,
        {text_left, area.y, area.x + area.width - text_left, area.height},
        std::string(geometry::shown_text(game_lock_text)),
        DialogFont::small,
        kit::colour::lock,
        kit::Align::left,
        clip
    );
}

/// Returns the row and the part of the row a control is, from its number.
struct RowControl {
    std::size_t index{}; ///< the registry's place
    int32_t part{};      ///< 0 the switch, 1 REMOVE or UNDO, 2 ADD MIRROR…
};

/// Finds the row a control of the part belongs to.
///
/// @param dialog the dialog
/// @param control the control
/// @return the row and part; nothing for a control of no row
std::optional<RowControl> row_control(const Dialog& dialog, int32_t control) noexcept {
    const int32_t offset = control - registry_switch_control(0);
    if (offset < 0)
        return std::nullopt;
    const auto index = static_cast<std::size_t>(offset / registry_row_controls);
    if (index >= dialog.registries.registries.size())
        return std::nullopt;
    return RowControl{index, offset % registry_row_controls};
}

/// Returns the player's choice for a registry, made when it is missing.
///
/// @param[in,out] dialog the dialog
/// @param index the registry's place, in the offer
/// @return the choice
RegistryChoice& chosen(Dialog& dialog, std::size_t index) {
    while (dialog.registry_chosen.size() <= index) {
        const RegistryRow& row = dialog.registries.registries[dialog.registry_chosen.size()];
        dialog.registry_chosen.push_back(RegistryChoice{row.id, row.on, false});
    }
    return dialog.registry_chosen[index];
}

} // namespace

bool shown(const Dialog& dialog) noexcept {
    return dialog.kind == DialogKind::engine && dialog.page == Page::downloads &&
           dialog.downloads_part == DownloadsPart::registries;
}

void show(Dialog& dialog) noexcept {
    dialog.downloads_part = DownloadsPart::registries;
    dialog.scroll[static_cast<std::size_t>(Page::downloads)] = 0;
    dialog.wheel_rows = 0.0F;
    dialog.hovered = no_control;
    if (dialog.focused != no_control)
        dialog.focused = registries_back_control;
}

DialogAction back(Dialog& dialog) noexcept {
    dialog.downloads_part = DownloadsPart::main;
    dialog.scroll[static_cast<std::size_t>(Page::downloads)] = 0;
    dialog.wheel_rows = 0.0F;
    dialog.hovered = no_control;
    dialog.pressed = no_control;
    // The Registries row is Downloads' last: after the check, two rows for
    // each install ID, and the files.
    if (dialog.focused != no_control)
        dialog.focused =
            first_row_control + 2 + 2 * static_cast<int32_t>(dialog.downloads.install_ids.size());
    return DialogAction::redraw;
}

std::string summary(const Dialog& dialog) {
    const std::vector<RegistryRow>& rows = dialog.registries.registries;
    std::size_t listed_count = 0;
    std::size_t on = 0;
    const RegistryRow* only = nullptr;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (removed(dialog, index))
            continue;
        ++listed_count;
        if (switch_on(dialog, index)) {
            ++on;
            only = &rows[index];
        }
    }
    if (listed_count == 0)
        return std::string(geometry::shown_text("None"));
    if (on == 1 && only != nullptr && only->built_in)
        return geometry::filled("{name} only", {{"name", only->name}});
    const std::string count = std::to_string(on);
    const std::string of = std::to_string(listed_count);
    return geometry::filled("{n} on of {m}", {{"n", count}, {"m", of}});
}

std::string initials(std::string_view name) {
    std::vector<std::string_view> words;
    std::size_t start = 0;
    while (start < name.size()) {
        while (start < name.size() && name[start] == ' ')
            ++start;
        std::size_t end = start;
        while (end < name.size() && name[end] != ' ')
            ++end;
        if (end > start)
            words.push_back(name.substr(start, end - start));
        start = end;
    }
    const auto upper = [](std::string_view character) {
        std::string text(character);
        if (text.size() == 1 && text[0] >= 'a' && text[0] <= 'z')
            text[0] = static_cast<char>(text[0] - 'a' + 'A');
        return text;
    };
    const auto first = [](std::string_view word) {
        return word.substr(0, std::max(kit::character_bytes(word), std::size_t{1}));
    };
    std::string letters;
    if (words.size() >= 2) {
        for (std::size_t index = 0; index < words.size() && index < 3; ++index)
            letters += upper(first(words[index]));
        return letters;
    }
    if (words.empty())
        return letters;
    std::string_view word = words.front();
    for (int32_t taken = 0; taken < 2 && !word.empty(); ++taken) {
        const std::string_view character = first(word);
        letters += upper(character);
        word.remove_prefix(character.size());
    }
    return letters;
}

std::string age_text(int64_t seconds) {
    constexpr int64_t minute = 60;
    constexpr int64_t hour = 60 * minute;
    constexpr int64_t day = 24 * hour;
    if (seconds < minute)
        return std::string(geometry::shown_text("just now"));
    if (seconds < hour)
        return geometry::filled("{n} min ago", {{"n", std::to_string(seconds / minute)}});
    if (seconds < day)
        return geometry::filled("{n} h ago", {{"n", std::to_string(seconds / hour)}});
    if (seconds < 2 * day)
        return std::string(geometry::shown_text("1 day ago"));
    return geometry::filled("{n} days ago", {{"n", std::to_string(seconds / day)}});
}

std::string subtitle(const RegistryRow& row) {
    const std::string separator = " · ";
    std::string unreached;
    if (row.unreachable) {
        unreached = std::string(geometry::shown_text("couldn't reach"));
        if (row.checked_ago)
            unreached += separator +
                         geometry::filled("list from {age}", {{"age", age_text(*row.checked_ago)}});
    }
    const std::string packages = counted(row.packages, "{n} package", "{n} packages");
    std::string text;
    if (row.built_in) {
        text = packages + separator;
        if (row.unreachable)
            text += unreached;
        else if (row.checked_ago)
            text += geometry::filled("checked {age}", {{"age", age_text(*row.checked_ago)}});
        else
            text += geometry::shown_text("not checked yet");
    } else {
        text = row.address + separator + packages + separator +
               std::string(geometry::shown_text("not reviewed"));
    }
    if (row.mirrors > 0)
        text += separator + counted(row.mirrors, "{n} mirror", "{n} mirrors");
    if (!row.built_in && row.unreachable)
        text += separator + unreached;
    return text;
}

std::string note_text(const Dialog& dialog) {
    std::vector<std::string> names;
    for (const RegistryRow& row : dialog.registries.registries)
        if (row.built_in)
            names.push_back(row.name);
    if (names.empty())
        return std::string(
            geometry::shown_text(
                "Content from these registries isn't reviewed by the OA team. OA still checks "
                "every package before using it."
            )
        );
    return geometry::filled(
        "Content from registries other than {names} isn't reviewed by the OA team. OA still "
        "checks every package before using it.",
        {{"names", listed(names)}}
    );
}

bool adding_locked(const Dialog& dialog) noexcept {
    // A game locks pathfinding, as it locks Downloads' RESET and EMPTY.
    return dialog.locks.path_search != Lock::none;
}

geometry::ScrolledRows scrolled(const Dialog& dialog) {
    const geometry::Sizes& sized = geometry::sizes_of(dialog);
    geometry::ScrolledRows open{};
    open.area = sized.section_scroll;
    open.rows.bottom = geometry::first_row_top;
    open.content_height = place(dialog, 0).content_height;
    open.limit = kit::ScrollArea::limit_for(open.content_height, sized.view.height);
    open.scroll = offset_of(dialog, open.content_height);
    return open;
}

int32_t scroll_showing(const Dialog& dialog, const geometry::ScrolledRows& open, int32_t control) {
    const std::optional<RowControl> found = row_control(dialog, control);
    if (!found)
        return open.scroll;
    const Placed at_top = place(dialog, 0);
    if (found->index >= at_top.rows.size())
        return open.scroll;
    const PlacedRow& row = at_top.rows[found->index];
    const geometry::Sizes& sized = geometry::sizes_of(dialog);
    kit::ScrollArea area;
    area.view = sized.view;
    area.content_height = open.content_height;
    area.limit = open.limit;
    area.offset = open.scroll;
    return area.offset_showing(row.top, row.height, found->index + 1 == at_top.rows.size());
}

void add_part(kit::DisplayList& part, const Dialog& dialog, const DialogFonts* fonts) {
    const geometry::Sizes& sized = geometry::sizes_of(dialog);
    const SourceRect& view = sized.view;
    const SourceRect& clip = sized.view_clip;
    const geometry::TextWidth small = width_in(fonts, DialogFont::small);
    const geometry::TextWidth regular = width_in(fonts, DialogFont::regular);

    // The heading's words, right of the link that goes back.
    kit::Item heading;
    heading.role = kit::Role::heading;
    heading.rect = heading_rect(dialog);
    heading.look = kit::HeadingLook{std::string(geometry::shown_text(heading_text))};
    part.items.push_back(std::move(heading));
    {
        const SourceRect link = back_rect(dialog);
        const std::string caption(geometry::shown_text(back_text));
        const bool hovered =
            dialog.hovered == registries_back_control || dialog.pressed == registries_back_control;
        kit::Item item;
        item.role = kit::Role::link;
        item.rect = link;
        item.text = caption;
        item.control = registries_back_control;
        item.state.hovered = hovered;
        item.look = kit::LinkLook{caption, hovered, true};
        part.items.push_back(std::move(item));
        if (dialog.focused == registries_back_control) {
            kit::Item ring;
            ring.role = kit::Role::focus_ring;
            ring.rect = link;
            ring.look = kit::FocusRingLook{true};
            part.items.push_back(std::move(ring));
        }
        kit::Control control;
        control.id = registries_back_control;
        control.rect = link;
        control.name = std::string(kNamePrefix) + ".registries.back";
        control.kind = kit::ControlKind::link;
        control.text = caption;
        part.controls.push_back(std::move(control));
        part.tab_order.push_back(registries_back_control);
    }

    const Placed placed = placed_now(dialog);
    const bool locked = adding_locked(dialog);
    for (const PlacedRow& row : placed.rows) {
        const RegistryRow& registry = dialog.registries.registries[row.index];
        const std::string word = word_of(registry);
        const bool gone = removed(dialog, row.index);
        const bool on = switch_on(dialog, row.index);

        kit::Item rule;
        rule.role = kit::Role::rule;
        rule.rect = {sized.content_left, row.top, sized.content_width, 1};
        rule.clip = clip;
        rule.colour = kit::colour::rule;
        part.items.push_back(std::move(rule));

        // The avatar: the name's letters on the accent for a built-in
        // registry, on a quieter face for one the player added.
        kit::Item face;
        face.role = kit::Role::fill;
        face.rect = row.avatar;
        face.clip = clip;
        face.colour = registry.built_in ? kit::colour::accent : kit::colour::switch_idle;
        part.items.push_back(std::move(face));
        add_text(
            part,
            row.avatar,
            initials(registry.name),
            DialogFont::small,
            kit::colour::on_accent,
            kit::Align::centre,
            clip
        );

        const TitleParts title = title_parts(row, registry, regular, small);
        add_text(
            part,
            {row.title.x, row.title.y, title.text.width, row.title.height},
            title.title,
            DialogFont::regular,
            gone ? kit::colour::hint : kit::colour::text,
            kit::Align::left,
            clip
        );
        if (registry.built_in) {
            kit::Item chip;
            chip.role = kit::Role::chip;
            chip.rect = title.chip;
            chip.clip = clip;
            chip.text = std::string(geometry::shown_text(built_in_text));
            chip.look = kit::ChipLook{chip.text, kit::ChipState::installed, false, false, false};
            part.items.push_back(std::move(chip));
        }
        add_text(
            part,
            row.subtitle,
            geometry::cut_text(subtitle_of(dialog, row.index), row.subtitle.width, small),
            DialogFont::small,
            gone ? kit::colour::lock : kit::colour::hint,
            kit::Align::left,
            clip
        );

        // The switch. A row marked for removal shows it locked, and it takes
        // no press until UNDO.
        {
            const int32_t control = registry_switch_control(row.index);
            const bool hovered = !gone && (dialog.hovered == control || dialog.pressed == control);
            kit::SwitchLook look{
                on,
                hovered,
                gone,
                std::string(geometry::shown_text(geometry::off_text)),
                std::string(geometry::shown_text(geometry::on_text)),
            };
            kit::Item item;
            item.role = kit::Role::toggle;
            item.rect = row.toggle;
            item.clip = clip;
            item.text = on ? look.on_caption : look.off_caption;
            item.control = gone ? no_control : control;
            item.state.hovered = hovered;
            item.state.on = on;
            item.state.locked = gone;
            item.look = std::move(look);
            part.items.push_back(std::move(item));
            if (!gone) {
                if (dialog.focused == control) {
                    kit::Item ring;
                    ring.role = kit::Role::focus_ring;
                    ring.rect = row.toggle;
                    ring.clip = clip;
                    ring.look = kit::FocusRingLook{true};
                    part.items.push_back(std::move(ring));
                }
                kit::Control added = part_control(
                    control, row.toggle, "registry." + word, kit::ControlKind::toggle, view
                );
                added.steps = true;
                added.checked = on;
                added.text = registry.name;
                part.controls.push_back(std::move(added));
                part.tab_order.push_back(control);
            }
        }
        if (registry.built_in)
            continue;

        // REMOVE, or UNDO once the row is marked; then ADD MIRROR…, or the
        // lock that stands in its place during a game.
        {
            const int32_t control = registry_remove_control(row.index);
            const std::string caption(geometry::shown_text(gone ? kUndoText : kRemoveText));
            add_button(part, dialog, row.remove, caption, control, clip);
            kit::Control added = part_control(
                control,
                row.remove,
                "registry." + word + (gone ? ".undo" : ".remove"),
                kit::ControlKind::button,
                view
            );
            added.text = caption;
            part.controls.push_back(std::move(added));
            part.tab_order.push_back(control);
        }
        if (locked) {
            add_lock(part, row.lock, clip);
            continue;
        }
        if (gone)
            continue;
        const int32_t control = registry_mirror_control(row.index);
        const std::string caption(geometry::shown_text(kMirrorText));
        add_button(part, dialog, row.mirror, caption, control, clip);
        kit::Control added = part_control(
            control, row.mirror, "registry." + word + ".add-mirror", kit::ControlKind::button, view
        );
        added.text = caption;
        part.controls.push_back(std::move(added));
        part.tab_order.push_back(control);
    }

    for (std::size_t line = 0; line < placed.notice.size(); ++line)
        add_text(
            part,
            placed.notice_lines[line],
            placed.notice[line],
            DialogFont::small,
            kit::colour::lock,
            kit::Align::left,
            clip
        );
    for (std::size_t line = 0; line < placed.note.size(); ++line)
        add_text(
            part,
            placed.note_lines[line],
            placed.note[line],
            DialogFont::small,
            kit::colour::quiet,
            kit::Align::left,
            clip
        );
}

void append_layout(const Dialog& dialog, std::vector<LayoutPart>& parts) {
    const geometry::Sizes& sized = geometry::sizes_of(dialog);
    const geometry::TextWidth measure = width_in(nullptr, DialogFont::small);
    parts.push_back(
        LayoutPart{
            heading_rect(dialog),
            std::string(geometry::shown_text(heading_text)),
            DialogFont::small,
            geometry::heading_tracking,
            no_control,
        }
    );
    parts.push_back(
        LayoutPart{
            back_rect(dialog),
            std::string(geometry::shown_text(back_text)),
            DialogFont::small,
            0,
            registries_back_control,
        }
    );
    const auto keep = [&parts, &sized](LayoutPart part) {
        if (part.rect.width <= 0 || part.rect.height <= 0)
            return;
        const SourceRect& view = sized.view;
        const SourceRect& rect = part.rect;
        if (rect.x >= view.x && rect.y >= view.y && rect.x + rect.width <= view.x + view.width &&
            rect.y + rect.height <= view.y + view.height)
            parts.push_back(std::move(part));
    };
    const Placed placed = placed_now(dialog);
    const bool locked = adding_locked(dialog);
    for (const PlacedRow& row : placed.rows) {
        const RegistryRow& registry = dialog.registries.registries[row.index];
        const bool gone = removed(dialog, row.index);
        keep(LayoutPart{row.avatar, initials(registry.name), DialogFont::small, 0, no_control});
        const TitleParts title = title_parts(row, registry, measure, measure);
        keep(LayoutPart{title.text, title.title, DialogFont::regular, 0, no_control});
        if (registry.built_in)
            keep(
                LayoutPart{
                    title.chip,
                    std::string(geometry::shown_text(built_in_text)),
                    DialogFont::small,
                    0,
                    no_control,
                }
            );
        keep(
            LayoutPart{
                row.subtitle,
                geometry::cut_text(subtitle_of(dialog, row.index), row.subtitle.width, measure),
                DialogFont::small,
                0,
                no_control,
            }
        );
        const int32_t half = (row.toggle.width - 2) / 2;
        const std::array<std::string_view, 2> halves{geometry::off_text, geometry::on_text};
        for (std::size_t side = 0; side < halves.size(); ++side)
            keep(
                LayoutPart{
                    {row.toggle.x + 1 + static_cast<int32_t>(side) * half,
                     row.toggle.y + 1,
                     half,
                     row.toggle.height - 2},
                    std::string(geometry::shown_text(halves[side])),
                    DialogFont::small,
                    0,
                    gone ? no_control : registry_switch_control(row.index),
                }
            );
        if (registry.built_in)
            continue;
        keep(
            LayoutPart{
                row.remove,
                std::string(geometry::shown_text(gone ? kUndoText : kRemoveText)),
                DialogFont::small,
                0,
                registry_remove_control(row.index),
            }
        );
        if (locked) {
            const SourceRect& area = row.lock;
            keep(
                LayoutPart{
                    {area.x, area.y, geometry::padlock_width, area.height},
                    {},
                    DialogFont::regular,
                    0,
                    no_control,
                }
            );
            const int32_t text_left = area.x + geometry::padlock_width + geometry::padlock_gap;
            keep(
                LayoutPart{
                    {text_left, area.y, area.x + area.width - text_left, area.height},
                    std::string(geometry::shown_text(game_lock_text)),
                    DialogFont::small,
                    0,
                    no_control,
                }
            );
        } else if (!gone) {
            keep(
                LayoutPart{
                    row.mirror,
                    std::string(geometry::shown_text(kMirrorText)),
                    DialogFont::small,
                    0,
                    registry_mirror_control(row.index),
                }
            );
        }
    }
    for (std::size_t line = 0; line < placed.notice.size(); ++line)
        keep(
            LayoutPart{
                placed.notice_lines[line], placed.notice[line], DialogFont::small, 0, no_control
            }
        );
    for (std::size_t line = 0; line < placed.note.size(); ++line)
        keep(
            LayoutPart{placed.note_lines[line], placed.note[line], DialogFont::small, 0, no_control}
        );
}

SourceRect footer_lock(const Dialog& dialog) noexcept {
    const geometry::Sizes& sized = geometry::sizes_of(dialog);
    const SourceRect& restore = sized.restore_button;
    return {
        restore.x,
        restore.y,
        sized.cancel_button.x - geometry::label_gap - restore.x,
        restore.height,
    };
}

std::optional<DialogAction> activate(Dialog& dialog, int32_t control) {
    if (!shown(dialog))
        return std::nullopt;
    if (control == add_registry_control)
        return adding_locked(dialog) ? DialogAction::none : DialogAction::add_registry;
    if (control == registries_back_control)
        return back(dialog);
    const std::optional<RowControl> found = row_control(dialog, control);
    if (!found)
        return std::nullopt;
    const RegistryRow& registry = dialog.registries.registries[found->index];
    RegistryChoice& choice = chosen(dialog, found->index);
    switch (found->part) {
    case 0:
        if (choice.removed)
            return DialogAction::none;
        choice.on = !choice.on;
        return DialogAction::redraw;
    case 1:
        if (registry.built_in)
            return DialogAction::none;
        choice.removed = !choice.removed;
        return DialogAction::redraw;
    default:
        if (registry.built_in || choice.removed || adding_locked(dialog))
            return DialogAction::none;
        dialog.mirror_request = registry.id;
        return DialogAction::add_mirror;
    }
}

std::optional<DialogAction> release(Dialog& dialog, int32_t control, int32_t x, int32_t y) {
    static_cast<void>(y);
    if (!shown(dialog))
        return std::nullopt;
    const std::optional<RowControl> found = row_control(dialog, control);
    if (!found || found->part != 0)
        return activate(dialog, control);
    const Placed placed = placed_now(dialog);
    if (found->index >= placed.rows.size())
        return DialogAction::none;
    RegistryChoice& choice = chosen(dialog, found->index);
    if (choice.removed)
        return DialogAction::none;
    const SourceRect& toggle = placed.rows[found->index].toggle;
    choice.on = x >= toggle.x + toggle.width / 2;
    return DialogAction::redraw;
}

std::optional<DialogAction> step(Dialog& dialog, int32_t control, bool up) {
    if (!shown(dialog))
        return std::nullopt;
    const std::optional<RowControl> found = row_control(dialog, control);
    if (!found || found->part != 0)
        return std::nullopt;
    RegistryChoice& choice = chosen(dialog, found->index);
    if (choice.removed || choice.on == up)
        return DialogAction::none;
    choice.on = up;
    return DialogAction::redraw;
}

} // namespace oa::ui::engine_settings::registries

namespace oa::ui::engine_settings {

void set_registries_offer(Dialog& dialog, const RegistriesOffer& offer) {
    std::vector<RegistryChoice> chosen;
    std::vector<RegistryChoice> opened;
    chosen.reserve(offer.registries.size());
    opened.reserve(offer.registries.size());
    const auto find = [](const std::vector<RegistryChoice>& choices, const std::string& id) {
        return std::find_if(choices.begin(), choices.end(), [&id](const RegistryChoice& choice) {
            return choice.id == id;
        });
    };
    for (const RegistryRow& row : offer.registries) {
        const auto was_chosen = find(dialog.registry_chosen, row.id);
        const auto was_opened = find(dialog.registry_opened, row.id);
        chosen.push_back(
            was_chosen != dialog.registry_chosen.end() ? *was_chosen
                                                       : RegistryChoice{row.id, row.on, false}
        );
        opened.push_back(
            was_opened != dialog.registry_opened.end() ? *was_opened
                                                       : RegistryChoice{row.id, row.on, false}
        );
    }
    dialog.registries = offer;
    dialog.registry_chosen = std::move(chosen);
    dialog.registry_opened = std::move(opened);
}

std::vector<RegistryChange> registry_changes(const Dialog& dialog) {
    std::vector<RegistryChange> changes;
    const std::vector<RegistryRow>& rows = dialog.registries.registries;
    for (std::size_t index = 0; index < rows.size() && index < dialog.registry_chosen.size();
         ++index) {
        const RegistryChoice& choice = dialog.registry_chosen[index];
        const bool opened_on = index < dialog.registry_opened.size()
                                   ? dialog.registry_opened[index].on
                                   : rows[index].on;
        if (choice.removed && !rows[index].built_in) {
            changes.push_back(RegistryChange{rows[index].id, RegistryChangeKind::remove});
            continue;
        }
        if (choice.on != opened_on)
            changes.push_back(
                RegistryChange{
                    rows[index].id,
                    choice.on ? RegistryChangeKind::turn_on : RegistryChangeKind::turn_off,
                }
            );
    }
    return changes;
}

} // namespace oa::ui::engine_settings
