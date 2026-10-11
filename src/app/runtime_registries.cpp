// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The registries' host: the Registries part's offer in Settings › Downloads,
// from the content service's registries in effect, and the changes OK makes,
// each through the content service, which applies the registry rules.
// Nothing here fetches, verifies or writes a registry itself.

#include "oa/app/content/service.hpp"
#include "oa/app/runtime.hpp"
#include "oa/data/catalogue/catalogue.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/data/registry/trust.hpp"
#include "oa/ui/engine_settings/dialog.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {

namespace {

namespace settings = oa::ui::engine_settings;
namespace content = oa::app::content;
namespace registry = oa::data::registry;

/// Returns now, in seconds since 1970, as the content service's checked times are.
///
/// @return the time
int64_t seconds_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    )
        .count();
}

/// Writes a line to standard error, which the log keeps.
///
/// @param line the line
void log_line(std::string_view line) {
    std::cerr << "open-annihilation: registries: " << line << '\n';
}

/// What outlives one dialog: why the last OK could not change a registry.
struct RegistriesHold {
    std::string notice; ///< the next Registries part's note
};

RegistriesHold hold;

} // namespace

void Runtime::give_settings_registries(settings::Dialog& dialog, bool opening) {
    settings::RegistriesOffer offer;
    if (opening) {
        offer.notice = std::move(hold.notice);
        hold.notice.clear();
    } else {
        offer.notice = dialog.registries.notice;
    }
    const std::shared_ptr<const content::Snapshot> snapshot = content_service().snapshot();
    if (snapshot) {
        const int64_t now = seconds_now();
        for (const content::RegistryView& listed : snapshot->registries) {
            // An added registry whose id a built-in one has taken is never
            // used; the built-in registry stands for it.
            if (listed.registry.conflicting)
                continue;
            const registry::Descriptor& descriptor = listed.registry.descriptor;
            settings::RegistryRow row;
            row.id = descriptor.id;
            row.name = descriptor.name;
            row.address = registry::address_text(descriptor);
            row.built_in = listed.registry.origin == registry::Origin::built_in;
            row.on = listed.registry.enabled;
            row.packages = listed.catalogue ? listed.catalogue->packages.size() : 0;
            if (listed.checked)
                row.checked_ago = std::max(now - *listed.checked, int64_t{0});
            row.mirrors = listed.registry.player_mirrors.size();
            row.unreachable = !listed.last_error.empty();
            offer.registries.push_back(std::move(row));
        }
    }
    settings::set_registries_offer(dialog, offer);
}

void Runtime::apply_registry_changes(const std::vector<settings::RegistryChange>& changes) {
    if (changes.empty())
        return;
    content::Service& service = content_service();
    for (const settings::RegistryChange& change : changes) {
        std::string why;
        registry::Refusal refusal = registry::Refusal::none;
        switch (change.kind) {
        case settings::RegistryChangeKind::turn_on:
        case settings::RegistryChangeKind::turn_off:
            refusal = service.set_registry_enabled(
                change.id, change.kind == settings::RegistryChangeKind::turn_on, &why
            );
            break;
        case settings::RegistryChangeKind::remove:
            refusal = service.remove_registry(change.id, &why);
            break;
        }
        if (refusal == registry::Refusal::none)
            continue;
        const std::string reason = why.empty() ? std::string(registry::refusal_text(refusal)) : why;
        log_line(
            change.id + ": not changed: " + registry::refusal_text(refusal) +
            (why.empty() ? std::string() : " (" + why + ")")
        );
        hold.notice = "OA could not change the registries: " + reason;
        while (!hold.notice.empty() && hold.notice.back() == '.')
            hold.notice.pop_back();
        hold.notice += '.';
    }
}

} // namespace oa::app
