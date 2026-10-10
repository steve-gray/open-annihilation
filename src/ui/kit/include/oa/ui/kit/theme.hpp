// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Provisional: this header changes whenever OA's screens need it to, until the kit is declared stable (src/ui/kit/README.md).

// The OA UI kit's colours and Compact metrics. One point is one pixel of the
// 640 by 480 picture. Regular and Large metrics are not here yet.
#pragma once

#include <array>
#include <cstdint>

namespace oa::ui::kit {

/// A colour: red, green, blue and opacity, each 0 to 255.
struct Colour {
    uint8_t r{};    ///< red
    uint8_t g{};    ///< green
    uint8_t b{};    ///< blue
    uint8_t a{255}; ///< opacity; 255 is opaque
    friend constexpr bool operator==(Colour, Colour) = default;
};

/// Returns a colour as the artless painter takes one, its opacity dropped.
///
/// @param colour the colour
/// @return red, green and blue
[[nodiscard]] constexpr std::array<uint8_t, 3> rgb(Colour colour) noexcept {
    return {colour.r, colour.g, colour.b};
}

/// The settings dialog, its notices, its prompts and the OA button.
namespace colour {

/// The panel's face.
inline constexpr Colour panel{0x1b, 0x1e, 0x19};
/// The header's and the footer's face.
inline constexpr Colour band{0x14, 0x16, 0x12};
/// The section list's face.
inline constexpr Colour list{0x17, 0x1a, 0x15};
/// The selected section's entry.
inline constexpr Colour list_selected{0x26, 0x2b, 0x21};
/// An entry, or a footer button, under the pointer or held.
inline constexpr Colour hover{0x20, 0x24, 0x1c};
/// The hairline rules between parts.
inline constexpr Colour rule{0x2b, 0x30, 0x27};
/// The raised edges' light side, top and left.
inline constexpr Colour edge_light{0x4a, 0x51, 0x43};
/// The raised edges' dark side, bottom and right.
inline constexpr Colour edge_dark{0x08, 0x09, 0x07};
/// Labels, values and the title.
inline constexpr Colour text{0xe7, 0xe8, 0xdf};
/// Hints and the title's last word.
inline constexpr Colour hint{0x9a, 0xa1, 0x90};
/// The section heading, the version and a hack's id under its title.
inline constexpr Colour quiet{0x8a, 0x91, 0x80};
/// The list's entries that are not selected.
inline constexpr Colour list_text{0xa3, 0xaa, 0x98};
/// The accent: what is selected, On, the slider's filled track and OK.
inline constexpr Colour accent{0x9c, 0xcc, 0x3c};
/// The accent's lighter edge, and the accent under the pointer.
inline constexpr Colour accent_light{0xb6, 0xe0, 0x5a};
/// The accent while it is held.
inline constexpr Colour accent_held{0x8a, 0xb8, 0x30};
/// Text on the accent.
inline constexpr Colour on_accent{0x10, 0x12, 0x0d};
/// The well of a switch, a level strip and a slider's track.
inline constexpr Colour well{0x12, 0x14, 0x10};
/// The border of a switch, a level strip, a slider's track and a footer button.
inline constexpr Colour control_border{0x3a, 0x40, 0x34};
/// That border under the pointer.
inline constexpr Colour control_hover{0x5b, 0x63, 0x52};
/// A switch's selected Off half.
inline constexpr Colour off_selected{0x2c, 0x32, 0x26};
/// A switch's caption that is not selected.
inline constexpr Colour switch_idle{0x7d, 0x84, 0x74};
/// The footer buttons' and the levels' captions.
inline constexpr Colour button_text{0xc9, 0xcd, 0xbf};
/// Locks: the padlock, the lock's text and the shared game's note.
inline constexpr Colour lock{0xe0, 0xb0, 0x4f};
/// The colour the screen under the dialog is darkened with.
inline constexpr Colour backdrop{5, 6, 4};

} // namespace colour

/// The Game files screen and the folder chooser.
namespace screen_colour {

/// The screen behind everything.
inline constexpr Colour background = colour::panel;
/// Cards, rows and sheets.
inline constexpr Colour panel{0x23, 0x27, 0x21};
/// Outlines and dividers.
inline constexpr Colour line{0x3a, 0x40, 0x37};
/// Text.
inline constexpr Colour text{0xe8, 0xea, 0xe4};
/// Secondary text.
inline constexpr Colour dim{0x9a, 0xa0, 0x94};
/// The main button, found and done.
inline constexpr Colour green = colour::accent;
/// Warnings.
inline constexpr Colour amber{0xe8, 0xb4, 0x4c};
/// Problems and removals.
inline constexpr Colour red{0xe0, 0x6c, 0x5c};
/// The main button's text, dark ink on the green fill.
inline constexpr Colour ink = colour::on_accent;
/// A plain button's text.
inline constexpr Colour button_text = colour::button_text;

} // namespace screen_colour

/// The touch controls.
namespace hud_colour {

/// Panels and buttons: gunmetal at 85% opacity.
inline constexpr Colour panel{0x1b, 0x1e, 0x19, 217};
/// Sheets and the radial's ring: darker and nearly opaque.
inline constexpr Colour sheet{0x14, 0x16, 0x12, 248};
/// An empty cell of the drawer or the MORE sheet.
inline constexpr Colour cell{0x24, 0x28, 0x20, 230};
/// A control a finger rests on: a lighter gunmetal.
inline constexpr Colour pressed{0x3b, 0x42, 0x35, 240};
/// The hairline round panels and buttons.
inline constexpr Colour edge{0xff, 0xff, 0xff, 34};
/// Lit, latched and armed controls.
inline constexpr Colour lit{0x9c, 0xcc, 0x3c};
/// A lit control a finger rests on.
inline constexpr Colour lit_pressed{0xb8, 0xde, 0x66};
/// Labels and icons on a lit control.
inline constexpr Colour ink{0x1b, 0x1e, 0x19};
/// Labels and icons.
inline constexpr Colour label{0xf2, 0xf4, 0xee};
/// The quieter text of the drawer's caption and page dots.
inline constexpr Colour quiet{0xb4, 0xb9, 0xae};
/// SELF-DESTRUCT · HOLD's red: its edge, and its fill as the hold fills it.
inline constexpr Colour danger{0xc0, 0x39, 0x2b};
/// SELF-DESTRUCT · HOLD's panel before its hold fills it.
inline constexpr Colour danger_panel{0x2a, 0x15, 0x12, 235};
/// SELF-DESTRUCT · HOLD's label before its hold fills it.
inline constexpr Colour danger_label{0xf0, 0x8c, 0x80};
/// The lines between the radial's wedges.
inline constexpr Colour wedge_gap{0x08, 0x09, 0x07};
/// Greyed items show at this opacity.
inline constexpr float greyed_opacity = 0.4F;

} // namespace hud_colour

/// How far the main menu is darkened under the dialog, in 256ths.
inline constexpr uint32_t menu_backdrop_opacity = 159;
/// How far the in-game menu's column is darkened beside the dialog, in 256ths.
inline constexpr uint32_t ingame_backdrop_opacity = 128;
/// How far a locked row is faded into the panel, in 256ths.
inline constexpr uint32_t locked_fade = 115;

/// The Compact metrics, in points: the settings dialog's sizes at 0.7.3.
struct Metrics {
    /// The raised edge's width.
    int32_t edge{};
    /// The header's height, under the top edge.
    int32_t header_height{};
    /// The footer's height, over the bottom edge.
    int32_t footer_height{};
    /// The space between a panel's edge and what it holds.
    int32_t padding{};
    /// The header mark's top row.
    int32_t mark_top{};
    /// The header mark's side.
    int32_t mark_side{};
    /// The OA mark's outlined square, which stands in for the icon.
    int32_t mark_square{};
    /// The space between the header's mark and the title.
    int32_t header_gap{};
    /// Extra columns after each glyph of a heading.
    int32_t heading_tracking{};
    /// A footer button's height.
    int32_t button_height{};
    /// OK's and Cancel's width.
    int32_t button_width{};
    /// The columns between OK and Cancel.
    int32_t button_gap{};
    /// The columns between a control and its keyboard focus outline.
    int32_t focus_inset{};
    /// A line's height in the small font.
    int32_t small_line{};
    /// A line's height in the regular font.
    int32_t regular_line{};
};

/// Compact metrics: the settings dialog's sizes at 0.7.3, exactly.
inline constexpr Metrics compact_metrics{1, 26, 32, 12, 4, 20, 13, 6, 1, 17, 52, 5, 2, 12, 16};

} // namespace oa::ui::kit
