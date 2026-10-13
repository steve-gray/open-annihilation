// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The first-run gate, and the coach marks at Compact, Regular and Large.

#include "oa/ui/first_run/first_run.hpp"

#include "oa/data/languages.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/test/check.hpp"
#include "oa/ui/kit/looks.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

using oa::ui::first_run::Ask;
using oa::ui::first_run::Card;
using oa::ui::first_run::CardKind;
using oa::ui::first_run::CatalogueNote;
using oa::ui::first_run::GateFacts;
using oa::ui::first_run::Hold;
using oa::ui::first_run::Leave;
using oa::ui::first_run::Model;
using oa::ui::first_run::PackState;
using oa::ui::first_run::Session;
using oa::ui::first_run::Step;
using oa::ui::first_run::Walk;

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
    OA_CHECK(oa::ui::first_run::step_named("game") == Step::game);
    OA_CHECK(oa::ui::first_run::step_named("copying") == Step::copying);
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

/// The step line the setup chrome will draw.
///
/// @param scene the layout
/// @return the filled line; empty when the step has no chrome
std::string chrome_step(const oa::ui::first_run::Scene& scene) {
    for (const oa::ui::kit::Item& item : scene.list.items) {
        const auto* look = std::get_if<oa::ui::kit::SetupChromeLook>(&item.look);
        if (look == nullptr)
            continue;
        oa::ui::kit::StepLook step;
        step.name = look->step_name;
        step.pattern = look->step_pattern;
        step.index = look->step_index;
        step.count = look->step_count;
        return oa::ui::kit::step_line(step);
    }
    return {};
}

/// The left footer's words.
///
/// @param scene the layout
/// @return the caption; empty when the step has no chrome
std::string chrome_skip(const oa::ui::first_run::Scene& scene) {
    for (const oa::ui::kit::Item& item : scene.list.items) {
        const auto* look = std::get_if<oa::ui::kit::SetupChromeLook>(&item.look);
        if (look != nullptr)
            return look->skip;
    }
    return {};
}

/// Tells whether a scene draws a line.
///
/// @param scene the layout
/// @param text the line
/// @return true when an item shows it
bool shows_line(const oa::ui::first_run::Scene& scene, std::string_view text) {
    for (const oa::ui::kit::Item& item : scene.list.items)
        if (item.text == text)
            return true;
    return false;
}

/// A fake catalogue that marks a text, so a missed lookup stays English.
///
/// @param scene a step, laid out
/// @param line the line it should show
void expect_line(const oa::ui::first_run::Scene& scene, std::string_view line, bool chrome) {
    const std::string got = chrome ? chrome_step(scene) : std::string{};
    const bool shown = chrome ? got == line : shows_line(scene, line);
    if (!shown) {
        std::fprintf(stderr, "step line: %s\n", chrome ? got.c_str() : "(not in the list)");
        OA_CHECK(shown);
    }
}

/// The step line and the sample Library are looked up. A catalogue that marks
/// each text changes them; the sample changes only when its hook is set.
void check_lookup() {
    oa::ui::kit::Frame frame;
    frame.area = {0, 0, 1280, 720};
    frame.size_class = oa::ui::kit::SizeClass::large;
    frame.scale_percent = 100;
    Session welcome;
    welcome.model.step = Step::language;
    const auto welcome_scene = oa::ui::first_run::lay_out(welcome, frame, nullptr);
    expect_line(welcome_scene, "WELCOME · 1 OF 3", true);
    Session online;
    online.model.step = Step::online;
    expect_line(oa::ui::first_run::lay_out(online, frame, nullptr), "ONLINE · 2 OF 3", true);
    const Session buttons = buttons_session({100, 16, 48, 48}, {160, 16, 48, 48});
    expect_line(
        oa::ui::first_run::lay_out(buttons, frame, nullptr), "THE MAIN MENU · 3 OF 3", false
    );
    Session setup;
    setup.model.walk = Walk::setup;
    setup.model.step = Step::language;
    expect_line(oa::ui::first_run::lay_out(setup, frame, nullptr), "LANGUAGE · 1 OF 5", true);
    setup.model.step = Step::game;
    expect_line(oa::ui::first_run::lay_out(setup, frame, nullptr), "THE GAME · 2 OF 5", true);
    setup.model.step = Step::copying;
    expect_line(oa::ui::first_run::lay_out(setup, frame, nullptr), "COPYING · 3 OF 5", true);
    setup.model.step = Step::online;
    expect_line(oa::ui::first_run::lay_out(setup, frame, nullptr), "ONLINE · 4 OF 5", true);
    setup.model.step = Step::buttons;
    setup.model.library_button = {100, 16, 48, 48};
    setup.model.settings_button = {160, 16, 48, 48};
    expect_line(oa::ui::first_run::lay_out(setup, frame, nullptr), "THE MAIN MENU · 5 OF 5", false);

    Model english;
    const std::string plain = oa::ui::first_run::sample_words(english);
    OA_CHECK(plain.find("Active\n") != std::string::npos);
    OA_CHECK(plain.find("UNLOAD MOD\n") != std::string::npos);
    OA_CHECK(plain.find("Built in\n") != std::string::npos);
    OA_CHECK(plain.find("~") == std::string::npos);

    const char* catalogue_text = "[{step} · {index} OF {count}]\n"
                                 "{\n"
                                 "fr=^{step}|{index}|{count}^;\n"
                                 "}\n"
                                 "[WELCOME]\n"
                                 "{\n"
                                 "fr=~WELCOME~;\n"
                                 "}\n"
                                 "[ONLINE]\n"
                                 "{\n"
                                 "fr=~ONLINE~;\n"
                                 "}\n"
                                 "[THE MAIN MENU]\n"
                                 "{\n"
                                 "fr=~THE MAIN MENU~;\n"
                                 "}\n"
                                 "[Active]\n"
                                 "{\n"
                                 "fr=~Active~;\n"
                                 "}\n"
                                 "[Mods]\n"
                                 "{\n"
                                 "fr=~Mods~;\n"
                                 "}\n"
                                 "[PLAYING]\n"
                                 "{\n"
                                 "fr=~PLAYING~;\n"
                                 "}\n"
                                 "[UNLOAD MOD]\n"
                                 "{\n"
                                 "fr=~UNLOAD MOD~;\n"
                                 "}\n"
                                 "[Built in]\n"
                                 "{\n"
                                 "fr=~Built in~;\n"
                                 "}\n";
    oa::data::languages::InterfaceText catalogue;
    OA_CHECK(catalogue.add(catalogue_text));
    const oa::data::languages::Language* french = oa::data::languages::find_by_tag("fr");
    OA_CHECK(french != nullptr);
    if (french == nullptr)
        return;
    oa::data::languages::set_interface_language(&catalogue, *french);

    expect_line(oa::ui::first_run::lay_out(welcome, frame, nullptr), "^~WELCOME~|1|3^", true);
    expect_line(oa::ui::first_run::lay_out(online, frame, nullptr), "^~ONLINE~|2|3^", true);
    expect_line(
        oa::ui::first_run::lay_out(buttons, frame, nullptr), "^~THE MAIN MENU~|3|3^", false
    );

    // The catalogue is installed, and the sample still shows English until it
    // uses the same lookup the live Library uses.
    OA_CHECK(oa::ui::first_run::sample_words(english).find("~") == std::string::npos);
    english.sample_text.shown = [](void*, std::string_view text) {
        return oa::data::languages::interface_text(text);
    };
    const std::string marked = oa::ui::first_run::sample_words(english);
    OA_CHECK(marked.find("~Active~\n") != std::string::npos);
    OA_CHECK(marked.find("~Mods~\n") != std::string::npos);
    OA_CHECK(marked.find("~PLAYING~\n") != std::string::npos);
    OA_CHECK(marked.find("~UNLOAD MOD~\n") != std::string::npos);
    OA_CHECK(marked.find("~Built in~\n") != std::string::npos);

    oa::data::languages::set_interface_language(nullptr, oa::data::languages::english());
}

Card named_card(std::string_view tag, std::string_view own_name) {
    Card card;
    card.tag = std::string(tag);
    card.own_name = std::string(own_name);
    card.english_name = std::string(own_name);
    card.kind = CardKind::built_in;
    return card;
}

const oa::ui::kit::CarouselSlot* slot_of(const oa::ui::first_run::Scene& scene, int32_t index) {
    for (const oa::ui::kit::CarouselSlot& slot : scene.places)
        if (slot.index == index)
            return &slot;
    return nullptr;
}

void check_wrap_click_and_buttons() {
    Session session;
    session.model.cards = {
        named_card("en", "English"),
        named_card("de", "Deutsch"),
        named_card("es", "Espanol"),
        named_card("fr", "Francais"),
        named_card("it", "Italiano"),
    };
    session.carousel.selected = 0;
    oa::ui::first_run::bind(session);
    oa::ui::kit::Frame frame;
    frame.area = {0, 0, 1280, 720};
    frame.size_class = oa::ui::kit::SizeClass::large;
    const auto scene = oa::ui::first_run::lay_out(session, frame, nullptr);
    oa::ui::first_run::ensure_focus(session, scene.list);
    const auto left = oa::ui::first_run::key(session, scene, oa::ui::kit::Key::left);
    OA_CHECK(left.redraw);
    OA_CHECK(session.carousel.selected == 4);
    const int32_t started = oa::ui::kit::carousel_index_milli(session.carousel);
    OA_CHECK(started - 4 * 1000 == 1000);
    oa::ui::first_run::tick(session, 20);
    OA_CHECK(oa::ui::kit::carousel_index_milli(session.carousel) < started);

    Session wheeled;
    wheeled.model.cards = session.model.cards;
    wheeled.carousel.selected = 0;
    oa::ui::first_run::bind(wheeled);
    const auto wheel_scene = oa::ui::first_run::lay_out(wheeled, frame, nullptr);
    const auto turned = oa::ui::first_run::wheel(wheeled, wheel_scene, 1.F);
    OA_CHECK(turned.redraw);
    OA_CHECK(wheeled.carousel.selected == 4);
    const int32_t wheel_start = oa::ui::kit::carousel_index_milli(wheeled.carousel);
    oa::ui::first_run::tick(wheeled, 20);
    OA_CHECK(oa::ui::kit::carousel_index_milli(wheeled.carousel) < wheel_start);

    Session picking;
    picking.model.cards = session.model.cards;
    picking.carousel.selected = 0;
    oa::ui::first_run::bind(picking);
    const auto pick_scene = oa::ui::first_run::lay_out(picking, frame, nullptr);
    const oa::ui::kit::CarouselSlot* side = slot_of(pick_scene, 1);
    OA_CHECK(side != nullptr);
    if (side != nullptr) {
        const oa::ui::kit::Point at{
            side->face.x + side->face.width / 2, side->face.y + side->face.height / 2
        };
        const auto down = oa::ui::first_run::pointer(
            picking, pick_scene, oa::ui::first_run::PointerKind::down, at, 0, 2
        );
        OA_CHECK(down.ask == Ask::none);
        const auto up = oa::ui::first_run::pointer(
            picking, pick_scene, oa::ui::first_run::PointerKind::up, at, 0, 2
        );
        OA_CHECK(up.ask == Ask::pick);
        OA_CHECK(up.index == side->index);
        OA_CHECK(picking.carousel.selected == side->index);
    }

    Session centring;
    centring.model.cards = session.model.cards;
    centring.carousel.selected = 0;
    oa::ui::first_run::bind(centring);
    const auto centre_scene = oa::ui::first_run::lay_out(centring, frame, nullptr);
    const oa::ui::kit::CarouselSlot* other = slot_of(centre_scene, 1);
    OA_CHECK(other != nullptr);
    if (other != nullptr) {
        const oa::ui::kit::Point at{
            other->face.x + other->face.width / 2, other->face.y + other->face.height / 2
        };
        std::ignore = oa::ui::first_run::pointer(
            centring, centre_scene, oa::ui::first_run::PointerKind::down, at, 0, 1
        );
        const auto up = oa::ui::first_run::pointer(
            centring, centre_scene, oa::ui::first_run::PointerKind::up, at, 0, 1
        );
        OA_CHECK(up.ask == Ask::none);
        OA_CHECK(up.redraw);
        OA_CHECK(centring.carousel.selected == other->index);
    }

    Session buttons;
    buttons.model.step = Step::buttons;
    buttons.model.cards = session.model.cards;
    const auto button_scene = oa::ui::first_run::lay_out(buttons, frame, nullptr);
    bool saw_skip = false;
    bool saw_back = false;
    bool saw_start = false;
    bool saw_pulse = false;
    for (const oa::ui::kit::Control& control : button_scene.list.controls) {
        if (control.name == "first-run.buttons.skip")
            saw_skip = true;
        if (control.name == "first-run.buttons.back")
            saw_back = true;
        if (control.name == "first-run.buttons.start")
            saw_start = true;
    }
    for (const oa::ui::kit::Item& item : button_scene.list.items) {
        if (item.role == oa::ui::kit::Role::call_to_action &&
            std::holds_alternative<oa::ui::kit::CallToActionLook>(item.look))
            saw_pulse = true;
    }
    OA_CHECK(!saw_skip);
    OA_CHECK(saw_back);
    OA_CHECK(saw_start);
    OA_CHECK(saw_pulse);
    buttons.interaction.focused = oa::ui::first_run::back_id;
    const auto enter = oa::ui::first_run::key(buttons, button_scene, oa::ui::kit::Key::enter);
    OA_CHECK(enter.ask == Ask::advance);
    const auto escape = oa::ui::first_run::key(buttons, button_scene, oa::ui::kit::Key::escape);
    OA_CHECK(escape.ask == Ask::advance);
}

/// The caption of Next.
///
/// @param scene the layout
/// @return the caption; empty when Next is not there
std::string next_caption(const oa::ui::first_run::Scene& scene) {
    const oa::ui::kit::Control* next =
        oa::ui::kit::control_of(scene.list, oa::ui::first_run::next_id);
    if (next == nullptr)
        return {};
    return next->text;
}

/// The note and Next follow a readable language in the middle, then the
/// language in use. A pack that is not installed leaves both as they are.
void check_centred_language() {
    const char* catalogue_text =
        "[Next]\n"
        "{\n"
        "de=~de-next~;\n"
        "fr=~fr-next~;\n"
        "}\n"
        "[Other languages download from Core Prime. The first download makes a random install "
        "ID, which the next step explains.]\n"
        "{\n"
        "de=~de-note~;\n"
        "fr=~fr-note~;\n"
        "}\n";
    oa::data::languages::InterfaceText catalogue;
    OA_CHECK(catalogue.add(catalogue_text));
    const oa::data::languages::Language* french = oa::data::languages::find_by_tag("fr");
    const oa::data::languages::Language* german = oa::data::languages::find_by_tag("de");
    OA_CHECK(french != nullptr && german != nullptr);
    if (french == nullptr || german == nullptr)
        return;
    oa::data::languages::set_interface_language(&catalogue, *french);
    OA_CHECK(&oa::data::languages::interface_language() == french);

    Card french_card = named_card("fr", "Francais");
    french_card.current = true;
    Card german_card = named_card("de", "Deutsch");
    Card pack;
    pack.tag = "pt";
    pack.own_name = "Portugues";
    pack.english_name = "Portuguese";
    pack.kind = CardKind::pack;
    pack.pack = PackState::ready;

    oa::ui::kit::Frame frame;
    frame.area = {0, 0, 1280, 720};
    frame.size_class = oa::ui::kit::SizeClass::large;

    Session session;
    session.model.step = Step::language;
    session.model.cards = {french_card, german_card, pack};
    session.carousel.selected = 1;
    oa::ui::first_run::bind(session);
    const auto centred = oa::ui::first_run::lay_out(session, frame, nullptr);
    OA_CHECK(next_caption(centred) == "~de-next~");
    OA_CHECK(shows_line(centred, "~de-note~"));
    OA_CHECK(&oa::data::languages::interface_language() == french);

    session.carousel.selected = 0;
    oa::ui::first_run::bind(session);
    const auto home = oa::ui::first_run::lay_out(session, frame, nullptr);
    OA_CHECK(next_caption(home) == "~fr-next~");
    OA_CHECK(shows_line(home, "~fr-note~"));
    OA_CHECK(!shows_line(home, "~de-note~"));

    session.carousel.selected = 2;
    oa::ui::first_run::bind(session);
    const auto waiting = oa::ui::first_run::lay_out(session, frame, nullptr);
    OA_CHECK(next_caption(waiting) == "~fr-next~");
    OA_CHECK(shows_line(waiting, "~fr-note~"));
    OA_CHECK(!shows_line(waiting, "~de-note~"));
    OA_CHECK(&oa::data::languages::interface_language() == french);

    session.model.step = Step::online;
    session.carousel.selected = 1;
    const auto online = oa::ui::first_run::lay_out(session, frame, nullptr);
    OA_CHECK(next_caption(online) == "~fr-next~");
    OA_CHECK(!shows_line(online, "~de-note~"));

    oa::data::languages::set_interface_language(nullptr, oa::data::languages::english());
}

/// Presses the left footer button.
///
/// @param session the session
/// @param scene the layout
/// @return what the press asked
oa::ui::first_run::Outcome press_skip(Session& session, const oa::ui::first_run::Scene& scene) {
    const oa::ui::kit::Control* skip =
        oa::ui::kit::control_of(scene.list, oa::ui::first_run::skip_id);
    OA_CHECK(skip != nullptr && skip->enabled);
    const oa::ui::kit::Point at{
        skip->rect.x + skip->rect.width / 2, skip->rect.y + skip->rect.height / 2
    };
    std::ignore =
        oa::ui::first_run::pointer(session, scene, oa::ui::first_run::PointerKind::down, at, 0, 1);
    return oa::ui::first_run::pointer(session, scene, oa::ui::first_run::PointerKind::up, at, 0, 1);
}

/// Quit on every step of a first start, Skip on every step of a re-run.
/// The footer buttons stay inside the window, at Settings' button size.
void check_leave_word() {
    oa::ui::kit::Frame frame;
    frame.area = {0, 0, 1280, 720};
    frame.size_class = oa::ui::kit::SizeClass::large;
    const Step welcome_steps[] = {Step::language, Step::online};
    Session again;
    again.model.leave = Leave::skip;
    again.model.walk = Walk::welcome;
    for (const Step step : welcome_steps) {
        again.model.step = step;
        const auto scene = oa::ui::first_run::lay_out(again, frame, nullptr);
        OA_CHECK(chrome_skip(scene) == "Skip");
        Session pressed = again;
        OA_CHECK(press_skip(pressed, scene).ask == Ask::skip);
    }

    const Step setup_steps[] = {Step::language, Step::game, Step::copying, Step::online};
    Session first;
    first.model.leave = Leave::quit;
    first.model.walk = Walk::setup;
    first.model.copy.running = true;
    for (const Step step : setup_steps) {
        first.model.step = step;
        const auto scene = oa::ui::first_run::lay_out(first, frame, nullptr);
        OA_CHECK(chrome_skip(scene) == "Quit");
        const oa::ui::kit::Control* skip =
            oa::ui::kit::control_of(scene.list, oa::ui::first_run::skip_id);
        OA_CHECK(skip != nullptr && skip->text == "Quit");
        OA_CHECK(skip->rect.height == oa::ui::kit::compact_metrics.button_height);
        OA_CHECK(skip->rect.width >= oa::ui::kit::compact_metrics.button_width);
        Session pressed = first;
        OA_CHECK(press_skip(pressed, scene).ask == Ask::quit);
    }

    const int32_t sizes[][2] = {{640, 480}, {1280, 720}, {1344, 838}};
    for (const auto& size : sizes) {
        oa::ui::kit::Frame room;
        room.area = {0, 0, size[0], size[1]};
        room.size_class = oa::ui::kit::size_class_of(size[0], size[1]);
        room.scale_percent = 100;
        Session session;
        session.model.leave = Leave::quit;
        session.model.walk = Walk::setup;
        session.model.step = Step::language;
        const auto scene = oa::ui::first_run::lay_out(session, room, nullptr);
        for (const oa::ui::kit::Control& control : scene.list.controls) {
            const std::string_view name = control.name;
            const bool footer =
                name.ends_with(".skip") || name.ends_with(".back") || name.ends_with(".next");
            if (!footer)
                continue;
            const int32_t right = control.rect.x + control.rect.width;
            const int32_t bottom = control.rect.y + control.rect.height;
            OA_CHECK(control.rect.x >= oa::ui::kit::compact_metrics.padding);
            OA_CHECK(control.rect.y >= 0);
            OA_CHECK(right <= size[0] - oa::ui::kit::compact_metrics.edge);
            OA_CHECK(bottom <= size[1]);
            OA_CHECK(control.rect.height == oa::ui::kit::compact_metrics.button_height);
            OA_CHECK(control.rect.width >= oa::ui::kit::compact_metrics.button_width);
        }
    }
}

} // namespace

int main() {
    check_gate();
    check_coaches(oa::ui::kit::SizeClass::compact, 800, 500, false);
    check_coaches(oa::ui::kit::SizeClass::regular, 1024, 640, false);
    check_coaches(oa::ui::kit::SizeClass::large, 1280, 720, false);
    check_coaches(oa::ui::kit::SizeClass::large, 1280, 720, true);
    check_keys();
    check_wrap_click_and_buttons();
    check_lookup();
    check_leave_word();
    check_centred_language();
    return oa::test::check_exit_status();
}
