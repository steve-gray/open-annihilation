// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the Library's entries ask of the game: ROLL BACK, which asks the Roll
// Back Mod question over the Library and rolls the folder back, a refusal
// told in a prompt; PLAY NOW, which switches the game to the mod, a refusal
// told in a note; SWITCH OFF, which asks the Switch Mod question and plays
// the game with no mod; UPDATE and UPDATE ALL, which start updates through
// request_update, the one place an update reaches the download queue; and
// the footer's CHECK NOW, which asks the registries for their lists again.
// An update that may stop saved games loading, of a mod from a registry the
// player added, asks the update question first
// (oa::ui::library::asks_before_update); every other starts at once.

#include "library_host.hpp"
#include "mod_install_state.hpp"
#include "oa_layer.hpp"

#include "oa/app/content/downloads.hpp"
#include "oa/app/content/service.hpp"
#include "oa/app/package_install.hpp"
#include "oa/app/package_install/oamod.hpp"
#include "oa/app/runtime.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/ui/kit/components_more.hpp"
#include "oa/ui/library/library.hpp"
#include "oa/ui/library/mod_questions.hpp"
#include "oa/ui/library/text.hpp"
#include "oa/ui/library/update_question.hpp"

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

namespace library = oa::ui::library;
namespace install = oa::app::package_install;

namespace {

/// Writes a line of the Library's to standard error, which the log keeps.
///
/// @param line the line
void library_log(std::string_view line) {
    std::cerr << "open-annihilation: library: " << line << '\n';
}

/// Returns the place of a question's button with an id.
///
/// @param question the question
/// @param id the button's name to automation
/// @return its place, from 0; -1 when the question has none such
int32_t button_named(const oa::ui::kit::Question& question, std::string_view id) {
    for (std::size_t place = 0; place < question.buttons.size(); ++place)
        if (question.buttons[place].id == id)
            return static_cast<int32_t>(place);
    return -1;
}

} // namespace

void Runtime::library_entry_action(library::Action action, const library::EntryId& entry) {
    LibraryHost& host = library_host();
    if (action == library::Action::check_now) {
        // CHECK NOW: the registries are asked for their lists again; the
        // header's age and CHECK NOW itself follow the check.
        content_service().refresh(content::RefreshReason::check_now);
        return;
    }
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
        // The updates that ask first are asked about once, together; every
        // other starts at once, in list order, every built-in registry's
        // among them, whatever the question's answer.
        std::vector<library::Entry> asking;
        std::vector<library::EntryId> asked_for;
        std::vector<library::EntryId> at_once;
        for (const library::EntryId& each : due) {
            const library::Entry* found = library::find_entry(host.model, each);
            if (found != nullptr && found->update != nullptr &&
                library::asks_before_update(*found, *found->update)) {
                asking.push_back(*found);
                asked_for.push_back(each);
            } else {
                at_once.push_back(each);
            }
        }
        const std::optional<library::UpdateQuestion> question =
            library::update_all_question(asking, host.model.text);
        for (const library::EntryId& each : at_once)
            std::ignore = request_update(each);
        if (question)
            ask_before_updates(library::update_prompt(*question, host.model.text), asked_for);
        return;
    }
    const library::Entry* found = library::find_entry(host.model, entry);
    if (found == nullptr || found->installed == nullptr)
        return;
    const fs::path folder = found->installed->folder;
    OaLayer& layer = oa_layer();
    // The mod played changes only from the main menu, and never while the
    // command line chose it: PLAY NOW and SWITCH OFF are off then.
    const bool switching =
        action == library::Action::play_now || action == library::Action::switch_off;
    if (switching && (screen_ == Screen::match || match_ != nullptr || !options_.mod_dir.empty() ||
                      options_.base_game))
        return;
    if (action == library::Action::play_now) {
        std::string refusal;
        if (!switch_to_mod_folder(folder, refusal)) {
            status_ = refusal;
            library_log("PLAY NOW: " + refusal);
            // The refusal is told over the Library, under the Switch Mod
            // question's title, until OK.
            QuestionScreen::Host told;
            told.tick = [this] { return screen_ != Screen::main_menu; };
            layer.push(
                std::make_unique<QuestionScreen>(
                    layer,
                    Screen::main_menu,
                    library::switch_refused_note(
                        oa::data::languages::interface_text(refusal), host.model.text
                    ),
                    told
                )
            );
        }
        return;
    }
    if (action == library::Action::switch_off) {
        // The Switch Mod question of no mod: CANCEL leaves the mod played,
        // SWITCH plays none.
        oa::ui::kit::Question asked = library::switch_off_question(host.model.text);
        const int32_t switch_button = button_named(asked, library::switch_button_id);
        QuestionScreen::Host answers;
        answers.answer = [this, switch_button](int32_t button) {
            if (button != switch_button)
                return true;
            std::string refusal;
            if (!switch_to_mod_folder({}, refusal)) {
                status_ = refusal;
                library_log("SWITCH OFF: " + refusal);
            }
            return true;
        };
        answers.tick = [this] { return screen_ != Screen::main_menu; };
        layer.push(
            std::make_unique<QuestionScreen>(layer, Screen::main_menu, std::move(asked), answers)
        );
        return;
    }
    if (action != library::Action::roll_back)
        return;
    // A refusal is told in a prompt over the Library (MOD NOT ROLLED BACK)
    // and in the status line, and the Library's feed looks again: the
    // version put back may have an update.
    const auto roll_back = [this, folder] {
        std::string refusal;
        if (!roll_back_mod_folder(folder, refusal)) {
            status_ = refusal;
            library_log("ROLL BACK: " + refusal);
            mod_install_state().tell_roll_back_refusal(*this);
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
    // The Roll Back Mod question over the Library: CANCEL closes it, ROLL
    // BACK rolls the folder back.
    const bool same_version = installed.version == kept->version;
    const std::string title =
        installed.name.empty() ? std::string(library::entry_name(*found)) : installed.name;
    oa::ui::kit::Question asked = library::roll_back_question(
        title,
        install::oamod::version_label(installed.version, installed.revision, same_version),
        install::oamod::version_label(kept->version, kept->revision, same_version),
        package_target_in_use(install::mod_kind(), folder),
        host.model.text
    );
    const int32_t roll_back_button = button_named(asked, library::roll_back_button_id);
    QuestionScreen::Host answers;
    answers.answer = [roll_back, roll_back_button](int32_t button) {
        if (button == roll_back_button)
            roll_back();
        return true;
    };
    // It goes with the main menu, as the Library does.
    answers.tick = [this] { return screen_ != Screen::main_menu; };
    layer.push(
        std::make_unique<QuestionScreen>(layer, Screen::main_menu, std::move(asked), answers)
    );
}

void Runtime::ask_before_updates(
    const oa::ui::kit::Question& question, const std::vector<library::EntryId>& entries
) {
    int32_t update_button = -1;
    for (std::size_t place = 0; place < question.buttons.size(); ++place)
        if (question.buttons[place].id == library::update_button_id)
            update_button = static_cast<int32_t>(place);
    QuestionScreen::Host answers;
    answers.answer = [this, entries, update_button](int32_t button) {
        if (button == update_button)
            for (const library::EntryId& each : entries)
                std::ignore = request_update(each, true);
        return true;
    };
    // It goes with the main menu, as the Library and the settings it asks
    // over do.
    answers.tick = [this] { return screen_ != Screen::main_menu; };
    OaLayer& layer = oa_layer();
    layer.push(std::make_unique<QuestionScreen>(layer, Screen::main_menu, question, answers));
}

bool Runtime::request_update(const library::EntryId& entry, bool answered) {
    LibraryHost& host = library_host();
    const library::Entry* found = library::find_entry(host.model, entry);
    if (found == nullptr || host.feed.enqueue == nullptr || entry.registry.empty())
        return false;
    // The update question, where asks_before_update says the update asks:
    // pushed at once over the screen that asked. Its UPDATE comes back here
    // answered; NOT NOW leaves the update.
    if (!answered && found->update != nullptr) {
        const bool playing = found->installed != nullptr && found->installed->playing;
        if (const std::optional<library::UpdateQuestion> asked =
                library::update_question(*found, *found->update, playing, host.model.text)) {
            ask_before_updates(library::update_prompt(*asked, host.model.text), {entry});
            return true;
        }
    }
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
