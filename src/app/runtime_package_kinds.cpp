// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What each kind of package asks of the running game. A kind is named in
// one branch of each function: a mod package (oamod), a map pack (oamap)
// and a language pack (oalang).

#include "map_packs.hpp"
#include "package_paths.hpp"

#include "oa/app/package_install.hpp"
#include "oa/app/package_install/oamap.hpp"
#include "oa/app/runtime.hpp"

#include <string>
#include <tuple>
#include <utility>

namespace oa::app {

struct Runtime::MapPackState {
    MapPacks packs;
    fs::path game_dir{};
    package_install::oamap::FitHooks hooks{};

    /// Remembers the Maps folder and the game the fit check reads.
    ///
    /// @param maps_folder the Maps folder
    /// @param game the base game's folder
    MapPackState(fs::path maps_folder, fs::path game)
        : packs(std::move(maps_folder)), game_dir(std::move(game)) {}
};

namespace {

/// Checks one staged map against the base game.
///
/// @param context the base game's folder
/// @param staged the unpacked folder
/// @param map the map
/// @param id the pack's id
/// @return every rule the map broke
oa::data::map_fit::Fit check_runtime_base_fit(
    void* context,
    const fs::path& staged,
    const oa::data::map_pack::MapEntry& map,
    std::string_view id
) {
    const auto* game_dir = static_cast<const fs::path*>(context);
    if (game_dir == nullptr)
        return {};
    return check_base_game_fit(*game_dir, staged, map, id);
}

} // namespace

void Runtime::destroy_map_pack_state(MapPackState* state) noexcept {
    delete state;
}

MapPacks& Runtime::map_packs() const {
    if (!map_pack_state_) {
        map_pack_state_.reset(new MapPackState(
            user_folder_ / std::string(package_install::oamap::maps_folder_name), options_.game_dir
        ));
    }
    return map_pack_state_->packs;
}

bool Runtime::package_target_in_use(
    const package_install::PackageKind& kind, const fs::path& folder
) const {
    if (kind.name == "oamod")
        return options_.game_folders.size() > 1 &&
               same_folder(kept_path(folder), kept_path(options_.game_folders.front()));
    if (kind.name == "oamap")
        return false;
    // A pack is read whole when packs are read. Nothing holds its folder
    // open, and nothing waits for the run to end.
    if (kind.name == "oalang")
        return false;
    return false;
}

bool Runtime::package_offers_play(const package_install::PackageKind& kind) const {
    if (kind.name == "oamod")
        return options_.mod_dir.empty() && !options_.base_game;
    if (kind.name == "oamap")
        return false;
    if (kind.name == "oalang")
        return false;
    return false;
}

void Runtime::package_changed(const package_install::PackageKind& kind, const fs::path&) {
    if (kind.name == "oamod")
        list_offered_mods();
    if (kind.name == "oamap")
        map_packs().refresh();
    // A pack is read whole. Nothing holds its folder, so the lists, the
    // catalogue and the font faces are replaced in place.
    if (kind.name == "oalang")
        reload_language_packs();
}

package_install::PackageOptions
Runtime::package_options(const package_install::PackageKind& kind) const {
    package_install::PackageOptions options{};
    if (kind.name == "oamod") {
        options.accept_unimplemented_hacks = options_.accept_unimplemented_hacks;
        options.preferences = &preference_values_;
        options.game_folder =
            options_.game_folders.empty() ? options_.game_dir : options_.game_folders.back();
    }
    if (kind.name == "oamap") {
        std::ignore = map_packs();
        map_pack_state_->game_dir = options_.game_dir;
        map_pack_state_->hooks.context = &map_pack_state_->game_dir;
        map_pack_state_->hooks.check_base_fit = &check_runtime_base_fit;
        options.context = &map_pack_state_->hooks;
    }
    // A language pack's hooks read the package themselves and need no context.
    if (kind.name == "oalang")
        return options;
    return options;
}

} // namespace oa::app
