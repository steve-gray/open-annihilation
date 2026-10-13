// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Active tab's base game and expansions. A pill asks
// turn_content_expansion and keeps the result on the Library until the
// Library closes. Nothing here writes a preference.
#include "oa/ui/library/library.hpp"

#include "oa/data/content_bundles.hpp"
#include "oa/data/content_bundles/content_choice.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/library/text.hpp"
#include "screen_controls.hpp"

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
    // A pill does not choose a base game. One that leaves the mod in place
    // clears an earlier unload, unless another base game is already pending.
    if (!draft.pending_base) {
        draft.unload_mod = false;
        draft.clear_reason = PendingClear::none;
    }
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
    library.chooser = {};
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
    face.change_enabled = pills_on && !library.inputs.mod_from_command_line;
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
    if (draft.pending_base) {
        face.hint = std::string(shown(text, words::base_applies));
    } else if (draft.edited && !draft.last_id.empty()) {
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
    draft.pending_base = false;
    draft.unload_mod = true;
    draft.clear_reason = PendingClear::required_expansion_off;
}

void cancel_expansion_off(Library& library) {
    ContentDraft& draft = library.content;
    draft.asking = false;
    draft.ask_id.clear();
    draft.proposed.clear();
    draft.turned_off.clear();
}

namespace {

/// Returns the name of the mod being played, when the pending choice has not
/// already unloaded it.
///
/// @param library the Library
/// @return the name; empty when no mod shows
std::string played_mod_name(const Library& library) {
    if (library.content.unload_mod)
        return {};
    for (const Installed& installed : library.inputs.installed) {
        if (installed.kind == Kind::mod && installed.playing && !installed.name.empty())
            return installed.name;
    }
    if (library.inputs.playing_mod && !library.inputs.playing_mod->name.empty())
        return library.inputs.playing_mod->name;
    if (mod_is_played(library))
        return std::string(shown(library.text, words::no_mod));
    return {};
}

/// Returns the base games, in the built-in order.
///
/// @return those bundles
std::vector<const bundles::Bundle*> base_games() {
    std::vector<const bundles::Bundle*> found;
    for (const bundles::Bundle& bundle : bundles::bundles()) {
        if (bundle.kind == bundles::Kind::base_game)
            found.push_back(&bundle);
    }
    return found;
}

/// Writes the chooser's rows from the inputs, keeping `marked`.
///
/// @param[in,out] library the Library
/// @param marked the row the radio marks
void fill_chooser(Library& library, std::string_view marked) {
    const TextHooks& text = library.text;
    const ContentSnapshot& stored = library.inputs.content;
    const PendingChoice pending = pending_choice(library);
    BaseChooser& chooser = library.chooser;
    const std::string problem = chooser.problem;
    chooser = {};
    chooser.open = true;
    chooser.problem = problem;
    chooser.marked = std::string(marked);
    const std::string mounted = resolved_base(stored.base_id);
    for (const bundles::Bundle* game : base_games()) {
        BaseGameRow row;
        row.id = std::string(game->id);
        const bool demo = game->id == "totala-demo";
        const bool retail = game->id == bundles::default_base_game_id;
        row.letters = demo ? "DEMO" : "TA";
        if (retail)
            row.name = std::string(shown(text, words::active_game_name));
        else if (demo)
            row.name = std::string(shown(text, words::base_demo_name));
        else
            row.name = std::string(game->english_name);
        if (retail)
            row.version = std::string(shown(text, words::active_version));
        else if (demo)
            row.version = std::string(shown(text, words::base_demo_version));
        const bool copied = holds(stored.copied, game->id);
        if (game->id == pending.base)
            row.state = BaseGameState::in_use;
        else if (copied)
            row.state = BaseGameState::copied;
        else
            row.state = BaseGameState::not_added;
        if (row.state == BaseGameState::in_use)
            row.state_text = std::string(shown(text, words::chip_in_use));
        else if (row.state == BaseGameState::copied)
            row.state_text = std::string(shown(text, words::base_copied));
        else
            row.state_text = std::string(shown(text, words::base_not_added));
        if (row.state == BaseGameState::not_added) {
            row.detail =
                std::string(shown(text, demo ? words::base_demo_detail : words::base_ask_game));
        } else if (game->id == mounted && !stored.source.empty()) {
            row.detail = filled(text, words::base_copied_from, {{"source", stored.source}});
            if (stored.files_differ) {
                std::string check;
                const int32_t count = *stored.files_differ;
                if (count <= 0) {
                    check = std::string(shown(text, words::active_files_match));
                } else if (count == 1) {
                    check = filled(text, words::active_file_differs, {{"count", "1"}});
                } else {
                    check = filled(
                        text, words::active_files_differ, {{"count", std::to_string(count)}}
                    );
                }
                row.detail += " · ";
                // The check line is already looked up. Fold its first letter
                // so it reads as the rest of the source line.
                if (!check.empty() && check.front() >= 'A' && check.front() <= 'Z')
                    check.front() = static_cast<char>(check.front() - 'A' + 'a');
                row.detail += check;
            }
        }
        if (game->id == pending.base) {
            for (const bundles::Bundle* expansion : expansions_of(game->id)) {
                if (holds(pending.choice.expansions, expansion->id))
                    row.expansions.emplace_back(expansion->english_name);
            }
        } else if (copied) {
            for (const bundles::Bundle* expansion : expansions_of(game->id)) {
                if (holds(stored.copied, expansion->id))
                    row.expansions.emplace_back(expansion->english_name);
            }
        }
        row.marked = row.id == chooser.marked;
        chooser.rows.push_back(std::move(row));
    }
    if (chooser.marked != pending.base) {
        const std::string mod = played_mod_name(library);
        if (mod.empty())
            chooser.note = std::string(shown(text, words::base_note_plain));
        else
            chooser.note = filled(text, words::base_note_mod, {{"mod", mod}});
    }
}

} // namespace

BaseOpen open_base_chooser(Library& library) {
    BaseOpen opened;
    if (library.inputs.content.locked || library.inputs.in_match ||
        library.inputs.mod_from_command_line || library.content.asking)
        return opened;
    if (library.content.seed_base.empty() && !library.content.edited)
        note_content(library);
    const PendingChoice pending = pending_choice(library);
    fill_chooser(library, pending.base);
    opened.opened = true;
    for (std::size_t index = 0;
         index < library.chooser.rows.size() && index < controls::most_base_rows;
         ++index) {
        if (library.chooser.rows[index].id == pending.base) {
            opened.focus = controls::first_base_row + static_cast<int32_t>(index);
            break;
        }
    }
    return opened;
}

void close_base_chooser(Library& library) {
    library.chooser = {};
}

void mark_base_game(Library& library, std::string_view id) {
    if (!library.chooser.open)
        return;
    bool known = false;
    for (const BaseGameRow& row : library.chooser.rows) {
        if (row.id == id)
            known = true;
    }
    if (!known)
        return;
    const std::string problem = library.chooser.problem;
    fill_chooser(library, id);
    library.chooser.problem = problem;
}

void refresh_base_chooser(Library& library) {
    if (!library.chooser.open)
        return;
    const std::string marked = library.chooser.marked;
    const std::string problem = library.chooser.problem;
    fill_chooser(library, marked);
    library.chooser.problem = problem;
}

void note_base_copy_problem(Library& library, std::string_view problem) {
    if (!library.chooser.open)
        return;
    library.chooser.problem = std::string(problem);
}

BaseUse use_marked_base_game(Library& library) {
    BaseUse used;
    if (!library.chooser.open)
        return used;
    if (library.inputs.content.locked || library.inputs.in_match ||
        library.inputs.mod_from_command_line) {
        used.kind = BaseUseKind::refused;
        return used;
    }
    if (library.content.seed_base.empty() && !library.content.edited)
        note_content(library);
    const std::string marked = library.chooser.marked;
    const PendingChoice pending = pending_choice(library);
    if (marked.empty() || marked == pending.base) {
        close_base_chooser(library);
        used.kind = BaseUseKind::none;
        return used;
    }
    if (!holds(library.inputs.content.copied, marked)) {
        used.kind = BaseUseKind::needs_source;
        used.id = marked;
        return used;
    }
    const std::vector<std::string_view> copied_ids = id_views(library.inputs.content.copied);
    const bool playing = mod_is_played(library) && !library.content.unload_mod;
    const bundles::ContentChangeResult turned = bundles::choose_content_base_game(
        pending.choice, marked, copied_ids, library.inputs.in_match, playing
    );
    if (turned.refusal != bundles::ContentRefusal::none || !turned.applied) {
        used.kind = BaseUseKind::refused;
        return used;
    }
    ContentDraft& draft = library.content;
    const PendingClear owed = draft.clear_reason;
    draft.base_game = turned.choice.base_game;
    draft.expansions = turned.choice.expansions;
    draft.edited =
        !same_ids(draft.expansions, draft.seed_enabled) || draft.base_game != draft.seed_base;
    draft.last_id.clear();
    draft.last_on = false;
    draft.asking = false;
    draft.ask_id.clear();
    draft.proposed.clear();
    draft.turned_off.clear();
    draft.pending_base = draft.base_game != draft.seed_base;
    if (turned.mod_cleared == bundles::ModClearReason::base_game_chosen ||
        owed == PendingClear::required_expansion_off) {
        draft.unload_mod = true;
        if (owed == PendingClear::required_expansion_off)
            draft.clear_reason = PendingClear::required_expansion_off;
        else
            draft.clear_reason = PendingClear::base_game_chosen;
    }
    close_base_chooser(library);
    used.kind = BaseUseKind::applied;
    return used;
}

} // namespace oa::ui::library
