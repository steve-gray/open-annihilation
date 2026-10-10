// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The refresh rules, the merged snapshot and reference resolution. No network.
#include "oa/app/content/service.hpp"

#include "oa/test/check.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace oa::app::content;
namespace catalogue = oa::data::catalogue;
namespace registry = oa::data::registry;
namespace url = oa::formats::url;

void expect_due(
    RefreshReason reason,
    CheckForUpdates check,
    int64_t now,
    std::optional<int64_t> checked,
    std::optional<int64_t> last_attempt,
    bool due
) {
    OA_CHECK(refresh_due(reason, check, now, checked, last_attempt) == due);
}

void test_refresh_due() {
    constexpr int64_t age = start_refresh_age_seconds;
    constexpr int64_t gap = library_refresh_gap_seconds;
    constexpr int64_t then = 1'000'000;

    expect_due(RefreshReason::check_now, CheckForUpdates::never, then, then, then, true);
    expect_due(
        RefreshReason::check_now,
        CheckForUpdates::library_only,
        then,
        std::nullopt,
        std::nullopt,
        true
    );
    expect_due(RefreshReason::check_now, CheckForUpdates::automatically, 0, 50, 50, true);

    expect_due(
        RefreshReason::start, CheckForUpdates::automatically, then, std::nullopt, std::nullopt, true
    );
    expect_due(
        RefreshReason::start, CheckForUpdates::automatically, then + age, then, std::nullopt, false
    );
    expect_due(
        RefreshReason::start,
        CheckForUpdates::automatically,
        then + age + 1,
        then,
        std::nullopt,
        true
    );
    expect_due(
        RefreshReason::start,
        CheckForUpdates::automatically,
        then + age - 1,
        then,
        std::nullopt,
        false
    );
    expect_due(
        RefreshReason::start, CheckForUpdates::automatically, then - 1, then, std::nullopt, false
    );
    expect_due(
        RefreshReason::start, CheckForUpdates::library_only, then, std::nullopt, std::nullopt, false
    );
    expect_due(
        RefreshReason::start, CheckForUpdates::never, then, std::nullopt, std::nullopt, false
    );

    expect_due(
        RefreshReason::library_opened,
        CheckForUpdates::never,
        then,
        std::nullopt,
        std::nullopt,
        false
    );
    expect_due(
        RefreshReason::library_opened, CheckForUpdates::never, then + 1000, then, then, false
    );
    expect_due(
        RefreshReason::library_opened,
        CheckForUpdates::library_only,
        then,
        std::nullopt,
        std::nullopt,
        true
    );
    expect_due(
        RefreshReason::library_opened,
        CheckForUpdates::automatically,
        then,
        std::nullopt,
        std::nullopt,
        true
    );
    expect_due(
        RefreshReason::library_opened,
        CheckForUpdates::library_only,
        then + gap - 1,
        then,
        then,
        false
    );
    expect_due(
        RefreshReason::library_opened, CheckForUpdates::library_only, then + gap, then, then, true
    );
    expect_due(
        RefreshReason::library_opened,
        CheckForUpdates::automatically,
        then + gap + 1,
        then,
        then,
        true
    );
    expect_due(
        RefreshReason::library_opened, CheckForUpdates::library_only, then - 1, then, then, false
    );
}

RegistryView view_of(
    std::string id,
    registry::Origin origin,
    RegistryStatus status,
    std::shared_ptr<const catalogue::Catalogue> catalogue
) {
    RegistryView view;
    view.registry.descriptor.id = std::move(id);
    view.registry.origin = origin;
    view.registry.enabled = status != RegistryStatus::disabled;
    view.registry.conflicting = status == RegistryStatus::conflicting;
    view.status = status;
    view.catalogue = std::move(catalogue);
    return view;
}

std::shared_ptr<const catalogue::Catalogue> packages_of(std::vector<std::string> ids) {
    auto loaded = std::make_shared<catalogue::Catalogue>();
    for (std::string& id : ids) {
        catalogue::Package package;
        package.id = std::move(id);
        loaded->packages.push_back(std::move(package));
    }
    return loaded;
}

void test_make_snapshot() {
    std::vector<RegistryView> views;
    views.push_back(view_of(
        "alpha", registry::Origin::built_in, RegistryStatus::fresh, packages_of({"ridge", "extra"})
    ));
    views.push_back(view_of(
        "beta", registry::Origin::added, RegistryStatus::out_of_date, packages_of({"ridge"})
    ));
    views.push_back(
        view_of("old", registry::Origin::added, RegistryStatus::failed, packages_of({"kept"}))
    );
    views.push_back(view_of(
        "off", registry::Origin::built_in, RegistryStatus::disabled, packages_of({"hidden"})
    ));
    views.push_back(view_of(
        "clash", registry::Origin::added, RegistryStatus::conflicting, packages_of({"hidden"})
    ));
    views.push_back(view_of(
        "local",
        registry::Origin::added,
        RegistryStatus::needs_developer_mode,
        packages_of({"hidden"})
    ));
    views.push_back(
        view_of("empty", registry::Origin::built_in, RegistryStatus::never_fetched, nullptr)
    );

    const Snapshot snapshot = make_snapshot(std::move(views), "the file was refused");
    OA_CHECK(snapshot.generation == 0);
    OA_CHECK(snapshot.registries_file_error == "the file was refused");
    OA_CHECK(snapshot.registries.size() == 7);
    OA_CHECK(snapshot.registries[3].status == RegistryStatus::disabled);
    OA_CHECK(snapshot.registries[4].status == RegistryStatus::conflicting);
    OA_CHECK(snapshot.entries.size() == 4);
    if (snapshot.entries.size() == 4) {
        OA_CHECK(snapshot.entries[0].registry == "alpha" && snapshot.entries[0].reviewed);
        OA_CHECK(snapshot.entries[0].package && snapshot.entries[0].package->id == "ridge");
        OA_CHECK(
            snapshot.entries[1].registry == "alpha" && snapshot.entries[1].package->id == "extra"
        );
        OA_CHECK(snapshot.entries[2].registry == "beta" && !snapshot.entries[2].reviewed);
        OA_CHECK(snapshot.entries[2].package && snapshot.entries[2].package->id == "ridge");
        OA_CHECK(
            snapshot.entries[3].registry == "old" && snapshot.entries[3].package->id == "kept"
        );
    }
}

void expect_url(const std::vector<url::Url>& urls, std::size_t index, const char* expected) {
    if (index >= urls.size()) {
        std::fprintf(stderr, "resolved nothing at %zu, wanted %s\n", index, expected);
        OA_CHECK(index < urls.size());
        return;
    }
    const std::string text = url::url_text(urls[index]);
    if (text != expected)
        std::fprintf(stderr, "resolved %s, wanted %s\n", text.c_str(), expected);
    OA_CHECK(text == expected);
}

void test_reference_urls() {
    RegistryView view;
    view.catalogue = std::make_shared<catalogue::Catalogue>();
    view.served_from = url::parse_http_url("http://127.0.0.1:9/mirror/catalogue.json");
    view.sources.push_back(*url::parse_http_url("http://127.0.0.1:8/v1/catalogue.json"));
    view.sources.push_back(*url::parse_http_url("http://127.0.0.1:9/mirror/catalogue.json"));
    view.sources.push_back(*url::parse_http_url("http://127.0.0.1:7/v1/catalogue.json"));
    OA_CHECK(view.served_from.has_value());

    const std::vector<url::Url> root = reference_urls(view, "/v1/p/x.oamod");
    OA_CHECK(root.size() == 3);
    expect_url(root, 0, "http://127.0.0.1:9/v1/p/x.oamod");
    expect_url(root, 1, "http://127.0.0.1:8/v1/p/x.oamod");
    expect_url(root, 2, "http://127.0.0.1:7/v1/p/x.oamod");

    const std::vector<url::Url> relative = reference_urls(view, "p/x.oamod");
    OA_CHECK(relative.size() == 3);
    expect_url(relative, 0, "http://127.0.0.1:9/mirror/p/x.oamod");
    expect_url(relative, 1, "http://127.0.0.1:8/v1/p/x.oamod");
    expect_url(relative, 2, "http://127.0.0.1:7/v1/p/x.oamod");

    RegistryView bare;
    OA_CHECK(reference_urls(bare, "/v1/p/x.oamod").empty());
    OA_CHECK(reference_urls(view, "not a reference").empty());
}

} // namespace

int main() {
    test_refresh_due();
    test_make_snapshot();
    test_reference_urls();
    return oa::test::check_exit_status();
}
