// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The packages waiting for the main menu, the change waiting for its run to
// end and its outcome, kept for the whole process.

#include "files.hpp"

#include "oa/app/package_install/inbox.hpp"
#include "oa/base/threads.hpp"
#include "oa/platform/file_types.hpp"

#include <algorithm>
#include <deque>
#include <exception>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace oa::app::package_install {

namespace fs = std::filesystem;

namespace {

/// What the process keeps between runs.
struct Kept {
    base::threads::Mutex lock{};
    std::deque<fs::path> files{};      ///< packages waiting, oldest first
    std::deque<fs::path> registries{}; ///< .oareg files waiting, oldest first
    std::optional<fs::path> current{}; ///< the package being installed
    std::optional<PendingChange> pending{};
    std::optional<ChangeOutcome> outcome{};
    std::vector<fs::path> discards{};
    std::optional<fs::path> handoff{}; ///< the hand-off folder, while the lock is held
};

/// Returns the process's kept state.
///
/// @return the state
Kept& kept() {
    static Kept state;
    return state;
}

/// Returns the form a path is compared in: weakly canonical, else absolute
/// and normal.
///
/// @param file the path
/// @return its form
fs::path compared(const fs::path& file) {
    std::error_code error;
    fs::path form = fs::weakly_canonical(file, error);
    if (!error)
        return form;
    form = fs::absolute(file, error);
    return (error ? file : form).lexically_normal();
}

/// Returns a file's extension without its dot, with ASCII letters lowered.
///
/// @param file the file
/// @return the extension; empty when the name has none
std::string extension_of(const fs::path& file) {
    std::string extension = detail::folded(detail::utf8_of(file.extension()));
    if (!extension.empty() && extension.front() == '.')
        extension.erase(extension.begin());
    return extension;
}

/// Tells whether `file`'s extension is `extension`, without case.
///
/// @param file the file
/// @param extension the extension, without its dot
/// @return true when they are the same
bool extension_is(const fs::path& file, std::string_view extension) {
    return extension_of(file) == detail::folded(extension);
}

} // namespace

void post_package_file(const fs::path& file) {
    const fs::path form = compared(file);
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    if ((state.current && *state.current == form) ||
        std::find(state.files.begin(), state.files.end(), form) != state.files.end())
        return;
    state.files.push_back(form);
}

std::optional<fs::path> take_package_file() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    if (state.files.empty())
        return std::nullopt;
    fs::path file = std::move(state.files.front());
    state.files.pop_front();
    state.current = file;
    return file;
}

void return_package_file(const fs::path& file) {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    const fs::path form = compared(file);
    state.current.reset();
    if (std::find(state.files.begin(), state.files.end(), form) == state.files.end())
        state.files.push_front(form);
}

void finish_package_file() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    state.current.reset();
}

bool package_files_waiting() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    return !state.files.empty();
}

bool opens_file(const fs::path& file) {
    const std::string extension = extension_of(file);
    for (const oa::platform::file_types::FileType& type : oa::platform::file_types::file_types)
        if (extension == type.extension)
            return true;
    return false;
}

void post_registry_file(const fs::path& file) {
    const fs::path form = compared(file);
    {
        Kept& state = kept();
        const base::threads::LockGuard guard(state.lock);
        if (std::find(state.registries.begin(), state.registries.end(), form) !=
            state.registries.end())
            return;
        state.registries.push_back(form);
    }
    // A .oareg file waits here until M08 takes it, and the log says so.
    detail::log_line("a .oareg file waits: " + detail::utf8_of(form));
}

std::optional<fs::path> take_registry_file() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    if (state.registries.empty())
        return std::nullopt;
    fs::path file = std::move(state.registries.front());
    state.registries.pop_front();
    return file;
}

bool registry_files_waiting() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    return !state.registries.empty();
}

void post_opened_file(const fs::path& file) {
    if (extension_is(file, oa::platform::file_types::registry_extension))
        post_registry_file(file);
    else
        post_package_file(file);
}

void set_pending_change(PendingChange change) {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    state.pending = std::move(change);
}

bool pending_change_waiting() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    return state.pending.has_value();
}

void finish_pending_change(const ChangeOptions& options) {
    std::optional<PendingChange> pending;
    {
        Kept& state = kept();
        const base::threads::LockGuard guard(state.lock);
        pending = std::move(state.pending);
        state.pending.reset();
    }
    if (!pending)
        return;
    ChangeOptions checked = options;
    checked.expected = pending->replaced;
    ChangeOutcome outcome{};
    if (pending->kind == nullptr) {
        pending->hold.reset();
        return;
    }
    outcome.result =
        commit_change(*pending->kind, pending->root, pending->target, pending->change, checked);
    detail::log_line(
        pending->target + (outcome.result.changed ? ": changed before the run" : ": not changed")
    );
    // The hold goes once the change is in place; then the folder is settled.
    pending->hold.reset();
    const Recovery recovery = recover_changes(*pending->kind, pending->root, options);
    std::vector<fs::path> discards = outcome.result.discards;
    discards.insert(discards.end(), recovery.discards.begin(), recovery.discards.end());
    outcome.change = std::move(*pending);
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    state.outcome = std::move(outcome);
    for (fs::path& folder : discards)
        if (std::find(state.discards.begin(), state.discards.end(), folder) == state.discards.end())
            state.discards.push_back(std::move(folder));
}

std::optional<ChangeOutcome> take_change_outcome() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    std::optional<ChangeOutcome> outcome = std::move(state.outcome);
    state.outcome.reset();
    return outcome;
}

void keep_discards(const std::vector<fs::path>& folders) {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    for (const fs::path& folder : folders)
        if (std::find(state.discards.begin(), state.discards.end(), folder) == state.discards.end())
            state.discards.push_back(folder);
}

std::vector<fs::path> take_discards() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    return std::exchange(state.discards, {});
}

void set_handoff_folder(const fs::path& folder) {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    state.handoff = folder;
}

std::optional<fs::path> handoff_folder() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    return state.handoff;
}

} // namespace oa::app::package_install
