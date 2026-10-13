// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Steps 1 to 3 before the game is mounted. The window is the game's own
// window, drawn with the bundled fonts, so a language's name in itself can
// be read before any archive is open. The copy runs on a side thread; this
// thread only pumps the window and reads the progress. A player who already
// has the game, or who named --game-dir, does not see these steps.

#include "oa/app/setup_before_game.hpp"

#include "render_host.hpp"
#include "window_pixels.hpp"

#include "oa/app/content_copy.hpp"
#include "oa/app/disk_image_source.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/app/game_files_hooks.hpp"
#include "language_packs.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/data/content_bundles.hpp"
#include "oa/data/languages.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/platform/files.hpp"
#include "oa/platform/game_installs.hpp"
#include "oa/platform/locale.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/platform/system.hpp"
#include "oa/platform/text_font.hpp"
#include "oa/ui/engine_settings.hpp"
#include "oa/ui/kit/canvas.hpp"
#include "oa/ui/paint/painter.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace oa::app {
namespace {

namespace fs = std::filesystem;
namespace bundles = oa::data::content_bundles;
namespace first_run = oa::ui::first_run;
namespace installs = oa::platform::game_installs;
namespace kit = oa::ui::kit;
namespace languages = oa::data::languages;
namespace locale = oa::platform::locale;
namespace paint = oa::ui::paint;
namespace preferences = oa::platform::preferences;
namespace settings = oa::ui::engine_settings;
namespace text_font = oa::platform::text_font;

/// How many places the game step lists. More than this would run off the step.
constexpr int32_t kMostSources = 4;

/// Which reader a remembered place uses.
enum class SourceKind : uint8_t {
    folder,  ///< a folder of game files
    windows, ///< the Windows installer
    macos,   ///< the macOS package
    disc,    ///< the disk image
    fixture, ///< the check's small install
};

/// A place the game step offered, so the copy can open it again.
struct Remembered {
    std::string token{}; ///< the path, as the row keeps it
    SourceKind kind{SourceKind::folder};
    fs::path path{};
};

/// What this start tells the menu welcome.
struct Resume {
    bool continue_setup{false}; ///< the five-step walk carries on at online
    bool copied{false};         ///< the copying step finished the copy
};

/// The copy, shared by the pre-game window and the menu's copying step.
struct CopyJob {
    ~CopyJob() {
        stop.store(true);
        {
            const std::lock_guard<std::mutex> lock(mutex);
            paused = false;
        }
        wake.notify_all();
        if (worker.joinable())
            worker.join();
    }

    std::mutex mutex{};
    std::condition_variable wake{};
    std::atomic<bool> stop{false};
    bool paused{false};
    bool changed{false};
    first_run::CopyView view{};
    std::thread worker{};
    std::chrono::steady_clock::time_point started{};
    /// Bundle ids in the same order as `view.bundles`. The progress report names an id.
    std::vector<std::string> bundle_ids{};
    uint64_t bytes_done{};  ///< bytes finished, for the line the window's thread writes
    uint64_t bytes_total{}; ///< bytes of every matched file
    uint32_t files_done{};  ///< files finished
    uint32_t files_total{}; ///< matched files
    bool needs_check{
        false
    }; ///< the copy's thread checked it; the window's thread has not applied it
    ContentCheck pending_check{}; ///< that check; empty until needs_check
    /// Bundle ids the copy reported. The start-up check reuses only these.
    std::vector<std::string> copied_bundle_ids{};
};

CopyJob& copy_job() {
    static CopyJob job;
    return job;
}

Resume& resume_state() {
    static Resume state;
    return state;
}

std::vector<Remembered>& remembered_sources() {
    static std::vector<Remembered> sources;
    return sources;
}

/// The catalogue read before the game is mounted. It lives for the process.
languages::InterfaceText& early_catalogue() {
    static languages::InterfaceText catalogue;
    static bool loaded = false;
    if (loaded)
        return catalogue;
    loaded = true;
    const std::string base = oa::platform::program_directory();
    if (base.empty())
        return catalogue;
    const fs::path folder = path_from_utf8(base) / path_from_utf8(engine_languages_folder);
    std::error_code failure;
    if (!fs::is_directory(folder, failure) || failure)
        return catalogue;
    std::vector<fs::path> files;
    for (fs::directory_iterator entry(folder, failure), end; !failure && entry != end;
         entry.increment(failure)) {
        if (!entry->is_regular_file(failure))
            continue;
        const std::string name = path_to_utf8(entry->path().filename());
        if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".tdf") == 0)
            files.push_back(entry->path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        std::ifstream input(file, std::ios::binary);
        std::string text;
        if (input)
            text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        std::string error;
        if (!input || text.size() > languages::most_catalogue_bytes || !catalogue.add(text, &error))
            std::cerr << "open-annihilation: the interface catalogue " << path_to_utf8(file)
                      << " was not read" << (error.empty() ? "" : ": ") << error << '\n';
    }
    return catalogue;
}

/// Writes one preference without dropping the others.
///
/// @param options the parsed command line
/// @param key the preference
/// @param value the value
void write_preference(const Options& options, std::string_view key, std::string_view value) {
    try {
        const fs::path file = preference_file(options.preferences_file);
        std::error_code failure;
        preferences::Values values;
        if (fs::exists(file, failure) && !failure)
            values = preferences::load(file);
        values[std::string(key)] = std::string(value);
        preferences::save(file, values, preferences::SyncFolder::no);
    } catch (const std::exception& error) {
        std::cerr << "open-annihilation: the setup could not store a preference: " << error.what()
                  << '\n';
    }
}

/// Puts a language in effect for the words drawn before the game is mounted.
///
/// @param choice a tag, or the operating system's choice
void apply_early_language(std::string_view choice) {
    const languages::Language& system = languages::preferred_language(locale::preferred_locales());
    const languages::Language& chosen = languages::chosen_language(choice, system);
    languages::set_interface_language(&early_catalogue(), chosen);
}

/// The player's folder, or empty when it cannot be resolved.
///
/// @param options the parsed command line
/// @return the folder
fs::path resolve_user_folder(const Options& options) {
    try {
        const fs::path preferences = preference_file(options.preferences_file);
        std::error_code failure;
        preferences::Values values;
        if (fs::exists(preferences, failure) && !failure)
            values = preferences::load(preferences);
        std::string note;
        return own_user_folder(
            options.user_folder, options.preferences_file, preferences, values, note
        );
    } catch (const std::exception&) {
        return {};
    }
}

/// Tells whether a place lies inside the managed copy.
///
/// @param path the place
/// @param user_folder the player's folder
/// @return true when copying it would write into itself
bool inside_managed(const fs::path& path, const fs::path& user_folder) {
    if (path.empty() || user_folder.empty())
        return false;
    std::error_code failure;
    const fs::path place = fs::weakly_canonical(path, failure);
    if (failure)
        return false;
    const std::string place_text = path_to_utf8(place);
    for (const std::string_view folder : {managed_games_folder, managed_expansions_folder}) {
        const fs::path root = fs::weakly_canonical(user_folder / std::string(folder), failure);
        if (failure)
            continue;
        const std::string root_text = path_to_utf8(root);
        if (place_text == root_text)
            return true;
        if (place_text.size() > root_text.size() &&
            place_text.compare(0, root_text.size(), root_text) == 0 &&
            (place_text[root_text.size()] == '/' || place_text[root_text.size()] == '\\'))
            return true;
    }
    return false;
}

/// A size as the player reads it, in the same steps the downloads page uses.
///
/// @param bytes the size
/// @return the text
std::string bytes_text(uint64_t bytes) {
    if (bytes < 1000)
        return std::to_string(bytes) + " B";
    if (bytes < 1000ULL * 1000ULL)
        return std::to_string(bytes / 1000ULL) + " kB";
    if (bytes < 1000ULL * 1000ULL * 1000ULL) {
        const uint64_t tenths = bytes / 100000ULL;
        return std::to_string(tenths / 10ULL) + "." + std::to_string(tenths % 10ULL) + " MB";
    }
    const uint64_t tenths = bytes / 100000000ULL;
    return std::to_string(tenths / 10ULL) + "." + std::to_string(tenths % 10ULL) + " GB";
}

/// The bundle's name, in English, as the table writes it.
///
/// @param table the table
/// @param id the bundle id
/// @return the name; the id when the table has none
std::string bundle_name(std::span<const bundles::Bundle> table, std::string_view id) {
    for (const bundles::Bundle& bundle : table) {
        if (bundle.id == id)
            return std::string(bundle.english_name);
    }
    return std::string(id);
}

/// Fills a row from a description. A place with no matched file is not listed.
///
/// @param[in,out] model the game step
/// @param path the place
/// @param kind which reader
/// @param mark the badge
/// @param detail the line under the title
/// @param described what the place holds
/// @param table the table the description was made from
/// @return true when the row was added
bool add_described(
    first_run::Model& model,
    const fs::path& path,
    SourceKind kind,
    first_run::SourceMark mark,
    std::string detail,
    const SourceDescription& described,
    std::span<const bundles::Bundle> table
) {
    if (described.bundles.empty() || static_cast<int32_t>(model.sources.size()) >= kMostSources)
        return false;
    const std::string token = path_to_utf8(path);
    for (const Remembered& held : remembered_sources()) {
        if (held.token == token)
            return false;
    }
    first_run::GameSource row;
    row.token = token;
    row.title =
        path.filename().empty() ? std::string("Total Annihilation") : path_to_utf8(path.filename());
    row.detail = std::move(detail);
    row.mark = mark;
    row.footnote = bytes_text(described.bytes_to_copy);
    for (const std::string& id : described.bundles) {
        first_run::SourceChip chip;
        chip.name = bundle_name(table, id);
        chip.held = true;
        row.chips.push_back(std::move(chip));
    }
    std::vector<std::string> missing_ids;
    for (const MissingContentFile& missing : described.missing) {
        if (std::find(described.bundles.begin(), described.bundles.end(), missing.bundle_id) !=
            described.bundles.end())
            continue;
        if (std::find(missing_ids.begin(), missing_ids.end(), missing.bundle_id) !=
            missing_ids.end())
            continue;
        missing_ids.push_back(missing.bundle_id);
        first_run::SourceChip chip;
        chip.name = bundle_name(table, missing.bundle_id);
        chip.held = false;
        row.chips.push_back(std::move(chip));
    }
    remembered_sources().push_back(Remembered{token, kind, path});
    model.sources.push_back(std::move(row));
    return true;
}

/// Lists a folder when it holds game files.
///
/// @param[in,out] model the game step
/// @param folder the folder
/// @param user_folder the player's folder
/// @param detail the line under the title
/// @param table the table to match
/// @param kind which reader; fixture uses the check's table
void add_folder_source(
    first_run::Model& model,
    const fs::path& folder,
    const fs::path& user_folder,
    std::string detail,
    std::span<const bundles::Bundle> table,
    SourceKind kind
) {
    if (folder.empty() || inside_managed(folder, user_folder))
        return;
    std::error_code failure;
    if (!fs::is_directory(folder, failure) || failure)
        return;
    FolderContentSource source(folder, table);
    SourceDescription described;
    std::string error;
    if (!describe_source(source, table, described, &error))
        return;
    add_described(
        model, folder, kind, first_run::SourceMark::folder, std::move(detail), described, table
    );
}

/// Lists an installer whose size is one this game knows.
///
/// @param[in,out] model the game step
/// @param file the installer
/// @param user_folder the player's folder
void add_installer_source(
    first_run::Model& model, const fs::path& file, const fs::path& user_folder
) {
    if (file.empty() || inside_managed(file, user_folder))
        return;
    std::error_code failure;
    if (!fs::is_regular_file(file, failure) || failure)
        return;
    const auto size = fs::file_size(file, failure);
    if (failure)
        return;
    const bundles::Installer* installer = bundles::find_installer(static_cast<uint64_t>(size));
    if (installer == nullptr)
        return;
    SourceKind kind = SourceKind::windows;
    first_run::SourceMark mark = first_run::SourceMark::gog;
    if (installer->kind == bundles::InstallerKind::macos) {
        kind = SourceKind::macos;
        mark = first_run::SourceMark::package;
    } else if (installer->kind == bundles::InstallerKind::older_mac) {
        kind = SourceKind::disc;
        mark = first_run::SourceMark::disc;
    }
    std::unique_ptr<ContentSource> source;
    const std::span<const bundles::Bundle> table = bundles::bundles();
    if (kind == SourceKind::macos)
        source = std::make_unique<MacosInstallerSource>(file, table, true);
    else if (kind == SourceKind::disc)
        source = std::make_unique<DiskImageContentSource>(file, table, DiskImageOptions{true});
    else
        source = std::make_unique<WindowsInstallerSource>(file, table);
    SourceDescription described;
    std::string error;
    if (!describe_source(*source, table, described, &error))
        return;
    add_described(model, file, kind, mark, path_to_utf8(file.parent_path()), described, table);
}

/// Lists the places this machine already has.
///
/// @param[in,out] model the game step
/// @param user_folder the player's folder
/// @param found_install a folder already found; empty when there is none
void fill_sources(
    first_run::Model& model, const fs::path& user_folder, const fs::path& found_install
) {
    model.sources.clear();
    model.source = -1;
    remembered_sources().clear();
    const std::span<const bundles::Bundle> table = bundles::bundles();
    if (!found_install.empty())
        add_folder_source(model, found_install, user_folder, {}, table, SourceKind::folder);
    for (const installs::Candidate& candidate :
         installs::find_candidates(installs::default_search_roots())) {
        add_folder_source(
            model,
            candidate.folder,
            user_folder,
            std::string(installs::source_words(candidate.source, candidate.removable)),
            table,
            SourceKind::folder
        );
    }
    std::vector<fs::path> downloads;
    const fs::path home = installs::home_folder();
    if (!home.empty())
        downloads.push_back(home / "Downloads");
    try {
        const fs::path documents = preferences::documents_directory();
        if (!documents.empty())
            downloads.push_back(documents.parent_path() / "Downloads");
    } catch (const std::exception&) {
    }
    for (const fs::path& folder : downloads) {
        std::error_code failure;
        if (!fs::is_directory(folder, failure) || failure)
            continue;
        for (fs::directory_iterator entry(folder, failure), end; !failure && entry != end;
             entry.increment(failure)) {
            if (!entry->is_regular_file(failure))
                continue;
            add_installer_source(model, entry->path(), user_folder);
        }
    }
    if (!model.sources.empty())
        model.source = 0;
}

/// The languages the step can offer before a pack is downloaded.
///
/// @param[in,out] model the model
/// @param shown the tag in effect
void fill_cards(first_run::Model& model, std::string_view shown) {
    model.cards.clear();
    for (const languages::Language* language : languages::known_languages()) {
        if (language == nullptr || !languages::playable(*language))
            continue;
        if (language->source != languages::Source::built_in &&
            language->source != languages::Source::installed)
            continue;
        first_run::Card card;
        card.tag = std::string(language->tag);
        card.own_name = std::string(language->endonym);
        card.english_name = std::string(language->english_name);
        card.kind = language->source == languages::Source::installed
                        ? first_run::CardKind::installed
                        : first_run::CardKind::built_in;
        card.current = language->tag == shown;
        model.cards.push_back(std::move(card));
    }
}

/// Opens the centred card's language.
///
/// @param session the language step
/// @param options the parsed command line
void apply_selected_language(first_run::Session& session, const Options& options) {
    const int32_t index = session.carousel.selected;
    if (index < 0 || static_cast<std::size_t>(index) >= session.model.cards.size())
        return;
    const first_run::Card& card = session.model.cards[static_cast<std::size_t>(index)];
    const languages::Language* known = languages::find_by_tag(card.tag);
    if (known == nullptr || !languages::playable(*known))
        return;
    apply_early_language(card.tag);
    write_preference(options, settings::key::language, known->tag);
    for (first_run::Card& other : session.model.cards)
        other.current = other.tag == card.tag;
}

/// Replaces `{token}` once.
void replace_token(std::string& text, std::string_view token, std::string_view value) {
    const auto at = text.find(token);
    if (at != std::string::npos)
        text.replace(at, token.size(), value);
}

/// The overall line, in the language the interface shows.
std::string overall_line(uint64_t done, uint64_t total) {
    std::string text{languages::interface_text("{done} of {total} · {percent}%")};
    const uint64_t percent = total == 0 ? 0 : (done * 100ULL) / total;
    replace_token(text, "{done}", std::to_string(done));
    replace_token(text, "{total}", std::to_string(total));
    replace_token(text, "{percent}", std::to_string(percent));
    return text;
}

/// About how long is left, or empty when it cannot be said.
std::string remaining_line(const CopyJob& job, uint64_t done, uint64_t total) {
    if (done == 0 || total <= done)
        return {};
    const auto elapsed = std::chrono::steady_clock::now() - job.started;
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    if (seconds < 1)
        return {};
    const uint64_t left = ((total - done) * static_cast<uint64_t>(seconds)) / done;
    std::string text{languages::interface_text("about {seconds} seconds left")};
    replace_token(text, "{seconds}", std::to_string(left));
    return text;
}

/// Copies one progress report into the view. The report's views die with it.
/// A paused copy waits here, so the bytes stop between chunks.
void note_progress(CopyJob& job, const CopyProgress& progress) {
    std::unique_lock<std::mutex> lock(job.mutex);
    job.wake.wait(lock, [&job] { return !job.paused || job.stop.load(); });
    first_run::CopyView& view = job.view;
    view.whole = 1000;
    if (progress.bytes_total == 0)
        view.done = 0;
    else {
        const uint64_t scaled = (progress.bytes_done * 1000ULL) / progress.bytes_total;
        view.done = static_cast<int32_t>(std::min<uint64_t>(scaled, 1000ULL));
    }
    job.bytes_done = progress.bytes_done;
    job.bytes_total = progress.bytes_total;
    job.files_done = progress.files_done;
    job.files_total = progress.files_total;
    if (!progress.bundle_id.empty()) {
        bool seen = false;
        for (const std::string& id : job.copied_bundle_ids) {
            if (id == progress.bundle_id)
                seen = true;
        }
        if (!seen)
            job.copied_bundle_ids.emplace_back(progress.bundle_id);
    }
    view.running = true;
    int32_t current = -1;
    const auto rows = static_cast<int32_t>(job.bundle_ids.size());
    for (int32_t index = 0; index < rows; ++index) {
        if (job.bundle_ids[static_cast<std::size_t>(index)] == progress.bundle_id)
            current = index;
    }
    if (current >= 0) {
        for (int32_t index = 0; index < current; ++index) {
            first_run::CopyBundle& earlier = view.bundles[static_cast<std::size_t>(index)];
            if (earlier.phase == first_run::CopyPhase::differ)
                continue;
            earlier.phase = first_run::CopyPhase::done;
            earlier.done = earlier.whole;
        }
        first_run::CopyBundle& row = view.bundles[static_cast<std::size_t>(current)];
        row.phase = first_run::CopyPhase::copying;
        row.file = std::string(progress.copy_name);
        row.done = view.done;
        row.amount = bytes_text(progress.bytes_done);
    }
    job.changed = true;
}

/// The progress callback. It runs on the copy's thread.
void on_copy_progress(void* context, const CopyProgress& progress) {
    if (context == nullptr)
        return;
    note_progress(*static_cast<CopyJob*>(context), progress);
}

/// The check's progress. It runs on the copy's thread and only stores counts.
bool on_copy_check(uint32_t done, uint32_t total, void* context) {
    if (context == nullptr)
        return false;
    auto* job = static_cast<CopyJob*>(context);
    const std::lock_guard<std::mutex> lock(job->mutex);
    job->view.hashing = true;
    job->view.running = true;
    job->view.check_done = done;
    job->view.check_total = total;
    job->changed = true;
    return !job->stop.load();
}

/// One bundle row for each bundle the table names that the copy may write.
void prime_bundles(CopyJob& job, std::span<const bundles::Bundle> table) {
    job.view.bundles.clear();
    job.bundle_ids.clear();
    for (const bundles::Bundle& bundle : table) {
        first_run::CopyBundle row;
        row.name = std::string(bundle.english_name);
        row.whole = 1000;
        row.phase = first_run::CopyPhase::waiting;
        job.bundle_ids.emplace_back(bundle.id);
        job.view.bundles.push_back(std::move(row));
    }
}

/// Marks every row from the check and sets the verdict.
void apply_check(first_run::CopyView& view, const ContentCheck& check, bool finished) {
    uint32_t differ = 0;
    const auto rows = static_cast<int32_t>(view.bundles.size());
    for (int32_t index = 0; index < rows; ++index) {
        first_run::CopyBundle& row = view.bundles[static_cast<std::size_t>(index)];
        const ContentBundleCheck* found = nullptr;
        const std::string id = index < static_cast<int32_t>(copy_job().bundle_ids.size())
                                   ? copy_job().bundle_ids[static_cast<std::size_t>(index)]
                                   : std::string{};
        for (const ContentBundleCheck& bundle : check.bundles) {
            if (bundle.id == id || bundle.english_name == row.name)
                found = &bundle;
        }
        if (found == nullptr)
            continue;
        row.amount = content_bundle_line(*found);
        if (found->files_differ == 0) {
            row.phase = first_run::CopyPhase::done;
            row.done = row.whole;
        } else {
            row.phase = first_run::CopyPhase::differ;
            differ += found->files_differ;
        }
    }
    view.running = false;
    view.paused = false;
    view.hashing = false;
    view.remaining.clear();
    if (!finished) {
        view.verdict = std::string(languages::interface_text("The game could not be copied."));
        view.checked = false;
        return;
    }
    view.verdict = content_match_line(differ);
    view.checked = true;
    view.done = view.whole;
}

/// Opens the chosen place.
std::unique_ptr<ContentSource> open_source(const Remembered& remembered) {
    const std::span<const bundles::Bundle> table =
        remembered.kind == SourceKind::fixture ? copy_check_bundles() : bundles::bundles();
    switch (remembered.kind) {
    case SourceKind::folder:
    case SourceKind::fixture:
        return std::make_unique<FolderContentSource>(remembered.path, table);
    case SourceKind::windows:
        return std::make_unique<WindowsInstallerSource>(remembered.path, table);
    case SourceKind::macos:
        return std::make_unique<MacosInstallerSource>(remembered.path, table, true);
    case SourceKind::disc:
        return std::make_unique<DiskImageContentSource>(
            remembered.path, table, DiskImageOptions{true}
        );
    }
    return nullptr;
}

/// Ends access a system picker gave, after the copy no longer reads the place.
void release_picked_sources() {
    const GameFilesHooks& hooks = game_files_hooks();
    if (hooks.release_source == nullptr)
        return;
    for (const Remembered& remembered : remembered_sources()) {
        if (remembered.kind == SourceKind::fixture)
            continue;
        const std::string path = path_to_utf8(remembered.path);
        hooks.release_source(hooks.context, path.c_str());
    }
}

/// A key the setup answers, or nothing.
std::optional<kit::Key> setup_key(const SDL_Event& event) {
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat)
        return std::nullopt;
    switch (event.key.key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return kit::Key::enter;
    case SDLK_ESCAPE:
        return kit::Key::escape;
    case SDLK_UP:
        return kit::Key::up;
    case SDLK_DOWN:
        return kit::Key::down;
    case SDLK_LEFT:
        return kit::Key::left;
    case SDLK_RIGHT:
        return kit::Key::right;
    case SDLK_SPACE:
        return kit::Key::space;
    case SDLK_TAB:
        return (event.key.mod & SDL_KMOD_SHIFT) != 0 ? kit::Key::back_tab : kit::Key::tab;
    default:
        return std::nullopt;
    }
}

/// The frame the window is laid out in.
kit::Frame frame_of_window(const oa::ui::game_files::Viewport& viewport) {
    kit::Viewport points;
    points.width = viewport.width;
    points.height = viewport.height;
    points.density = viewport.px_per_point > 0.F ? viewport.px_per_point : 1.F;
    const int32_t scale = static_cast<int32_t>(std::lround(points.density * 100.F));
    points.scale_percent = std::max(scale, int32_t{100});
    points.safe.left = viewport.safe.left;
    points.safe.top = viewport.safe.top;
    points.safe.right = viewport.safe.right;
    points.safe.bottom = viewport.safe.bottom;
    return kit::frame_of(points);
}

/// A canvas pixel as a point of the frame.
kit::Point point_of(float x, float y, const kit::Frame& frame) {
    const double scale = static_cast<double>(std::max(frame.scale_percent, int32_t{1})) / 100.0;
    return {
        static_cast<int32_t>(std::lround(static_cast<double>(x) / scale)) - frame.area.x,
        static_cast<int32_t>(std::lround(static_cast<double>(y) / scale)) - frame.area.y
    };
}

struct PickWait {
    bool done{false};
    bool quit{false};
    std::vector<std::string> paths{};
    std::string error{};
};

void picker_done(void* userdata, const std::vector<std::string>& paths, bool, const char* error) {
    auto* wait = static_cast<PickWait*>(userdata);
    wait->paths = paths;
    if (error != nullptr)
        wait->error = error;
    wait->done = true;
}

void SDLCALL folder_done(void* userdata, const char* const* filelist, int) {
    auto* wait = static_cast<PickWait*>(userdata);
    if (filelist == nullptr) {
        const char* error = SDL_GetError();
        if (error != nullptr && error[0] != '\0')
            wait->error = error;
    } else if (filelist[0] != nullptr) {
        wait->paths.emplace_back(filelist[0]);
    }
    wait->done = true;
}

/// Pumps until the picker answers. A quit is pushed back for the setup.
bool pump_until_picked(PickWait& wait) {
    while (!wait.done) {
        SDL_Event event;
        if (!SDL_WaitEventTimeout(&event, 50))
            continue;
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            wait.quit = true;
            wait.done = true;
            SDL_PushEvent(&event);
            break;
        }
    }
    return !wait.quit && !wait.paths.empty() && wait.error.empty();
}

/// Writes the check's small files under `folder`, one path piece at a time.
bool write_fixture(const fs::path& folder) {
    for (const CopyCheckFile& file : copy_check_files()) {
        fs::path dest = folder;
        std::string part;
        for (const char byte : file.copy_name) {
            if (byte == '/' || byte == '\\') {
                if (!part.empty()) {
                    dest /= path_from_utf8(part);
                    part.clear();
                }
            } else {
                part.push_back(byte);
            }
        }
        if (!part.empty())
            dest /= path_from_utf8(part);
        const auto* data = reinterpret_cast<const uint8_t*>(file.bytes.data());
        std::string error;
        if (!oa::platform::replace_file(
                dest, std::span<const uint8_t>(data, file.bytes.size()), &error
            ))
            return false;
    }
    return true;
}

/// Tells whether this start shows the game steps.
bool setup_due(const Options& options, bool mount_copy) {
    // The pre-game window check walks these steps here. --game-dir is what the
    // menu plays afterwards, and a dummy video would otherwise count as unattended.
    if (options.check_setup_window && !options.check_first_run)
        return true;
    if (!mount_copy || options.game_dir_named || options.check_first_run)
        return false;
    if (options.unattended || options.headless_check || options.benchmark_frames.has_value() ||
        !options.stage_file.empty() || options.remote_controlled)
        return false;
    const char* ci = SDL_getenv("CI");
    const char* video = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
    if (unattended_environment(
            ci == nullptr ? std::string_view{} : std::string_view{ci},
            video == nullptr ? std::string_view{} : std::string_view{video}
        ))
        return false;
    return true;
}

/// Reads the preference that records a finished welcome.
bool welcome_is_done(const Options& options) {
    try {
        const fs::path file = preference_file(options.preferences_file);
        std::error_code failure;
        if (!fs::exists(file, failure) || failure)
            return false;
        const preferences::Values values = preferences::load(file);
        const auto found = values.find(std::string(first_run::preference_key));
        return found != values.end() && found->second == first_run::preference_done;
    } catch (const std::exception&) {
        return false;
    }
}

/// The language the preferences already name, or the operating system's.
std::string stored_language(const Options& options) {
    try {
        const fs::path file = preference_file(options.preferences_file);
        std::error_code failure;
        if (!fs::exists(file, failure) || failure)
            return std::string(languages::system_choice);
        const preferences::Values values = preferences::load(file);
        const auto found = values.find(std::string(settings::key::language));
        if (found == values.end() || found->second.empty())
            return std::string(languages::system_choice);
        return found->second;
    } catch (const std::exception&) {
        return std::string(languages::system_choice);
    }
}

/// How long the pre-game window check may run, in frames of this loop.
constexpr int kSetupCheckFrames = 2000;

/// Deutsch on the language carousel.
constexpr std::string_view kDeutschCard = "first-run.language.card.de";

/// Where the pre-game window check is in its walk. One call takes one action.
struct SetupWindowDrive {
    enum class Phase : uint8_t {
        seek,     ///< bring Deutsch onto the carousel
        pick,     ///< choose Deutsch
        language, ///< the German lines, then Next
        game,     ///< the fixture, then Next
        copying,  ///< the copying step's German lines
        wait,     ///< the copy and its check
        records,  ///< the managed copy and its records, then Next
        done,     ///< the walk has left the window
    };
    Phase phase{Phase::seek};
    int frames{0};  ///< frames this walk has taken
    int back{0};    ///< previous-card presses
    int forward{0}; ///< next-card presses
    int pause{0};   ///< frames to wait before the next card press
    int picks{0};   ///< presses on Deutsch
};

/// Throws when a step of the pre-game window check failed.
void setup_check_expect(bool ok, std::string_view what) {
    if (!ok)
        throw std::runtime_error("setup window check: " + std::string(what));
}

/// Walks language, the game and the copy. One call is one press, or a look.
///
/// The scene is the one just drawn. A press is handled and the next call sees
/// the step it opened. The check's install is already the chosen source.
template <typename Handle>
void drive_setup_window_check(
    SetupWindowDrive& drive,
    const fs::path& user,
    first_run::Session& session,
    const first_run::Scene& scene,
    Handle&& handle
) {
    if (drive.phase == SetupWindowDrive::Phase::done)
        return;
    ++drive.frames;
    setup_check_expect(drive.frames <= kSetupCheckFrames, "ran out of frames");

    const auto has = [&](std::string_view name) {
        for (const kit::Control& control : scene.list.controls)
            if (control.name == name && control.enabled)
                return true;
        return false;
    };
    const auto shows = [&](std::string_view words) {
        std::string joined;
        for (const kit::Item& item : scene.list.items) {
            if (item.text.find(words) != std::string::npos)
                return true;
            joined += item.text;
        }
        for (const kit::Control& control : scene.list.controls) {
            if (control.text.find(words) != std::string::npos)
                return true;
            joined += control.text;
        }
        return joined.find(words) != std::string::npos;
    };
    const auto click = [&](std::string_view name) {
        kit::Rect rect{};
        bool enabled = false;
        for (const kit::Control& control : scene.list.controls) {
            if (control.name != name)
                continue;
            rect = control.rect;
            enabled = control.enabled;
            break;
        }
        setup_check_expect(enabled, std::string(name) + " is not on the setup");
        const kit::Point at{rect.x + rect.width / 2, rect.y + rect.height / 2};
        handle(first_run::pointer(session, scene, first_run::PointerKind::down, at, 0, 1));
        handle(first_run::pointer(session, scene, first_run::PointerKind::up, at, 0, 1));
    };
    const auto german = [](std::string_view key, std::string_view words) {
        const std::string got{languages::interface_text(key)};
        setup_check_expect(got == words, std::string(key) + " is not German");
    };

    if (drive.pause > 0) {
        if (drive.phase == SetupWindowDrive::Phase::seek && has(kDeutschCard)) {
            drive.pause = 0;
            drive.phase = SetupWindowDrive::Phase::pick;
            return;
        }
        --drive.pause;
        return;
    }

    switch (drive.phase) {
    case SetupWindowDrive::Phase::seek:
        setup_check_expect(
            session.model.step == first_run::Step::language, "the language step did not show"
        );
        if (has(kDeutschCard)) {
            drive.phase = SetupWindowDrive::Phase::pick;
            return;
        }
        if (drive.back < 8 && has("first-run.language.previous")) {
            click("first-run.language.previous");
            ++drive.back;
            drive.pause = 40;
            return;
        }
        if (drive.forward < 8 && has("first-run.language.next-card")) {
            click("first-run.language.next-card");
            ++drive.forward;
            drive.pause = 40;
            return;
        }
        throw std::runtime_error("setup window check: Deutsch is not on the carousel");
    case SetupWindowDrive::Phase::pick:
        setup_check_expect(has(kDeutschCard), "Deutsch is not on the carousel");
        click(kDeutschCard);
        ++drive.picks;
        if (drive.picks >= 2)
            drive.phase = SetupWindowDrive::Phase::language;
        return;
    case SetupWindowDrive::Phase::language:
        german("Where is Total Annihilation?", "Wo ist Total Annihilation?");
        german(
            "OA copies the game once. The installation is left as it is.",
            "OA kopiert das Spiel einmal. Die Installation bleibt, wie sie ist."
        );
        german(
            "If OA stops, the copy carries on from here next time.",
            "Wenn OA anhält, geht die Kopie beim nächsten Mal von hier weiter."
        );
        german("Copying Total Annihilation", "Total Annihilation wird kopiert");
        german("All files match", "Alle Dateien stimmen überein");
        german("Not now", "Nicht jetzt");
        click("first-run.language.next");
        drive.phase = SetupWindowDrive::Phase::game;
        return;
    case SetupWindowDrive::Phase::game:
        setup_check_expect(
            session.model.step == first_run::Step::game, "the game step did not show"
        );
        setup_check_expect(session.model.source == 0, "the check's install was not chosen");
        setup_check_expect(
            scene.title_text == "Wo ist Total Annihilation?", "the game step's title is not German"
        );
        setup_check_expect(shows("OA kopiert das Spiel einmal"), "the game step is not in German");
        click("first-run.game.next");
        drive.phase = SetupWindowDrive::Phase::copying;
        return;
    case SetupWindowDrive::Phase::copying:
        setup_check_expect(
            session.model.step == first_run::Step::copying, "the copying step did not show"
        );
        setup_check_expect(
            scene.title_text == "Total Annihilation wird kopiert",
            "the copying step's title is not German"
        );
        setup_check_expect(shows("Wenn OA anhält"), "the copying step is not in German");
        drive.phase = SetupWindowDrive::Phase::wait;
        return;
    case SetupWindowDrive::Phase::wait:
        setup_check_expect(
            session.model.step == first_run::Step::copying, "the copying step closed early"
        );
        if (shows("Alle Dateien stimmen überein"))
            drive.phase = SetupWindowDrive::Phase::records;
        return;
    case SetupWindowDrive::Phase::records: {
        const fs::path games = user / std::string(bundles::managed_games_folder) / "totala";
        const fs::path campaign =
            user / std::string(bundles::managed_expansions_folder) / "totala" / "campaign";
        const auto regular = [](const fs::path& path) {
            std::error_code failure;
            return fs::is_regular_file(path, failure) && !failure;
        };
        setup_check_expect(
            regular(games / std::string(bundles::content_record_name)),
            "Games/totala/.oa-content was not written"
        );
        setup_check_expect(regular(games / "rev31.gp3"), "rev31.gp3 was not copied");
        setup_check_expect(regular(games / "Data" / "2.zrb"), "Data/2.zrb was not copied");
        setup_check_expect(
            regular(campaign / std::string(bundles::content_record_name)),
            "Expansions/totala/campaign/.oa-content was not written"
        );
        setup_check_expect(regular(campaign / "totala4.hpi"), "totala4.hpi was not copied");
        setup_check_expect(session.model.copy.checked, "the copy was not checked");
        click("first-run.copying.next");
        drive.phase = SetupWindowDrive::Phase::done;
        std::cout << "setup window check: the game was copied\n" << std::flush;
        return;
    }
    case SetupWindowDrive::Phase::done:
        return;
    }
}

} // namespace

bool setup_before_game_due(const Options& options, bool mount_copy) {
    return setup_due(options, mount_copy);
}

void setup_welcome_again() noexcept {
    resume_state().continue_setup = false;
}

SetupBefore run_setup_before_game(
    const Options& options, SetupDisplay display, bool mount_copy, const fs::path& found_install
) {
    resume_state() = {};
    if (!setup_due(options, mount_copy) || display.window == nullptr || display.renderer == nullptr)
        return SetupBefore::not_run;
    const fs::path user = resolve_user_folder(options);
    if (user.empty())
        return SetupBefore::not_run;
    if (managed_base_present(user, bundles::bundles()))
        return SetupBefore::not_run;

    const std::string language = stored_language(options);
    apply_early_language(language);
    const languages::Language& shown = languages::interface_language();

    first_run::Session session;
    session.model.walk = first_run::Walk::setup;
    session.model.step =
        welcome_is_done(options) ? first_run::Step::game : first_run::Step::language;
    session.model.controller = SDL_HasGamepad();
    fill_cards(session.model, shown.tag);
    session.carousel.selected = 0;
    for (int32_t index = 0; index < static_cast<int32_t>(session.model.cards.size()); ++index) {
        if (session.model.cards[static_cast<std::size_t>(index)].current)
            session.carousel.selected = index;
    }
    first_run::bind(session);
    if (session.model.step == first_run::Step::game) {
        fill_sources(session.model, user, found_install);
        if (options.check_setup_window)
            setup_install_check_source(user, session.model);
    }

    std::unique_ptr<text_font::FontStack> fonts;
    try {
        fonts = text_font::FontStack::open(text_font::bundled_font_directory());
    } catch (const std::exception& error) {
        std::cerr << "open-annihilation: the setup's fonts could not be opened: " << error.what()
                  << '\n';
    }
    RenderState render_state(display.renderer);
    WindowFrame frame(display.renderer, "the setup");
    oa::ui::game_files::Viewport viewport;
    paint::Canvas pixels;
    bool dirty = true;
    bool awake = false;
    SetupBefore ended = SetupBefore::skipped;
    uint64_t last_ticks = 0;

    const auto relayout = [&](const kit::Frame& placed) {
        first_run::Scene laid = first_run::lay_out(session, placed, nullptr);
        first_run::ensure_focus(session, laid.list);
        return laid;
    };

    // The check's German sentences stay on one line at the picture's wide size.
    if (options.check_setup_window &&
        (!SDL_SetWindowSize(display.window, 1280, 720) || !SDL_SyncWindow(display.window)))
        throw std::runtime_error("setup window check: the window did not take 1280x720");
    kit::Frame room = frame_of_window(viewport);
    if (read_window_viewport(display.window, display.renderer, viewport))
        room = frame_of_window(viewport);
    first_run::Scene scene = relayout(room);
    SetupWindowDrive window_check;

    const auto present = [&] {
        if (viewport.width <= 0 || viewport.height <= 0)
            return;
        if (pixels.width != viewport.width || pixels.height != viewport.height)
            pixels = paint::make_canvas(viewport.width, viewport.height);
        paint::Painter painter(pixels);
        painter.fill_rect({0, 0, pixels.width, pixels.height}, paint::Rgba{0x1b, 0x1e, 0x19, 255});
        kit::Canvas canvas;
        canvas.smooth.painter = &painter;
        canvas.smooth.fonts = fonts.get();
        canvas.smooth.scale_percent = room.scale_percent;
        canvas.smooth.origin = {room.area.x, room.area.y};
        first_run::draw(canvas, scene, session);
        static_cast<void>(frame.show(pixels, render_state, display.host));
        dirty = false;
    };

    const auto handle = [&](const first_run::Outcome& outcome) {
        if (outcome.ask == first_run::Ask::none) {
            dirty = dirty || outcome.redraw;
            return;
        }
        switch (outcome.ask) {
        case first_run::Ask::none:
            break;
        case first_run::Ask::skip:
            write_preference(options, first_run::preference_key, first_run::preference_done);
            ended = SetupBefore::skipped;
            return;
        case first_run::Ask::quit:
            ended = SetupBefore::quit;
            return;
        case first_run::Ask::back:
            if (session.model.step == first_run::Step::game)
                session.model.step = first_run::Step::language;
            else if (session.model.step == first_run::Step::online)
                session.model.step = first_run::Step::copying;
            session.focus_ready = false;
            break;
        case first_run::Ask::advance:
            if (session.model.step == first_run::Step::language) {
                apply_selected_language(session, options);
                fill_sources(session.model, user, found_install);
                if (options.check_setup_window)
                    setup_install_check_source(user, session.model);
                session.model.step = first_run::Step::game;
                session.focus_ready = false;
            } else if (session.model.step == first_run::Step::game) {
                if (setup_begin_copy(user, session.model)) {
                    session.model.step = first_run::Step::copying;
                    session.focus_ready = false;
                    if (!awake) {
                        SDL_DisableScreenSaver();
                        awake = true;
                    }
                }
            } else if (
                session.model.step == first_run::Step::copying && session.model.copy.checked
            ) {
                resume_state().continue_setup = true;
                resume_state().copied = true;
                ended = SetupBefore::copied;
                return;
            }
            break;
        case first_run::Ask::pick:
            apply_selected_language(session, options);
            break;
        case first_run::Ask::locate:
            if (setup_locate_source(display.window, user, session.model))
                session.focus_ready = false;
            break;
        case first_run::Ask::pause_copy:
            setup_set_paused(!session.model.copy.paused);
            session.model.copy.paused = !session.model.copy.paused;
            break;
        case first_run::Ask::cancel_copy:
            setup_request_cancel();
            setup_stop_join();
            session.model.copy.running = false;
            session.model.copy.paused = false;
            session.model.step = first_run::Step::game;
            session.focus_ready = false;
            if (awake) {
                SDL_EnableScreenSaver();
                awake = false;
            }
            break;
        case first_run::Ask::cancel_download:
        case first_run::Ask::retry_download:
        case first_run::Ask::choose_on:
        case first_run::Ask::choose_off:
            break;
        }
        dirty = true;
    };

    WindowPress press;
    ended = SetupBefore::not_run;
    bool running = true;
    while (running) {
        if (read_window_viewport(display.window, display.renderer, viewport)) {
            room = frame_of_window(viewport);
            dirty = true;
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                if (session.model.step == first_run::Step::copying && session.model.copy.running)
                    setup_request_cancel();
                setup_stop_join();
                ended = SetupBefore::quit;
                running = false;
                break;
            }
            if (const std::optional<kit::Key> pressed = setup_key(event)) {
                handle(first_run::key(session, scene, *pressed));
                continue;
            }
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                kit::Key pressed = kit::Key::info;
                bool mapped = true;
                switch (event.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_SOUTH:
                    pressed = kit::Key::enter;
                    break;
                case SDL_GAMEPAD_BUTTON_EAST:
                    pressed = kit::Key::escape;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_UP:
                    pressed = kit::Key::up;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                    pressed = kit::Key::down;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                    pressed = kit::Key::left;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                    pressed = kit::Key::right;
                    break;
                default:
                    mapped = false;
                    break;
                }
                if (mapped)
                    handle(first_run::key(session, scene, pressed));
                continue;
            }
            const PointerEvent pointer = read_pointer(event, viewport, press);
            if (pointer.step == PointerStep::down) {
                press.active = true;
                press.finger = pointer.finger;
                press.touch = pointer.touch;
                press.id = pointer.id;
            } else if (pointer.step == PointerStep::up || pointer.step == PointerStep::cancel) {
                press.active = false;
            }
            if (pointer.step == PointerStep::wheel) {
                const float notches = -pointer.wheel_points / wheel_notch_points;
                handle(first_run::wheel(session, scene, notches));
                continue;
            }
            if (pointer.step == PointerStep::none)
                continue;
            const first_run::PointerKind kind =
                pointer.step == PointerStep::down ? first_run::PointerKind::down
                : pointer.step == PointerStep::up ? first_run::PointerKind::up
                                                  : first_run::PointerKind::move;
            const int32_t reach = pointer.finger ? 22 : 0;
            handle(
                first_run::pointer(
                    session, scene, kind, point_of(pointer.x, pointer.y, room), reach, 1
                )
            );
        }
        if (!running)
            break;
        if (session.model.step == first_run::Step::copying) {
            first_run::CopyView view;
            if (setup_pull_copy(view)) {
                session.model.copy = std::move(view);
                dirty = true;
            }
        }
        const uint64_t now = SDL_GetTicks();
        uint32_t dt = 16;
        if (last_ticks != 0 && now >= last_ticks)
            dt = static_cast<uint32_t>(std::min<uint64_t>(now - last_ticks, 1000ULL));
        last_ticks = now;
        first_run::tick(session, dt);
        if (dirty) {
            scene = relayout(room);
            present();
        }
        if (options.check_setup_window) {
            try {
                drive_setup_window_check(window_check, user, session, scene, handle);
            } catch (...) {
                setup_request_cancel();
                setup_stop_join();
                if (awake) {
                    SDL_EnableScreenSaver();
                    awake = false;
                }
                release_picked_sources();
                throw;
            }
        }
        if (ended == SetupBefore::skipped || ended == SetupBefore::quit ||
            ended == SetupBefore::copied)
            running = false;
        else
            SDL_Delay(16);
    }
    if (awake)
        SDL_EnableScreenSaver();
    setup_stop_join();
    release_picked_sources();
    if (ended == SetupBefore::not_run)
        ended = SetupBefore::quit;
    return ended;
}

first_run::Step setup_menu_step() noexcept {
    return resume_state().continue_setup ? first_run::Step::online : first_run::Step::language;
}

first_run::Walk setup_menu_walk() noexcept {
    return resume_state().continue_setup ? first_run::Walk::setup : first_run::Walk::welcome;
}

bool setup_copied_this_start() noexcept {
    return resume_state().copied;
}

void setup_install_check_source(const fs::path& user_folder, first_run::Model& model) {
    model.sources.clear();
    model.source = -1;
    remembered_sources().clear();
    if (user_folder.empty())
        return;
    const fs::path folder = user_folder / "fixture-install";
    if (!write_fixture(folder)) {
        std::cerr << "open-annihilation: the setup check could not write its install\n";
        return;
    }
    FolderContentSource source(folder, copy_check_bundles());
    SourceDescription described;
    std::string error;
    if (!describe_source(source, copy_check_bundles(), described, &error)) {
        std::cerr << "open-annihilation: the setup check could not read its install"
                  << (error.empty() ? "" : ": ") << error << '\n';
        return;
    }
    add_described(
        model,
        folder,
        SourceKind::fixture,
        first_run::SourceMark::folder,
        {},
        described,
        copy_check_bundles()
    );
    if (!model.sources.empty()) {
        model.source = 0;
        // The copying step's "from" line says this is the check's install,
        // and the check that follows uses the check's table.
        model.sources.front().title = "Total Annihilation";
    }
}

bool setup_begin_copy(const fs::path& user_folder, const first_run::Model& model) {
    if (model.source < 0 || static_cast<std::size_t>(model.source) >= model.sources.size())
        return false;
    const std::string token = model.sources[static_cast<std::size_t>(model.source)].token;
    const Remembered* chosen = nullptr;
    for (const Remembered& remembered : remembered_sources()) {
        if (remembered.token == token)
            chosen = &remembered;
    }
    if (chosen == nullptr || user_folder.empty())
        return false;
    setup_stop_join();
    CopyJob& job = copy_job();
    job.stop.store(false);
    job.paused = false;
    job.changed = true;
    job.started = std::chrono::steady_clock::now();
    job.bundle_ids.clear();
    job.view = {};
    job.view.from = path_to_utf8(chosen->path.filename());
    job.view.running = true;
    job.view.whole = 1000;
    const bool fixture = chosen->kind == SourceKind::fixture;
    job.needs_check = false;
    job.pending_check = {};
    job.copied_bundle_ids.clear();
    // A copy that does not finish must not leave an earlier result for the
    // start-up check. A finished copy replaces this before the menu opens.
    keep_this_run_copy_check({});
    job.bytes_done = 0;
    job.bytes_total = 0;
    job.files_done = 0;
    job.files_total = 0;
    const std::span<const bundles::Bundle> table =
        fixture ? copy_check_bundles() : bundles::bundles();
    prime_bundles(job, table);
    const Remembered copy = *chosen;
    job.worker = std::thread([copy, user_folder] {
        CopyJob& held = copy_job();
        CopyResult result;
        try {
            const std::unique_ptr<ContentSource> source = open_source(copy);
            if (!source) {
                result.status = CopyStatus::failed;
            } else {
                CopyRequest request;
                request.user_folder = user_folder;
                request.bundles =
                    copy.kind == SourceKind::fixture ? copy_check_bundles() : bundles::bundles();
                request.stop = &held.stop;
                request.on_progress = on_copy_progress;
                request.progress_context = &held;
                request.news = &std::cerr;
                result = copy_content(*source, request);
            }
        } catch (const std::exception& error) {
            result.status = CopyStatus::failed;
            result.error = error.what();
        }
        const bool bytes_done = result.status == CopyStatus::finished;
        const bool run_check = bytes_done && !held.stop.load();
        const std::string error = result.error;
        if (!run_check) {
            const std::lock_guard<std::mutex> lock(held.mutex);
            held.view.running = false;
            held.view.paused = false;
            held.view.hashing = false;
            held.view.bytes_landed = bytes_done;
            if (!bytes_done && result.status != CopyStatus::stopped && !held.stop.load()) {
                held.view.checked = false;
                held.view.verdict =
                    error.empty() ? std::string("The game could not be copied.") : error;
            }
            held.changed = true;
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(held.mutex);
            held.view.bytes_landed = true;
            held.view.hashing = true;
            held.view.running = true;
            held.view.check_done = 0;
            held.view.check_total = 0;
            held.changed = true;
        }
        ContentCheck check;
        std::string failure;
        bool threw = false;
        try {
            const std::span<const bundles::Bundle> checked =
                copy.kind == SourceKind::fixture ? copy_check_bundles() : bundles::bundles();
            check = check_managed_content(user_folder, checked, on_copy_check, &held, &held.stop);
        } catch (const std::exception& caught) {
            threw = true;
            failure = caught.what();
        }
        const bool stopped = held.stop.load();
        const std::lock_guard<std::mutex> lock(held.mutex);
        held.view.hashing = false;
        held.view.running = false;
        held.view.paused = false;
        if (threw) {
            held.view.checked = false;
            held.view.verdict = std::move(failure);
        } else if (!stopped) {
            ContentCheck for_start;
            for (const ContentBundleCheck& bundle : check.bundles) {
                bool copied = false;
                for (const std::string& id : held.copied_bundle_ids) {
                    if (id == bundle.id)
                        copied = true;
                }
                if (copied)
                    for_start.bundles.push_back(bundle);
            }
            // Only bundles this copy reported. One already in the folder, and
            // not part of this copy, is checked again when the menu opens.
            keep_this_run_copy_check(std::move(for_start));
            held.pending_check = std::move(check);
            held.needs_check = true;
        }
        held.changed = true;
    });
    return true;
}

void setup_set_paused(bool paused) {
    CopyJob& job = copy_job();
    {
        const std::lock_guard<std::mutex> lock(job.mutex);
        job.paused = paused;
        job.view.paused = paused;
        job.changed = true;
    }
    job.wake.notify_all();
}

void setup_request_cancel() {
    CopyJob& job = copy_job();
    job.stop.store(true);
    {
        const std::lock_guard<std::mutex> lock(job.mutex);
        job.paused = false;
        job.view.paused = false;
    }
    job.wake.notify_all();
}

bool setup_pull_copy(first_run::CopyView& view) {
    CopyJob& job = copy_job();
    ContentCheck pending;
    bool check_now = false;
    {
        const std::lock_guard<std::mutex> lock(job.mutex);
        check_now = job.needs_check;
        if (check_now) {
            pending = std::move(job.pending_check);
            job.pending_check = {};
            job.needs_check = false;
        }
    }
    // The copy's thread already hashed. This thread only writes the lines.
    if (check_now) {
        const std::lock_guard<std::mutex> lock(job.mutex);
        apply_check(job.view, pending, true);
        job.changed = true;
    }
    const std::lock_guard<std::mutex> lock(job.mutex);
    view = job.view;
    if (job.files_total > 0)
        view.overall = overall_line(job.files_done, job.files_total);
    if (view.running && !view.paused && !view.hashing)
        view.remaining = remaining_line(job, job.bytes_done, job.bytes_total);
    if (view.verdict == "The game could not be copied.")
        view.verdict = std::string(languages::interface_text(view.verdict));
    const bool changed = job.changed;
    job.changed = false;
    return changed;
}

void setup_stop_join() {
    CopyJob& job = copy_job();
    job.stop.store(true);
    {
        const std::lock_guard<std::mutex> lock(job.mutex);
        job.paused = false;
    }
    job.wake.notify_all();
    if (job.worker.joinable())
        job.worker.join();
}

bool setup_locate_source(SDL_Window* window, const fs::path& user_folder, first_run::Model& model) {
    PickWait wait;
    const GameFilesHooks& hooks = game_files_hooks();
    if (hooks.show_picker != nullptr) {
        hooks.show_picker(hooks.context, PickKind::game_folder, picker_done, &wait);
        if (!pump_until_picked(wait))
            return false;
    } else {
        SDL_ShowOpenFolderDialog(folder_done, &wait, window, nullptr, false);
        if (!pump_until_picked(wait))
            return false;
    }
    bool added = false;
    for (const std::string& path : wait.paths) {
        const fs::path folder = path_from_utf8(path);
        const std::size_t before = model.sources.size();
        std::error_code failure;
        if (fs::is_directory(folder, failure) && !failure)
            add_folder_source(
                model, folder, user_folder, {}, bundles::bundles(), SourceKind::folder
            );
        else
            add_installer_source(model, folder, user_folder);
        if (model.sources.size() != before) {
            model.source = static_cast<int32_t>(model.sources.size()) - 1;
            added = true;
        }
    }
    return added;
}

void setup_release_located() {
    release_picked_sources();
}

} // namespace oa::app
