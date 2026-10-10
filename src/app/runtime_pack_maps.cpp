// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Pack maps in the skirmish picker and the match: one map's files mounted at
// a time, checked against the game and the mod played when they are mounted,
// and unmounted when another map is chosen, when the match ends and whenever
// the main menu shows.

#include "map_packs.hpp"

#include "oa/app/runtime.hpp"
#include "oa/data/defs/layout.hpp"
#include "oa/data/map_fit/map_fit.hpp"
#include "oa/data/map_pack/manifest.hpp"
#include "oa/data/map_pack/map_name.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/ui/frontend/savegame_dialogs.hpp"

#include <array>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace oa::app {

namespace fit = oa::data::map_fit;
namespace pack = oa::data::map_pack;

// A saved game keeps the name of the map it was played on in its summary,
// with the terminating NUL. A pack map's name always fits there whole, so a
// saved game on a pack map loads that map by its name.
// clang-format off
static_assert(oa::data::map_pack::most_map_name_bytes < std::tuple_size_v<decltype(oa::ui::frontend::LoadSummary::mission)>, "a pack map's name cut short in a saved game names no map");
// clang-format on

struct Runtime::PackMapState {
    /// The game's and the mod's feature, weapon and unit names, collected at
    /// the first check: the run's archives never change.
    std::optional<fit::GameNames> game_names{};
    /// Each pack map's fit, by the pack's SHA-256 and the map's stem.
    std::map<std::pair<std::string, std::string>, fit::Fit> fits{};
    /// Why each refused map was refused: the first failure's description.
    std::map<std::string, std::string, std::less<>> refusals{};
    /// The name of the map whose files are mounted; empty when none is.
    std::string mounted_name{};
    /// The pack id of that map.
    std::string mounted_id{};
};

namespace {

/// The number of hex digits of a pack's SHA-256 in its layer's label.
constexpr std::size_t kLabelDigestDigits = 8;

/// Returns the manifest entry of an installed pack map.
///
/// @param map the map, as the installed packs list it
/// @return the entry its fit check and its layer read
pack::MapEntry entry_of(const PackMap& map) {
    pack::MapEntry entry{};
    entry.stem = map.stem;
    entry.title = map.title;
    entry.description = map.description;
    entry.size = map.size;
    entry.players = map.players;
    entry.files = map.files;
    return entry;
}

/// A fit with one failure: the map could not be checked or mounted.
///
/// @param subject the map's name
/// @param detail what went wrong
/// @return the fit
fit::Fit unreadable_fit(const std::string& subject, std::string detail) {
    fit::Fit unfit;
    unfit.failures.push_back({fit::Rule::complete, subject, std::move(detail)});
    return unfit;
}

} // namespace

void Runtime::destroy_pack_map_state(PackMapState* state) noexcept {
    delete state;
}

Runtime::PackMapState& Runtime::pack_map_state() {
    if (!pack_map_state_)
        pack_map_state_.reset(new PackMapState());
    return *pack_map_state_;
}

const PackMap* Runtime::pack_map(std::string_view name) {
    if (!pack::split_pack_map_name(name))
        return nullptr;
    return map_packs().find(name);
}

const fit::Fit& Runtime::pack_map_fit(const PackMap& map) {
    auto& state = pack_map_state();
    auto key = std::make_pair(map.sha256, map.stem);
    if (const auto found = state.fits.find(key); found != state.fits.end())
        return found->second;
    fit::Fit checked;
    try {
        const oa::data::defs::DataLayout& layout = oa::data::defs::data_layout();
        if (!state.game_names)
            state.game_names = fit::collect_game_names(assets_, layout);
        const pack::MapEntry entry = entry_of(map);
        const std::unique_ptr<fit::MapFiles> files =
            fit::folder_map_files(map.folder, entry, map.id);
        checked = files ? fit::check_map(assets_, *state.game_names, *files, entry, map.id, layout)
                        : unreadable_fit(map.name, "could not be read from its pack's folder");
    } catch (const std::exception& error) {
        checked = unreadable_fit(map.name, std::string("could not be checked: ") + error.what());
    }
    return state.fits.emplace(std::move(key), std::move(checked)).first->second;
}

bool Runtime::prepare_pack_map(std::string_view name, std::string* reason) {
    auto& state = pack_map_state();
    // A map already mounted stays as it is: a joiner asks every frame.
    if (!state.mounted_name.empty() && state.mounted_name == name && assets_.pack_layer_mounted())
        return true;
    release_pack_map();
    const PackMap* map = pack_map(name);
    if (map == nullptr) {
        if (reason != nullptr)
            *reason = "the map is not installed";
        return false;
    }
    const auto refuse = [&](const fit::Fit& unfit) {
        for (const fit::Failure& failure : unfit.failures)
            std::cerr << "open-annihilation: map " << name
                      << " does not fit: " << fit::describe(failure) << '\n';
        const std::string why = unfit.failures.empty() ? std::string("it does not fit")
                                                       : fit::describe(unfit.failures.front());
        if (const auto kept = state.refusals.find(name); kept != state.refusals.end())
            kept->second = why;
        else
            state.refusals.emplace(std::string(name), why);
        if (reason != nullptr)
            *reason = why;
        return false;
    };
    const fit::Fit& kept = pack_map_fit(*map);
    if (!kept.fits())
        return refuse(kept);

    const pack::MapEntry entry = entry_of(*map);
    oa::PackLayerSpec spec{};
    spec.kind = oa::PackLayerKind::folder;
    spec.location = map->folder;
    spec.files = fit::layer_files(entry, map->id);
    spec.label = map->id + " " + map->sha256.substr(0, kLabelDigestDigits);
    const std::size_t file_count = spec.files.size();
    std::string error;
    if (!assets_.mount_pack_layer(std::move(spec), &error))
        return refuse(unreadable_fit(
            map->name, "could not be mounted: " + (error.empty() ? std::string("no reason") : error)
        ));
    // The pack's folder may have changed since the kept check, so the files
    // as the store now shows them are checked whatever that check found.
    fit::Fit mounted;
    try {
        if (!state.game_names)
            state.game_names = fit::collect_game_names(assets_, oa::data::defs::data_layout());
        const std::unique_ptr<fit::MapFiles> files = fit::mounted_map_files(assets_);
        mounted = files ? fit::check_map(
                              assets_,
                              *state.game_names,
                              *files,
                              entry,
                              map->id,
                              oa::data::defs::data_layout()
                          )
                        : unreadable_fit(map->name, "could not be read as mounted");
    } catch (const std::exception& caught) {
        mounted = unreadable_fit(map->name, std::string("could not be checked: ") + caught.what());
    }
    if (!mounted.fits()) {
        std::ignore = assets_.unmount_pack_layer();
        return refuse(mounted);
    }
    state.mounted_name = map->name;
    state.mounted_id = map->id;
    if (const auto refused = state.refusals.find(name); refused != state.refusals.end())
        state.refusals.erase(refused);
    std::cerr << "open-annihilation: map pack " << map->id << ": mounted " << map->name << " ("
              << file_count << " files)\n";
    return true;
}

void Runtime::release_pack_map() {
    if (!assets_.pack_layer_mounted()) {
        if (pack_map_state_)
            pack_map_state_->mounted_name.clear();
        return;
    }
    const std::optional<std::string> label = assets_.pack_layer_label();
    if (!assets_.unmount_pack_layer())
        return;
    if (pack_map_state_ && !pack_map_state_->mounted_name.empty()) {
        std::cerr << "open-annihilation: map pack " << pack_map_state_->mounted_id << ": unmounted "
                  << pack_map_state_->mounted_name << '\n';
        pack_map_state_->mounted_name.clear();
        pack_map_state_->mounted_id.clear();
    } else {
        std::cerr << "open-annihilation: map pack layer " << label.value_or(std::string())
                  << ": unmounted\n";
    }
}

std::optional<std::string> Runtime::pack_map_refusal(std::string_view name) const {
    if (!pack_map_state_)
        return std::nullopt;
    const auto found = pack_map_state_->refusals.find(name);
    if (found == pack_map_state_->refusals.end())
        return std::nullopt;
    return found->second;
}

} // namespace oa::app
