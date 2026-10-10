// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's entries built from its inputs, browsing and selection, and
// what the details and the footer say about them.
#include "oa/ui/library/library.hpp"

#include "oa/data/mod_profile/registry.hpp"
#include "oa/ui/library/text.hpp"
#include "visible.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::library {

namespace {

namespace hacks = data::mod_profile::registry;

using Phase = QueueItem::Phase;

/// A whole percentage.
constexpr uint64_t percent_whole = 100;

/// Finds a registry by its id.
///
/// @param inputs the inputs
/// @param id the registry's id
/// @return the registry, or null when the inputs do not name it
const Registry* find_registry(const Inputs& inputs, std::string_view id) {
    for (const Registry& registry : inputs.registries)
        if (registry.id == id)
            return &registry;
    return nullptr;
}

/// Tells whether a queue item is still to finish.
///
/// @param phase the item's phase
/// @return true from waiting to installing, and while held
bool pending(Phase phase) {
    return phase != Phase::failed && phase != Phase::installed;
}

/// Tells whether a queue item is being worked on now.
///
/// @param phase the item's phase
/// @return true while downloading, verifying, checking or installing
bool active(Phase phase) {
    return phase == Phase::downloading || phase == Phase::verifying || phase == Phase::checking ||
           phase == Phase::installing;
}

/// Lists the hacks a listing turns on that this Open Annihilation does not carry out.
///
/// @param listing the listing
/// @return the hack ids, in catalogue order
std::vector<std::string_view> missing_hacks(const Listing& listing) {
    std::vector<std::string_view> missing;
    for (const std::string& id : listing.hack_ids) {
        const hacks::Entry* entry = hacks::find_entry(id);
        if (entry == nullptr || !entry->implemented)
            missing.push_back(id);
    }
    return missing;
}

/// Names missing hacks: up to named_missing_hacks of them, then how many more.
///
/// @param missing the hack ids
/// @param text how texts are looked up
/// @return the list, such as "a.b, c.d, e.f · 2 more"
std::string
missing_hacks_text(const std::vector<std::string_view>& missing, const TextHooks& text) {
    std::string named;
    const std::size_t shown_count = std::min(missing.size(), named_missing_hacks);
    for (std::size_t index = 0; index < shown_count; ++index) {
        if (index != 0)
            named += shown(text, words::list_separator);
        named += missing[index];
    }
    if (missing.size() <= named_missing_hacks)
        return named;
    const std::string more = std::to_string(missing.size() - named_missing_hacks);
    return filled(text, words::hacks_and_more, {{"hacks", named}, {"count", more}});
}

/// Writes why GET or UPDATE is off for an entry.
///
/// The reasons are tried in order: a game is being played; the registry's
/// downloads are off; for GET, the engine requirement, the base game and
/// the hacks; for UPDATE, what the update note says.
///
/// @param library the Library
/// @param entry the entry, with its state and registry fields set
/// @return the reason; empty when nothing blocks it
std::string blocked_reason(const Library& library, const Entry& entry) {
    const TextHooks& text = library.text;
    const Inputs& inputs = library.inputs;
    if (entry.state != State::get && entry.state != State::update)
        return {};
    if (inputs.in_match)
        return std::string(shown(text, words::blocked_in_match));
    const Registry* registry = find_registry(inputs, entry.id.registry);
    if (registry != nullptr && !registry->downloads_on) {
        if (!registry->downloads_off_reason.empty())
            return registry->downloads_off_reason;
        return filled(text, words::blocked_downloads_off, {{"registry", entry.registry_name}});
    }
    if (entry.state == State::update) {
        const UpdateNote& update = *entry.update;
        switch (update.blocked) {
        case UpdateNote::Blocked::none:
            return {};
        case UpdateNote::Blocked::engine:
            return filled(text, words::blocked_engine, {{"range", update.blocked_detail}});
        case UpdateNote::Blocked::hacks:
            return filled(text, words::blocked_hacks, {{"hacks", update.blocked_detail}});
        case UpdateNote::Blocked::base:
            return filled(text, words::blocked_base, {{"base", update.blocked_detail}});
        }
        return {};
    }
    const Listing& listing = *entry.listing;
    if (const std::optional<formats::oamod::EngineRange> range =
            formats::oamod::parse_engine_range(listing.requires_engine)) {
        if (!formats::oamod::engine_range_met(*range, inputs.engine))
            return filled(text, words::blocked_engine, {{"range", engine_text(*range, text)}});
    }
    if (!listing.base.empty() && listing.base != supported_base)
        return filled(text, words::blocked_base, {{"base", listing.base}});
    const std::vector<std::string_view> missing = missing_hacks(listing);
    if (!missing.empty())
        return filled(text, words::blocked_hacks, {{"hacks", missing_hacks_text(missing, text)}});
    return {};
}

/// Sets an entry's state, registry fields and whether it can act.
///
/// @param library the Library whose inputs the entry points into
/// @param[in,out] entry the entry, with its pointers set
void settle_entry(const Library& library, Entry& entry) {
    if (entry.update != nullptr)
        entry.state = State::update;
    else if (entry.installed != nullptr && entry.installed->playing)
        entry.state = State::playing;
    else if (entry.installed != nullptr)
        entry.state = State::installed;
    else
        entry.state = State::get;

    if (!entry.id.registry.empty()) {
        const Registry* registry = find_registry(library.inputs, entry.id.registry);
        entry.built_in = registry != nullptr && registry->built_in;
        entry.not_reviewed = !entry.built_in;
        entry.registry_name = registry != nullptr ? registry->name : entry.id.registry;
    }
    entry.blocked = blocked_reason(library, entry);
    entry.can_act = entry.blocked.empty();
}

/// Makes the selection one of the visible entries.
///
/// A selection that still shows stays. Otherwise the first visible entry
/// is selected, or nothing when none shows, and the details close.
///
/// @param[in,out] library the Library, its visible list up to date
void settle_selection(Library& library) {
    if (library.selected) {
        for (const std::size_t index : library.visible)
            if (library.entries[index].id == *library.selected)
                return;
    }
    if (library.visible.empty())
        library.selected.reset();
    else
        library.selected = library.entries[library.visible.front()].id;
    library.details_open = false;
}

/// Recomputes the visible entries and keeps the selection among them.
///
/// @param[in,out] library the Library
void recompute(Library& library) {
    update_visible(library);
    settle_selection(library);
}

/// Writes a queue item's name and version.
///
/// @param item the item
/// @param text how texts are looked up
/// @return such as "Fixture Mod 4.9"; the name alone when it has no version
std::string package_text(const QueueItem& item, const TextHooks& text) {
    if (item.version.empty())
        return item.name;
    return filled(text, words::queue_package, {{"name", item.name}, {"version", item.version}});
}

/// Names a queue item's phase in one or a few words.
///
/// @param phase the phase
/// @param text how texts are looked up
/// @return the words, such as "checking"
std::string_view phase_text(Phase phase, const TextHooks& text) {
    switch (phase) {
    case Phase::waiting:
        return shown(text, words::phase_waiting);
    case Phase::downloading:
        return shown(text, words::phase_downloading);
    case Phase::verifying:
        return shown(text, words::phase_verifying);
    case Phase::checking:
        return shown(text, words::phase_checking);
    case Phase::installing:
        return shown(text, words::phase_installing);
    case Phase::held:
        return shown(text, words::phase_held);
    case Phase::failed:
        return shown(text, words::phase_failed);
    case Phase::installed:
        return shown(text, words::phase_installed);
    }
    return {};
}

/// Picks the queue item the footer's first line describes.
///
/// @param queue the queue
/// @return its index; the queue is not empty
std::size_t headline_item(const std::vector<QueueItem>& queue) {
    const auto first_where = [&](auto&& wanted) -> std::optional<std::size_t> {
        for (std::size_t index = 0; index < queue.size(); ++index)
            if (wanted(queue[index].phase))
                return index;
        return std::nullopt;
    };
    if (const auto index = first_where([](Phase phase) { return phase == Phase::downloading; }))
        return *index;
    if (const auto index = first_where(active))
        return *index;
    if (const auto index = first_where([](Phase phase) { return phase == Phase::failed; }))
        return *index;
    if (const auto index = first_where([](Phase phase) { return phase == Phase::held; }))
        return *index;
    if (const auto index = first_where([](Phase phase) { return phase == Phase::waiting; }))
        return *index;
    return queue.size() - 1;
}

/// Writes the footer's first line for a queue item.
///
/// @param item the item
/// @param text how texts are looked up
/// @return the line
std::string first_queue_line(const QueueItem& item, const TextHooks& text) {
    switch (item.phase) {
    case Phase::waiting:
        return filled(text, words::queue_waiting, {{"package", package_text(item, text)}});
    case Phase::downloading:
        if (item.total_bytes == 0)
            return filled(
                text, words::queue_downloading_unsized, {{"package", package_text(item, text)}}
            );
        return filled(
            text,
            words::queue_downloading,
            {{"package", package_text(item, text)},
             {"progress", progress_text(item.done_bytes, item.total_bytes, text)}}
        );
    case Phase::verifying:
        return filled(text, words::queue_verifying, {{"name", item.name}});
    case Phase::checking:
        return filled(text, words::queue_checking, {{"name", item.name}});
    case Phase::installing:
        return filled(text, words::queue_installing, {{"name", item.name}});
    case Phase::held:
        return filled(text, words::queue_held, {{"name", item.name}});
    case Phase::failed:
        if (item.retryable)
            return filled(text, words::queue_failed_retry, {{"problem", item.problem}});
        return item.problem;
    case Phase::installed:
        switch (item.update_rules) {
        case UpdateNote::Rules::same:
            break;
        case UpdateNote::Rules::changes:
            return filled(
                text,
                words::queue_updated_changes,
                {{"name", item.name}, {"new", item.update_to}, {"old", item.update_from}}
            );
        case UpdateNote::Rules::unknown:
            return filled(
                text,
                words::queue_updated_unknown,
                {{"name", item.name}, {"new", item.update_to}, {"old", item.update_from}}
            );
        }
        return filled(text, words::queue_installed, {{"name", item.name}});
    }
    return {};
}

/// Writes the footer's line for a small screen.
///
/// @param item the item the first line describes
/// @param text how texts are looked up
/// @return the line
std::string compact_queue_line(const QueueItem& item, const TextHooks& text) {
    if (item.phase == Phase::downloading) {
        uint64_t percent = 0;
        if (item.total_bytes != 0 && item.done_bytes >= item.total_bytes)
            percent = percent_whole;
        else if (
            item.total_bytes != 0 &&
            item.done_bytes <= std::numeric_limits<uint64_t>::max() / percent_whole
        )
            percent = item.done_bytes * percent_whole / item.total_bytes;
        else if (item.total_bytes != 0)
            percent = item.done_bytes / (item.total_bytes / percent_whole);
        const std::string number = std::to_string(percent);
        return filled(text, words::queue_percent, {{"name", item.name}, {"percent", number}});
    }
    if (item.phase == Phase::installed && item.update_rules != UpdateNote::Rules::same)
        return filled(text, words::queue_rules_changed, {{"name", item.name}});
    return filled(
        text, words::queue_phase, {{"name", item.name}, {"phase", phase_text(item.phase, text)}}
    );
}

/// Writes the line under an entry's name while its queue item is unfinished.
///
/// @param item the item
/// @param text how texts are looked up
/// @return the line
std::string queue_status(const QueueItem& item, const TextHooks& text) {
    switch (item.phase) {
    case Phase::waiting:
        return std::string(shown(text, words::status_waiting));
    case Phase::downloading:
        if (item.total_bytes == 0)
            return std::string(shown(text, words::status_downloading_unsized));
        return filled(
            text,
            words::status_downloading,
            {{"progress", progress_text(item.done_bytes, item.total_bytes, text)}}
        );
    case Phase::verifying:
        return std::string(shown(text, words::status_verifying));
    case Phase::checking:
        return std::string(shown(text, words::status_checking));
    case Phase::installing:
        return std::string(shown(text, words::status_installing));
    case Phase::held:
        return filled(text, words::queue_held, {{"name", item.name}});
    case Phase::failed:
        if (item.problem.empty())
            return std::string(shown(text, words::status_failed));
        return item.problem;
    case Phase::installed:
        break;
    }
    return {};
}

/// Writes the Rules fact of an update.
///
/// @param library the Library
/// @param entry the entry, with an update
/// @return the fact
Fact update_rules_fact(const Library& library, const Entry& entry) {
    const TextHooks& text = library.text;
    const UpdateNote& update = *entry.update;
    const ChangeTexts names = change_texts(*entry.installed, update, text);
    const std::string from = update.from_rules ? rules_text(*update.from_rules) : std::string();
    const std::string to = update.to_rules ? rules_text(*update.to_rules) : std::string();
    Fact fact;
    fact.long_label = shown(text, words::fact_rules);
    fact.short_label = fact.long_label;
    switch (update.rules) {
    case UpdateNote::Rules::same:
        fact.long_value = from.empty() || to.empty()
                              ? std::string(shown(text, words::rules_same_short))
                              : filled(text, words::rules_same, {{"from", from}, {"to", to}});
        fact.short_value = shown(text, words::rules_same_short);
        break;
    case UpdateNote::Rules::changes:
        fact.long_value = filled(
            text,
            words::rules_changes,
            {{"from", from}, {"to", to}, {"old", names.from}, {"new", names.to}}
        );
        fact.short_value = filled(text, words::rules_changes_short, {{"old", names.from_short}});
        fact.mark = Mark::warn;
        break;
    case UpdateNote::Rules::unknown:
        fact.long_value =
            filled(text, words::rules_unknown, {{"old", names.from}, {"new", names.to}});
        fact.short_value = shown(text, words::rules_unknown_short);
        fact.mark = Mark::warn;
        break;
    }
    return fact;
}

/// Makes a fact whose long and short forms share a label.
///
/// @param label the label, looked up
/// @param long_value the long value
/// @param short_value the short value
/// @param mark how the value is marked
/// @return the fact
Fact fact_of(std::string_view label, std::string long_value, std::string short_value, Mark mark) {
    Fact fact;
    fact.long_label = label;
    fact.short_label = label;
    fact.long_value = std::move(long_value);
    fact.short_value = std::move(short_value);
    fact.mark = mark;
    return fact;
}

} // namespace

void refresh(Library& library, Inputs inputs) {
    library.inputs = std::move(inputs);
    library.entries.clear();
    const Inputs& in = library.inputs;
    std::map<EntryId, std::size_t> by_id;
    const auto entry_for = [&](const EntryId& id) -> Entry& {
        const auto [found, added] = by_id.emplace(id, library.entries.size());
        if (added) {
            Entry entry;
            entry.id = id;
            library.entries.push_back(std::move(entry));
        }
        return library.entries[found->second];
    };
    for (const Listing& listing : in.listings) {
        Entry& entry = entry_for({listing.registry, listing.kind, listing.key});
        if (entry.listing == nullptr)
            entry.listing = &listing;
    }
    for (const Installed& installed : in.installed) {
        Entry& entry = entry_for({installed.registry, installed.kind, installed.key});
        if (entry.installed == nullptr)
            entry.installed = &installed;
    }
    for (const UpdateNote& update : in.updates) {
        const auto found = by_id.find({update.registry, update.kind, update.key});
        if (found == by_id.end())
            continue;
        Entry& entry = library.entries[found->second];
        if (entry.installed != nullptr && entry.update == nullptr)
            entry.update = &update;
    }
    for (const QueueItem& item : in.queue) {
        const auto found = by_id.find({item.registry, item.kind, item.key});
        if (found != by_id.end())
            library.entries[found->second].queued = &item;
    }
    for (Entry& entry : library.entries)
        settle_entry(library, entry);
    for (Entry& entry : library.entries) {
        if (entry.state != State::get)
            continue;
        for (const Installed& installed : in.installed) {
            if (installed.kind != entry.id.kind || installed.key != entry.id.key)
                continue;
            if (installed.registry.empty() || installed.registry == entry.id.registry)
                continue;
            const Registry* registry = find_registry(in, installed.registry);
            entry.replaces_registry_name =
                registry != nullptr ? registry->name : installed.registry;
            break;
        }
    }
    recompute(library);
}

void set_tab(Library& library, Tab tab) {
    library.tab = tab;
    recompute(library);
}

void set_filter(Library& library, Filter filter) {
    library.filter = filter;
    recompute(library);
}

void set_tag(Library& library, std::string_view tag) {
    library.tag = tag;
    recompute(library);
}

void set_query(Library& library, std::string_view query) {
    library.query = capped_query(query);
    recompute(library);
}

void select(Library& library, const EntryId& id) {
    update_visible(library);
    for (const std::size_t index : library.visible) {
        if (library.entries[index].id == id) {
            library.selected = id;
            break;
        }
    }
    settle_selection(library);
}

void move_selection(Library& library, int delta) {
    update_visible(library);
    if (library.visible.empty()) {
        settle_selection(library);
        return;
    }
    std::optional<std::size_t> at;
    if (library.selected) {
        for (std::size_t row = 0; row < library.visible.size(); ++row)
            if (library.entries[library.visible[row]].id == *library.selected)
                at = row;
    }
    if (!at) {
        settle_selection(library);
        return;
    }
    const auto last = static_cast<int64_t>(library.visible.size()) - 1;
    const int64_t row = std::clamp(static_cast<int64_t>(*at) + delta, int64_t{0}, last);
    library.selected = library.entries[library.visible[static_cast<std::size_t>(row)]].id;
}

void open_details(Library& library) {
    recompute(library);
    library.details_open = library.selected.has_value();
}

void close_details(Library& library) {
    recompute(library);
    library.details_open = false;
}

const Entry* find_entry(const Library& library, const EntryId& id) {
    for (const Entry& entry : library.entries)
        if (entry.id == id)
            return &entry;
    return nullptr;
}

const Entry* selected_entry(const Library& library) {
    if (!library.selected)
        return nullptr;
    return find_entry(library, *library.selected);
}

std::string_view entry_name(const Entry& entry) {
    if (entry.listing != nullptr)
        return entry.listing->name;
    if (entry.installed != nullptr)
        return entry.installed->name;
    return entry.id.key;
}

std::vector<std::pair<std::string, std::size_t>> tags_in_tab(const Library& library) {
    std::map<std::string, std::size_t> counts;
    for (const Entry& entry : library.entries) {
        if (entry.listing == nullptr || !shown_in_tab(library, entry, false))
            continue;
        for (const std::string& tag : entry.listing->tags)
            ++counts[tag];
    }
    return {counts.begin(), counts.end()};
}

std::size_t update_count(const Library& library) {
    return static_cast<std::size_t>(
        std::count_if(library.entries.begin(), library.entries.end(), [](const Entry& entry) {
            return entry.state == State::update && entry.can_act;
        })
    );
}

std::vector<ActionButton> entry_actions(const Library& library, const Entry& entry) {
    const TextHooks& text = library.text;
    const bool in_match = library.inputs.in_match;
    const QueueItem* item = entry.queued;
    std::vector<ActionButton> buttons;
    const auto add = [&](Action action, bool enabled) {
        buttons.push_back({action, enabled, action_text(action, text)});
    };
    if (entry.state == State::get || entry.state == State::update) {
        if (item != nullptr && pending(item->phase))
            add(Action::cancel, true);
        else if (item != nullptr && item->phase == Phase::failed && item->retryable)
            add(Action::retry, entry.can_act);
        else
            add(entry.state == State::get ? Action::get : Action::update, entry.can_act);
    }
    const Installed* installed = entry.installed;
    if (installed != nullptr && entry.id.kind == Kind::mod && !installed->playing)
        add(Action::play_now, !in_match);
    if (installed != nullptr && installed->kept)
        add(Action::roll_back, !in_match);
    if (installed != nullptr)
        add(Action::open_folder, true);
    if (entry.listing != nullptr && !entry.listing->homepage.empty())
        add(Action::homepage, true);
    return buttons;
}

std::vector<Fact> facts(const Library& library, const Entry& entry) {
    const TextHooks& text = library.text;
    const Listing* listing = entry.listing;
    std::vector<Fact> list;

    if (listing != nullptr) {
        const std::string size = size_text(listing->size, text);
        if (entry.update != nullptr) {
            const std::string download = size_text(entry.update->size, text);
            list.push_back(fact_of(
                shown(text, words::fact_size),
                filled(text, words::size_with_download, {{"size", size}, {"download", download}}),
                filled(text, words::size_download_short, {{"download", download}}),
                Mark::none
            ));
        } else {
            list.push_back(fact_of(shown(text, words::fact_size), size, size, Mark::none));
        }

        if ((listing->base.empty() && listing->kind == Kind::mod) ||
            listing->base == supported_base) {
            list.push_back(fact_of(
                shown(text, words::fact_base),
                std::string(shown(text, words::base_supported)),
                std::string(shown(text, words::base_supported_short)),
                Mark::none
            ));
        } else if (!listing->base.empty()) {
            list.push_back(
                fact_of(shown(text, words::fact_base), listing->base, listing->base, Mark::warn)
            );
        }

        if (const std::optional<formats::oamod::EngineRange> range =
                formats::oamod::parse_engine_range(listing->requires_engine)) {
            const bool met = formats::oamod::engine_range_met(*range, library.inputs.engine);
            list.push_back(fact_of(
                shown(text, words::fact_needs),
                filled(text, words::needs_engine, {{"range", engine_text(*range, text)}}),
                filled(
                    text, words::needs_engine_short, {{"range", engine_short_text(*range, text)}}
                ),
                met ? Mark::good : Mark::warn
            ));
        }

        if (listing->kind == Kind::mod) {
            const std::string game = std::to_string(listing->game_hacks);
            const std::string view = std::to_string(listing->view_hacks);
            std::string counts;
            std::string counts_short;
            if (listing->game_hacks == 0 && listing->view_hacks == 0 && listing->hack_ids.empty()) {
                counts = shown(text, words::hacks_none);
                counts_short = counts;
            } else {
                counts = filled(text, words::hacks_counts, {{"game", game}, {"view", view}});
                counts_short =
                    filled(text, words::hacks_counts_short, {{"game", game}, {"view", view}});
            }
            const std::vector<std::string_view> missing = missing_hacks(*listing);
            if (missing.empty()) {
                list.push_back(
                    fact_of(shown(text, words::fact_hacks), counts, counts_short, Mark::none)
                );
            } else {
                const std::string named = filled(
                    text, words::blocked_hacks, {{"hacks", missing_hacks_text(missing, text)}}
                );
                const std::string count = std::to_string(missing.size());
                list.push_back(fact_of(
                    shown(text, words::fact_hacks),
                    filled(
                        text, words::hacks_with_missing, {{"counts", counts}, {"missing", named}}
                    ),
                    filled(
                        text,
                        words::hacks_with_missing_short,
                        {{"counts", counts_short}, {"count", count}}
                    ),
                    Mark::warn
                ));
            }
        }
    }

    if (entry.id.kind == Kind::mod) {
        if (entry.update != nullptr && entry.installed != nullptr) {
            list.push_back(update_rules_fact(library, entry));
        } else {
            std::optional<base::sha256::Digest> rules;
            if (entry.installed != nullptr)
                rules = entry.installed->rules;
            else if (listing != nullptr)
                rules = listing->sim_hash;
            if (rules) {
                const std::string hash = rules_text(*rules);
                list.push_back(fact_of(shown(text, words::fact_rules), hash, hash, Mark::none));
            }
        }
    }

    if (listing != nullptr && listing->kind == Kind::map_pack) {
        const std::string count = std::to_string(listing->maps.size());
        const std::string maps = filled(
            text,
            listing->maps.size() == 1 ? words::map_count_one : words::map_count,
            {{"count", count}}
        );
        list.push_back(fact_of(shown(text, words::fact_maps), maps, maps, Mark::none));
    }

    if (listing != nullptr && listing->coverage) {
        const double clamped = std::clamp(*listing->coverage, 0.0, 1.0);
        const auto percent =
            static_cast<uint64_t>(clamped * static_cast<double>(percent_whole) + 0.5);
        const std::string value =
            filled(text, words::coverage_percent, {{"percent", std::to_string(percent)}});
        list.push_back(fact_of(shown(text, words::fact_coverage), value, value, Mark::none));
    }

    if (entry.id.registry.empty()) {
        const std::string own(shown(text, words::from_own));
        list.push_back(fact_of(shown(text, words::fact_from), own, own, Mark::none));
    } else if (entry.not_reviewed) {
        const std::string from =
            filled(text, words::from_not_reviewed, {{"registry", entry.registry_name}});
        list.push_back(fact_of(shown(text, words::fact_from), from, from, Mark::warn));
    } else {
        list.push_back(fact_of(
            shown(text, words::fact_from), entry.registry_name, entry.registry_name, Mark::none
        ));
    }
    return list;
}

std::string status_line(const Library& library, const Entry& entry) {
    const TextHooks& text = library.text;
    if (entry.queued != nullptr && entry.queued->phase != Phase::installed)
        return queue_status(*entry.queued, text);
    if (!entry.can_act)
        return entry.blocked;
    switch (entry.state) {
    case State::update: {
        const UpdateNote& update = *entry.update;
        const std::string size = size_text(update.size, text);
        if (entry.installed->version == update.to_version)
            return filled(
                text,
                words::status_revision_available,
                {{"revision", std::to_string(update.to_revision)}, {"size", size}}
            );
        return filled(
            text, words::status_version_available, {{"version", update.to_version}, {"size", size}}
        );
    }
    case State::installed:
        return std::string(shown(text, words::status_installed));
    case State::playing:
        return std::string(shown(text, words::status_playing));
    case State::get:
        return filled(
            text, words::status_not_installed, {{"size", size_text(entry.listing->size, text)}}
        );
    }
    return {};
}

std::string byline(const Entry& entry, const TextHooks& text) {
    if (entry.listing == nullptr || entry.id.registry.empty())
        return {};
    const std::string& author =
        entry.listing->author.empty() ? entry.listing->publisher : entry.listing->author;
    if (author.empty())
        return {};
    return filled(text, words::byline, {{"author", author}});
}

std::string playing_note(const Library& library) {
    const TextHooks& text = library.text;
    const Entry* entry = selected_entry(library);
    if (entry == nullptr || entry->id.kind != Kind::mod || entry->installed == nullptr)
        return {};
    const std::string name(entry_name(*entry));
    if (entry->installed->playing) {
        const bool held = entry->queued != nullptr && entry->queued->phase == Phase::held;
        if (entry->update != nullptr || held)
            return filled(text, words::note_finishes, {{"name", name}});
        return {};
    }
    if (entry->update == nullptr)
        return {};
    for (const Installed& installed : library.inputs.installed) {
        if (installed.kind == Kind::mod && installed.playing)
            return filled(
                text, words::note_playing_other, {{"playing", installed.name}, {"name", name}}
            );
    }
    return {};
}

QueueLines queue_lines(const Library& library) {
    const TextHooks& text = library.text;
    const std::vector<QueueItem>& queue = library.inputs.queue;
    QueueLines lines;
    if (queue.empty())
        return lines;
    const std::size_t first = headline_item(queue);
    lines.first = first_queue_line(queue[first], text);
    lines.compact = compact_queue_line(queue[first], text);

    std::optional<std::size_t> next;
    std::size_t after_next = 0;
    for (std::size_t index = 0; index < queue.size(); ++index) {
        if (index == first || !pending(queue[index].phase))
            continue;
        if (!next)
            next = index;
        else
            ++after_next;
    }
    if (next) {
        const QueueItem& item = queue[*next];
        lines.second = filled(
            text,
            words::queue_then,
            {{"package", package_text(item, text)}, {"phase", phase_text(item.phase, text)}}
        );
        if (after_next != 0)
            lines.second = filled(
                text,
                words::queue_more,
                {{"line", lines.second}, {"count", std::to_string(after_next)}}
            );
    }
    return lines;
}

} // namespace oa::ui::library
