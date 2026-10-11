// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// One wrap for every OA screen. Words break at spaces. WrapRules carries the
// differences: a wide script, a new line, a word wider than a line, and a
// limit on the lines.

#include "oa/ui/kit/text.hpp"

#include "oa/base/text/line_break.hpp"

#include <string>
#include <utility>

namespace oa::ui::kit {

namespace {

/// Returns the bytes of the first character, at least one when the text is not empty.
///
/// @param text the text
/// @return the character's bytes; 0 for an empty text
std::size_t one_character(std::string_view text) noexcept {
    const std::size_t bytes = character_bytes(text);
    if (bytes == 0)
        return 0;
    return bytes > text.size() ? text.size() : bytes;
}

/// Breaks a text that is wider than a line between its characters.
///
/// @param text the text
/// @param width the room
/// @param measure a text's width
/// @param[in,out] lines receives every line but the last
/// @return the last line, which may take more after it
std::string break_characters(
    std::string_view text, int32_t width, const Measure& measure, std::vector<std::string>& lines
) {
    std::string line;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t bytes = one_character(text.substr(at));
        if (bytes == 0)
            break;
        const std::string_view character = text.substr(at, bytes);
        if (!line.empty() && measure(line + std::string(character)) > width) {
            lines.push_back(line);
            line.clear();
        }
        line += character;
        at += bytes;
    }
    return line;
}

/// Breaks a text that holds Chinese, Japanese or Korean between its characters.
///
/// @param text the text
/// @param width the room
/// @param measure a text's width
/// @param[in,out] lines receives the lines
void wrap_wide_script(
    std::string_view text, int32_t width, const Measure& measure, std::vector<std::string>& lines
) {
    const auto fits = [&](std::string_view start) { return measure(start) <= width; };
    for (;;) {
        while (!text.empty() && text.front() == ' ')
            text.remove_prefix(1);
        if (text.empty())
            break;
        const auto row = oa::base::text::first_row(text, fits);
        lines.emplace_back(text.substr(0, row.bytes));
        text.remove_prefix(row.next);
    }
}

/// Breaks one paragraph into lines, at its spaces.
///
/// @param text the paragraph, without a new line
/// @param width the room
/// @param measure a text's width
/// @param rules how a word wider than a line is broken
/// @param[in,out] lines receives the lines
void wrap_words(
    std::string_view text,
    int32_t width,
    const Measure& measure,
    const WrapRules& rules,
    std::vector<std::string>& lines
) {
    std::string line;
    std::size_t at = 0;
    while (at < text.size()) {
        if (text[at] == ' ') {
            ++at;
            continue;
        }
        const auto end = std::min(text.find(' ', at), text.size());
        std::string_view word = text.substr(at, end - at);
        at = end;
        if (word.empty())
            continue;
        const std::string joined =
            line.empty() ? std::string(word) : line + " " + std::string(word);
        if (measure(joined) <= width) {
            line = joined;
            continue;
        }
        if (!line.empty()) {
            lines.push_back(std::move(line));
            line.clear();
        }
        // A word that fits on its own line is kept whole. shorten_word does
        // that itself when the word fits, and breaks the word into one line
        // when it does not. Without it the word is broken across lines.
        if (rules.shorten_word) {
            line = rules.shorten_word(word);
            continue;
        }
        while (!word.empty() && measure(word) > width) {
            std::size_t piece =
                rules.break_word ? rules.break_word(word) : fitting_start(word, width, measure);
            if (piece == 0 || piece > word.size())
                piece = one_character(word);
            if (piece == 0)
                break;
            lines.emplace_back(word.substr(0, piece));
            word.remove_prefix(piece);
        }
        line = std::string(word);
    }
    if (!line.empty())
        lines.push_back(std::move(line));
}

/// Keeps at most most_lines, the last one shortened when lines were dropped.
///
/// @param rules the limit and the shortening
/// @param[in,out] lines the lines
void keep_most_lines(const WrapRules& rules, std::vector<std::string>& lines) {
    if (rules.most_lines == 0 || lines.size() <= rules.most_lines || !rules.shorten_last)
        return;
    std::string joined = lines[rules.most_lines - 1];
    for (std::size_t index = rules.most_lines; index < lines.size(); ++index)
        joined += " " + lines[index];
    lines.resize(rules.most_lines);
    lines.back() = rules.shorten_last(joined);
}

} // namespace

std::size_t fitting_start(std::string_view word, int32_t width, const Measure& measure) {
    const std::size_t first = one_character(word);
    if (first == 0)
        return 0;
    std::size_t fits = first;
    for (std::size_t at = fits; at < word.size();) {
        const std::size_t next = at + one_character(word.substr(at));
        if (next <= at)
            break;
        if (measure(word.substr(0, next)) > width)
            break;
        fits = next;
        at = next;
    }
    return fits;
}

std::vector<std::string>
wrap(std::string_view text, int32_t width, const Measure& measure, const WrapRules& rules) {
    std::vector<std::string> lines;
    if (rules.newlines) {
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t end = std::min(text.find('\n', start), text.size());
            const std::string_view paragraph = text.substr(start, end - start);
            if (rules.wide_scripts && oa::base::text::has_wide_script(paragraph))
                wrap_wide_script(paragraph, width, measure, lines);
            else
                wrap_words(paragraph, width, measure, rules, lines);
            if (end >= text.size())
                break;
            start = end + 1;
        }
    } else if (rules.wide_scripts && oa::base::text::has_wide_script(text)) {
        wrap_wide_script(text, width, measure, lines);
    } else {
        wrap_words(text, width, measure, rules, lines);
    }
    keep_most_lines(rules, lines);
    return lines;
}

std::vector<std::string> wrap_path(std::string_view path, int32_t width, const Measure& measure) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t at = 0;
    while (at < path.size()) {
        // A component and the separator after it.
        const auto separator = path.find_first_of("/\\", at);
        const auto end = separator == std::string_view::npos ? path.size() : separator + 1;
        const std::string_view piece = path.substr(at, end - at);
        at = end;
        if (measure(line + std::string(piece)) <= width) {
            line += piece;
            continue;
        }
        if (!line.empty())
            lines.push_back(line);
        line = measure(piece) <= width ? std::string(piece)
                                       : break_characters(piece, width, measure, lines);
    }
    if (!line.empty())
        lines.push_back(std::move(line));
    return lines;
}

} // namespace oa::ui::kit
