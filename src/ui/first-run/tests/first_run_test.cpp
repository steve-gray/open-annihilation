// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The first-run gate, and the coach marks at Compact, Regular and Large.

#include "oa/ui/first_run/first_run.hpp"

#include "oa/test/check.hpp"

#include <string>

namespace {

using oa::ui::first_run::Ask;
using oa::ui::first_run::Card;
using oa::ui::first_run::CardKind;
using oa::ui::first_run::CatalogueNote;
using oa::ui::first_run::GateFacts;
using oa::ui::first_run::Hold;
using oa::ui::first_run::Model;
using oa::ui::first_run::PackState;
using oa::ui::first_run::Session;
using oa::ui::first_run::Step;

bool meets(const oa::ui::kit::Rect& left, const oa::ui::kit::Rect& right) {
    if (left.width <= 0 || left.height <= 0 || right.width <= 0 || right.height <= 0)
        return false;
    const oa::ui::kit::Rect shared = oa::ui::kit::intersect(left, right);
    return shared.width > 0 && shared.height > 0;
}

void expect_hold(GateFacts facts, Hold hold, const char* name) {
    const Hold decided = oa::ui::first_run::decide(facts);
    if (decided != hold) {
        std::fprintf(stderr, "gate %s decided %d\n", name, static_cast<int>(decided));
        OA_CHECK(decided == hold);
    }
}

void check_gate() {
    expect_hold({}, Hold::show, "a played start");
    GateFacts done;
    done.done = true;
    expect_hold(done, Hold::done, "done");
    GateFacts headless;
    headless.headless_check = true;
    headless.unattended = true;
    expect_hold(headless, Hold::headless_check, "headless check");
    GateFacts preferences;
    preferences.preferences_file = true;
    preferences.unattended = true;
    expect_hold(preferences, Hold::preferences_file, "preferences file");
    GateFacts benchmark;
    benchmark.benchmark = true;
    benchmark.unattended = true;
    expect_hold(benchmark, Hold::benchmark, "benchmark");
    GateFacts stage;
    stage.stage = true;
    stage.unattended = true;
    expect_hold(stage, Hold::stage, "stage");
    GateFacts fark;
    fark.fark = true;
    fark.unattended = true;
    expect_hold(fark, Hold::fark, "fark");
    GateFacts unattended;
    unattended.unattended = true;
    expect_hold(unattended, Hold::unattended, "unattended");
    GateFacts forced;
    forced.force = true;
    forced.done = true;
    forced.headless_check = true;
    forced.preferences_file = true;
    forced.benchmark = true;
    forced.stage = true;
    forced.fark = true;
    forced.unattended = true;
    expect_hold(forced, Hold::show, "--first-run");
    OA_CHECK(oa::ui::first_run::hold_reason(Hold::headless_check) == "headless check");
    OA_CHECK(oa::ui::first_run::hold_reason(Hold::preferences_file) == "preferences file");
    OA_CHECK(oa::ui::first_run::hold_reason(Hold::stage) == "stage");
    OA_CHECK(oa::ui::first_run::hold_reason(Hold::fark) == "fark");
    OA_CHECK(oa::ui::first_run::hold_reason(Hold::show).empty());
    OA_CHECK(oa::ui::first_run::step_named("language") == Step::language);
    OA_CHECK(oa::ui::first_run::step_named("online") == Step::online);
    OA_CHECK(oa::ui::first_run::step_named("buttons") == Step::buttons);
    OA_CHECK(!oa::ui::first_run::step_named("language-notice"));
    OA_CHECK(std::string(oa::ui::first_run::preference_key) == "open-annihilation.first-run");
    OA_CHECK(std::string(oa::ui::first_run::preference_done) == "done");
}

Session buttons_session(oa::ui::kit::Rect library, oa::ui::kit::Rect settings) {
    Session session;
    session.model.step = Step::buttons;
    session.model.library_button = library;
    session.model.settings_button = settings;
    session.model.library_count = 2;
    return session;
}

void check_coaches(
    oa::ui::kit::SizeClass size_class, int32_t width, int32_t height, bool short_right
) {
    oa::ui::kit::Frame frame;
    frame.area = {0, 0, width, height};
    frame.size_class = size_class;
    frame.scale_percent = 100;
    constexpr int32_t kSide = 48;
    oa::ui::kit::Rect settings{width - 24 - kSide, 16, kSide, kSide};
    oa::ui::kit::Rect library{settings.x - 12 - kSide, 16, kSide, kSide};
    if (short_right) {
        settings = {width - 4 - kSide, 16, kSide, kSide};
        library = {settings.x - 8 - kSide, 16, kSide, kSide};
    }
    const Session session = buttons_session(library, settings);
    const auto scene = oa::ui::first_run::lay_out(session, frame, nullptr);
    OA_CHECK(scene.coach_library.width > 0);
    OA_CHECK(scene.coach_settings.width > 0);
    OA_CHECK(oa::ui::kit::wholly_in(scene.coach_library, frame.area));
    OA_CHECK(oa::ui::kit::wholly_in(scene.coach_settings, frame.area));
    OA_CHECK(!meets(scene.coach_library, library));
    OA_CHECK(!meets(scene.coach_library, settings));
    OA_CHECK(!meets(scene.coach_settings, library));
    OA_CHECK(!meets(scene.coach_settings, settings));
    OA_CHECK(!meets(scene.coach_library, scene.coach_settings));
    if (short_right) {
        const bool library_flipped =
            scene.coach_library.x + scene.coach_library.width <= library.x ||
            scene.coach_library.y >= library.y + library.height ||
            scene.coach_library.y + scene.coach_library.height <= library.y;
        OA_CHECK(library_flipped);
    }

    const int32_t card_width = oa::ui::first_run::coach_card_width(size_class);
    const auto placed =
        oa::ui::first_run::place_coaches(frame.area, {}, library, settings, card_width, 96, 96);
    OA_CHECK(!meets(placed.library, library));
    OA_CHECK(!meets(placed.library, settings));
    OA_CHECK(!meets(placed.settings, library));
    OA_CHECK(!meets(placed.settings, settings));
    OA_CHECK(!meets(placed.library, placed.settings));
}

void check_keys() {
    Session session;
    Card english;
    english.tag = "en";
    english.own_name = "English";
    english.english_name = "English";
    english.kind = CardKind::built_in;
    english.current = true;
    Card german;
    german.tag = "de";
    german.own_name = "Deutsch";
    german.english_name = "German";
    german.kind = CardKind::built_in;
    session.model.cards = {english, german};
    session.carousel.selected = 0;
    oa::ui::first_run::bind(session);
    oa::ui::kit::Frame frame;
    frame.area = {0, 0, 1280, 720};
    frame.size_class = oa::ui::kit::SizeClass::large;
    const auto scene = oa::ui::first_run::lay_out(session, frame, nullptr);
    oa::ui::first_run::ensure_focus(session, scene.list);
    const auto escape = oa::ui::first_run::key(session, scene, oa::ui::kit::Key::escape);
    OA_CHECK(escape.ask == Ask::none);
    const auto right = oa::ui::first_run::key(session, scene, oa::ui::kit::Key::right);
    OA_CHECK(right.redraw);
    OA_CHECK(session.carousel.selected == 1);
    session.model.step = Step::online;
    session.focus_ready = false;
    const auto online = oa::ui::first_run::lay_out(session, frame, nullptr);
    oa::ui::first_run::ensure_focus(session, online.list);
    const auto back = oa::ui::first_run::key(session, online, oa::ui::kit::Key::escape);
    OA_CHECK(back.ask == Ask::back);
    OA_CHECK(session.model.online_on);
    const auto off = oa::ui::first_run::key(session, online, oa::ui::kit::Key::down);
    OA_CHECK(off.ask == Ask::choose_off);
    OA_CHECK(!session.model.online_on);

    Card pack;
    pack.tag = "en-XA";
    pack.own_name = "Pseudo";
    pack.kind = CardKind::pack;
    pack.pack = PackState::downloading;
    session.model.step = Step::language;
    session.model.cards = {english, pack};
    session.model.catalogue = CatalogueNote::none;
    session.carousel.selected = 0;
    session.focus_ready = false;
    oa::ui::first_run::bind(session);
    OA_CHECK(oa::ui::first_run::download_busy(session.model));
    const auto busy = oa::ui::first_run::lay_out(session, frame, nullptr);
    const oa::ui::kit::Control* next =
        oa::ui::kit::control_of(busy.list, oa::ui::first_run::next_id);
    OA_CHECK(next != nullptr && !next->enabled);
    bool saw_card = false;
    for (const oa::ui::kit::Control& control : busy.list.controls) {
        if (control.name == "first-run.language.card.en")
            saw_card = true;
        if (control.name == "first-run.language.skip")
            OA_CHECK(control.enabled);
    }
    OA_CHECK(saw_card);
}

} // namespace

int main() {
    check_gate();
    check_coaches(oa::ui::kit::SizeClass::compact, 800, 500, false);
    check_coaches(oa::ui::kit::SizeClass::regular, 1024, 640, false);
    check_coaches(oa::ui::kit::SizeClass::large, 1280, 720, false);
    check_coaches(oa::ui::kit::SizeClass::large, 1280, 720, true);
    check_keys();
    return oa::test::check_exit_status();
}
