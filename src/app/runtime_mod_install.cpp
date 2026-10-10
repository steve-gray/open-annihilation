// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The packages opened in the game, installed over the main menu: each taken
// from the inbox once the menu has settled, read, its question asked in a
// prompt in the settings dialog's look, a question screen of the OA layer,
// its files unpacked a budget a frame with the progress shown, put in place,
// and what came of it told; a change
// to the mod played waits for the run to end. A catalogue map pack, and a
// catalogue language pack, ask nothing and show nothing, including while
// Settings or a notice show and in a run nobody watches. PLAY NOW switches
// to the mod installed, and the Mods page's ROLL BACK swaps a mod folder
// with the version its .backup keeps.

#include "engine_settings_state.hpp"
#include "mod_install_state.hpp"
#include "oa_layer.hpp"
#include "user_folder_state.hpp"

#include "oa/app/game_directory.hpp"
#include "package_paths.hpp"

#include "oa/app/package_install.hpp"
#include "oa/app/package_install/handoff.hpp"
#include "oa/app/package_install/inbox.hpp"
#include "oa/app/package_install/oamod.hpp"
#include "oa/app/package_install/prompts.hpp"
#include "oa/app/platform_hooks.hpp"
#include "oa/app/runtime.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/base/threads.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/engine_settings/prompt.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/frontend_state/app_modes.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace install = oa::app::package_install;

namespace {

/// Frames in a row the main menu shows before a prompt.
constexpr uint32_t kPromptMenuFrames = 2;
/// The package bytes an unpacking step reads.
constexpr uint64_t kUnpackBudget = uint64_t{256} << 10;
/// The most time a frame spends unpacking.
constexpr std::chrono::milliseconds kUnpackFrameTime{15};
/// How often the hand-off folder is looked in, in milliseconds.
constexpr uint64_t kHandoffPollMs = 1000;
/// The slices a wait between renames pumps events in, in milliseconds.
constexpr uint32_t kWaitSliceMs = 50;
/// The sound a prompt's button plays as it closes the prompt.
constexpr std::string_view kCloseSound = "Options";

/// Writes a line to standard error, which the log keeps.
///
/// @param line the line
void log_line(std::string_view line) {
    std::cerr << "open-annihilation: package install: " << line << '\n';
}

/// Waits between tries of a rename, keeping the window answering: SDL's
/// events are pumped every slice.
///
/// @param milliseconds how long
void wait_pumping(void*, uint32_t milliseconds) {
    while (milliseconds > 0) {
        const uint32_t slice = std::min(milliseconds, kWaitSliceMs);
        SDL_PumpEvents();
        oa::base::threads::sleep_ms(slice);
        milliseconds -= slice;
    }
}

} // namespace

void Runtime::destroy_mod_install_state(ModInstallState* state) noexcept {
    if (state != nullptr && state->unpacking && state->stage == ModInstallState::Stage::unpacking)
        state->unpacking->cancel();
    delete state;
}

Runtime::ModInstallState& Runtime::mod_install_state() {
    if (!mod_install_state_) {
        mod_install_state_.reset(new ModInstallState{});
        // The folders the start's recovery, or a change that waited, left
        // to delete.
        const auto discards = install::take_discards();
        mod_install_state_->discarder.add(discards);
    }
    return *mod_install_state_;
}

bool Runtime::mod_install_prompt_shown() const noexcept {
    return oa_layer_ && mod_install_state_ && mod_install_state_->prompt != nullptr &&
           oa_layer_->holds(mod_install_state_->prompt);
}

void Runtime::ModInstallState::show(Runtime& runtime, install::PackagePrompt made, bool at_once) {
    ++prompts_shown;
    answers = std::move(made.answers);
    if (prompt != nullptr) {
        prompt->question() = std::move(made.prompt);
        prompt->changed();
        return;
    }
    QuestionScreen::Host host;
    // The answer goes to the install, which takes the prompt off itself
    // once it is done with it (hide).
    host.answer = [&runtime](int32_t button) {
        runtime.answer_mod_install_prompt(button);
        return false;
    };
    // A question is set aside, and a telling ends, once another screen
    // replaces the main menu; an unpacking runs on under its prompt, which
    // shows again over the main menu.
    host.tick = [&runtime] {
        return runtime.screen_ != Screen::main_menu &&
               runtime.mod_install_state().stage != Stage::unpacking;
    };
    host.closed = [&runtime](const QuestionScreen& leaving) {
        auto& state = runtime.mod_install_state();
        if (state.prompt != &leaving)
            return;
        state.prompt = nullptr;
        state.answers.clear();
        state.set_aside();
    };
    auto& layer = runtime.oa_layer();
    auto screen = std::make_unique<QuestionScreen>(
        layer, Screen::main_menu, std::move(made.prompt), std::move(host)
    );
    prompt = screen.get();
    if (at_once)
        layer.push(std::move(screen));
    else
        layer.show_when_free(std::move(screen));
}

void Runtime::ModInstallState::update(Runtime& runtime, install::PackagePrompt made) {
    const uint32_t counted = prompts_shown;
    show(runtime, std::move(made));
    prompts_shown = counted;
}

void Runtime::ModInstallState::hide(Runtime& runtime) {
    answers.clear();
    if (prompt == nullptr)
        return;
    // Let go first, so that its leaving sets nothing aside.
    const QuestionScreen* leaving = prompt;
    prompt = nullptr;
    runtime.oa_layer().close(leaving);
}

void Runtime::ModInstallState::set_aside() {
    if (stage == Stage::asking) {
        install::return_package_file({file, origin});
        stage = Stage::idle;
        package.reset();
    } else if (stage == Stage::telling || stage == Stage::idle) {
        stage = Stage::idle;
    }
}

void Runtime::ModInstallState::release_opened_copy() {
    if (opened_copy.empty())
        return;
    const PlatformHooks& hooks = platform_hooks();
    if (hooks.release_opened_file != nullptr)
        hooks.release_opened_file(hooks.context, path_to_utf8(opened_copy).c_str());
    opened_copy.clear();
}

void Runtime::ModInstallState::finish_package() {
    release_opened_copy();
    package.reset();
    kind = nullptr;
    install::finish_package_file();
}

void Runtime::take_handed_mod_files() {
    const auto folder = install::handoff_folder();
    if (!folder)
        return;
    const auto files = install::take_handed_files(*folder);
    for (const auto& file : files) {
        log_line("handed over by another start: " + path_to_utf8(file));
        install::post_opened_file(file);
    }
    if (!files.empty() && sdl_.window != nullptr) {
        if ((SDL_GetWindowFlags(sdl_.window) & SDL_WINDOW_MINIMIZED) != 0)
            std::ignore = SDL_RestoreWindow(sdl_.window);
        std::ignore = SDL_RaiseWindow(sdl_.window);
    }
}

void Runtime::tell_mod_installs() {
    auto& state = mod_install_state();
    // An unpacking runs on under its prompt, a budget a frame, whatever
    // shows. A mod's prompt is modal on the main menu. A catalogue map pack
    // unpacks with no prompt on any screen but a match.
    if (state.stage == ModInstallState::Stage::unpacking && state.unpacking &&
        state.kind != nullptr) {
        const install::PackageKind& kind = *state.kind;
        const fs::path root = user_folder_ / std::string(kind.root_folder);
        const bool watched =
            !UserFolderState::unwatched(options_.unattended) || !state.check_shows_prompts;
        if (state.placing) {
            state.placing = false;
            install::ChangeOptions options{};
            options.expected = state.expected;
            options.hooks.wait = wait_pumping;
            const install::ChangeResult result =
                install::commit_change(kind, root, state.target, state.change, options);
            state.discarder.add(result.discards);
            state.discarder.add(state.unpacking->discards());
            state.unpacking.reset();
            const fs::path folder = root / path_from_utf8(state.target);
            if (!result.changed) {
                install::Problem problem{};
                problem.refusal = result.refusal;
                problem.detail = result.detail;
                log_line(state.package->file_name + ": not put in place: " + result.detail);
                install::report_package_outcome({
                    .file = state.file,
                    .result = install::OutcomeResult::failed,
                    .reason = install::refusal_text(kind, problem),
                });
                if (!state.finish_quietly(*this)) {
                    state.stage = ModInstallState::Stage::telling;
                    state.show(
                        *this,
                        install::refused_prompt(kind, state.package->file_name, problem, true)
                    );
                }
            } else {
                log_line(state.package->file_name + ": installed in " + path_to_utf8(folder));
                install::report_package_outcome({
                    .file = state.file,
                    .result = install::OutcomeResult::installed,
                });
                state.told_folder = folder;
                state.play_folder = folder;
                package_changed(kind, folder);
                if (!state.finish_quietly(*this)) {
                    state.stage = ModInstallState::Stage::telling;
                    const bool play_now = package_offers_play(kind);
                    state.show(
                        *this,
                        state.change == install::Change::install
                            ? install::installed_prompt(kind, state.incoming, folder, play_now)
                            : install::updated_prompt(
                                  kind,
                                  state.change,
                                  state.incoming,
                                  state.expected,
                                  folder,
                                  result,
                                  play_now
                              )
                    );
                }
            }
            state.finish_package();
            return;
        }
        const auto started = std::chrono::steady_clock::now();
        install::Problem problem{};
        auto step = oa::formats::zip::StreamStep::more;
        while (step == oa::formats::zip::StreamStep::more) {
            step = state.unpacking->step(kUnpackBudget, problem);
            if (watched && std::chrono::steady_clock::now() - started >= kUnpackFrameTime)
                break;
        }
        if (step == oa::formats::zip::StreamStep::failed) {
            state.discarder.add(state.unpacking->discards());
            state.unpacking.reset();
            log_line(state.package->file_name + ": " + install::refusal_text(kind, problem));
            install::report_package_outcome({
                .file = state.file,
                .result = install::OutcomeResult::refused,
                .reason = install::refusal_text(kind, problem),
            });
            if (!state.finish_quietly(*this)) {
                state.stage = ModInstallState::Stage::telling;
                state.show(
                    *this, install::refused_prompt(kind, state.package->file_name, problem, false)
                );
            }
            state.finish_package();
            return;
        }
        if (step == oa::formats::zip::StreamStep::more) {
            if (state.quiet) {
                state.hide(*this);
                return;
            }
            const bool checking = state.unpacking->checking();
            auto made = install::installing_prompt(
                kind,
                state.incoming,
                checking ? state.unpacking->checked_bytes() : state.unpacking->done_bytes(),
                checking ? state.unpacking->archive_bytes() : state.unpacking->total_bytes(),
                checking ? install::InstallingPhase::checking : install::InstallingPhase::unpacking,
                state.package->file_name
            );
            made.prompt.hovered =
                state.prompt != nullptr ? state.prompt->question().hovered : settings::no_control;
            made.prompt.pressed =
                state.prompt != nullptr ? state.prompt->question().pressed : settings::no_control;
            state.update(*this, std::move(made));
            return;
        }
        // Unpacked whole. A kind that checks the staged files does so before
        // they are put in place or left for the run's end. The folder the
        // game plays, when the kind checks nothing of its own, is checked as
        // the Mods page checks a folder, then changed once the run ends and
        // its archives are closed. Any other is put in place on the next
        // frame, once the prompt says so.
        if (kind.check_staged != nullptr) {
            install::Problem staged{};
            if (!kind.check_staged(
                    state.unpacking->staging(), *state.package, package_options(kind), staged
                )) {
                for (const auto& line : staged.lines)
                    log_line(state.package->file_name + ": " + line);
                log_line(
                    state.package->file_name + ": " + install::refusal_text(kind, staged) +
                    (staged.detail.empty() ? "" : " (" + staged.detail + ")")
                );
                state.unpacking->cancel();
                state.discarder.add(state.unpacking->discards());
                state.unpacking.reset();
                install::report_package_outcome({
                    .file = state.file,
                    .result = install::OutcomeResult::refused,
                    .reason = install::refusal_text(kind, staged),
                });
                if (!state.finish_quietly(*this)) {
                    state.stage = ModInstallState::Stage::telling;
                    state.show(
                        *this,
                        install::refused_prompt(kind, state.package->file_name, staged, false)
                    );
                }
                state.finish_package();
                return;
            }
        } else if (state.target_played) {
            const auto& game_folder =
                options_.game_folders.empty() ? options_.game_dir : options_.game_folders.back();
            const auto check = check_picked_mod_folder(
                state.unpacking->staging(),
                game_folder,
                ModChoice{{}, {}, options_.accept_unimplemented_hacks, &preference_values_}
            );
            if (!check.refusal.empty()) {
                for (const auto& line : check.errors)
                    log_line(state.package->file_name + ": " + line);
                state.unpacking->cancel();
                state.discarder.add(state.unpacking->discards());
                state.unpacking.reset();
                install::Problem refused{};
                refused.refusal = install::Refusal::manifest_errors;
                refused.lines = check.errors;
                install::report_package_outcome({
                    .file = state.file,
                    .result = install::OutcomeResult::refused,
                    .reason = install::refusal_text(kind, refused),
                });
                if (!state.finish_quietly(*this)) {
                    state.stage = ModInstallState::Stage::telling;
                    state.show(
                        *this,
                        install::refused_prompt(kind, state.package->file_name, refused, false)
                    );
                }
                state.finish_package();
                return;
            }
        }
        if (!state.target_played) {
            state.placing = true;
            if (state.quiet) {
                state.hide(*this);
                return;
            }
            state.update(
                *this,
                install::installing_prompt(
                    kind,
                    state.incoming,
                    state.unpacking->total_bytes(),
                    state.unpacking->total_bytes(),
                    install::InstallingPhase::placing,
                    state.package->file_name
                )
            );
            return;
        }
        install::PendingChange pending{};
        pending.kind = &kind;
        pending.root = root;
        pending.target = state.target;
        pending.change = state.change;
        pending.incoming = state.incoming;
        pending.replaced = state.expected;
        pending.file_name = state.package->file_name;
        pending.origin = state.origin;
        pending.file = state.file;
        pending.hold = state.unpacking->take_hold();
        install::set_pending_change(std::move(pending));
        log_line(state.package->file_name + ": put in place as the run ends");
        state.unpacking.reset();
        state.hide(*this);
        state.stage = ModInstallState::Stage::idle;
        state.finish_package();
        request_soft_restart();
        return;
    }
    namespace frontend_state = oa::ui::frontend_state;
    const bool settled = screen_ == Screen::main_menu && !frame_owned_by_package() &&
                         state_.state == frontend_state::state_id::main_menu &&
                         state_.pending_signal != frontend_state::signal_id::multiplayer &&
                         !saves_notice_shown() && oa::ui::frontend_dialogs::dialog_count() == 0 &&
                         engine_settings_dialog() == nullptr;
    // Opens the package taken and plans it. A quiet install shows nothing:
    // a plan that asks goes back to the inbox, and a refusal is reported.
    const auto begin_package = [&](const install::OpenedPackage& opened_file) {
        state.file = opened_file.file;
        state.origin = opened_file.origin;
        state.told_folder.clear();
        state.play_folder.clear();
        state.kind = install::kind_for_file(state.file);
        log_line("opening " + path_to_utf8(state.file));
        const std::string name = path_to_utf8(state.file.filename());
        if (state.kind == nullptr) {
            log_line(name + ": it is not a kind of package this game installs");
            install::report_package_outcome({
                .file = state.file,
                .result = install::OutcomeResult::refused,
                .reason = "It is not a kind of package this game installs.",
            });
            if (!state.finish_quietly(*this)) {
                state.stage = ModInstallState::Stage::telling;
                state.show(*this, install::unknown_kind_prompt(name));
            }
            install::finish_package_file();
            return;
        }
        // The platform may bring a file the system opened into the game's own
        // storage first. A catalogue package was not opened that way.
        fs::path readable = state.file;
        if (state.origin.kind != install::OriginKind::catalogue) {
            if (const PlatformHooks& hooks = platform_hooks(); hooks.take_opened_file != nullptr) {
                std::string copy;
                std::string why;
                if (!hooks.take_opened_file(
                        hooks.context, path_to_utf8(state.file).c_str(), &copy, &why
                    )) {
                    install::Problem problem{};
                    problem.refusal = install::Refusal::unreadable;
                    problem.detail = why;
                    log_line(path_to_utf8(state.file) + ": " + why);
                    install::report_package_outcome({
                        .file = state.file,
                        .result = install::OutcomeResult::refused,
                        .reason = install::refusal_text(*state.kind, problem),
                    });
                    if (!state.finish_quietly(*this)) {
                        state.stage = ModInstallState::Stage::telling;
                        state.show(
                            *this, install::refused_prompt(*state.kind, name, problem, false)
                        );
                    }
                    state.kind = nullptr;
                    install::finish_package_file();
                    return;
                }
                if (!copy.empty() && copy != path_to_utf8(state.file)) {
                    readable = path_from_utf8(copy);
                    state.opened_copy = readable;
                }
            }
        }
        auto opened = install::open_package(readable, package_options(*state.kind));
        if (!opened.package) {
            log_line(
                name + ": " +
                (opened.problem.refusal == install::Refusal::unknown_kind
                     ? std::string("it is not a kind of package this game installs")
                     : install::refusal_text(*state.kind, opened.problem)) +
                (opened.problem.detail.empty() ? "" : " (" + opened.problem.detail + ")")
            );
            for (const auto& line : opened.problem.lines)
                log_line(name + ": " + line);
            install::report_package_outcome({
                .file = state.file,
                .result = install::OutcomeResult::refused,
                .reason = opened.problem.refusal == install::Refusal::unknown_kind
                              ? std::string("It is not a kind of package this game installs.")
                              : install::refusal_text(*state.kind, opened.problem),
            });
            if (!state.finish_quietly(*this)) {
                state.stage = ModInstallState::Stage::telling;
                state.show(
                    *this,
                    opened.problem.refusal == install::Refusal::unknown_kind
                        ? install::unknown_kind_prompt(name)
                        : install::refused_prompt(*state.kind, name, opened.problem, false)
                );
            }
            state.finish_package();
            return;
        }
        state.package = std::move(opened.package);
        state.kind = state.package->kind;
        // The texts name the file the player opened, not the platform's copy.
        state.package->file_name = name;
        for (const auto& warning : state.package->warnings)
            log_line(state.package->file_name + ": " + warning);
        if (state.package->backup_left_out)
            log_line(state.package->file_name + ": its own .backup folder is left out");
        if (state.package->origin_left_out)
            log_line(state.package->file_name + ": its own .oa-origin.yaml is left out");
        if (state.kind == nullptr || state.kind->plan == nullptr) {
            log_line(name + ": it is not a kind of package this game installs");
            install::report_package_outcome({
                .file = state.file,
                .result = install::OutcomeResult::refused,
                .reason = "It is not a kind of package this game installs.",
            });
            if (!state.finish_quietly(*this)) {
                state.stage = ModInstallState::Stage::telling;
                state.show(*this, install::unknown_kind_prompt(name));
            }
            state.finish_package();
            return;
        }
        const install::PackageKind& kind = *state.kind;
        const fs::path root = user_folder_ / std::string(kind.root_folder);
        state.incoming = state.package->incoming;
        state.incoming.origin = state.origin;
        state.plan = kind.plan(state.incoming, install::folder_hooks(kind, root));
        switch (state.plan.kind) {
        case install::PlanKind::install:
            state.target = state.plan.target;
            state.change = state.plan.reinstalling ? install::Change::reinstall
                           : state.plan.replacing  ? install::Change::replace
                                                   : install::Change::install;
            state.expected = state.plan.reinstalling || state.plan.replacing
                                 ? state.plan.installed
                                 : install::InstalledPackage{};
            state.target_played = false;
            answer_mod_install_prompt(-1);
            return;
        case install::PlanKind::refuse: {
            install::Problem problem{};
            problem.refusal = state.plan.refusal;
            problem.subject = state.incoming.id;
            install::report_package_outcome({
                .file = state.file,
                .result = install::OutcomeResult::refused,
                .reason = install::refusal_text(kind, problem),
            });
            if (!state.finish_quietly(*this)) {
                state.stage = ModInstallState::Stage::telling;
                state.show(
                    *this, install::refused_prompt(kind, state.package->file_name, problem, false)
                );
            }
            state.finish_package();
            return;
        }
        default:
            if (state.quiet) {
                install::return_package_file({state.file, state.origin});
                state.catalogue_question = state.file;
                state.release_opened_copy();
                state.package.reset();
                state.kind = nullptr;
                state.hide(*this);
                state.quiet = false;
                state.stage = ModInstallState::Stage::idle;
                return;
            }
            state.stage = ModInstallState::Stage::asking;
            state.show(
                *this,
                install::question_prompt(
                    kind,
                    state.plan,
                    state.incoming,
                    state.package->file_name,
                    root,
                    !state.plan.target.empty() &&
                        package_target_in_use(kind, root / path_from_utf8(state.plan.target))
                )
            );
            return;
        }
    };
    // A catalogue language pack asks nothing. It is installed with no prompt
    // while Settings or a notice show, and in a run nobody watches. A match
    // still waits. Any other package is left in the inbox.
    const auto take_catalogue_language = [&]() -> bool {
        if (state.stage != ModInstallState::Stage::idle || state.prompt != nullptr)
            return false;
        const auto next = install::next_package_file();
        if (!next)
            return false;
        const install::PackageKind* kind = install::kind_for_file(next->file);
        if (kind == nullptr || kind->name != "oalang" ||
            next->origin.kind != install::OriginKind::catalogue)
            return false;
        const auto opened_file = install::take_package_file();
        if (!opened_file)
            return false;
        state.quiet = true;
        begin_package(*opened_file);
        return true;
    };
    if (!settled) {
        state.settled_frames = 0;
        // Nothing starts while a match loads or runs. A catalogue map pack
        // or language pack otherwise installs here, with no prompt; every
        // other package waits.
        if (match_ != nullptr || screen_ == Screen::loading || screen_ == Screen::match)
            return;
        if (state.stage != ModInstallState::Stage::idle || state.prompt != nullptr)
            return;
        if (take_catalogue_language())
            return;
        if (UserFolderState::unwatched(options_.unattended) && !state.check_shows_prompts)
            return;
        if (!state.catalogue_question.empty())
            return;
        const auto opened_file = install::take_package_file();
        if (!opened_file)
            return;
        const install::PackageKind* kind = install::kind_for_file(opened_file->file);
        const bool catalogue_map = kind != nullptr && kind->name == "oamap" &&
                                   opened_file->origin.kind == install::OriginKind::catalogue;
        if (!catalogue_map) {
            install::return_package_file(*opened_file);
            return;
        }
        state.quiet = true;
        begin_package(*opened_file);
        return;
    }
    if (++state.settled_frames < kPromptMenuFrames || engine_settings_fonts() == nullptr)
        return;
    // A run nobody watches leaves the packages waiting for one someone does,
    // except a catalogue language pack, which asks nothing.
    if (UserFolderState::unwatched(options_.unattended) && !state.check_shows_prompts) {
        std::ignore = take_catalogue_language();
        return;
    }
    if (state.discarder.busy())
        std::ignore = state.discarder.step();
    if (state.stage != ModInstallState::Stage::idle || state.prompt != nullptr)
        return;
    // What a change that waited for the last run did.
    if (auto outcome = install::take_change_outcome()) {
        const install::PackageKind* kind = outcome->change.kind;
        const fs::path folder = outcome->change.root / path_from_utf8(outcome->change.target);
        state.told_folder = folder;
        state.stage = ModInstallState::Stage::telling;
        const std::string_view told =
            outcome->change.file_name.empty() ? outcome->change.target : outcome->change.file_name;
        if (kind == nullptr) {
            log_line(std::string(told) + ": a change waited with no kind");
            state.show(*this, install::unknown_kind_prompt(told));
            return;
        }
        if (!outcome->result.changed) {
            install::Problem problem{};
            problem.refusal = outcome->result.refusal;
            problem.detail = outcome->result.detail;
            state.show(*this, install::refused_prompt(*kind, told, problem, true));
        } else {
            state.show(
                *this,
                install::updated_prompt(
                    *kind,
                    outcome->change.change,
                    outcome->change.incoming,
                    outcome->change.replaced,
                    folder,
                    outcome->result,
                    false
                )
            );
        }
        return;
    }
    const uint64_t now = SDL_GetTicks();
    if (now >= state.next_handoff_ms) {
        state.next_handoff_ms = now + kHandoffPollMs;
        take_handed_mod_files();
    }
    // A catalogue map pack that needed a question waited for this menu.
    state.catalogue_question.clear();
    state.quiet = false;
    const auto opened_file = install::take_package_file();
    if (!opened_file)
        return;
    begin_package(*opened_file);
}

void Runtime::answer_mod_install_prompt(int32_t button) {
    auto& state = mod_install_state();
    install::Answer answer = install::Answer::ok;
    if (button >= 0) {
        if (state.prompt == nullptr || static_cast<std::size_t>(button) >= state.answers.size())
            return;
        answer = state.answers[static_cast<std::size_t>(button)];
    }
    const auto start_unpacking = [&] {
        if (state.kind == nullptr || !state.package)
            return;
        const install::PackageKind& kind = *state.kind;
        const fs::path root = user_folder_ / std::string(kind.root_folder);
        const fs::path folder = root / path_from_utf8(state.target);
        state.target_played = package_target_in_use(kind, folder);
        state.unpacking = std::make_unique<install::Unpacking>();
        state.origin.installed = options_.fixed_clock
                                     ? "2000-01-01"
                                     : install::utc_date_text(std::chrono::system_clock::now());
        install::Problem problem{};
        if (!state.unpacking->start(
                *state.package, root, state.target, state.expected, state.origin, problem
            )) {
            state.discarder.add(state.unpacking->discards());
            state.unpacking.reset();
            log_line(
                state.package->file_name + ": " + install::refusal_text(kind, problem) +
                (problem.detail.empty() ? "" : " (" + problem.detail + ")")
            );
            install::report_package_outcome({
                .file = state.file,
                .result = install::OutcomeResult::refused,
                .reason = install::refusal_text(kind, problem),
            });
            if (!state.finish_quietly(*this)) {
                state.stage = ModInstallState::Stage::telling;
                state.show(
                    *this, install::refused_prompt(kind, state.package->file_name, problem, false)
                );
            }
            state.finish_package();
            return;
        }
        log_line(
            state.package->file_name + ": unpacking into " +
            path_to_utf8(state.unpacking->staging())
        );
        state.stage = ModInstallState::Stage::unpacking;
        state.placing = false;
        if (state.quiet) {
            state.hide(*this);
            return;
        }
        state.show(
            *this,
            install::installing_prompt(
                kind,
                state.incoming,
                0,
                state.unpacking->archive_bytes(),
                install::InstallingPhase::checking,
                state.package->file_name
            )
        );
    };
    switch (state.stage) {
    case ModInstallState::Stage::idle:
        // A plan that asks nothing.
        if (button < 0 && state.package)
            start_unpacking();
        return;
    case ModInstallState::Stage::asking:
        switch (answer) {
        case install::Answer::replace:
            state.target = state.plan.target;
            state.change = install::Change::replace;
            state.expected = state.plan.installed;
            start_unpacking();
            return;
        case install::Answer::reinstall:
            state.target = state.plan.target;
            state.change = install::Change::reinstall;
            state.expected = state.plan.installed;
            start_unpacking();
            return;
        case install::Answer::alongside:
            state.target = state.plan.alongside;
            state.change = install::Change::install;
            state.expected = {};
            start_unpacking();
            return;
        default:
            log_line(state.package ? state.package->file_name + ": cancelled" : "cancelled");
            install::report_package_outcome({
                .file = state.file,
                .result = install::OutcomeResult::set_aside,
            });
            play_ui_sound(kCloseSound, 0);
            state.hide(*this);
            state.stage = ModInstallState::Stage::idle;
            state.finish_package();
            return;
        }
    case ModInstallState::Stage::unpacking:
        // CANCEL stops the unpacking; nothing is changed.
        if (answer != install::Answer::cancel || state.placing || !state.unpacking)
            return;
        state.unpacking->cancel();
        state.discarder.add(state.unpacking->discards());
        state.unpacking.reset();
        log_line(state.package ? state.package->file_name + ": unpacking cancelled" : "cancelled");
        install::report_package_outcome({
            .file = state.file,
            .result = install::OutcomeResult::set_aside,
        });
        play_ui_sound(kCloseSound, 0);
        state.hide(*this);
        state.stage = ModInstallState::Stage::idle;
        state.finish_package();
        return;
    case ModInstallState::Stage::telling:
        switch (answer) {
        case install::Answer::open_folder: {
            const FolderOpening opening = open_player_folder(state.told_folder);
            if (state.prompt != nullptr) {
                state.prompt->question().failure = opening.opened ? std::string() : opening.reason;
                state.prompt->changed();
            }
            return;
        }
        case install::Answer::play_now: {
            std::string refusal;
            if (!switch_to_mod_folder(state.play_folder, refusal)) {
                if (state.prompt != nullptr) {
                    state.prompt->question().failure =
                        std::string(oa::data::languages::interface_text(refusal));
                    state.prompt->changed();
                }
                return;
            }
            state.hide(*this);
            state.stage = ModInstallState::Stage::idle;
            return;
        }
        default:
            play_ui_sound(kCloseSound, 0);
            state.hide(*this);
            state.stage = ModInstallState::Stage::idle;
            return;
        }
    }
}

bool Runtime::switch_to_mod_folder(const fs::path& folder, std::string& refusal) {
    auto& state = engine_settings_state();
    settings::EngineSettings chosen = state.current;
    chosen.mod_folder = kept_path(folder);
    const auto& game_folder =
        options_.game_folders.empty() ? options_.game_dir : options_.game_folders.back();
    auto check = check_picked_mod_folder(
        folder,
        game_folder,
        ModChoice{{}, {}, options_.accept_unimplemented_hacks, &preference_values_}
    );
    if (check.refusal.empty() && check.without_profile)
        if (auto problem =
                side_data_problem_over(folder, game_folder, data_word_for(chosen.language));
            !problem.empty()) {
            check.refusal = "That folder cannot be played; the log says why.";
            check.errors.push_back(std::move(problem));
        }
    for (const auto& line : check.errors)
        std::cerr << "open-annihilation: the mod chosen: " << line << '\n';
    if (!check.refusal.empty()) {
        refusal = check.refusal;
        return false;
    }
    const settings::EngineSettings opened = state.current;
    apply_engine_settings(chosen);
    if (const auto failure = save_engine_settings(opened, chosen, false)) {
        EngineSettingsState::report_failed_save(*this, *failure);
        refusal = *failure;
        return false;
    }
    request_soft_restart();
    return true;
}

bool Runtime::roll_back_mod_folder(settings::Dialog& dialog) {
    auto& state = engine_settings_state();
    const fs::path folder = path_from_utf8(dialog.roll_back_folder);
    const fs::path mods = folder.parent_path();
    const std::string target = path_to_utf8(folder.filename());
    const install::PackageKind& kind = install::mod_kind();
    const install::InstalledPackage installed = install::oamod::read_installed_mod(folder);
    const auto backup = install::oamod::read_backup(folder);
    const std::string title = installed.name.empty() ? target : installed.name;
    // A refusal is told in a prompt over the dialog, whose text wraps, and
    // the dialog stays open under it.
    const auto refuse = [&](install::PackagePrompt made, std::string_view why) {
        log_line(path_to_utf8(folder) + ": not rolled back: " + std::string(why));
        auto& install_state = mod_install_state();
        if (install_state.prompt == nullptr &&
            install_state.stage == ModInstallState::Stage::idle) {
            install_state.told_folder.clear();
            install_state.play_folder.clear();
            install_state.stage = ModInstallState::Stage::telling;
            install_state.show(*this, std::move(made), true);
        }
        std::ignore = settings::set_folder_notice(dialog, {});
        return false;
    };
    if (!backup || backup->kind != install::FolderKind::package)
        return refuse(
            install::oamod::roll_back_failed_prompt(title), "it keeps no version that can be read"
        );
    // The kept version must still be playable: an engine since may refuse it.
    const auto& game_folder =
        options_.game_folders.empty() ? options_.game_dir : options_.game_folders.back();
    const fs::path kept = entry_without_case(folder, install::backup_folder_name)
                              .value_or(folder / std::string(install::backup_folder_name));
    const auto check = check_picked_mod_folder(
        kept,
        game_folder,
        ModChoice{{}, {}, options_.accept_unimplemented_hacks, &preference_values_}
    );
    for (const auto& line : check.errors)
        log_line(path_to_utf8(kept) + ": " + line);
    if (!check.refusal.empty())
        return refuse(
            install::oamod::roll_back_refused_prompt(
                title,
                install::oamod::version_label(
                    backup->version, backup->revision, backup->version == installed.version
                ),
                oa::data::languages::interface_text(check.refusal)
            ),
            "the version it keeps cannot be played"
        );
    const bool played = package_target_in_use(kind, folder);
    install::PendingChange pending{};
    pending.kind = &kind;
    pending.root = mods;
    pending.target = target;
    pending.change = install::Change::roll_back;
    pending.incoming = {backup->id, backup->name, backup->version, backup->revision};
    pending.replaced = installed;
    if (played) {
        // As SWITCH does: the dialog's settings are kept and saved, it
        // closes, and the run ends for the folder to change.
        take_renderer_retry(dialog);
        keep_renderer_records();
        apply_engine_settings(dialog.chosen);
        const auto failure = save_engine_settings(dialog.opened, dialog.chosen, dialog.restored);
        state.last_page = dialog.page;
        state.last_developer_list = dialog.developer;
        state.dialog.reset();
        if (failure) {
            EngineSettingsState::report_failed_save(*this, *failure);
            return true;
        }
        install::set_pending_change(std::move(pending));
        log_line(path_to_utf8(folder) + ": rolled back as the run ends");
        request_soft_restart();
        return true;
    }
    install::ChangeOptions options{};
    options.expected = installed;
    options.hooks.wait = wait_pumping;
    const install::ChangeResult result =
        install::commit_change(kind, mods, target, install::Change::roll_back, options);
    mod_install_state().discarder.add(result.discards);
    if (!result.changed)
        return refuse(install::oamod::roll_back_failed_prompt(title), result.detail);
    log_line(path_to_utf8(folder) + ": rolled back to " + backup->version);
    package_changed(kind, folder);
    dialog.mod_names = state.mod_names;
    dialog.mod_folders = state.mod_folders;
    dialog.mod_details = state.mod_details;
    std::ignore = settings::set_folder_notice(dialog, {});
    return false;
}

} // namespace oa::app
