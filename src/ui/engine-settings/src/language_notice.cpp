// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The missing-language notice's display list, built as a question and then
// patched: the English body dimmed, a failure in the lock colour, and
// Download greyed while it cannot be pressed.

#include "oa/ui/engine_settings/language_notice.hpp"

#include "oa/data/languages/interface_text.hpp"
#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/components_more.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/kit/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace oa::ui::engine_settings {

namespace kit = oa::ui::kit;
namespace languages = oa::data::languages;

namespace {

/// Use English, the button on the left.
constexpr kit::ControlId kUseEnglish = 0;
/// Download, the primary, on the right.
constexpr kit::ControlId kDownload = 1;
/// The notice's name to automation.
constexpr std::string_view kWord = "language-notice";

/// Which paragraph a wrapped line came from.
enum class LineKind : uint8_t {
    title,    ///< the language's own name
    own,      ///< the body in the language
    english,  ///< the body in English
    status,   ///< the line under the body
    progress, ///< how far a download has come
};

/// One paragraph, and which part of the notice it is.
struct TaggedLine {
    std::string text{};               ///< the paragraph
    bool path{};                      ///< drawn large, as a path
    LineKind kind{LineKind::english}; ///< which part it is
};

/// Tells whether every byte of a text is ASCII.
///
/// @param text the text
/// @return true when none is past ASCII
bool ascii(std::string_view text) noexcept {
    for (const unsigned char byte : text)
        if (byte >= 0x80U)
            return false;
    return true;
}

/// Returns a text as the interface shows it.
///
/// A text that is not ASCII is returned as given, so a line in another
/// language is never looked up.
///
/// @param text the text, in English when it is ASCII
/// @return the text to draw
std::string shown(std::string_view text) {
    if (!ascii(text))
        return std::string(text);
    return std::string(languages::interface_text(text));
}

/// Appends the non-empty pieces of a status, one paragraph each.
///
/// @param[in,out] lines the paragraphs
/// @param status the status; a line break separates paragraphs
/// @param kind which part they are
void append_status(std::vector<TaggedLine>& lines, std::string_view status, LineKind kind) {
    std::size_t start = 0;
    while (start < status.size()) {
        const auto end = status.find('\n', start);
        const auto count = end == std::string_view::npos ? status.size() - start : end - start;
        if (count > 0)
            lines.push_back(TaggedLine{std::string(status.substr(start, count)), false, kind});
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
}

/// Returns the paragraphs, in the order the notice draws them.
///
/// @param notice what the notice shows
/// @return the paragraphs
std::vector<TaggedLine> paragraphs_of(const LanguageNotice& notice) {
    std::vector<TaggedLine> lines;
    if (!notice.title.empty())
        lines.push_back(TaggedLine{notice.title, true, LineKind::title});
    if (!notice.own_body.empty())
        lines.push_back(TaggedLine{notice.own_body, false, LineKind::own});
    if (!notice.english_body.empty())
        lines.push_back(TaggedLine{shown(notice.english_body), false, LineKind::english});
    append_status(lines, notice.status, LineKind::status);
    if (!notice.progress_text.empty())
        lines.push_back(TaggedLine{shown(notice.progress_text), false, LineKind::progress});
    return lines;
}

/// Returns how far a bar is filled, on the question's scale.
///
/// @param progress from 0 to 1; below 0 means no bar
/// @return the fill, or -1 for no bar
int32_t bar_of(float progress) noexcept {
    if (progress < 0.F)
        return -1;
    float fraction = progress;
    if (fraction > 1.F)
        fraction = 1.F;
    const auto whole = static_cast<int32_t>(kit::question_progress_whole);
    auto done = static_cast<int32_t>(static_cast<double>(fraction) * static_cast<double>(whole));
    if (done < 0)
        done = 0;
    if (done > whole)
        done = whole;
    return done;
}

/// Returns the width a paragraph wraps at.
///
/// @param size_class the notice's size class
/// @return the width, in points
int32_t wrap_width(kit::SizeClass size_class) noexcept {
    return kit::metrics_of(size_class).notice_width - 2 * kit::compact_metrics.padding;
}

/// Returns a text's width in a font, or its estimated width without fonts.
///
/// @param fonts the fonts; null to estimate
/// @param role which font
/// @return the measure
kit::Measure measure_of(const kit::Fonts* fonts, kit::FontRole role) {
    if (fonts == nullptr)
        return [](std::string_view text) { return kit::estimated_width(text); };
    return [fonts, role](std::string_view text) { return kit::text_width(*fonts, role, text); };
}

/// Paints the English body dim and a failure in the lock colour.
///
/// The question draws every line in the text colour. The lines are the
/// paragraphs' wraps, in order, measured as the question measured them.
///
/// @param[in,out] list the display list
/// @param lines the paragraphs
/// @param failed true when the status is a failure
/// @param size_class the notice's size class
/// @param fonts the fonts the question wrapped with; null when it estimated
void colour_lines(
    kit::DisplayList& list,
    const std::vector<TaggedLine>& lines,
    bool failed,
    kit::SizeClass size_class,
    const kit::Fonts* fonts
) {
    const int32_t width = wrap_width(size_class);
    const kit::Measure regular = measure_of(fonts, kit::FontRole::regular);
    const kit::Measure small = measure_of(fonts, kit::FontRole::small);
    std::vector<LineKind> kinds;
    for (const TaggedLine& line : lines) {
        const std::vector<std::string> wrapped = line.path
                                                     ? kit::wrap_path(line.text, width, regular)
                                                     : kit::wrap(line.text, width, small);
        for (std::size_t index = 0; index < wrapped.size(); ++index)
            kinds.push_back(line.kind);
    }
    std::size_t index = 0;
    for (kit::Item& item : list.items) {
        if (item.role != kit::Role::text)
            continue;
        if (index >= kinds.size())
            break;
        const LineKind kind = kinds[index];
        ++index;
        if (kind == LineKind::english)
            item.colour = kit::colour::quiet;
        else if (kind == LineKind::status && failed)
            item.colour = kit::colour::lock;
        else
            item.colour = kit::colour::text;
    }
}

/// Greys Download, and makes a press miss it, while it is not enabled.
///
/// @param[in,out] list the display list
/// @param enabled true when Download takes a press
void grey_download(kit::DisplayList& list, bool enabled) {
    if (enabled)
        return;
    kit::ControlId download = kDownload;
    for (const kit::Control& control : list.controls)
        if (control.name == std::string(kWord) + ".download")
            download = control.id;
    for (kit::Control& control : list.controls)
        if (control.id == download)
            control.enabled = false;
    for (kit::Item& item : list.items) {
        if (item.role != kit::Role::button || item.control != download)
            continue;
        if (auto* look = std::get_if<kit::ButtonLook>(&item.look)) {
            look->enabled = false;
            look->style = kit::ButtonStyle::plain;
        }
        item.state.disabled = true;
    }
}

} // namespace

kit::DisplayList layout_language_notice(
    const LanguageNotice& notice, const kit::Frame& frame, const kit::Fonts* fonts
) {
    const std::vector<TaggedLine> lines = paragraphs_of(notice);
    kit::Question question;
    question.title = shown("Language");
    question.paragraphs.reserve(lines.size());
    for (const TaggedLine& line : lines)
        question.paragraphs.push_back(kit::Paragraph{line.text, line.path});
    question.buttons.push_back(kit::QuestionButton{shown("Use English"), false, "use-english"});
    question.buttons.push_back(
        kit::QuestionButton{shown(notice.download_label), notice.download_enabled, "download"}
    );
    question.cancel_button = kUseEnglish;
    question.primary_button = kDownload;
    question.marked = kDownload;
    question.progress = bar_of(notice.progress);
    question.size_class = frame.size_class;
    question.word = std::string(kWord);
    kit::DisplayList list = kit::question_list(question, fonts);
    colour_lines(list, lines, notice.failed, frame.size_class, fonts);
    grey_download(list, notice.download_enabled);
    return list;
}

} // namespace oa::ui::engine_settings
