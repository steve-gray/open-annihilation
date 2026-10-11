// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Registry table behaviour: duplicate rejection, lookups, overlay z order and
// the dispatcher-query table.
#include "oa/ui/frontend_state/dispatcher.hpp"
#include "oa/ui/kit/rows.hpp"
#include "oa/ui/screen_registry.hpp"
#include "oa/ui/screen_registry/kit.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace oa::app {
void register_builtin_screens(ScreenRegistry*) {
}
} // namespace oa::app

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

void noop(oa::app::ScreenContext*, void*) {
}

// Answers a query with the number its state holds.
uint32_t answer(oa::app::ScreenContext*, void* state) {
    return *static_cast<const uint32_t*>(state);
}

// A query label no dispatcher state asks, for filling the table.
oa::ui::frontend_state::Query spare_query(uint32_t index) {
    constexpr uint32_t kFirstSpareQuery = 0x300;
    return static_cast<oa::ui::frontend_state::Query>(kFirstSpareQuery + index);
}

// Queries: binding, lookup, and the null, duplicate and full-table rejections.
void test_queries() {
    using namespace oa::app;
    using oa::ui::frontend_state::Query;
    static ScreenRegistry registry{};
    expect(query_find(&registry, Query::map_list_object_state) == nullptr, "no query bound yet");
    expect(
        !query_register(&registry, Query::map_list_object_state, nullptr, nullptr),
        "a null query handler is rejected"
    );
    expect(
        registry.rejected != nullptr && std::strcmp(registry.rejected, "dispatcher query") == 0,
        "a rejected query records \"dispatcher query\""
    );
    expect(registry.query_count == 0, "a rejected query takes no entry");

    static uint32_t value = 42;
    expect(
        query_register(&registry, Query::map_list_object_state, answer, &value), "query registers"
    );
    const auto* bound = query_find(&registry, Query::map_list_object_state);
    expect(bound != nullptr && bound->run == answer && bound->state == &value, "query found");
    expect(bound != nullptr && bound->run(nullptr, bound->state) == 42, "the handler answers");
    expect(
        !query_register(&registry, Query::map_list_object_state, answer, nullptr),
        "duplicate query rejected"
    );
    expect(query_find(&registry, spare_query(0)) == nullptr, "unknown query absent");

    static ScreenRegistry full{};
    for (uint32_t index = 0; index < kMaxQueries; ++index)
        expect(query_register(&full, spare_query(index), answer, &value), "the table fills");
    expect(full.rejected == nullptr, "a filled table rejected nothing");
    expect(
        !query_register(&full, Query::map_list_object_state, answer, &value),
        "a query past the table's end is rejected"
    );
    expect(
        full.rejected != nullptr && std::strcmp(full.rejected, "dispatcher query") == 0,
        "the full table records \"dispatcher query\""
    );
    expect(full.query_count == kMaxQueries, "the full table keeps its entries");
    expect(query_find(&full, spare_query(kMaxQueries - 1)) != nullptr, "the last entry is found");
}

oa::ui::kit::DisplayList lay_check(void*, const oa::ui::kit::Frame&) {
    return {};
}

int act_check(void*, const oa::app::KitScreenAction&) {
    return 0;
}

// A page and a screen are taken. Bad ids and keys, a duplicate, a full
// registry and an engine section's word are rejected.
void test_kit_registrations() {
    using namespace oa::app;
    namespace kit = oa::ui::kit;
    static const kit::PreferenceRow rows[] = {
        kit::preference_toggle("toggle", "Toggle", "check.toggle", "1"),
    };
    static const std::array<std::string_view, 2> choice_ids{"quiet", "loud"};
    static const std::array<std::string_view, 2> choice_captions{"Quiet", "Loud"};
    static const kit::PreferenceRow choice =
        kit::preference_choice("mode", "Mode", "check.mode", "quiet", choice_ids, choice_captions);

    SettingsRegistry registry{};
    SettingsPageDesc page{};
    page.id = "check.page";
    page.title = "Check page";
    page.owner = "Check";
    page.rows = rows;
    page.row_count = 1;
    expect(settings_page_register(&registry, page) == 1, "a page is taken");
    expect(
        registry.page_count == 1 && registry.rejected == nullptr, "a taken page records nothing"
    );
    expect(std::strcmp(registry.pages[0].title, "Check page") == 0, "the title is copied");
    expect(std::strcmp(registry.pages[0].owner, "Check") == 0, "the owner is copied");

    expect(settings_page_register(&registry, page) == 0, "a duplicate id is rejected");
    expect(
        registry.rejected != nullptr && std::strcmp(registry.rejected, "a page id is taken") == 0,
        "rejected names the duplicate"
    );
    expect(registry.page_count == 1, "a rejected page takes no entry");

    page.id = "Developer";
    expect(settings_page_register(&registry, page) == 0, "a bad id is rejected");
    expect(registry.rejected != nullptr, "rejected names the bad id");

    page.id = "developer";
    expect(settings_page_register(&registry, page) == 0, "an engine section word is rejected");
    expect(
        registry.rejected != nullptr &&
            std::strcmp(registry.rejected, "a page id is an engine section") == 0,
        "rejected names the engine section"
    );

    page.id = "registered";
    expect(settings_page_register(&registry, page) == 0, "the registered word is rejected");

    static const kit::PreferenceRow engine_key =
        kit::preference_toggle("toggle", "Toggle", "open-annihilation.language", "1");
    page.id = "other.page";
    page.rows = &engine_key;
    page.row_count = 1;
    expect(settings_page_register(&registry, page) == 0, "an engine key is rejected");
    expect(registry.rejected != nullptr, "rejected names the engine key");

    static const kit::PreferenceRow pipe_key =
        kit::preference_toggle("toggle", "Toggle", "section|key", "1");
    page.rows = &pipe_key;
    expect(settings_page_register(&registry, page) == 0, "a section|key key is rejected");
    expect(registry.rejected != nullptr, "rejected names the malformed key");

    static const kit::PreferenceRow unbound = kit::preference_toggle("toggle", "Toggle", "", "1");
    page.rows = &unbound;
    expect(settings_page_register(&registry, page) == 0, "a row with no binding is rejected");
    expect(
        registry.rejected != nullptr && std::strcmp(registry.rejected, "a row has no binding") == 0,
        "rejected names the missing binding"
    );

    page.rows = &choice;
    page.id = "choice.page";
    expect(settings_page_register(&registry, page) == 1, "a choice page is taken");

    SettingsRegistry full{};
    page.rows = rows;
    page.row_count = 1;
    page.title = "Full";
    page.owner = "Full";
    std::string ids[kMaxSettingsPages + 1];
    for (uint32_t index = 0; index < kMaxSettingsPages + 1; ++index) {
        ids[index] = "page." + std::to_string(index);
        page.id = ids[index].c_str();
        const int taken = settings_page_register(&full, page);
        expect(taken == (index < kMaxSettingsPages ? 1 : 0), "the page table fills");
    }
    expect(
        full.rejected != nullptr && std::strcmp(full.rejected, "the registry is full") == 0,
        "a full registry is rejected"
    );
    expect(full.page_count == kMaxSettingsPages, "a full registry keeps its pages");

    KitScreenRegistry screens{};
    KitScreenDesc screen{};
    screen.name = "check.screen";
    screen.lay_out = lay_check;
    screen.act = act_check;
    expect(kit_screen_register(&screens, screen) == 1, "a screen is taken");
    expect(kit_screen_register(&screens, screen) == 0, "a duplicate screen is rejected");
    expect(screens.rejected != nullptr, "rejected names the duplicate screen");
    screen.name = "Not a name";
    expect(kit_screen_register(&screens, screen) == 0, "a bad screen name is rejected");
    expect(screens.rejected != nullptr, "rejected names the bad screen name");

    KitScreenRegistry full_screens{};
    std::string names[kMaxKitScreens + 1];
    for (uint32_t index = 0; index < kMaxKitScreens + 1; ++index) {
        names[index] = "screen." + std::to_string(index);
        screen.name = names[index].c_str();
        const int taken = kit_screen_register(&full_screens, screen);
        expect(taken == (index < kMaxKitScreens ? 1 : 0), "the screen table fills");
    }
    expect(
        full_screens.rejected != nullptr &&
            std::strcmp(full_screens.rejected, "the registry is full") == 0,
        "a full screen registry is rejected"
    );
}

} // namespace

int main() {
    using namespace oa::app;
    static ScreenRegistry registry{};
    ScreenDesc screen{};
    screen.id = kFirstPackageScreen;
    screen.name = "package";
    expect(screen_register(&registry, &screen), "first screen registers");
    expect(!screen_register(&registry, &screen), "duplicate screen id rejected");
    expect(std::strcmp(registry.rejected, "package") == 0, "rejected name recorded");
    expect(screen_find(&registry, kFirstPackageScreen) != nullptr, "screen found");
    expect(screen_find(&registry, kFirstPackageScreen + 1) == nullptr, "unknown screen absent");

    const int16_t order[] = {5, -1, 5, 0};
    for (const auto z : order) {
        OverlayDesc overlay{};
        overlay.screen = kScreenAny;
        overlay.z = z;
        overlay.name = z == 5 && registry.overlay_count == 0 ? "first5" : "other";
        expect(overlay_register(&registry, &overlay), "overlay registers");
    }
    expect(registry.overlay_count == 4, "four overlays");
    expect(registry.overlays[0].z == -1 && registry.overlays[1].z == 0, "ascending z");
    expect(std::strcmp(registry.overlays[2].name, "first5") == 0, "equal z keeps order");

    using oa::ui::frontend_state::Step;
    expect(step_register(&registry, Step::setup_main_menu, noop, nullptr), "step registers");
    expect(!step_register(&registry, Step::setup_main_menu, noop, nullptr), "duplicate step");
    expect(step_find(&registry, Step::setup_main_menu) != nullptr, "step found");
    expect(step_find(&registry, Step::setup_skirmish) == nullptr, "unknown step absent");
    test_queries();
    test_kit_registrations();
    return failures == 0 ? 0 : 1;
}
