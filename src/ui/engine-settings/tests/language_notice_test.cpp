// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The missing-language notice: its parts in order at each size class, the
// body wrapped inside the notice, Download greyed until it can be pressed,
// the focus on that button first and Tab from Use English to it, the reach
// of both buttons, and the failed state's texts. English is looked up;
// a line that is not English is not.

#include "oa/ui/engine_settings/language_notice.hpp"

#include "oa/data/languages.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/test/check.hpp"
#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/components_more.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/looks.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/kit/theme.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace settings = oa::ui::engine_settings;
namespace kit = oa::ui::kit;
namespace languages = oa::data::languages;

/// The language the notice is about in these tests.
constexpr std::string_view kTag = "zh-Hans";
/// Its name in itself.
constexpr std::string_view kEndonym = "\347\256\200\344\275\223\344\270\255\346\226\207";
/// The body in that language when the size is not known.
constexpr std::string_view kOwnBody = "\347\256\200\344\275\223\344\270\255\346\226\207\347\216\260"
                                      "\345\234\250\351\234\200\350\246\201"
                                      "\345\215\225\347\213\254\344\270\213\350\275\275\343\200\202"
                                      "\345\256\211\350\243\205\345\256\214"
                                      "\346\210\220\345\211\215\357\274\214\346\270\270\346\210\217"
                                      "\345\260\206\346\230\276\347\244\272"
                                      "\350\213\261\346\226\207\343\200\202";
/// Download, kept whole.
constexpr std::string_view kDownload = "\344\270\213\350\275\275 \302\267 Download";
/// The line when it cannot be downloaded.
constexpr std::string_view kUnavailable =
    "\347\216\260\345\234\250\346\227\240\346\263\225\344\270\213\350\275\275\343\200\202";
/// The line when the download failed.
constexpr std::string_view kFailed = "\344\270\213\350\275\275\345\244\261\350\264\245\343\200\202";
/// Try again, kept whole.
constexpr std::string_view kRetry = "\351\207\215\350\257\225 \302\267 Try again";
/// The body in English when the size is not known.
constexpr std::string_view kEnglish =
    "Chinese (Simplified) is now a separate download. Until it is installed, the game shows "
    "English.";
/// The English line when it cannot be downloaded.
constexpr std::string_view kCannot = "It cannot be downloaded now.";

/// Returns the notice while Download cannot be pressed.
///
/// @return the notice
settings::LanguageNotice unavailable_notice() {
    settings::LanguageNotice notice;
    notice.tag = std::string(kTag);
    notice.title = std::string(kEndonym);
    notice.own_body = std::string(kOwnBody);
    notice.english_body = std::string(kEnglish);
    notice.status = std::string(kUnavailable) + "\n" + std::string(kCannot);
    notice.download_label = std::string(kDownload);
    notice.download_enabled = false;
    return notice;
}

/// Returns a frame of a size class.
///
/// @param size_class the class
/// @param width the frame's width
/// @param height the frame's height
/// @return the frame
kit::Frame frame_of(kit::SizeClass size_class, int32_t width, int32_t height) {
    kit::Frame frame;
    frame.size_class = size_class;
    frame.area = {0, 0, width, height};
    return frame;
}

/// Returns the control of a name.
///
/// @param list the display list
/// @param name the control's name
/// @return the control; null when the list has none
const kit::Control* named(const kit::DisplayList& list, std::string_view name) {
    for (const kit::Control& control : list.controls)
        if (control.name == name)
            return &control;
    return nullptr;
}

/// Returns the fill the notice is drawn on.
///
/// @param list the display list
/// @return the fill; null when the list has none
const kit::Item* fill_of(const kit::DisplayList& list) {
    for (const kit::Item& item : list.items)
        if (item.role == kit::Role::fill)
            return &item;
    return nullptr;
}

/// Returns a text with its spaces removed.
///
/// @param text the text
/// @return the text
std::string squash(std::string_view text) {
    std::string out;
    for (const char byte : text)
        if (byte != ' ')
            out.push_back(byte);
    return out;
}

/// Requires the parts in order, and the body inside the notice.
///
/// @param list the display list
/// @param notice the notice
/// @param wrapped true when the language's body takes more than one line
void require_parts(
    const kit::DisplayList& list, const settings::LanguageNotice& notice, bool wrapped
) {
    const kit::Item* fill = fill_of(list);
    OA_CHECK(fill != nullptr);
    if (fill == nullptr)
        return;
    int header_at = -1;
    int title_at = -1;
    int own_at = -1;
    int english_at = -1;
    int status_at = -1;
    int button_at = -1;
    std::string own;
    int own_lines = 0;
    std::string english;
    bool english_quiet = true;
    bool saw_english = false;
    bool status_quiet = false;
    for (std::size_t index = 0; index < list.items.size(); ++index) {
        const kit::Item& item = list.items[index];
        const auto at = static_cast<int>(index);
        if (item.role == kit::Role::header && item.text == "Language")
            header_at = at;
        if (item.role == kit::Role::text) {
            OA_CHECK(item.rect.x >= 0);
            OA_CHECK(item.rect.x + item.rect.width <= fill->rect.width);
            if (item.font == kit::FontRole::regular && item.text == notice.title)
                title_at = at;
        }
        if (item.role == kit::Role::button && button_at < 0)
            button_at = at;
    }
    for (const kit::Item& item : list.items) {
        if (item.role != kit::Role::text || item.font != kit::FontRole::small)
            continue;
        if (own != notice.own_body && notice.own_body.find(item.text) != std::string::npos &&
            item.text != kUnavailable) {
            if (own_at < 0)
                own_at = 1;
            own += item.text;
            ++own_lines;
            continue;
        }
        if (squash(english) != squash(kEnglish) &&
            kEnglish.find(item.text) != std::string_view::npos) {
            if (!saw_english)
                english_quiet = item.colour == kit::colour::quiet;
            saw_english = true;
            english_at = 1;
            english += item.text;
            if (item.colour != kit::colour::quiet)
                english_quiet = false;
            continue;
        }
        if (item.text == kCannot || item.text == kUnavailable) {
            status_at = 1;
            if (item.text == kCannot)
                status_quiet = item.colour == kit::colour::quiet;
        }
    }
    // Order is the items' order: header, title, body, English, status, buttons.
    int stage = 0;
    for (const kit::Item& item : list.items) {
        if (stage == 0 && item.role == kit::Role::header && item.text == "Language")
            stage = 1;
        else if (stage == 1 && item.role == kit::Role::text && item.text == notice.title)
            stage = 2;
        else if (stage == 2 && item.role == kit::Role::text && notice.own_body.find(item.text) == 0)
            stage = 3;
        else if (stage == 3 && item.role == kit::Role::text && kEnglish.find(item.text) == 0)
            stage = 4;
        else if (
            stage == 4 && item.role == kit::Role::text &&
            (item.text == kUnavailable || item.text == kCannot)
        )
            stage = 5;
        else if (stage == 5 && item.role == kit::Role::button)
            stage = 6;
    }
    OA_CHECK(header_at >= 0);
    OA_CHECK(title_at > header_at);
    OA_CHECK(stage == 6);
    OA_CHECK(own_at > 0 && english_at > 0 && status_at > 0);
    OA_CHECK(own == notice.own_body);
    OA_CHECK(squash(english) == squash(kEnglish));
    OA_CHECK(saw_english && english_quiet);
    OA_CHECK(!status_quiet);
    if (wrapped)
        OA_CHECK(own_lines > 1);
    const kit::Control* body = named(list, "language-notice.body");
    OA_CHECK(body != nullptr && body->text.find(notice.own_body) != std::string::npos);
    OA_CHECK(body != nullptr && body->text.find(kEnglish) != std::string::npos);
    const kit::Control* use = named(list, "language-notice.use-english");
    const kit::Control* download = named(list, "language-notice.download");
    OA_CHECK(use != nullptr && download != nullptr);
    if (use == nullptr || download == nullptr)
        return;
    OA_CHECK(list.tab_order.size() == 2);
    OA_CHECK(list.tab_order[0] == use->id);
    OA_CHECK(list.tab_order[1] == download->id);
    bool focused = false;
    bool ring = false;
    for (const kit::Item& item : list.items) {
        if (item.role == kit::Role::button && item.control == download->id && item.state.focused)
            focused = true;
        if (item.role == kit::Role::focus_ring)
            ring = true;
    }
    OA_CHECK(focused);
    OA_CHECK(ring);
    static_cast<void>(button_at);
}

/// Requires Download greyed, and a press on it refused.
///
/// @param list the display list
void require_greyed(const kit::DisplayList& list) {
    const kit::Control* download = named(list, "language-notice.download");
    OA_CHECK(download != nullptr && !download->enabled);
    if (download == nullptr)
        return;
    OA_CHECK(download->text == kDownload);
    bool plain = false;
    for (const kit::Item& item : list.items) {
        if (item.role != kit::Role::button || item.control != download->id)
            continue;
        const auto* look = std::get_if<kit::ButtonLook>(&item.look);
        OA_CHECK(look != nullptr && !look->enabled && look->style == kit::ButtonStyle::plain);
        plain = look != nullptr;
    }
    OA_CHECK(plain);
    const kit::Point centre{
        download->rect.x + download->rect.width / 2, download->rect.y + download->rect.height / 2
    };
    OA_CHECK(kit::hit(list, centre) == kit::no_control);
    kit::Interaction interaction;
    const kit::PointerOutcome down = kit::pointer_down(interaction, list, centre);
    const kit::PointerOutcome up = kit::pointer_up(interaction, list, centre);
    OA_CHECK(down.result != kit::PointerResult::activated);
    OA_CHECK(up.result != kit::PointerResult::activated);
}

/// Requires Tab and the arrows to move between the two buttons.
///
/// @param list the display list
void require_keys(const kit::DisplayList& list) {
    const kit::Control* use = named(list, "language-notice.use-english");
    const kit::Control* download = named(list, "language-notice.download");
    OA_CHECK(use != nullptr && download != nullptr);
    if (use == nullptr || download == nullptr)
        return;
    kit::Interaction interaction;
    interaction.focused = download->id;
    interaction.focus_shown = true;
    OA_CHECK(kit::key(interaction, list, kit::Key::tab).result == kit::KeyResult::redraw);
    OA_CHECK(interaction.focused == use->id);
    OA_CHECK(kit::key(interaction, list, kit::Key::tab).result == kit::KeyResult::redraw);
    OA_CHECK(interaction.focused == download->id);
}

/// Requires Left and Right to move between the two buttons.
///
/// Arrows pass a button that takes no press, so both buttons are enabled.
///
/// @param list the display list
void require_arrows(const kit::DisplayList& list) {
    const kit::Control* use = named(list, "language-notice.use-english");
    const kit::Control* download = named(list, "language-notice.download");
    OA_CHECK(use != nullptr && download != nullptr && use->enabled && download->enabled);
    if (use == nullptr || download == nullptr)
        return;
    kit::Interaction interaction;
    interaction.focused = download->id;
    interaction.focus_shown = true;
    OA_CHECK(kit::key(interaction, list, kit::Key::left).result == kit::KeyResult::redraw);
    OA_CHECK(interaction.focused == use->id);
    OA_CHECK(kit::key(interaction, list, kit::Key::right).result == kit::KeyResult::redraw);
    OA_CHECK(interaction.focused == download->id);
}

/// Requires a finger just above a button to reach it.
///
/// @param list the display list
/// @param control the button
void require_reach(const kit::DisplayList& list, const kit::Control& control) {
    const kit::Point above{control.rect.x + control.rect.width / 2, control.rect.y - 12};
    const kit::Reached reached = kit::reach(list, above, 22);
    OA_CHECK(reached.control == control.id);
}

/// The parts, the greyed button and the keys, at each size class.
void parts_at_each_size() {
    OA_CHECK(kit::size_class_of(640, 480) == kit::SizeClass::compact);
    const settings::LanguageNotice notice = unavailable_notice();
    const kit::Frame frames[] = {
        frame_of(kit::SizeClass::compact, 640, 480),
        frame_of(kit::SizeClass::regular, 960, 540),
        frame_of(kit::SizeClass::large, 1280, 720),
    };
    for (const kit::Frame& frame : frames) {
        const kit::DisplayList list = settings::layout_language_notice(notice, frame);
        const kit::Item* fill = fill_of(list);
        OA_CHECK(
            fill != nullptr && fill->rect.width == kit::metrics_of(frame.size_class).notice_width
        );
        require_parts(list, notice, frame.size_class == kit::SizeClass::compact);
        require_greyed(list);
        require_keys(list);
    }
}

/// Both buttons answer a finger while Download can be pressed.
void reach_of_both_buttons() {
    settings::LanguageNotice notice = unavailable_notice();
    notice.download_enabled = true;
    notice.status.clear();
    const kit::DisplayList list =
        settings::layout_language_notice(notice, frame_of(kit::SizeClass::compact, 640, 480));
    const kit::Control* use = named(list, "language-notice.use-english");
    const kit::Control* download = named(list, "language-notice.download");
    OA_CHECK(use != nullptr && use->enabled);
    OA_CHECK(download != nullptr && download->enabled);
    if (use == nullptr || download == nullptr)
        return;
    require_reach(list, *use);
    require_reach(list, *download);
    require_arrows(list);
    bool accent = false;
    for (const kit::Item& item : list.items) {
        if (item.role != kit::Role::button || item.control != download->id)
            continue;
        const auto* look = std::get_if<kit::ButtonLook>(&item.look);
        accent = look != nullptr && look->enabled && look->style == kit::ButtonStyle::accent;
    }
    OA_CHECK(accent);
    const kit::Point far{-100, -100};
    OA_CHECK(kit::reach(list, far, 22).control == kit::no_control);
}

/// A failure names itself and offers Try again.
void failed_state() {
    settings::LanguageNotice notice = unavailable_notice();
    notice.failed = true;
    notice.download_enabled = true;
    notice.download_label = std::string(kRetry);
    notice.status = std::string(kFailed) + "\nThe download failed.";
    const kit::DisplayList list =
        settings::layout_language_notice(notice, frame_of(kit::SizeClass::compact, 640, 480));
    bool chinese = false;
    bool english = false;
    bool locked = false;
    for (const kit::Item& item : list.items) {
        if (item.role != kit::Role::text)
            continue;
        if (item.text == kFailed) {
            chinese = true;
            locked = item.colour == kit::colour::lock;
        }
        if (item.text == "The download failed.") {
            english = true;
            OA_CHECK(item.colour == kit::colour::lock);
        }
    }
    OA_CHECK(chinese && english && locked);
    const kit::Control* download = named(list, "language-notice.download");
    OA_CHECK(download != nullptr && download->text == kRetry && download->enabled);
}

/// A bar is shown while a download has come some of the way.
void progress_bar() {
    settings::LanguageNotice notice = unavailable_notice();
    notice.download_enabled = true;
    notice.status.clear();
    notice.progress = 0.5F;
    notice.progress_text = "2.0 MB of 4.0 MB";
    const kit::DisplayList list =
        settings::layout_language_notice(notice, frame_of(kit::SizeClass::compact, 640, 480));
    bool bar = false;
    bool caption = false;
    for (const kit::Item& item : list.items) {
        if (item.role == kit::Role::progress) {
            const auto* look = std::get_if<kit::ProgressLook>(&item.look);
            bar = look != nullptr && look->done == kit::question_progress_whole / 2;
        }
        if (item.role == kit::Role::text && item.text == notice.progress_text) {
            caption = true;
            OA_CHECK(item.colour != kit::colour::quiet);
        }
    }
    OA_CHECK(bar && caption);
}

/// Puts the interface language back when the test leaves.
struct InterfaceGuard {
    /// Remembers English with no catalogue.
    InterfaceGuard() { languages::set_interface_language(nullptr, languages::english()); }

    /// Puts that back.
    ~InterfaceGuard() { languages::set_interface_language(nullptr, languages::english()); }

    InterfaceGuard(const InterfaceGuard&) = delete;
    InterfaceGuard& operator=(const InterfaceGuard&) = delete;
};

/// English is looked up. The download button, which is not English, is not.
void english_is_looked_up() {
    const InterfaceGuard guard;
    languages::InterfaceText catalogue;
    const std::string file = std::string("[Language]\n{\nfr=Langue;\n}\n") +
                             "[Use English]\n{\nfr=En anglais;\n}\n" + "[" + std::string(kEnglish) +
                             "]\n{\nfr=Corps;\n}\n" + "[" + std::string(kCannot) +
                             "]\n{\nfr=Pas maintenant;\n}\n" + "[" + std::string(kDownload) +
                             "]\n{\nfr=SHOULD NOT;\n}\n";
    OA_CHECK(catalogue.add(file));
    const languages::Language* french = languages::find_by_tag("fr");
    OA_CHECK(french != nullptr);
    if (french == nullptr)
        return;
    languages::set_interface_language(&catalogue, *french);
    const kit::DisplayList list = settings::layout_language_notice(
        unavailable_notice(), frame_of(kit::SizeClass::compact, 640, 480)
    );
    bool header = false;
    for (const kit::Item& item : list.items) {
        if (item.role == kit::Role::header && item.text == "Langue")
            header = true;
        if (item.role == kit::Role::text && item.text == "SHOULD NOT")
            OA_CHECK(false);
    }
    const kit::Control* body = named(list, "language-notice.body");
    const kit::Control* use = named(list, "language-notice.use-english");
    const kit::Control* download = named(list, "language-notice.download");
    OA_CHECK(header);
    OA_CHECK(body != nullptr && body->text.find(kOwnBody) != std::string::npos);
    OA_CHECK(body != nullptr && body->text.find("SHOULD NOT") == std::string::npos);
    OA_CHECK(use != nullptr && use->text == "En anglais");
    OA_CHECK(download != nullptr && download->text == kDownload);
    OA_CHECK(download != nullptr && download->text.find("SHOULD NOT") == std::string::npos);
}

} // namespace

int main() {
    parts_at_each_size();
    reach_of_both_buttons();
    failed_state();
    progress_bar();
    english_is_looked_up();
    return oa::test::check_exit_status();
}
