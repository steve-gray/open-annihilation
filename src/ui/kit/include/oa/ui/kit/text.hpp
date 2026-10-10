// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Provisional: this header changes whenever OA's screens need it to, until the kit is declared stable (src/ui/kit/README.md).

// The OA UI kit's text: the game's two fonts and the modern faces that draw
// what they lack, one measure, one wrap, and the stand-in characters. The
// kit never looks a word up in the interface catalogue; a caller passes the
// text to show.
#pragma once

#include "oa/formats/hpi.hpp"
#include "oa/present/game_text.hpp"
#include "oa/ui/frontend_renderer.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::kit {

/// Which of the two game fonts a text is drawn in.
enum class FontRole : uint8_t {
    regular, ///< labels, values and buttons
    small,   ///< the section heading, the hints and the version
};

/// The fonts a screen draws its texts in.
struct Fonts {
    oa::ui::frontend_renderer::TextFont regular; ///< labels, values and buttons
    oa::ui::frontend_renderer::TextFont small;   ///< the section heading, the hints and the version
    /// The characters each font draws, for UTF-8 texts such as a language's
    /// name in itself; the modern fonts draw the others.
    oa::present::FontCharacters regular_characters{};
    oa::present::FontCharacters small_characters{}; ///< the small font's
};

/// Loads the game's fonts: the button font as the regular one and the label
/// font as the small one, each readied for text in one colour. Their letters
/// keep their shading and lose the dark outline round them.
///
/// Throws std::runtime_error when a font or the game's palette is missing, or a
/// font is malformed.
///
/// @param assets the game's files
/// @return the fonts
[[nodiscard]] Fonts load_game_fonts(oa::AssetStore& assets);

/// Returns a UTF-8 text's width as it is drawn: the characters a font draws
/// at its glyphs' widths, and the others in the modern fonts. A character the
/// game font lacks, of the ones it stands in for, is measured as the
/// characters drawn in its place.
///
/// @param fonts the fonts
/// @param role which of them
/// @param text the text, in UTF-8
/// @return the width, in points
[[nodiscard]] int32_t text_width(const Fonts& fonts, FontRole role, std::string_view text);

/// Returns a text's width with extra columns after each character but the last.
///
/// The extra columns are counted on the text as given. The width itself counts
/// a stood-in character as the characters drawn in its place.
///
/// @param fonts the fonts
/// @param role which of them
/// @param text the text, in UTF-8
/// @param tracking the extra columns
/// @return the width, in points
[[nodiscard]] int32_t
tracked_width(const Fonts& fonts, FontRole role, std::string_view text, int32_t tracking);

/// Returns how far a tracked text drawn wholly in the modern face moves the
/// pen, one character at a time at a scale, as draw_boxed_text draws it.
///
/// @param fonts the fonts
/// @param role which of them
/// @param text the text, in UTF-8, as it is drawn
/// @param tracking the extra columns after each character but the last
/// @param scale the placement's scale
/// @return the width, in points
[[nodiscard]] int32_t modern_tracked_width(
    const Fonts& fonts, FontRole role, std::string_view text, int32_t tracking, int32_t scale
);

/// Returns how many rows a font's capitals stand: the game font's nominal
/// height, or, for a font the modern face stands in for wholly, the rows its
/// capital H stands above its baseline.
///
/// @param fonts the fonts
/// @param role which of them
/// @return the rows, in points
[[nodiscard]] int32_t capitals(const Fonts& fonts, FontRole role);

/// Returns a text as a font shows it. Where the game font lacks the ellipsis
/// or the mark between a location's folders, the characters it draws in their
/// place are written instead: "..." and ">". A font the modern face stands in
/// for wholly is returned unchanged.
///
/// @param fonts the fonts
/// @param role which of them
/// @param text the text, in UTF-8
/// @return the text to draw and measure
[[nodiscard]] std::string with_stand_ins(const Fonts& fonts, FontRole role, std::string_view text);

/// Draws a UTF-8 text: the characters the font draws with its glyphs, the
/// others in the modern fonts on the font's baseline, at the placement's
/// scale and within its clip.
///
/// @param[in,out] target the surface
/// @param placement where the picture lands
/// @param fonts the fonts
/// @param role which of them
/// @param text the text, in UTF-8, as it is drawn
/// @param pen the pen column, in points
/// @param pen_row the pen row, in points
/// @param colour the colour, red, green and blue
/// @return the pen column after the text, in points
int32_t draw_text(
    oa::ui::frontend_renderer::Surface& target,
    const oa::ui::frontend_renderer::Placement& placement,
    const Fonts& fonts,
    FontRole role,
    std::string_view text,
    int32_t pen,
    int32_t pen_row,
    std::array<uint8_t, 3> colour
);

/// Where a text sits along a box's width.
enum class Align : uint8_t {
    left,   ///< at the box's left
    centre, ///< in the box's middle
    right,  ///< ending at the box's right
};

/// Draws a text in a box, its capitals centred on the box's height. The text
/// is what the caller wants shown: the kit does not look it up. A character
/// the game font lacks, of the ones it stands in for, is drawn as the
/// characters in its place.
///
/// @param[in,out] target the surface
/// @param placement where the picture lands
/// @param fonts the fonts
/// @param role which of them
/// @param shown the text to draw, in UTF-8
/// @param box the box, in points
/// @param align where the text sits along the box's width
/// @param colour the text's colour, red, green and blue
/// @param tracking extra columns after each glyph but the last
void draw_boxed_text(
    oa::ui::frontend_renderer::Surface& target,
    const oa::ui::frontend_renderer::Placement& placement,
    const Fonts& fonts,
    FontRole role,
    std::string_view shown,
    const oa::ui::frontend_renderer::SourceRect& box,
    Align align,
    std::array<uint8_t, 3> colour,
    int32_t tracking = 0
);

/// Returns the bytes of the UTF-8 character a text starts with.
///
/// A well-formed sequence of two to four bytes returns its length. Anything
/// else, an ASCII byte included, returns 1. An empty text returns 0.
///
/// @param text the text, in UTF-8
/// @return the character's bytes
[[nodiscard]] std::size_t character_bytes(std::string_view text) noexcept;

/// Tells whether a byte continues a UTF-8 character rather than starting one.
///
/// @param byte the byte
/// @return true for 10xxxxxx
[[nodiscard]] bool continuation_byte(char byte) noexcept;

/// Returns how many characters a text holds, counting a byte that continues
/// a character as part of it.
///
/// @param text the text, in UTF-8
/// @return the characters
[[nodiscard]] std::size_t character_count(std::string_view text) noexcept;

/// A text's width, in points.
using Measure = std::function<int32_t(std::string_view)>;

/// The width counted for one character of a text measured without a font, in points.
inline constexpr int32_t estimated_character_width = 7;

/// Returns a text's estimated width: estimated_character_width a character,
/// twice that for a Chinese, Japanese or Korean character.
///
/// @param text the text, in UTF-8
/// @return the width, in points
[[nodiscard]] int32_t estimated_width(std::string_view text);

/// How one wrap differs from another. Unset, a word wider than a line is
/// broken between characters, a line has no limit, and a new line is not a
/// paragraph of its own.
struct WrapRules {
    /// A text with Chinese, Japanese or Korean characters breaks between them,
    /// and never starts a line with a closing mark or ends one with an opening
    /// mark.
    bool wide_scripts{true};
    /// A new line starts a paragraph. An empty paragraph adds no blank line.
    bool newlines{};
    /// A word wider than a line becomes what this returns. Unset, the word is
    /// broken instead.
    std::function<std::string(std::string_view)> shorten_word{};
    /// Where a word wider than a line breaks, in bytes, at least one character.
    /// Unset, the break is after the whole characters that fit, and a character
    /// that does not fit takes a line of its own.
    std::function<std::size_t(std::string_view)> break_word{};
    /// The most lines kept. 0 keeps every line.
    std::size_t most_lines{};
    /// When more lines are made than most_lines, the last kept line becomes
    /// what this returns of that line and every dropped line joined by single
    /// spaces. The ellipsis, when one is wanted, is this function's.
    std::function<std::string(std::string_view)> shorten_last{};
};

/// Breaks a text into lines no wider than a width.
///
/// Words break at spaces. The rules say how a wide script, a new line, a word
/// wider than a line and a limit on the lines differ from that. One loop
/// wraps every caller.
///
/// @param text the text, in UTF-8
/// @param width the room, in points
/// @param measure a text's width in the font it is drawn in
/// @param rules how this call differs
/// @return the lines; none for an empty text
[[nodiscard]] std::vector<std::string>
wrap(std::string_view text, int32_t width, const Measure& measure, const WrapRules& rules = {});

/// Breaks a folder's path into lines no wider than a width: after a separator
/// ('/' or '\\'), each line as many whole components as fit, a component wider
/// than a line broken between its characters.
///
/// @param path the path, in UTF-8
/// @param width the room, in points
/// @param measure a text's width in the font it is drawn in
/// @return the lines, which put together give the path
[[nodiscard]] std::vector<std::string>
wrap_path(std::string_view path, int32_t width, const Measure& measure);

} // namespace oa::ui::kit
