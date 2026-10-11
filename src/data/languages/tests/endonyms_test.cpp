// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Every language's own name draws from the fonts that travel with the game,
// and so does every line of the endonym list, which is the notice shown
// before a language pack is installed.

#include "oa/data/languages.hpp"
#include "oa/platform/text_font.hpp"
#include "oa/test/check.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace languages = oa::data::languages;
namespace text_font = oa::platform::text_font;

/// Requires that a line of the endonym list draws in both weights.
void require_line(text_font::FontStack& stack, std::string_view line) {
    OA_CHECK(stack.draws(line, text_font::Weight::bold));
    OA_CHECK(stack.draws(line, text_font::Weight::regular));
}

/// Returns a notice line with the list's sample size in place of "%s".
///
/// @param text a notice line
/// @return the line the endonym list holds
std::string notice_line(std::string_view text) {
    std::string line(text);
    const auto size = line.find("%s");
    if (size != std::string::npos)
        line.replace(size, 2, "4.0 MB");
    return line;
}

/// Requires every line of one notice, and that each is a line of the list.
///
/// @param stack the fonts that travel with the game
/// @param lines the list's lines
/// @param notice the notice
void require_notice(
    text_font::FontStack& stack,
    const std::vector<std::string>& lines,
    const languages::PackNotice& notice
) {
    const std::string_view texts[] = {
        notice.body_sized,
        notice.body,
        notice.download,
        notice.failed,
        notice.retry,
        notice.unavailable,
    };
    for (const std::string_view text : texts) {
        const std::string line = notice_line(text);
        require_line(stack, line);
        bool listed = false;
        for (const std::string& known : lines)
            if (known == line)
                listed = true;
        OA_CHECK(listed);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "data-languages-endonyms: the endonym list's path is required\n";
        return 1;
    }
    auto stack = text_font::FontStack::open(text_font::bundled_font_directory());
    OA_CHECK(stack != nullptr);
    if (!stack)
        return oa::test::check_exit_status();
    OA_CHECK(stack->has_face(text_font::Face::endonyms));
    for (const languages::Language* language : languages::known_languages()) {
        OA_CHECK(language != nullptr);
        if (language != nullptr)
            require_line(*stack, language->endonym);
    }
    std::ifstream list(argv[1], std::ios::binary);
    if (!list) {
        std::cerr << "data-languages-endonyms: cannot read " << argv[1] << '\n';
        return 1;
    }
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(list, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        require_line(*stack, line);
        lines.push_back(line);
    }
    OA_CHECK(languages::pack_notice("en") == nullptr);
    OA_CHECK(languages::pack_notice("zh_Hans") != nullptr);
    for (const languages::Language* language : languages::known_languages()) {
        if (language == nullptr)
            continue;
        if (const languages::PackNotice* notice = languages::pack_notice(language->tag))
            require_notice(*stack, lines, *notice);
    }
    return oa::test::check_exit_status();
}
