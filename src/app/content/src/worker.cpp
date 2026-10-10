// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The one worker: a queue of refreshes and posted jobs, and one HTTP client.
#include "service_state.hpp"

#include <exception>
#include <string>
#include <utility>

namespace oa::app::content {
namespace {

/// Runs queued work until the service stops. The client dies with this function.
///
/// @param state the service
void worker_loop(ServiceState& state) {
    netgame::http::Client client(state.options.http);
    for (;;) {
        WorkItem item;
        {
            base::threads::LockGuard guard(state.mutex);
            while (!state.stop && state.queue.empty())
                state.wake.wait(state.mutex);
            if (state.stop) {
                state.queue.clear();
                for (RegistryRecord& record : state.registries)
                    record.refreshing = false;
                return;
            }
            item = std::move(state.queue.front());
            state.queue.erase(state.queue.begin());
        }

        if (item.kind == WorkItem::Kind::refresh) {
            try {
                refresh_registry(state, item.index, client);
            } catch (const std::exception& error) {
                log_line(state, std::string("a catalogue refresh failed: ") + error.what());
                clear_refreshing(state, item.index, true);
            } catch (...) {
                log_line(state, "a catalogue refresh failed");
                clear_refreshing(state, item.index, true);
            }
            continue;
        }

        if (!item.job)
            continue;
        WorkerTools tools{client, &state.cancel, content_directory(state)};
        try {
            item.job(tools);
        } catch (const std::exception& error) {
            log_line(state, std::string("a catalogue job failed: ") + error.what());
        } catch (...) {
            log_line(state, "a catalogue job failed");
        }
    }
}

} // namespace

void clear_refreshing(ServiceState& state, std::size_t index, bool publish_change) {
    base::threads::LockGuard guard(state.mutex);
    if (index >= state.registries.size())
        return;
    if (!state.registries[index].refreshing)
        return;
    state.registries[index].refreshing = false;
    if (publish_change && !state.stop)
        publish(state);
}

bool ensure_worker(ServiceState& state) {
    if (state.thread_started || state.queue.empty() || state.stop)
        return true;
    if (!base::threads::start_thread(state.thread, worker_entry, &state)) {
        state.queue.clear();
        for (RegistryRecord& record : state.registries)
            record.refreshing = false;
        return false;
    }
    state.thread_started = true;
    state.wake.notify_one();
    return true;
}

void worker_entry(void* argument) {
    auto* const state = static_cast<ServiceState*>(argument);
    try {
        worker_loop(*state);
    } catch (const std::exception& error) {
        log_line(*state, std::string("the catalogue worker stopped: ") + error.what());
    } catch (...) {
        log_line(*state, "the catalogue worker stopped");
    }
}

} // namespace oa::app::content
