// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The choice of base game and expansions, decided from the built-in bundles.
#include "oa/data/content_bundles/content_choice.hpp"

#include "oa/data/content_bundles.hpp"

#include <cstddef>
#include <utility>

namespace oa::data::content_bundles {
namespace {

/// Reports whether an expansion extends a base game.
///
/// @param bundle an expansion
/// @param game a base game id
/// @return true when `game` is one it extends
[[nodiscard]] bool extends_base(const Bundle& bundle, std::string_view game) noexcept {
    for (const std::string_view extended : bundle.extends) {
        if (extended == game)
            return true;
    }
    return false;
}

/// Reports whether an id is among the copied bundles.
///
/// @param bundle_id a bundle id
/// @param copied the copied ids
/// @return true when it is copied
[[nodiscard]] bool is_copied(std::string_view bundle_id, std::span<const std::string_view> copied) {
    for (const std::string_view id : copied) {
        if (id == bundle_id)
            return true;
    }
    return false;
}

/// Reports whether an expansion id is on.
///
/// @param expansions the expansions that are on
/// @param bundle_id the expansion
/// @return true when it is on
[[nodiscard]] bool is_on(const std::vector<std::string>& expansions, std::string_view bundle_id) {
    for (const std::string& id : expansions) {
        if (id == bundle_id)
            return true;
    }
    return false;
}

/// Reports whether every expansion a bundle needs is copied.
///
/// The base games it extends are not expansions, so they are not required
/// to be in the copied set.
///
/// @param bundle_id a bundle
/// @param copied the copied ids
/// @return true when no expansion it needs is missing
[[nodiscard]] bool
needs_are_copied(std::string_view bundle_id, std::span<const std::string_view> copied) {
    const std::string_view one[]{bundle_id};
    const std::vector<std::string_view> needs = requirement_closure(one);
    for (const std::string_view need : needs) {
        const Bundle* bundle = find_bundle(need);
        if (bundle != nullptr && bundle->kind == Kind::expansion && !is_copied(need, copied))
            return false;
    }
    return true;
}

/// Splits a stored expansions value on commas, trimming spaces and tabs.
///
/// Empty pieces are skipped. The pieces are not checked against the table.
///
/// @param text the preference value
/// @return the pieces, in the order written
[[nodiscard]] std::vector<std::string> split_lenient(std::string_view text) {
    std::vector<std::string> ids;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
        std::size_t from = start;
        std::size_t to = end;
        while (from < to && (text[from] == ' ' || text[from] == '\t'))
            ++from;
        while (to > from && (text[to - 1] == ' ' || text[to - 1] == '\t'))
            --to;
        if (from < to)
            ids.emplace_back(text.substr(from, to - from));
        if (comma == std::string_view::npos)
            break;
        start = comma + 1;
    }
    return ids;
}

/// Keeps the copied expansions of a base game, adding copied needs and
/// dropping an expansion whose need is not copied.
///
/// Waiting expansions are not added. The result is in the built-in order.
///
/// @param game the base game
/// @param named the ids asked for
/// @param copied the copied ids
/// @return the expansions that are on
[[nodiscard]] std::vector<std::string> close_copied(
    std::string_view game,
    const std::vector<std::string>& named,
    std::span<const std::string_view> copied
) {
    std::vector<std::string_view> seeds;
    for (const std::string& id : named) {
        const Bundle* bundle = find_bundle(id);
        if (bundle == nullptr || bundle->kind != Kind::expansion || !extends_base(*bundle, game))
            continue;
        if (!is_copied(bundle->id, copied))
            continue;
        seeds.push_back(bundle->id);
    }
    const std::vector<std::string_view> closed = requirement_closure(seeds);
    std::vector<std::string_view> candidates;
    for (const std::string_view id : closed) {
        const Bundle* bundle = find_bundle(id);
        if (bundle == nullptr || bundle->kind != Kind::expansion || !extends_base(*bundle, game))
            continue;
        if (!is_copied(id, copied))
            continue;
        candidates.push_back(id);
    }
    bool dropped = true;
    while (dropped) {
        dropped = false;
        std::vector<std::string_view> kept;
        for (const std::string_view id : candidates) {
            if (!needs_are_copied(id, copied)) {
                dropped = true;
                continue;
            }
            kept.push_back(id);
        }
        candidates = std::move(kept);
    }
    std::vector<std::string> expansions;
    expansions.reserve(candidates.size());
    for (const std::string_view id : candidates)
        expansions.emplace_back(id);
    return expansions;
}

/// Returns every copied expansion of a base game that can be on.
///
/// @param game the base game
/// @param copied the copied ids
/// @return those expansions, in the built-in order
[[nodiscard]] std::vector<std::string>
playable_expansions(std::string_view game, std::span<const std::string_view> copied) {
    std::vector<std::string> named;
    for (const Bundle& bundle : bundles()) {
        if (bundle.kind == Kind::expansion && extends_base(bundle, game) &&
            is_copied(bundle.id, copied))
            named.emplace_back(bundle.id);
    }
    return close_copied(game, named, copied);
}

/// Reports whether a required expansion went from on to off.
///
/// @param before the expansions that were on
/// @param after the expansions that are on
/// @param required the expansions a mod needs
/// @return true when one it needs went off
[[nodiscard]] bool required_went_off(
    const std::vector<std::string>& before,
    const std::vector<std::string>& after,
    std::span<const std::string_view> required
) {
    for (const std::string_view id : required) {
        if (is_on(before, id) && !is_on(after, id))
            return true;
    }
    return false;
}

/// A refusal that leaves the choice as it was.
///
/// @param choice the choice now
/// @param refusal why
/// @return the unchanged result
[[nodiscard]] ContentChangeResult refuse(const ContentChoice& choice, ContentRefusal refusal) {
    ContentChangeResult result;
    result.choice = choice;
    result.refusal = refusal;
    return result;
}

/// The expansions of a base game from a bundle list in built-in order.
///
/// The base game and anything that does not extend it are left out. An
/// expansion that is not copied is left out.
///
/// @param enabled bundles, base game included
/// @param game the base game
/// @param copied the copied ids
/// @return the expansions that stay on
[[nodiscard]] std::vector<std::string> filter_enabled(
    std::span<const std::string_view> enabled,
    std::string_view game,
    std::span<const std::string_view> copied
) {
    std::vector<std::string> expansions;
    for (const std::string_view id : enabled) {
        const Bundle* bundle = find_bundle(id);
        if (bundle == nullptr || bundle->kind != Kind::expansion || !extends_base(*bundle, game))
            continue;
        if (!is_copied(id, copied))
            continue;
        expansions.emplace_back(id);
    }
    return expansions;
}

/// The choice's bundles as views, the base game first.
///
/// The views address `choice`, which the caller keeps alive.
///
/// @param choice the choice
/// @param[out] storage the base game id, then each expansion; the views address it
/// @return views of `storage`
[[nodiscard]] std::vector<std::string_view>
enabled_views(const ContentChoice& choice, std::vector<std::string_view>& storage) {
    storage.clear();
    storage.push_back(choice.base_game);
    for (const std::string& id : choice.expansions)
        storage.push_back(id);
    return storage;
}

} // namespace

std::vector<std::string_view> present_bundles(std::span<const SourceFile> files) {
    std::vector<const ContentFile*> matched;
    matched.reserve(files.size());
    for (const SourceFile& source : files) {
        const FileMatch found = match_file(source.name, source.size);
        if (found.file != nullptr)
            matched.push_back(found.file);
    }
    std::vector<std::string_view> present;
    for (const Bundle& bundle : bundles()) {
        if (bundle.files.empty())
            continue;
        bool whole = true;
        for (const ContentFile& file : bundle.files) {
            bool hit = false;
            for (const ContentFile* found : matched) {
                if (found == &file) {
                    hit = true;
                    break;
                }
            }
            if (!hit) {
                whole = false;
                break;
            }
        }
        if (whole)
            present.push_back(bundle.id);
    }
    return present;
}

ContentChoice parse_content_argument(std::string_view text) {
    ContentChoice choice;
    choice.command_line = true;
    if (text.empty()) {
        choice.error = "--content requires a value";
        return choice;
    }
    if (text.find(' ') != std::string_view::npos || text.find('\t') != std::string_view::npos) {
        choice.error = "--content has a space in GAME[:EXPANSION,...]";
        return choice;
    }
    const std::size_t colon = text.find(':');
    const std::string_view game = colon == std::string_view::npos ? text : text.substr(0, colon);
    const std::string_view list =
        colon == std::string_view::npos ? std::string_view{} : text.substr(colon + 1);
    const Bundle* base = find_bundle(game);
    if (base == nullptr) {
        choice.error = "--content names an unknown base game: " + std::string(game);
        return choice;
    }
    if (base->kind != Kind::base_game) {
        choice.error = "--content names an expansion as the base game: " + std::string(game);
        return choice;
    }
    std::vector<std::string_view> named;
    if (!list.empty()) {
        std::size_t start = 0;
        while (start <= list.size()) {
            const std::size_t comma = list.find(',', start);
            if (comma == start || (comma == std::string_view::npos && start == list.size())) {
                choice.error = "--content has an empty expansion";
                return choice;
            }
            const std::string_view piece = comma == std::string_view::npos
                                               ? list.substr(start)
                                               : list.substr(start, comma - start);
            const Bundle* bundle = find_bundle(piece);
            if (bundle == nullptr) {
                choice.error = "--content names an unknown expansion: " + std::string(piece);
                return choice;
            }
            if (bundle->kind != Kind::expansion) {
                choice.error = "--content names a base game as an expansion: " + std::string(piece);
                return choice;
            }
            if (!extends_base(*bundle, game)) {
                choice.error =
                    "--content: " + std::string(piece) + " does not extend " + std::string(game);
                return choice;
            }
            named.push_back(bundle->id);
            if (comma == std::string_view::npos)
                break;
            start = comma + 1;
        }
    }
    choice.base_game = std::string(base->id);
    const std::vector<std::string_view> closed = requirement_closure(named);
    for (const std::string_view id : closed) {
        const Bundle* bundle = find_bundle(id);
        if (bundle == nullptr || bundle->kind != Kind::expansion)
            continue;
        if (!extends_base(*bundle, game)) {
            choice.base_game.clear();
            choice.error =
                "--content: " + std::string(id) + " does not extend " + std::string(game);
            return choice;
        }
        choice.expansions.emplace_back(id);
    }
    return choice;
}

ContentChoice resolve_content_choice(
    const StoredContent& stored,
    std::span<const std::string_view> copied,
    std::string_view command_line
) {
    if (!command_line.empty())
        return parse_content_argument(command_line);
    ContentChoice choice;
    const Bundle* base = nullptr;
    if (stored.base_game.has_value())
        base = find_bundle(*stored.base_game);
    const bool known = base != nullptr && base->kind == Kind::base_game;
    choice.migration = !stored.base_game.has_value() || !known;
    const std::string_view game = known ? base->id : default_base_game_id;
    choice.base_game = std::string(game);
    if (choice.migration || !stored.expansions.has_value())
        choice.expansions = playable_expansions(game, copied);
    else
        choice.expansions = close_copied(game, split_lenient(*stored.expansions), copied);
    return choice;
}

std::string expansions_text(std::span<const std::string> expansions) {
    std::string text;
    for (const std::string& id : expansions) {
        if (!text.empty())
            text.push_back(',');
        text += id;
    }
    return text;
}

ContentChangeResult turn_content_expansion(
    const ContentChoice& choice,
    std::string_view bundle_id,
    bool on,
    std::span<const std::string_view> copied,
    bool in_match,
    bool mod_active,
    std::span<const std::string_view> mod_required_expansions
) {
    if (in_match)
        return refuse(choice, ContentRefusal::in_match);
    if (choice.command_line)
        return refuse(choice, ContentRefusal::command_line);
    const Bundle* bundle = find_bundle(bundle_id);
    if (bundle == nullptr)
        return refuse(choice, ContentRefusal::unknown);
    if (bundle->kind != Kind::expansion || !extends_base(*bundle, choice.base_game))
        return refuse(choice, ContentRefusal::not_for_game);
    if (!on && !is_on(choice.expansions, bundle->id))
        return refuse(choice, ContentRefusal::none);
    if (on && (!is_copied(bundle->id, copied) || !needs_are_copied(bundle->id, copied)))
        return refuse(choice, ContentRefusal::not_copied);

    std::vector<std::string_view> storage;
    const std::vector<std::string_view> enabled = enabled_views(choice, storage);
    const std::vector<std::string_view> next =
        on ? after_turning_on(enabled, bundle->id) : after_turning_off(enabled, bundle->id);
    ContentChangeResult result;
    result.choice.base_game = choice.base_game;
    result.choice.expansions = filter_enabled(next, choice.base_game, copied);
    if (on && !is_on(result.choice.expansions, bundle->id))
        return refuse(choice, ContentRefusal::not_copied);
    result.applied = result.choice.expansions != choice.expansions;
    if (mod_active &&
        required_went_off(choice.expansions, result.choice.expansions, mod_required_expansions)) {
        result.mod_cleared = ModClearReason::required_expansion_off;
        result.applied = true;
    }
    return result;
}

ContentChangeResult choose_content_base_game(
    const ContentChoice& choice,
    std::string_view bundle_id,
    std::span<const std::string_view> copied,
    bool in_match,
    bool mod_active
) {
    if (in_match)
        return refuse(choice, ContentRefusal::in_match);
    if (choice.command_line)
        return refuse(choice, ContentRefusal::command_line);
    const Bundle* bundle = find_bundle(bundle_id);
    if (bundle == nullptr)
        return refuse(choice, ContentRefusal::unknown);
    if (bundle->kind != Kind::base_game)
        return refuse(choice, ContentRefusal::not_for_game);
    ContentChangeResult result;
    result.choice.base_game = std::string(bundle->id);
    result.choice.expansions = playable_expansions(bundle->id, copied);
    if (mod_active) {
        result.mod_cleared = ModClearReason::base_game_chosen;
        result.applied = true;
    }
    if (result.choice.base_game != choice.base_game ||
        result.choice.expansions != choice.expansions)
        result.applied = true;
    return result;
}

} // namespace oa::data::content_bundles
