// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's model over made-up registries and packages: every state,
// every reason GET or UPDATE is off, built-in and added registries, a key
// installed from another registry, tabs, filters and tags, the order with no
// search, the update count, the selection across refreshes, the buttons, the
// facts with the sample numbers, the status lines, the bylines, the playing
// note, the queue lines and every text.
#include "oa/data/mod_profile/registry.hpp"
#include "oa/test/check.hpp"
#include "oa/ui/library/library.hpp"
#include "oa/ui/library/text.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace lib = oa::ui::library;
using lib::Action;
using lib::Kind;
using lib::Mark;
using lib::State;
using Phase = lib::QueueItem::Phase;

/// Checks that a text is what a case expects, printing both when it is not.
///
/// @param actual the text the model wrote
/// @param expected the text the case expects
/// @param file the check's file
/// @param line the check's line
/// @param expression the text's expression, as written
void check_text(
    std::string_view actual,
    std::string_view expected,
    const char* file,
    int line,
    const char* expression
) {
    if (actual == expected)
        return;
    std::fprintf(
        stderr,
        "%s:%d: %s is \"%.*s\", expected \"%.*s\"\n",
        file,
        line,
        expression,
        static_cast<int>(actual.size()),
        actual.data(),
        static_cast<int>(expected.size()),
        expected.data()
    );
    ++oa::test::failed_checks();
}

#define OA_CHECK_TEXT(actual, expected)                                                            \
    check_text((actual), (expected), __FILE__, __LINE__, #actual)

constexpr uint64_t mib = uint64_t{1} << 20;

/// The built-in registry's id and the added one's.
constexpr std::string_view core = "fixture-core";
constexpr std::string_view example = "example-maps";

/// A digest whose first four bytes are given and whose others are zero.
///
/// @param b0 the first byte
/// @param b1 the second byte
/// @param b2 the third byte
/// @param b3 the fourth byte
/// @return the digest
oa::base::sha256::Digest digest(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    oa::base::sha256::Digest value{};
    value[0] = b0;
    value[1] = b1;
    value[2] = b2;
    value[3] = b3;
    return value;
}

/// The sample rules hashes "9c1f 4e0b" and "51d2 07aa".
const oa::base::sha256::Digest old_rules = digest(0x9c, 0x1f, 0x4e, 0x0b);
const oa::base::sha256::Digest new_rules = digest(0x51, 0xd2, 0x07, 0xaa);

/// The two registries: a built-in one and one the player added.
///
/// @return them
std::vector<lib::Registry> fixture_registries() {
    lib::Registry built_in;
    built_in.id = std::string(core);
    built_in.name = "Fixture Core";
    built_in.built_in = true;
    lib::Registry added;
    added.id = std::string(example);
    added.name = "Example Maps";
    return {built_in, added};
}

/// A listing with a name, a version and a size.
///
/// @param registry the registry's id
/// @param kind the kind
/// @param key the key
/// @param name the name
/// @return the listing
lib::Listing
listing(std::string_view registry, Kind kind, std::string_view key, std::string_view name) {
    lib::Listing made;
    made.registry = std::string(registry);
    made.kind = kind;
    made.key = std::string(key);
    made.name = std::string(name);
    made.version = "1.0";
    made.revision = 1;
    made.release = 1;
    made.size = 64 * mib;
    made.author = "Fixture Author";
    return made;
}

/// An installed package.
///
/// @param registry where it came from; empty for the player's own
/// @param kind the kind
/// @param key the key
/// @param name the name
/// @return the package
lib::Installed
installed(std::string_view registry, Kind kind, std::string_view key, std::string_view name) {
    lib::Installed made;
    made.registry = std::string(registry);
    made.kind = kind;
    made.key = std::string(key);
    made.name = std::string(name);
    made.folder = std::string(key);
    made.version = "1.0";
    made.revision = 1;
    made.release = registry.empty() ? 0 : 1;
    return made;
}

/// An update of a package to revision 4 of version 4.8, 3.2 MB.
///
/// @param registry the registry's id
/// @param kind the kind
/// @param key the key
/// @return the update
lib::UpdateNote update(std::string_view registry, Kind kind, std::string_view key) {
    lib::UpdateNote made;
    made.registry = std::string(registry);
    made.kind = kind;
    made.key = std::string(key);
    made.to_release = 2;
    made.to_version = "4.8";
    made.to_revision = 4;
    made.size = 3355443; // 3.2 MiB
    return made;
}

/// A queue item.
///
/// @param registry the registry's id
/// @param key the key
/// @param name the name
/// @param phase the phase
/// @return the item, a mod at version 1.0
lib::QueueItem
queued(std::string_view registry, std::string_view key, std::string_view name, Phase phase) {
    lib::QueueItem made;
    made.registry = std::string(registry);
    made.kind = Kind::mod;
    made.key = std::string(key);
    made.name = std::string(name);
    made.version = "1.0";
    made.phase = phase;
    return made;
}

/// Inputs with the two registries, at engine 0.8.0, with a list four minutes old.
///
/// @return the inputs
lib::Inputs base_inputs() {
    lib::Inputs inputs;
    inputs.registries = fixture_registries();
    inputs.engine = {0, 8, 0};
    inputs.version_text = "v0.8.0";
    inputs.list_age_seconds = 240;
    return inputs;
}

/// Builds a Library from inputs.
///
/// @param inputs the inputs
/// @return the Library
lib::Library built(lib::Inputs inputs) {
    lib::Library library;
    lib::refresh(library, std::move(inputs));
    return library;
}

/// Finds an entry.
///
/// @param library the Library
/// @param registry the registry's id; empty for the player's own
/// @param kind the kind
/// @param key the key
/// @return the entry, or null
const lib::Entry*
at(const lib::Library& library, std::string_view registry, Kind kind, std::string_view key) {
    return lib::find_entry(library, {std::string(registry), kind, std::string(key)});
}

/// The keys of the visible entries, in order.
///
/// @param library the Library
/// @return the keys
std::vector<std::string> visible_keys(const lib::Library& library) {
    std::vector<std::string> keys;
    for (const std::size_t index : library.visible)
        keys.push_back(library.entries[index].id.key + "@" + library.entries[index].id.registry);
    return keys;
}

/// The actions of an entry's buttons, with whether each is enabled.
///
/// @param library the Library
/// @param entry the entry
/// @return each button's action and enabled flag
std::vector<std::pair<Action, bool>>
actions_of(const lib::Library& library, const lib::Entry& entry) {
    std::vector<std::pair<Action, bool>> found;
    for (const lib::ActionButton& button : lib::entry_actions(library, entry))
        found.emplace_back(button.action, button.enabled);
    return found;
}

/// Finds a fact by its long label.
///
/// @param list the facts
/// @param label the label
/// @return the fact, or null
const lib::Fact* fact(const std::vector<lib::Fact>& list, std::string_view label) {
    for (const lib::Fact& item : list)
        if (item.long_label == label)
            return &item;
    return nullptr;
}

/// An id of a hack this Open Annihilation carries out.
///
/// @return the id; empty when the registry has none
std::string_view implemented_hack() {
    namespace hacks = oa::data::mod_profile::registry;
    for (const hacks::Entry& entry : hacks::table().entries)
        if (entry.kind == hacks::EntryKind::hack && entry.implemented)
            return entry.id;
    return {};
}

void test_state_matrix() {
    lib::Inputs inputs = base_inputs();
    inputs.listings.push_back(listing(core, Kind::mod, "get-mod", "Get Mod"));
    inputs.installed.push_back(installed("", Kind::mod, "own-mod", "Own Mod"));
    inputs.listings.push_back(listing(core, Kind::mod, "kept-mod", "Kept Mod"));
    inputs.installed.push_back(installed(core, Kind::mod, "kept-mod", "Kept Mod"));
    inputs.listings.push_back(listing(core, Kind::mod, "fixture-mod", "Fixture Mod"));
    inputs.installed.push_back(installed(core, Kind::mod, "fixture-mod", "Fixture Mod"));
    inputs.updates.push_back(update(core, Kind::mod, "fixture-mod"));
    inputs.listings.push_back(listing(core, Kind::mod, "played-mod", "Played Mod"));
    inputs.installed.push_back(installed(core, Kind::mod, "played-mod", "Played Mod"));
    inputs.installed.back().playing = true;
    inputs.listings.push_back(listing(core, Kind::mod, "played-update", "Played Update"));
    inputs.installed.push_back(installed(core, Kind::mod, "played-update", "Played Update"));
    inputs.installed.back().playing = true;
    inputs.updates.push_back(update(core, Kind::mod, "played-update"));
    inputs.listings.push_back(listing(core, Kind::mod, "blocked-update", "Blocked Update"));
    inputs.installed.push_back(installed(core, Kind::mod, "blocked-update", "Blocked Update"));
    inputs.updates.push_back(update(core, Kind::mod, "blocked-update"));
    inputs.updates.back().blocked = lib::UpdateNote::Blocked::engine;
    inputs.updates.back().blocked_detail = "0.9.0 or later";
    // Installed from a registry that no longer lists it.
    inputs.installed.push_back(installed(example, Kind::map_pack, "gone-maps", "Gone Maps"));
    // An update with no installed package is not an entry's.
    inputs.updates.push_back(update(core, Kind::mod, "get-mod"));
    const lib::Library library = built(std::move(inputs));

    OA_CHECK(library.entries.size() == 8);
    const lib::Entry* get = at(library, core, Kind::mod, "get-mod");
    OA_CHECK(get != nullptr && get->state == State::get && get->listing != nullptr);
    OA_CHECK(get != nullptr && get->installed == nullptr && get->update == nullptr && get->can_act);
    const lib::Entry* own = at(library, "", Kind::mod, "own-mod");
    OA_CHECK(own != nullptr && own->state == State::installed && own->listing == nullptr);
    const lib::Entry* kept = at(library, core, Kind::mod, "kept-mod");
    OA_CHECK(kept != nullptr && kept->state == State::installed && kept->listing != nullptr);
    OA_CHECK(kept != nullptr && kept->installed != nullptr && kept->update == nullptr);
    const lib::Entry* updated = at(library, core, Kind::mod, "fixture-mod");
    OA_CHECK(updated != nullptr && updated->state == State::update && updated->can_act);
    const lib::Entry* played = at(library, core, Kind::mod, "played-mod");
    OA_CHECK(played != nullptr && played->state == State::playing);
    const lib::Entry* played_update = at(library, core, Kind::mod, "played-update");
    OA_CHECK(played_update != nullptr && played_update->state == State::update);
    const lib::Entry* blocked = at(library, core, Kind::mod, "blocked-update");
    OA_CHECK(blocked != nullptr && blocked->state == State::update && !blocked->can_act);
    if (blocked != nullptr)
        OA_CHECK_TEXT(blocked->blocked, "Needs Open Annihilation 0.9.0 or later");
    const lib::Entry* gone = at(library, example, Kind::map_pack, "gone-maps");
    OA_CHECK(gone != nullptr && gone->state == State::installed && gone->listing == nullptr);
    if (gone != nullptr)
        OA_CHECK_TEXT(gone->registry_name, "Example Maps");
}

void test_queue_states() {
    lib::Inputs inputs = base_inputs();
    const std::vector<Phase> phases = {
        Phase::waiting,
        Phase::downloading,
        Phase::verifying,
        Phase::checking,
        Phase::installing,
        Phase::held,
        Phase::failed,
        Phase::installed,
    };
    for (std::size_t index = 0; index < phases.size(); ++index) {
        const std::string key = "queued-" + std::to_string(index);
        inputs.listings.push_back(listing(core, Kind::mod, key, "Queued " + std::to_string(index)));
        inputs.queue.push_back(queued(core, key, "Queued " + std::to_string(index), phases[index]));
    }
    inputs.queue[1].done_bytes = 19083674; // 18.2 MiB
    inputs.queue[1].total_bytes = 64 * mib;
    inputs.queue[6].problem = "Couldn't reach Example Maps";
    inputs.queue[6].retryable = true;
    const lib::Library library = built(std::move(inputs));

    const std::vector<std::string_view> lines = {
        "Waiting to download",
        "Downloading · 18.2 of 64.0 MB",
        "Verifying",
        "Checking",
        "Installing",
        "Waiting · finishes when you leave Queued 5",
        "Couldn't reach Example Maps",
        "Not installed · 64.0 MB",
    };
    for (std::size_t index = 0; index < phases.size(); ++index) {
        const lib::Entry* entry = at(library, core, Kind::mod, "queued-" + std::to_string(index));
        OA_CHECK(entry != nullptr && entry->queued != nullptr && entry->state == State::get);
        if (entry == nullptr)
            continue;
        OA_CHECK_TEXT(lib::status_line(library, *entry), lines[index]);
        const std::vector<std::pair<Action, bool>> actions = actions_of(library, *entry);
        OA_CHECK(!actions.empty());
        if (actions.empty())
            continue;
        if (index < 6)
            OA_CHECK(actions.front() == std::make_pair(Action::cancel, true));
        else if (index == 6)
            OA_CHECK(actions.front() == std::make_pair(Action::retry, true));
        else
            OA_CHECK(actions.front() == std::make_pair(Action::get, true));
    }
}

void test_blocked_reasons() {
    const std::string_view working = implemented_hack();
    OA_CHECK(!working.empty());

    lib::Inputs inputs = base_inputs();
    lib::Listing engine = listing(core, Kind::mod, "engine-mod", "Engine Mod");
    engine.requires_engine = ">= 0.9.0";
    engine.base = "other";
    inputs.listings.push_back(engine);
    lib::Listing met = listing(core, Kind::mod, "met-mod", "Met Mod");
    met.requires_engine = ">= 0.8.0";
    inputs.listings.push_back(met);
    lib::Listing unreadable = listing(core, Kind::mod, "unreadable-mod", "Unreadable Mod");
    unreadable.requires_engine = "soon";
    inputs.listings.push_back(unreadable);
    lib::Listing base = listing(core, Kind::mod, "base-mod", "Base Mod");
    base.base = "other";
    base.hack_ids = {"fixture.missing-one"};
    inputs.listings.push_back(base);
    lib::Listing hacks = listing(core, Kind::mod, "hacks-mod", "Hacks Mod");
    hacks.hack_ids = {
        "fixture.missing-one",
        std::string(working),
        "fixture.missing-two",
        "fixture.missing-three",
        "fixture.missing-four",
        "fixture.missing-five",
    };
    inputs.listings.push_back(hacks);
    lib::Listing two_hacks = listing(core, Kind::mod, "two-hacks-mod", "Two Hacks Mod");
    two_hacks.hack_ids = {"fixture.missing-one", std::string(working), "fixture.missing-two"};
    inputs.listings.push_back(two_hacks);
    lib::Listing fits = listing(core, Kind::mod, "fits-mod", "Fits Mod");
    fits.base = "ta-3.1c";
    fits.hack_ids = {std::string(working)};
    inputs.listings.push_back(fits);
    inputs.listings.push_back(listing(core, Kind::mod, "hacks-update", "Hacks Update"));
    inputs.installed.push_back(installed(core, Kind::mod, "hacks-update", "Hacks Update"));
    inputs.updates.push_back(update(core, Kind::mod, "hacks-update"));
    inputs.updates.back().blocked = lib::UpdateNote::Blocked::hacks;
    inputs.updates.back().blocked_detail = "fixture.missing-one";
    inputs.listings.push_back(listing(core, Kind::mod, "base-update", "Base Update"));
    inputs.installed.push_back(installed(core, Kind::mod, "base-update", "Base Update"));
    inputs.updates.push_back(update(core, Kind::mod, "base-update"));
    inputs.updates.back().blocked = lib::UpdateNote::Blocked::base;
    inputs.updates.back().blocked_detail = "other";
    lib::Library library = built(inputs);

    const auto blocked_of = [&](std::string_view key) -> std::string {
        const lib::Entry* entry = at(library, core, Kind::mod, key);
        OA_CHECK(entry != nullptr);
        if (entry == nullptr)
            return "(none)";
        OA_CHECK(entry->can_act == entry->blocked.empty());
        return entry->blocked;
    };
    // The engine requirement comes before the base, and the base before the hacks.
    OA_CHECK_TEXT(blocked_of("engine-mod"), "Needs Open Annihilation 0.9.0 or later");
    OA_CHECK_TEXT(blocked_of("met-mod"), "");
    OA_CHECK_TEXT(blocked_of("unreadable-mod"), "");
    OA_CHECK_TEXT(blocked_of("base-mod"), "Needs the base game other");
    OA_CHECK_TEXT(
        blocked_of("hacks-mod"),
        "Needs hacks this OA lacks: fixture.missing-one, fixture.missing-two, "
        "fixture.missing-three · 2 more"
    );
    OA_CHECK_TEXT(
        blocked_of("two-hacks-mod"),
        "Needs hacks this OA lacks: fixture.missing-one, fixture.missing-two"
    );
    OA_CHECK_TEXT(blocked_of("fits-mod"), "");
    OA_CHECK_TEXT(blocked_of("hacks-update"), "Needs hacks this OA lacks: fixture.missing-one");
    OA_CHECK_TEXT(blocked_of("base-update"), "Needs the base game other");
    OA_CHECK(lib::update_count(library) == 0);

    // A blocked GET is listed, with its reason as its status line.
    std::size_t blocked_shown = 0;
    for (const std::size_t index : library.visible)
        if (!library.entries[index].can_act)
            ++blocked_shown;
    OA_CHECK(blocked_shown == 6);
    const lib::Entry* engine_entry = at(library, core, Kind::mod, "engine-mod");
    if (engine_entry != nullptr) {
        OA_CHECK_TEXT(
            lib::status_line(library, *engine_entry), "Needs Open Annihilation 0.9.0 or later"
        );
        OA_CHECK(actions_of(library, *engine_entry).front() == std::make_pair(Action::get, false));
    }

    // Downloads turned off for the registry come before the package's own reasons.
    inputs.registries[0].downloads_on = false;
    lib::refresh(library, inputs);
    OA_CHECK_TEXT(
        blocked_of("engine-mod"), "Downloads from Fixture Core are off (Settings › Downloads)"
    );
    OA_CHECK_TEXT(
        blocked_of("hacks-update"), "Downloads from Fixture Core are off (Settings › Downloads)"
    );
    inputs.registries[0].downloads_off_reason = "Fixture Core needs an install ID";
    lib::refresh(library, inputs);
    OA_CHECK_TEXT(blocked_of("met-mod"), "Fixture Core needs an install ID");

    // A game being played comes first of all; installed entries are never blocked.
    inputs.in_match = true;
    lib::refresh(library, inputs);
    OA_CHECK_TEXT(blocked_of("engine-mod"), "Not during a game");
    OA_CHECK_TEXT(blocked_of("met-mod"), "Not during a game");
    OA_CHECK_TEXT(blocked_of("base-update"), "Not during a game");
}

void test_installed_never_blocked() {
    lib::Inputs inputs = base_inputs();
    inputs.registries[1].downloads_on = false;
    inputs.in_match = true;
    lib::Listing needs = listing(example, Kind::mod, "fixture-mod", "Fixture Mod");
    needs.requires_engine = ">= 9.0.0";
    needs.hack_ids = {"fixture.missing-one"};
    inputs.listings.push_back(needs);
    inputs.installed.push_back(installed(example, Kind::mod, "fixture-mod", "Fixture Mod"));
    inputs.installed.back().kept = lib::Kept{"0.9", 2};
    inputs.installed.push_back(installed(example, Kind::mod, "second-mod", "Second Mod"));
    inputs.installed.back().playing = true;
    inputs.installed.back().kept = lib::Kept{"0.9", 2};
    const lib::Library library = built(std::move(inputs));
    const lib::Entry* fixture = at(library, example, Kind::mod, "fixture-mod");
    const lib::Entry* second = at(library, example, Kind::mod, "second-mod");
    OA_CHECK(fixture != nullptr && fixture->state == State::installed);
    OA_CHECK(fixture != nullptr && fixture->can_act && fixture->blocked.empty());
    OA_CHECK(second != nullptr && second->state == State::playing);
    OA_CHECK(second != nullptr && second->can_act && second->blocked.empty());
    if (fixture != nullptr) {
        OA_CHECK_TEXT(lib::status_line(library, *fixture), "Installed");
        const std::vector<std::pair<Action, bool>> expected = {
            {Action::play_now, false},
            {Action::roll_back, false},
            {Action::open_folder, true},
        };
        OA_CHECK(actions_of(library, *fixture) == expected);
    }
    if (second != nullptr) {
        OA_CHECK_TEXT(lib::status_line(library, *second), "Playing");
        const std::vector<std::pair<Action, bool>> expected = {
            {Action::roll_back, false},
            {Action::open_folder, true},
        };
        OA_CHECK(actions_of(library, *second) == expected);
    }
}

void test_registries() {
    lib::Inputs inputs = base_inputs();
    inputs.listings.push_back(listing(core, Kind::mod, "fixture-mod", "Fixture Mod"));
    inputs.listings.push_back(listing(example, Kind::mod, "fixture-mod", "Fixture Mod"));
    inputs.installed.push_back(installed(core, Kind::mod, "fixture-mod", "Fixture Mod"));
    inputs.listings.push_back(listing(example, Kind::map_pack, "example-pack", "Example Pack"));
    inputs.installed.push_back(installed("", Kind::map_pack, "example-pack", "Example Pack"));
    // A registry the inputs no longer name.
    inputs.installed.push_back(installed("removed", Kind::mod, "removed-mod", "Removed Mod"));
    const lib::Library library = built(std::move(inputs));

    // Two registries with one key are two entries.
    const lib::Entry* built_in = at(library, core, Kind::mod, "fixture-mod");
    const lib::Entry* added = at(library, example, Kind::mod, "fixture-mod");
    OA_CHECK(built_in != nullptr && added != nullptr && built_in != added);
    if (built_in != nullptr) {
        OA_CHECK(built_in->built_in && !built_in->not_reviewed);
        OA_CHECK_TEXT(built_in->registry_name, "Fixture Core");
        OA_CHECK(built_in->state == State::installed);
        OA_CHECK(built_in->replaces_registry_name.empty());
    }
    if (added != nullptr) {
        OA_CHECK(!added->built_in && added->not_reviewed);
        OA_CHECK_TEXT(added->registry_name, "Example Maps");
        OA_CHECK(added->state == State::get);
        OA_CHECK_TEXT(added->replaces_registry_name, "Fixture Core");
    }
    // The player's own package: no registry, nothing replaced by name.
    const lib::Entry* own = at(library, "", Kind::map_pack, "example-pack");
    OA_CHECK(own != nullptr && !own->built_in && !own->not_reviewed && own->registry_name.empty());
    const lib::Entry* pack = at(library, example, Kind::map_pack, "example-pack");
    OA_CHECK(pack != nullptr && pack->state == State::get && pack->replaces_registry_name.empty());
    const lib::Entry* removed = at(library, "removed", Kind::mod, "removed-mod");
    OA_CHECK(removed != nullptr && !removed->built_in && removed->not_reviewed);
    if (removed != nullptr)
        OA_CHECK_TEXT(removed->registry_name, "removed");
}

void test_tabs_filters_tags() {
    lib::Inputs inputs = base_inputs();
    lib::Listing alpha = listing(core, Kind::mod, "alpha-mod", "Alpha Mod");
    alpha.tags = {"balance", "units"};
    inputs.listings.push_back(alpha);
    lib::Listing beta = listing(core, Kind::mod, "beta-mod", "Beta Mod");
    beta.tags = {"balance"};
    inputs.listings.push_back(beta);
    inputs.installed.push_back(installed(core, Kind::mod, "beta-mod", "Beta Mod"));
    lib::Listing gamma = listing(core, Kind::mod, "gamma-mod", "Gamma Mod");
    gamma.tags = {"total-conversion"};
    inputs.listings.push_back(gamma);
    inputs.installed.push_back(installed(core, Kind::mod, "gamma-mod", "Gamma Mod"));
    inputs.updates.push_back(update(core, Kind::mod, "gamma-mod"));
    inputs.installed.push_back(installed(core, Kind::mod, "delta-mod", "Delta Mod"));
    inputs.installed.back().playing = true;
    inputs.listings.push_back(listing(example, Kind::map_pack, "example-pack", "Example Pack"));
    inputs.installed.push_back(installed(example, Kind::map_pack, "example-pack", "Example Pack"));
    inputs.updates.push_back(update(example, Kind::map_pack, "example-pack"));
    inputs.listings.push_back(listing(core, Kind::language, "xx", "Fixture Language"));
    lib::Library library = built(std::move(inputs));

    using Keys = std::vector<std::string>;
    OA_CHECK(library.tab == lib::Tab::mods && library.filter == lib::Filter::all);
    const Keys every_mod = {
        "gamma-mod@fixture-core",
        "beta-mod@fixture-core",
        "delta-mod@fixture-core",
        "alpha-mod@fixture-core",
    };
    OA_CHECK(visible_keys(library) == every_mod);
    lib::set_tab(library, lib::Tab::maps);
    OA_CHECK(visible_keys(library) == Keys({"example-pack@example-maps"}));
    lib::set_tab(library, lib::Tab::languages);
    OA_CHECK(visible_keys(library) == Keys({"xx@fixture-core"}));

    lib::set_tab(library, lib::Tab::mods);
    lib::set_filter(library, lib::Filter::installed);
    const Keys installed_mods = {
        "gamma-mod@fixture-core",
        "beta-mod@fixture-core",
        "delta-mod@fixture-core",
    };
    OA_CHECK(visible_keys(library) == installed_mods);
    lib::set_filter(library, lib::Filter::updates);
    OA_CHECK(visible_keys(library) == Keys({"gamma-mod@fixture-core"}));

    // The Updates tab shows every kind's updates and ignores the filter and tag.
    lib::set_filter(library, lib::Filter::installed);
    lib::set_tag(library, "units");
    lib::set_tab(library, lib::Tab::updates);
    const Keys every_update = {
        "example-pack@example-maps",
        "gamma-mod@fixture-core",
    };
    OA_CHECK(visible_keys(library) == every_update);
    OA_CHECK(lib::update_count(library) == 2);

    lib::set_tab(library, lib::Tab::mods);
    lib::set_filter(library, lib::Filter::all);
    OA_CHECK(visible_keys(library) == Keys({"alpha-mod@fixture-core"}));
    lib::set_tag(library, "balance");
    OA_CHECK(visible_keys(library) == Keys({"beta-mod@fixture-core", "alpha-mod@fixture-core"}));
    lib::set_tag(library, "");
    using Counts = std::vector<std::pair<std::string, std::size_t>>;
    OA_CHECK(
        lib::tags_in_tab(library) == Counts({{"balance", 2}, {"total-conversion", 1}, {"units", 1}})
    );
    lib::set_filter(library, lib::Filter::installed);
    OA_CHECK(lib::tags_in_tab(library) == Counts({{"balance", 1}, {"total-conversion", 1}}));
    lib::set_tab(library, lib::Tab::maps);
    OA_CHECK(lib::tags_in_tab(library).empty());

    lib::set_tab(library, lib::Tab::mods);
    lib::set_filter(library, lib::Filter::all);
    const std::string_view text = lib::tab_text(lib::Tab::languages, true);
    OA_CHECK_TEXT(text, "Lang");
    OA_CHECK_TEXT(lib::tab_text(lib::Tab::updates, true), "Upd");
    OA_CHECK_TEXT(lib::tab_text(lib::Tab::updates, false), "Updates");
    OA_CHECK_TEXT(lib::tab_text(lib::Tab::mods, true), "Mods");
    OA_CHECK_TEXT(lib::filter_text(lib::Filter::installed), "Installed");
    OA_CHECK_TEXT(lib::state_text(State::playing), "PLAYING");
}

void test_order_without_search() {
    lib::Inputs inputs = base_inputs();
    lib::Registry other;
    other.id = "another";
    other.name = "Another";
    inputs.registries.push_back(other);
    // Blocked GET, GET, installed, playing and update, named so names alone would order them otherwise.
    lib::Listing blocked = listing(core, Kind::mod, "aa-blocked", "Aa Blocked");
    blocked.requires_engine = ">= 1.0.0";
    inputs.listings.push_back(blocked);
    inputs.listings.push_back(listing(core, Kind::mod, "bb-get", "bb Get"));
    inputs.listings.push_back(listing(example, Kind::mod, "same-name", "Same Name"));
    inputs.listings.push_back(listing("another", Kind::mod, "same-name", "Same Name"));
    inputs.listings.push_back(listing(core, Kind::mod, "same-name", "same name"));
    inputs.installed.push_back(installed("", Kind::mod, "cc-own", "Cc Own"));
    inputs.installed.push_back(installed(core, Kind::mod, "dd-playing", "DD Playing"));
    inputs.installed.back().playing = true;
    inputs.listings.push_back(listing(core, Kind::mod, "zz-update", "Zz Update"));
    inputs.installed.push_back(installed(core, Kind::mod, "zz-update", "Zz Update"));
    inputs.updates.push_back(update(core, Kind::mod, "zz-update"));
    const lib::Library library = built(std::move(inputs));
    using Keys = std::vector<std::string>;
    const Keys order = {
        "zz-update@fixture-core",
        "cc-own@",
        "dd-playing@fixture-core",
        "bb-get@fixture-core",
        "same-name@fixture-core",
        "same-name@another",
        "same-name@example-maps",
        "aa-blocked@fixture-core",
    };
    OA_CHECK(visible_keys(library) == order);
}

void test_selection() {
    lib::Inputs inputs = base_inputs();
    inputs.listings.push_back(listing(core, Kind::mod, "alpha-mod", "Alpha Mod"));
    inputs.listings.push_back(listing(core, Kind::mod, "beta-mod", "Beta Mod"));
    inputs.listings.push_back(listing(core, Kind::mod, "gamma-mod", "Gamma Mod"));
    inputs.listings.push_back(listing(core, Kind::map_pack, "example-pack", "Example Pack"));
    lib::Library library = built(inputs);
    const lib::EntryId alpha{std::string(core), Kind::mod, "alpha-mod"};
    const lib::EntryId beta{std::string(core), Kind::mod, "beta-mod"};
    const lib::EntryId gamma{std::string(core), Kind::mod, "gamma-mod"};

    // The first refresh selects the first visible entry.
    OA_CHECK(library.selected == alpha && !library.details_open);
    lib::move_selection(library, 1);
    OA_CHECK(library.selected == beta);
    lib::move_selection(library, 5);
    OA_CHECK(library.selected == gamma);
    lib::move_selection(library, -9);
    OA_CHECK(library.selected == alpha);
    lib::select(library, beta);
    OA_CHECK(library.selected == beta);
    // An entry that is not visible is not selected.
    lib::select(library, {std::string(core), Kind::map_pack, "example-pack"});
    OA_CHECK(library.selected == beta);
    lib::open_details(library);
    OA_CHECK(library.details_open);
    const lib::Entry* selected = lib::selected_entry(library);
    OA_CHECK(selected != nullptr && lib::entry_name(*selected) == "Beta Mod");

    // A refresh keeps a selection that still shows, with its details.
    inputs.installed.push_back(installed(core, Kind::mod, "beta-mod", "Beta Mod"));
    lib::refresh(library, inputs);
    OA_CHECK(library.selected == beta && library.details_open);
    selected = lib::selected_entry(library);
    OA_CHECK(selected != nullptr && selected->state == State::installed);

    // When it is gone, the first visible entry is selected and the details close.
    inputs.installed.clear();
    inputs.listings.erase(inputs.listings.begin() + 1);
    lib::refresh(library, inputs);
    OA_CHECK(library.selected == alpha && !library.details_open);

    // A search or tab that hides it moves it too.
    lib::open_details(library);
    lib::set_query(library, "gamma");
    OA_CHECK(library.selected == gamma && !library.details_open);
    lib::set_query(library, "nothing at all");
    OA_CHECK(!library.selected && library.visible.empty());
    lib::open_details(library);
    OA_CHECK(!library.details_open);
    lib::move_selection(library, 1);
    OA_CHECK(!library.selected);
    lib::set_query(library, "");
    lib::set_tab(library, lib::Tab::maps);
    OA_CHECK((library.selected == lib::EntryId{std::string(core), Kind::map_pack, "example-pack"}));
    lib::open_details(library);
    lib::close_details(library);
    OA_CHECK(!library.details_open);
}

void test_entry_actions() {
    lib::Inputs inputs = base_inputs();
    lib::Listing get = listing(core, Kind::mod, "get-mod", "Get Mod");
    get.homepage = "https://example.org/get-mod";
    inputs.listings.push_back(get);
    inputs.listings.push_back(listing(core, Kind::mod, "fixture-mod", "Fixture Mod"));
    inputs.installed.push_back(installed(core, Kind::mod, "fixture-mod", "Fixture Mod"));
    inputs.installed.back().kept = lib::Kept{"4.8", 2};
    inputs.updates.push_back(update(core, Kind::mod, "fixture-mod"));
    inputs.installed.push_back(installed("", Kind::mod, "own-mod", "Own Mod"));
    inputs.installed.push_back(installed(core, Kind::map_pack, "example-pack", "Example Pack"));
    inputs.listings.push_back(listing(core, Kind::mod, "failed-mod", "Failed Mod"));
    inputs.queue.push_back(queued(core, "failed-mod", "Failed Mod", Phase::failed));
    inputs.queue.back().problem = "The file's SHA-256 didn't match";
    inputs.listings.push_back(listing(core, Kind::mod, "update-queued", "Update Queued"));
    inputs.installed.push_back(installed(core, Kind::mod, "update-queued", "Update Queued"));
    inputs.updates.push_back(update(core, Kind::mod, "update-queued"));
    inputs.queue.push_back(queued(core, "update-queued", "Update Queued", Phase::downloading));
    const lib::Library library = built(std::move(inputs));

    using Buttons = std::vector<std::pair<Action, bool>>;
    const lib::Entry* get_entry = at(library, core, Kind::mod, "get-mod");
    if (get_entry != nullptr) {
        OA_CHECK(
            actions_of(library, *get_entry) ==
            Buttons({{Action::get, true}, {Action::homepage, true}})
        );
        const std::vector<lib::ActionButton> buttons = lib::entry_actions(library, *get_entry);
        OA_CHECK_TEXT(buttons.front().text, "GET");
        OA_CHECK_TEXT(buttons.back().text, "HOMEPAGE");
    }
    const lib::Entry* update_entry = at(library, core, Kind::mod, "fixture-mod");
    if (update_entry != nullptr) {
        OA_CHECK(
            actions_of(library, *update_entry) == Buttons(
                                                      {{Action::update, true},
                                                       {Action::play_now, true},
                                                       {Action::roll_back, true},
                                                       {Action::open_folder, true}}
                                                  )
        );
        OA_CHECK_TEXT(lib::entry_actions(library, *update_entry)[2].text, "ROLL BACK");
    }
    const lib::Entry* own = at(library, "", Kind::mod, "own-mod");
    if (own != nullptr)
        OA_CHECK(
            actions_of(library, *own) ==
            Buttons({{Action::play_now, true}, {Action::open_folder, true}})
        );
    // A map pack is never played.
    const lib::Entry* pack = at(library, core, Kind::map_pack, "example-pack");
    if (pack != nullptr)
        OA_CHECK(actions_of(library, *pack) == Buttons({{Action::open_folder, true}}));
    // A failure that cannot be retried offers GET again.
    const lib::Entry* failed = at(library, core, Kind::mod, "failed-mod");
    if (failed != nullptr) {
        OA_CHECK(actions_of(library, *failed) == Buttons({{Action::get, true}}));
        OA_CHECK_TEXT(lib::status_line(library, *failed), "The file's SHA-256 didn't match");
    }
    const lib::Entry* update_queued = at(library, core, Kind::mod, "update-queued");
    if (update_queued != nullptr)
        OA_CHECK(
            actions_of(library, *update_queued) ==
            Buttons({{Action::cancel, true}, {Action::play_now, true}, {Action::open_folder, true}})
        );
    OA_CHECK_TEXT(lib::action_text(Action::update_all), "UPDATE ALL");
    OA_CHECK_TEXT(lib::action_text(Action::settings), "SETTINGS…");
    OA_CHECK_TEXT(lib::action_text(Action::close), "CLOSE");
}

/// Inputs holding the sample mod: 4.8 revision 3 installed, 41.0 MB, revision 4 offered.
///
/// @return the inputs
lib::Inputs sample_inputs() {
    lib::Inputs inputs = base_inputs();
    lib::Listing sample = listing(core, Kind::mod, "fixture-mod", "Fixture Mod");
    sample.version = "4.8";
    sample.revision = 4;
    sample.size = 43011223;
    sample.base = "ta-3.1c";
    sample.requires_engine = ">= 0.8.0";
    sample.game_hacks = 8;
    sample.view_hacks = 4;
    sample.sim_hash = new_rules;
    sample.author = "";
    sample.publisher = "Fixture Publisher";
    sample.summary = "Balance changes for testing.";
    inputs.listings.push_back(sample);
    lib::Installed mod = installed(core, Kind::mod, "fixture-mod", "Fixture Mod");
    mod.version = "4.8";
    mod.revision = 3;
    mod.rules = old_rules;
    mod.kept = lib::Kept{"4.8", 2};
    inputs.installed.push_back(mod);
    lib::UpdateNote note = update(core, Kind::mod, "fixture-mod");
    note.from_rules = old_rules;
    note.to_rules = old_rules;
    note.rules = lib::UpdateNote::Rules::same;
    inputs.updates.push_back(note);
    return inputs;
}

void test_facts() {
    lib::Inputs inputs = sample_inputs();
    lib::Library library = built(inputs);
    const lib::EntryId id{std::string(core), Kind::mod, "fixture-mod"};
    const auto facts_now = [&]() {
        const lib::Entry* entry = lib::find_entry(library, id);
        OA_CHECK(entry != nullptr);
        return entry != nullptr ? lib::facts(library, *entry) : std::vector<lib::Fact>();
    };

    std::vector<lib::Fact> list = facts_now();
    std::vector<std::string> labels;
    for (const lib::Fact& item : list)
        labels.push_back(item.long_label);
    OA_CHECK(
        labels == std::vector<std::string>({"Size", "Base", "Needs", "Hacks", "Rules", "From"})
    );
    if (const lib::Fact* size = fact(list, "Size")) {
        OA_CHECK_TEXT(size->long_value, "41.0 MB · 3.2 MB to download");
        OA_CHECK_TEXT(size->short_value, "3.2 MB to download");
    }
    if (const lib::Fact* base = fact(list, "Base")) {
        OA_CHECK_TEXT(base->long_value, "Total Annihilation 3.1c");
        OA_CHECK_TEXT(base->short_value, "TA 3.1c");
    }
    if (const lib::Fact* needs = fact(list, "Needs")) {
        OA_CHECK_TEXT(needs->long_value, "Open Annihilation 0.8.0 or later");
        OA_CHECK_TEXT(needs->short_value, "OA 0.8.0+");
        OA_CHECK(needs->mark == Mark::good);
    }
    if (const lib::Fact* hacks = fact(list, "Hacks")) {
        OA_CHECK_TEXT(hacks->long_value, "8 change the game · 4 view only");
        OA_CHECK_TEXT(hacks->short_value, "8 game · 4 view");
        OA_CHECK(hacks->mark == Mark::none);
    }
    if (const lib::Fact* rules = fact(list, "Rules")) {
        OA_CHECK_TEXT(rules->long_value, "9c1f 4e0b → 9c1f 4e0b same");
        OA_CHECK_TEXT(rules->short_value, "unchanged");
        OA_CHECK(rules->mark == Mark::none);
    }
    if (const lib::Fact* from = fact(list, "From")) {
        OA_CHECK_TEXT(from->long_value, "Fixture Core");
        OA_CHECK(from->mark == Mark::none);
    }

    // An update that changes the rules says so, and which saved games stop loading.
    inputs.updates[0].to_rules = new_rules;
    inputs.updates[0].rules = lib::UpdateNote::Rules::changes;
    lib::refresh(library, inputs);
    list = facts_now();
    if (const lib::Fact* rules = fact(list, "Rules")) {
        OA_CHECK_TEXT(
            rules->long_value,
            "9c1f 4e0b → 51d2 07aa · changes the game's rules; saved games from revision 3 "
            "won't load under revision 4"
        );
        OA_CHECK_TEXT(rules->short_value, "changes · rev 3 saves won't load");
        OA_CHECK(rules->mark == Mark::warn);
    }

    // Unknown when a hash is missing.
    inputs.updates[0].to_rules.reset();
    inputs.updates[0].rules = lib::UpdateNote::Rules::unknown;
    lib::refresh(library, inputs);
    list = facts_now();
    if (const lib::Fact* rules = fact(list, "Rules")) {
        OA_CHECK_TEXT(
            rules->long_value, "unknown · saved games from revision 3 may not load under revision 4"
        );
        OA_CHECK_TEXT(rules->short_value, "unknown · saves may not load");
        OA_CHECK(rules->mark == Mark::warn);
    }

    // A new version names the versions instead.
    inputs.updates[0].to_version = "4.9";
    inputs.updates[0].to_rules = new_rules;
    inputs.updates[0].rules = lib::UpdateNote::Rules::changes;
    lib::refresh(library, inputs);
    list = facts_now();
    if (const lib::Fact* rules = fact(list, "Rules")) {
        OA_CHECK_TEXT(
            rules->long_value,
            "9c1f 4e0b → 51d2 07aa · changes the game's rules; saved games from 4.8 won't load "
            "under 4.9"
        );
        OA_CHECK_TEXT(rules->short_value, "changes · 4.8 saves won't load");
    }

    // An engine that does not meet the requirement, missing hacks and an added registry.
    inputs = sample_inputs();
    inputs.engine = {0, 7, 3};
    inputs.listings[0].registry = std::string(example);
    inputs.listings[0].hack_ids = {std::string(implemented_hack()), "fixture.missing-one"};
    inputs.installed.clear();
    inputs.updates.clear();
    lib::refresh(library, inputs);
    const lib::Entry* added = at(library, example, Kind::mod, "fixture-mod");
    OA_CHECK(added != nullptr);
    if (added != nullptr) {
        list = lib::facts(library, *added);
        if (const lib::Fact* size = fact(list, "Size"))
            OA_CHECK_TEXT(size->long_value, "41.0 MB");
        if (const lib::Fact* needs = fact(list, "Needs"))
            OA_CHECK(needs->mark == Mark::warn);
        if (const lib::Fact* hacks = fact(list, "Hacks")) {
            OA_CHECK_TEXT(
                hacks->long_value,
                "8 change the game · 4 view only · Needs hacks this OA lacks: fixture.missing-one"
            );
            OA_CHECK_TEXT(hacks->short_value, "8 game · 4 view · 1 missing");
            OA_CHECK(hacks->mark == Mark::warn);
        }
        if (const lib::Fact* rules = fact(list, "Rules"))
            OA_CHECK_TEXT(rules->long_value, "51d2 07aa");
        if (const lib::Fact* from = fact(list, "From")) {
            OA_CHECK_TEXT(from->long_value, "Example Maps · Not reviewed by the OA team");
            OA_CHECK(from->mark == Mark::warn);
        }
    }

    // A map pack's maps, a language's coverage, and the player's own package.
    inputs = base_inputs();
    lib::Listing pack = listing(example, Kind::map_pack, "example-pack", "Example Pack");
    pack.version = "1.2";
    pack.maps = {{"First Map", 4, "16x16"}, {"Second Map", 2, "8x8"}};
    inputs.listings.push_back(pack);
    lib::Listing language = listing(example, Kind::language, "xx", "Fixture Language");
    language.coverage = 0.92;
    inputs.listings.push_back(language);
    inputs.installed.push_back(installed("", Kind::mod, "own-mod", "Own Mod"));
    inputs.installed.back().rules = old_rules;
    lib::refresh(library, inputs);
    if (const lib::Entry* entry = at(library, example, Kind::map_pack, "example-pack")) {
        list = lib::facts(library, *entry);
        if (const lib::Fact* maps = fact(list, "Maps"))
            OA_CHECK_TEXT(maps->long_value, "2 maps");
        OA_CHECK(fact(list, "Hacks") == nullptr && fact(list, "Rules") == nullptr);
        OA_CHECK(fact(list, "Base") == nullptr);
        OA_CHECK_TEXT(lib::version_text(*entry->listing), "1.2 · 2 maps");
    }
    if (const lib::Entry* entry = at(library, example, Kind::language, "xx")) {
        list = lib::facts(library, *entry);
        if (const lib::Fact* coverage = fact(list, "Coverage"))
            OA_CHECK_TEXT(coverage->long_value, "92%");
        OA_CHECK(fact(list, "Coverage") != nullptr);
    }
    if (const lib::Entry* entry = at(library, "", Kind::mod, "own-mod")) {
        list = lib::facts(library, *entry);
        OA_CHECK(list.size() == 2);
        if (const lib::Fact* rules = fact(list, "Rules"))
            OA_CHECK_TEXT(rules->long_value, "9c1f 4e0b");
        if (const lib::Fact* from = fact(list, "From"))
            OA_CHECK_TEXT(from->long_value, "Your own copy");
    }
}

void test_status_lines_and_bylines() {
    lib::Inputs inputs = sample_inputs();
    inputs.listings.push_back(listing(core, Kind::mod, "get-mod", "Get Mod"));
    inputs.installed.push_back(installed("", Kind::mod, "own-mod", "Own Mod"));
    lib::Library library = built(inputs);
    const lib::Entry* entry = at(library, core, Kind::mod, "fixture-mod");
    if (entry != nullptr) {
        OA_CHECK_TEXT(
            lib::status_line(library, *entry), "Revision 4 available · 3.2 MB to download"
        );
        OA_CHECK_TEXT(lib::byline(*entry), "by Fixture Publisher");
    }
    const lib::Entry* get = at(library, core, Kind::mod, "get-mod");
    if (get != nullptr) {
        OA_CHECK_TEXT(lib::status_line(library, *get), "Not installed · 64.0 MB");
        OA_CHECK_TEXT(lib::byline(*get), "by Fixture Author");
    }
    const lib::Entry* own = at(library, "", Kind::mod, "own-mod");
    if (own != nullptr)
        OA_CHECK_TEXT(lib::byline(*own), "");

    inputs.updates[0].to_version = "4.9";
    lib::refresh(library, inputs);
    entry = at(library, core, Kind::mod, "fixture-mod");
    if (entry != nullptr)
        OA_CHECK_TEXT(
            lib::status_line(library, *entry), "Version 4.9 available · 3.2 MB to download"
        );
}

void test_playing_note() {
    lib::Inputs inputs = sample_inputs();
    inputs.installed[0].playing = true;
    lib::Library library = built(inputs);
    const lib::EntryId fixture{std::string(core), Kind::mod, "fixture-mod"};
    lib::select(library, fixture);
    OA_CHECK_TEXT(lib::playing_note(library), "Finishes when you leave Fixture Mod.");

    // A held update of the mod being played says the same.
    inputs.updates.clear();
    inputs.queue.push_back(queued(core, "fixture-mod", "Fixture Mod", Phase::held));
    lib::refresh(library, inputs);
    OA_CHECK(library.selected == fixture);
    OA_CHECK_TEXT(lib::playing_note(library), "Finishes when you leave Fixture Mod.");
    inputs.queue.clear();
    lib::refresh(library, inputs);
    OA_CHECK_TEXT(lib::playing_note(library), "");

    // Another mod is played.
    inputs = sample_inputs();
    inputs.installed.push_back(installed(core, Kind::mod, "second-mod", "Second Mod"));
    inputs.installed.back().playing = true;
    lib::refresh(library, inputs);
    lib::select(library, fixture);
    OA_CHECK(library.selected == fixture);
    OA_CHECK_TEXT(
        lib::playing_note(library),
        "You are playing Second Mod. Updating Fixture Mod doesn't restart the game."
    );
    lib::select(library, {std::string(core), Kind::mod, "second-mod"});
    OA_CHECK_TEXT(lib::playing_note(library), "");
}

void test_queue_lines() {
    lib::Inputs inputs = base_inputs();
    OA_CHECK(lib::queue_lines(built(inputs)).first.empty());

    lib::QueueItem done = queued(core, "done-mod", "Done Mod", Phase::installed);
    lib::QueueItem downloading = queued(core, "third-mod", "Third Mod", Phase::downloading);
    downloading.version = "3.1";
    downloading.done_bytes = 19083674; // 18.2 MiB
    downloading.total_bytes = 64 * mib;
    lib::QueueItem checking = queued(core, "second-mod", "Second Mod", Phase::checking);
    checking.version = "10.2.0";
    // The download comes first, though an item checked is earlier in the queue.
    inputs.queue = {done, checking, downloading};
    lib::QueueLines lines = lib::queue_lines(built(inputs));
    OA_CHECK_TEXT(lines.first, "Downloading Third Mod 3.1 · 18.2 of 64.0 MB");
    OA_CHECK_TEXT(lines.second, "Then Second Mod 10.2.0 · checking");
    OA_CHECK_TEXT(lines.compact, "Third Mod 28%");

    // With nothing downloading, the first item at work.
    inputs.queue = {queued(core, "a", "Waiting One", Phase::waiting), checking};
    lines = lib::queue_lines(built(inputs));
    OA_CHECK_TEXT(lines.first, "Checking Second Mod");
    OA_CHECK_TEXT(lines.second, "Then Waiting One 1.0 · waiting");
    OA_CHECK_TEXT(lines.compact, "Second Mod · checking");

    inputs.queue = {
        done,
        downloading,
        checking,
        queued(core, "a", "Waiting One", Phase::waiting),
        queued(core, "b", "Waiting Two", Phase::waiting)
    };
    lines = lib::queue_lines(built(inputs));
    OA_CHECK_TEXT(lines.first, "Downloading Third Mod 3.1 · 18.2 of 64.0 MB");
    OA_CHECK_TEXT(lines.second, "Then Second Mod 10.2.0 · checking · 2 more");
    OA_CHECK_TEXT(lines.compact, "Third Mod 28%");

    const auto first_line = [&](lib::QueueItem item) {
        lib::Inputs one = base_inputs();
        one.queue = {std::move(item)};
        return lib::queue_lines(built(std::move(one)));
    };
    OA_CHECK_TEXT(
        first_line(queued(core, "m", "Fixture Mod", Phase::verifying)).first,
        "Verifying Fixture Mod"
    );
    OA_CHECK_TEXT(
        first_line(queued(core, "m", "Fixture Mod", Phase::installing)).first,
        "Installing Fixture Mod"
    );
    OA_CHECK_TEXT(
        first_line(queued(core, "m", "Fixture Mod", Phase::waiting)).first,
        "Waiting to download Fixture Mod 1.0"
    );
    lines = first_line(queued(core, "m", "Fixture Mod", Phase::held));
    OA_CHECK_TEXT(lines.first, "Waiting · finishes when you leave Fixture Mod");
    OA_CHECK_TEXT(lines.compact, "Fixture Mod · after you leave it");
    lib::QueueItem failed = queued(example, "m", "Example Pack", Phase::failed);
    failed.problem = "Couldn't reach Example Maps";
    failed.retryable = true;
    lines = first_line(failed);
    OA_CHECK_TEXT(lines.first, "Couldn't reach Example Maps · RETRY");
    OA_CHECK_TEXT(lines.compact, "Example Pack · failed");
    failed.retryable = false;
    OA_CHECK_TEXT(first_line(failed).first, "Couldn't reach Example Maps");

    // After an update, the queue says whether it changed the game's rules.
    lib::QueueItem updated = queued(core, "m", "Fixture Mod", Phase::installed);
    updated.update_rules = lib::UpdateNote::Rules::changes;
    updated.update_from = "revision 3";
    updated.update_to = "revision 4";
    lines = first_line(updated);
    OA_CHECK_TEXT(
        lines.first,
        "Updated Fixture Mod · revision 4 changes the game's rules · ROLL BACK restores revision 3"
    );
    OA_CHECK_TEXT(lines.compact, "Fixture Mod · rules changed");
    updated.update_rules = lib::UpdateNote::Rules::unknown;
    updated.update_from = "4.8";
    updated.update_to = "4.9";
    lines = first_line(updated);
    OA_CHECK_TEXT(
        lines.first,
        "Updated Fixture Mod · 4.9 may change the game's rules · ROLL BACK restores 4.8"
    );
    OA_CHECK_TEXT(lines.compact, "Fixture Mod · rules changed");
    updated.update_rules = lib::UpdateNote::Rules::same;
    lines = first_line(updated);
    OA_CHECK_TEXT(lines.first, "Installed Fixture Mod");
    OA_CHECK_TEXT(lines.compact, "Fixture Mod · installed");
    OA_CHECK(lines.second.empty());

    // A failure comes before items that wait, and the last item installed comes last.
    inputs = base_inputs();
    inputs.queue = {queued(core, "a", "Waiting One", Phase::waiting), failed, done};
    lines = lib::queue_lines(built(inputs));
    OA_CHECK_TEXT(lines.first, "Couldn't reach Example Maps");
    OA_CHECK_TEXT(lines.second, "Then Waiting One 1.0 · waiting");
    inputs.queue = {done, queued(core, "b", "Later Mod", Phase::installed)};
    OA_CHECK_TEXT(lib::queue_lines(built(inputs)).first, "Installed Later Mod");
}

void test_change_texts() {
    lib::Installed mod = installed(core, Kind::mod, "fixture-mod", "Fixture Mod");
    mod.version = "4.8";
    mod.revision = 3;
    lib::UpdateNote note = update(core, Kind::mod, "fixture-mod");
    lib::ChangeTexts names = lib::change_texts(mod, note);
    OA_CHECK_TEXT(names.from, "revision 3");
    OA_CHECK_TEXT(names.to, "revision 4");
    OA_CHECK_TEXT(names.from_short, "rev 3");
    OA_CHECK_TEXT(names.to_short, "rev 4");
    note.to_version = "4.9";
    note.to_revision = 1;
    names = lib::change_texts(mod, note);
    OA_CHECK_TEXT(names.from, "4.8");
    OA_CHECK_TEXT(names.to, "4.9");
    OA_CHECK_TEXT(names.from_short, "4.8");
    OA_CHECK_TEXT(names.to_short, "4.9");
}

void test_texts() {
    OA_CHECK_TEXT(lib::size_text(0), "1 KB");
    OA_CHECK_TEXT(lib::size_text(1), "1 KB");
    OA_CHECK_TEXT(lib::size_text(1535), "1 KB");
    OA_CHECK_TEXT(lib::size_text(1536), "2 KB");
    OA_CHECK_TEXT(lib::size_text(mib - 1), "1024 KB");
    OA_CHECK_TEXT(lib::size_text(mib), "1.0 MB");
    OA_CHECK_TEXT(lib::size_text(1101004), "1.0 MB");
    OA_CHECK_TEXT(lib::size_text(1101005), "1.1 MB");
    OA_CHECK_TEXT(lib::size_text(43011223), "41.0 MB");
    OA_CHECK_TEXT(lib::size_text(100 * mib - 1), "100.0 MB");
    OA_CHECK_TEXT(lib::size_text(100 * mib), "100 MB");
    OA_CHECK_TEXT(lib::size_text(197132288), "188 MB");
    OA_CHECK_TEXT(lib::size_text(1024 * mib - 1), "1024 MB");
    OA_CHECK_TEXT(lib::size_text(1024 * mib), "1.0 GB");
    OA_CHECK_TEXT(lib::size_text(1536 * mib), "1.5 GB");
    OA_CHECK_TEXT(lib::progress_text(19083674, 64 * mib), "18.2 of 64.0 MB");
    OA_CHECK_TEXT(lib::progress_text(512, 900 * 1024), "1 of 900 KB");
    OA_CHECK_TEXT(lib::progress_text(50 * mib, 188 * mib), "50 of 188 MB");

    OA_CHECK_TEXT(lib::age_text(-5), "just now");
    OA_CHECK_TEXT(lib::age_text(59), "just now");
    OA_CHECK_TEXT(lib::age_text(60), "1 min ago");
    OA_CHECK_TEXT(lib::age_text(240), "4 min ago");
    OA_CHECK_TEXT(lib::age_text(3599), "59 min ago");
    OA_CHECK_TEXT(lib::age_text(3600), "1 h ago");
    OA_CHECK_TEXT(lib::age_text(172799), "47 h ago");
    OA_CHECK_TEXT(lib::age_text(172800), "2 days ago");

    lib::Inputs inputs = base_inputs();
    OA_CHECK_TEXT(lib::header_age_text(inputs), "list from 4 min ago");
    inputs.offline = true;
    OA_CHECK_TEXT(lib::header_age_text(inputs), "list from 4 min ago · offline");
    inputs.offline = false;
    inputs.list_age_seconds = -1;
    OA_CHECK_TEXT(lib::header_age_text(inputs), "no list yet");

    OA_CHECK_TEXT(lib::rules_text(old_rules), "9c1f 4e0b");
    OA_CHECK_TEXT(lib::rules_text(new_rules), "51d2 07aa");

    lib::Listing sample = listing(core, Kind::mod, "fixture-mod", "Fixture Mod");
    sample.version = "4.8";
    sample.revision = 3;
    OA_CHECK_TEXT(lib::version_text(sample), "4.8 · rev 3");
    sample.revision = 1;
    OA_CHECK_TEXT(lib::version_text(sample), "4.8");
    lib::Installed mod = installed(core, Kind::mod, "fixture-mod", "Fixture Mod");
    mod.version = "4.8";
    mod.revision = 3;
    OA_CHECK_TEXT(lib::version_text(mod), "4.8 · rev 3");
    mod.revision = 0;
    OA_CHECK_TEXT(lib::version_text(mod), "4.8");
    lib::Listing pack = listing(core, Kind::map_pack, "example-pack", "Example Pack");
    pack.version = "1.2";
    pack.maps = {{"Only Map", 2, "8x8"}};
    OA_CHECK_TEXT(lib::version_text(pack), "1.2 · 1 map");

    const auto range = [](std::string_view written) {
        return oa::formats::oamod::parse_engine_range(written).value_or(
            oa::formats::oamod::EngineRange{}
        );
    };
    OA_CHECK_TEXT(lib::engine_text(range(">= 0.8.0")), "0.8.0 or later");
    OA_CHECK_TEXT(lib::engine_short_text(range(">= 0.8.0")), "0.8.0+");
    OA_CHECK_TEXT(lib::engine_text(range(">= 0.8.0, < 0.9.0")), "0.8.0 or later, before 0.9.0");
    OA_CHECK_TEXT(lib::engine_short_text(range(">= 0.8.0, < 0.9.0")), "0.8.0+, before 0.9.0");
    OA_CHECK_TEXT(
        lib::engine_text(range("> 0.8.0, <= 1.0.0")), "later than 0.8.0, 1.0.0 or earlier"
    );
    OA_CHECK_TEXT(lib::engine_short_text(range("> 0.8.0, <= 1.0.0")), "after 0.8.0, up to 1.0.0");
    OA_CHECK_TEXT(lib::engine_text(range("= 0.8.1")), "exactly 0.8.1");
    OA_CHECK_TEXT(lib::engine_short_text(range("= 0.8.1")), "0.8.1 only");

    // A place no value names is kept as written.
    OA_CHECK_TEXT(
        lib::filled({}, "{name} and {other}", {{"name", "Fixture"}}), "Fixture and {other}"
    );
}

/// A made-up language for the lookup: every known pattern turned around.
///
/// @param english the pattern
/// @return its translation, or the pattern
std::string_view reversed_words(void*, std::string_view english) {
    if (english == lib::words::queue_installed)
        return "{name} ist installiert";
    if (english == lib::words::size_mb)
        return "{size} Mo";
    if (english == lib::words::one_decimal)
        return "{whole},{tenth}";
    if (english == lib::words::action_get)
        return "HOLEN";
    return english;
}

void test_lookup() {
    lib::TextHooks hooks;
    hooks.shown = reversed_words;
    OA_CHECK_TEXT(lib::size_text(43011223, hooks), "41,0 Mo");
    lib::Inputs inputs = base_inputs();
    inputs.listings.push_back(listing(core, Kind::mod, "get-mod", "Get Mod"));
    inputs.queue.push_back(queued(core, "other", "Fixture Mod", Phase::installed));
    lib::Library library;
    library.text = hooks;
    lib::refresh(library, std::move(inputs));
    OA_CHECK_TEXT(lib::queue_lines(library).first, "Fixture Mod ist installiert");
    const lib::Entry* get = at(library, core, Kind::mod, "get-mod");
    if (get != nullptr) {
        OA_CHECK_TEXT(lib::status_line(library, *get), "Not installed · 64,0 Mo");
        OA_CHECK_TEXT(lib::entry_actions(library, *get).front().text, "HOLEN");
    }
}

} // namespace

int main() {
    test_state_matrix();
    test_queue_states();
    test_blocked_reasons();
    test_installed_never_blocked();
    test_registries();
    test_tabs_filters_tags();
    test_order_without_search();
    test_selection();
    test_entry_actions();
    test_facts();
    test_status_lines_and_bylines();
    test_playing_note();
    test_queue_lines();
    test_change_texts();
    test_texts();
    test_lookup();
    return oa::test::check_exit_status();
}
