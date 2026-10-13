// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's model over made-up registries and packages: every state,
// every reason GET or UPDATE is off, built-in and added registries, a key
// installed from another registry, tabs, each tab's own filter, New, the
// filters' counts and tags, a tag left from another tab, an update a finished
// install has already reached, the order with no search, the update count, the
// Active tab's setup, the selection across refreshes, the buttons, the facts
// with the sample numbers, the status lines, the bylines, the playing note,
// the queue lines and every text.
#include "oa/data/languages.hpp"
#include "oa/data/mod_profile/registry.hpp"
#include "oa/test/check.hpp"
#include "oa/ui/library/library.hpp"
#include "oa/ui/library/mod_questions.hpp"
#include "oa/ui/library/text.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
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

/// Builds a Library from inputs, on its Mods tab.
///
/// @param inputs the inputs
/// @return the Library
lib::Library built(lib::Inputs inputs) {
    lib::Library library;
    lib::refresh(library, std::move(inputs));
    lib::set_tab(library, lib::Tab::mods);
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

/// The built-in languages, in the order Settings › Language lists them,
/// written as visible_keys writes a row.
///
/// @return the keys
std::vector<std::string> built_in_language_rows() {
    std::vector<std::string> keys;
    for (const oa::data::languages::Language* language : oa::data::languages::known_languages()) {
        if (language == nullptr || language->source != oa::data::languages::Source::built_in ||
            !oa::data::languages::playable(*language))
            continue;
        keys.push_back(lib::built_in_language_id(language->tag).key + "@");
    }
    return keys;
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

    // The packages, the base game, and the built-in languages refresh adds.
    OA_CHECK(library.entries.size() == 9 + built_in_language_rows().size());
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
            {Action::uninstall, true},
            {Action::open_folder, true},
        };
        OA_CHECK(actions_of(library, *fixture) == expected);
    }
    if (second != nullptr) {
        OA_CHECK_TEXT(lib::status_line(library, *second), "Playing");
        const std::vector<std::pair<Action, bool>> expected = {
            {Action::switch_off, false},
            {Action::roll_back, false},
            {Action::uninstall, true},
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
    OA_CHECK(library.tab == lib::Tab::mods && lib::shown_filter(library) == lib::Filter::all);
    const Keys every_mod = {
        "@base-game@",
        "gamma-mod@fixture-core",
        "beta-mod@fixture-core",
        "delta-mod@fixture-core",
        "alpha-mod@fixture-core",
    };
    OA_CHECK(visible_keys(library) == every_mod);
    lib::set_tab(library, lib::Tab::maps);
    OA_CHECK(visible_keys(library) == Keys({"example-pack@example-maps"}));
    lib::set_tab(library, lib::Tab::languages);
    Keys language_rows = built_in_language_rows();
    language_rows.push_back("xx@fixture-core");
    OA_CHECK(visible_keys(library) == language_rows);

    lib::set_tab(library, lib::Tab::mods);
    lib::set_filter(library, lib::Filter::installed);
    const Keys installed_mods = {
        "@base-game@",
        "gamma-mod@fixture-core",
        "beta-mod@fixture-core",
        "delta-mod@fixture-core",
    };
    OA_CHECK(visible_keys(library) == installed_mods);
    lib::set_filter(library, lib::Filter::updates);
    OA_CHECK(visible_keys(library) == Keys({"gamma-mod@fixture-core"}));

    // Each tab keeps its own filter: the Updates tab opens on Updates, of
    // every kind, whatever Mods shows.
    lib::set_filter(library, lib::Filter::installed);
    lib::set_tab(library, lib::Tab::updates);
    OA_CHECK(lib::shown_filter(library) == lib::Filter::updates);
    const Keys every_update = {
        "example-pack@example-maps",
        "gamma-mod@fixture-core",
    };
    OA_CHECK(visible_keys(library) == every_update);
    OA_CHECK(lib::update_count(library) == 2 && lib::updates_shown(library) == 2);
    lib::set_tab(library, lib::Tab::mods);
    OA_CHECK(lib::shown_filter(library) == lib::Filter::installed);
    OA_CHECK(visible_keys(library) == installed_mods);

    // The Updates tab's filters work as the others do, across every kind.
    lib::set_tab(library, lib::Tab::updates);
    lib::set_filter(library, lib::Filter::all);
    const Keys every_entry = {
        "example-pack@example-maps",
        "gamma-mod@fixture-core",
        "beta-mod@fixture-core",
        "delta-mod@fixture-core",
        "alpha-mod@fixture-core",
        "xx@fixture-core",
    };
    OA_CHECK(visible_keys(library) == every_entry);
    OA_CHECK(lib::updates_shown(library) == 2);
    lib::set_filter(library, lib::Filter::installed);
    OA_CHECK(
        visible_keys(library) == Keys({
                                     "example-pack@example-maps",
                                     "gamma-mod@fixture-core",
                                     "beta-mod@fixture-core",
                                     "delta-mod@fixture-core",
                                 })
    );
    lib::set_tag(library, "total-conversion");
    OA_CHECK(visible_keys(library) == Keys({"gamma-mod@fixture-core"}));
    lib::set_tag(library, "");
    lib::set_filter(library, lib::Filter::updates);

    // Each filter's count is of the tab's entries it keeps, whatever the tag and the search.
    lib::set_tab(library, lib::Tab::mods);
    for (const std::string_view tag : {std::string_view(), std::string_view("units")}) {
        lib::set_tag(library, tag);
        for (const std::string_view query : {std::string_view(), std::string_view("gamma")}) {
            lib::set_query(library, query);
            OA_CHECK(lib::filter_count(library, lib::Filter::all) == 4);
            OA_CHECK(lib::filter_count(library, lib::Filter::newest) == 0);
            OA_CHECK(lib::filter_count(library, lib::Filter::installed) == 3);
            OA_CHECK(lib::filter_count(library, lib::Filter::updates) == 1);
        }
    }
    lib::set_query(library, "");
    lib::set_tag(library, "");
    OA_CHECK_TEXT(lib::filter_caption(library, lib::Filter::installed), "Installed (3)");
    OA_CHECK_TEXT(lib::filter_caption(library, lib::Filter::newest), "New (0)");
    lib::set_tab(library, lib::Tab::updates);
    OA_CHECK_TEXT(lib::filter_caption(library, lib::Filter::all), "All (6)");
    OA_CHECK_TEXT(lib::filter_caption(library, lib::Filter::updates), "Updates (2)");
    lib::set_tab(library, lib::Tab::maps);
    OA_CHECK_TEXT(lib::filter_caption(library, lib::Filter::all), "All (1)");
    lib::set_tab(library, lib::Tab::active);
    OA_CHECK(library.visible.empty() && !library.selected);
    OA_CHECK(lib::filter_count(library, lib::Filter::all) == 0);
    OA_CHECK(lib::tags_in_tab(library).empty());

    lib::set_tab(library, lib::Tab::mods);
    lib::set_filter(library, lib::Filter::all);
    lib::set_tag(library, "units");
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
    OA_CHECK_TEXT(lib::tab_text(lib::Tab::registries, true), "Reg");
    OA_CHECK_TEXT(lib::tab_text(lib::Tab::registries, false), "Registries");
    OA_CHECK_TEXT(lib::tab_text(lib::Tab::mods, true), "Mods");
    OA_CHECK_TEXT(lib::tab_text(lib::Tab::active, true), "Active");
    OA_CHECK_TEXT(lib::tab_text(lib::Tab::active, false), "Active");
    OA_CHECK_TEXT(lib::filter_text(lib::Filter::installed), "Installed");
    OA_CHECK_TEXT(lib::filter_text(lib::Filter::newest), "New");
    OA_CHECK_TEXT(lib::state_text(State::playing), "PLAYING");
}

void test_opens_on_active() {
    lib::Inputs inputs = base_inputs();
    inputs.listings.push_back(listing(core, Kind::mod, "alpha-mod", "Alpha Mod"));
    lib::Library library;
    lib::refresh(library, inputs);
    // A Library opens on Active, which lists no entry and selects none.
    OA_CHECK(library.tab == lib::Tab::active);
    OA_CHECK(library.visible.empty() && !library.selected);
    lib::open_details(library);
    OA_CHECK(!library.details_open);
    lib::set_query(library, "alpha");
    OA_CHECK(library.visible.empty());
    lib::set_query(library, "");
    lib::set_tab(library, lib::Tab::mods);
    // The base game entry leads Mods, ahead of the one package.
    OA_CHECK(library.selected == lib::base_game_id() && library.visible.size() == 2);
    // A refresh keeps the tab shown.
    lib::refresh(library, inputs);
    OA_CHECK(library.tab == lib::Tab::mods);
}

void test_new_filter() {
    lib::Inputs inputs = base_inputs();
    // Twelve dated mods, two of them on one day, and one with no day; the
    // undated one was released most often, which says nothing of when.
    const std::vector<std::pair<std::string, std::string>> dated = {
        {"m01", "2026-01-05"},
        {"m02", "2026-02-11"},
        {"m03", "2026-03-02"},
        {"m04", "2026-04-20"},
        {"m05", "2026-05-01"},
        {"m06", "2026-06-15"},
        {"m07", "2026-07-07"},
        {"m08", "2026-08-30"},
        {"zz-tie", "2026-09-09"},
        {"aa-tie", "2026-09-09"},
        {"m11", "2026-10-01"},
        {"m12", "2026-10-11"},
    };
    for (const auto& [key, day] : dated) {
        lib::Listing made = listing(core, Kind::mod, key, key);
        made.published = day;
        inputs.listings.push_back(std::move(made));
    }
    lib::Listing undated = listing(core, Kind::mod, "undated", "Undated");
    undated.release = 90;
    undated.revision = 40;
    inputs.listings.push_back(undated);
    lib::Listing pack = listing(example, Kind::map_pack, "fresh-pack", "Fresh Pack");
    pack.published = "2026-10-12";
    pack.tags = {"desert"};
    inputs.listings.push_back(pack);
    inputs.installed.push_back(installed(core, Kind::mod, "m01", "m01"));
    lib::Library library = built(std::move(inputs));

    using Keys = std::vector<std::string>;
    lib::set_filter(library, lib::Filter::newest);
    // The ten with the latest days, newest first, ties by name.
    const Keys newest_mods = {
        "m12@fixture-core",
        "m11@fixture-core",
        "aa-tie@fixture-core",
        "zz-tie@fixture-core",
        "m08@fixture-core",
        "m07@fixture-core",
        "m06@fixture-core",
        "m05@fixture-core",
        "m04@fixture-core",
        "m03@fixture-core",
    };
    OA_CHECK(visible_keys(library) == newest_mods);
    OA_CHECK(lib::filter_count(library, lib::Filter::newest) == lib::newest_count);
    OA_CHECK(lib::filter_count(library, lib::Filter::all) == 13);
    OA_CHECK_TEXT(lib::filter_caption(library, lib::Filter::newest), "New (10)");
    // A search narrows New and leaves its count; an old one is never found there.
    lib::set_query(library, "m0");
    OA_CHECK(
        visible_keys(library) == Keys({
                                     "m08@fixture-core",
                                     "m07@fixture-core",
                                     "m06@fixture-core",
                                     "m05@fixture-core",
                                     "m04@fixture-core",
                                     "m03@fixture-core",
                                 })
    );
    OA_CHECK(lib::filter_count(library, lib::Filter::newest) == lib::newest_count);
    lib::set_query(library, "undated");
    OA_CHECK(library.visible.empty());
    lib::set_query(library, "");

    // New on Maps holds maps alone; on Updates, every kind.
    lib::set_tab(library, lib::Tab::maps);
    OA_CHECK(lib::shown_filter(library) == lib::Filter::all);
    lib::set_filter(library, lib::Filter::newest);
    OA_CHECK(visible_keys(library) == Keys({"fresh-pack@example-maps"}));
    using Counts = std::vector<std::pair<std::string, std::size_t>>;
    OA_CHECK(lib::tags_in_tab(library) == Counts({{"desert", 1}}));
    lib::set_tab(library, lib::Tab::updates);
    lib::set_filter(library, lib::Filter::newest);
    const std::vector<std::string> every_kind = visible_keys(library);
    OA_CHECK(every_kind.size() == lib::newest_count);
    OA_CHECK(!every_kind.empty() && every_kind.front() == "fresh-pack@example-maps");
    OA_CHECK(
        std::find(every_kind.begin(), every_kind.end(), "m03@fixture-core") == every_kind.end()
    );
    // Mods kept its own New.
    lib::set_tab(library, lib::Tab::mods);
    OA_CHECK(visible_keys(library) == newest_mods);
}

void test_active_setup() {
    lib::Inputs inputs = base_inputs();
    lib::Listing ridge = listing(core, Kind::mod, "ridge", "Ridge");
    ridge.badge = lib::Badge{std::vector<uint8_t>(4 * 4 * 4, 0x80), 4, 4};
    inputs.listings.push_back(ridge);
    lib::Installed ridge_installed = installed(core, Kind::mod, "ridge", "Ridge (folder)");
    ridge_installed.version = "4.8";
    ridge_installed.revision = 3;
    ridge_installed.playing = true;
    inputs.installed.push_back(ridge_installed);
    inputs.playing_mod = lib::PlayingMod{"Ridge (settings)", "4.8", std::nullopt};
    inputs.language = {"Deutsch", "Deutsch (Fixture)", "1.2"};
    inputs.map_packs.push_back({"zeta pack", {{"Last Map", 1, "8x8"}}});
    inputs.map_packs.push_back({"Alpha Pack", {{"Ridgeline", 4, "16x16"}, {"Twin", 2, "12x12"}}});
    lib::MapPicture minimap;
    minimap.width = 8;
    minimap.height = 2;
    minimap.rgba.assign(8U * 2U * 4U, 0);
    minimap.rgba[0] = 255;
    minimap.rgba[3] = 255;
    inputs.map_packs[0].maps[0].preview = std::move(minimap);
    inputs.map_packs[0].maps[0].description = "The last island.";
    lib::Installed alpha = installed(core, Kind::map_pack, "alpha", "Alpha Pack");
    alpha.badge = lib::Badge{std::vector<uint8_t>(4, 0xff), 1, 1};
    inputs.installed.push_back(std::move(alpha));
    lib::Library library = built(inputs);

    // The installed mod marked playing comes first, by its listing's name and badge.
    lib::ActiveSetup setup = lib::active_setup(library);
    OA_CHECK_TEXT(setup.mod_name, "Ridge");
    OA_CHECK_TEXT(setup.mod_version, "4.8 · rev 3");
    OA_CHECK_TEXT(setup.mod_release, "4.8 (rev 3)");
    OA_CHECK_TEXT(setup.mod_author, "by Fixture Author");
    OA_CHECK_TEXT(setup.mod_state, "Playing");
    OA_CHECK(setup.base_title.empty());
    OA_CHECK_TEXT(setup.mod_line, "Ridge 4.8 · rev 3");
    OA_CHECK(setup.mod_badge != nullptr && setup.mod_badge->width == 4);
    OA_CHECK_TEXT(setup.language_line, "Deutsch (Fixture) 1.2");
    // Folded title, then folded pack: Last Map, Ridgeline, Twin.
    OA_CHECK(setup.maps.size() == 3);
    if (setup.maps.size() == 3) {
        OA_CHECK_TEXT(setup.maps[0].name, "Last Map");
        OA_CHECK_TEXT(setup.maps[0].pack_name, "zeta pack");
        OA_CHECK_TEXT(setup.maps[0].players, "1 player");
        OA_CHECK_TEXT(setup.maps[0].line, "Last Map · 1 player · 8x8");
        OA_CHECK(setup.maps[0].from.empty());
        OA_CHECK_TEXT(setup.maps[0].description, "The last island.");
        OA_CHECK(setup.maps[0].preview != nullptr && setup.maps[0].preview->width == 8);
        OA_CHECK(setup.maps[0].pack_id.key.empty());
        OA_CHECK_TEXT(setup.maps[1].name, "Ridgeline");
        OA_CHECK_TEXT(setup.maps[1].pack_name, "Alpha Pack");
        OA_CHECK_TEXT(setup.maps[1].players, "4 players");
        OA_CHECK_TEXT(setup.maps[1].line, "Ridgeline · 4 players · 16x16");
        OA_CHECK_TEXT(setup.maps[1].from, "From Alpha Pack");
        OA_CHECK(setup.maps[1].preview == nullptr);
        OA_CHECK(setup.maps[1].pack_id.kind == Kind::map_pack);
        OA_CHECK(setup.maps[1].pack_id.key == "alpha");
        OA_CHECK(setup.maps[1].pack_id.registry == core);
        OA_CHECK_TEXT(setup.maps[2].name, "Twin");
        OA_CHECK_TEXT(setup.maps[2].pack_name, "Alpha Pack");
        OA_CHECK_TEXT(setup.maps[2].from, "From Alpha Pack");
        OA_CHECK(setup.maps[2].pack_id.key == "alpha");
    }

    // Without one, the mod the app says is played; a pack without a version is named alone.
    inputs.installed[0].playing = false;
    inputs.language = {"Deutsch", "Deutsch (Fixture)", ""};
    lib::refresh(library, inputs);
    setup = lib::active_setup(library);
    OA_CHECK_TEXT(setup.mod_line, "Ridge (settings) 4.8");
    OA_CHECK_TEXT(setup.mod_release, "4.8");
    OA_CHECK_TEXT(setup.mod_state, "Playing");
    OA_CHECK(setup.mod_author.empty());
    OA_CHECK(setup.mod_badge == nullptr);
    OA_CHECK_TEXT(setup.language_line, "Deutsch (Fixture)");

    // No mod, a language the game holds itself, English, and no map packs.
    inputs.playing_mod.reset();
    inputs.language = {"Français", "", ""};
    inputs.map_packs.clear();
    lib::refresh(library, inputs);
    setup = lib::active_setup(library);
    OA_CHECK(setup.mod_name.empty() && setup.mod_badge == nullptr);
    library.inputs.base_game_badge = lib::Badge{std::vector<uint8_t>(2 * 2 * 4, 9), 2, 2};
    setup = lib::active_setup(library);
    OA_CHECK(library.inputs.base_game_badge.has_value() && setup.mod_badge == nullptr);
    const lib::ActiveContent face = lib::active_content(library);
    OA_CHECK(face.badge == &*library.inputs.base_game_badge);
    OA_CHECK_TEXT(face.letters, "TA");
    library.inputs.base_game_badge.reset();
    setup = lib::active_setup(library);
    OA_CHECK(setup.mod_badge == nullptr);
    OA_CHECK(lib::active_content(library).badge == nullptr);
    OA_CHECK_TEXT(setup.mod_line, "No Mod");
    OA_CHECK_TEXT(setup.base_title, "No Mod");
    OA_CHECK_TEXT(setup.base_look, "Total Annihilation as it was made");
    OA_CHECK(setup.mod_state.empty() && setup.mod_release.empty() && setup.mod_author.empty());
    OA_CHECK_TEXT(setup.language_name, "Français");
    OA_CHECK_TEXT(setup.language_note, "Built in");
    OA_CHECK(setup.language_english.empty() && setup.language_version.empty());
    OA_CHECK_TEXT(setup.language_line, "Français · built in");
    OA_CHECK(setup.maps.empty());
    inputs.language = {};
    lib::refresh(library, inputs);
    setup = lib::active_setup(library);
    OA_CHECK_TEXT(setup.language_line, "English · built in");
    OA_CHECK_TEXT(setup.language_name, "English");
    OA_CHECK_TEXT(setup.language_note, "Built in");

    // A pack in use names itself, its English name, its revision and its coverage.
    inputs.language.name = "Deutsch";
    inputs.language.pack = "Deutsch (Fixture)";
    inputs.language.version = "1.2";
    inputs.language.english = "German";
    inputs.language.revision = 4;
    lib::Listing deutsch = listing(core, Kind::language, "de", "Deutsch");
    deutsch.coverage = 0.92;
    inputs.listings.push_back(deutsch);
    lib::Installed deutsch_installed = installed(core, Kind::language, "de", "Deutsch (folder)");
    deutsch_installed.version = "1.2";
    deutsch_installed.revision = 4;
    deutsch_installed.in_use = true;
    inputs.installed.push_back(deutsch_installed);
    lib::refresh(library, inputs);
    setup = lib::active_setup(library);
    OA_CHECK_TEXT(setup.language_name, "Deutsch");
    OA_CHECK_TEXT(setup.language_english, "German");
    OA_CHECK_TEXT(setup.language_pack, "Deutsch (Fixture)");
    OA_CHECK_TEXT(setup.language_version, "1.2 (rev 4)");
    OA_CHECK_TEXT(setup.language_coverage, "Coverage 92%");
    OA_CHECK(setup.language_note.empty());
    OA_CHECK(setup.language_badge == nullptr);
    OA_CHECK_TEXT(setup.language_line, "Deutsch (Fixture) 1.2");
}

void test_order_without_search() {
    lib::Inputs inputs = base_inputs();
    lib::Registry other;
    other.id = "another";
    other.name = "Another";
    inputs.registries.push_back(other);
    // Blocked GET, GET, installed, playing and update, named so names alone
    // would order them otherwise. The order is the group, then the name,
    // then the registry. Nothing about how often a package was fetched
    // enters it.
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
        "@base-game@",
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

    // The first refresh selects the first visible entry, the base game.
    const lib::EntryId base = lib::base_game_id();
    OA_CHECK(library.selected == base && !library.details_open);
    lib::move_selection(library, 1);
    OA_CHECK(library.selected == alpha);
    lib::move_selection(library, 5);
    OA_CHECK(library.selected == gamma);
    lib::move_selection(library, -9);
    OA_CHECK(library.selected == base);
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
    OA_CHECK(library.selected == base && !library.details_open);

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
                                                       {Action::uninstall, true},
                                                       {Action::open_folder, true}}
                                                  )
        );
        OA_CHECK_TEXT(lib::entry_actions(library, *update_entry)[2].text, "ROLL BACK");
    }
    const lib::Entry* own = at(library, "", Kind::mod, "own-mod");
    if (own != nullptr)
        OA_CHECK(
            actions_of(library, *own) ==
            Buttons(
                {{Action::play_now, true}, {Action::uninstall, true}, {Action::open_folder, true}}
            )
        );
    // A map pack is never played.
    const lib::Entry* pack = at(library, core, Kind::map_pack, "example-pack");
    if (pack != nullptr)
        OA_CHECK(
            actions_of(library, *pack) ==
            Buttons({{Action::uninstall, true}, {Action::open_folder, true}})
        );
    // A failure that cannot be retried offers GET again.
    const lib::Entry* failed = at(library, core, Kind::mod, "failed-mod");
    if (failed != nullptr) {
        OA_CHECK(actions_of(library, *failed) == Buttons({{Action::get, true}}));
        OA_CHECK_TEXT(lib::status_line(library, *failed), "The file's SHA-256 didn't match");
    }
    const lib::Entry* update_queued = at(library, core, Kind::mod, "update-queued");
    if (update_queued != nullptr)
        OA_CHECK(
            actions_of(library, *update_queued) == Buttons(
                                                       {{Action::cancel, true},
                                                        {Action::play_now, true},
                                                        {Action::uninstall, false},
                                                        {Action::open_folder, true}}
                                                   )
        );
    OA_CHECK_TEXT(lib::action_text(Action::update_all), "UPDATE ALL");
    OA_CHECK_TEXT(lib::action_text(Action::play_now), "PLAY NOW");
    OA_CHECK_TEXT(lib::action_text(Action::switch_off), "SWITCH OFF");
    OA_CHECK_TEXT(lib::action_text(Action::check_now), "Refresh");
    OA_CHECK_TEXT(lib::action_text(Action::settings), "SETTINGS…");
    OA_CHECK_TEXT(lib::action_text(Action::close), "CLOSE");
}

void test_switch_off() {
    // The mod being played offers SWITCH OFF where the others offer PLAY
    // NOW, whatever its registry, and a map pack offers neither.
    lib::Inputs inputs = base_inputs();
    inputs.listings.push_back(listing(core, Kind::mod, "fixture-mod", "Fixture Mod"));
    inputs.installed.push_back(installed(core, Kind::mod, "fixture-mod", "Fixture Mod"));
    inputs.installed.back().playing = true;
    inputs.installed.back().kept = lib::Kept{"4.8", 2};
    inputs.installed.push_back(installed("", Kind::mod, "own-mod", "Own Mod"));
    inputs.installed.push_back(installed(core, Kind::map_pack, "example-pack", "Example Pack"));
    lib::Library library = built(inputs);
    using Buttons = std::vector<std::pair<Action, bool>>;
    const lib::EntryId played{std::string(core), Kind::mod, "fixture-mod"};
    const lib::EntryId own{"", Kind::mod, "own-mod"};
    const auto buttons_of = [&library](const lib::EntryId& id) {
        const lib::Entry* entry = lib::find_entry(library, id);
        OA_CHECK(entry != nullptr);
        return entry != nullptr ? actions_of(library, *entry) : Buttons{};
    };
    OA_CHECK(
        buttons_of(played) == Buttons(
                                  {{Action::switch_off, true},
                                   {Action::roll_back, true},
                                   {Action::uninstall, true},
                                   {Action::open_folder, true}}
                              )
    );
    if (const lib::Entry* entry = lib::find_entry(library, played); entry != nullptr)
        OA_CHECK_TEXT(lib::entry_actions(library, *entry)[0].text, "SWITCH OFF");
    OA_CHECK(
        buttons_of(own) ==
        Buttons({{Action::play_now, true}, {Action::uninstall, true}, {Action::open_folder, true}})
    );
    OA_CHECK(
        buttons_of({std::string(core), Kind::map_pack, "example-pack"}) ==
        Buttons({{Action::uninstall, true}, {Action::open_folder, true}})
    );
    lib::select(library, played);
    OA_CHECK_TEXT(lib::playing_note(library), "");

    // While the command line chose the mod, neither can be pressed, and an
    // installed mod's note says why; a map pack's says nothing.
    inputs.mod_from_command_line = true;
    lib::refresh(library, inputs);
    OA_CHECK(
        buttons_of(played) == Buttons(
                                  {{Action::switch_off, false},
                                   {Action::roll_back, true},
                                   {Action::uninstall, true},
                                   {Action::open_folder, true}}
                              )
    );
    OA_CHECK(
        buttons_of(own) ==
        Buttons({{Action::play_now, false}, {Action::uninstall, true}, {Action::open_folder, true}})
    );
    OA_CHECK_TEXT(lib::playing_note(library), "The command line chose this run's mod.");
    lib::select(library, own);
    OA_CHECK_TEXT(lib::playing_note(library), "The command line chose this run's mod.");
    lib::set_tab(library, lib::Tab::maps);
    lib::select(library, {std::string(core), Kind::map_pack, "example-pack"});
    OA_CHECK(library.selected && library.selected->kind == Kind::map_pack);
    OA_CHECK_TEXT(lib::playing_note(library), "");

    // The note on an update of the mod being played follows it.
    inputs.updates.push_back(update(core, Kind::mod, "fixture-mod"));
    lib::set_tab(library, lib::Tab::mods);
    lib::refresh(library, inputs);
    lib::select(library, played);
    OA_CHECK(library.selected == played);
    OA_CHECK_TEXT(
        lib::playing_note(library),
        "The command line chose this run's mod. Finishes when you leave Fixture Mod."
    );
}

void test_own_mod_description() {
    // The details show the listing's summary, else the installed manifest's
    // description, as for a mod of the player's own; a folder with no
    // oamod.yaml says so in its status line, installed or played.
    lib::Inputs inputs = base_inputs();
    inputs.listings.push_back(listing(core, Kind::mod, "fixture-mod", "Fixture Mod"));
    inputs.listings.back().summary = "The listing's summary.";
    inputs.installed.push_back(installed(core, Kind::mod, "fixture-mod", "Fixture Mod"));
    inputs.installed.back().description = "The manifest's description.";
    inputs.installed.push_back(installed("", Kind::mod, "own-mod", "Own Mod"));
    inputs.installed.back().description = "A mod of the player's own.";
    inputs.installed.push_back(installed("", Kind::mod, "plain", "plain"));
    inputs.installed.back().version = "N/A";
    inputs.installed.back().without_manifest = true;
    inputs.installed.push_back(installed("", Kind::mod, "plain-played", "plain-played"));
    inputs.installed.back().without_manifest = true;
    inputs.installed.back().playing = true;
    inputs.installed.push_back(installed("", Kind::mod, "silent", "Silent"));
    const lib::Library library = built(inputs);
    const auto summary_of = [&library](std::string_view registry, std::string_view key) {
        const lib::Entry* entry = at(library, registry, Kind::mod, key);
        OA_CHECK(entry != nullptr);
        return entry != nullptr ? std::string(lib::entry_summary(*entry)) : std::string("?");
    };
    const auto status_of = [&library](std::string_view key) {
        const lib::Entry* entry = at(library, "", Kind::mod, key);
        OA_CHECK(entry != nullptr);
        return entry != nullptr ? lib::status_line(library, *entry) : std::string("?");
    };
    OA_CHECK_TEXT(summary_of(core, "fixture-mod"), "The listing's summary.");
    OA_CHECK_TEXT(summary_of("", "own-mod"), "A mod of the player's own.");
    OA_CHECK_TEXT(summary_of("", "plain"), "");
    OA_CHECK_TEXT(summary_of("", "silent"), "");
    OA_CHECK_TEXT(status_of("own-mod"), "Installed");
    OA_CHECK_TEXT(status_of("plain"), "No oamod.yaml present");
    OA_CHECK_TEXT(status_of("plain-played"), "No oamod.yaml present");

    // A listing with no summary leaves the manifest's description.
    inputs.listings.back().summary.clear();
    const lib::Library unsummed = built(inputs);
    if (const lib::Entry* entry = at(unsummed, core, Kind::mod, "fixture-mod"))
        OA_CHECK_TEXT(lib::entry_summary(*entry), "The manifest's description.");
}

void test_check_now() {
    // The header's refresh icon can be pressed while a registry is on to
    // ask, none is being asked, and no game is being played.
    lib::Inputs inputs = base_inputs();
    OA_CHECK(!lib::can_check_now(built(inputs)));
    inputs.can_check = true;
    OA_CHECK(lib::can_check_now(built(inputs)));
    inputs.checking = true;
    OA_CHECK(!lib::can_check_now(built(inputs)));
    inputs.checking = false;
    inputs.in_match = true;
    OA_CHECK(!lib::can_check_now(built(inputs)));
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
        labels ==
        std::vector<std::string>(
            {"Installed", "Latest", "Author", "Size", "Base", "Needs", "Hacks", "Rules", "From"}
        )
    );
    if (const lib::Fact* installed_fact = fact(list, "Installed")) {
        OA_CHECK_TEXT(installed_fact->long_value, "4.8 (rev 3)");
        OA_CHECK_TEXT(installed_fact->short_value, "4.8 (rev 3)");
        OA_CHECK(installed_fact->mark == Mark::none);
    }
    if (const lib::Fact* latest = fact(list, "Latest")) {
        OA_CHECK_TEXT(latest->long_value, "4.8 (rev 4)");
        OA_CHECK_TEXT(latest->short_value, "4.8 (rev 4)");
    }
    // The listing names no author: its publisher stands in.
    if (const lib::Fact* author = fact(list, "Author"))
        OA_CHECK_TEXT(author->long_value, "Fixture Publisher");
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
        OA_CHECK(list.size() == 3);
        if (const lib::Fact* installed_fact = fact(list, "Installed"))
            OA_CHECK_TEXT(installed_fact->long_value, "1.0 (rev 1)");
        OA_CHECK(fact(list, "Latest") == nullptr && fact(list, "Author") == nullptr);
        if (const lib::Fact* rules = fact(list, "Rules"))
            OA_CHECK_TEXT(rules->long_value, "9c1f 4e0b");
        if (const lib::Fact* from = fact(list, "From"))
            OA_CHECK_TEXT(from->long_value, "Your own copy");
    }

    // A revision of 0 is left out of its fact, an empty version leaves the
    // revision alone, a side with neither is left out, and the installed
    // manifest's author stands in for a listing that names none.
    inputs = base_inputs();
    lib::Listing pack_r0 = listing(core, Kind::map_pack, "pack-r0", "Pack R0");
    pack_r0.revision = 0;
    pack_r0.author.clear();
    inputs.listings.push_back(pack_r0);
    lib::Installed pack_installed = installed(core, Kind::map_pack, "pack-r0", "Pack R0");
    pack_installed.version.clear();
    pack_installed.revision = 3;
    pack_installed.author = "The Pack Author";
    inputs.installed.push_back(pack_installed);
    lib::Listing language_none = listing(core, Kind::language, "xx", "No Version");
    language_none.version.clear();
    language_none.revision = 0;
    language_none.author.clear();
    inputs.listings.push_back(language_none);
    lib::refresh(library, inputs);
    if (const lib::Entry* entry = at(library, core, Kind::map_pack, "pack-r0")) {
        list = lib::facts(library, *entry);
        if (const lib::Fact* installed_fact = fact(list, "Installed"))
            OA_CHECK_TEXT(installed_fact->long_value, "rev 3");
        if (const lib::Fact* latest = fact(list, "Latest"))
            OA_CHECK_TEXT(latest->long_value, "1.0");
        if (const lib::Fact* author = fact(list, "Author"))
            OA_CHECK_TEXT(author->long_value, "The Pack Author");
        OA_CHECK(fact(list, "Installed") != nullptr && fact(list, "Latest") != nullptr);
        OA_CHECK(fact(list, "Author") != nullptr);
    }
    if (const lib::Entry* entry = at(library, core, Kind::language, "xx")) {
        list = lib::facts(library, *entry);
        OA_CHECK(fact(list, "Installed") == nullptr && fact(list, "Latest") == nullptr);
        OA_CHECK(fact(list, "Author") == nullptr);
        OA_CHECK(!list.empty() && list.front().long_label == "Size");
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

    lib::Listing named = listing(core, Kind::mod, "named", "Named");
    named.author = "Fixture Author";
    inputs.listings.push_back(named);
    lib::Listing unnamed = listing(core, Kind::mod, "unnamed", "Unnamed");
    unnamed.author.clear();
    unnamed.publisher.clear();
    inputs.listings.push_back(unnamed);
    lib::refresh(library, inputs);
    if (const lib::Entry* with_maker = at(library, core, Kind::mod, "named"))
        OA_CHECK_TEXT(lib::byline(*with_maker), "by Fixture Author");
    if (const lib::Entry* no_maker = at(library, core, Kind::mod, "unnamed"))
        OA_CHECK_TEXT(lib::byline(*no_maker), "");

    inputs.updates[0].to_version = "4.9";
    lib::refresh(library, inputs);
    entry = at(library, core, Kind::mod, "fixture-mod");
    if (entry != nullptr)
        OA_CHECK_TEXT(
            lib::status_line(library, *entry), "Version 4.9 available · 3.2 MB to download"
        );

    // No revision named, the version the same or none: an update, never "Revision 0".
    inputs.updates[0].to_version = "4.8";
    inputs.updates[0].to_revision = 0;
    lib::refresh(library, inputs);
    entry = at(library, core, Kind::mod, "fixture-mod");
    if (entry != nullptr)
        OA_CHECK_TEXT(lib::status_line(library, *entry), "Update available · 3.2 MB to download");
    inputs.updates[0].to_version.clear();
    lib::refresh(library, inputs);
    entry = at(library, core, Kind::mod, "fixture-mod");
    if (entry != nullptr)
        OA_CHECK_TEXT(lib::status_line(library, *entry), "Update available · 3.2 MB to download");
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

    // A side whose revision is 0 where its revision would name it is the
    // old or the new copy, never "revision 0"; so is a side with no version
    // where its version would, unless a revision names it.
    note.to_version = "4.8";
    note.to_revision = 0;
    names = lib::change_texts(mod, note);
    OA_CHECK_TEXT(names.from, "revision 3");
    OA_CHECK_TEXT(names.to, "the new copy");
    OA_CHECK_TEXT(names.from_short, "rev 3");
    OA_CHECK_TEXT(names.to_short, "new");
    mod.revision = 0;
    names = lib::change_texts(mod, note);
    OA_CHECK_TEXT(names.from, "the old copy");
    OA_CHECK_TEXT(names.from_short, "old");
    mod.version.clear();
    mod.revision = 2;
    note.to_version = "5.0";
    names = lib::change_texts(mod, note);
    OA_CHECK_TEXT(names.from, "revision 2");
    OA_CHECK_TEXT(names.to, "5.0");
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

    OA_CHECK_TEXT(lib::release_text("4.8", 3), "4.8 (rev 3)");
    OA_CHECK_TEXT(lib::release_text("4.8", 1), "4.8 (rev 1)");
    OA_CHECK_TEXT(lib::release_text("1.0", 0), "1.0");
    OA_CHECK_TEXT(lib::release_text("", 3), "rev 3");
    OA_CHECK_TEXT(lib::release_text("", 0), "");

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

/// The Registries tab's initials, ages, refresh line, subtitle and note.
void test_registry_texts() {
    OA_CHECK_TEXT(lib::registry_initials("Core Prime"), "CP");
    OA_CHECK_TEXT(lib::registry_initials("Example Maps"), "EM");
    OA_CHECK_TEXT(lib::registry_initials("Kirby's laptop"), "KL");
    OA_CHECK_TEXT(lib::registry_initials("ridge"), "RI");
    OA_CHECK_TEXT(lib::registry_initials("North archive of maps"), "NAO");
    OA_CHECK_TEXT(lib::registry_initials("  spaced  out  "), "SO");
    OA_CHECK_TEXT(lib::registry_initials("简体中文"), "简体");
    OA_CHECK_TEXT(lib::registry_initials(""), "");

    OA_CHECK_TEXT(lib::registry_age_text(10), "just now");
    OA_CHECK_TEXT(lib::registry_age_text(4 * 60), "4 min ago");
    OA_CHECK_TEXT(lib::registry_age_text(3 * 3600 + 5), "3 h ago");
    OA_CHECK_TEXT(lib::registry_age_text(30 * 3600), "1 day ago");
    OA_CHECK_TEXT(lib::registry_age_text(5 * 86400), "5 days ago");

    lib::Registry built;
    built.name = "Core Prime";
    built.built_in = true;
    built.packages = 218;
    built.checked_ago = 4 * 60;
    OA_CHECK_TEXT(lib::registry_subtitle(built), "218 packages");
    OA_CHECK_TEXT(lib::registry_refresh_line(built), "refreshed 4 min ago");
    built.checked_ago.reset();
    OA_CHECK_TEXT(lib::registry_subtitle(built), "218 packages");
    OA_CHECK_TEXT(lib::registry_refresh_line(built), "never refreshed");
    built.checked_ago = 2 * 3600;
    built.unreachable = true;
    OA_CHECK_TEXT(lib::registry_subtitle(built), "218 packages");
    OA_CHECK_TEXT(lib::registry_refresh_line(built), "couldn't reach · 2 h ago");
    built.checked_ago.reset();
    OA_CHECK_TEXT(lib::registry_refresh_line(built), "couldn't reach");
    built.checked_ago = 2 * 86400;
    OA_CHECK_TEXT(lib::registry_refresh_line(built), "couldn't reach · 2 days ago");

    lib::Registry added;
    added.address = "maps.example.org";
    added.packages = 31;
    OA_CHECK_TEXT(lib::registry_subtitle(added), "31 packages · not reviewed");
    OA_CHECK_TEXT(lib::registry_refresh_line(added), "never refreshed");
    added.mirrors = 2;
    OA_CHECK_TEXT(lib::registry_subtitle(added), "31 packages · not reviewed · 2 mirrors");
    added.mirrors = 1;
    added.unreachable = true;
    added.checked_ago = 60;
    OA_CHECK_TEXT(lib::registry_subtitle(added), "31 packages · not reviewed · 1 mirror");
    OA_CHECK_TEXT(lib::registry_refresh_line(added), "couldn't reach · 1 min ago");
    added = {};
    added.address = "192.168.1.20";
    added.packages = 1;
    OA_CHECK_TEXT(lib::registry_subtitle(added), "1 package · not reviewed");

    lib::Registry core_prime;
    core_prime.name = "Core Prime";
    core_prime.built_in = true;
    lib::Registry north;
    north.name = "North";
    north.built_in = true;
    lib::Registry west;
    west.name = "West";
    west.built_in = true;
    constexpr std::string_view tail =
        " isn't reviewed by the OA team. OA still checks every package before using it.";
    OA_CHECK_TEXT(
        lib::registry_note({core_prime}),
        std::string("Content from registries other than Core Prime") + std::string(tail)
    );
    OA_CHECK_TEXT(
        lib::registry_note({core_prime, north}),
        std::string("Content from registries other than Core Prime and North") + std::string(tail)
    );
    OA_CHECK_TEXT(
        lib::registry_note({core_prime, north, west}),
        std::string("Content from registries other than Core Prime, North and West") +
            std::string(tail)
    );
    lib::Registry only_added;
    only_added.name = "Example";
    OA_CHECK_TEXT(
        lib::registry_note({only_added}),
        "Content from these registries isn't reviewed by the OA team. OA still checks "
        "every package before using it."
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

/// Tells whether the visible list holds a key.
///
/// @param library the Library
/// @param key the key, as visible_keys writes it
/// @return true when it shows
bool shows_key(const lib::Library& library, std::string_view key) {
    const std::vector<std::string> keys = visible_keys(library);
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

/// The base game entry's name and summary in another language.
///
/// @param english the pattern
/// @return its translation, or the pattern
std::string_view translated_base_game(void*, std::string_view english) {
    if (english == lib::words::no_mod)
        return "Jeu de base";
    if (english == lib::words::active_as_made)
        return "Sans mod";
    return english;
}

void test_base_game() {
    using Keys = std::vector<std::string>;
    using Buttons = std::vector<std::pair<Action, bool>>;

    // No package: it is first, marked PLAYING, and offers no action.
    const lib::Library none = built({});
    OA_CHECK(visible_keys(none) == Keys({"@base-game@"}));
    OA_CHECK(lib::filter_count(none, lib::Filter::all) == 0);
    OA_CHECK(lib::filter_count(none, lib::Filter::installed) == 0);
    OA_CHECK(lib::filter_count(none, lib::Filter::newest) == 0);
    OA_CHECK(lib::filter_count(none, lib::Filter::updates) == 0);
    OA_CHECK(lib::update_count(none) == 0);
    const lib::Entry* entry = lib::find_entry(none, lib::base_game_id());
    OA_CHECK(entry != nullptr && entry->state == State::playing);
    OA_CHECK(entry != nullptr && entry->listing == nullptr && entry->installed == nullptr);
    OA_CHECK(entry != nullptr && entry->update == nullptr && entry->queued == nullptr);
    OA_CHECK(entry != nullptr && !entry->built_in && !entry->not_reviewed);
    OA_CHECK(entry != nullptr && entry->registry_name.empty() && entry->badge == nullptr);
    if (entry != nullptr) {
        OA_CHECK(actions_of(none, *entry).empty());
        OA_CHECK(lib::facts(none, *entry).empty());
        OA_CHECK_TEXT(lib::entry_name(*entry), "No Mod");
        OA_CHECK_TEXT(lib::entry_summary(*entry), "Total Annihilation as it was made");
        OA_CHECK_TEXT(lib::status_line(none, *entry), "Playing");
    }
    OA_CHECK_TEXT(lib::playing_note(none), "");

    // The badge the app supplies is the one the row shows.
    lib::Inputs with_badge;
    with_badge.base_game_badge = lib::Badge{{1, 2, 3, 4}, 1, 1};
    lib::Library badged;
    lib::refresh(badged, std::move(with_badge));
    const lib::Entry* marked = lib::find_entry(badged, lib::base_game_id());
    OA_CHECK(marked != nullptr && marked->badge != nullptr && marked->badge->width == 1);
    OA_CHECK(marked != nullptr && marked->badge != nullptr && marked->badge->height == 1);
    if (marked != nullptr && marked->badge != nullptr && marked->badge->rgba.size() == 4)
        OA_CHECK(marked->badge->rgba[0] == 1 && marked->badge->rgba[3] == 4);

    // A mod played: INSTALLED, and PLAY NOW alone, on the same rule as SWITCH OFF.
    lib::Inputs inputs = base_inputs();
    inputs.installed.push_back(installed(core, Kind::mod, "ridge", "Ridge"));
    inputs.installed.back().playing = true;
    lib::Library library = built(inputs);
    entry = lib::find_entry(library, lib::base_game_id());
    OA_CHECK(entry != nullptr && entry->state == State::installed);
    if (entry != nullptr) {
        OA_CHECK(actions_of(library, *entry) == Buttons({{Action::play_now, true}}));
        OA_CHECK_TEXT(lib::entry_actions(library, *entry).front().text, "PLAY NOW");
        OA_CHECK(lib::facts(library, *entry).empty());
    }
    lib::select(library, lib::base_game_id());
    OA_CHECK_TEXT(lib::playing_note(library), "");
    inputs.in_match = true;
    lib::refresh(library, inputs);
    entry = lib::find_entry(library, lib::base_game_id());
    if (entry != nullptr)
        OA_CHECK(actions_of(library, *entry) == Buttons({{Action::play_now, false}}));
    inputs.in_match = false;
    inputs.mod_from_command_line = true;
    lib::refresh(library, inputs);
    lib::select(library, lib::base_game_id());
    entry = lib::find_entry(library, lib::base_game_id());
    if (entry != nullptr)
        OA_CHECK(actions_of(library, *entry) == Buttons({{Action::play_now, false}}));
    OA_CHECK_TEXT(lib::playing_note(library), "The command line chose this run's mod.");

    // A mod the inputs name, with no installed copy marked playing, is still one played.
    inputs.mod_from_command_line = false;
    inputs.installed.clear();
    inputs.playing_mod = lib::PlayingMod{"Ridge", "1", std::nullopt};
    lib::refresh(library, inputs);
    entry = lib::find_entry(library, lib::base_game_id());
    OA_CHECK(entry != nullptr && entry->state == State::installed);
    if (entry != nullptr)
        OA_CHECK(actions_of(library, *entry) == Buttons({{Action::play_now, true}}));

    // A folder keyed @base-game cannot take the entry: it stays the base game.
    inputs.playing_mod.reset();
    inputs.installed.push_back(installed("", Kind::mod, "@base-game", "Clash"));
    lib::refresh(library, inputs);
    entry = lib::find_entry(library, lib::base_game_id());
    OA_CHECK(entry != nullptr && entry->installed == nullptr && entry->state == State::playing);
    if (entry != nullptr)
        OA_CHECK(actions_of(library, *entry).empty());
    std::size_t clashes = 0;
    for (const lib::Entry& each : library.entries)
        if (lib::is_base_game(each.id))
            ++clashes;
    OA_CHECK(clashes == 1);

    // First under All and Installed. Absent from New, Updates, Maps, Active and Registries.
    // Never counted. A search of its name keeps it first, ahead of a name that also matches.
    inputs = base_inputs();
    lib::Listing dated = listing(core, Kind::mod, "dated", "Dated Mod");
    dated.published = "2026-10-01";
    inputs.listings.push_back(dated);
    lib::Listing kit = listing(core, Kind::mod, "game-kit", "Game Kit");
    kit.tags = {"units"};
    inputs.listings.push_back(kit);
    inputs.listings.push_back(listing(example, Kind::map_pack, "pack", "Pack"));
    inputs.installed.push_back(installed(core, Kind::mod, "dated", "Dated Mod"));
    inputs.updates.push_back(update(core, Kind::mod, "dated"));
    library = built(std::move(inputs));
    OA_CHECK(visible_keys(library).front() == "@base-game@");
    OA_CHECK(lib::filter_count(library, lib::Filter::all) == 2);
    OA_CHECK(lib::filter_count(library, lib::Filter::installed) == 1);
    OA_CHECK(lib::filter_count(library, lib::Filter::newest) == 1);
    OA_CHECK(lib::filter_count(library, lib::Filter::updates) == 1);
    OA_CHECK(lib::update_count(library) == 1);
    lib::set_filter(library, lib::Filter::installed);
    OA_CHECK(visible_keys(library).front() == "@base-game@");
    OA_CHECK(shows_key(library, "dated@fixture-core"));
    lib::set_filter(library, lib::Filter::newest);
    OA_CHECK(visible_keys(library) == Keys({"dated@fixture-core"}));
    lib::set_filter(library, lib::Filter::updates);
    OA_CHECK(visible_keys(library) == Keys({"dated@fixture-core"}));
    lib::set_filter(library, lib::Filter::all);
    lib::set_tag(library, "units");
    OA_CHECK(!shows_key(library, "@base-game@"));
    lib::set_tag(library, "");
    for (const std::string_view word : {"no", "mod", "annihilation", "made"}) {
        lib::set_query(library, word);
        const std::vector<std::string> found = visible_keys(library);
        OA_CHECK(!found.empty() && found.front() == "@base-game@");
    }
    lib::set_query(library, "game");
    OA_CHECK(shows_key(library, "game-kit@fixture-core"));
    lib::set_query(library, "zzzz");
    OA_CHECK(library.visible.empty());
    lib::set_query(library, "");
    lib::set_tab(library, lib::Tab::maps);
    OA_CHECK(visible_keys(library) == Keys({"pack@example-maps"}));
    lib::set_tab(library, lib::Tab::updates);
    OA_CHECK(!shows_key(library, "@base-game@"));
    lib::set_tab(library, lib::Tab::active);
    OA_CHECK(library.visible.empty());
    lib::set_tab(library, lib::Tab::registries);
    OA_CHECK(!shows_key(library, "@base-game@"));

    // The name and summary are the language's, and a search matches that name.
    lib::TextHooks hooks;
    hooks.shown = translated_base_game;
    lib::Library translated;
    translated.text = hooks;
    lib::refresh(translated, base_inputs());
    lib::set_tab(translated, lib::Tab::mods);
    const lib::Entry* french = lib::find_entry(translated, lib::base_game_id());
    OA_CHECK(french != nullptr);
    if (french != nullptr) {
        OA_CHECK_TEXT(lib::entry_name(*french), "Jeu de base");
        OA_CHECK_TEXT(lib::entry_summary(*french), "Sans mod");
    }
    lib::set_query(translated, "jeu");
    OA_CHECK(visible_keys(translated) == Keys({"@base-game@"}));
    lib::set_query(translated, "disable");
    OA_CHECK(translated.visible.empty());
}

void test_built_in_languages() {
    using Buttons = std::vector<std::pair<Action, bool>>;
    // Settings › Language's built-in order, then the packs. English is in
    // use when the inputs name no language. Counts stay counts of packages.
    const std::vector<std::string> built_in = built_in_language_rows();
    OA_CHECK(
        built_in ==
        std::vector<std::string>(
            {"@language:en@", "@language:de@", "@language:es@", "@language:fr@", "@language:it@"}
        )
    );
    lib::Inputs inputs = base_inputs();
    inputs.listings.push_back(listing(core, Kind::language, "zh-Hans", "简体中文"));
    inputs.listings.push_back(listing(core, Kind::language, "xx", "Fixture Language"));
    lib::Installed hans = installed(core, Kind::language, "zh-Hans", "简体中文");
    inputs.installed.push_back(hans);
    lib::Library library = built(inputs);
    lib::set_tab(library, lib::Tab::languages);

    using Keys = std::vector<std::string>;
    Keys all_rows = built_in;
    all_rows.push_back("zh-Hans@fixture-core");
    all_rows.push_back("xx@fixture-core");
    OA_CHECK(visible_keys(library) == all_rows);
    OA_CHECK(lib::tab_count(library, lib::Tab::languages) == 2);
    OA_CHECK(lib::filter_count(library, lib::Filter::all) == 2);
    OA_CHECK(lib::filter_count(library, lib::Filter::installed) == 1);
    OA_CHECK(lib::filter_count(library, lib::Filter::newest) == 0);
    OA_CHECK(lib::filter_count(library, lib::Filter::updates) == 0);

    const lib::Entry* english = lib::find_entry(library, lib::built_in_language_id("en"));
    const lib::Entry* german = lib::find_entry(library, lib::built_in_language_id("de"));
    OA_CHECK(english != nullptr && german != nullptr);
    if (english != nullptr) {
        OA_CHECK(english->state == State::playing);
        OA_CHECK_TEXT(lib::entry_name(*english), "English");
        OA_CHECK(lib::entry_summary(*english).empty());
        OA_CHECK_TEXT(lib::status_line(library, *english), "Built into the game");
        OA_CHECK(lib::facts(library, *english).empty());
        OA_CHECK(actions_of(library, *english).empty());
    }
    if (german != nullptr) {
        OA_CHECK(german->state == State::installed);
        OA_CHECK_TEXT(lib::entry_name(*german), "Deutsch");
        OA_CHECK_TEXT(lib::entry_summary(*german), "German");
        OA_CHECK_TEXT(lib::status_line(library, *german), "Built into the game");
        OA_CHECK(lib::facts(library, *german).empty());
        OA_CHECK(actions_of(library, *german) == Buttons({{Action::use_now, true}}));
        OA_CHECK_TEXT(lib::entry_actions(library, *german).front().text, "USE NOW");
    }

    // New and Updates do not list them. Mods does not either.
    lib::set_filter(library, lib::Filter::newest);
    OA_CHECK(library.visible.empty());
    lib::set_filter(library, lib::Filter::updates);
    OA_CHECK(library.visible.empty());
    lib::set_filter(library, lib::Filter::installed);
    Keys installed_rows = built_in;
    installed_rows.push_back("zh-Hans@fixture-core");
    OA_CHECK(visible_keys(library) == installed_rows);
    lib::set_tab(library, lib::Tab::updates);
    lib::set_filter(library, lib::Filter::all);
    OA_CHECK(visible_keys(library) == Keys({"zh-Hans@fixture-core", "xx@fixture-core"}));
    lib::set_tab(library, lib::Tab::mods);
    OA_CHECK(visible_keys(library) == Keys({"@base-game@"}));

    // Search finds a built-in language by either name and by its tag.
    lib::set_tab(library, lib::Tab::languages);
    lib::set_filter(library, lib::Filter::all);
    lib::set_query(library, "german");
    OA_CHECK(visible_keys(library) == Keys({"@language:de@"}));
    lib::set_query(library, "deutsch");
    OA_CHECK(visible_keys(library) == Keys({"@language:de@"}));
    lib::set_query(library, "de");
    OA_CHECK(visible_keys(library) == Keys({"@language:de@"}));
    lib::set_query(library, "");

    // Choosing Deutsch marks it in use and offers USE NOW on English.
    inputs.language.tag = "de";
    inputs.language.name = "Deutsch";
    inputs.language.english = "German";
    lib::refresh(library, inputs);
    lib::set_tab(library, lib::Tab::languages);
    english = lib::find_entry(library, lib::built_in_language_id("en"));
    german = lib::find_entry(library, lib::built_in_language_id("de"));
    OA_CHECK(english != nullptr && english->state == State::installed);
    OA_CHECK(german != nullptr && german->state == State::playing);
    if (english != nullptr)
        OA_CHECK(actions_of(library, *english) == Buttons({{Action::use_now, true}}));
    if (german != nullptr)
        OA_CHECK(actions_of(library, *german).empty());
    OA_CHECK(visible_keys(library).front() == "@language:en@");

    // An installed pack offers USE NOW until it is the one in use. One that
    // is only listed keeps GET. After an install, USE NOW is there at once.
    const lib::Entry* pack = at(library, core, Kind::language, "zh-Hans");
    const lib::Entry* listed = at(library, core, Kind::language, "xx");
    OA_CHECK(pack != nullptr && listed != nullptr);
    if (pack != nullptr)
        OA_CHECK(
            actions_of(library, *pack) ==
            Buttons(
                {{Action::use_now, true}, {Action::uninstall, true}, {Action::open_folder, true}}
            )
        );
    if (listed != nullptr)
        OA_CHECK(actions_of(library, *listed) == Buttons({{Action::get, true}}));
    inputs.installed.back().in_use = true;
    inputs.language.tag = "zh-Hans";
    inputs.language.pack = "简体中文";
    inputs.language.name = "简体中文";
    lib::refresh(library, inputs);
    lib::set_tab(library, lib::Tab::languages);
    pack = at(library, core, Kind::language, "zh-Hans");
    german = lib::find_entry(library, lib::built_in_language_id("de"));
    OA_CHECK(pack != nullptr && german != nullptr && german->state == State::installed);
    if (pack != nullptr) {
        OA_CHECK(
            actions_of(library, *pack) ==
            Buttons({{Action::uninstall, true}, {Action::open_folder, true}})
        );
    }
    if (german != nullptr)
        OA_CHECK(actions_of(library, *german) == Buttons({{Action::use_now, true}}));

    lib::Inputs gained = library.inputs;
    gained.installed.push_back(installed(core, Kind::language, "xx", "Fixture Language"));
    lib::refresh(library, std::move(gained));
    lib::set_tab(library, lib::Tab::languages);
    listed = at(library, core, Kind::language, "xx");
    OA_CHECK(listed != nullptr && listed->state == State::installed);
    if (listed != nullptr)
        OA_CHECK(
            actions_of(library, *listed) ==
            Buttons(
                {{Action::use_now, true}, {Action::uninstall, true}, {Action::open_folder, true}}
            )
        );
    OA_CHECK(lib::tab_count(library, lib::Tab::languages) == 2);
    OA_CHECK_TEXT(lib::action_text(Action::use_now), "USE NOW");
}

void test_tab_counts() {
    // A tab's count is its All count, before the search and the tag, and it
    // does not follow the tab that is open. Registries counts its rows.
    lib::Inputs inputs = base_inputs();
    inputs.listings.push_back(listing(core, Kind::mod, "counted-mod", "Counted Mod"));
    inputs.listings.push_back(listing(core, Kind::mod, "counted-other", "Counted Other"));
    inputs.listings.push_back(listing(core, Kind::map_pack, "counted-maps", "Counted Maps"));
    inputs.listings.push_back(listing(core, Kind::language, "counted-lang", "Counted Lang"));
    lib::Library library = built(inputs);
    lib::set_tab(library, lib::Tab::updates);
    lib::set_filter(library, lib::Filter::installed);
    lib::set_tag(library, "missing");
    lib::set_query(library, "nothing-matches");
    OA_CHECK(lib::tab_count(library, lib::Tab::active) == 0);
    OA_CHECK(lib::tab_count(library, lib::Tab::mods) == 2);
    OA_CHECK(lib::tab_count(library, lib::Tab::maps) == 1);
    OA_CHECK(lib::tab_count(library, lib::Tab::languages) == 1);
    OA_CHECK(lib::tab_count(library, lib::Tab::updates) == lib::update_count(library));
    OA_CHECK(lib::tab_count(library, lib::Tab::registries) == 2);
    // Base Game is not a mod: Mods' count leaves it out.
    OA_CHECK(lib::find_entry(library, lib::base_game_id()) != nullptr);
}

void test_tabs_list_what_their_counts_hold() {
    // Mods can read "Mods (n)" and "Installed (n)" while the list says
    // "Nothing to show." That line is drawn only when the visible list is
    // empty, the search is empty and the filter is not Updates. Counts
    // ignore the tag. A tag chosen where the filter offers it, then carried
    // to a tab that does not, used to empty the list. A finished install
    // used to leave Updates counting the note the queue had already reached.
    lib::Inputs inputs = base_inputs();
    lib::Listing alpha = listing(core, Kind::mod, "alpha-mod", "Alpha Mod");
    alpha.tags = {"balance"};
    inputs.listings.push_back(alpha);
    inputs.installed.push_back(installed(core, Kind::mod, "alpha-mod", "Alpha Mod"));
    lib::Listing ridge = listing(core, Kind::mod, "ridge", "Ridge");
    ridge.tags = {"units"};
    inputs.listings.push_back(ridge);
    lib::Listing shore = listing(core, Kind::map_pack, "shore", "Shore");
    shore.tags = {"naval"};
    inputs.listings.push_back(shore);
    inputs.installed.push_back(installed(core, Kind::map_pack, "shore", "Shore"));
    lib::Listing hans = listing(core, Kind::language, "zh-hans", "Simplified Chinese");
    hans.tags = {"translation"};
    hans.release = 6;
    hans.revision = 4;
    inputs.listings.push_back(hans);
    // The copy in the folder is still the previous release. The worker has
    // not rewritten it, and the download of release 6 has finished.
    lib::Installed hans_installed =
        installed(core, Kind::language, "zh-hans", "Simplified Chinese");
    hans_installed.release = 5;
    hans_installed.revision = 3;
    inputs.installed.push_back(hans_installed);
    lib::UpdateNote hans_update = update(core, Kind::language, "zh-hans");
    hans_update.to_release = 6;
    hans_update.to_revision = 4;
    inputs.updates.push_back(hans_update);

    lib::Library library = built(std::move(inputs));
    lib::set_tab(library, lib::Tab::updates);
    OA_CHECK(lib::update_count(library) == 1);
    OA_CHECK(lib::tab_count(library, lib::Tab::updates) == 1);
    lib::set_tag(library, "translation");
    using Keys = std::vector<std::string>;
    OA_CHECK(visible_keys(library) == Keys({"zh-hans@fixture-core"}));

    // The install finishes. The note is still in the inputs. The queue holds
    // the finished item at the catalogue release it installed.
    lib::Inputs finished = library.inputs;
    lib::QueueItem done = queued(core, "zh-hans", "Simplified Chinese", Phase::installed);
    done.kind = Kind::language;
    done.release = 6;
    finished.queue.push_back(std::move(done));
    lib::refresh(library, std::move(finished));
    const lib::Entry* language = at(library, core, Kind::language, "zh-hans");
    OA_CHECK(
        language != nullptr && language->state == State::installed && language->update == nullptr
    );
    OA_CHECK(lib::update_count(library) == 0);
    OA_CHECK(lib::tab_count(library, lib::Tab::updates) == 0);
    OA_CHECK(library.visible.empty());
    OA_CHECK(library.tag == "translation");

    lib::set_tab(library, lib::Tab::mods);
    lib::set_filter(library, lib::Filter::installed);
    OA_CHECK(lib::tab_count(library, lib::Tab::mods) == 2);
    OA_CHECK(lib::filter_count(library, lib::Filter::installed) == 1);
    OA_CHECK(visible_keys(library) == Keys({"@base-game@", "alpha-mod@fixture-core"}));
    OA_CHECK(library.tag == "translation");
    lib::set_filter(library, lib::Filter::all);
    OA_CHECK(lib::filter_count(library, lib::Filter::all) == 2);
    OA_CHECK(
        visible_keys(library) ==
        Keys({"@base-game@", "alpha-mod@fixture-core", "ridge@fixture-core"})
    );

    lib::set_tab(library, lib::Tab::maps);
    OA_CHECK(lib::tab_count(library, lib::Tab::maps) == 1);
    OA_CHECK(lib::filter_count(library, lib::Filter::all) == 1);
    OA_CHECK(visible_keys(library) == Keys({"shore@fixture-core"}));

    // A mod's tag, chosen next, still must not blank Languages or Maps.
    lib::set_tag(library, "balance");
    lib::set_tab(library, lib::Tab::languages);
    OA_CHECK(library.tag == "balance");
    OA_CHECK(lib::tab_count(library, lib::Tab::languages) == 1);
    Keys language_rows = built_in_language_rows();
    language_rows.push_back("zh-hans@fixture-core");
    OA_CHECK(visible_keys(library) == language_rows);
    language = at(library, core, Kind::language, "zh-hans");
    OA_CHECK(language != nullptr && language->state == State::installed);
    lib::set_tab(library, lib::Tab::maps);
    OA_CHECK(visible_keys(library) == Keys({"shore@fixture-core"}));

    // Where the tab offers the tag, it filters again. It was never cleared.
    lib::set_tab(library, lib::Tab::mods);
    lib::set_filter(library, lib::Filter::all);
    OA_CHECK(visible_keys(library) == Keys({"alpha-mod@fixture-core"}));

    // A search that matches nothing still empties the list. That line is
    // "Nothing matches …", which an empty search does not say.
    lib::set_filter(library, lib::Filter::installed);
    lib::set_query(library, "zzz");
    OA_CHECK(library.visible.empty());
    OA_CHECK(lib::filter_count(library, lib::Filter::installed) == 1);
    OA_CHECK(lib::tab_count(library, lib::Tab::mods) == 2);
    lib::set_query(library, "");
    // balance is offered by the installed mod, so the base game stays out.
    OA_CHECK(visible_keys(library) == Keys({"alpha-mod@fixture-core"}));

    // The worker has rewritten the copy and dropped the queue item. The last
    // search's note is still in the inputs, and the copy is already there.
    lib::Inputs caught_up = library.inputs;
    caught_up.queue.clear();
    for (lib::Installed& copy : caught_up.installed) {
        if (copy.key == "zh-hans") {
            copy.release = 6;
            copy.revision = 4;
        }
    }
    lib::refresh(library, std::move(caught_up));
    language = at(library, core, Kind::language, "zh-hans");
    OA_CHECK(
        language != nullptr && language->state == State::installed && language->update == nullptr
    );
    OA_CHECK(lib::update_count(library) == 0);

    // A later release is still an update. Held, and a finished item that
    // names no release, do not hide the note. The finished release does.
    lib::Inputs later = library.inputs;
    later.updates.clear();
    lib::UpdateNote next = update(core, Kind::language, "zh-hans");
    next.to_release = 7;
    later.updates.push_back(next);
    lib::QueueItem held = queued(core, "zh-hans", "Simplified Chinese", Phase::held);
    held.kind = Kind::language;
    held.release = 7;
    later.queue = {held};
    lib::refresh(library, later);
    OA_CHECK(lib::update_count(library) == 1);
    later.queue[0].phase = Phase::installed;
    later.queue[0].release = 0;
    lib::refresh(library, later);
    OA_CHECK(lib::update_count(library) == 1);
    later.queue[0].release = 7;
    lib::refresh(library, std::move(later));
    OA_CHECK(lib::update_count(library) == 0);
}

void test_rolled_back_update_returns() {
    // Install release 1, update to release 2, then roll back to release 1.
    // The finished queue item hides the note only while the installed copy
    // has not been read again. Once a later reading names release 1, Updates
    // counts the note and the row offers UPDATE.
    lib::Inputs inputs = base_inputs();
    lib::Listing ridge = listing(core, Kind::mod, "ridge", "Ridge");
    ridge.release = 2;
    inputs.listings.push_back(ridge);
    lib::Installed copy = installed(core, Kind::mod, "ridge", "Ridge");
    copy.release = 1;
    inputs.installed.push_back(copy);
    lib::UpdateNote note = update(core, Kind::mod, "ridge");
    note.to_release = 2;
    inputs.updates.push_back(note);
    inputs.installed_generation = 1;

    lib::Library library = built(std::move(inputs));
    lib::set_tab(library, lib::Tab::updates);
    const lib::Entry* row = at(library, core, Kind::mod, "ridge");
    OA_CHECK(row != nullptr && row->state == State::update);
    OA_CHECK(lib::update_count(library) == 1);
    OA_CHECK(lib::tab_count(library, lib::Tab::updates) == 1);
    if (row != nullptr) {
        OA_CHECK_TEXT(lib::entry_actions(library, *row).front().text, "UPDATE");
        OA_CHECK(lib::entry_actions(library, *row).front().action == Action::update);
        OA_CHECK(lib::entry_actions(library, *row).front().enabled);
    }

    // The update finishes. The next reading of the installed copy still
    // names release 1, and the finished item is of that same reading.
    lib::Inputs finished = library.inputs;
    lib::QueueItem done = queued(core, "ridge", "Ridge", Phase::installed);
    done.release = 2;
    done.finished_scan = finished.installed_generation;
    finished.queue.push_back(std::move(done));
    lib::refresh(library, std::move(finished));
    row = at(library, core, Kind::mod, "ridge");
    OA_CHECK(row != nullptr && row->state == State::installed && row->update == nullptr);
    OA_CHECK(lib::update_count(library) == 0);
    OA_CHECK(lib::tab_count(library, lib::Tab::updates) == 0);

    // Roll back. A later reading names release 1 again. The finished item
    // is of the earlier reading, so it no longer hides the note.
    lib::Inputs rolled = library.inputs;
    rolled.installed_generation = 2;
    lib::refresh(library, std::move(rolled));
    lib::set_tab(library, lib::Tab::updates);
    row = at(library, core, Kind::mod, "ridge");
    OA_CHECK(row != nullptr && row->state == State::update);
    OA_CHECK(lib::update_count(library) == 1);
    OA_CHECK(lib::tab_count(library, lib::Tab::updates) == 1);
    if (row != nullptr) {
        OA_CHECK_TEXT(lib::entry_actions(library, *row).front().text, "UPDATE");
        OA_CHECK(lib::entry_actions(library, *row).front().enabled);
    }
}

/// Tells whether an id list holds an id.
///
/// @param ids the ids
/// @param id the id
/// @return true when it is among them
bool holds_id(const std::vector<std::string>& ids, std::string_view id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

/// A pill press goes through turn_content_expansion and stays on the draft.
void test_expansion_pills() {
    lib::Inputs inputs;
    inputs.content.base_id = "totala";
    inputs.content.copied = {"campaign", "music"};
    inputs.content.enabled = {"campaign"};
    lib::Library library;
    lib::refresh(library, inputs);

    const lib::ContentPress off = lib::press_expansion(library, "campaign");
    OA_CHECK(off.kind == lib::ContentPressKind::redraw);
    OA_CHECK(library.content.edited);
    OA_CHECK(!holds_id(library.content.expansions, "campaign"));
    OA_CHECK_TEXT(lib::active_content(library).hint, "Campaign turns off when the Library closes.");
    const lib::ContentPress again = lib::press_expansion(library, "campaign");
    OA_CHECK(again.kind == lib::ContentPressKind::redraw);
    OA_CHECK(holds_id(library.content.expansions, "campaign"));
    OA_CHECK(!library.content.edited);

    const lib::ContentPress missing = lib::press_expansion(library, "core-contingency");
    OA_CHECK(missing.kind == lib::ContentPressKind::add);
    OA_CHECK(missing.expansion == "core-contingency");
    OA_CHECK(!holds_id(library.content.expansions, "core-contingency"));

    inputs.content.copied = {"campaign", "core-contingency"};
    inputs.content.enabled = {};
    lib::refresh(library, inputs);
    const lib::ContentPress on = lib::press_expansion(library, "core-contingency");
    OA_CHECK(on.kind == lib::ContentPressKind::redraw);
    OA_CHECK(holds_id(library.content.expansions, "campaign"));
    OA_CHECK(holds_id(library.content.expansions, "core-contingency"));
    OA_CHECK_TEXT(
        lib::active_content(library).hint, "Core Contingency turns on when the Library closes."
    );

    const lib::ContentPress cascade = lib::press_expansion(library, "campaign");
    OA_CHECK(cascade.kind == lib::ContentPressKind::redraw);
    OA_CHECK(!holds_id(library.content.expansions, "campaign"));
    OA_CHECK(!holds_id(library.content.expansions, "core-contingency"));

    inputs.content.copied = {"campaign", "cavedog-units"};
    inputs.content.enabled = {};
    lib::refresh(library, inputs);
    const lib::ContentPress refused = lib::press_expansion(library, "cavedog-units");
    OA_CHECK(refused.kind == lib::ContentPressKind::none);
    OA_CHECK(!library.content.edited);

    inputs.content.copied = {"campaign"};
    inputs.content.enabled = {"campaign"};
    inputs.content.mod_required = {"campaign"};
    lib::Installed ridge;
    ridge.kind = Kind::mod;
    ridge.key = "ridge";
    ridge.folder = "ridge";
    ridge.name = "Ridge";
    ridge.playing = true;
    inputs.installed.push_back(ridge);
    lib::refresh(library, inputs);
    const lib::ContentPress asked = lib::press_expansion(library, "campaign");
    OA_CHECK(asked.kind == lib::ContentPressKind::ask);
    OA_CHECK(asked.expansion == "campaign");
    OA_CHECK(!library.content.edited);
    OA_CHECK(library.content.asking);
    OA_CHECK(holds_id(library.content.seed_enabled, "campaign"));
    const lib::ContentPress during = lib::press_expansion(library, "campaign");
    OA_CHECK(during.kind == lib::ContentPressKind::none);
    const oa::ui::kit::Question question = lib::expansion_off_question(library);
    OA_CHECK(question.title == "Turn off Campaign?");
    OA_CHECK(question.buttons.size() == 2);
    OA_CHECK(question.buttons[0].id == lib::cancel_button_id);
    OA_CHECK(question.buttons[1].id == lib::turn_off_button_id);
    OA_CHECK(question.buttons[1].caption == "TURN OFF");
    OA_CHECK(question.paragraphs.size() == 2);
    if (question.paragraphs.size() == 2) {
        OA_CHECK(
            question.paragraphs[0].text ==
            "Ridge needs Campaign, so it is unloaded and the game plays with No Mod."
        );
        OA_CHECK(question.paragraphs[1].text.find("Nothing is deleted.") != std::string::npos);
    }
    lib::cancel_expansion_off(library);
    OA_CHECK(!library.content.asking && !library.content.edited);
    OA_CHECK(holds_id(library.content.seed_enabled, "campaign"));

    const lib::ContentPress asked_again = lib::press_expansion(library, "campaign");
    OA_CHECK(asked_again.kind == lib::ContentPressKind::ask);
    lib::accept_expansion_off(library);
    OA_CHECK(!library.content.asking && library.content.edited);
    OA_CHECK(!holds_id(library.content.expansions, "campaign"));
    OA_CHECK(library.content.last_id == "campaign" && !library.content.last_on);

    library.inputs.in_match = true;
    const lib::ContentPress blocked = lib::press_expansion(library, "music");
    OA_CHECK(blocked.kind == lib::ContentPressKind::none);
    library.inputs.in_match = false;
    library.inputs.content.locked = true;
    const lib::ContentPress locked = lib::press_expansion(library, "campaign");
    OA_CHECK(locked.kind == lib::ContentPressKind::none);

    lib::Inputs demo;
    demo.content.base_id = "totala-demo";
    lib::Library shown;
    lib::refresh(shown, std::move(demo));
    const lib::ActiveContent face = lib::active_content(shown);
    OA_CHECK_TEXT(face.name, "Total Annihilation");
    OA_CHECK_TEXT(face.version_line, "Demo");
    OA_CHECK_TEXT(face.expansions_heading, "EXPANSIONS");
    OA_CHECK_TEXT(face.hint, "This game has no expansions.");
    OA_CHECK(face.pills.empty());
}

/// The base game chooser marks a game and keeps the choice on the draft.
void test_base_chooser() {
    lib::Inputs inputs;
    inputs.content.base_id = "totala";
    inputs.content.copied = {"totala", "campaign", "core-contingency", "music"};
    inputs.content.enabled = {"campaign"};
    lib::Installed ridge;
    ridge.kind = Kind::mod;
    ridge.key = "ridge";
    ridge.name = "Ridge";
    ridge.playing = true;
    inputs.installed.push_back(ridge);
    lib::Library library;
    lib::refresh(library, inputs);

    library.inputs.in_match = true;
    OA_CHECK(!lib::open_base_chooser(library).opened);
    library.inputs.in_match = false;
    library.inputs.content.locked = true;
    OA_CHECK(!lib::open_base_chooser(library).opened);
    library.inputs.content.locked = false;
    library.inputs.mod_from_command_line = true;
    OA_CHECK(!lib::open_base_chooser(library).opened);
    library.inputs.mod_from_command_line = false;

    const lib::BaseOpen opened = lib::open_base_chooser(library);
    OA_CHECK(opened.opened && library.chooser.open);
    OA_CHECK(library.chooser.marked == "totala");
    bool saw_demo = false;
    for (const lib::BaseGameRow& row : library.chooser.rows) {
        if (row.id == "totala")
            OA_CHECK(row.state == lib::BaseGameState::in_use && row.marked);
        if (row.id == "totala-demo") {
            saw_demo = true;
            OA_CHECK(row.state == lib::BaseGameState::not_added);
            OA_CHECK(row.name == "Total Annihilation demo");
            OA_CHECK(row.version == "1997");
            OA_CHECK(row.letters == "DEMO");
            OA_CHECK(row.expansions.empty());
        }
    }
    OA_CHECK(saw_demo);

    lib::mark_base_game(library, "totala-demo");
    OA_CHECK(library.chooser.marked == "totala-demo");
    OA_CHECK(library.chooser.note.find("Ridge") != std::string::npos);
    OA_CHECK(library.chooser.note.find("Library closes") != std::string::npos);
    const lib::BaseUse needs = lib::use_marked_base_game(library);
    OA_CHECK(needs.kind == lib::BaseUseKind::needs_source);
    OA_CHECK(needs.id == "totala-demo");
    OA_CHECK(!library.content.edited && library.chooser.open);

    library.inputs.in_match = true;
    const lib::BaseUse blocked = lib::use_marked_base_game(library);
    OA_CHECK(blocked.kind == lib::BaseUseKind::refused);
    library.inputs.in_match = false;

    lib::mark_base_game(library, "totala");
    OA_CHECK(library.chooser.note.empty());
    const lib::BaseUse same = lib::use_marked_base_game(library);
    OA_CHECK(same.kind == lib::BaseUseKind::none);
    OA_CHECK(!library.chooser.open && !library.content.edited);

    inputs.content.copied.push_back("totala-demo");
    lib::refresh(library, inputs);
    OA_CHECK(lib::open_base_chooser(library).opened);
    lib::mark_base_game(library, "totala");
    lib::close_base_chooser(library);
    OA_CHECK(!library.chooser.open && !library.content.pending_base);

    OA_CHECK(lib::open_base_chooser(library).opened);
    lib::mark_base_game(library, "totala-demo");
    const lib::BaseUse applied = lib::use_marked_base_game(library);
    OA_CHECK(applied.kind == lib::BaseUseKind::applied);
    OA_CHECK(!library.chooser.open);
    OA_CHECK(library.content.pending_base);
    OA_CHECK(library.content.base_game == "totala-demo");
    OA_CHECK(library.content.expansions.empty());
    OA_CHECK(library.content.unload_mod);
    OA_CHECK(library.content.clear_reason == lib::PendingClear::base_game_chosen);
    OA_CHECK_TEXT(
        lib::active_content(library).hint, "This base game is used when the Library closes."
    );

    lib::Library asked;
    lib::Inputs pill_inputs;
    pill_inputs.content.base_id = "totala";
    pill_inputs.content.copied = {"campaign"};
    pill_inputs.content.enabled = {"campaign"};
    pill_inputs.content.mod_required = {"campaign"};
    pill_inputs.installed.push_back(ridge);
    lib::refresh(asked, std::move(pill_inputs));
    OA_CHECK(lib::press_expansion(asked, "campaign").kind == lib::ContentPressKind::ask);
    lib::accept_expansion_off(asked);
    OA_CHECK(asked.content.unload_mod);
    OA_CHECK(!asked.content.pending_base);
    OA_CHECK(asked.content.clear_reason == lib::PendingClear::required_expansion_off);
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
    test_opens_on_active();
    test_new_filter();
    test_active_setup();
    test_order_without_search();
    test_selection();
    test_entry_actions();
    test_switch_off();
    test_base_game();
    test_expansion_pills();
    test_base_chooser();
    test_check_now();
    test_built_in_languages();
    test_tab_counts();
    test_tabs_list_what_their_counts_hold();
    test_rolled_back_update_returns();
    test_own_mod_description();
    test_facts();
    test_status_lines_and_bylines();
    test_playing_note();
    test_queue_lines();
    test_change_texts();
    test_texts();
    test_registry_texts();
    test_lookup();
    return oa::test::check_exit_status();
}
