// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Active tab's base game and expansions. A pill asks
// turn_content_expansion and keeps the result on the Library until the
// Library closes. Nothing here writes a preference.
#include "oa/ui/library/library.hpp"

#include "oa/data/content_bundles.hpp"
#include "oa/data/content_bundles/content_choice.hpp"
#include "oa/ui/library/text.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::library {

namespace {

namespace bundles = oa::data::content_bundles;

/// The letters drawn on the base game tile when its icon is absent.
constexpr std::string_view base_letters = "TA";

/// Tells whether a list holds an id.
///
/// @param ids the ids
/// @param id the id
/// @return true when it is among them
bool holds(const std::vector<std::string>& ids, std::string_view id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

/// Tells whether two id lists hold the same ids.
///
/// @param left one list
/// @param right the other
/// @return true when each id of either is in the other
bool same_ids(const std::vector<std::string>& left, const std::vector<std::string>& right) {
    if (left.size() != right.size())
        return false;
    for (const std::string& id : left) {
        if (!holds(right, id))
            return false;
    }
    return true;
}

/// Returns views of ids owned by `ids`, for the call that reads them.
///
/// @param ids the ids
/// @return views of them, in the same order
std::vector<std::string_view> id_views(const std::vector<std::string>& ids) {
    std::vector<std::string_view> views;
    views.reserve(ids.size());
    for (const std::string& id : ids)
        views.emplace_back(id);
    return views;
}

/// Returns the base game id the inputs name, or the default when they name none.
///
/// @param id the stored id
/// @return the id to show and to turn
std::string resolved_base(std::string_view id) {
    if (id.empty())
        return std::string(bundles::default_base_game_id);
    return std::string(id);
}

/// Tells whether a mod is played: an installed mod marked playing, or a mod
/// the inputs name outside the packages.
///
/// @param library the Library
/// @return true when one is
bool mod_is_played(const Library& library) {
    if (library.inputs.playing_mod)
        return true;
    for (const Installed& installed : library.inputs.installed) {
        if (installed.kind == Kind::mod && installed.playing)
            return true;
    }
    return false;
}

/// Tells whether an expansion extends a base game.
///
/// @param bundle the expansion
/// @param base the base game id
/// @return true when it extends that game
bool extends_game(const bundles::Bundle& bundle, std::string_view base) {
    for (const std::string_view game : bundle.extends) {
        if (game == base)
            return true;
    }
    return false;
}

/// The choice the pills are turning, and the base game it names.
struct PendingChoice {
    bundles::ContentChoice choice{};
    std::string base{};
};

/// Returns the choice a pill turns: the draft while it has been edited, and
/// the stored choice otherwise.
///
/// @param library the Library
/// @return the choice
PendingChoice pending_choice(const Library& library) {
    PendingChoice pending;
    const ContentDraft& draft = library.content;
    const ContentSnapshot& stored = library.inputs.content;
    if (draft.edited && !draft.base_game.empty())
        pending.base = draft.base_game;
    else if (!draft.seed_base.empty())
        pending.base = draft.seed_base;
    else
        pending.base = resolved_base(stored.base_id);
    pending.choice.base_game = pending.base;
    pending.choice.command_line = stored.locked;
    if (draft.edited)
        pending.choice.expansions = draft.expansions;
    else if (!draft.seed_base.empty())
        pending.choice.expansions = draft.seed_enabled;
    else
        pending.choice.expansions = stored.enabled;
    return pending;
}

/// Returns the expansions of one base game, in the built-in order.
///
/// @param base the base game id
/// @return those expansions
std::vector<const bundles::Bundle*> expansions_of(std::string_view base) {
    std::vector<const bundles::Bundle*> found;
    for (const bundles::Bundle& bundle : bundles::bundles()) {
        if (bundle.kind == bundles::Kind::expansion && extends_game(bundle, base))
            found.push_back(&bundle);
    }
    return found;
}

/// Returns the name a bundle shows. An unknown id is shown as itself.
///
/// @param id the bundle id
/// @return the name
std::string bundle_name(std::string_view id) {
    const bundles::Bundle* bundle = bundles::find_bundle(id);
    if (bundle == nullptr)
        return std::string(id);
    return std::string(bundle->english_name);
}

/// Returns the expansions that went off with a press, apart from the one pressed.
///
/// @param before the expansions that were on
/// @param after the expansions that are on
/// @param pressed the expansion the pill named
/// @return the others that went off, in the built-in order
std::vector<std::string> dependents_off(
    const std::vector<std::string>& before,
    const std::vector<std::string>& after,
    std::string_view pressed
) {
    std::vector<std::string> gone;
    for (const bundles::Bundle& bundle : bundles::bundles()) {
        if (bundle.kind != bundles::Kind::expansion || bundle.id == pressed)
            continue;
        if (holds(before, bundle.id) && !holds(after, bundle.id))
            gone.emplace_back(bundle.id);
    }
    return gone;
}

/// Writes a choice onto the draft and records the pill that made it.
///
/// @param[in,out] library the Library
/// @param choice the choice G04 returned
/// @param id the expansion
/// @param on that expansion is on afterwards
void keep_choice(
    Library& library, const bundles::ContentChoice& choice, std::string_view id, bool on
) {
    ContentDraft& draft = library.content;
    draft.base_game = choice.base_game;
    draft.expansions = choice.expansions;
    draft.edited =
        !same_ids(draft.expansions, draft.seed_enabled) || draft.base_game != draft.seed_base;
    draft.last_id = std::string(id);
    draft.last_on = on;
    draft.asking = false;
    draft.ask_id.clear();
    draft.proposed.clear();
    draft.turned_off.clear();
}

} // namespace

void note_content(Library& library) {
    const std::string base = resolved_base(library.inputs.content.base_id);
    const std::vector<std::string>& enabled = library.inputs.content.enabled;
    ContentDraft& draft = library.content;
    if (draft.edited && draft.seed_base == base && same_ids(draft.seed_enabled, enabled))
        return;
    draft = {};
    draft.seed_base = base;
    draft.seed_enabled = enabled;
}

ActiveContent active_content(const Library& library) {
    const TextHooks& text = library.text;
    const ContentSnapshot& stored = library.inputs.content;
    const PendingChoice pending = pending_choice(library);
    ActiveContent face;
    face.letters = std::string(base_letters);
    face.badge = library.inputs.base_game_badge ? &*library.inputs.base_game_badge : nullptr;

    const bool demo = pending.base == "totala-demo";
    const bool retail = pending.base == bundles::default_base_game_id;
    if (retail || demo) {
        face.name = std::string(shown(text, words::active_game_name));
    } else {
        const bundles::Bundle* game = bundles::find_bundle(pending.base);
        face.name = game != nullptr ? std::string(game->english_name) : pending.base;
    }
    std::string version;
    if (retail)
        version = std::string(shown(text, words::active_version));
    else if (demo)
        version = std::string(shown(text, words::active_version_demo));
    if (!version.empty() && !stored.source.empty())
        face.version_line = filled(
            text, words::active_version_source, {{"version", version}, {"source", stored.source}}
        );
    else
        face.version_line = version;
    if (stored.files_differ) {
        const int32_t count = *stored.files_differ;
        if (count <= 0) {
            face.check_line = std::string(shown(text, words::active_files_match));
            face.check_good = true;
        } else if (count == 1) {
            const std::string one = "1";
            face.check_line = filled(text, words::active_file_differs, {{"count", one}});
            face.check_good = false;
        } else {
            const std::string count_text = std::to_string(count);
            face.check_line = filled(text, words::active_files_differ, {{"count", count_text}});
            face.check_good = false;
        }
    }
    face.read = version.empty() ? face.name : face.name + " · " + version;

    const std::vector<const bundles::Bundle*> expansions = expansions_of(pending.base);
    const bool pills_on = !stored.locked && !library.inputs.in_match;
    int32_t on_count = 0;
    face.pills.reserve(expansions.size());
    for (const bundles::Bundle* bundle : expansions) {
        ExpansionPill pill;
        pill.id = std::string(bundle->id);
        const bool on = holds(pending.choice.expansions, bundle->id);
        const bool copied = holds(stored.copied, bundle->id);
        if (on)
            ++on_count;
        if (on)
            pill.face = ExpansionFace::on;
        else if (!copied)
            pill.face = ExpansionFace::add;
        else
            pill.face = ExpansionFace::off;
        if (pill.face == ExpansionFace::add)
            pill.text = filled(text, words::active_add_from, {{"name", bundle->english_name}});
        else
            pill.text = std::string(bundle->english_name);
        pill.enabled = pills_on;
        face.pills.push_back(std::move(pill));
    }

    if (expansions.empty()) {
        face.expansions_heading = std::string(shown(text, words::active_heading_expansions));
        face.hint = std::string(shown(text, words::active_no_expansions));
    } else {
        const std::string on_text = std::to_string(on_count);
        const std::string count_text = std::to_string(static_cast<int32_t>(expansions.size()));
        face.expansions_heading =
            filled(text, words::active_expansions_count, {{"on", on_text}, {"count", count_text}});
    }

    const ContentDraft& draft = library.content;
    if (draft.edited && !draft.last_id.empty()) {
        const std::string name = bundle_name(draft.last_id);
        face.hint = filled(
            text, draft.last_on ? words::active_turns_on : words::active_turns_off, {{"name", name}}
        );
    } else if (!expansions.empty()) {
        for (const bundles::Bundle* bundle : expansions) {
            if (holds(pending.choice.expansions, bundle->id) || bundle->needs.empty())
                continue;
            const std::string need = bundle_name(bundle->needs.front());
            face.hint =
                filled(text, words::active_needs, {{"name", bundle->english_name}, {"need", need}});
            break;
        }
    }
    return face;
}

ContentPress press_expansion(Library& library, std::string_view id) {
    ContentPress result;
    if (library.content.seed_base.empty() && !library.content.edited)
        note_content(library);
    if (library.content.asking)
        return result;

    const bundles::Bundle* bundle = bundles::find_bundle(id);
    const PendingChoice pending = pending_choice(library);
    if (bundle == nullptr || bundle->kind != bundles::Kind::expansion ||
        !extends_game(*bundle, pending.base))
        return result;
    if (library.inputs.content.locked || library.inputs.in_match)
        return result;

    const bool copied = holds(library.inputs.content.copied, id);
    const bool on_now = holds(pending.choice.expansions, id);
    if (!copied && !on_now) {
        result.kind = ContentPressKind::add;
        result.expansion = std::string(id);
        return result;
    }

    const std::vector<std::string_view> copied_ids = id_views(library.inputs.content.copied);
    const bool playing = mod_is_played(library);
    const std::vector<std::string_view> required_ids =
        playing ? id_views(library.inputs.content.mod_required) : std::vector<std::string_view>{};
    const bool turning_on = !on_now;
    const bundles::ContentChangeResult turned = bundles::turn_content_expansion(
        pending.choice, id, turning_on, copied_ids, library.inputs.in_match, playing, required_ids
    );
    if (turned.refusal != bundles::ContentRefusal::none || !turned.applied)
        return result;

    if (turned.mod_cleared == bundles::ModClearReason::required_expansion_off) {
        ContentDraft& draft = library.content;
        draft.asking = true;
        draft.ask_id = std::string(id);
        draft.proposed = turned.choice.expansions;
        draft.turned_off = dependents_off(pending.choice.expansions, turned.choice.expansions, id);
        result.kind = ContentPressKind::ask;
        result.expansion = std::string(id);
        return result;
    }

    const bool on_after = holds(turned.choice.expansions, id);
    keep_choice(library, turned.choice, id, on_after);
    result.kind = ContentPressKind::redraw;
    return result;
}

void accept_expansion_off(Library& library) {
    ContentDraft& draft = library.content;
    if (!draft.asking)
        return;
    if (draft.base_game.empty())
        draft.base_game = draft.seed_base.empty() ? resolved_base(library.inputs.content.base_id)
                                                  : draft.seed_base;
    draft.expansions = draft.proposed;
    draft.last_id = draft.ask_id;
    draft.last_on = false;
    draft.edited =
        !same_ids(draft.expansions, draft.seed_enabled) || draft.base_game != draft.seed_base;
    draft.asking = false;
    draft.ask_id.clear();
    draft.proposed.clear();
    draft.turned_off.clear();
}

void cancel_expansion_off(Library& library) {
    ContentDraft& draft = library.content;
    draft.asking = false;
    draft.ask_id.clear();
    draft.proposed.clear();
    draft.turned_off.clear();
}

} // namespace oa::ui::library
