// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/oamod/content_needs.hpp"

#include "oa/data/content_bundles.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace oa::formats::oamod {
namespace {

namespace bundles = oa::data::content_bundles;

/// The most ids one key lists. There are fewer built-in bundles than this.
constexpr std::size_t most_ids = 16;

/// The base a manifest used to name, before supportedGames.
constexpr std::string_view legacy_base_text = "ta-3.1c";

/// The base game that legacy name reads as.
constexpr std::string_view totala_id = "totala";

/// The key a package writes for the games it runs with.
constexpr std::string_view games_key = "supportedGames";

/// The key a package writes for the expansions it needs.
constexpr std::string_view expansions_key = "requiredExpansions";

/// Appends one problem.
///
/// @param problems the list
/// @param position where the value starts
/// @param path the key's path
/// @param message what is wrong
void add_problem(
    std::vector<KeyProblem>& problems, TextPosition position, std::string path, std::string message
) {
    problems.push_back(KeyProblem{position, std::move(path), std::move(message)});
}

/// The base games, in the built-in order.
///
/// @return their ids
std::vector<std::string> every_base_game() {
    std::vector<std::string> games;
    for (const bundles::Bundle& bundle : bundles::bundles()) {
        if (bundle.kind == bundles::Kind::base_game)
            games.emplace_back(bundle.id);
    }
    return games;
}

/// The games an omitted supportedGames means.
///
/// @param when_omitted the caller's default
/// @param legacy_base true when requires.base is ta-3.1c
/// @return those ids
std::vector<std::string> omitted_games(GamesWhenOmitted when_omitted, bool legacy_base) {
    if (legacy_base || when_omitted == GamesWhenOmitted::totala)
        return {std::string{totala_id}};
    return every_base_game();
}

/// Reads one id list.
///
/// @param node the value
/// @param key the key's name
/// @param empty_message what an empty list gets wrong; empty when an empty list is none
/// @param[out] ids the ids, when the whole list passed
/// @param problems the problems
/// @return true when the list passed
bool read_id_list(
    const Node& node,
    std::string_view key,
    std::string_view empty_message,
    std::vector<std::string>& ids,
    std::vector<KeyProblem>& problems
) {
    if (node.kind != NodeKind::sequence) {
        add_problem(
            problems, node.position, std::string{key}, std::string{key} + " must be a list of ids"
        );
        return false;
    }
    if (node.children.empty()) {
        if (!empty_message.empty())
            add_problem(problems, node.position, std::string{key}, std::string{empty_message});
        return empty_message.empty();
    }
    if (node.children.size() > most_ids) {
        add_problem(
            problems,
            node.position,
            std::string{key},
            std::string{key} + " lists " + std::to_string(node.children.size()) + " ids; at most " +
                std::to_string(most_ids)
        );
        return false;
    }
    std::vector<std::string> read;
    bool passed = true;
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        const Node& item = node.children[index];
        const std::string path = std::string{key} + "[" + std::to_string(index) + "]";
        if (item.kind != NodeKind::string || item.text.empty()) {
            add_problem(problems, item.position, path, path + " must be an id");
            passed = false;
            continue;
        }
        bool repeated = false;
        for (const std::string& earlier : read) {
            if (earlier == item.text) {
                repeated = true;
                break;
            }
        }
        if (repeated) {
            add_problem(problems, item.position, path, path + ": '" + item.text + "' is repeated");
            passed = false;
            continue;
        }
        read.push_back(item.text);
    }
    if (!passed)
        return false;
    ids = std::move(read);
    return true;
}

/// Tells whether an expansion extends one of the supported games.
///
/// @param expansion the expansion
/// @param games the supported games
/// @return true when it extends one of them
bool extends_supported(const bundles::Bundle& expansion, const std::vector<std::string>& games) {
    for (const std::string_view game : expansion.extends) {
        for (const std::string& supported : games) {
            if (supported == game)
                return true;
        }
    }
    return false;
}

/// Checks that each id is a bundle of the kind the key asks for.
///
/// @param key the key's name
/// @param kind the kind each id must be
/// @param kind_name the words a player reads for that kind
/// @param ids the ids
/// @param position where the list starts, for an id the table does not hold
/// @param problems the problems
/// @return true when every id is of that kind
bool check_kind(
    std::string_view key,
    bundles::Kind kind,
    std::string_view kind_name,
    const std::vector<std::string>& ids,
    TextPosition position,
    std::vector<KeyProblem>& problems
) {
    bool passed = true;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        const std::string& id = ids[index];
        const bundles::Bundle* bundle = bundles::find_bundle(id);
        if (bundle != nullptr && bundle->kind == kind)
            continue;
        const std::string path = std::string{key} + "[" + std::to_string(index) + "]";
        add_problem(
            problems, position, path, path + ": '" + id + "' is not " + std::string{kind_name}
        );
        passed = false;
    }
    return passed;
}

} // namespace

void read_content_needs(
    const Node& root,
    const Node* requires_block,
    GamesWhenOmitted when_omitted,
    ContentNeeds& needs,
    std::vector<KeyProblem>& problems
) {
    needs = {};
    if (requires_block != nullptr) {
        const Node* base = find_entry(*requires_block, "base");
        needs.legacy_base =
            base != nullptr && base->kind == NodeKind::string && base->text == legacy_base_text;
    }

    const Node* games = find_entry(root, games_key);
    std::vector<std::string> written_games;
    bool games_ok = true;
    if (games != nullptr) {
        games_ok = read_id_list(
            *games,
            games_key,
            "supportedGames must list at least one base game",
            written_games,
            problems
        );
        if (games_ok) {
            games_ok = check_kind(
                games_key,
                bundles::Kind::base_game,
                "a base game",
                written_games,
                games->position,
                problems
            );
        }
        needs.games_written = games_ok;
    }

    const Node* expansions = find_entry(root, expansions_key);
    std::vector<std::string> written_expansions;
    bool expansions_ok = true;
    if (expansions != nullptr) {
        expansions_ok = read_id_list(*expansions, expansions_key, {}, written_expansions, problems);
        if (expansions_ok) {
            expansions_ok = check_kind(
                expansions_key,
                bundles::Kind::expansion,
                "an expansion",
                written_expansions,
                expansions->position,
                problems
            );
        }
        needs.expansions_written = expansions_ok;
    }

    if (games == nullptr)
        needs.supported_games = omitted_games(when_omitted, needs.legacy_base);
    else if (games_ok)
        needs.supported_games = std::move(written_games);
    if (expansions_ok)
        needs.required_expansions = std::move(written_expansions);

    if (!games_ok || !expansions_ok || needs.supported_games.empty())
        return;
    const std::vector<std::string>& games_held = needs.supported_games;
    for (std::size_t index = 0; index < needs.required_expansions.size(); ++index) {
        const std::string& id = needs.required_expansions[index];
        const bundles::Bundle* bundle = bundles::find_bundle(id);
        if (bundle != nullptr && extends_supported(*bundle, games_held))
            continue;
        const std::string path = std::string{expansions_key} + "[" + std::to_string(index) + "]";
        const TextPosition position = expansions != nullptr ? expansions->position : TextPosition{};
        add_problem(
            problems, position, path, path + ": '" + id + "' extends none of the supported games"
        );
    }
}

} // namespace oa::formats::oamod
