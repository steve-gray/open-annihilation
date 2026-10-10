// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-language-registry: every language the game knows, and the one the
// run shows. English comes first, the built-in languages follow in their
// order, and the pseudo pack installed beside the preferences file is
// listed as installed, with the word Pseudo, and is the language shown.

#include "oa/app/runtime.hpp"
#include "oa/data/languages.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oa::app {

namespace {

namespace languages = oa::data::languages;

/// The built-in languages, in the order the game lists them.
constexpr std::array<std::string_view, 6> kBuiltInOrder{"en", "de", "es", "fr", "it", "zh-Hans"};
/// The pseudo pack's tag, and the word its manifest names.
constexpr std::string_view kPseudoTag = "en-XA";
constexpr std::string_view kPseudoWord = "Pseudo";

/// Stops the check with a reason.
///
/// @param what what went wrong
[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("language registry check: " + what);
}

/// Adds one reason to the list the check reports after it has printed.
///
/// @param problems the reasons so far
/// @param problem the next reason
void note(std::string& problems, std::string_view problem) {
    if (!problems.empty())
        problems += "; ";
    problems += problem;
}

/// Names where a live language comes from, as the check prints it.
///
/// @param source the language's source
/// @return built-in, installed or available
const char* source_name(languages::Source source) {
    switch (source) {
    case languages::Source::built_in:
        return "built-in";
    case languages::Source::installed:
        return "installed";
    case languages::Source::available:
        return "available";
    }
    return "unknown";
}

/// Names what drawing a language needs, as the check prints it.
///
/// @param needs the language's needs
/// @return the hyphenated name
const char* needs_name(languages::TextNeeds needs) {
    switch (needs) {
    case languages::TextNeeds::game_fonts:
        return "game-fonts";
    case languages::TextNeeds::modern_fonts:
        return "modern-fonts";
    case languages::TextNeeds::more_font_faces:
        return "more-font-faces";
    case languages::TextNeeds::text_shaping:
        return "text-shaping";
    }
    return "unknown";
}

/// Tells whether the built-in languages are live in their order, with other
/// languages allowed between them.
///
/// @param live the live languages
/// @return true when their tags are en, de, es, fr, it, zh-Hans in order
bool built_ins_follow(std::span<const languages::Language* const> live) {
    std::size_t next = 0;
    for (const languages::Language* language : live) {
        if (language->source != languages::Source::built_in)
            continue;
        if (next == kBuiltInOrder.size() || language->tag != kBuiltInOrder[next])
            return false;
        ++next;
    }
    return next == kBuiltInOrder.size();
}

} // namespace

void Runtime::check_language_registry() {
    // The check reads the language the preferences file chooses, so it never
    // runs over the player's own file.
    if (!options_.preferences_file)
        fail("needs --preferences-file");

    const auto live = languages::known_languages();
    for (const languages::Language* language : live)
        std::cout << "language registry: " << language->tag << ' ' << source_name(language->source)
                  << ' ' << needs_name(language->needs) << '\n';
    std::cout << "language registry: shown " << shown_language().tag << '\n';
    std::cout.flush();

    std::string problems;
    if (live.empty() || live.front()->tag != "en" ||
        live.front()->source != languages::Source::built_in)
        note(problems, "English is not first");
    if (!built_ins_follow(live))
        note(problems, "the built-in languages are not in their order");
    const languages::Language* pseudo = nullptr;
    for (const languages::Language* language : live)
        if (language->tag == kPseudoTag)
            pseudo = language;
    if (pseudo == nullptr)
        note(problems, "en-XA is not listed");
    else if (pseudo->source != languages::Source::installed)
        note(problems, "en-XA is not installed");
    else if (pseudo->game_name != kPseudoWord)
        note(problems, "en-XA's word is not Pseudo");
    if (shown_language().tag != kPseudoTag)
        note(problems, "the run shows " + std::string(shown_language().tag) + ", not en-XA");
    if (!problems.empty())
        fail(problems);
}

} // namespace oa::app
