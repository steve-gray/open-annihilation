// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The first-run steps: their layout, their keys and pointer, and their drawing.
// Words are looked up as the screen is laid out. On the language step a built-in
// or installed language in the middle is read from the same catalogue, and the
// language the game shows stays as it was until the player chooses.

#include "oa/ui/first_run/first_run.hpp"

#include "oa/data/languages/interface_text.hpp"
#include "oa/ui/kit/canvas.hpp"
#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/components_more.hpp"
#include "oa/ui/kit/controls.hpp"
#include "oa/ui/kit/text.hpp"

#include <algorithm>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace oa::ui::first_run {

void draw_sample(
    const kit::Canvas& canvas, const kit::Rect& inset, const Model& model, kit::SizeClass size_class
);

namespace {

using oa::data::languages::interface_text;

/// The room inside the setup chrome, from the window's top left.
struct Band {
    int32_t left{};
    int32_t top{};
    int32_t width{};
    int32_t height{};
};

/// Returns the band the step's own words share, under the header and above the footer.
///
/// @param width the window's width, in points
/// @param height the window's height, in points
/// @return the band
Band content_band(int32_t width, int32_t height) noexcept {
    const int32_t left = kit::compact_metrics.edge + kit::compact_metrics.padding;
    const int32_t top = kit::compact_metrics.edge + kit::compact_metrics.header_height + 1 +
                        kit::compact_metrics.padding;
    const int32_t bottom = height - kit::compact_metrics.edge - kit::compact_metrics.footer_height -
                           1 - kit::compact_metrics.padding;
    Band band;
    band.left = left;
    band.top = top;
    band.width = std::max(width - 2 * left, int32_t{0});
    band.height = std::max(bottom - top, int32_t{0});
    return band;
}

/// Looks a word up and keeps a string the look can own.
///
/// A language reads that word from the installed catalogue. Null reads the
/// language the game is showing.
///
/// @param english the catalogue text
/// @param language the language to read it in; null for the one in use
/// @return the words to show
std::string
show(std::string_view english, const oa::data::languages::Language* language = nullptr) {
    if (language != nullptr)
        return std::string(oa::data::languages::interface_text_in(english, *language));
    return std::string(interface_text(english));
}

/// The language the language step's own words show in: the card in the middle
/// when the game can read it now. A pack that is not installed yet, and any
/// other step, leave the words in the language in use.
///
/// @param session the session
/// @return the language; null to keep the language in use
const oa::data::languages::Language* centred_language(const Session& session) noexcept {
    if (session.model.step != Step::language || session.carousel.selected < 0)
        return nullptr;
    const auto index = static_cast<std::size_t>(session.carousel.selected);
    if (index >= session.model.cards.size())
        return nullptr;
    const Card& card = session.model.cards[index];
    if (card.kind != CardKind::built_in && card.kind != CardKind::installed)
        return nullptr;
    const oa::data::languages::Language* centred = oa::data::languages::find_by_tag(card.tag);
    if (centred == nullptr || !oa::data::languages::playable(*centred))
        return nullptr;
    return centred;
}

/// Adds one line of text.
///
/// @param[in,out] list the display list
/// @param rect the line
/// @param text the words
/// @param font which font
/// @param ink the colour
void add_text(
    kit::DisplayList& list,
    const kit::Rect& rect,
    std::string text,
    kit::FontRole font,
    kit::Colour ink,
    kit::Align align = kit::Align::left
) {
    if (rect.width <= 0 || rect.height <= 0 || text.empty())
        return;
    kit::Item item;
    item.role = kit::Role::text;
    item.rect = rect;
    item.text = std::move(text);
    item.font = font;
    item.colour = ink;
    item.align = align;
    list.items.push_back(std::move(item));
}

/// Measures a line. A font that reports nothing falls back to the estimate,
/// so a paragraph still wraps when the game font has no glyphs.
///
/// @param fonts the fonts; null estimates
/// @param font which font
/// @return the measure
kit::Measure measure_of(const kit::Fonts* fonts, kit::FontRole font) {
    return [fonts, font](std::string_view words) {
        if (fonts == nullptr || words.empty())
            return kit::estimated_width(words);
        const int32_t measured = kit::text_width(*fonts, font, words);
        if (measured <= 0)
            return kit::estimated_width(words);
        return measured;
    };
}

/// Breaks a paragraph into lines and adds them. Returns the row under the last line.
///
/// @param[in,out] list the display list
/// @param fonts the fonts; null estimates
/// @param left the lines' left
/// @param top the first line's top
/// @param width the room
/// @param text the paragraph
/// @param font which font
/// @param ink the colour
/// @return the row under the last line
int32_t add_paragraph(
    kit::DisplayList& list,
    const kit::Fonts* fonts,
    int32_t left,
    int32_t top,
    int32_t width,
    std::string_view text,
    kit::FontRole font,
    kit::Colour ink,
    kit::Align align = kit::Align::left
) {
    const kit::Measure measure = measure_of(fonts, font);
    std::vector<std::string> lines = kit::wrap(text, std::max(width, int32_t{1}), measure);
    if (lines.empty() && !text.empty())
        lines.emplace_back(text);
    const int32_t line_height = font == kit::FontRole::small ? kit::compact_metrics.small_line
                                                             : kit::compact_metrics.regular_line;
    int32_t row = top;
    for (std::string& line : lines) {
        add_text(list, {left, row, width, line_height}, std::move(line), font, ink, align);
        row += line_height;
    }
    return row;
}

/// How tall a wrapped paragraph is.
///
/// @param fonts the fonts; null estimates
/// @param width the room
/// @param text the paragraph
/// @param font which font
/// @return the height, in points
int32_t paragraph_height(
    const kit::Fonts* fonts, int32_t width, std::string_view text, kit::FontRole font
) {
    const kit::Measure measure = measure_of(fonts, font);
    const std::vector<std::string> lines = kit::wrap(text, std::max(width, int32_t{1}), measure);
    const int32_t count = std::max(static_cast<int32_t>(lines.size()), int32_t{1});
    const int32_t line_height = font == kit::FontRole::small ? kit::compact_metrics.small_line
                                                             : kit::compact_metrics.regular_line;
    return count * line_height;
}

/// Adds a button the chrome does not draw, and remembers it for Tab.
///
/// @param[in,out] list the display list
/// @param[in,out] order Tab's order
/// @param rect the button
/// @param id its number
/// @param name its automation name
/// @param caption its words
/// @param enabled false while a press does not reach it
/// @param style which look
void add_button(
    kit::DisplayList& list,
    std::vector<kit::ControlId>& order,
    const kit::Rect& rect,
    kit::ControlId id,
    std::string name,
    std::string caption,
    bool enabled,
    kit::ButtonStyle style
) {
    kit::ButtonLook look;
    look.caption = caption;
    look.style = style;
    look.enabled = enabled;
    kit::Item item;
    item.role = kit::Role::button;
    item.rect = rect;
    item.text = caption;
    item.look = look;
    list.items.push_back(std::move(item));
    kit::Control control;
    control.id = id;
    control.rect = rect;
    control.enabled = enabled;
    control.name = std::move(name);
    control.kind = kit::ControlKind::button;
    control.focusable = true;
    control.text = std::move(caption);
    list.controls.push_back(std::move(control));
    order.push_back(id);
}

/// The step's word in an automation name.
///
/// @param step the step
/// @return language, online or buttons
std::string_view step_word(Step step) noexcept {
    return step_token(step);
}

/// An automation name, first-run.<step>.<control>.
///
/// @param step the step
/// @param control the control's word
/// @return the name
std::string control_name(Step step, std::string_view control) {
    std::string name = "first-run.";
    name += step_word(step);
    name += '.';
    name += control;
    return name;
}

/// A tag as an automation word: lower case, and anything else a hyphen.
///
/// @param tag the language's tag
/// @return the word
std::string tag_word(std::string_view tag) {
    std::string word;
    word.reserve(tag.size());
    for (const char letter : tag) {
        const auto unit = static_cast<unsigned char>(letter);
        if (unit >= 'A' && unit <= 'Z')
            word.push_back(static_cast<char>(unit - 'A' + 'a'));
        else if ((unit >= 'a' && unit <= 'z') || (unit >= '0' && unit <= '9'))
            word.push_back(static_cast<char>(unit));
        else
            word.push_back('-');
    }
    if (word.empty())
        word = "pack";
    return word;
}

/// The badge's letters: the tag's first two, in capitals.
///
/// @param tag the language's tag
/// @return one or two letters
std::string badge_letters(std::string_view tag) {
    std::string letters;
    for (const char letter : tag) {
        const auto unit = static_cast<unsigned char>(letter);
        if (unit >= 'a' && unit <= 'z')
            letters.push_back(static_cast<char>(unit - 'a' + 'A'));
        else if (unit >= 'A' && unit <= 'Z')
            letters.push_back(static_cast<char>(unit));
        if (letters.size() == 2)
            break;
    }
    if (letters.empty())
        letters = "EN";
    return letters;
}

/// The pill's colours.
///
/// @param card the card
/// @return the availability
kit::LanguageAvailability availability_of(const Card& card) noexcept {
    if (card.current)
        return kit::LanguageAvailability::active;
    if (card.kind != CardKind::pack)
        return kit::LanguageAvailability::built_in;
    switch (card.pack) {
    case PackState::downloading:
        return kit::LanguageAvailability::downloading;
    case PackState::checking:
        return kit::LanguageAvailability::checking;
    case PackState::failed:
        return kit::LanguageAvailability::failed;
    case PackState::offline:
        return kit::LanguageAvailability::offline;
    case PackState::ready:
        return kit::LanguageAvailability::get;
    }
    return kit::LanguageAvailability::get;
}

/// The pill's words.
///
/// @param card the card
/// @param language the language the step is speaking; null for the one in use
/// @return the words
std::string pill_text(const Card& card, const oa::data::languages::Language* language) {
    if (card.current)
        return show("Active", language);
    if (card.kind == CardKind::built_in)
        return show("Built in", language);
    if (card.kind == CardKind::installed)
        return show("Installed", language);
    switch (card.pack) {
    case PackState::downloading:
        return show("Downloading", language);
    case PackState::checking:
        return show("Checking…", language);
    case PackState::failed:
        return show("Couldn't download", language);
    case PackState::offline:
        return show("Offline", language);
    case PackState::ready:
        break;
    }
    std::string pill = show("Get", language);
    if (!card.size_text.empty()) {
        pill += " · ";
        pill += card.size_text;
    }
    return pill;
}

/// The line under a card's names: a download's progress, or why it failed.
///
/// @param card the card
/// @param language the language the step is speaking; null for the one in use
/// @return the line; empty when the card has none
std::string facts_text(const Card& card, const oa::data::languages::Language* language) {
    if (card.kind == CardKind::pack && card.pack == PackState::downloading) {
        std::string line = show("Downloading", language);
        line += ' ';
        line += card.own_name;
        line += " · ";
        line += card.progress_done_text;
        line += ' ';
        line += show("of", language);
        line += ' ';
        line += card.progress_size_text;
        return line;
    }
    if (card.kind == CardKind::pack && card.pack == PackState::failed)
        return card.failure;
    return {};
}

/// Fills the setup chrome's look for a step.
///
/// @param session the session
/// @param width the window's width
/// @param height the window's height
/// @param back_enabled Back takes a press
/// @param next_enabled Next takes a press
/// @param language the language the language step is speaking; null on the other steps
/// @return the look
/// The page word beside the title: Setup before the game is mounted, Welcome after.
///
/// @param session the session
/// @param language the language the language step is speaking; null otherwise
/// @return the word
std::string page_word(const Session& session, const oa::data::languages::Language* language) {
    const bool setup_page =
        session.model.walk == Walk::setup &&
        (session.model.step == Step::language || session.model.step == Step::game ||
         session.model.step == Step::copying);
    return show(setup_page ? "Setup" : "Welcome", language);
}

kit::SetupChromeLook chrome_look(
    const Session& session,
    int32_t width,
    int32_t height,
    bool back_enabled,
    bool next_enabled,
    const oa::data::languages::Language* language
) {
    kit::SetupChromeLook look;
    look.width = width;
    look.height = height;
    look.page = page_word(session, language);
    const StepPlace place = step_place(session.model.step, session.model.walk);
    look.step_name = show(place.name, language);
    look.step_pattern = language != nullptr ? show(kit::step_line_pattern, language)
                                            : std::string(interface_text(kit::step_line_pattern));
    look.step_index = place.index;
    look.step_count = place.count;
    // One word for the whole walk. The host chooses it. Copying does not
    // swap in Cancel: Escape still returns to the game step.
    look.skip = show(session.model.leave == Leave::quit ? "Quit" : "Skip", language);
    look.back = show("Back", language);
    look.next = show("Next", language);
    look.skip_enabled = true;
    look.back_enabled = back_enabled;
    look.next_enabled = next_enabled;
    const Step step = session.model.step;
    if (step == Step::game) {
        look.back_enabled = true;
        look.next = show("Copy the game", language);
        look.next_enabled =
            session.model.source >= 0 &&
            session.model.source < static_cast<int32_t>(session.model.sources.size());
    } else if (step == Step::copying) {
        look.back_enabled = false;
        if (session.model.copy.checked || session.model.copy.paused) {
            look.next = show("Continue", language);
            look.next_enabled = true;
        } else {
            look.next = show("Pause", language);
            look.next_enabled = session.model.copy.running;
        }
    }
    return look;
}

/// Adds the chrome and its three buttons.
///
/// @param[in,out] scene the scene
/// @param[in,out] order Tab's order
/// @param session the session
/// @param width the window's width
/// @param height the window's height
/// @param back_enabled Back takes a press
/// @param next_enabled Next takes a press
/// @param language the language the language step is speaking; null on the other steps
void add_chrome(
    Scene& scene,
    std::vector<kit::ControlId>& order,
    const Session& session,
    int32_t width,
    int32_t height,
    bool back_enabled,
    bool next_enabled,
    const oa::data::languages::Language* language
) {
    const kit::SetupChromeLook look =
        chrome_look(session, width, height, back_enabled, next_enabled, language);
    kit::add_setup_chrome(scene.list, {0, 0}, look);
    const kit::SetupButtons buttons = kit::setup_buttons(nullptr, look);
    const Step step = session.model.step;
    kit::add_setup_controls(
        scene.list,
        {0, 0},
        buttons,
        look,
        skip_id,
        control_name(step, "skip"),
        back_id,
        control_name(step, "back"),
        next_id,
        control_name(step, "next")
    );
    order.push_back(skip_id);
    order.push_back(back_id);
    order.push_back(next_id);
}

/// Draws the floor the language cards stand on: a grid, denser toward its top.
///
/// @param[in,out] list the display list
/// @param width the window's width
/// @param top the grid's top
/// @param bottom the grid's bottom
void add_floor(kit::DisplayList& list, int32_t width, int32_t top, int32_t bottom) {
    if (width <= 0 || bottom - top < 8)
        return;
    constexpr int32_t kRows = 5;
    for (int32_t row = 0; row <= kRows; ++row) {
        const int32_t y = top + (bottom - top) * row * row / (kRows * kRows);
        kit::Item rule;
        rule.role = kit::Role::rule;
        rule.rect = {0, y, width, 1};
        rule.colour = kit::colour::edge_light;
        list.items.push_back(std::move(rule));
    }
    constexpr int32_t kColumns = 12;
    for (int32_t column = 0; column <= kColumns; ++column) {
        const int32_t x = width * column / kColumns;
        kit::Item rule;
        rule.role = kit::Role::rule;
        rule.rect = {x, top, 1, bottom - top};
        rule.colour = kit::colour::edge_light;
        list.items.push_back(std::move(rule));
    }
}

/// The largest Library window that fits a room, at that window's own class.
struct SampleFit {
    kit::SizeClass size_class{kit::SizeClass::compact};
    int32_t width{};
    int32_t height{};
};

/// Picks a Library window that fits a room so the picture is not stretched.
///
/// @param room_width the room's width
/// @param room_height the room's height
/// @return the window
SampleFit fit_sample(int32_t room_width, int32_t room_height) {
    constexpr int32_t kChrome = 2 + 2 * kit::picture_well_inset;
    const kit::SizeClass order[] = {
        kit::SizeClass::large, kit::SizeClass::regular, kit::SizeClass::compact
    };
    for (const kit::SizeClass size_class : order) {
        const kit::Metrics& metrics = kit::metrics_of(size_class);
        if (metrics.dialog_width + kChrome <= room_width &&
            metrics.dialog_height + kChrome <= room_height)
            return {size_class, metrics.dialog_width, metrics.dialog_height};
    }
    return {
        kit::SizeClass::compact,
        std::max(room_width - kChrome, int32_t{1}),
        std::max(room_height - kChrome, int32_t{1}),
    };
}

/// Moves the carousel one card, wrapping from either end when it has more than one.
///
/// @param[in,out] model the carousel
/// @param next true moves to the next card, false to the previous
/// @return true when the selection changed
bool step_carousel(kit::CarouselModel& model, bool next) {
    if (model.count <= 1)
        return false;
    // The unwrapped index keeps the direction when two cards make both ways
    // the same length. carousel_go turns that into one step.
    const kit::CarouselCommand command{!next, next, false};
    return kit::carousel_command(model, command) != kit::CarouselEvent::none;
}

/// Lays the language step out.
///
/// @param[in,out] scene the scene
/// @param session the session
/// @param frame the room
/// @param fonts the fonts
void lay_language(
    Scene& scene, const Session& session, const kit::Frame& frame, const kit::Fonts* fonts
) {
    const int32_t width = frame.area.width;
    const int32_t height = frame.area.height;
    const bool busy = download_busy(session.model);
    const oa::data::languages::Language* const preview = centred_language(session);
    std::vector<kit::ControlId> order;
    add_chrome(scene, order, session, width, height, false, !busy, preview);

    const Band band = content_band(width, height);
    const kit::Metrics& metrics = kit::metrics_of(frame.size_class);
    constexpr int32_t kTitle = 40;
    scene.title_text = show("Welcome to Open Annihilation", preview);
    scene.title = {band.left, band.top, band.width, kTitle};
    int32_t row = band.top + kTitle + 4;
    add_text(
        scene.list,
        {band.left, row, band.width, kit::compact_metrics.small_line},
        show("Choose your language.", preview),
        kit::FontRole::small,
        kit::colour::hint,
        kit::Align::centre
    );
    row += kit::compact_metrics.small_line + 8;

    constexpr int32_t kArrow = 28;
    const std::string note = show(
        "Other languages download from Core Prime. The first download makes a random install ID, "
        "which the next step explains.",
        preview
    );
    const int32_t note_width = std::max(band.width, int32_t{1});
    const int32_t note_height = paragraph_height(fonts, note_width, note, kit::FontRole::small);
    std::string status;
    bool show_bar = false;
    bool show_action = false;
    bool action_cancel = false;
    if (session.carousel.selected >= 0 &&
        static_cast<std::size_t>(session.carousel.selected) < session.model.cards.size()) {
        const Card& chosen =
            session.model.cards[static_cast<std::size_t>(session.carousel.selected)];
        status = facts_text(chosen, preview);
        show_bar = chosen.kind == CardKind::pack && chosen.pack == PackState::downloading &&
                   chosen.progress_whole > 0;
        action_cancel = chosen.kind == CardKind::pack && (chosen.pack == PackState::downloading ||
                                                          chosen.pack == PackState::checking);
        show_action =
            action_cancel || (chosen.kind == CardKind::pack && chosen.pack == PackState::failed);
    }
    std::string hint;
    if (session.model.catalogue == CatalogueNote::unread ||
        session.model.catalogue == CatalogueNote::downloads_off) {
        hint =
            session.model.catalogue == CatalogueNote::downloads_off
                ? show("Downloads are off, so language packs stay offline.", preview)
                : show("The catalogue could not be read, so language packs stay offline.", preview);
    }
    int32_t under = 8 + note_height;
    if (!status.empty())
        under += kit::compact_metrics.small_line + 4;
    if (show_bar)
        under += metrics.progress_bar_height + 4;
    if (show_action)
        under += kit::compact_metrics.button_height + 4;
    if (!hint.empty())
        under += 4 + paragraph_height(fonts, note_width, hint, kit::FontRole::small);
    const int32_t footer_limit = height - kit::compact_metrics.edge -
                                 kit::compact_metrics.footer_height - 1 -
                                 kit::compact_metrics.padding;
    const kit::CarouselGeometry geometry = kit::carousel_fit(width, metrics);
    constexpr int32_t kFloor = 48;
    const int32_t band_height = geometry.card_height + kFloor;
    const int32_t carousel_height = band_height + kit::carousel_pager_band;
    int32_t band_y = height / 2 - band_height / 2;
    if (band_y < row)
        band_y = row;
    if (band_y + carousel_height + under > footer_limit)
        band_y = std::max(row, footer_limit - under - carousel_height);
    scene.carousel = {0, band_y, width, carousel_height};
    const int32_t arrow_y = band_y + band_height / 2 - kArrow / 2;
    scene.previous = {4, arrow_y, kArrow, kArrow};
    scene.next_arrow = {width - 4 - kArrow, arrow_y, kArrow, kArrow};
    add_floor(scene.list, width, band_y + band_height / 2, band_y + band_height);

    kit::CarouselLook carousel;
    carousel.card_width = geometry.card_width;
    carousel.card_height = geometry.card_height;
    carousel.step = geometry.step;
    carousel.centre_milli = kit::carousel_index_milli(session.carousel);
    carousel.reduced_motion = frame.reduced_motion;
    carousel.wrap = session.model.cards.size() > 1;
    carousel.cards.reserve(session.model.cards.size());
    for (const Card& card : session.model.cards) {
        kit::CarouselCard face;
        face.letters = badge_letters(card.tag);
        face.own_name = card.own_name;
        face.english_name = card.english_name;
        face.facts = facts_text(card, preview);
        face.pill = pill_text(card, preview);
        face.availability = availability_of(card);
        face.disabled =
            busy && card.pack != PackState::downloading && card.pack != PackState::checking;
        carousel.cards.push_back(std::move(face));
    }
    kit::add_carousel(scene.list, scene.carousel, carousel);

    const kit::Rect places_band = kit::carousel_band(scene.carousel);
    scene.places = kit::carousel_places(
        places_band,
        geometry,
        static_cast<int32_t>(session.model.cards.size()),
        carousel.centre_milli,
        frame.reduced_motion,
        carousel.wrap
    );
    std::vector<kit::CarouselSlot> left_to_right = scene.places;
    std::sort(
        left_to_right.begin(),
        left_to_right.end(),
        [](const kit::CarouselSlot& left, const kit::CarouselSlot& right) {
            return left.face.x < right.face.x;
        }
    );
    const bool wraps = session.carousel.count > 1;
    const bool has_previous = wraps;
    const bool has_next = wraps;
    kit::add_carousel_control(
        scene.list,
        scene.previous,
        previous_id,
        control_name(Step::language, "previous"),
        show("Previous", preview),
        has_previous
    );
    order.push_back(previous_id);
    for (const kit::CarouselSlot& slot : left_to_right) {
        if (slot.index < 0 || static_cast<std::size_t>(slot.index) >= session.model.cards.size())
            continue;
        const Card& card = session.model.cards[static_cast<std::size_t>(slot.index)];
        const bool enabled = !busy;
        kit::add_carousel_control(
            scene.list,
            slot.face,
            card_id + slot.index,
            control_name(Step::language, "card." + tag_word(card.tag)),
            card.own_name,
            enabled
        );
        order.push_back(card_id + slot.index);
    }
    kit::add_carousel_control(
        scene.list,
        scene.next_arrow,
        next_card_id,
        control_name(Step::language, "next-card"),
        show("Next", preview),
        has_next
    );
    order.push_back(next_card_id);

    int32_t row_under = scene.carousel.y + scene.carousel.height + 8;
    if (!status.empty()) {
        add_text(
            scene.list,
            {band.left, row_under, band.width, kit::compact_metrics.small_line},
            status,
            kit::FontRole::small,
            kit::colour::text,
            kit::Align::centre
        );
        row_under += kit::compact_metrics.small_line + 4;
    }
    if (show_bar && session.carousel.selected >= 0 &&
        static_cast<std::size_t>(session.carousel.selected) < session.model.cards.size()) {
        const Card& chosen =
            session.model.cards[static_cast<std::size_t>(session.carousel.selected)];
        const int32_t bar_width = std::min(geometry.card_width, band.width);
        const kit::Rect bar{
            band.left + std::max((band.width - bar_width) / 2, int32_t{0}),
            row_under,
            bar_width,
            metrics.progress_bar_height
        };
        kit::Item progress;
        progress.role = kit::Role::progress;
        progress.rect = bar;
        progress.look = kit::ProgressLook{chosen.progress_done, chosen.progress_whole};
        scene.list.items.push_back(std::move(progress));
        row_under += metrics.progress_bar_height + 4;
    }
    if (show_action) {
        const std::string caption =
            action_cancel ? show("Cancel download", preview) : show("Retry", preview);
        const int32_t button_width = std::max(kit::compact_metrics.button_width, int32_t{120});
        const int32_t used = std::min(button_width, band.width);
        const kit::Rect button{
            band.left + std::max((band.width - used) / 2, int32_t{0}),
            row_under,
            used,
            kit::compact_metrics.button_height
        };
        add_button(
            scene.list,
            order,
            button,
            action_cancel ? cancel_id : retry_id,
            control_name(Step::language, action_cancel ? "cancel" : "retry"),
            caption,
            true,
            action_cancel ? kit::ButtonStyle::plain : kit::ButtonStyle::accent
        );
        row_under += kit::compact_metrics.button_height + 4;
    }
    const int32_t note_top = add_paragraph(
        scene.list,
        fonts,
        band.left,
        row_under,
        band.width,
        note,
        kit::FontRole::small,
        kit::colour::hint,
        kit::Align::centre
    );
    if (!hint.empty()) {
        add_paragraph(
            scene.list,
            fonts,
            band.left,
            note_top + 4,
            band.width,
            hint,
            kit::FontRole::small,
            kit::colour::quiet,
            kit::Align::centre
        );
    }
    scene.list.tab_order = std::move(order);
}

/// Lays the online-features step out.
///
/// @param[in,out] scene the scene
/// @param session the session
/// @param frame the room
/// @param fonts the fonts
void lay_online(
    Scene& scene, const Session& session, const kit::Frame& frame, const kit::Fonts* fonts
) {
    const int32_t width = frame.area.width;
    const int32_t height = frame.area.height;
    std::vector<kit::ControlId> order;
    add_chrome(scene, order, session, width, height, true, true, nullptr);
    const Band band = content_band(width, height);

    const std::string heading = show("Downloads, updates and a number only you hold");
    const std::string feature_packs =
        show("Get language packs, mods and map packs from Core Prime.");
    const std::string feature_lists =
        show("Lists of what is new are checked as the game starts and when the Library opens.");
    const std::string feature_id = show(
        "A random install ID, one per registry, is sent only when downloading from that registry."
    );
    const std::string panel_body = show(
        "A random 128-bit number, made the first time you download from a registry, not when the "
        "game starts. Each registry that asks has its own. Core Prime asks for one. It is sent "
        "only when asking that registry for a download. Turning it off forgets it, and downloads "
        "from that registry stop. Built-in languages and files you open keep working."
    );
    const std::string on_title = show("Turn on online features");
    const std::string on_hint = show(
        "Check for content updates stays at Automatically, and Core Prime's install ID stays on."
    );
    const std::string off_title = show("Not now");
    const std::string off_hint = show(
        "Check for content updates is set to Never, and Core Prime's install ID is turned off."
    );
    const int32_t card_height = kit::choice_card_height();
    constexpr int32_t kGap = 16;
    constexpr int32_t kChrome = 2 + 2 * kit::picture_well_inset;
    constexpr int32_t kTextFloor = 360;
    const auto column_height = [&](int32_t column) {
        const int32_t room = std::max(column, int32_t{1});
        const int32_t heading_h = paragraph_height(fonts, room, heading, kit::FontRole::regular);
        const int32_t packs_h = paragraph_height(fonts, room, feature_packs, kit::FontRole::small);
        const int32_t lists_h = paragraph_height(fonts, room, feature_lists, kit::FontRole::small);
        const int32_t id_h = paragraph_height(fonts, room, feature_id, kit::FontRole::small);
        const int32_t body_h = paragraph_height(
            fonts, std::max(column - 16, int32_t{1}), panel_body, kit::FontRole::small
        );
        const int32_t panel_h = 8 + kit::compact_metrics.regular_line + 4 + body_h + 8;
        // The card's own height holds one hint line. A wrapped hint grows it.
        const auto hint_extra = [&](std::string_view hint) {
            const int32_t hint_room = std::max(column - 30, int32_t{1});
            const int32_t tall = paragraph_height(fonts, hint_room, hint, kit::FontRole::small);
            return std::max(tall - kit::compact_metrics.small_line, int32_t{0});
        };
        return kit::compact_metrics.small_line + 4 + heading_h + 6 + packs_h + 2 + lists_h + 2 +
               id_h + 8 + panel_h + 8 + card_height * 2 + hint_extra(on_hint) +
               hint_extra(off_hint) + 8;
    };
    const auto place_well = [&](const kit::Rect& well_rect, const SampleFit& fitted) {
        kit::PictureWellLook well;
        well.stand_in = fonts == nullptr;
        kit::add_picture_well(scene.list, well_rect, well);
        const kit::PictureWellLayout placed = kit::picture_well_layout(well_rect, false);
        scene.well_inset = placed.inset;
        scene.sample_class = fitted.size_class;
        scene.sample = fonts != nullptr && placed.inset.width > 8 && placed.inset.height > 8;
    };

    bool beside = false;
    SampleFit fitted{};
    int32_t text_left = band.left;
    int32_t text_top = band.top;
    int32_t text_width = band.width;
    if (band.width >= kTextFloor + kGap + 200 && band.height >= 200) {
        const int32_t well_room = band.width - kTextFloor - kGap;
        fitted = fit_sample(well_room, band.height);
        const kit::Metrics& fitted_metrics = kit::metrics_of(fitted.size_class);
        const bool real_dialog = fitted.width == fitted_metrics.dialog_width &&
                                 fitted.height == fitted_metrics.dialog_height;
        const int32_t well_w = fitted.width + kChrome;
        const int32_t well_h = fitted.height + kChrome;
        const int32_t column = band.width - well_w - kGap;
        if (real_dialog && column >= 280 && well_h <= band.height &&
            column_height(column) <= band.height) {
            beside = true;
            place_well({band.left, band.top, well_w, well_h}, fitted);
            text_left = band.left + well_w + kGap;
            text_width = std::max(band.left + band.width - text_left, int32_t{1});
        }
    }
    if (!beside) {
        const int32_t words = column_height(std::max(band.width, int32_t{1}));
        int32_t room_h = std::max(band.height - words - 8, int32_t{72});
        const int32_t cap = std::max(band.height * 2 / 5, int32_t{72});
        if (room_h > cap)
            room_h = cap;
        fitted = fit_sample(band.width, room_h);
        const int32_t well_w = std::min(fitted.width + kChrome, std::max(band.width, int32_t{1}));
        const int32_t well_h = std::min(fitted.height + kChrome, std::max(room_h, int32_t{72}));
        const kit::Rect well_rect{
            band.left + std::max((band.width - well_w) / 2, int32_t{0}), band.top, well_w, well_h
        };
        place_well(well_rect, fitted);
        text_top = well_rect.y + well_rect.height + 8;
        text_width = std::max(band.width, int32_t{1});
    }

    int32_t row = text_top;
    add_text(
        scene.list,
        {text_left, row, text_width, kit::compact_metrics.small_line},
        show("ONLINE FEATURES"),
        kit::FontRole::small,
        kit::colour::quiet
    );
    row += kit::compact_metrics.small_line + 4;
    row = add_paragraph(
        scene.list,
        fonts,
        text_left,
        row,
        text_width,
        heading,
        kit::FontRole::regular,
        kit::colour::text
    );
    row += 6;
    const std::string features_text[] = {feature_packs, feature_lists, feature_id};
    for (const std::string& line : features_text) {
        row = add_paragraph(
            scene.list,
            fonts,
            text_left,
            row,
            text_width,
            line,
            kit::FontRole::small,
            kit::colour::text
        );
        row += 2;
    }
    row += 6;

    const int32_t panel_body_height = paragraph_height(
        fonts, std::max(text_width - 16, int32_t{1}), panel_body, kit::FontRole::small
    );
    const int32_t panel_height = 8 + kit::compact_metrics.regular_line + 4 + panel_body_height + 8;
    const kit::Rect panel{text_left, row, text_width, panel_height};
    kit::Item ground;
    ground.role = kit::Role::fill;
    ground.rect = panel;
    ground.colour = kit::colour::list;
    scene.list.items.push_back(ground);
    kit::Item edge;
    edge.role = kit::Role::outline;
    edge.rect = panel;
    edge.colour = kit::colour::control_border;
    scene.list.items.push_back(edge);
    add_text(
        scene.list,
        {panel.x + 8,
         panel.y + 6,
         std::max(panel.width - 16, int32_t{0}),
         kit::compact_metrics.regular_line},
        show("Your install ID"),
        kit::FontRole::regular,
        kit::colour::text
    );
    add_paragraph(
        scene.list,
        fonts,
        panel.x + 8,
        panel.y + 6 + kit::compact_metrics.regular_line + 2,
        std::max(panel.width - 16, int32_t{1}),
        panel_body,
        kit::FontRole::small,
        kit::colour::hint
    );
    row += panel_height + 8;

    const int32_t hint_room = std::max(text_width - 30, int32_t{1});
    const auto extra_for = [&](std::string_view hint) {
        return std::max(
            paragraph_height(fonts, hint_room, hint, kit::FontRole::small) -
                kit::compact_metrics.small_line,
            int32_t{0}
        );
    };
    const int32_t on_height = card_height + extra_for(on_hint);
    const int32_t off_height = card_height + extra_for(off_hint);
    const kit::Rect on_rect{text_left, row, text_width, on_height};
    const kit::Rect off_rect{text_left, row + on_height + 8, text_width, off_height};
    kit::ChoiceCardLook on_look;
    on_look.title = on_title;
    on_look.hint = on_hint;
    on_look.chosen = session.model.online_on;
    kit::ChoiceCardLook off_look;
    off_look.title = off_title;
    off_look.hint = off_hint;
    off_look.chosen = !session.model.online_on;
    kit::add_choice_card(scene.list, on_rect, on_look, on_id, control_name(Step::online, "on"));
    kit::add_choice_card(
        scene.list, off_rect, off_look, off_id, control_name(Step::online, "not-now")
    );
    order.insert(order.begin(), off_id);
    order.insert(order.begin(), on_id);
    scene.list.tab_order = std::move(order);
}

/// The short badge a source row shows.
///
/// @param mark the kind of place
/// @return the badge
std::string_view source_badge(SourceMark mark) noexcept {
    switch (mark) {
    case SourceMark::gog:
        return "GOG";
    case SourceMark::package:
        return "PKG";
    case SourceMark::demo:
        return "DEMO";
    case SourceMark::disc:
        return "DISC";
    case SourceMark::folder:
        return "FOLDER";
    }
    return "FOLDER";
}

/// Lays the game step out: the sources that were found, and Locate another.
///
/// @param[in,out] scene the scene
/// @param session the session
/// @param frame the room
/// @param fonts the fonts
void lay_game(
    Scene& scene, const Session& session, const kit::Frame& frame, const kit::Fonts* fonts
) {
    const int32_t width = frame.area.width;
    const int32_t height = frame.area.height;
    std::vector<kit::ControlId> order;
    add_chrome(scene, order, session, width, height, true, true, nullptr);
    const Band band = content_band(width, height);
    const bool narrow = frame.size_class == kit::SizeClass::compact || band.width < 520;
    int32_t column = band.width;
    if (!narrow)
        column = std::min(band.width, int32_t{640});
    const int32_t left = band.left + std::max((band.width - column) / 2, int32_t{0});

    int32_t row = band.top;
    constexpr int32_t kTitle = 36;
    scene.title_text = show("Where is Total Annihilation?");
    scene.title = {left, row, column, kTitle};
    row += kTitle + 4;
    row = add_paragraph(
        scene.list,
        fonts,
        left,
        row,
        column,
        show("OA copies the game once. The installation is left as it is."),
        kit::FontRole::small,
        kit::colour::hint
    );
    row += 10;

    const int32_t card_height = kit::choice_card_height();
    const auto count = static_cast<int32_t>(session.model.sources.size());
    for (int32_t index = 0; index < count; ++index) {
        const GameSource& source = session.model.sources[static_cast<std::size_t>(index)];
        std::string hint = std::string(source_badge(source.mark));
        if (!source.detail.empty()) {
            hint += " · ";
            hint += source.detail;
        }
        if (!source.status.empty()) {
            hint += " · ";
            hint += source.status;
        }
        kit::ChoiceCardLook card;
        card.title = source.title;
        card.hint = std::move(hint);
        card.chosen = session.model.source == index;
        const kit::Rect rect{left, row, column, card_height};
        const kit::ControlId id = source_id + index;
        kit::add_choice_card(
            scene.list, rect, card, id, control_name(Step::game, "source." + std::to_string(index))
        );
        order.insert(order.begin(), id);
        row += card_height + 4;
        int32_t chip_x = left + 8;
        for (const SourceChip& chip : source.chips) {
            if (chip.name.empty())
                continue;
            kit::PillLook pill;
            pill.text = chip.held ? chip.name : show("no {name}");
            if (!chip.held) {
                const std::string token = "{name}";
                const auto at = pill.text.find(token);
                if (at != std::string::npos)
                    pill.text.replace(at, token.size(), chip.name);
            }
            pill.state = chip.held ? kit::PillState::active : kit::PillState::inactive;
            const int32_t pill_w = std::min(
                std::max(static_cast<int32_t>(pill.text.size()) * 7 + 16, int32_t{48}),
                std::max(column - 16, int32_t{48})
            );
            if (chip_x + pill_w > left + column)
                break;
            kit::add_pill(
                scene.list, {chip_x, row, pill_w, kit::pill_height()}, pill, kit::no_control, {}
            );
            chip_x += pill_w + 6;
        }
        if (!source.chips.empty())
            row += kit::pill_height() + 2;
        if (!source.footnote.empty()) {
            row = add_paragraph(
                scene.list,
                fonts,
                left + 8,
                row,
                std::max(column - 16, int32_t{1}),
                source.footnote,
                kit::FontRole::small,
                kit::colour::quiet
            );
        }
        row += 8;
    }

    const int32_t button_height = kit::compact_metrics.button_height;
    const kit::Rect locate_rect{left, row, std::min(column, int32_t{220}), button_height};
    add_button(
        scene.list,
        order,
        locate_rect,
        locate_id,
        control_name(Step::game, "locate"),
        show("Locate another…"),
        true,
        kit::ButtonStyle::plain
    );
    row += button_height + 8;
    add_paragraph(
        scene.list,
        fonts,
        left,
        row,
        column,
        show("Copies go to Documents › Open Annihilation"),
        kit::FontRole::small,
        kit::colour::quiet,
        kit::Align::centre
    );
    if (session.model.controller) {
        add_paragraph(
            scene.list,
            fonts,
            left,
            band.top + band.height - kit::compact_metrics.small_line,
            column,
            show("A continues. B goes back."),
            kit::FontRole::small,
            kit::colour::hint,
            kit::Align::centre
        );
    }
    scene.list.tab_order = std::move(order);
}

/// Lays the copying step out: the overall bar, one row a bundle, and the check.
///
/// @param[in,out] scene the scene
/// @param session the session
/// @param frame the room
/// @param fonts the fonts
void lay_copying(
    Scene& scene, const Session& session, const kit::Frame& frame, const kit::Fonts* fonts
) {
    const int32_t width = frame.area.width;
    const int32_t height = frame.area.height;
    std::vector<kit::ControlId> order;
    add_chrome(scene, order, session, width, height, false, true, nullptr);
    const Band band = content_band(width, height);
    const bool narrow = frame.size_class == kit::SizeClass::compact || band.width < 520;
    int32_t column = narrow ? band.width : std::min(band.width, int32_t{640});
    const int32_t left = band.left + std::max((band.width - column) / 2, int32_t{0});
    const CopyView& copy = session.model.copy;

    int32_t row = band.top;
    constexpr int32_t kTitle = 36;
    scene.title_text = show("Copying Total Annihilation");
    scene.title = {left, row, column, kTitle};
    row += kTitle + 4;
    std::string subtitle;
    if (copy.checked && !copy.verdict.empty())
        subtitle = copy.verdict;
    else if (!copy.from.empty()) {
        subtitle = show("From {place}…");
        const std::string token = "{place}";
        const auto at = subtitle.find(token);
        if (at != std::string::npos)
            subtitle.replace(at, token.size(), copy.from);
        subtitle += " ";
        subtitle += show("Nothing is installed and your files are not changed.");
    } else {
        subtitle = show("Nothing is installed and your files are not changed.");
    }
    row = add_paragraph(
        scene.list, fonts, left, row, column, subtitle, kit::FontRole::small, kit::colour::hint
    );
    row += 8;
    if (!copy.overall.empty()) {
        add_text(
            scene.list,
            {left, row, column, kit::compact_metrics.regular_line},
            copy.overall,
            kit::FontRole::regular,
            kit::colour::text
        );
        row += kit::compact_metrics.regular_line;
    }
    if (!copy.remaining.empty()) {
        add_text(
            scene.list,
            {left, row, column, kit::compact_metrics.small_line},
            copy.remaining,
            kit::FontRole::small,
            kit::colour::quiet
        );
        row += kit::compact_metrics.small_line;
    }
    row += 4;
    kit::add_progress(scene.list, {left, row, column, 8}, copy.done, copy.whole);
    row += 16;

    for (const CopyBundle& bundle : copy.bundles) {
        add_text(
            scene.list,
            {left, row, column, kit::compact_metrics.regular_line},
            bundle.name,
            kit::FontRole::regular,
            kit::colour::text
        );
        row += kit::compact_metrics.regular_line;
        if (!bundle.file.empty()) {
            add_text(
                scene.list,
                {left, row, column, kit::compact_metrics.small_line},
                bundle.file,
                kit::FontRole::small,
                kit::colour::hint
            );
            row += kit::compact_metrics.small_line;
        }
        kit::add_progress(
            scene.list, {left, row, std::max(column / 2, int32_t{80}), 6}, bundle.done, bundle.whole
        );
        row += 8;
        if (!bundle.amount.empty()) {
            add_text(
                scene.list,
                {left, row, column, kit::compact_metrics.small_line},
                bundle.amount,
                kit::FontRole::small,
                kit::colour::quiet
            );
            row += kit::compact_metrics.small_line;
        }
        row += 8;
    }

    add_paragraph(
        scene.list,
        fonts,
        left,
        std::max(row, band.top + band.height - kit::compact_metrics.small_line * 3),
        column,
        show("If OA stops, the copy carries on from here next time."),
        kit::FontRole::small,
        kit::colour::quiet,
        kit::Align::centre
    );
    if (session.model.controller) {
        add_paragraph(
            scene.list,
            fonts,
            left,
            band.top + band.height - kit::compact_metrics.small_line,
            column,
            show("A continues. B goes back."),
            kit::FontRole::small,
            kit::colour::hint,
            kit::Align::centre
        );
    }
    add_text(
        scene.list,
        {left,
         band.top + band.height - kit::compact_metrics.small_line * 2,
         column,
         kit::compact_metrics.small_line},
        show("To Documents › Open Annihilation › Games and Expansions"),
        kit::FontRole::small,
        kit::colour::quiet,
        kit::Align::centre
    );
    scene.list.tab_order = std::move(order);
}

/// How tall a coach card is for a body.
///
/// @param fonts the fonts; null estimates
/// @param width the card's width
/// @param body the body
/// @return the height, in points
int32_t coach_height(const kit::Fonts* fonts, int32_t width, std::string_view body) {
    const int32_t text =
        paragraph_height(fonts, std::max(width - 12, int32_t{1}), body, kit::FontRole::small);
    return 8 + kit::compact_metrics.regular_line + 4 + text + 8;
}

/// Lays the OA-buttons step out, over a clear layer.
///
/// @param[in,out] scene the scene
/// @param session the session
/// @param frame the room
/// @param fonts the fonts
void lay_buttons(
    Scene& scene, const Session& session, const kit::Frame& frame, const kit::Fonts* fonts
) {
    const int32_t width = frame.area.width;
    const int32_t height = frame.area.height;
    const kit::Rect room{0, 0, width, height};
    const StepPlace place = step_place(session.model.step, session.model.walk);
    kit::StepLook step;
    step.name = std::string(interface_text(place.name));
    step.pattern = std::string(interface_text(kit::step_line_pattern));
    step.index = place.index;
    step.count = place.count;
    const std::string kicker = kit::step_line(step);
    const std::string heading = show("Two buttons for everything Open Annihilation adds");
    const std::string paragraph = show(
        "The Library is where you get mods, map packs and languages. Settings is where you change "
        "how Open Annihilation looks and plays. Everything else on this menu is the game's own."
    );
    const int32_t button_height = kit::compact_metrics.button_height;
    const auto content_of = [&](int32_t panel_w) {
        const int32_t inner = std::max(panel_w - 20, int32_t{1});
        int32_t tall = 10;
        tall += kit::compact_metrics.small_line;
        tall += 6;
        tall += paragraph_height(fonts, inner, heading, kit::FontRole::regular);
        tall += 6;
        tall += paragraph_height(fonts, inner, paragraph, kit::FontRole::small);
        tall += 10 + button_height + 10;
        return tall;
    };
    int32_t panel_width = std::min(int32_t{360}, std::max(width / 3, int32_t{220}));
    int32_t panel_left = kit::compact_metrics.padding;
    int32_t panel_top = kit::compact_metrics.padding;
    int32_t panel_height = content_of(panel_width);
    int32_t limit = width - kit::compact_metrics.padding;
    const auto stop_before = [&](const kit::Rect& button) {
        if (button.width <= 0 || button.height <= 0)
            return;
        if (button.y + button.height <= panel_top || button.y >= panel_top + panel_height)
            return;
        limit = std::min(limit, button.x - 8);
    };
    stop_before(session.model.library_button);
    stop_before(session.model.settings_button);
    panel_width = std::min(panel_width, std::max(limit - panel_left, int32_t{0}));
    panel_height = content_of(std::max(panel_width, int32_t{1}));
    if (panel_width < 180) {
        panel_width = std::min(std::max(width - 2 * panel_left, int32_t{0}), int32_t{420});
        panel_height = content_of(std::max(panel_width, int32_t{1}));
        panel_top = std::max(height - panel_height - kit::compact_metrics.padding, int32_t{0});
    }
    const kit::Rect panel{panel_left, panel_top, panel_width, panel_height};

    const std::string library_body = show(
        "Get, update and switch mods, map packs and languages. The number is how many mods you "
        "have."
    );
    const std::string settings_body = show(
        "Open Annihilation's own settings: controls, graphics, the interface's size, downloads and "
        "your files."
    );
    int32_t card_width = coach_card_width(frame.size_class);
    if (card_width > width - 16)
        card_width = std::max(width - 16, int32_t{80});
    const int32_t library_height = coach_height(fonts, card_width, library_body);
    const int32_t settings_height = coach_height(fonts, card_width, settings_body);
    const CoachCards coaches = place_coaches(
        room,
        panel,
        session.model.library_button,
        session.model.settings_button,
        card_width,
        library_height,
        settings_height
    );
    scene.coach_library = coaches.library;
    scene.coach_settings = coaches.settings;

    kit::Item ground;
    ground.role = kit::Role::fill;
    ground.rect = panel;
    ground.colour = kit::colour::panel;
    scene.list.items.push_back(std::move(ground));
    kit::Item edge;
    edge.role = kit::Role::outline;
    edge.rect = panel;
    edge.colour = kit::colour::accent;
    scene.list.items.push_back(std::move(edge));

    int32_t row = panel.y + 10;
    const int32_t text_width = std::max(panel.width - 20, int32_t{1});
    add_text(
        scene.list,
        {panel.x + 10, row, text_width, kit::compact_metrics.small_line},
        kicker,
        kit::FontRole::small,
        kit::colour::quiet
    );
    row += kit::compact_metrics.small_line;
    row += 6;
    row = add_paragraph(
        scene.list,
        fonts,
        panel.x + 10,
        row,
        text_width,
        heading,
        kit::FontRole::regular,
        kit::colour::text
    );
    row += 6;
    row = add_paragraph(
        scene.list,
        fonts,
        panel.x + 10,
        row,
        text_width,
        paragraph,
        kit::FontRole::small,
        kit::colour::hint
    );
    row += 10;

    const int32_t back_width = std::max(kit::compact_metrics.button_width, int32_t{72});
    const int32_t start_width = std::max(int32_t{140}, panel.width / 2);
    const kit::Rect back_rect{panel.x + 10, row, back_width, button_height};
    int32_t start_left = panel.x + panel.width - 10 - start_width;
    if (start_left < back_rect.x + back_rect.width + 8)
        start_left = back_rect.x + back_rect.width + 8;
    const int32_t start_w = std::max(panel.x + panel.width - 10 - start_left, int32_t{72});
    const kit::Rect start_rect{start_left, row, start_w, button_height};

    std::vector<kit::ControlId> order;
    add_button(
        scene.list,
        order,
        back_rect,
        back_id,
        control_name(Step::buttons, "back"),
        show("Back"),
        true,
        kit::ButtonStyle::plain
    );
    kit::CallToActionLook call;
    call.caption = show("Start playing");
    call.enabled = true;
    call.time_ms = session.pulse_ms;
    call.reduced_motion = frame.reduced_motion;
    kit::add_call_to_action(
        scene.list, start_rect, call, start_id, control_name(Step::buttons, "start")
    );
    order.push_back(start_id);

    kit::CoachMarkLook library_mark;
    library_mark.number = 1;
    library_mark.title = show("Library");
    library_mark.body = library_body;
    library_mark.target = session.model.library_button;
    kit::add_coach_mark(scene.list, coaches.library, library_mark);
    kit::CoachMarkLook settings_mark;
    settings_mark.number = 2;
    settings_mark.title = show("Settings");
    settings_mark.body = settings_body;
    settings_mark.target = session.model.settings_button;
    kit::add_coach_mark(scene.list, coaches.settings, settings_mark);
    scene.list.tab_order = std::move(order);
}

/// What a control asks for.
///
/// @param[in,out] session the session
/// @param scene the layout
/// @param id the control
/// @return what the setup did
Outcome activate(Session& session, const Scene& scene, kit::ControlId id) {
    Outcome outcome;
    const kit::Control* control = kit::control_of(scene.list, id);
    if (control == nullptr || !control->enabled)
        return outcome;
    switch (id) {
    case skip_id:
        outcome.ask = session.model.leave == Leave::quit ? Ask::quit : Ask::skip;
        return outcome;
    case back_id:
        if (session.model.step != Step::language && session.model.step != Step::copying)
            outcome.ask = Ask::back;
        return outcome;
    case next_id:
    case start_id:
        if (session.model.step == Step::copying && session.model.copy.running &&
            !session.model.copy.checked)
            outcome.ask = Ask::pause_copy;
        else
            outcome.ask = Ask::advance;
        return outcome;
    case locate_id:
        outcome.ask = Ask::locate;
        return outcome;
    case on_id:
        session.model.online_on = true;
        session.interaction.focused = on_id;
        outcome.ask = Ask::choose_on;
        outcome.redraw = true;
        return outcome;
    case off_id:
        session.model.online_on = false;
        session.interaction.focused = off_id;
        outcome.ask = Ask::choose_off;
        outcome.redraw = true;
        return outcome;
    case cancel_id:
        outcome.ask = Ask::cancel_download;
        return outcome;
    case retry_id:
        outcome.ask = Ask::retry_download;
        return outcome;
    case previous_id:
        if (step_carousel(session.carousel, false))
            outcome.redraw = true;
        return outcome;
    case next_card_id:
        if (step_carousel(session.carousel, true))
            outcome.redraw = true;
        return outcome;
    default:
        break;
    }
    if (id >= source_id) {
        const int32_t index = id - source_id;
        if (index < 0 || index >= static_cast<int32_t>(session.model.sources.size()))
            return outcome;
        if (session.model.source != index) {
            session.model.source = index;
            session.interaction.focused = id;
            outcome.redraw = true;
        }
        return outcome;
    }
    if (id >= card_id && id < source_id) {
        const int32_t index = id - card_id;
        if (index < 0 || index >= session.carousel.count)
            return outcome;
        if (index != session.carousel.selected) {
            kit::carousel_go(session.carousel, index);
            outcome.redraw = true;
            return outcome;
        }
        if (!download_busy(session.model)) {
            outcome.ask = Ask::pick;
            outcome.index = index;
        }
        return outcome;
    }
    return outcome;
}

/// Dims the layer except two holes, and rings each hole.
///
/// @param canvas where it is drawn
/// @param layer the rectangle that dims
/// @param library the Library button's hole
/// @param settings the Settings button's hole
void dim_around(
    const kit::Canvas& canvas, const kit::Rect& layer, kit::Rect library, kit::Rect settings
) {
    if (layer.width <= 0 || layer.height <= 0)
        return;
    const kit::Rect holes[] = {library, settings};
    std::vector<int32_t> rows;
    rows.push_back(layer.y);
    rows.push_back(layer.y + layer.height);
    for (const kit::Rect& hole : holes) {
        const kit::Rect shared = kit::intersect(hole, layer);
        if (shared.width <= 0 || shared.height <= 0)
            continue;
        rows.push_back(shared.y);
        rows.push_back(shared.y + shared.height);
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    struct Span {
        int32_t begin{};
        int32_t end{};
    };

    for (std::size_t row_at = 0; row_at + 1 < rows.size(); ++row_at) {
        const int32_t top = rows[row_at];
        const int32_t bottom = rows[row_at + 1];
        if (bottom <= top)
            continue;
        std::vector<Span> open;
        for (const kit::Rect& hole : holes) {
            if (hole.width <= 0 || hole.height <= 0)
                continue;
            if (hole.y >= bottom || hole.y + hole.height <= top)
                continue;
            const int32_t left = std::max(hole.x, layer.x);
            const int32_t right = std::min(hole.x + hole.width, layer.x + layer.width);
            if (right > left)
                open.push_back(Span{left, right});
        }
        std::sort(open.begin(), open.end(), [](const Span& left, const Span& right) {
            return left.begin < right.begin;
        });
        std::vector<Span> merged;
        for (const Span& span : open) {
            if (merged.empty() || span.begin > merged.back().end)
                merged.push_back(span);
            else
                merged.back().end = std::max(merged.back().end, span.end);
        }
        int32_t cursor = layer.x;
        const int32_t limit = layer.x + layer.width;
        for (const Span& span : merged) {
            if (span.begin > cursor) {
                kit::blend_rect(
                    canvas,
                    {cursor, top, span.begin - cursor, bottom - top},
                    kit::colour::backdrop,
                    kit::spotlight_opacity
                );
            }
            cursor = std::max(cursor, span.end);
        }
        if (cursor < limit) {
            kit::blend_rect(
                canvas,
                {cursor, top, limit - cursor, bottom - top},
                kit::colour::backdrop,
                kit::spotlight_opacity
            );
        }
    }
    for (const kit::Rect& hole : holes) {
        if (hole.width > 0 && hole.height > 0)
            kit::outline_rect(canvas, hole, kit::colour::accent);
    }
}

} // namespace

void tick(Session& session, uint32_t dt_ms) noexcept {
    kit::carousel_tick(session.carousel, dt_ms);
    session.pulse_ms += dt_ms;
}

void ensure_focus(Session& session, const kit::DisplayList& list) {
    const bool placed =
        session.focus_ready && kit::control_of(list, session.interaction.focused) != nullptr;
    if (placed)
        return;
    session.focus_ready = true;
    session.interaction.focus_shown = true;
    kit::ControlId wanted = next_id;
    if (session.model.step == Step::language)
        wanted = card_id + session.carousel.selected;
    else if (session.model.step == Step::game)
        wanted = session.model.source >= 0 ? source_id + session.model.source : locate_id;
    else if (session.model.step == Step::online)
        wanted = session.model.online_on ? on_id : off_id;
    else if (session.model.step == Step::buttons)
        wanted = start_id;
    else
        wanted = next_id;
    if (kit::control_of(list, wanted) == nullptr && !list.tab_order.empty())
        wanted = list.tab_order.front();
    session.interaction.focused = wanted;
}

Scene lay_out(const Session& session, const kit::Frame& frame, const kit::Fonts* fonts) {
    Scene scene;
    scene.width = frame.area.width;
    scene.height = frame.area.height;
    if (frame.area.width <= 0 || frame.area.height <= 0)
        return scene;
    switch (session.model.step) {
    case Step::language:
        lay_language(scene, session, frame, fonts);
        break;
    case Step::game:
        lay_game(scene, session, frame, fonts);
        break;
    case Step::copying:
        lay_copying(scene, session, frame, fonts);
        break;
    case Step::online:
        lay_online(scene, session, frame, fonts);
        break;
    case Step::buttons:
        lay_buttons(scene, session, frame, fonts);
        break;
    }
    return scene;
}

Outcome pointer(
    Session& session,
    const Scene& scene,
    PointerKind kind,
    kit::Point at,
    int32_t finger_reach,
    uint8_t clicks
) {
    Outcome outcome;
    if (session.model.step == Step::language && !scene.places.empty()) {
        const kit::PointerPhase phase = kind == PointerKind::move   ? kit::PointerPhase::move
                                        : kind == PointerKind::down ? kit::PointerPhase::down
                                                                    : kit::PointerPhase::up;
        // Arrows are ordinary controls, so a press on either end wraps. The
        // carousel's own arrow handling stops at the first and last card.
        const kit::CarouselEvent event = kit::carousel_pointer(
            session.carousel, scene.places, {}, {}, session.drag, phase, at, clicks
        );
        if (kind == PointerKind::up && event != kit::CarouselEvent::none) {
            std::ignore = kit::pointer_up(session.interaction, scene.list, at);
            outcome.redraw = true;
            if (event == kit::CarouselEvent::picked && !download_busy(session.model)) {
                outcome.ask = Ask::pick;
                outcome.index = session.carousel.selected;
            }
            return outcome;
        }
    }
    kit::PointerOutcome pressed{};
    switch (kind) {
    case PointerKind::move:
        pressed = kit::pointer_move(session.interaction, scene.list, at);
        break;
    case PointerKind::down:
        pressed = finger_reach > 0
                      ? kit::finger_down(session.interaction, scene.list, at, finger_reach)
                      : kit::pointer_down(session.interaction, scene.list, at);
        break;
    case PointerKind::up:
        pressed = kit::pointer_up(session.interaction, scene.list, at);
        break;
    }
    if (pressed.result == kit::PointerResult::activated)
        return activate(session, scene, pressed.control);
    if (pressed.result == kit::PointerResult::redraw)
        outcome.redraw = true;
    return outcome;
}

Outcome key(Session& session, const Scene& scene, kit::Key pressed) {
    Outcome outcome;
    // On the last step both keys finish the walk, even when Back has the focus.
    if (session.model.step == Step::buttons &&
        (pressed == kit::Key::escape || pressed == kit::Key::enter)) {
        outcome.ask = Ask::advance;
        return outcome;
    }
    if (pressed == kit::Key::escape) {
        if (session.model.step == Step::copying && !session.model.copy.checked)
            outcome.ask = Ask::cancel_copy;
        else if (session.model.step != Step::language && session.model.step != Step::copying)
            outcome.ask = Ask::back;
        return outcome;
    }
    if (session.model.step == Step::game &&
        (pressed == kit::Key::up || pressed == kit::Key::down || pressed == kit::Key::left ||
         pressed == kit::Key::right)) {
        const int32_t count = static_cast<int32_t>(session.model.sources.size());
        if (count > 0) {
            const int32_t index = session.model.source < 0 ? 0 : session.model.source;
            const int32_t next = kit::choice_step(index, count, pressed);
            if (next != session.model.source)
                return activate(session, scene, source_id + next);
        }
    }
    const kit::ControlId focused = session.interaction.focused;
    const bool on_choice = focused == on_id || focused == off_id;
    if (session.model.step == Step::online && on_choice &&
        (pressed == kit::Key::up || pressed == kit::Key::down || pressed == kit::Key::left ||
         pressed == kit::Key::right)) {
        const int32_t index = session.model.online_on ? 0 : 1;
        const int32_t next = kit::choice_step(index, 2, pressed);
        if (next != index)
            return activate(session, scene, next == 0 ? on_id : off_id);
    }
    const kit::KeyOutcome taken = kit::key(session.interaction, scene.list, pressed);
    if (taken.result == kit::KeyResult::cancel) {
        if (session.model.step == Step::copying && !session.model.copy.checked)
            outcome.ask = Ask::cancel_copy;
        else if (session.model.step != Step::language && session.model.step != Step::copying)
            outcome.ask = Ask::back;
        return outcome;
    }
    if (taken.result == kit::KeyResult::redraw) {
        outcome.redraw = true;
        return outcome;
    }
    if (taken.result == kit::KeyResult::to_control) {
        if (pressed == kit::Key::left || pressed == kit::Key::right) {
            if (session.model.step == Step::language &&
                step_carousel(session.carousel, pressed == kit::Key::right))
                outcome.redraw = true;
            return outcome;
        }
        return activate(session, scene, taken.control);
    }
    if (taken.result == kit::KeyResult::accept)
        return activate(session, scene, session.interaction.focused);
    return outcome;
}

Outcome wheel(Session& session, const Scene& /*scene*/, float notches) {
    Outcome outcome;
    if (session.model.step != Step::language || session.carousel.count <= 1 || notches == 0.F)
        return outcome;
    // A turn away from the player is the previous card. One card does not move.
    if (step_carousel(session.carousel, notches < 0.F))
        outcome.redraw = true;
    return outcome;
}

void draw(const kit::Canvas& canvas, const Scene& scene, const Session& session) {
    kit::DisplayList list = scene.list;
    kit::mark_states(list, session.interaction);
    if (session.interaction.focus_shown) {
        if (const kit::Control* control = kit::control_of(list, session.interaction.focused))
            kit::add_focus_ring(list, control->rect, true);
    }
    if (session.model.step == Step::buttons) {
        const kit::Rect library = kit::grown(session.model.library_button, 4);
        const kit::Rect settings = kit::grown(session.model.settings_button, 4);
        dim_around(canvas, {0, 0, scene.width, scene.height}, library, settings);
    }
    kit::paint(canvas, list);
    if (!scene.title_text.empty() && scene.title.width > 0 && scene.title.height > 0)
        kit::draw_prominent_line(canvas, scene.title_text, scene.title, kit::colour::text);
    if (scene.sample)
        draw_sample(canvas, scene.well_inset, session.model, scene.sample_class);
}

} // namespace oa::ui::first_run
