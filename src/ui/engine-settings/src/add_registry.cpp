// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Add registry window's words, its placement at each size class, and
// what a pointer, a finger, a key and typed text do to it. The window is a
// notice's chrome: the class's notice width and height limits, and the
// Compact metrics for the text, the field, the buttons and the footer, as a
// notice uses them. Its buttons' places hang on its height alone, so a
// press lands where a button is drawn.

#include "oa/ui/engine_settings/add_registry.hpp"

#include "oa/formats/url.hpp"
#include "oa/ui/kit/chrome.hpp"
#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/components_more.hpp"
#include "oa/ui/kit/controls.hpp"
#include "oa/ui/kit/looks.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/kit/theme.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::engine_settings {

namespace {

namespace kit = oa::ui::kit;

/// The Compact metrics the text, the field, the buttons and the footer are
/// placed by. The width and the height limits come from the size class.
constexpr const kit::Metrics& metrics = kit::compact_metrics;

/// The columns a fact's label keeps before its value.
constexpr int32_t kLabelWidth = 76;
/// The rows between the name and the facts under it.
constexpr int32_t kNameGap = 4;

/// The window's words, in English.
constexpr std::string_view kAddTitle = "Add registry";
constexpr std::string_view kMirrorTitle = "Add mirror";
constexpr std::string_view kPlaceholder = "http://…";
constexpr std::string_view kCancelCaption = "CANCEL";
constexpr std::string_view kCheckCaption = "CHECK";
constexpr std::string_view kAddCaption = "ADD";
constexpr std::string_view kAddMirrorCaption = "ADD MIRROR";
constexpr std::string_view kUnsignedText = "Unsigned · accepted in Developer mode only";
constexpr std::string_view kWarningText =
    "Not reviewed by the OA team. Only add registries you trust. OA checks every package it "
    "downloads.";

/// One line of the window's text, placed.
struct Line {
    std::string text{};                       ///< the words, as shown
    kit::Rect rect{};                         ///< where they are drawn
    kit::FontRole font{kit::FontRole::small}; ///< which font
    kit::Colour colour{kit::colour::text};    ///< their colour
    bool heading{};                           ///< drawn in the heading role
};

/// Which label a line belongs to.
enum class Label : uint8_t {
    none,    ///< no label
    name,    ///< add-registry.name
    address, ///< add-registry.address
    key,     ///< add-registry.key
    refusal, ///< add-registry.refusal
};

/// A line and the label it belongs to.
struct LabelledLine {
    Line line{};              ///< the line
    Label label{Label::none}; ///< its label
};

/// One button, placed.
struct PlacedButton {
    kit::ControlId id{kit::no_control}; ///< its number
    std::string caption{};              ///< the caption
    std::string name{};                 ///< its automation name
    bool accent{};                      ///< drawn as the accent button
    kit::Rect rect{};                   ///< where it lies
};

/// The window, placed.
struct Placed {
    int32_t width{};                   ///< the window's width
    int32_t height{};                  ///< its height
    int32_t footer_rule{};             ///< the row of the line over the footer
    std::optional<kit::Rect> field{};  ///< the address field; none for a file
    std::vector<LabelledLine> lines;   ///< the text that fits, top to bottom
    std::vector<PlacedButton> buttons; ///< the footer's buttons, left to right
};

/// Returns a text's width in a font, or its estimated width without fonts.
///
/// @param fonts the fonts; null to estimate
/// @param role which font
/// @return the measure
kit::Measure measure_of(const kit::Fonts* fonts, kit::FontRole role) {
    if (fonts == nullptr)
        return kit::estimated_width;
    return [fonts, role](std::string_view text) { return kit::text_width(*fonts, role, text); };
}

/// Fills a text's {name} places.
///
/// @param text the text, in English
/// @param place the place's name
/// @param value its value
/// @return the text
std::string filled(std::string_view text, std::string_view place, std::string_view value) {
    std::string out(text);
    const std::string marker = "{" + std::string(place) + "}";
    const std::size_t at = out.find(marker);
    if (at != std::string::npos)
        out.replace(at, marker.size(), value);
    return out;
}

/// Returns the window's title.
///
/// @param window the window
/// @return "Add registry", or "Add mirror" for a mirror
std::string title_of(const AddRegistry& window) {
    return std::string(window.mode == AddRegistry::Mode::mirror ? kMirrorTitle : kAddTitle);
}

/// Returns the Packages line's value: the count, and the kinds joined.
///
/// @param shown what the check found
/// @return such as "31 · maps" or "4 · mods, maps"
std::string packages_value(const ProbeView& shown) {
    std::string text = std::to_string(shown.packages);
    if (shown.kinds.empty())
        return text;
    text += " · ";
    for (std::size_t index = 0; index < shown.kinds.size(); ++index) {
        if (index > 0)
            text += ", ";
        text += shown.kinds[index];
    }
    return text;
}

/// Returns the fingerprint lines: at most shown_fingerprints, then "+N more".
///
/// @param fingerprints the keys' fingerprints
/// @return the lines
std::vector<std::string> fingerprint_lines(const std::vector<std::string>& fingerprints) {
    std::vector<std::string> lines;
    for (std::size_t index = 0; index < fingerprints.size() && index < shown_fingerprints; ++index)
        lines.push_back(fingerprints[index]);
    if (fingerprints.size() > shown_fingerprints)
        lines.push_back("+" + std::to_string(fingerprints.size() - shown_fingerprints) + " more");
    return lines;
}

/// Returns the row of the line over the footer.
///
/// @param height the window's height
/// @return the row
int32_t footer_rule_of(int32_t height) noexcept {
    return height - metrics.edge - metrics.footer_height - 1;
}

/// Returns the top row of the footer's buttons.
///
/// @param height the window's height
/// @return the row
int32_t button_top_of(int32_t height) noexcept {
    return footer_rule_of(height) + 1 + (metrics.footer_height - metrics.button_height) / 2;
}

/// Tells whether a rectangle sits above the footer with the gap the text keeps.
///
/// @param rect the rectangle
/// @param footer_rule the row of the line over the footer
/// @return true when it fits
bool fits_over(const kit::Rect& rect, int32_t footer_rule) noexcept {
    return rect.y + rect.height <= footer_rule - metrics.text_bottom_gap / 2;
}

/// Returns the buttons a phase offers, left to right, before they are placed.
///
/// @param window the window
/// @return the buttons
std::vector<PlacedButton> buttons_of(const AddRegistry& window) {
    std::vector<PlacedButton> buttons;
    buttons.push_back(
        {add_registry_cancel_control, std::string(kCancelCaption), "add-registry.cancel", false, {}}
    );
    if (add_registry_editable(window))
        buttons.push_back(
            {add_registry_check_control, std::string(kCheckCaption), "add-registry.check", true, {}}
        );
    else if (window.phase == AddRegistry::Phase::ready)
        buttons.push_back(
            {add_registry_add_control,
             std::string(
                 window.mode == AddRegistry::Mode::mirror ? kAddMirrorCaption : kAddCaption
             ),
             "add-registry.add",
             true,
             {}}
        );
    return buttons;
}

/// Places the window: its text from the top, its height from that, and its
/// buttons right-aligned in the footer.
///
/// @param window the window
/// @param fonts the fonts; null estimates each character
/// @return the placed window
Placed place(const AddRegistry& window, const kit::Fonts* fonts) {
    Placed placed;
    placed.width = add_registry_width(window.size_class);
    const int32_t line_width = placed.width - 2 * metrics.padding;
    const kit::Measure small = measure_of(fonts, kit::FontRole::small);
    int32_t row = metrics.text_top;
    std::vector<LabelledLine> lines;

    const auto text_line = [&](std::string text, kit::Colour colour, Label label) {
        Line line;
        line.text = kit::fit(text, line_width, small);
        line.rect = {metrics.padding, row, line_width, metrics.small_line};
        line.colour = colour;
        lines.push_back({std::move(line), label});
        row += metrics.small_line;
    };
    const auto wrapped = [&](std::string_view text, kit::Colour colour, Label label) {
        for (const std::string& part : kit::wrap(text, line_width, small))
            text_line(part, colour, label);
    };
    const auto heading_line = [&](std::string text, Label label) {
        Line line;
        line.text = kit::fit(text, line_width, small);
        line.rect = {metrics.padding, row, line_width, metrics.small_line};
        line.heading = true;
        lines.push_back({std::move(line), label});
        row += metrics.small_line;
    };
    // A fact: its label at the left, in the hint colour, and its value after it.
    const auto fact = [&](std::string_view label_text, std::string value, Label label) {
        if (!label_text.empty()) {
            Line name;
            name.text = std::string(label_text);
            name.rect = {metrics.padding, row, kLabelWidth, metrics.small_line};
            name.colour = kit::colour::hint;
            lines.push_back({std::move(name), label});
        }
        const int32_t value_left = metrics.padding + kLabelWidth;
        Line shown;
        shown.text = kit::fit(value, line_width - kLabelWidth, small);
        shown.rect = {value_left, row, line_width - kLabelWidth, metrics.small_line};
        lines.push_back({std::move(shown), label});
        row += metrics.small_line;
    };

    const bool mirror = window.mode == AddRegistry::Mode::mirror;
    if (mirror) {
        heading_line(filled("Mirror of {name}", "name", window.mirror_of), Label::name);
        row += metrics.paragraph_gap;
    }
    if (window.mode == AddRegistry::Mode::from_file) {
        text_line(filled("From {file}", "file", window.file_name), kit::colour::text, Label::none);
    } else {
        placed.field = kit::Rect{metrics.padding, row, line_width, metrics.field_height};
        row += metrics.field_height;
    }
    row += metrics.paragraph_gap;

    switch (window.phase) {
    case AddRegistry::Phase::editing:
        break;
    case AddRegistry::Phase::checking:
        text_line(
            filled("Checking {address}…", "address", window.checking),
            kit::colour::text,
            Label::none
        );
        break;
    case AddRegistry::Phase::ready: {
        const ProbeView& shown = window.shown;
        if (!mirror) {
            heading_line(shown.name, Label::name);
            row += kNameGap;
        }
        fact("Address", shown.address, Label::address);
        if (mirror) {
            row += metrics.paragraph_gap;
            wrapped(
                filled("Checked against the keys you trust for {name}", "name", window.mirror_of),
                kit::colour::text,
                Label::none
            );
            for (const std::string& key : fingerprint_lines(shown.fingerprints))
                text_line(key, kit::colour::text, Label::key);
            break;
        }
        fact("Packages", packages_value(shown), Label::none);
        if (shown.is_signed) {
            const std::vector<std::string> keys = fingerprint_lines(shown.fingerprints);
            for (std::size_t index = 0; index < keys.size(); ++index)
                fact(
                    index == 0 ? (shown.fingerprints.size() > 1 ? "Keys" : "Key") : "",
                    keys[index],
                    Label::key
                );
        } else {
            text_line(std::string(kUnsignedText), kit::colour::lock, Label::key);
        }
        fact("Downloads", shown.downloads, Label::none);
        row += metrics.paragraph_gap;
        wrapped(kWarningText, kit::colour::lock, Label::none);
        break;
    }
    case AddRegistry::Phase::refused:
        wrapped(window.shown.refusal, kit::colour::lock, Label::refusal);
        break;
    }

    const kit::Metrics& sized = kit::metrics_of(window.size_class);
    placed.height = std::clamp(
        row + metrics.text_bottom_gap + 1 + metrics.footer_height + metrics.edge,
        sized.least_notice_height,
        sized.greatest_notice_height
    );
    placed.footer_rule = footer_rule_of(placed.height);
    for (LabelledLine& line : lines)
        if (fits_over(line.line.rect, placed.footer_rule))
            placed.lines.push_back(std::move(line));
    if (placed.field && !fits_over(*placed.field, placed.footer_rule))
        placed.field.reset();

    placed.buttons = buttons_of(window);
    int32_t right = placed.width - metrics.padding;
    for (std::size_t index = placed.buttons.size(); index-- > 0;) {
        PlacedButton& button = placed.buttons[index];
        const int32_t width = std::max(
            metrics.button_width,
            kit::estimated_width(button.caption) + metrics.prompt_button_padding
        );
        button.rect = {right - width, button_top_of(placed.height), width, metrics.button_height};
        right -= width + metrics.button_gap;
    }
    return placed;
}

/// Returns what pressing a control asks.
///
/// @param window the window
/// @param id the control
/// @return its action, or none when it is not a button
AddRegistryAction action_for(const AddRegistry& window, kit::ControlId id) noexcept {
    if (id == add_registry_cancel_control)
        return AddRegistryAction::cancel;
    if (id == add_registry_check_control && add_registry_editable(window))
        return AddRegistryAction::check;
    if (id == add_registry_add_control && window.phase == AddRegistry::Phase::ready)
        return AddRegistryAction::add;
    return AddRegistryAction::none;
}

/// Returns what one pointer event asks.
///
/// @param[in,out] window the window; a release on the field focuses it
/// @param outcome what the kit's pointer did
/// @return the control's action, redraw, or none
AddRegistryAction from_pointer(AddRegistry& window, const kit::PointerOutcome& outcome) noexcept {
    if (outcome.result == kit::PointerResult::activated) {
        if (outcome.control == add_registry_url_control) {
            window.interaction.focused = add_registry_url_control;
            window.interaction.focus_shown = true;
            return AddRegistryAction::start_typing;
        }
        return action_for(window, outcome.control);
    }
    if (outcome.result == kit::PointerResult::redraw)
        return AddRegistryAction::redraw;
    return AddRegistryAction::none;
}

/// Appends a label automation reads and a press does not reach, over the
/// lines that belong to it.
///
/// @param[in,out] list the display list
/// @param placed the placed window
/// @param label which label
/// @param id its number
/// @param name its automation name
void add_label(
    kit::DisplayList& list,
    const Placed& placed,
    Label label,
    kit::ControlId id,
    std::string_view name
) {
    std::optional<kit::Rect> area;
    std::string text;
    int32_t last_row = -1;
    for (const LabelledLine& line : placed.lines) {
        if (line.label != label || line.line.text.empty())
            continue;
        const kit::Rect& rect = line.line.rect;
        if (!area) {
            area = rect;
        } else {
            const int32_t left = std::min(area->x, rect.x);
            const int32_t top = std::min(area->y, rect.y);
            const int32_t right = std::max(area->x + area->width, rect.x + rect.width);
            const int32_t bottom = std::max(area->y + area->height, rect.y + rect.height);
            area = kit::Rect{left, top, right - left, bottom - top};
        }
        // A fact's label and value share a row; the next row starts a line.
        if (!text.empty())
            text += line.line.rect.y == last_row ? " " : "\n";
        text += line.line.text;
        last_row = line.line.rect.y;
    }
    if (!area)
        return;
    kit::Control control;
    control.id = id;
    control.rect = *area;
    control.enabled = false;
    control.name = std::string(name);
    control.kind = kit::ControlKind::area;
    control.focusable = false;
    control.text = std::move(text);
    list.controls.push_back(std::move(control));
}

} // namespace

int32_t add_registry_width(kit::SizeClass size_class) noexcept {
    return kit::metrics_of(size_class).notice_width;
}

int32_t add_registry_height(const AddRegistry& window, const kit::Fonts* fonts) {
    return place(window, fonts).height;
}

bool add_registry_editable(const AddRegistry& window) noexcept {
    return window.mode != AddRegistry::Mode::from_file &&
           (window.phase == AddRegistry::Phase::editing ||
            window.phase == AddRegistry::Phase::refused);
}

std::optional<kit::Rect> add_registry_field(const AddRegistry& window) noexcept {
    if (!add_registry_editable(window) || !window.interaction.focus_shown ||
        window.interaction.focused != add_registry_url_control)
        return std::nullopt;
    int32_t row = metrics.text_top;
    if (window.mode == AddRegistry::Mode::mirror)
        row += metrics.small_line + metrics.paragraph_gap;
    const int32_t width = add_registry_width(window.size_class);
    return kit::Rect{metrics.padding, row, width - 2 * metrics.padding, metrics.field_height};
}

kit::DisplayList add_registry_list(const AddRegistry& window, const kit::Fonts* fonts) {
    const Placed placed = place(window, fonts);
    const int32_t inner_width = placed.width - 2 * metrics.edge;
    kit::DisplayList list;

    kit::Item face;
    face.role = kit::Role::fill;
    face.rect = {0, 0, placed.width, placed.height};
    face.colour = kit::colour::panel;
    list.items.push_back(std::move(face));

    const std::string title = title_of(window);
    kit::HeaderLook header{};
    header.width = placed.width;
    header.title = title;
    header.title_width = placed.width - metrics.padding -
                         (kit::notice_mark.x + kit::notice_mark.width + metrics.header_gap);
    header.tracking = metrics.heading_tracking;
    kit::Item header_item;
    header_item.role = kit::Role::header;
    header_item.rect = {metrics.edge, metrics.edge, inner_width, metrics.header_height};
    header_item.text = title;
    header_item.look = header;
    list.items.push_back(std::move(header_item));

    const kit::Interaction& interaction = window.interaction;
    const bool editable = add_registry_editable(window);
    if (placed.field) {
        const bool focused =
            editable && interaction.focus_shown && interaction.focused == add_registry_url_control;
        kit::SearchLook look;
        look.placeholder = std::string(kPlaceholder);
        look.field.text = window.url;
        look.field.caret = std::min(window.caret, window.url.size());
        look.focused = focused;
        look.hovered = editable && interaction.hovered == add_registry_url_control;
        look.magnifier = false;
        kit::Item item;
        item.role = kit::Role::field;
        item.rect = *placed.field;
        item.control = add_registry_url_control;
        item.text = window.url;
        item.state.hovered = look.hovered;
        item.state.focused = focused;
        item.state.disabled = !editable;
        item.look = std::move(look);
        list.items.push_back(std::move(item));
        kit::Control control;
        control.id = add_registry_url_control;
        control.rect = *placed.field;
        control.enabled = editable;
        control.focusable = editable;
        control.steps = true;
        control.name = "add-registry.url";
        control.kind = kit::ControlKind::text_field;
        control.text = window.url;
        list.controls.push_back(std::move(control));
        if (editable)
            list.tab_order.push_back(add_registry_url_control);
    }

    for (const LabelledLine& labelled : placed.lines) {
        const Line& line = labelled.line;
        kit::Item item;
        item.rect = line.rect;
        item.text = line.text;
        item.font = line.font;
        if (line.heading) {
            item.role = kit::Role::heading;
            item.look = kit::HeadingLook{line.text};
        } else {
            item.role = kit::Role::text;
            item.align = kit::Align::left;
            item.colour = line.colour;
        }
        list.items.push_back(std::move(item));
    }

    kit::Item footer;
    footer.role = kit::Role::footer_band;
    footer.rect = {metrics.edge, placed.footer_rule, inner_width, 1 + metrics.footer_height};
    footer.look = kit::FooterBandLook{placed.width, placed.footer_rule};
    list.items.push_back(std::move(footer));

    for (const PlacedButton& button : placed.buttons) {
        const bool marked = interaction.focus_shown && interaction.focused == button.id;
        const bool hovered = interaction.hovered == button.id;
        const bool held = interaction.pressed == button.id && hovered;
        kit::ButtonLook look{};
        look.caption = button.caption;
        look.style = button.accent ? kit::ButtonStyle::accent : kit::ButtonStyle::plain;
        look.hovered = hovered;
        look.held = held;
        look.enabled = true;
        kit::Item item;
        item.role = kit::Role::button;
        item.rect = button.rect;
        item.control = button.id;
        item.text = button.caption;
        item.state.hovered = hovered;
        item.state.pressed = held;
        item.state.focused = marked;
        item.look = std::move(look);
        list.items.push_back(std::move(item));
        if (marked)
            kit::add_focus_ring(list, button.rect, true);
        kit::Control control;
        control.id = button.id;
        control.rect = button.rect;
        control.name = button.name;
        control.kind = kit::ControlKind::button;
        control.text = button.caption;
        list.controls.push_back(std::move(control));
        list.tab_order.push_back(button.id);
    }

    add_label(list, placed, Label::name, add_registry_name_control, "add-registry.name");
    add_label(list, placed, Label::address, add_registry_address_control, "add-registry.address");
    add_label(list, placed, Label::key, add_registry_key_control, "add-registry.key");
    add_label(list, placed, Label::refusal, add_registry_refusal_control, "add-registry.refusal");

    kit::Item edge;
    edge.role = kit::Role::bevel;
    edge.rect = {0, 0, placed.width, placed.height};
    edge.colour = kit::colour::edge_light;
    list.items.push_back(std::move(edge));
    return list;
}

void add_registry_settle(AddRegistry& window) noexcept {
    window.interaction.focused =
        add_registry_editable(window) ? add_registry_url_control : add_registry_cancel_control;
    window.interaction.focus_shown = true;
    window.interaction.hovered = kit::no_control;
    window.interaction.pressed = kit::no_control;
    window.interaction.finger_shift = {};
}

AddRegistryAction add_registry_text(AddRegistry& window, std::string_view utf8) {
    if (!add_registry_editable(window))
        return AddRegistryAction::none;
    const std::size_t line_break = utf8.find_first_of("\r\n");
    if (line_break != std::string_view::npos)
        utf8 = utf8.substr(0, line_break);
    const std::size_t room = oa::formats::url::max_url_bytes > window.url.size()
                                 ? oa::formats::url::max_url_bytes - window.url.size()
                                 : 0;
    if (utf8.size() > room) {
        std::size_t cut = room;
        while (cut > 0 && kit::continuation_byte(utf8[cut]))
            --cut;
        utf8 = utf8.substr(0, cut);
    }
    kit::TextField field{window.url, std::min(window.caret, window.url.size())};
    if (!kit::insert_text(field, utf8))
        return AddRegistryAction::none;
    window.url = std::move(field.text);
    window.caret = field.caret;
    if (window.phase == AddRegistry::Phase::refused) {
        window.phase = AddRegistry::Phase::editing;
        window.shown = {};
    }
    return AddRegistryAction::redraw;
}

AddRegistryAction add_registry_key(AddRegistry& window, kit::Key pressed, const kit::Fonts* fonts) {
    const kit::DisplayList list = add_registry_list(window, fonts);
    const kit::KeyOutcome outcome = kit::key(window.interaction, list, pressed);
    switch (outcome.result) {
    case kit::KeyResult::none:
    case kit::KeyResult::scroll:
        return AddRegistryAction::none;
    case kit::KeyResult::redraw:
        return AddRegistryAction::redraw;
    case kit::KeyResult::cancel:
        return AddRegistryAction::cancel;
    case kit::KeyResult::accept:
        return add_registry_editable(window) ? AddRegistryAction::check : AddRegistryAction::none;
    case kit::KeyResult::to_control:
        break;
    }
    if (outcome.control != add_registry_url_control)
        return action_for(window, outcome.control);
    if (pressed == kit::Key::space)
        return AddRegistryAction::start_typing;
    kit::TextField field{window.url, std::min(window.caret, window.url.size())};
    if (!kit::edit_text(field, pressed))
        return AddRegistryAction::none;
    const bool changed = field.text != window.url;
    window.url = std::move(field.text);
    window.caret = field.caret;
    if (changed && window.phase == AddRegistry::Phase::refused) {
        window.phase = AddRegistry::Phase::editing;
        window.shown = {};
    }
    return AddRegistryAction::redraw;
}

AddRegistryAction
add_registry_pointer_move(AddRegistry& window, kit::Point at, const kit::Fonts* fonts) {
    const kit::DisplayList list = add_registry_list(window, fonts);
    return from_pointer(window, kit::pointer_move(window.interaction, list, at));
}

AddRegistryAction
add_registry_pointer_down(AddRegistry& window, kit::Point at, const kit::Fonts* fonts) {
    const kit::DisplayList list = add_registry_list(window, fonts);
    return from_pointer(window, kit::pointer_down(window.interaction, list, at));
}

AddRegistryAction add_registry_finger_down(
    AddRegistry& window, kit::Point at, int32_t reach, const kit::Fonts* fonts
) {
    const kit::DisplayList list = add_registry_list(window, fonts);
    return from_pointer(window, kit::finger_down(window.interaction, list, at, reach));
}

AddRegistryAction
add_registry_pointer_up(AddRegistry& window, kit::Point at, const kit::Fonts* fonts) {
    const kit::DisplayList list = add_registry_list(window, fonts);
    return from_pointer(window, kit::pointer_up(window.interaction, list, at));
}

void draw_add_registry(const kit::Canvas& canvas, const AddRegistry& window) {
    kit::paint(canvas, add_registry_list(window, canvas.fonts));
}

} // namespace oa::ui::engine_settings
