// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What each kind of package asks of the running game. A kind is named in
// one branch of each function; today only a mod package (oamod) has one.

#include "package_paths.hpp"

#include "oa/app/package_install.hpp"
#include "oa/app/runtime.hpp"

namespace oa::app {

bool Runtime::package_target_in_use(
    const package_install::PackageKind& kind, const fs::path& folder
) const {
    if (kind.name == "oamod")
        return options_.game_folders.size() > 1 &&
               same_folder(kept_path(folder), kept_path(options_.game_folders.front()));
    return false;
}

bool Runtime::package_offers_play(const package_install::PackageKind& kind) const {
    if (kind.name == "oamod")
        return options_.mod_dir.empty() && !options_.base_game;
    return false;
}

void Runtime::package_changed(const package_install::PackageKind& kind, const fs::path&) {
    if (kind.name == "oamod")
        list_offered_mods();
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
    return options;
}

} // namespace oa::app
