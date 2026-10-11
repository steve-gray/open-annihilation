// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Language page. Sizes, asides and the menu are decided here. The host
// fills Dialog::available_languages; this file only reads it.

#include "language_page.hpp"

#include "oa/data/languages.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace oa::ui::engine_settings {

namespace {

namespace languages = oa::data::languages;

/// The gap between an aside's words, a middle dot.
constexpr std::string_view kDot = " \u00B7 ";

/// Returns a coverage as a whole percent, rounded down, from 0 to 100.
///
/// @param coverage the fraction, from 0 to 1
/// @return the percent, such as "92%"; empty when coverage is not a number
std::string coverage_percent(double coverage) {
    if (!std::isfinite(coverage))
        return {};
    const double scaled = coverage * 100.0 + 1e-9;
    if (scaled <= 0.0)
        return "0%";
    if (scaled >= 100.0)
        return "100%";
    return std::to_string(static_cast<int>(scaled)) + "%";
}

} // namespace

std::string download_size_text(uint64_t bytes) {
    constexpr uint64_t kB = 1000;
    constexpr uint64_t one_mb = 1000 * 1000;
    if (bytes < one_mb)
        return std::to_string(bytes / kB) + " " + std::string(geometry::shown_text("kB"));
    if (bytes < 100 * one_mb) {
        const uint64_t tenths = bytes * 10 / one_mb;
        return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " " +
               std::string(geometry::shown_text("MB"));
    }
    return std::to_string(bytes / one_mb) + " " + std::string(geometry::shown_text("MB"));
}

std::string language_download_aside(
    bool reviewed,
    std::string_view publisher,
    std::optional<double> coverage,
    uint64_t bytes,
    bool downloads_on
) {
    if (!downloads_on)
        return std::string(geometry::shown_text("downloads off"));
    const std::string size = download_size_text(bytes);
    if (!reviewed) {
        std::string aside = std::string(geometry::shown_text("not reviewed")) + std::string(kDot);
        if (coverage.has_value()) {
            const std::string percent = coverage_percent(*coverage);
            if (!percent.empty())
                aside += percent + std::string(kDot);
        }
        return aside + size;
    }
    if (publisher == "OA team")
        return size + " \u21E9";
    std::string aside = std::string(geometry::shown_text("community")) + std::string(kDot);
    if (coverage.has_value()) {
        const std::string percent = coverage_percent(*coverage);
        if (!percent.empty())
            aside += percent + std::string(kDot);
    }
    return aside + size;
}

namespace geometry {

namespace {

/// Returns the language the operating system's preferred locales choose.
///
/// @param model the model
/// @return the dialog's system language, or English
const languages::Language& system_language(const SettingsModel& model) {
    if (model.dialog != nullptr && model.dialog->system_language != nullptr)
        return *model.dialog->system_language;
    return languages::english();
}

/// Tells whether a menu item is the divider above the packs to download.
///
/// @param model the model
/// @param index the item, from 0
/// @return true when it is
bool divider_at(const SettingsModel& model, int32_t index) noexcept {
    return model.dialog != nullptr && !model.dialog->available_languages.empty() &&
           index == language_installed_count();
}

/// Returns the available pack a menu item lists.
///
/// @param model the model
/// @param index the item, from 0
/// @return the pack; null for an installed language, the divider and past the menu
const AvailableLanguage* available_at(const SettingsModel& model, int32_t index) noexcept {
    if (model.dialog == nullptr || model.dialog->available_languages.empty())
        return nullptr;
    const int32_t at = index - (language_installed_count() + 1);
    const auto& packs = model.dialog->available_languages;
    if (at < 0 || static_cast<std::size_t>(at) >= packs.size())
        return nullptr;
    return &packs[static_cast<std::size_t>(at)];
}

/// Returns the name a pack shows: its endonym, or its English name when the
/// endonym cannot be drawn.
///
/// @param pack the pack
/// @return the name
std::string shown_name(const AvailableLanguage& pack) {
    if (pack.endonym_drawable && !pack.endonym.empty())
        return pack.endonym;
    if (!pack.english_name.empty())
        return pack.english_name;
    return pack.tag;
}

/// Returns a pack listed as available whose tag is chosen and is not installed.
///
/// @param dialog the dialog
/// @return the pack; null when the choice is installed or not listed
const AvailableLanguage* pending_language(const Dialog& dialog) {
    for (const languages::Language* offered : offered_languages())
        if (offered->tag == dialog.chosen.language)
            return nullptr;
    for (const AvailableLanguage& pack : dialog.available_languages)
        if (pack.tag == dialog.chosen.language)
            return &pack;
    return nullptr;
}

/// Returns the state a closed field shows beside a pack's name.
///
/// @param pack the pack
/// @return "waiting", a percent, "installing" or "failed"; empty before a download
std::string field_state(const AvailableLanguage& pack) {
    switch (pack.state) {
    case AvailableLanguage::State::offered:
        return {};
    case AvailableLanguage::State::waiting:
        return std::string(shown_text("waiting"));
    case AvailableLanguage::State::downloading:
        return std::to_string(std::min<unsigned>(pack.percent, 100)) + "%";
    case AvailableLanguage::State::installing:
        return std::string(shown_text("installing"));
    case AvailableLanguage::State::failed:
        return std::string(shown_text("failed"));
    }
    return {};
}

/// Holds an index to the menu.
///
/// @param index the index
/// @param count the items
/// @return the index, from 0 to count - 1
int32_t held(int32_t index, int32_t count) noexcept {
    return std::clamp(index, int32_t{0}, std::max(count - 1, int32_t{0}));
}

} // namespace

int32_t language_installed_count() {
    return 1 + static_cast<int32_t>(offered_languages().size());
}

int32_t language_count(const SettingsModel& model) {
    const int32_t installed = language_installed_count();
    if (model.dialog == nullptr || model.dialog->available_languages.empty())
        return installed;
    return installed + 1 + static_cast<int32_t>(model.dialog->available_languages.size());
}

int32_t language_index(const SettingsModel& model) {
    const std::string_view tag = model.settings->language;
    const auto offered = offered_languages();
    for (std::size_t index = 0; index < offered.size(); ++index)
        if (offered[index]->tag == tag)
            return static_cast<int32_t>(index) + 1;
    if (model.dialog != nullptr) {
        const int32_t first = language_installed_count() + 1;
        const auto& packs = model.dialog->available_languages;
        for (std::size_t index = 0; index < packs.size(); ++index)
            if (packs[index].tag == tag)
                return first + static_cast<int32_t>(index);
    }
    return 0;
}

void set_language(SettingsModel& model, int32_t index) {
    const int32_t choice = held(index, language_count(model));
    if (divider_at(model, choice))
        return;
    EngineSettings& settings = *model.settings;
    if (const AvailableLanguage* pack = available_at(model, choice)) {
        settings.language = pack->tag;
        return;
    }
    if (choice == 0) {
        settings.language = std::string(languages::system_choice);
    } else {
        const auto offered = offered_languages();
        settings.language = std::string(offered[static_cast<std::size_t>(choice - 1)]->tag);
    }
    if (languages::chosen_language(settings.language, system_language(model)).needs ==
        languages::TextNeeds::modern_fonts)
        settings.modern_fonts = true;
}

std::string language_caption(const SettingsModel& model, int32_t index) {
    if (index < 0 || index >= language_count(model))
        return {};
    if (divider_at(model, index))
        return std::string(shown_text("AVAILABLE TO DOWNLOAD"));
    if (index == 0)
        return std::string(shown_text("System default")) + " (" +
               std::string(system_language(model).endonym) + ")";
    if (const AvailableLanguage* pack = available_at(model, index))
        return shown_name(*pack);
    return std::string(offered_languages()[static_cast<std::size_t>(index - 1)]->endonym);
}

std::string language_id(const SettingsModel& model, int32_t index) {
    if (index < 0 || index >= language_count(model) || divider_at(model, index))
        return {};
    if (index == 0)
        return word_form(languages::system_choice, false);
    if (const AvailableLanguage* pack = available_at(model, index))
        return word_form(pack->tag, false);
    return word_form(offered_languages()[static_cast<std::size_t>(index - 1)]->tag, false);
}

std::string language_hint(const SettingsModel& model, std::size_t line) {
    if (model.dialog != nullptr)
        if (const AvailableLanguage* pending = pending_language(*model.dialog)) {
            if (line != 0)
                return {};
            if (pending->state == AvailableLanguage::State::failed)
                return std::string(shown_text("The download failed: ")) + pending->failure +
                       std::string(shown_text(". Choose it again to try again."));
            return "Downloads, then switches to it.";
        }
    if (line == 0)
        return "The game's own text and unit names, where its";
    if (line == 1)
        return "data has them in the language.";
    return {};
}

std::size_t language_hint_lines(const SettingsModel& model) {
    if (model.dialog != nullptr && pending_language(*model.dialog) != nullptr)
        return 1;
    return 2;
}

int32_t language_mark_toward(const Dialog& dialog, int32_t marked, int32_t step) {
    const int32_t last = std::max(language_count(reading(dialog.chosen, dialog)), int32_t{0}) - 1;
    if (last < 0)
        return 0;
    const int32_t divider = dialog.available_languages.empty() ? -1 : language_installed_count();
    const auto is_divider = [divider](int32_t index) noexcept { return index == divider; };
    if (step == 0)
        return std::clamp(marked, int32_t{0}, last);
    const int32_t direction = step > 0 ? 1 : -1;
    const int32_t target = std::clamp(marked + step, int32_t{0}, last);
    if (!is_divider(target))
        return target;
    const int32_t skipped = target + direction;
    if (skipped < 0 || skipped > last || is_divider(skipped))
        return std::clamp(marked, int32_t{0}, last);
    return skipped;
}

LanguagePick language_pick(const Dialog& dialog, int32_t item) {
    const SettingsModel model = reading(dialog.chosen, dialog);
    if (item < 0 || item >= language_count(model))
        return {};
    if (divider_at(model, item))
        return {LanguagePickKind::refused, {}};
    if (const AvailableLanguage* pack = available_at(model, item)) {
        if (!pack->choosable)
            return {LanguagePickKind::refused, {}};
        return {LanguagePickKind::download, pack->tag};
    }
    return {};
}

std::string language_closed_field(const Dialog& dialog, std::string current) {
    const AvailableLanguage* pending = pending_language(dialog);
    if (pending == nullptr)
        return current;
    const std::string name = std::string(shown_text(shown_name(*pending)));
    const std::string state = field_state(*pending);
    if (state.empty())
        return name;
    return name + " \u00B7 " + state;
}

bool language_item_divider(const Dialog& dialog, Setting setting, int32_t index) {
    if (setting != Setting::language)
        return false;
    return divider_at(reading(dialog.chosen, dialog), index);
}

bool language_item_choosable(const Dialog& dialog, Setting setting, int32_t index) {
    if (setting != Setting::language)
        return true;
    const SettingsModel model = reading(dialog.chosen, dialog);
    if (divider_at(model, index))
        return false;
    if (const AvailableLanguage* pack = available_at(model, index))
        return pack->choosable;
    return true;
}

std::string language_item_aside(const Dialog& dialog, Setting setting, int32_t index) {
    if (setting != Setting::language)
        return {};
    if (const AvailableLanguage* pack = available_at(reading(dialog.chosen, dialog), index))
        return pack->aside;
    return {};
}

void fill_language_menu(const Dialog& dialog, Setting setting, kit::ChoiceMenuLook& look) {
    if (setting != Setting::language)
        return;
    const SettingsModel model = reading(dialog.chosen, dialog);
    const int32_t count = language_count(model);
    look.items.resize(static_cast<std::size_t>(std::max(count, int32_t{0})));
    for (int32_t index = 0; index < count; ++index) {
        kit::ChoiceMenuRow& row = look.items[static_cast<std::size_t>(index)];
        row.aside.clear();
        row.id.clear();
        row.divider = false;
        row.choosable = true;
        if (divider_at(model, index)) {
            row.divider = true;
            row.choosable = false;
            continue;
        }
        row.id = language_id(model, index);
        if (const AvailableLanguage* pack = available_at(model, index)) {
            row.aside = pack->aside;
            row.choosable = pack->choosable;
        }
    }
}

} // namespace geometry

} // namespace oa::ui::engine_settings
