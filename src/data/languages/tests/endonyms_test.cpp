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

namespace {

namespace languages = oa::data::languages;
namespace text_font = oa::platform::text_font;

/// Requires that a line of the endonym list draws in both weights.
void require_line(text_font::FontStack& stack, std::string_view line) {
    OA_CHECK(stack.draws(line, text_font::Weight::bold));
    OA_CHECK(stack.draws(line, text_font::Weight::regular));
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
    while (std::getline(list, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        require_line(*stack, line);
    }
    return oa::test::check_exit_status();
}
