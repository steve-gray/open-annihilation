// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's host (Runtime::LibraryHost), which runtime_library.cpp
// drives: the Library's model and its screen's state, the feed its inputs
// come from (LibraryFeed), the pictures it has taken, the updates it keeps
// for the queue, and its worker (LibraryWork), which finds the installed
// packages and their updates and reads and fetches the badge pictures off
// the main thread. The Library itself is a screen of the OA layer
// (Runtime::LibraryScreen, named library).
#pragma once

#include "oa/app/content/downloads.hpp"
#include "oa/app/content/pictures.hpp"
#include "oa/app/content/service.hpp"
#include "oa/app/content/updates.hpp"
#include "oa/app/mod_summary.hpp"
#include "oa/app/runtime.hpp"
#include "oa/base/threads.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/library/library.hpp"
#include "oa/ui/library/screen.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {

/// The name the Library has on the OA layer.
inline constexpr std::string_view library_screen_name = "library";

/// Where the Library's inputs come from, and where its downloads go. The
/// runtime fills it from the content service, the Library's worker and the
/// download queue (queue_download); --check-library fills it with fixtures
/// and starts no download.
struct LibraryFeed {
    void* context{}; ///< passed back to each function
    /// Returns everything the Library is built from now. Null shows an empty
    /// Library.
    oa::ui::library::Inputs (*inputs)(void* context){};
    /// Returns a count that moves on whenever inputs would give something
    /// else. Null never moves: the Library is built once.
    uint64_t (*generation)(void* context){};
    /// Queues one package of a registry for download, GET's or UPDATE's.
    /// Null queues nothing.
    ///
    /// Returns the queue item, or nothing when it was refused, why saying
    /// why when it is not null.
    std::optional<uint64_t> (*enqueue)(
        void* context,
        std::string_view registry,
        std::string_view key,
        content::DownloadReason reason,
        std::string* why
    ){};
    /// Cancels the download of one package of a registry that has not ended.
    /// Null cancels nothing.
    void (*cancel)(void* context, std::string_view registry, std::string_view key){};
    /// Queues one package's failed download of a registry again. Null queues
    /// nothing.
    void (*retry)(void* context, std::string_view registry, std::string_view key){};
    /// Tells whether a registry is built in: the Registry::built_in inputs
    /// gives it. Null tells that none is.
    bool (*built_in)(void* context, std::string_view registry){};
};

/// The Library's worker: one thread of its own, started at its first job and
/// joined as the host goes, which finds the installed packages and the
/// updates the catalogues offer for them after a catalogue refresh or an
/// install, and reads and fetches the badge pictures asked for. What it
/// found is kept under its lock for the main thread to read.
struct LibraryWork {
    base::threads::Mutex mutex{};            ///< guards every member below but cancel and pictures
    base::threads::ConditionVariable wake{}; ///< signalled when a job is due or the worker stops
    base::threads::Thread thread{};          ///< the worker, once started
    bool started{};                          ///< the worker was started
    bool stop{};                             ///< the worker stops at its next wait
    /// Set as the worker stops: a picture's fetch ends within 50 milliseconds.
    std::atomic<bool> cancel{false};
    /// The next search for updates is due: list the installed packages
    /// again, match them to the catalogues and find their updates.
    bool updates_due{};
    std::shared_ptr<const content::Snapshot> snapshot{}; ///< the catalogues the next search reads
    std::filesystem::path user_folder{};                 ///< the player's folder it lists
    /// The packages the last search found installed, in list_installed's order.
    std::vector<content::InstalledPackage> installed{};
    std::vector<content::Match> matches{};    ///< the catalogue release each is matched to
    std::vector<content::Update> updates{};   ///< the updates the catalogues offer for them
    std::vector<ModBadge> installed_badges{}; ///< each installed mod's own picture; empty for none
    uint64_t searches{};                      ///< counts the searches done
    bool pictures_due{};                      ///< pictures wait to be read or fetched
    bool match_running{};                     ///< a match runs: nothing is fetched
    /// The pictures asked for, read and fetched; its own lock guards it.
    std::unique_ptr<content::PictureCache> pictures{};
};

/// An update queued from the Library, kept so that the queue line still says
/// what it changes once its update note is gone.
struct KeptUpdate {
    std::string registry{};       ///< the package's registry
    oa::ui::library::Kind kind{}; ///< its kind
    std::string key{};            ///< its key
    /// Whether the update changes the rules saved games are tied to, as its
    /// update note said when it was queued.
    oa::ui::library::UpdateNote::Rules rules{oa::ui::library::UpdateNote::Rules::same};
    std::string from{}; ///< what ROLL BACK restores, as change_texts gives it
    std::string to{};   ///< what the update installs
};

/// The Library's host: its model and screen state, its feed, the pictures
/// it holds, the updates it keeps for the queue and its worker.
struct Runtime::LibraryHost {
    /// Makes the host, its worker not started.
    LibraryHost() = default;

    /// Stops the worker and joins it, within a second even while it fetches.
    ~LibraryHost();

    LibraryHost(const LibraryHost&) = delete;
    LibraryHost& operator=(const LibraryHost&) = delete;

    oa::ui::library::Library model{};     ///< the Library's model
    oa::ui::library::ScreenState state{}; ///< its screen's hover, focus, scroll and search
    LibraryFeed feed{};                   ///< where its inputs come from
    bool own_feed{true};                  ///< the feed is the runtime's own
    bool refreshed{};                     ///< the model was built from the feed once
    uint64_t feed_generation{};           ///< the feed's generation the model was built at
    uint64_t service_generation{};        ///< the content service's generation last searched at
    uint64_t queue_generation{};          ///< the download queue's generation last read
    uint64_t model_revision{};            ///< counts the model's rebuilds
    std::size_t update_count{};           ///< the updates that can be installed (update_count)
    /// The pictures taken from the worker, by address: they stay while the
    /// host lives.
    std::map<std::string, oa::ui::library::Badge> badges{};
    /// Each listing's picture's address, by registry and key, as the last
    /// inputs resolved it.
    std::map<std::pair<std::string, std::string>, std::string> picture_urls{};
    std::vector<KeptUpdate> kept_updates{};  ///< the updates queued from the Library
    std::vector<uint64_t> installed_items{}; ///< the queue's items seen installed
    LibraryWork work{};                      ///< the worker and what it found

    /// Starts the worker when it has not started.
    ///
    /// @return false when the worker could not start
    bool ensure_worker();

    /// Asks the worker to search for updates again over the content
    /// service's snapshot now: after a catalogue refresh, an install or a
    /// roll back. The runtime's own feed only.
    ///
    /// @param runtime the runtime
    void look_again(Runtime& runtime);

    /// Builds the model from the feed's inputs, as they are now, and keeps
    /// the update count; the screen lays itself out again at its next tick.
    ///
    /// @param runtime the runtime
    void rebuild(Runtime& runtime);

    /// Asks for the pictures of the entries the Library's list shows, and of
    /// the entry whose details show, that it does not hold yet, and wakes the
    /// worker while any waits. The runtime's own feed only; nothing while a
    /// match runs.
    ///
    /// @param list the display list the Library shows
    void request_pictures(const oa::ui::kit::DisplayList& list);

    /// The runtime's own feed: the Library's inputs (Runtime::library_inputs).
    ///
    /// @param context the runtime
    /// @return the inputs
    static oa::ui::library::Inputs own_inputs(void* context);

    /// The runtime's own feed: what moves whenever the inputs would change
    /// (Runtime::library_feed_generation).
    ///
    /// @param context the runtime
    /// @return the count
    static uint64_t own_generation(void* context);

    /// The runtime's own feed: queues a package (Runtime::queue_download).
    ///
    /// @param context the runtime
    /// @param registry the registry's id
    /// @param key the package's key
    /// @param reason why the player asked
    /// @param[out] why why it was not queued; may be null
    /// @return the queue item; nothing when it was refused
    static std::optional<uint64_t> own_enqueue(
        void* context,
        std::string_view registry,
        std::string_view key,
        content::DownloadReason reason,
        std::string* why
    );

    /// The runtime's own feed: cancels the latest download of a package that
    /// has not ended.
    ///
    /// @param context the runtime
    /// @param registry the registry's id
    /// @param key the package's key
    static void own_cancel(void* context, std::string_view registry, std::string_view key);

    /// The runtime's own feed: queues the latest failed or cancelled
    /// download of a package again.
    ///
    /// @param context the runtime
    /// @param registry the registry's id
    /// @param key the package's key
    static void own_retry(void* context, std::string_view registry, std::string_view key);

    /// The runtime's own feed: whether a registry is built in, from the
    /// content service's snapshot: its origin is built-in, as the inputs'
    /// Registry::built_in says it.
    ///
    /// @param context the runtime
    /// @param registry the registry's id
    /// @return true for a registry that ships with the game
    static bool own_built_in(void* context, std::string_view registry);
};

/// Returns the Library's tab a tab of the settings dialog names.
///
/// @param tab the dialog's tab (Dialog::library_tab)
/// @return the Library's
[[nodiscard]] oa::ui::library::Tab library_tab_of(oa::ui::engine_settings::LibraryTab tab) noexcept;

} // namespace oa::app
