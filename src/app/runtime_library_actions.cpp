// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the Library's entries ask of the game: ROLL BACK, which asks the Mods
// page's Roll Back Mod question over the Library and rolls the folder back as
// the Mods page does; PLAY NOW, which switches the game to the mod; and
// UPDATE and UPDATE ALL, which queue updates through request_update, the one
// place an update reaches the download queue.

#include "library_host.hpp"
#include "mod_install_state.hpp"
#include "oa_layer.hpp"

#include "oa/app/content/downloads.hpp"
#include "oa/app/package_install.hpp"
#include "oa/app/package_install/oamod.hpp"
#include "oa/app/runtime.hpp"
#include "oa/ui/engine_settings/prompt.hpp"
#include "oa/ui/library/library.hpp"
#include "oa/ui/library/text.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace library = oa::ui::library;
namespace install = oa::app::package_install;

namespace {

/// The name the Roll Back question's ROLL BACK has to automation.
constexpr std::string_view kRollBackWord = "roll-back";

/// Writes a line of the Library's to standard error, which the log keeps.
///
/// @param line the line
void library_log(std::string_view line) {
    std::cerr << "open-annihilation: library: " << line << '\n';
}

} // namespace

void Runtime::library_entry_action(library::Action action, const library::EntryId& entry) {
    LibraryHost& host = library_host();
    if (action == library::Action::update) {
        std::ignore = request_update(entry);
        return;
    }
    if (action == library::Action::update_all) {
        // Every UPDATE that can act and is not queued, in the order the list
        // shows them: the Updates tab's, or the model's off it.
        std::vector<library::EntryId> due;
        const auto take_due = [&due](const library::Entry& each) {
            const bool queued = each.queued != nullptr &&
                                each.queued->phase != library::QueueItem::Phase::failed &&
                                each.queued->phase != library::QueueItem::Phase::installed;
            if (each.state == library::State::update && each.can_act && !queued)
                due.push_back(each.id);
        };
        if (host.model.tab == library::Tab::updates)
            for (const std::size_t index : host.model.visible)
                take_due(host.model.entries[index]);
        else
            for (const library::Entry& each : host.model.entries)
                take_due(each);
        for (const library::EntryId& each : due)
            std::ignore = request_update(each);
        return;
    }
    const library::Entry* found = library::find_entry(host.model, entry);
    if (found == nullptr || found->installed == nullptr)
        return;
    const fs::path folder = found->installed->folder;
    if (action == library::Action::play_now) {
        std::string refusal;
        if (!switch_to_mod_folder(folder, refusal)) {
            status_ = refusal;
            library_log("PLAY NOW: " + refusal);
        }
        return;
    }
    if (action != library::Action::roll_back)
        return;
    // A refusal shows in the status line, and the Library's feed looks
    // again: the version put back may have an update.
    const auto roll_back = [this, folder] {
        std::string refusal;
        if (!roll_back_mod_folder(folder, refusal)) {
            status_ = refusal;
            library_log("ROLL BACK: " + refusal);
        }
        if (library_) {
            library_->look_again(*this);
            library_->refreshed = false;
        }
    };
    // With no version kept that can be read there is nothing to ask:
    // roll_back_mod_folder refuses it, changing nothing.
    const install::InstalledPackage installed = install::oamod::read_installed_mod(folder);
    const auto kept = install::oamod::read_backup(folder);
    if (!kept || kept->kind != install::FolderKind::package) {
        roll_back();
        return;
    }
    // The Mods page's Roll Back Mod question, in its words, over the
    // Library: CANCEL closes it, ROLL BACK rolls the folder back.
    const bool same_version = installed.version == kept->version;
    const std::string title =
        installed.name.empty() ? std::string(library::entry_name(*found)) : installed.name;
    settings::Prompt asked = settings::roll_back_prompt(
        title,
        install::oamod::version_label(installed.version, installed.revision, same_version),
        install::oamod::version_label(kept->version, kept->revision, same_version),
        package_target_in_use(install::mod_kind(), folder)
    );
    int32_t roll_back_button = -1;
    for (std::size_t place = 0; place < asked.buttons.size(); ++place)
        if (asked.buttons[place].id == kRollBackWord)
            roll_back_button = static_cast<int32_t>(place);
    QuestionScreen::Host answers;
    answers.answer = [roll_back, roll_back_button](int32_t button) {
        if (button == roll_back_button)
            roll_back();
        return true;
    };
    // It goes with the main menu, as the Library does.
    answers.tick = [this] { return screen_ != Screen::main_menu; };
    OaLayer& layer = oa_layer();
    layer.push(
        std::make_unique<QuestionScreen>(layer, Screen::main_menu, std::move(asked), answers)
    );
}

bool Runtime::request_update(const library::EntryId& entry) {
    LibraryHost& host = library_host();
    const library::Entry* found = library::find_entry(host.model, entry);
    if (found == nullptr || host.feed.enqueue == nullptr || entry.registry.empty())
        return false;
    // What the installer noted of an earlier try is done with.
    if (mod_install_state_)
        mod_install_state_->note_for_library(*this, {entry.registry, entry.key, {}, {}});
    std::string why;
    const std::optional<uint64_t> item = host.feed.enqueue(
        host.feed.context, entry.registry, entry.key, content::DownloadReason::update, &why
    );
    if (!item) {
        if (!why.empty()) {
            status_ = why;
            library_log(why);
        }
        return false;
    }
    // The queue line keeps saying what the update changes once its update
    // note is gone.
    if (found->update != nullptr && found->installed != nullptr) {
        const library::ChangeTexts change =
            library::change_texts(*found->installed, *found->update, host.model.text);
        host.kept_updates.push_back(
            {entry.registry, entry.kind, entry.key, found->update->rules, change.from, change.to}
        );
    }
    return true;
}

} // namespace oa::app
