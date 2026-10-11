// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Downloads' Registries part: the Registries row's value and MANAGE…, every
// row's avatar, name, chip and subtitle in each state, built-in registries
// without REMOVE or ADD MIRROR…, REMOVE and UNDO, Cancel putting the
// switches and removals back, the changes OK yields, Escape and the link
// going back to Downloads, the locks in a game, the offer keeping the
// player's choices, and the parts at Compact, Regular and Large. Every name
// is unique.

#include "oa/test/check.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"

#include "geometry.hpp"
#include "registries_page.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace settings = oa::ui::engine_settings;
namespace geometry = oa::ui::engine_settings::geometry;
namespace registries = oa::ui::engine_settings::registries;
namespace kit = oa::ui::kit;
namespace renderer = oa::ui::frontend_renderer;

using settings::DialogAction;
using settings::DialogKey;
using settings::Page;

/// The classes every layout is checked at.
constexpr std::array<kit::SizeClass, 3> kClasses{
    kit::SizeClass::compact, kit::SizeClass::regular, kit::SizeClass::large
};

/// Returns a registry row.
///
/// @param id the id
/// @param name the name
/// @param built_in it ships with the game
/// @param on its switch
/// @return the row
settings::RegistryRow registry_row(std::string id, std::string name, bool built_in, bool on) {
    settings::RegistryRow row;
    row.id = std::move(id);
    row.name = std::move(name);
    row.built_in = built_in;
    row.on = on;
    return row;
}

/// The built-in registry, checked four minutes ago.
///
/// @return the row
settings::RegistryRow core_prime() {
    settings::RegistryRow row = registry_row("coreprime", "Core Prime", true, true);
    row.address = "downloads.coreprime.net";
    row.packages = 218;
    row.checked_ago = 4 * 60;
    return row;
}

/// An added registry of maps.
///
/// @return the row
settings::RegistryRow example_maps() {
    settings::RegistryRow row = registry_row("example-maps", "Example Maps", false, true);
    row.address = "maps.example.org";
    row.packages = 31;
    row.checked_ago = 60;
    return row;
}

/// An added registry on a laptop, turned off.
///
/// @return the row
settings::RegistryRow laptop() {
    settings::RegistryRow row = registry_row("kirbys-laptop", "Kirby's laptop", false, false);
    row.address = "192.168.1.20";
    row.packages = 4;
    return row;
}

/// The three registries the mockup shows.
///
/// @return the offer
settings::RegistriesOffer three() {
    settings::RegistriesOffer offer;
    offer.registries = {core_prime(), example_maps(), laptop()};
    return offer;
}

/// Opens Downloads over an offer of registries, on its main part.
///
/// @param offer the registries
/// @return the dialog
settings::Dialog opened(const settings::RegistriesOffer& offer) {
    settings::Dialog dialog;
    settings::open_dialog(dialog, {}, {}, {}, "0.8", Page::downloads);
    settings::set_registries_offer(dialog, offer);
    return dialog;
}

/// Opens Downloads' Registries part over an offer of registries.
///
/// @param offer the registries
/// @return the dialog
settings::Dialog part(const settings::RegistriesOffer& offer) {
    settings::Dialog dialog = opened(offer);
    registries::show(dialog);
    return dialog;
}

/// Tells whether the layout draws a text.
///
/// @param dialog the dialog
/// @param text the text
/// @return true when a part shows it
bool layout_has(const settings::Dialog& dialog, std::string_view text) {
    for (const settings::LayoutPart& part : settings::dialog_layout(dialog))
        if (part.text == text)
            return true;
    return false;
}

/// Tells whether the dialog's display list draws a text.
///
/// @param dialog the dialog
/// @param text the text
/// @return true when an item shows it
bool list_has(const settings::Dialog& dialog, std::string_view text) {
    for (const kit::Item& item : geometry::dialog_list(dialog, nullptr).items)
        if (item.text == text)
            return true;
    return false;
}

/// Returns the control of a name in the dialog's list, or null.
///
/// @param list the list
/// @param name the name, without oa.
/// @return the control
const kit::Control* named(const kit::DisplayList& list, std::string_view name) {
    return kit::control_of(list, kit::control_named(list, name));
}

/// Tells whether the dialog lists a control of a name.
///
/// @param dialog the dialog
/// @param name the name
/// @return true when it does
bool has_control(const settings::Dialog& dialog, std::string_view name) {
    return named(geometry::dialog_list(dialog, nullptr), name) != nullptr;
}

/// Returns a rectangle's middle.
///
/// @param rect the rectangle
/// @return its middle point
kit::Point centre(const renderer::SourceRect& rect) {
    return {rect.x + rect.width / 2, rect.y + rect.height / 2};
}

/// Clicks a point of the dialog.
///
/// @param[in,out] dialog the dialog
/// @param point the point
/// @return what the release asked
DialogAction click(settings::Dialog& dialog, kit::Point point) {
    static_cast<void>(settings::dialog_pointer_move(dialog, point.x, point.y));
    static_cast<void>(settings::dialog_pointer_down(dialog, point.x, point.y));
    return settings::dialog_pointer_up(dialog, point.x, point.y);
}

/// Clicks a named control, or a named part of one, where it lies.
///
/// @param[in,out] dialog the dialog
/// @param name the control's or part's name
/// @return what the release asked; none when nothing has the name
DialogAction click_named(settings::Dialog& dialog, std::string_view name) {
    const kit::DisplayList list = geometry::dialog_list(dialog, nullptr);
    for (const kit::AutomationEntry& entry : kit::automation_parts(list, {}))
        if (entry.name == name)
            return click(dialog, centre(entry.rect));
    std::fprintf(stderr, "no control is named %s\n", std::string(name).c_str());
    OA_CHECK(false);
    return DialogAction::none;
}

/// Returns the subtitle the layout shows under a registry's name.
///
/// @param dialog the dialog, its Registries part showing
/// @param index the registry's place
/// @return the subtitle as the part places it
std::string shown_subtitle(const settings::Dialog& dialog, std::size_t index) {
    return registries::subtitle(dialog.registries.registries[index]);
}

/// Checks that every name a list gives is unique and well formed.
///
/// @param list the list
/// @return the names
std::set<std::string> names_of(const kit::DisplayList& list) {
    OA_CHECK(kit::name_problem(list).empty());
    std::set<std::string> names;
    for (const kit::AutomationEntry& entry : kit::automation_parts(list, {}))
        OA_CHECK(names.insert(entry.name).second);
    return names;
}

void the_downloads_row_counts_and_manage_opens_the_part() {
    settings::Dialog dialog = opened(three());
    OA_CHECK(registries::summary(dialog) == "2 on of 3");
    OA_CHECK(layout_has(dialog, "Registries"));
    OA_CHECK(list_has(dialog, "2 on of 3"));
    OA_CHECK(has_control(dialog, "settings.registries.manage"));
    OA_CHECK(!registries::shown(dialog));
    OA_CHECK(click_named(dialog, "settings.registries.manage") == DialogAction::redraw);
    OA_CHECK(registries::shown(dialog));
    OA_CHECK(dialog.downloads_part == settings::DownloadsPart::registries);
    OA_CHECK(layout_has(dialog, "DOWNLOADS · REGISTRIES"));
    OA_CHECK(layout_has(dialog, "‹ DOWNLOADS"));
    OA_CHECK(!layout_has(dialog, "Check for content updates"));

    // Only the built-in registry on: its name and "only".
    settings::RegistriesOffer alone = three();
    alone.registries[1].on = false;
    OA_CHECK(registries::summary(opened(alone)) == "Core Prime only");
    // A registry marked for removal is not counted.
    settings::Dialog marked = part(three());
    marked.registry_chosen[1].removed = true;
    OA_CHECK(registries::summary(marked) == "Core Prime only");
    OA_CHECK(registries::summary(opened({})) == "None");
}

void rows_show_each_state() {
    OA_CHECK(registries::initials("Core Prime") == "CP");
    OA_CHECK(registries::initials("Example Maps") == "EM");
    OA_CHECK(registries::initials("Kirby's laptop") == "KL");
    OA_CHECK(registries::initials("ridge") == "RI");
    OA_CHECK(registries::initials("North archive of maps") == "NAO");
    OA_CHECK(registries::initials("  spaced  out  ") == "SO");
    OA_CHECK(registries::initials("简体中文") == "简体");
    OA_CHECK(registries::initials("") == "");

    OA_CHECK(registries::age_text(10) == "just now");
    OA_CHECK(registries::age_text(4 * 60) == "4 min ago");
    OA_CHECK(registries::age_text(3 * 3600 + 5) == "3 h ago");
    OA_CHECK(registries::age_text(30 * 3600) == "1 day ago");
    OA_CHECK(registries::age_text(5 * 86400) == "5 days ago");

    OA_CHECK(registries::subtitle(core_prime()) == "218 packages · checked 4 min ago");
    settings::RegistryRow never = core_prime();
    never.checked_ago.reset();
    OA_CHECK(registries::subtitle(never) == "218 packages · not checked yet");
    settings::RegistryRow unreached = core_prime();
    unreached.unreachable = true;
    unreached.checked_ago = 2 * 86400;
    OA_CHECK(
        registries::subtitle(unreached) == "218 packages · couldn't reach · list from 2 days ago"
    );
    OA_CHECK(
        registries::subtitle(example_maps()) == "maps.example.org · 31 packages · not reviewed"
    );
    settings::RegistryRow mirrored = example_maps();
    mirrored.mirrors = 2;
    OA_CHECK(
        registries::subtitle(mirrored) ==
        "maps.example.org · 31 packages · not reviewed · 2 mirrors"
    );
    mirrored.mirrors = 1;
    mirrored.unreachable = true;
    OA_CHECK(
        registries::subtitle(mirrored) ==
        "maps.example.org · 31 packages · not reviewed · 1 mirror · couldn't "
        "reach · list from 1 min ago"
    );
    settings::RegistryRow one = laptop();
    one.packages = 1;
    OA_CHECK(registries::subtitle(one) == "192.168.1.20 · 1 package · not reviewed");

    // The part shows each row's avatar, name, chip and subtitle.
    const settings::Dialog dialog = part(three());
    OA_CHECK(layout_has(dialog, "CP"));
    OA_CHECK(layout_has(dialog, "Core Prime"));
    OA_CHECK(layout_has(dialog, "Built in"));
    OA_CHECK(layout_has(dialog, "EM"));
    OA_CHECK(layout_has(dialog, "Example Maps"));
    OA_CHECK(layout_has(dialog, "KL"));
    const kit::DisplayList list = geometry::dialog_list(dialog, nullptr);
    std::size_t chips = 0;
    bool core_subtitle = false;
    for (const kit::Item& item : list.items) {
        chips += item.role == kit::Role::chip ? 1U : 0U;
        core_subtitle = core_subtitle || item.text == shown_subtitle(dialog, 0);
    }
    OA_CHECK(chips == 1);
    OA_CHECK(core_subtitle);

    // The note names the built-in registries, or says none is reviewed.
    OA_CHECK(
        registries::note_text(dialog) ==
        "Content from registries other than Core Prime isn't reviewed by the OA team. OA still "
        "checks every package before using it."
    );
    settings::RegistriesOffer two_built_in = three();
    two_built_in.registries.insert(
        two_built_in.registries.begin() + 1, registry_row("north", "North", true, true)
    );
    OA_CHECK(
        registries::note_text(opened(two_built_in)) ==
        "Content from registries other than Core Prime and North isn't reviewed by the OA team. "
        "OA still checks every package before using it."
    );
    settings::RegistriesOffer none_built_in;
    none_built_in.registries = {example_maps()};
    OA_CHECK(
        registries::note_text(opened(none_built_in)) ==
        "Content from these registries isn't reviewed by the OA team. OA still checks every "
        "package before using it."
    );
    // The last OK's failure shows over the note, in amber.
    settings::RegistriesOffer told = three();
    told.notice = "OA could not change the registries: the list could not be read.";
    const settings::Dialog notice = part(told);
    bool amber = false;
    for (const kit::Item& item : geometry::dialog_list(notice, nullptr).items)
        amber = amber || (item.text.rfind("OA could not change", 0) == 0 &&
                          item.colour == kit::colour::lock);
    OA_CHECK(amber);
}

void built_in_registries_have_no_remove_or_mirror() {
    const settings::Dialog dialog = part(three());
    const std::set<std::string> names = names_of(geometry::dialog_list(dialog, nullptr));
    OA_CHECK(names.count("settings.registries.back") == 1);
    OA_CHECK(names.count("settings.registry.coreprime") == 1);
    OA_CHECK(names.count("settings.registry.coreprime.off") == 1);
    OA_CHECK(names.count("settings.registry.coreprime.on") == 1);
    OA_CHECK(names.count("settings.registry.coreprime.remove") == 0);
    OA_CHECK(names.count("settings.registry.coreprime.add-mirror") == 0);
    OA_CHECK(names.count("settings.registry.example-maps") == 1);
    OA_CHECK(names.count("settings.registry.example-maps.remove") == 1);
    OA_CHECK(names.count("settings.registry.example-maps.add-mirror") == 1);
    OA_CHECK(names.count("settings.registry.kirbys-laptop.remove") == 1);
    OA_CHECK(names.count("settings.add-registry") == 1);
    OA_CHECK(names.count("settings.restore-defaults") == 0);
    OA_CHECK(names.count("settings.cancel") == 1);
    OA_CHECK(names.count("settings.ok") == 1);

    // Space on a built-in registry's unused numbers does nothing.
    settings::Dialog keys = part(three());
    keys.focused = settings::registry_remove_control(0);
    OA_CHECK(settings::dialog_key(keys, DialogKey::space) == DialogAction::none);
    OA_CHECK(settings::registry_changes(keys).empty());
    keys.focused = settings::registry_mirror_control(0);
    OA_CHECK(settings::dialog_key(keys, DialogKey::space) == DialogAction::none);
}

void remove_undo_and_cancel() {
    settings::Dialog dialog = part(three());
    OA_CHECK(click_named(dialog, "settings.registry.example-maps.remove") == DialogAction::redraw);
    OA_CHECK(dialog.registry_chosen[1].removed);
    OA_CHECK(layout_has(dialog, "Removed when you press OK"));
    OA_CHECK(layout_has(dialog, "UNDO"));
    OA_CHECK(has_control(dialog, "settings.registry.example-maps.undo"));
    OA_CHECK(!has_control(dialog, "settings.registry.example-maps.remove"));
    OA_CHECK(!has_control(dialog, "settings.registry.example-maps.add-mirror"));
    // The switch of a registry marked for removal takes no press.
    OA_CHECK(!has_control(dialog, "settings.registry.example-maps"));
    OA_CHECK(click_named(dialog, "settings.registry.example-maps.undo") == DialogAction::redraw);
    OA_CHECK(!dialog.registry_chosen[1].removed);
    OA_CHECK(!layout_has(dialog, "Removed when you press OK"));

    // Removed and switched, then Cancel: both go back.
    OA_CHECK(click_named(dialog, "settings.registry.kirbys-laptop.remove") == DialogAction::redraw);
    OA_CHECK(click_named(dialog, "settings.registry.coreprime.off") == DialogAction::redraw);
    OA_CHECK(!dialog.registry_chosen[0].on);
    OA_CHECK(
        click(dialog, centre(geometry::footer_button(settings::cancel_control))) ==
        DialogAction::cancelled
    );
    OA_CHECK(dialog.registry_chosen[0].on);
    OA_CHECK(!dialog.registry_chosen[2].removed);
    OA_CHECK(settings::registry_changes(dialog).empty());
}

void ok_yields_the_changes() {
    settings::Dialog dialog = part(three());
    OA_CHECK(settings::registry_changes(dialog).empty());
    // Switches by click, by Space and by the arrows.
    OA_CHECK(click_named(dialog, "settings.registry.coreprime.off") == DialogAction::redraw);
    dialog.focused = settings::registry_switch_control(1);
    OA_CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::redraw);
    OA_CHECK(!dialog.registry_chosen[1].on);
    dialog.focused = settings::registry_switch_control(2);
    OA_CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::redraw);
    OA_CHECK(dialog.registry_chosen[2].on);
    OA_CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::none);
    OA_CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::redraw);
    OA_CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::redraw);
    // A removal wins over the switch.
    OA_CHECK(click_named(dialog, "settings.registry.kirbys-laptop.remove") == DialogAction::redraw);
    // Nothing applies before OK: the offer still says what is stored.
    OA_CHECK(dialog.registries.registries[0].on && dialog.registries.registries[1].on);
    OA_CHECK(
        click(dialog, centre(geometry::footer_button(settings::ok_control))) ==
        DialogAction::accepted
    );
    const std::vector<settings::RegistryChange> changes = settings::registry_changes(dialog);
    OA_CHECK(changes.size() == 3);
    if (changes.size() == 3) {
        OA_CHECK(changes[0].id == "coreprime");
        OA_CHECK(changes[0].kind == settings::RegistryChangeKind::turn_off);
        OA_CHECK(changes[1].id == "example-maps");
        OA_CHECK(changes[1].kind == settings::RegistryChangeKind::turn_off);
        OA_CHECK(changes[2].id == "kirbys-laptop");
        OA_CHECK(changes[2].kind == settings::RegistryChangeKind::remove);
    }
    // A built-in registry is never removed, whatever the choice says.
    settings::Dialog forced = part(three());
    forced.registry_chosen[0].removed = true;
    OA_CHECK(settings::registry_changes(forced).empty());
    // Enter is OK in the part too.
    settings::Dialog entered = part(three());
    entered.registry_chosen[1].on = false;
    OA_CHECK(settings::dialog_key(entered, DialogKey::enter) == DialogAction::accepted);
    OA_CHECK(settings::registry_changes(entered).size() == 1);
}

void the_offer_keeps_the_players_choices() {
    settings::Dialog dialog = part(three());
    dialog.registry_chosen[1].on = false;
    dialog.registry_chosen[2].removed = true;
    // A registry added while the dialog is open joins as it was added.
    settings::RegistriesOffer more = three();
    settings::RegistryRow north = registry_row("north", "North", false, true);
    north.address = "north.example.net";
    more.registries.push_back(north);
    settings::set_registries_offer(dialog, more);
    OA_CHECK(dialog.registry_chosen.size() == 4);
    OA_CHECK(!dialog.registry_chosen[1].on);
    OA_CHECK(dialog.registry_chosen[2].removed);
    OA_CHECK(dialog.registry_chosen[3].on && !dialog.registry_chosen[3].removed);
    const std::vector<settings::RegistryChange> changes = settings::registry_changes(dialog);
    OA_CHECK(changes.size() == 2);
    // Cancel keeps the new registry as it was added.
    OA_CHECK(settings::dialog_key(dialog, DialogKey::escape) == DialogAction::redraw);
    OA_CHECK(settings::dialog_key(dialog, DialogKey::escape) == DialogAction::cancelled);
    OA_CHECK(dialog.registry_chosen.size() == 4 && dialog.registry_chosen[3].on);
    OA_CHECK(settings::registry_changes(dialog).empty());
    // A registry no longer listed drops out.
    settings::set_registries_offer(dialog, three());
    OA_CHECK(dialog.registry_chosen.size() == 3);
}

void escape_and_the_link_go_back_to_downloads() {
    settings::Dialog dialog = part(three());
    OA_CHECK(settings::dialog_key(dialog, DialogKey::escape) == DialogAction::redraw);
    OA_CHECK(!registries::shown(dialog) && dialog.page == Page::downloads);
    OA_CHECK(has_control(dialog, "settings.registries.manage"));
    OA_CHECK(settings::dialog_key(dialog, DialogKey::escape) == DialogAction::cancelled);

    dialog = part(three());
    OA_CHECK(click_named(dialog, "settings.registries.back") == DialogAction::redraw);
    OA_CHECK(!registries::shown(dialog));

    // With the focus shown: MANAGE… puts it on the link, and going back puts
    // it on MANAGE….
    dialog = opened(three());
    OA_CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    dialog.focused = settings::first_row_control + 2;
    OA_CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::redraw);
    OA_CHECK(registries::shown(dialog));
    OA_CHECK(dialog.focused == settings::registries_back_control);
    OA_CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::redraw);
    OA_CHECK(!registries::shown(dialog));
    OA_CHECK(dialog.focused == settings::first_row_control + 2);

    // Another section, then Downloads again, shows Downloads' own rows.
    dialog = part(three());
    OA_CHECK(
        click_named(dialog, "settings.nav.developer") == DialogAction::redraw &&
        dialog.page == Page::developer
    );
    OA_CHECK(click_named(dialog, "settings.nav.downloads") == DialogAction::redraw);
    OA_CHECK(!registries::shown(dialog));
}

void add_registry_and_add_mirror_ask_the_host() {
    settings::Dialog dialog = part(three());
    OA_CHECK(click_named(dialog, "settings.add-registry") == DialogAction::add_registry);
    OA_CHECK(
        click_named(dialog, "settings.registry.example-maps.add-mirror") == DialogAction::add_mirror
    );
    OA_CHECK(dialog.mirror_request == "example-maps");
    dialog.focused = settings::registry_mirror_control(2);
    OA_CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::add_mirror);
    OA_CHECK(dialog.mirror_request == "kirbys-laptop");
    dialog.focused = settings::add_registry_control;
    OA_CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::add_registry);
    // Neither changes what OK does.
    OA_CHECK(settings::registry_changes(dialog).empty());
}

void a_game_locks_adding() {
    settings::Dialog dialog = part(three());
    dialog.locks = settings::settings_locks(settings::GameState{true, false, false, false});
    OA_CHECK(registries::adding_locked(dialog));
    OA_CHECK(layout_has(dialog, "Choose from the main menu."));
    OA_CHECK(!has_control(dialog, "settings.add-registry"));
    OA_CHECK(!has_control(dialog, "settings.registry.example-maps.add-mirror"));
    OA_CHECK(has_control(dialog, "settings.registry.example-maps.remove"));
    dialog.focused = settings::add_registry_control;
    OA_CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::none);
    dialog.focused = settings::registry_mirror_control(1);
    OA_CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::none);
    OA_CHECK(
        click(dialog, centre(geometry::footer_button(settings::restore_control))) !=
        DialogAction::add_registry
    );
    // Switches and removals still wait for OK.
    OA_CHECK(click_named(dialog, "settings.registry.example-maps.off") == DialogAction::redraw);
    OA_CHECK(settings::registry_changes(dialog).size() == 1);
    static_cast<void>(names_of(geometry::dialog_list(dialog, nullptr)));
}

/// Checks that a dialog's parts lie inside its face, apart, and inside the
/// view where they meet it.
///
/// @param dialog the dialog
void parts_inside(const settings::Dialog& dialog) {
    const geometry::Sizes& sized = geometry::sizes_of(dialog);
    const renderer::SourceRect face{
        geometry::edge,
        geometry::edge,
        sized.dialog_width - 2 * geometry::edge,
        sized.dialog_height - 2 * geometry::edge,
    };
    const std::vector<settings::LayoutPart> parts = settings::dialog_layout(dialog);
    OA_CHECK(!parts.empty());
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const renderer::SourceRect& rect = parts[index].rect;
        OA_CHECK(rect.width > 0 && rect.height > 0);
        OA_CHECK(kit::wholly_in(rect, face));
        if (kit::intersect(rect, sized.view).width > 0 &&
            kit::intersect(rect, sized.view).height > 0)
            OA_CHECK(kit::wholly_in(rect, sized.view) || rect.y < sized.view.y);
        for (std::size_t other = index + 1; other < parts.size(); ++other) {
            const renderer::SourceRect shared = kit::intersect(rect, parts[other].rect);
            if (shared.width > 0 && shared.height > 0) {
                std::fprintf(
                    stderr,
                    "overlap: '%s' and '%s'\n",
                    parts[index].text.c_str(),
                    parts[other].text.c_str()
                );
                OA_CHECK(false);
            }
        }
    }
}

void the_part_lies_inside_at_every_class() {
    for (const kit::SizeClass size_class : kClasses) {
        settings::RegistriesOffer many = three();
        for (int32_t index = 0; index < 6; ++index) {
            settings::RegistryRow row = example_maps();
            row.id = "extra-" + std::to_string(index);
            row.name = "Extra registry with a long name, number " + std::to_string(index);
            row.mirrors = 3;
            row.unreachable = true;
            many.registries.push_back(row);
        }
        many.notice = "OA could not change the registries: the list could not be read.";
        settings::Dialog dialog = part(many);
        dialog.size_class = size_class;
        parts_inside(dialog);
        static_cast<void>(names_of(geometry::dialog_list(dialog, nullptr)));
        // Many registries scroll; the end shows the note.
        OA_CHECK(settings::scroll_limit(dialog) > 0);
        dialog.scroll[static_cast<std::size_t>(Page::downloads)] = settings::scroll_limit(dialog);
        parts_inside(dialog);
        // Tab reaches the last registry's ADD MIRROR…, which scrolls into view.
        dialog.scroll[static_cast<std::size_t>(Page::downloads)] = 0;
        dialog.focused = settings::registry_mirror_control(many.registries.size() - 2);
        OA_CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
        OA_CHECK(dialog.focused == settings::registry_switch_control(many.registries.size() - 1));
        OA_CHECK(dialog.scroll[static_cast<std::size_t>(Page::downloads)] > 0);
        // In a game, too.
        settings::Dialog locked = part(three());
        locked.size_class = size_class;
        locked.locks = settings::settings_locks(settings::GameState{true, false, false, false});
        parts_inside(locked);
        // The main part with its Registries row.
        settings::Dialog main = opened(three());
        main.size_class = size_class;
        parts_inside(main);
    }
}

} // namespace

int main() {
    the_downloads_row_counts_and_manage_opens_the_part();
    rows_show_each_state();
    built_in_registries_have_no_remove_or_mirror();
    remove_undo_and_cancel();
    ok_yields_the_changes();
    the_offer_keeps_the_players_choices();
    escape_and_the_link_go_back_to_downloads();
    add_registry_and_add_mirror_ask_the_host();
    a_game_locks_adding();
    the_part_lies_inside_at_every_class();
    return oa::test::check_exit_status();
}
