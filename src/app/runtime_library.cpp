// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library on the OA layer: its feed from the content service, the
// installed packages and their updates its worker finds, and the download
// queue; the worker itself; the Library as a screen of the layer (named
// library), its input and the actions it asks for; and its opening, from the
// main menu's OA button, from Settings › Language and by the runtime.

#include "engine_settings_menu_host.hpp"
#include "library_host.hpp"
#include "oa_layer.hpp"

#include "oa/app/content/downloads.hpp"
#include "oa/app/content/pictures.hpp"
#include "oa/app/content/service.hpp"
#include "oa/app/content/settings.hpp"
#include "oa/app/content/updates.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/app/mod_summary.hpp"
#include "oa/app/package_install.hpp"
#include "oa/app/runtime.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/base/threads.hpp"
#include "oa/data/catalogue/catalogue.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/data/registry/trust.hpp"
#include "oa/formats/oamod/package_keys.hpp"
#include "oa/formats/url.hpp"
#include "oa/netgame/http/client.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/library/library.hpp"
#include "oa/ui/library/screen.hpp"
#include "oa/ui/library/text.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace kit = oa::ui::kit;
namespace library = oa::ui::library;

namespace {

/// The sound the Library plays as it opens: the OA button's.
constexpr std::string_view kOpenSound = "Options";
/// The sound CLOSE plays: the settings' Cancel.
constexpr std::string_view kCloseSound = "Previous";
/// The settings' screen's name on the OA layer.
constexpr std::string_view kSettingsScreen = "settings";
/// The words the Library's rows' automation names start with.
constexpr std::string_view kRowNamePrefix = "library.row.";
/// The modifier that holds the Library's Find: Cmd on macOS, Ctrl elsewhere.
#ifdef SDL_PLATFORM_MACOS
constexpr uint16_t kCommandModifier = SDL_KMOD_GUI;
#else
constexpr uint16_t kCommandModifier = SDL_KMOD_CTRL;
#endif

/// Returns one of the Library's words in the language shown.
///
/// @param english the word, in English as the Library's text.hpp writes it
/// @return its translation, or the English
std::string_view shown_word(void* /*context*/, std::string_view english) {
    return oa::data::languages::interface_text(english);
}

/// Returns the Library's kind for a catalogue's kind.
///
/// @param kind the catalogue's kind
/// @return the Library's
library::Kind kind_of(data::catalogue::Kind kind) noexcept {
    switch (kind) {
    case data::catalogue::Kind::oamod:
        return library::Kind::mod;
    case data::catalogue::Kind::oamap:
        return library::Kind::map_pack;
    case data::catalogue::Kind::oalang:
        return library::Kind::language;
    }
    return library::Kind::mod;
}

/// Returns the Library's kind for one of the installer's kinds.
///
/// @param kind the installer's kind; may be null
/// @return the Library's; nothing for a kind the Library does not list
std::optional<library::Kind> kind_of(const package_install::PackageKind* kind) {
    if (kind == nullptr)
        return std::nullopt;
    if (kind->name == "oamod")
        return library::Kind::mod;
    if (kind->name == "oamap")
        return library::Kind::map_pack;
    if (kind->name == "oalang")
        return library::Kind::language;
    return std::nullopt;
}

/// Returns where a queued download stands, as the Library's queue line
/// says it.
///
/// @param state the queue's state
/// @return the phase; nothing for a cancelled item, which the Library leaves out
std::optional<library::QueueItem::Phase> phase_of(content::DownloadState state) noexcept {
    using Phase = library::QueueItem::Phase;
    switch (state) {
    case content::DownloadState::waiting:
    case content::DownloadState::asking:
    case content::DownloadState::challenge:
    case content::DownloadState::paused_for_match:
        return Phase::waiting;
    case content::DownloadState::downloading:
        return Phase::downloading;
    case content::DownloadState::verifying:
        return Phase::verifying;
    case content::DownloadState::handing_over:
    case content::DownloadState::installing:
        return Phase::installing;
    case content::DownloadState::failed:
        return Phase::failed;
    case content::DownloadState::done:
        return Phase::installed;
    case content::DownloadState::cancelled:
        return std::nullopt;
    }
    return std::nullopt;
}

/// Tells whether a queued download has ended: installed, failed or cancelled.
///
/// @param state the queue's state
/// @return true once it has
bool ended(content::DownloadState state) noexcept {
    return state == content::DownloadState::done || state == content::DownloadState::failed ||
           state == content::DownloadState::cancelled;
}

/// Returns whether an update changes the rules, as the Library says it.
///
/// @param change what the updates logic found
/// @return the Library's
library::UpdateNote::Rules rules_of(content::RulesChange change) noexcept {
    switch (change) {
    case content::RulesChange::same:
        return library::UpdateNote::Rules::same;
    case content::RulesChange::changes:
        return library::UpdateNote::Rules::changes;
    case content::RulesChange::unknown:
        return library::UpdateNote::Rules::unknown;
    }
    return library::UpdateNote::Rules::unknown;
}

/// Returns why an update cannot be installed, as the Library says it.
///
/// @param blocked what the updates logic found
/// @return the Library's
library::UpdateNote::Blocked blocked_of(content::Blocked blocked) noexcept {
    switch (blocked) {
    case content::Blocked::none:
        return library::UpdateNote::Blocked::none;
    case content::Blocked::engine:
        return library::UpdateNote::Blocked::engine;
    case content::Blocked::hacks:
        return library::UpdateNote::Blocked::hacks;
    case content::Blocked::base:
        return library::UpdateNote::Blocked::base;
    }
    return library::UpdateNote::Blocked::none;
}

/// Returns a badge the Library draws from a decoded picture.
///
/// @param picture the decoded picture
/// @return the badge; nothing for an empty picture
std::optional<library::Badge> badge_of(const ModBadge& picture) {
    if (picture.width == 0 || picture.height == 0 || picture.pixels.empty())
        return std::nullopt;
    return library::Badge{picture.pixels, picture.width, picture.height};
}

/// Returns the seconds since 1970 by the system's clock.
///
/// @return the seconds
int64_t seconds_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    )
        .count();
}

/// Returns a folder as the settings keep it: absolute, normal, in UTF-8.
///
/// @param folder the folder
/// @return its path
std::string kept_folder(const fs::path& folder) {
    std::error_code error;
    const fs::path absolute = fs::absolute(folder, error);
    return path_to_utf8((error ? folder : absolute).lexically_normal());
}

/// Returns the registry of a snapshot that has an id: the built-in one,
/// never an added registry that conflicts with it.
///
/// @param snapshot the content service's snapshot
/// @param registry the registry's id
/// @return the registry; null when the snapshot has none of that id
const content::RegistryView*
registry_view(const content::Snapshot& snapshot, std::string_view registry) {
    for (const content::RegistryView& view : snapshot.registries)
        if (!view.registry.conflicting && view.registry.descriptor.id == registry)
            return &view;
    return nullptr;
}

/// Searches for the installed packages and their updates on the worker:
/// lists the player's folder, matches each package to the catalogues and
/// adopts each file install a catalogue lists, finds the updates, and reads
/// each installed mod's own picture.
///
/// @param work the worker's state, whose results it fills under its lock
/// @param snapshot the catalogues in effect
/// @param folder the player's folder
void search_updates(
    LibraryWork& work, const content::Snapshot& snapshot, const std::filesystem::path& folder
) {
    std::vector<content::InstalledPackage> installed;
    std::vector<content::Match> matches;
    std::vector<content::Update> updates;
    std::vector<ModBadge> pictures;
    try {
        if (!folder.empty())
            installed = content::list_installed(folder);
        for (const content::InstalledPackage& package : installed) {
            content::Match match = content::match_installed(package, snapshot);
            if (match.provenance == content::Provenance::matched) {
                std::string error;
                if (!content::adopt_match(package, match, &error) && !error.empty())
                    std::cerr << "open-annihilation: library: " << error << '\n';
            }
            matches.push_back(std::move(match));
            pictures.push_back(
                kind_of(package.kind) == library::Kind::mod ? read_mod_summary(package.folder).badge
                                                            : ModBadge{}
            );
        }
        updates = content::find_updates(installed, snapshot, content::this_engine_rules());
    } catch (const std::exception& error) {
        std::cerr << "open-annihilation: library: the search for updates failed: " << error.what()
                  << '\n';
    }
    base::threads::LockGuard guard(work.mutex);
    work.installed = std::move(installed);
    work.matches = std::move(matches);
    work.updates = std::move(updates);
    work.installed_badges = std::move(pictures);
    ++work.searches;
}

/// The Library's worker: waits for a search or for pictures, and does each,
/// until the host stops it. Its HTTP client is its own, and carries no
/// install ID.
///
/// @param argument the worker's state (LibraryWork)
void library_worker(void* argument) {
    auto& work = *static_cast<LibraryWork*>(argument);
    netgame::http::Client client;
    for (;;) {
        bool search = false;
        bool pictures = false;
        bool match = false;
        std::shared_ptr<const content::Snapshot> snapshot;
        std::filesystem::path folder;
        {
            base::threads::LockGuard guard(work.mutex);
            while (!work.stop && !work.updates_due && !(work.pictures_due && !work.match_running))
                work.wake.wait(work.mutex);
            if (work.stop)
                return;
            search = work.updates_due;
            work.updates_due = false;
            snapshot = work.snapshot;
            folder = work.user_folder;
            match = work.match_running;
            pictures = work.pictures_due && !match;
            if (pictures)
                work.pictures_due = false;
        }
        if (search && snapshot)
            search_updates(work, *snapshot, folder);
        if (pictures && work.pictures) {
            try {
                work.pictures->fetch_waiting(client, match);
            } catch (const std::exception& error) {
                std::cerr << "open-annihilation: library: a picture could not be read: "
                          << error.what() << '\n';
            }
        }
    }
}

} // namespace

library::Tab library_tab_of(settings::LibraryTab tab) noexcept {
    switch (tab) {
    case settings::LibraryTab::mods:
        return library::Tab::mods;
    case settings::LibraryTab::maps:
        return library::Tab::maps;
    case settings::LibraryTab::languages:
        return library::Tab::languages;
    case settings::LibraryTab::updates:
        return library::Tab::updates;
    }
    return library::Tab::mods;
}

// ---------------------------------------------------------------------------------------------
// The host and its worker

Runtime::LibraryHost::~LibraryHost() {
    bool join = false;
    {
        base::threads::LockGuard guard(work.mutex);
        work.stop = true;
        join = work.started;
    }
    work.cancel.store(true);
    work.wake.notify_all();
    if (join)
        base::threads::join_thread(work.thread);
}

bool Runtime::LibraryHost::ensure_worker() {
    base::threads::LockGuard guard(work.mutex);
    if (work.started)
        return true;
    if (!base::threads::start_thread(work.thread, library_worker, &work)) {
        std::cerr << "open-annihilation: library: the worker could not start\n";
        return false;
    }
    work.started = true;
    return true;
}

void Runtime::LibraryHost::look_again(Runtime& runtime) {
    if (!own_feed)
        return;
    std::shared_ptr<const content::Snapshot> snapshot = runtime.content_service().snapshot();
    {
        base::threads::LockGuard guard(work.mutex);
        work.snapshot = std::move(snapshot);
        work.user_folder = runtime.user_folder_;
        work.updates_due = true;
    }
    if (ensure_worker())
        work.wake.notify_all();
}

void Runtime::LibraryHost::rebuild(Runtime& /*runtime*/) {
    library::Inputs inputs = feed.inputs != nullptr ? feed.inputs(feed.context) : library::Inputs{};
    // An update queued from the Library keeps saying what it changes while
    // the queue lists it.
    std::vector<KeptUpdate> still;
    for (const KeptUpdate& kept : kept_updates) {
        bool listed = false;
        for (library::QueueItem& item : inputs.queue) {
            if (item.registry != kept.registry || item.kind != kept.kind || item.key != kept.key)
                continue;
            listed = true;
            if (item.update_from.empty() && item.update_to.empty()) {
                item.update_rules = kept.rules;
                item.update_from = kept.from;
                item.update_to = kept.to;
            }
        }
        if (listed)
            still.push_back(kept);
    }
    kept_updates = std::move(still);
    library::refresh(model, std::move(inputs));
    update_count = library::update_count(model);
    refreshed = true;
    ++model_revision;
}

void Runtime::LibraryHost::request_pictures(const kit::DisplayList& list) {
    if (!own_feed || !work.pictures)
        return;
    {
        base::threads::LockGuard guard(work.mutex);
        if (work.match_running)
            return;
    }
    // A picture already asked for, ready or refused is not asked for
    // again; what waits is forgotten as the Library closes.
    const auto ask = [this](const library::Entry& entry) {
        const auto url = picture_urls.find({entry.id.registry, entry.id.key});
        if (url == picture_urls.end() || badges.count(url->second) != 0)
            return;
        work.pictures->request(entry.id.registry, url->second);
    };
    for (const kit::Control& control : list.controls) {
        if (control.kind != kit::ControlKind::list_item ||
            control.name.compare(0, kRowNamePrefix.size(), kRowNamePrefix) != 0)
            continue;
        for (const std::size_t index : model.visible) {
            const library::Entry& entry = model.entries[index];
            if (library::entry_name(entry) == control.text)
                ask(entry);
        }
    }
    if (const library::Entry* selected = library::selected_entry(model))
        ask(*selected);
    if (!work.pictures->waiting())
        return;
    {
        base::threads::LockGuard guard(work.mutex);
        work.pictures_due = true;
    }
    if (ensure_worker())
        work.wake.notify_all();
}

void Runtime::destroy_library_host(LibraryHost* host) noexcept {
    delete host;
}

Runtime::LibraryHost& Runtime::library_host() {
    if (!library_) {
        library_.reset(new LibraryHost{});
        LibraryHost& host = *library_;
        host.feed = LibraryFeed{
            this,
            LibraryHost::own_inputs,
            LibraryHost::own_generation,
            LibraryHost::own_enqueue,
            LibraryHost::own_cancel,
            LibraryHost::own_retry,
            LibraryHost::own_built_in
        };
        host.model.text = library::TextHooks{nullptr, shown_word};
        // The pictures are kept beside the cached catalogues.
        const std::filesystem::path content_folder = content_service().content_folder();
        std::filesystem::path pictures_folder;
        if (!content_folder.empty())
            pictures_folder = content_folder / std::string(content::pictures_folder_name);
        host.work.pictures =
            std::make_unique<content::PictureCache>(pictures_folder, &host.work.cancel);
    }
    return *library_;
}

// ---------------------------------------------------------------------------------------------
// The runtime's own feed

library::Inputs Runtime::LibraryHost::own_inputs(void* context) {
    return static_cast<Runtime*>(context)->library_inputs();
}

uint64_t Runtime::LibraryHost::own_generation(void* context) {
    Runtime& runtime = *static_cast<Runtime*>(context);
    return runtime.library_feed_generation();
}

std::optional<uint64_t> Runtime::LibraryHost::own_enqueue(
    void* context,
    std::string_view registry,
    std::string_view key,
    content::DownloadReason reason,
    std::string* why
) {
    return static_cast<Runtime*>(context)->queue_download(registry, key, reason, why);
}

void Runtime::LibraryHost::own_cancel(
    void* context, std::string_view registry, std::string_view key
) {
    content::Downloads& downloads = static_cast<Runtime*>(context)->content_downloads();
    std::optional<uint64_t> item;
    for (const content::DownloadView& queued : downloads.view())
        if (queued.target.registry == registry && queued.target.key == key && !ended(queued.state))
            item = queued.item;
    if (item)
        downloads.cancel(*item);
}

void Runtime::LibraryHost::own_retry(
    void* context, std::string_view registry, std::string_view key
) {
    content::Downloads& downloads = static_cast<Runtime*>(context)->content_downloads();
    std::optional<uint64_t> item;
    for (const content::DownloadView& queued : downloads.view())
        if (queued.target.registry == registry && queued.target.key == key &&
            (queued.state == content::DownloadState::failed ||
             queued.state == content::DownloadState::cancelled))
            item = queued.item;
    if (item)
        downloads.retry(*item);
}

bool Runtime::LibraryHost::own_built_in(void* context, std::string_view registry) {
    const std::shared_ptr<const content::Snapshot> snapshot =
        static_cast<Runtime*>(context)->content_service().snapshot();
    if (!snapshot)
        return false;
    const content::RegistryView* view = registry_view(*snapshot, registry);
    return view != nullptr && view->registry.origin == data::registry::Origin::built_in;
}

uint64_t Runtime::library_feed_generation() {
    LibraryHost& host = library_host();
    uint64_t searches = 0;
    {
        base::threads::LockGuard guard(host.work.mutex);
        searches = host.work.searches;
    }
    const uint64_t pictures = host.work.pictures ? host.work.pictures->generation() : 0;
    return content_service().generation() + content_downloads().generation() + searches + pictures;
}

library::Inputs Runtime::library_inputs() {
    LibraryHost& host = library_host();
    library::Inputs inputs;
    const std::shared_ptr<const content::Snapshot> snapshot = content_service().snapshot();
    const int64_t now = seconds_now();
    std::optional<int64_t> newest;
    if (snapshot) {
        for (const content::RegistryView& view : snapshot->registries) {
            if (view.registry.conflicting)
                continue;
            const data::registry::Descriptor& descriptor = view.registry.descriptor;
            library::Registry registry;
            registry.id = descriptor.id;
            registry.name = descriptor.name;
            registry.built_in = view.registry.origin == data::registry::Origin::built_in;
            registry.downloads_on =
                descriptor.install_id != data::registry::InstallIdUse::required ||
                content::read_install_id(preference_values_, descriptor.id).state !=
                    content::InstallIdState::off;
            inputs.registries.push_back(std::move(registry));
            if (!view.registry.enabled)
                continue;
            if (view.checked && (!newest || *view.checked > *newest))
                newest = view.checked;
            if (!view.last_error.empty() && !view.refreshing)
                inputs.offline = true;
        }
        host.picture_urls.clear();
        for (const content::Entry& entry : snapshot->entries) {
            if (entry.package == nullptr)
                continue;
            const data::catalogue::Package& package = *entry.package;
            library::Listing listing;
            listing.registry = entry.registry;
            listing.kind = kind_of(package.kind);
            listing.key = package.id;
            listing.name = package.name;
            listing.version = package.version;
            listing.revision = package.revision;
            listing.release = package.release;
            listing.publisher = package.publisher;
            listing.author = package.author;
            listing.summary = package.summary;
            listing.homepage = package.homepage;
            listing.tags = package.tags;
            listing.requires_engine = package.requires_engine_text;
            listing.base = package.requires_base;
            listing.sim_hash = package.sim_hash;
            listing.game_hacks =
                static_cast<uint32_t>(std::max(package.hacks_sim.value_or(0), int32_t{0}));
            listing.view_hacks =
                static_cast<uint32_t>(std::max(package.hacks_view.value_or(0), int32_t{0}));
            listing.hack_ids = package.hack_ids;
            listing.size = package.size;
            listing.coverage = package.coverage;
            for (const data::catalogue::MapEntry& map : package.maps)
                listing.maps.push_back(
                    {map.name,
                     map.players,
                     std::to_string(map.width) + "x" + std::to_string(map.height)}
                );
            // The badge, resolved against the host that served the
            // catalogue: shown once the worker has read and checked it.
            const content::RegistryView* view = registry_view(*snapshot, entry.registry);
            if (!package.badge.empty() && view != nullptr && view->served_from) {
                if (const std::optional<formats::url::Url> resolved =
                        formats::url::resolve_reference(*view->served_from, package.badge)) {
                    const std::string url = formats::url::url_text(*resolved);
                    host.picture_urls[{entry.registry, package.id}] = url;
                    auto held = host.badges.find(url);
                    if (held == host.badges.end() && host.work.pictures)
                        if (std::optional<ModBadge> taken =
                                host.work.pictures->take(entry.registry, url))
                            if (std::optional<library::Badge> badge = badge_of(*taken))
                                held = host.badges.emplace(url, std::move(*badge)).first;
                    if (held != host.badges.end())
                        listing.badge = held->second;
                }
            }
            inputs.listings.push_back(std::move(listing));
        }
    }
    inputs.list_age_seconds = newest ? std::max(now - *newest, int64_t{0}) : int64_t{-1};

    // What the worker last found installed, and the updates for it.
    std::vector<content::InstalledPackage> installed;
    std::vector<content::Match> matches;
    std::vector<content::Update> updates;
    std::vector<ModBadge> pictures;
    {
        base::threads::LockGuard guard(host.work.mutex);
        installed = host.work.installed;
        matches = host.work.matches;
        updates = host.work.updates;
        pictures = host.work.installed_badges;
    }
    for (std::size_t index = 0; index < installed.size() && index < matches.size(); ++index) {
        const content::InstalledPackage& package = installed[index];
        const content::Match& match = matches[index];
        const std::optional<library::Kind> kind = kind_of(package.kind);
        if (!kind)
            continue;
        library::Installed shown;
        shown.kind = *kind;
        const bool listed = match.provenance != content::Provenance::own;
        shown.key = listed ? match.key : package.key;
        shown.folder = package.folder;
        shown.name = package.name;
        shown.version = package.version;
        shown.revision = package.revision;
        shown.registry = listed ? match.registry : std::string{};
        shown.release = listed ? match.release : 0;
        shown.rules = package.rules;
        shown.playing = *kind == library::Kind::mod && package.kind != nullptr &&
                        package_target_in_use(*package.kind, package.folder);
        if (package.has_backup)
            shown.kept = library::Kept{package.backup_version, package.backup_revision};
        if (index < pictures.size())
            shown.badge = badge_of(pictures[index]);
        inputs.installed.push_back(std::move(shown));
    }
    for (const content::Update& update : updates) {
        if (update.installed >= installed.size())
            continue;
        const std::optional<library::Kind> kind = kind_of(installed[update.installed].kind);
        if (!kind)
            continue;
        library::UpdateNote note;
        note.registry = update.registry;
        note.kind = *kind;
        note.key = update.key;
        note.to_release = update.to_release;
        note.to_version = update.to_version;
        note.to_revision = update.to_revision;
        note.size = update.size;
        note.from_rules = update.from_rules;
        note.to_rules = update.to_rules;
        note.rules = rules_of(update.rules);
        note.blocked = blocked_of(update.blocked);
        note.blocked_detail = update.blocked_detail;
        inputs.updates.push_back(std::move(note));
    }

    // The download queue, in the order the items were queued.
    for (const content::DownloadView& queued : content_downloads().view()) {
        const std::optional<library::QueueItem::Phase> phase = phase_of(queued.state);
        if (!phase)
            continue;
        library::QueueItem item;
        item.registry = queued.target.registry;
        item.kind = kind_of(queued.target.kind);
        item.key = queued.target.key;
        item.name = queued.target.name;
        if (snapshot)
            for (const content::Entry& entry : snapshot->entries)
                if (entry.package != nullptr && entry.registry == item.registry &&
                    entry.package->id == item.key) {
                    item.version = entry.package->version;
                    break;
                }
        item.phase = *phase;
        item.done_bytes = queued.done;
        item.total_bytes = queued.target.size;
        if (*phase == library::QueueItem::Phase::failed) {
            item.problem = content::failure_text(queued.failure);
            if (!queued.detail.empty())
                item.problem += " (" + queued.detail + ")";
            if (!item.problem.empty() && item.problem[0] >= 'a' && item.problem[0] <= 'z')
                item.problem[0] = static_cast<char>(item.problem[0] - 'a' + 'A');
            item.retryable = queued.failure != content::DownloadFailure::install_id_off &&
                             queued.failure != content::DownloadFailure::not_offered;
        }
        inputs.queue.push_back(std::move(item));
    }

    inputs.engine = content::this_engine_rules().engine;
    inputs.version_text = "v" + formats::oamod::engine_version_text(inputs.engine);
    inputs.in_match = screen_ == Screen::match || match_ != nullptr;
    return inputs;
}

// ---------------------------------------------------------------------------------------------
// The Library as a screen of the layer

/// The Library as a screen of the OA layer: modal, over a backdrop, centred
/// in the window at its size class's size on the main menu alone. Its model
/// and state are the host's; it lays them out at the frame the layer gives
/// it and hands what its input asks for to the runtime.
struct Runtime::LibraryScreen final : LayerScreen {
    /// Makes the screen.
    ///
    /// @param runtime the runtime whose Library it shows
    explicit LibraryScreen(Runtime& runtime) noexcept : runtime_(runtime) {}

    /// Returns "library".
    ///
    /// @return the name
    [[nodiscard]] std::string_view name() const override { return library_screen_name; }

    /// Returns where the Library shows: centred in the window at the view's
    /// scale, its size class's window size (library::window_size).
    ///
    /// @param view what the screen is placed on
    /// @return its place; empty in a match or off the main menu
    [[nodiscard]] LayerPlacement placement(const LayerView& view) const override {
        if (view.match || runtime_.screen_ != Screen::main_menu)
            return {};
        const kit::Point size = library::window_size(view.frame);
        return centred_placement(view, size.x, size.y);
    }

    /// Tells that the Library takes every input.
    ///
    /// @return true
    [[nodiscard]] bool modal() const override { return true; }

    /// Tells that what lies under the Library darkens.
    ///
    /// @return true
    [[nodiscard]] bool backdrop() const override { return true; }

    /// Lays the Library out again when the view's frame changed.
    ///
    /// @param view what the screen is placed on
    void lay_out(const LayerView& view) override {
        if (laid_out_ && view.frame.area.width == frame_.area.width &&
            view.frame.area.height == frame_.area.height &&
            view.frame.size_class == frame_.size_class &&
            view.frame.scale_percent == frame_.scale_percent)
            return;
        frame_ = view.frame;
        laid_out_ = true;
        relayout();
    }

    /// Draws the Library at the canvas's scale.
    ///
    /// @param canvas where it draws
    void draw(const kit::Canvas& canvas) const override { library::draw_library(canvas, list_); }

    /// Takes a pointer's move, press or release.
    ///
    /// @param kind pointer_move, pointer_down or pointer_up
    /// @param button the pointer's button
    /// @param at the pointer, in the Library's points
    /// @param finger_reach a finger's reach in the Library's points; 0 for a mouse
    /// @return what the Library did
    LayerAnswer
    pointer(ScreenInputKind kind, uint8_t button, kit::Point at, int32_t finger_reach) override {
        library::PointerKind moved = library::PointerKind::move;
        if (kind == ScreenInputKind::pointer_down)
            moved = library::PointerKind::down;
        else if (kind == ScreenInputKind::pointer_up)
            moved = library::PointerKind::up;
        else if (kind != ScreenInputKind::pointer_move)
            return LayerAnswer::none;
        LibraryHost& host = runtime_.library_host();
        return take(
            library::library_pointer(host.model, host.state, list_, moved, button, at, finger_reach)
        );
    }

    /// Takes a key, as the kit names it.
    ///
    /// @param pressed the key
    /// @return what the Library did
    LayerAnswer key(kit::Key pressed, uint32_t /*sdl_key*/) override {
        LibraryHost& host = runtime_.library_host();
        return take(library::library_key(host.model, host.state, list_, pressed));
    }

    /// Takes the Library's own keys before the kit's: Cmd+F on macOS and
    /// Ctrl+F elsewhere, and `/` outside the search, find; Ctrl+Tab and
    /// Ctrl+Shift+Tab move between the tabs; the settings' shortcut opens
    /// Settings over the Library, as SETTINGS… does.
    ///
    /// @param sdl_key the key's SDL keycode
    /// @param modifiers SDL_Keymod bits
    /// @return what the Library did; pass for every other key
    LayerAnswer shortcut(uint32_t sdl_key, uint16_t modifiers) override {
        if (Runtime::engine_settings_shortcut(sdl_key, modifiers))
            return take({library::Action::settings, {}, true});
        LibraryHost& host = runtime_.library_host();
        std::optional<library::ScreenCommand> command;
        if (sdl_key == SDLK_F && (modifiers & kCommandModifier) != 0)
            command = library::ScreenCommand::find;
        else if (sdl_key == SDLK_TAB && (modifiers & SDL_KMOD_CTRL) != 0)
            command = (modifiers & SDL_KMOD_SHIFT) != 0 ? library::ScreenCommand::previous_tab
                                                        : library::ScreenCommand::next_tab;
        else if (
            sdl_key == SDLK_SLASH && (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) == 0 &&
            !library::focused_field(host.state, list_)
        )
            command = library::ScreenCommand::find;
        if (!command)
            return LayerAnswer::pass;
        return take(library::library_command(host.model, host.state, list_, *command));
    }

    /// Takes the controller's buttons that are the Library's own: Y focuses
    /// the search, RB and LB change tabs, B goes back and X does the selected
    /// entry's first action. Every other action is left to become a key, as
    /// on any other screen.
    ///
    /// @param action the gamepad action
    /// @return what the Library did; pass for an action it does not take
    LayerAnswer pad_action(oa::ui::pad_controls::Action action) override {
        std::optional<library::ScreenCommand> command;
        switch (action) {
        case oa::ui::pad_controls::Action::library_search:
            command = library::ScreenCommand::find;
            break;
        case oa::ui::pad_controls::Action::focus_next:
            command = library::ScreenCommand::next_tab;
            break;
        case oa::ui::pad_controls::Action::focus_previous:
            command = library::ScreenCommand::previous_tab;
            break;
        case oa::ui::pad_controls::Action::back:
            command = library::ScreenCommand::back;
            break;
        case oa::ui::pad_controls::Action::library_action:
            command = library::ScreenCommand::first_action;
            break;
        default:
            return LayerAnswer::pass;
        }
        LibraryHost& host = runtime_.library_host();
        return take(library::library_command(host.model, host.state, list_, *command));
    }

    /// Takes the controller's right stick: it scrolls the pane that has the
    /// focus, in notches as the wheel.
    ///
    /// @param notches the stick's turn; positive towards the top
    /// @return what the Library did
    LayerAnswer pad_stick(float notches) override {
        LibraryHost& host = runtime_.library_host();
        return take(library::library_stick(host.model, host.state, list_, notches));
    }

    /// Takes a turn of the wheel, which scrolls the pane under the pointer.
    ///
    /// @param at the pointer, in the Library's points
    /// @param notches how far the wheel turned
    /// @return what the Library did
    LayerAnswer wheel(kit::Point at, float notches) override {
        LibraryHost& host = runtime_.library_host();
        return take(library::library_wheel(host.model, host.state, list_, at, notches));
    }

    /// Takes text typed into the search.
    ///
    /// @param utf8 the text
    /// @return what the Library did
    LayerAnswer text(std::string_view utf8) override {
        LibraryHost& host = runtime_.library_host();
        return take(library::library_text(host.model, host.state, list_, utf8));
    }

    /// Returns the search field while it has the focus.
    ///
    /// @return the field's rectangle, in the Library's points; none otherwise
    [[nodiscard]] std::optional<kit::Rect> text_field() const override {
        if (!runtime_.library_)
            return std::nullopt;
        return library::focused_field(runtime_.library_->state, list_);
    }

    /// Returns what the Library draws and where its controls are.
    ///
    /// @return the display list
    [[nodiscard]] kit::DisplayList display_list() const override { return list_; }

    /// Returns the Library's hover, press and focus.
    ///
    /// @return the interaction
    [[nodiscard]] kit::Interaction interaction() const override {
        if (!runtime_.library_)
            return {};
        return runtime_.library_->state.interaction;
    }

    /// Follows the model as the host rebuilds it, and closes off the main menu.
    ///
    /// @return close off the main menu; redraw when the model moved; otherwise none
    LayerAnswer tick() override {
        if (runtime_.screen_ != Screen::main_menu)
            return LayerAnswer::close;
        if (!runtime_.library_ || runtime_.library_->model_revision == seen_model_)
            return LayerAnswer::none;
        relayout();
        return LayerAnswer::redraw;
    }

    /// Returns the Library's revision, which counts its layouts.
    ///
    /// @return the revision
    [[nodiscard]] uint64_t revision() const override { return revision_; }

    /// Forgets the pictures the Library waits for and any drag or open menu.
    void close(bool /*by_key*/) override {
        if (!runtime_.library_)
            return;
        LibraryHost& host = *runtime_.library_;
        host.state.drag = {};
        host.state.menu = library::MenuKind::none;
        if (host.work.pictures)
            host.work.pictures->drop_all();
    }

  private:

    /// Lays the Library out from the host's model and state, and asks for the
    /// pictures of the rows it shows.
    void relayout() {
        ++revision_;
        const kit::Fonts* fonts = runtime_.oa_layer().screen_fonts();
        if (fonts == nullptr || !runtime_.library_) {
            list_ = {};
            return;
        }
        LibraryHost& host = *runtime_.library_;
        seen_model_ = host.model_revision;
        list_ = library::library_layout(host.model, host.state, frame_, *fonts);
        host.request_pictures(list_);
    }

    /// Carries out what an input asked for, and lays the Library out again
    /// when what it shows changed.
    ///
    /// @param result what the input asked for
    /// @return close when the Library closes; redraw when it changed; otherwise none
    LayerAnswer take(const library::ScreenResult& result) {
        bool closes = false;
        if (result.action)
            closes = act(*result.action, result.entry);
        if (closes)
            return LayerAnswer::close;
        if (!result.redraw && !result.action)
            return LayerAnswer::none;
        relayout();
        return LayerAnswer::redraw;
    }

    /// Queues one entry's GET or UPDATE through the feed. An update keeps
    /// what it changes for the queue line (KeptUpdate).
    ///
    /// @param host the host
    /// @param entry the entry
    /// @param reason install or update
    void enqueue(LibraryHost& host, const library::Entry& entry, content::DownloadReason reason) {
        if (host.feed.enqueue == nullptr || entry.id.registry.empty())
            return;
        std::string why;
        const std::optional<uint64_t> item =
            host.feed.enqueue(host.feed.context, entry.id.registry, entry.id.key, reason, &why);
        if (!item) {
            if (!why.empty()) {
                runtime_.status_ = why;
                std::cerr << "open-annihilation: library: " << why << '\n';
            }
            return;
        }
        if (reason != content::DownloadReason::update || entry.update == nullptr ||
            entry.installed == nullptr)
            return;
        const library::ChangeTexts change =
            library::change_texts(*entry.installed, *entry.update, host.model.text);
        host.kept_updates.push_back(
            {entry.id.registry,
             entry.id.kind,
             entry.id.key,
             entry.update->rules,
             change.from,
             change.to}
        );
    }

    /// Opens Settings over the Library, or, where Settings is under it, goes
    /// back to Settings: the layer never holds two of either.
    void open_settings() {
        OaLayer& layer = runtime_.oa_layer();
        if (layer.find(kSettingsScreen) != nullptr) {
            layer.return_to(kSettingsScreen);
            return;
        }
        runtime_.open_engine_settings_from_menu();
    }

    /// Carries out one of the Library's actions.
    ///
    /// @param action what was pressed
    /// @param id the entry an entry's action is for
    /// @return true when the Library closes
    bool act(library::Action action, const library::EntryId& id) {
        LibraryHost& host = runtime_.library_host();
        const library::Entry* entry = library::find_entry(host.model, id);
        switch (action) {
        case library::Action::get:
            if (entry != nullptr)
                enqueue(host, *entry, content::DownloadReason::install);
            return false;
        case library::Action::update:
            if (entry != nullptr)
                enqueue(host, *entry, content::DownloadReason::update);
            return false;
        case library::Action::update_all: {
            // Every UPDATE that can act, in the order the list shows them.
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
                if (const library::Entry* found = library::find_entry(host.model, each))
                    enqueue(host, *found, content::DownloadReason::update);
            return false;
        }
        case library::Action::cancel:
            if (entry != nullptr && host.feed.cancel != nullptr)
                host.feed.cancel(host.feed.context, id.registry, id.key);
            return false;
        case library::Action::retry:
            if (entry != nullptr && host.feed.retry != nullptr)
                host.feed.retry(host.feed.context, id.registry, id.key);
            return false;
        case library::Action::roll_back:
        case library::Action::play_now:
            runtime_.library_entry_action(action, id);
            return false;
        case library::Action::open_folder:
            if (entry != nullptr && entry->installed != nullptr) {
                const FolderOpening opening = runtime_.open_player_folder(entry->installed->folder);
                if (!opening.opened)
                    runtime_.status_ = opening.reason;
            }
            return false;
        case library::Action::homepage:
            if (entry != nullptr && entry->listing != nullptr && !entry->listing->homepage.empty())
                runtime_.open_web_link(entry->listing->homepage);
            return false;
        case library::Action::settings:
            open_settings();
            return false;
        case library::Action::close:
            runtime_.play_ui_sound(kCloseSound, 0);
            return true;
        case library::Action::details:
        case library::Action::back:
            return false;
        }
        return false;
    }

    Runtime& runtime_;        ///< the runtime whose Library it shows
    kit::DisplayList list_{}; ///< what it draws and where its controls are
    kit::Frame frame_{};      ///< the frame it was laid out for
    bool laid_out_{};         ///< frame_ was set by lay_out
    uint64_t seen_model_{};   ///< the host's model revision the list was laid out from
    uint64_t revision_{};     ///< counts its layouts
};

// ---------------------------------------------------------------------------------------------
// Opening and closing the Library

void Runtime::open_library(std::optional<library::Tab> tab) {
    // Nothing opens over a message box or a frame a package owns, off the
    // main menu, or while the saves notice shows.
    if (screen_ != Screen::main_menu || frame_owned_by_package() ||
        oa::ui::frontend_dialogs::dialog_count() != 0 || saves_notice_shown() ||
        engine_settings_fonts() == nullptr)
        return;
    LibraryHost& host = library_host();
    // The catalogues are asked for again as the player's setting allows; a
    // run nobody watches fetches nothing.
    if (host.own_feed && !options_.unattended && !options_.headless_check)
        content_service().refresh(content::RefreshReason::library_opened);
    if (!host.refreshed) {
        host.feed_generation =
            host.feed.generation != nullptr ? host.feed.generation(host.feed.context) : 0;
        host.rebuild(*this);
    }
    const library::Tab shown =
        tab ? *tab : (host.update_count > 0 ? library::Tab::updates : library::Tab::mods);
    OaLayer& layer = oa_layer();
    if (layer.find(library_screen_name) != nullptr) {
        // The screens above it close, and it shows the tab asked for.
        layer.return_to(library_screen_name);
        host.state.details_page = false;
        library::close_details(host.model);
        library::set_tab(host.model, shown);
    } else {
        host.state = {};
        library::set_query(host.model, {});
        library::close_details(host.model);
        library::set_tab(host.model, shown);
        layer.push(std::make_unique<LibraryScreen>(*this));
    }
    ++host.model_revision;
    EngineSettingsMenuHost& button = engine_settings_menu_host();
    button.button_hovered = false;
    button.button_pressed = false;
    play_ui_sound(kOpenSound, 0);
}

void Runtime::close_library() {
    OaLayer& layer = oa_layer();
    const LayerScreen* shown = layer.find(library_screen_name);
    if (shown == nullptr)
        return;
    layer.return_to(library_screen_name);
    layer.close(shown);
}

std::size_t Runtime::library_update_count() {
    return library_ ? library_->update_count : 0;
}

bool Runtime::library_registry_built_in(std::string_view registry) {
    const LibraryHost& host = library_host();
    return host.feed.built_in != nullptr && host.feed.built_in(host.feed.context, registry);
}

void Runtime::library_entry_action(library::Action action, const library::EntryId& entry) {
    LibraryHost& host = library_host();
    const library::Entry* found = library::find_entry(host.model, entry);
    if (found == nullptr || found->installed == nullptr)
        return;
    const fs::path folder = found->installed->folder;
    if (action == library::Action::play_now) {
        std::string refusal;
        if (!switch_to_mod_folder(folder, refusal)) {
            status_ = refusal;
            std::cerr << "open-annihilation: library: PLAY NOW: " << refusal << '\n';
        }
        return;
    }
    if (action != library::Action::roll_back)
        return;
    // Settings over the Library on Mods, the Roll Back Mod question asked
    // for the folder; where Settings is under the Library, it comes back.
    // The Library may be taking the input that asked, so it never closes
    // anything but through return_to.
    OaLayer& layer = oa_layer();
    if (layer.find(kSettingsScreen) != nullptr)
        layer.return_to(kSettingsScreen);
    else
        open_engine_settings_from_menu();
    settings::Dialog* dialog = engine_settings_dialog();
    if (dialog == nullptr)
        return;
    dialog->page = settings::Page::mods;
    const std::string kept = kept_folder(folder);
    for (std::size_t index = 0; index < dialog->mod_folders.size(); ++index) {
        if (dialog->mod_folders[index] != kept)
            continue;
        dialog->switch_question = static_cast<int32_t>(index);
        dialog->mod_question = settings::ModQuestion::roll_back;
        dialog->question_marks_no = false;
        dialog->hovered = settings::no_control;
        dialog->pressed = settings::no_control;
        dialog->dragging = false;
        break;
    }
}

void Runtime::tick_library() {
    LibraryHost& host = library_host();
    const bool in_match = screen_ == Screen::match || match_ != nullptr;
    {
        base::threads::LockGuard guard(host.work.mutex);
        const bool was_running = host.work.match_running;
        host.work.match_running = in_match;
        // Pictures held for a match are fetched once it ends.
        if (was_running && !in_match && host.work.pictures && host.work.pictures->waiting()) {
            host.work.pictures_due = true;
            host.work.wake.notify_all();
        }
    }
    // The Library and its dot are the main menu's.
    if (screen_ != Screen::main_menu)
        return;
    if (host.own_feed) {
        // A catalogue that moved on, or a package the queue installed,
        // sends the worker to search for updates again.
        const uint64_t service = content_service().generation();
        if (service != host.service_generation) {
            host.service_generation = service;
            host.look_again(*this);
        }
        const uint64_t queue = content_downloads().generation();
        if (queue != host.queue_generation) {
            host.queue_generation = queue;
            bool installed = false;
            for (const content::DownloadView& queued : content_downloads().view()) {
                if (queued.state != content::DownloadState::done ||
                    std::find(
                        host.installed_items.begin(), host.installed_items.end(), queued.item
                    ) != host.installed_items.end())
                    continue;
                host.installed_items.push_back(queued.item);
                installed = true;
            }
            if (installed)
                host.look_again(*this);
        }
    }
    // The model follows its feed, at most once a frame.
    const uint64_t generation =
        host.feed.generation != nullptr ? host.feed.generation(host.feed.context) : 0;
    if (!host.refreshed || generation != host.feed_generation) {
        host.feed_generation = generation;
        host.rebuild(*this);
    }
}

} // namespace oa::app
