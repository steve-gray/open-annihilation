// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The content service the runtime owns from start to quit.
#include "oa/app/runtime.hpp"

#include "oa/app/content/service.hpp"
#include "oa/app/content/settings.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/platform/preferences.hpp"

#include <SDL3/SDL.h>

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace oa::app {
namespace {

/// Writes one content line to the error stream.
///
/// @param line the line, without a line end
void content_log(void*, std::string_view line) {
    std::cerr << "open-annihilation: content: " << line << '\n';
}

/// The built-in registries folder beside the program, or empty.
///
/// @return the folder; empty when the program's directory is not known
std::filesystem::path builtin_registries_folder() {
    // SDL caches this string for the process. The caller does not free it.
    const char* const base = SDL_GetBasePath();
    if (base == nullptr)
        return {};
    return path_from_utf8(base) / path_from_utf8(content::builtin_registries_folder_name);
}

} // namespace

struct Runtime::ContentState {
    content::Service service;
    bool developer_mode = false;

    /// Makes the service from options already filled in.
    ///
    /// @param options the service's options
    /// @param developer Developer mode at the moment the service is made
    ContentState(content::ServiceOptions options, bool developer)
        : service(std::move(options)), developer_mode(developer) {}
};

void Runtime::destroy_content_state(ContentState* state) noexcept {
    delete state;
}

void Runtime::start_content() {
    if (content_)
        return;
    content::ServiceOptions options;
    if (options_.data_dir) {
        options.data_folder = *options_.data_dir;
    } else {
        try {
            options.data_folder = oa::platform::preferences::data_directory();
        } catch (const std::exception& error) {
            std::cerr << "open-annihilation: content: " << error.what() << '\n';
        }
    }
    options.player_folder = user_folder();
    options.builtin_folder = builtin_registries_folder();
    options.developer_mode = developer_mode();
    options.automatic = !options_.headless_check && !options_.unattended;
    options.check = content::read_check_for_updates(preference_values_);
    options.log = content_log;
    const bool developer = options.developer_mode;
    content_.reset(new ContentState(std::move(options), developer));
    content_->service.start();
}

void Runtime::tick_content() {
    if (!content_)
        return;
    const bool developer = developer_mode();
    if (developer == content_->developer_mode)
        return;
    content_->developer_mode = developer;
    content_->service.set_developer_mode(developer);
}

content::Service& Runtime::content_service() {
    if (!content_)
        start_content();
    return content_->service;
}

std::optional<std::string> Runtime::content_install_id(std::string_view registry) {
    bool changed = false;
    std::optional<std::string> id =
        content::ensure_install_id(preference_values_, registry, changed);
    if (changed) {
        preferences_dirty_ = true;
        flush_preferences();
    }
    return id;
}

} // namespace oa::app
