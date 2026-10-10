// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The prompts of an install, taken from the kind's own words.

#include "prompt_text.hpp"

#include "oa/app/package_install/prompts.hpp"

#include <string>
#include <string_view>

namespace oa::app::package_install {

namespace {

/// Returns a prompt a kind's words build, or an empty one when it has none.
///
/// @param prompts the kind's words; null for none
/// @param call the kind's function
/// @return the prompt
template <typename Function, typename... Arguments>
PackagePrompt
from_kind(const KindPrompts* prompts, Function KindPrompts::* call, Arguments&&... arguments) {
    if (prompts == nullptr || prompts->*call == nullptr)
        return {};
    return (prompts->*call)(std::forward<Arguments>(arguments)...);
}

} // namespace

PackagePrompt installing_prompt(
    const PackageKind& kind,
    const Incoming& incoming,
    uint64_t done_bytes,
    uint64_t total_bytes,
    InstallingPhase phase,
    std::string_view file_name
) {
    return from_kind(
        kind.prompts, &KindPrompts::installing, incoming, done_bytes, total_bytes, phase, file_name
    );
}

PackagePrompt question_prompt(
    const PackageKind& kind,
    const InstallPlan& plan,
    const Incoming& incoming,
    std::string_view file_name,
    const std::filesystem::path& root,
    bool played
) {
    return from_kind(kind.prompts, &KindPrompts::question, plan, incoming, file_name, root, played);
}

PackagePrompt installed_prompt(
    const PackageKind& kind,
    const Incoming& incoming,
    const std::filesystem::path& folder,
    bool play_now
) {
    return from_kind(kind.prompts, &KindPrompts::installed, incoming, folder, play_now);
}

PackagePrompt updated_prompt(
    const PackageKind& kind,
    Change change,
    const Incoming& now,
    const InstalledPackage& before,
    const std::filesystem::path& folder,
    const ChangeResult& result,
    bool play_now
) {
    return from_kind(
        kind.prompts, &KindPrompts::updated, change, now, before, folder, result, play_now
    );
}

std::string refusal_text(const PackageKind& kind, const Problem& problem) {
    if (kind.prompts == nullptr || kind.prompts->refusal_text == nullptr)
        return {};
    return kind.prompts->refusal_text(problem);
}

PackagePrompt refused_prompt(
    const PackageKind& kind, std::string_view file_name, const Problem& problem, bool change_failed
) {
    return from_kind(kind.prompts, &KindPrompts::refused, file_name, problem, change_failed);
}

PackagePrompt unknown_kind_prompt(std::string_view file_name) {
    PackagePrompt made{};
    made.prompt.title = detail::tr("NOT INSTALLED");
    made.prompt.paragraphs.push_back(
        detail::text_of(
            detail::fill(
                "{file} is not a package Open Annihilation installs.",
                {{"file", std::string(file_name)}}
            )
        )
    );
    detail::add_button(made, detail::ok_caption, Answer::ok, true);
    detail::set_keys(made, Answer::ok, Answer::ok, Answer::ok);
    return made;
}

} // namespace oa::app::package_install
