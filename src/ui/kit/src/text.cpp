// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The game's two dialog fonts, the modern faces that draw what they lack,
// and the characters drawn in place of the ones those fonts do not hold.

#include "oa/ui/kit/text.hpp"

#include "oa/base/text/line_break.hpp"
#include "oa/formats/fnt.hpp"
#include "oa/ui/decoded.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace oa::ui::kit {

namespace {

namespace renderer = oa::ui::frontend_renderer;
namespace present = oa::present;

/// A font with the characters it draws and the modern face that draws the others.
struct FaceFont {
    const renderer::TextFont& font;                     ///< the game font
    const present::FontCharacters& characters;          ///< what it draws
    present::TextFace face{present::TextFace::message}; ///< the modern face for the rest
    /// The game font holds no glyphs, as before the game's files are
    /// installed: the modern face draws every text.
    bool modern_only{};
};

/// Tells whether a font holds any glyph.
///
/// @param font the font
/// @return false for a font with no glyph at all
bool holds_glyphs(const oa::formats::fnt::Font& font) noexcept {
    return std::any_of(font.glyphs.begin(), font.glyphs.end(), [](const auto& glyph) {
        return glyph.has_value();
    });
}

/// Returns one of the two fonts.
///
/// @param fonts the fonts
/// @param role which of them
/// @return the font, the message face for the regular one and the status face for the small one
FaceFont face_of(const Fonts& fonts, FontRole role) noexcept {
    const bool small = role == FontRole::small;
    const renderer::TextFont& font = small ? fonts.small : fonts.regular;
    const present::FontCharacters& characters =
        small ? fonts.small_characters : fonts.regular_characters;
    return {
        font,
        characters,
        small ? present::TextFace::status : present::TextFace::message,
        !holds_glyphs(font.font)
    };
}

/// Tells whether a text is all ASCII, which a game font draws byte for byte.
///
/// @param text the text
/// @return true without a byte from 0x80 up
bool plain_ascii(std::string_view text) noexcept {
    return std::all_of(text.begin(), text.end(), [](char letter) {
        return static_cast<unsigned char>(letter) < 0x80;
    });
}

/// A character a game font may lack, and the characters it draws in its place.
struct StandIn {
    char32_t character{};        ///< the character
    std::string_view utf8{};     ///< the character in UTF-8
    std::string_view in_place{}; ///< what the game font draws instead
};

/// The ellipsis as three full stops, and the mark between a location's folders
/// as a greater-than sign.
constexpr std::array<StandIn, 2> stand_ins{{
    {U'\u2026', "\xE2\x80\xA6", "..."},
    {U'\u203a', "\xE2\x80\xBA", ">"},
}};

/// Returns the characters a GUI font draws: every glyph that is not the
/// picture of glyph 0, the font's box for a missing character.
///
/// @param font the font
/// @return the characters
present::FontCharacters gui_characters(const oa::formats::fnt::Font& font) {
    const auto& box = font.glyphs[0];
    return present::FontCharacters::gui_font([&font, &box](uint8_t byte) {
        const auto& glyph = font.glyphs[byte];
        if (!glyph || glyph->width == 0 || glyph->height == 0)
            return false;
        return byte == 0 || !box || box->width != glyph->width || box->height != glyph->height ||
               box->pixels != glyph->pixels;
    });
}

/// The game's palette, which the GUI fonts' glyph pixels index.
constexpr std::string_view game_palette_path = "palettes/palette.pal";

/// Returns a UTF-8 text's width: the characters the font draws at its glyphs'
/// widths, the others as the modern fonts draw them.
///
/// @param font the font
/// @param shown the text, already stood in
/// @return the width, in points
int32_t width_of(const FaceFont& font, std::string_view shown) {
    if (font.modern_only) {
        const auto layers =
            present::modern_text(shown, font.face, 1, present::game_font_text_size, false);
        return layers ? layers->advance : 0;
    }
    if (plain_ascii(shown))
        return renderer::text_width(font.font, shown);
    int32_t width = 0;
    for (const auto& run : present::split_text(shown, font.characters)) {
        if (!run.modern)
            width += renderer::text_width(font.font, run.text);
        else if (const auto layers = present::modern_text(run.text, font.face, 1, run.size, false))
            width += layers->advance;
    }
    return width;
}

} // namespace

std::size_t character_bytes(std::string_view text) noexcept {
    if (text.empty())
        return 0;
    const auto sequence = present::utf8_sequence(text);
    return sequence.bytes != 0 ? sequence.bytes : 1;
}

bool continuation_byte(char byte) noexcept {
    return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
}

std::size_t character_count(std::string_view text) noexcept {
    std::size_t count = 0;
    for (const char byte : text)
        if (!continuation_byte(byte))
            ++count;
    return count;
}

int32_t estimated_width(std::string_view text) {
    return static_cast<int32_t>(oa::base::text::text_columns(text)) * estimated_character_width;
}

std::string fit(std::string_view text, int32_t width, const Measure& measure) {
    if (measure(text) <= width)
        return std::string(text);
    if (measure(ellipsis) > width)
        return {};
    std::vector<std::size_t> ends;
    for (std::size_t at = 0; at < text.size();) {
        at += std::max<std::size_t>(character_bytes(text.substr(at)), 1);
        ends.push_back(std::min(at, text.size()));
    }
    std::size_t low = 0;
    std::size_t high = ends.size();
    std::string best(ellipsis);
    while (low < high) {
        const std::size_t middle = (low + high + 1) / 2;
        std::string candidate(text.substr(0, ends[middle - 1]));
        while (!candidate.empty() && candidate.back() == ' ')
            candidate.pop_back();
        candidate += ellipsis;
        if (measure(candidate) <= width) {
            best = std::move(candidate);
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return best;
}

int32_t measured_width(
    const TextMeasureHooks& hooks, std::string_view text, int32_t pixel_size, bool bold
) {
    if (text.empty())
        return 0;
    if (hooks.width != nullptr)
        return hooks.width(hooks.context, text, pixel_size, bold);
    return static_cast<int32_t>(
        std::ceil(0.55 * pixel_size * static_cast<double>(character_count(text)))
    );
}

int32_t measured_line(const TextMeasureHooks& hooks, int32_t pixel_size, bool bold) {
    if (hooks.line_height != nullptr)
        return std::max(1, hooks.line_height(hooks.context, pixel_size, bold));
    return static_cast<int32_t>(std::ceil(1.25 * pixel_size));
}

std::string with_stand_ins(const Fonts& fonts, FontRole role, std::string_view text) {
    const FaceFont font = face_of(fonts, role);
    std::string shown(text);
    if (font.modern_only)
        return shown;
    for (const StandIn& stand_in : stand_ins) {
        if (font.characters.byte_for(stand_in.character))
            continue;
        for (std::size_t at = shown.find(stand_in.utf8); at != std::string::npos;
             at = shown.find(stand_in.utf8, at + stand_in.in_place.size()))
            shown.replace(at, stand_in.utf8.size(), stand_in.in_place);
    }
    return shown;
}

int32_t text_width(const Fonts& fonts, FontRole role, std::string_view text) {
    const std::string drawn = with_stand_ins(fonts, role, text);
    return width_of(face_of(fonts, role), drawn);
}

int32_t tracked_width(const Fonts& fonts, FontRole role, std::string_view text, int32_t tracking) {
    if (text.empty())
        return 0;
    int32_t characters = 0;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t bytes = character_bytes(text.substr(at));
        if (bytes == 0)
            break;
        ++characters;
        at += bytes;
    }
    return text_width(fonts, role, text) + tracking * (characters - 1);
}

int32_t modern_tracked_width(
    const Fonts& fonts, FontRole role, std::string_view text, int32_t tracking, int32_t scale
) {
    const FaceFont font = face_of(fonts, role);
    const int32_t used = std::max(scale, 1);
    int32_t width = 0;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t bytes = character_bytes(text.substr(at));
        if (bytes == 0)
            break;
        if (at > 0)
            width += tracking;
        if (const auto layers = present::modern_text(
                text.substr(at, bytes), font.face, used, present::game_font_text_size, false
            ))
            width += (layers->advance + used - 1) / used;
        at += bytes;
    }
    return width;
}

int32_t capitals(const Fonts& fonts, FontRole role) {
    const FaceFont font = face_of(fonts, role);
    if (!font.modern_only)
        return font.font.font.nominal_height;
    const auto layers =
        present::modern_text("H", font.face, 1, present::game_font_text_size, false);
    if (!layers || layers->width <= 0)
        return 0;
    for (int32_t row = 0; row < layers->height && row < layers->baseline; ++row)
        for (int32_t column = 0; column < layers->width; ++column)
            if (layers->fill[static_cast<std::size_t>(row * layers->width + column)] != 0)
                return layers->baseline - row;
    return 0;
}

int32_t draw_text(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Fonts& fonts,
    FontRole role,
    std::string_view text,
    int32_t pen,
    int32_t pen_row,
    std::array<uint8_t, 3> colour
) {
    const FaceFont font = face_of(fonts, role);
    if (!font.modern_only && plain_ascii(text))
        return renderer::draw_text(target, placement, font.font, text, pen, pen_row, colour);
    const int32_t scale = std::max(placement.scale, 1);
    // A font the modern face stands in for wholly draws the whole text in it.
    std::vector<present::TextRun> runs;
    if (font.modern_only)
        runs.push_back(present::TextRun{true, std::string(text), present::game_font_text_size});
    else
        runs = present::split_text(text, font.characters);
    const int32_t capital_rows = capitals(fonts, role);
    for (const auto& run : runs) {
        if (!run.modern) {
            pen = renderer::draw_text(target, placement, font.font, run.text, pen, pen_row, colour);
            continue;
        }
        auto layers = present::modern_text(run.text, font.face, scale, run.size, false);
        if (!layers || placement.scale < 1)
            continue;
        // Letters no lighter than their outline, as on the accent's light
        // face, are drawn bare: the outline and the shadow would only
        // thicken them into a blot there.
        if (std::equal(
                colour.begin(),
                colour.end(),
                present::text_outline_color.begin(),
                [](uint8_t letter, uint8_t outline) { return letter <= outline; }
            )) {
            std::fill(layers->outline.begin(), layers->outline.end(), uint8_t{0});
            std::fill(layers->shadow.begin(), layers->shadow.end(), uint8_t{0});
        }
        auto canvas = present::rgb_canvas(
            target.rgb, static_cast<int32_t>(target.width), static_cast<int32_t>(target.height), {}
        );
        if (placement.clip.width > 0 && placement.clip.height > 0) {
            canvas.clip_left = std::max(canvas.clip_left, placement.x + placement.clip.x * scale);
            canvas.clip_top = std::max(canvas.clip_top, placement.y + placement.clip.y * scale);
            canvas.clip_right = std::min(
                canvas.clip_right,
                placement.x + (placement.clip.x + placement.clip.width) * scale - 1
            );
            canvas.clip_bottom = std::min(
                canvas.clip_bottom,
                placement.y + (placement.clip.y + placement.clip.height) * scale - 1
            );
        }
        // A game font's capitals stand on the row under its nominal height.
        const int32_t baseline = pen_row + 1 + capital_rows;
        present::lay_text(
            canvas, *layers, placement.x + pen * scale, placement.y + baseline * scale, colour
        );
        pen += (layers->advance + scale - 1) / scale;
    }
    return pen;
}

void draw_boxed_text(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Fonts& fonts,
    FontRole role,
    std::string_view shown,
    const renderer::SourceRect& box,
    Align align,
    std::array<uint8_t, 3> colour,
    int32_t tracking
) {
    const std::string drawn = with_stand_ins(fonts, role, shown);
    shown = drawn;
    const int32_t width = tracked_width(fonts, role, shown, tracking);
    int32_t pen = box.x;
    if (align == Align::centre)
        pen = box.x + (box.width - width) / 2;
    else if (align == Align::right)
        pen = box.x + box.width - width;
    // A game font's capitals start one row under the pen row.
    const int32_t capital_rows = capitals(fonts, role);
    const int32_t pen_row = box.y + (box.height - capital_rows) / 2 - 1;
    if (tracking == 0) {
        draw_text(target, placement, fonts, role, shown, pen, pen_row, colour);
        return;
    }
    for (std::size_t at = 0; at < shown.size();) {
        const std::size_t bytes = character_bytes(shown.substr(at));
        if (bytes == 0)
            break;
        pen = draw_text(
                  target, placement, fonts, role, shown.substr(at, bytes), pen, pen_row, colour
              ) +
              tracking;
        at += bytes;
    }
}

Fonts load_game_fonts(oa::AssetStore& assets) {
    const std::vector<uint8_t> bytes = assets.read(game_palette_path).bytes;
    oa::PaletteBytes palette{};
    if (bytes.size() < palette.size())
        throw std::runtime_error("the game's palette is short: " + std::string(game_palette_path));
    std::copy_n(bytes.begin(), palette.size(), palette.begin());
    Fonts fonts;
    constexpr std::string_view regular_font = "anims/hattfont12.gaf";
    constexpr std::string_view small_font = "anims/hattfont11.gaf";
    fonts.regular = renderer::text_font(
        oa::ui::decoded::require(oa::formats::fnt::load_gaf(assets, regular_font), regular_font),
        palette
    );
    fonts.small = renderer::text_font(
        oa::ui::decoded::require(oa::formats::fnt::load_gaf(assets, small_font), small_font),
        palette
    );
    fonts.regular_characters = gui_characters(fonts.regular.font);
    fonts.small_characters = gui_characters(fonts.small.font);
    return fonts;
}

} // namespace oa::ui::kit
