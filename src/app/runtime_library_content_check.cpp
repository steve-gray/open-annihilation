// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-library-content: Core Contingency is turned off, the Library
// closes, and its maps leave the skirmish list; the demo is chosen when
// OA_DEMO_INSTALLER names its installer, the Library closes, and the demo
// is mounted. Nothing is mounted again while the Library stays open.

#include "library_host.hpp"
#include "oa_layer.hpp"
#include "oa_layer_check.hpp"

#include "oa/app/automation_host.hpp"
#include "oa/app/content_copy.hpp"
#include "oa/app/demo_installer.hpp"
#include "oa/app/runtime.hpp"
#include "oa/data/content_bundles.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/platform/system.hpp"
#include "oa/ui/display_layout.hpp"
#include "oa/ui/engine_settings.hpp"
#include "oa/ui/library/library.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {

namespace {

namespace fs = std::filesystem;
namespace layout = oa::ui::display_layout;
namespace library = oa::ui::library;
namespace settings = oa::ui::engine_settings;

/// Stops the check.
///
/// @param ok the step held
/// @param what what failed
void require(bool ok, std::string_view what) {
    if (!ok)
        throw std::runtime_error("library content check: " + std::string(what));
}

/// Folds ASCII letters.
///
/// @param text the text
/// @return the same text with A–Z folded
std::string folded(std::string_view text) {
    std::string out(text);
    for (char& letter : out) {
        if (letter >= 'A' && letter <= 'Z')
            letter = static_cast<char>(letter - 'A' + 'a');
    }
    return out;
}

/// Reports whether a path names an OTA map.
///
/// @param path the archive path
/// @return true when it ends in .ota
bool ota_path(std::string_view path) {
    if (path.size() < 4)
        return false;
    return folded(path.substr(path.size() - 4)) == ".ota";
}

/// The file's name without its extension.
///
/// @param path the archive path
/// @return the stem
std::string stem_of(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    std::string name(slash == std::string_view::npos ? path : path.substr(slash + 1));
    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos)
        name.resize(dot);
    return name;
}

/// The OTA stems in Core Contingency's map archive.
///
/// @param user_folder the player's folder
/// @return the stems; empty when the archive is not there
std::vector<std::string> contingency_map_stems(const fs::path& user_folder) {
    std::vector<std::string> stems;
    const oa::data::content_bundles::Bundle* bundle =
        oa::data::content_bundles::find_bundle("core-contingency");
    if (bundle == nullptr)
        return stems;
    // The maps are in one of the expansion's archives (ccmaps.ccx), not
    // necessarily its first.
    for (const oa::data::content_bundles::ContentFile& file : bundle->files) {
        const fs::path archive = bundle_folder(user_folder, *bundle) / std::string(file.copy_name);
        std::error_code error;
        if (!fs::is_regular_file(archive, error))
            continue;
        auto opened = oa::open_hpi_file(archive);
        if (!opened.ok())
            continue;
        const oa::HpiArchive maps = std::move(*opened.value);
        for (const oa::ArchiveEntry& entry : maps.entries())
            if (ota_path(entry.path))
                stems.push_back(folded(stem_of(entry.path)));
    }
    return stems;
}

/// Reports whether a stem is among the skirmish maps.
///
/// @param maps the skirmish list
/// @param stem the folded stem
/// @return true when the list holds it
bool lists_stem(const std::vector<std::string>& maps, std::string_view stem) {
    for (const std::string& name : maps) {
        if (folded(name) == stem)
            return true;
    }
    return false;
}

/// Reports whether the stored expansions of totala name an id.
///
/// @param values the preferences
/// @param id the expansion
/// @return true when the stored list holds it
bool expansions_hold(const oa::platform::preferences::Values& values, std::string_view id) {
    const auto found = values.find(std::string(settings::key::expansions_prefix) + "totala");
    if (found == values.end())
        return false;
    std::string_view list = found->second;
    while (!list.empty()) {
        const auto comma = list.find(',');
        const std::string_view one = list.substr(0, comma);
        if (one == id)
            return true;
        if (comma == std::string_view::npos)
            break;
        list.remove_prefix(comma + 1);
    }
    return false;
}

} // namespace

void Runtime::check_library_content() {
    bool running = true;
    const auto frames = [&](int count) {
        for (int index = 0; index < count; ++index) {
            run_frame(running);
            require(running, "a frame ended the run");
        }
    };
    const auto named = [this](std::string_view name) -> std::optional<AutomationControl> {
        std::vector<AutomationControl> controls;
        oa_layer().automation_controls(controls);
        for (const AutomationControl& control : controls) {
            if (control.name == name && control.visible && control.window_width > 0 &&
                control.window_height > 0)
                return control;
        }
        return std::nullopt;
    };
    const auto click_named = [&](std::string_view name) {
        const std::optional<AutomationControl> control = named(name);
        require(control.has_value(), std::string(name) + " is not shown");
        const layout::Point pixel{
            control->window_x + control->window_width / 2,
            control->window_y + control->window_height / 2
        };
        for (const SDL_EventType type :
             {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
            SDL_Event event = window_pointer_event(
                sdl_.window,
                sdl_.renderer,
                type,
                pixel,
                type == SDL_EVENT_MOUSE_MOTION ? 0 : SDL_BUTTON_LEFT
            );
            bool going = true;
            dispatch_event(event, going);
            require(going, "a click ended the run");
        }
    };

    require(
        SDL_SetWindowSize(sdl_.window, 1920, 1080) && SDL_SyncWindow(sdl_.window),
        "the window did not take 1920x1080"
    );
    apply_output_mode();
    frames(4);

    if (options_.restarts == 0) {
        const std::vector<std::string> before = eligible_map_names_;
        const std::vector<std::string> stems = contingency_map_stems(user_folder_);
        require(!stems.empty(), "Core Contingency's maps were not copied");
        bool listed = false;
        for (const std::string& stem : stems) {
            if (lists_stem(before, stem))
                listed = true;
        }
        require(listed, "Core Contingency's maps are not in the skirmish list");
        open_library(library::Tab::active);
        frames(3);
        require(oa_layer().find(library_screen_name) != nullptr, "the Library did not open");
        click_named("oa.library.active.expansion.core-contingency");
        frames(2);
        require(
            !soft_restart_requested(),
            "turning an expansion off remounted while the Library was open"
        );
        require(
            eligible_map_names_ == before, "the skirmish list changed while the Library was open"
        );
        close_library();
        require(
            soft_restart_requested(), "closing the Library did not ask to mount the files again"
        );
        require(
            !expansions_hold(preference_values_, "core-contingency"), "Core Contingency stayed on"
        );
        return;
    }

    if (options_.restarts == 1) {
        const std::vector<std::string> stems = contingency_map_stems(user_folder_);
        require(!stems.empty(), "Core Contingency's maps were not copied");
        for (const std::string& stem : stems)
            require(
                !lists_stem(eligible_map_names_, stem),
                "a Core Contingency map stayed in the skirmish list"
            );
        const fs::path demo_archive =
            demo_bundle_directory(user_folder_) / std::string(demo_1997.archive_name);
        std::error_code error;
        const bool demo_copied = fs::is_regular_file(demo_archive, error);
        const auto installer = oa::platform::environment_value("OA_DEMO_INSTALLER");
        if (!demo_copied && (!installer || installer->empty())) {
            std::cout << "library content check: the demo was not checked\n";
            return;
        }
        const std::vector<std::string> before = eligible_map_names_;
        open_library(library::Tab::active);
        frames(3);
        require(oa_layer().find(library_screen_name) != nullptr, "the Library did not open");
        click_named("oa.library.active.change");
        frames(2);
        require(
            library_ && library_->model.chooser.open, "CHANGE… did not open the base game chooser"
        );
        click_named("oa.library.base.totala-demo");
        frames(1);
        click_named("oa.library.base.use");
        frames(2);
        require(
            !soft_restart_requested(), "choosing a base game remounted while the Library was open"
        );
        require(
            eligible_map_names_ == before, "the skirmish list changed while the Library was open"
        );
        require(
            oa_layer().find(library_screen_name) != nullptr,
            "the Library closed before it was asked to"
        );
        close_library();
        require(soft_restart_requested(), "closing the Library did not mount the demo");
        const auto base = preference_values_.find(std::string(settings::key::base_game));
        require(
            base != preference_values_.end() && base->second == "totala-demo",
            "the demo was not the stored base game"
        );
        return;
    }

    const fs::path demo = demo_bundle_directory(user_folder_);
    require(options_.game_dir == demo, "the demo was not mounted");
    std::error_code present;
    require(
        fs::is_regular_file(demo / std::string(demo_1997.archive_name), present),
        "the demo's archive is not in its folder"
    );
    std::cout << "library content check: passed\n";
}

} // namespace oa::app
