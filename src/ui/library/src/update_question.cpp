// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The update question, written from the patterns in text.hpp.
#include "oa/ui/library/update_question.hpp"

#include "oa/ui/library/text.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::library {

namespace {

/// Returns a text with its first letter, when it is one of a to z, in capitals.
///
/// @param text the text
/// @return the text, its first letter a capital
std::string capitalised(std::string text) {
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z')
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    return text;
}

/// Tells whether a revision of the same version is what an update brings,
/// as change_texts names it.
///
/// @param installed the package installed
/// @param update its update
/// @return true when the version text stays
bool same_version(const Installed& installed, const UpdateNote& update) {
    return installed.version == update.to_version;
}

/// Writes the question's Rules fact: both rules hashes, or "unknown".
///
/// @param update the update
/// @param text how texts are looked up
/// @return the fact
Fact rules_fact(const UpdateNote& update, const TextHooks& text) {
    Fact fact;
    fact.long_label = shown(text, words::fact_rules);
    fact.short_label = fact.long_label;
    if (update.rules == UpdateNote::Rules::changes && update.from_rules && update.to_rules)
        fact.long_value = filled(
            text,
            words::question_rules,
            {{"from", rules_text(*update.from_rules)}, {"to", rules_text(*update.to_rules)}}
        );
    else
        fact.long_value = shown(text, words::question_rules_unknown);
    fact.short_value = fact.long_value;
    fact.mark = Mark::warn;
    return fact;
}

/// Writes the question's Download fact: what the update downloads.
///
/// @param update the update
/// @param text how texts are looked up
/// @return the fact
Fact download_fact(const UpdateNote& update, const TextHooks& text) {
    Fact fact;
    fact.long_label = shown(text, words::fact_download);
    fact.short_label = fact.long_label;
    fact.long_value = size_text(update.size, text);
    fact.short_value = fact.long_value;
    return fact;
}

/// Joins names with the list separator, up to named_updates of them, then
/// how many more there are.
///
/// @param names the names, in list order
/// @param text how texts are looked up
/// @return the names
std::string names_text(const std::vector<std::string>& names, const TextHooks& text) {
    std::string joined;
    const std::size_t listed = std::min(names.size(), named_updates);
    for (std::size_t index = 0; index < listed; ++index) {
        if (index != 0) {
            joined += shown(text, words::list_separator);
            joined.push_back(' ');
        }
        joined += names[index];
    }
    if (names.size() <= listed)
        return joined;
    const std::string more = std::to_string(names.size() - listed);
    return filled(text, words::question_names_more, {{"names", joined}, {"count", more}});
}

} // namespace

bool asks_before_update(const Entry& entry, const UpdateNote& update) noexcept {
    if (entry.built_in || entry.id.kind != Kind::mod || update.kind != Kind::mod)
        return false;
    return update.rules == UpdateNote::Rules::changes || update.rules == UpdateNote::Rules::unknown;
}

std::optional<UpdateQuestion>
update_question(const Entry& entry, const UpdateNote& update, bool playing, const TextHooks& text) {
    if (!asks_before_update(entry, update))
        return std::nullopt;
    Installed installed;
    if (entry.installed != nullptr)
        installed = *entry.installed;
    const std::string name(entry_name(entry));
    const ChangeTexts names = change_texts(installed, update, text);
    UpdateQuestion asked;
    asked.title = filled(text, words::question_title, {{"name", name}});
    if (same_version(installed, update) && installed.revision > 0 && update.to_revision > 0)
        asked.heading = filled(
            text,
            words::question_heading_revision,
            {{"name", name},
             {"version", installed.version},
             {"from", std::to_string(installed.revision)},
             {"to", std::to_string(update.to_revision)}}
        );
    else if (!same_version(installed, update))
        asked.heading = filled(
            text,
            words::question_heading_version,
            {{"name", name}, {"from", names.from}, {"to", names.to}}
        );
    else if (!installed.version.empty())
        asked.heading = filled(
            text, words::question_heading_same, {{"name", name}, {"version", installed.version}}
        );
    else
        asked.heading = name;
    const std::string from_capital = capitalised(names.from);
    const std::string to_capital = capitalised(names.to);
    asked.body = filled(
        text,
        update.rules == UpdateNote::Rules::changes ? words::question_changes
                                                   : words::question_unknown,
        {{"from", names.from}, {"to", names.to}, {"From", from_capital}, {"To", to_capital}}
    );
    if (playing)
        asked.body += " " + filled(text, words::question_finishes, {{"name", name}});
    asked.facts.push_back(rules_fact(update, text));
    asked.facts.push_back(download_fact(update, text));
    return asked;
}

std::optional<UpdateQuestion>
update_all_question(std::span<const Entry> entries, const TextHooks& text) {
    std::vector<std::string> names;
    for (const Entry& entry : entries)
        if (entry.update != nullptr && asks_before_update(entry, *entry.update))
            names.emplace_back(entry_name(entry));
    if (names.empty())
        return std::nullopt;
    UpdateQuestion asked;
    asked.title = shown(text, words::question_title_all);
    const std::string listed = names_text(names, text);
    if (names.size() == 1)
        asked.body = filled(text, words::question_all_one, {{"names", listed}});
    else
        asked.body = filled(
            text,
            words::question_all_many,
            {{"count", std::to_string(names.size())}, {"names", listed}}
        );
    return asked;
}

oa::ui::kit::Question update_prompt(const UpdateQuestion& asked, const TextHooks& text) {
    oa::ui::kit::Question question;
    question.title = asked.title;
    if (!asked.heading.empty())
        question.paragraphs.push_back({asked.heading, false});
    question.paragraphs.push_back({asked.body, false});
    for (const Fact& fact : asked.facts)
        question.paragraphs.push_back(
            {filled(
                 text,
                 words::question_fact,
                 {{"label", fact.long_label}, {"value", fact.long_value}}
             ),
             false}
        );
    question.buttons.push_back(
        {std::string(shown(text, words::action_not_now)), false, std::string(not_now_button_id)}
    );
    question.buttons.push_back(
        {std::string(shown(text, words::action_update)), true, std::string(update_button_id)}
    );
    question.cancel_button = 0;
    question.primary_button = 1;
    question.marked = 0;
    return question;
}

} // namespace oa::ui::library
