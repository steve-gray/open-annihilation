// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The kit's list rows, chips, fields, tabs, cards, hover cards and links:
// their pixels in the kit's tokens, their geometry, their controls and
// names, and paint against their direct draws.
//
// The synthetic font's glyph for byte b is 2 + b % 4 columns by 8 rows, set
// where (x + y + b) % 3 is not 0, so a glyph's top-left pixel is set unless
// b % 3 is 0. Text in a box 12 rows high starts one row down, in a box 16
// rows high three rows down. The widths below are sums of those columns.

#include "oa/formats/fnt.hpp"
#include "oa/test/check.hpp"
#include "oa/ui/frontend_renderer.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/kit/controls.hpp"
#include "oa/ui/kit/input.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace kit = oa::ui::kit;
namespace renderer = oa::ui::frontend_renderer;

renderer::Surface black(int32_t width, int32_t height) {
    renderer::Surface surface;
    surface.width = static_cast<uint32_t>(width);
    surface.height = static_cast<uint32_t>(height);
    surface.rgb.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3, 0);
    return surface;
}

/// The synthetic font of the settings pixel test, for both roles.
kit::Fonts block_fonts() {
    renderer::TextFont font;
    font.font.nominal_height = 8;
    constexpr uint8_t ink_index = 1;
    for (int byte = 0; byte < 256; ++byte) {
        const auto width = static_cast<uint16_t>(2 + byte % 4);
        constexpr uint16_t height = 8;
        const auto count = static_cast<std::size_t>(width) * height;
        std::vector<uint8_t> pixels(count, ink_index);
        std::vector<uint8_t> coverage(count, 0);
        for (uint16_t y = 0; y < height; ++y) {
            for (uint16_t x = 0; x < width; ++x) {
                if ((static_cast<int>(x) + y + byte) % 3 != 0)
                    coverage[static_cast<std::size_t>(y) * width + x] = 1;
            }
        }
        font.font.glyphs[static_cast<std::size_t>(byte)] =
            oa::formats::fnt::Glyph{width, height, 0, 0, std::move(pixels), std::move(coverage)};
    }
    font.ink[ink_index] = static_cast<uint16_t>(renderer::blend_opaque);
    return {font, font, {}, {}};
}

const kit::Fonts& fonts() {
    static const kit::Fonts loaded = block_fonts();
    return loaded;
}

kit::Canvas canvas_on(renderer::Surface& surface, int32_t scale = 1) {
    return {&surface, {0, 0, scale, {}}, &fonts(), {}};
}

/// The colour a surface holds at a pixel.
kit::Colour pixel(const renderer::Surface& surface, int32_t x, int32_t y) {
    const std::size_t at =
        (static_cast<std::size_t>(y) * surface.width + static_cast<std::size_t>(x)) * 3;
    return {surface.rgb[at], surface.rgb[at + 1], surface.rgb[at + 2]};
}

/// Checks that a pixel holds a colour, and says which when it does not.
void expect(const renderer::Surface& surface, int32_t x, int32_t y, kit::Colour colour) {
    const auto width = static_cast<int32_t>(surface.width);
    const auto height = static_cast<int32_t>(surface.height);
    if (x < 0 || y < 0 || x >= width || y >= height) {
        std::fprintf(stderr, "pixel %d,%d is outside %dx%d\n", x, y, width, height);
        OA_CHECK(false);
        return;
    }
    const kit::Colour got = pixel(surface, x, y);
    if (got != colour) {
        std::fprintf(
            stderr,
            "pixel %d,%d is #%02x%02x%02x, expected #%02x%02x%02x\n",
            x,
            y,
            got.r,
            got.g,
            got.b,
            colour.r,
            colour.g,
            colour.b
        );
        OA_CHECK(false);
    }
}

constexpr kit::Colour kBlack{0, 0, 0};

/// The colour a blend of a token over another colour leaves.
kit::Colour blended(kit::Colour under, kit::Colour over, uint32_t opacity) {
    renderer::Surface one = black(1, 1);
    renderer::fill_source_rect(one, {0, 0, 1, {}}, {0, 0, 1, 1}, kit::rgb(under));
    renderer::blend_source_rect(one, {0, 0, 1, {}}, {0, 0, 1, 1}, kit::rgb(over), opacity);
    return pixel(one, 0, 0);
}

void same(const renderer::Surface& left, const renderer::Surface& right) {
    if (left.rgb != right.rgb) {
        for (uint32_t y = 0; y < left.height; ++y) {
            for (uint32_t x = 0; x < left.width; ++x) {
                const std::size_t at = (static_cast<std::size_t>(y) * left.width + x) * 3;
                if (left.rgb[at] != right.rgb[at] || left.rgb[at + 1] != right.rgb[at + 1] ||
                    left.rgb[at + 2] != right.rgb[at + 2]) {
                    std::fprintf(stderr, "surfaces differ at %u,%u\n", x, y);
                    y = left.height;
                    break;
                }
            }
        }
    }
    OA_CHECK(left.width == right.width && left.height == right.height && left.rgb == right.rgb);
}

/// A picture of one colour, two by two, whose pixels live as long as it does.
struct OneColourPicture {
    std::array<uint8_t, 16> pixels{};

    explicit OneColourPicture(kit::Colour colour) {
        for (std::size_t at = 0; at < pixels.size(); at += 4) {
            pixels[at] = colour.r;
            pixels[at + 1] = colour.g;
            pixels[at + 2] = colour.b;
            pixels[at + 3] = 255;
        }
    }

    [[nodiscard]] renderer::RgbaPicture picture() const { return {2, 2, pixels}; }
};

// ---- List rows ----

/// The short form: a 20-point badge with its letters, the name over its
/// line, and a chip; the Mods row's places.
void a_short_row_lies_as_the_mods_row() {
    kit::ListRowLook look;
    look.badge_text = "RI";
    look.badge_colour = kit::colour::lock;
    look.title = "Ridge";
    look.lines = {"4.8 rev 3 - 41.0 MB"};
    look.chip = kit::ChipLook{"UPDATE", kit::ChipState::update};
    const kit::Rect rect{10, 10, 300, kit::compact_metrics.short_list_row_height};
    renderer::Surface surface = black(320, 50);
    kit::draw_list_row(canvas_on(surface), rect, look);

    // Nothing under the row is painted while it is neither hovered nor selected.
    expect(surface, 12, 12, kBlack);
    // The badge: 20 square at the inset, on its colour, its letters centred.
    expect(surface, 14, 14, kit::colour::lock);
    expect(surface, 33, 33, kit::colour::lock);
    expect(surface, 34, 33, kBlack);
    expect(surface, 20, 19, kit::colour::on_accent);
    // The title at the Mods row's title line, its line at row 15.
    expect(surface, 38, 14, kit::colour::text);
    expect(surface, 38, 26, kit::colour::list_text);
    // The chip: UPDATE is 15 columns and its padding, at the right inset.
    const kit::Rect chip = kit::list_row_chip_rect(fonts(), rect, *look.chip);
    OA_CHECK(chip.x == 283 && chip.y == 18 && chip.width == 23 && chip.height == 12);
    expect(surface, 283, 18, kit::colour::lock);
    expect(surface, 305, 29, kit::colour::lock);
}

/// The full form: a 28-point badge with a picture, the title and its byline,
/// a cut summary, a two-line aside and a chip; selected.
void a_full_row_cuts_its_summary_before_the_aside() {
    const OneColourPicture badge(kit::colour::online);
    kit::ListRowLook look;
    look.badge = badge.picture();
    look.badge_text = "RI";
    look.title = "Ridge";
    look.by = "by the Ridge team";
    look.lines = {
        "Balance overhaul for 3.1c with every unit of both sides rebalanced, new maps to play "
        "and a campaign of its own"
    };
    look.aside = {"4.8 rev 3", "41.0 MB"};
    look.chip = kit::ChipLook{"GET", kit::ChipState::get};
    look.selected = true;
    const kit::Rect rect{10, 50, 400, kit::compact_metrics.list_row_height};
    renderer::Surface surface = black(420, 100);
    kit::draw_list_row(canvas_on(surface), rect, look);

    // Selected: the accent outline and the accent's tint over the face.
    expect(surface, 10, 50, kit::colour::accent);
    expect(surface, 409, 85, kit::colour::accent);
    expect(surface, 12, 52, blended(kBlack, kit::colour::accent, kit::selected_tint));
    // The picture wins over the letters, outlined, 28 square.
    expect(surface, 14, 54, kit::colour::control_border);
    expect(surface, 20, 60, kit::colour::online);
    expect(surface, 41, 81, kit::colour::control_border);
    // The title, then its byline six columns after it in the hint colour.
    expect(surface, 46, 58, kit::colour::text);
    expect(surface, 69, 58, kit::colour::hint);
    // The aside, right-aligned at 354 to 381, one entry a line.
    expect(surface, 354, 58, kit::colour::hint);
    expect(surface, 362, 70, kit::colour::hint);
    // The chip at the right inset.
    expect(surface, 388, 62, kit::colour::control_hover);
    // The summary, cut so that nothing of it reaches the aside's gap.
    expect(surface, 47, 70, kit::colour::list_text);
    bool reaches_end = false;
    for (int32_t y = 70; y < 78; ++y) {
        for (int32_t x = 330; x < 348; ++x)
            reaches_end = reaches_end || pixel(surface, x, y) == kit::colour::list_text;
        for (int32_t x = 348; x < 354; ++x)
            OA_CHECK(pixel(surface, x, y) != kit::colour::list_text);
    }
    OA_CHECK(reaches_end);
    const std::string cut =
        kit::cut_to_width(fonts(), kit::FontRole::small, look.lines[0], 348 - 46);
    OA_CHECK(cut.size() > 3 && cut.substr(cut.size() - 3) == "...");
    OA_CHECK(kit::text_width(fonts(), kit::FontRole::small, cut) <= 302);
    OA_CHECK(kit::cut_to_width(fonts(), kit::FontRole::small, "Ridge", 17) == "Ridge");
    OA_CHECK(kit::cut_to_width(fonts(), kit::FontRole::small, "Ridge", 16) == "R...");
    OA_CHECK(kit::cut_to_width(fonts(), kit::FontRole::small, "Ridge", 15) == "...");
    OA_CHECK(kit::cut_to_width(fonts(), kit::FontRole::small, "Ridge", 2) == "...");
}

/// A row with neither picture nor letters draws the blank dashed badge; a
/// taller row puts its tags on the line after its last; hover is the hover face.
void a_blank_badge_and_tags() {
    kit::ListRowLook look;
    look.title = "Ridge";
    look.hovered = true;
    renderer::Surface surface = black(420, 200);
    kit::draw_list_row(canvas_on(surface), {10, 100, 300, 28}, look);
    expect(surface, 12, 102, kit::colour::hover);
    expect(surface, 14, 104, kit::colour::control_border);
    expect(surface, 15, 104, kit::colour::control_border);
    expect(surface, 16, 104, kit::colour::hover);
    expect(surface, 17, 104, kit::colour::control_border);
    expect(surface, 14, 106, kit::colour::hover);
    expect(surface, 14, 107, kit::colour::control_border);
    expect(surface, 16, 123, kit::colour::hover);
    expect(surface, 17, 123, kit::colour::control_border);
    expect(surface, 33, 106, kit::colour::hover);
    expect(surface, 33, 107, kit::colour::control_border);
    expect(surface, 20, 110, kit::colour::hover);

    kit::ListRowLook tagged;
    tagged.badge_text = "RI";
    tagged.badge_colour = kit::colour::accent;
    tagged.title = "Ridge";
    tagged.lines = {"Balance overhaul"};
    tagged.tags = {
        kit::ChipLook{"SIM", kit::ChipState::filter},
        kit::ChipLook{"VIEW", kit::ChipState::filter, true},
    };
    kit::draw_list_row(canvas_on(surface), {10, 140, 400, 48}, tagged);
    // The text is centred: title at 145, its line at 159, the tags at 171.
    expect(surface, 46, 148, kit::colour::text);
    expect(surface, 47, 160, kit::colour::list_text);
    expect(surface, 46, 171, kit::colour::control_border);
    expect(surface, 64, 182, kit::colour::control_border);
    expect(surface, 69, 171, kit::colour::accent);
    expect(surface, 70, 172, kit::colour::list_selected);
    // The badge stays 28 in a taller row, centred down it.
    expect(surface, 14, 150, kit::colour::accent);
    expect(surface, 14, 149, kBlack);
}

// ---- Chips ----

/// Draws a GET-wide chip of a state and checks its border, face and text.
void pin_chip(const kit::ChipLook& look, kit::Colour border, kit::Colour face, kit::Colour text) {
    renderer::Surface surface = black(40, 30);
    kit::draw_chip(canvas_on(surface), {10, 10, 18, 12}, look);
    expect(surface, 10, 10, border);
    expect(surface, 27, 21, border);
    expect(surface, 11, 11, face);
    expect(surface, 14, 11, text);
}

void chips_take_every_state() {
    OA_CHECK(kit::chip_width(fonts(), {"GET"}) == 18);
    OA_CHECK(kit::chip_width(fonts(), {"UPDATE"}) == 23);
    OA_CHECK(kit::chip_width(fonts(), {""}) == 8);

    using State = kit::ChipState;
    namespace colour = kit::colour;
    pin_chip({"GET", State::get}, colour::control_hover, kBlack, colour::hint);
    pin_chip({"GET", State::installed}, colour::accent, kBlack, colour::accent);
    pin_chip({"GET", State::update}, colour::lock, kBlack, colour::lock);
    pin_chip({"GET", State::playing}, colour::accent, colour::accent, colour::on_accent);
    pin_chip({"GET", State::online}, colour::online, kBlack, colour::online);
    pin_chip({"GET", State::problem}, colour::danger, kBlack, colour::danger);
    pin_chip({"GET", State::filter}, colour::control_border, kBlack, colour::hint);
    pin_chip(
        {"GET", State::filter, false, true}, colour::control_hover, colour::hover, colour::hint
    );
    pin_chip({"GET", State::filter, true}, colour::accent, colour::list_selected, colour::text);
    pin_chip({"GET", State::get, false, true}, colour::control_hover, colour::hover, colour::hint);
    pin_chip(
        {"GET", State::playing, false, true}, colour::accent, colour::accent, colour::on_accent
    );
    for (const State state :
         {State::get,
          State::installed,
          State::update,
          State::playing,
          State::online,
          State::problem,
          State::filter}) {
        pin_chip(
            {"GET", state, true, true, true}, colour::switch_idle, kBlack, colour::switch_idle
        );
    }
}

// ---- Text and search fields ----

void a_search_field_shows_its_placeholder_and_magnifier() {
    kit::SearchLook look;
    look.placeholder = "Search mods";
    renderer::Surface surface = black(140, 40);
    const kit::Rect rect{10, 10, 120, kit::compact_metrics.field_height};
    kit::draw_search(canvas_on(surface), rect, look);
    expect(surface, 10, 10, kit::colour::control_border);
    expect(surface, 11, 11, kit::colour::well);
    // The magnifier 4 points in, centred down the field.
    expect(surface, 14, 14, kit::colour::well);
    expect(surface, 15, 14, kit::colour::hint);
    expect(surface, 18, 15, kit::colour::hint);
    expect(surface, 20, 20, kit::colour::hint);
    // The placeholder after the mark.
    const kit::Rect text = kit::field_text_rect(rect, look);
    OA_CHECK(text.x == 25 && text.y == 10 && text.width == 101 && text.height == 16);
    expect(surface, 25, 13, kit::colour::hint);

    look.hovered = true;
    surface = black(140, 40);
    kit::draw_search(canvas_on(surface), rect, look);
    expect(surface, 10, 10, kit::colour::control_hover);

    // The plain text field: no magnifier, the placeholder 4 points in.
    look.magnifier = false;
    look.hovered = false;
    look.placeholder.clear();
    surface = black(140, 40);
    kit::draw_search(canvas_on(surface), rect, look);
    OA_CHECK(kit::field_text_rect(rect, look).x == 14);
    expect(surface, 15, 14, kit::colour::well);
    expect(surface, 20, 20, kit::colour::well);
    look.placeholder = "Search mods";
    kit::draw_search(canvas_on(surface), rect, look);
    expect(surface, 14, 13, kit::colour::hint);
}

void a_focused_field_shows_its_text_and_caret() {
    kit::SearchLook look;
    look.placeholder = "Search mods";
    look.field = {"ridge", 5};
    look.focused = true;
    renderer::Surface surface = black(140, 40);
    const kit::Rect rect{10, 10, 120, 16};
    kit::draw_search(canvas_on(surface), rect, look);
    expect(surface, 10, 10, kit::colour::accent);
    // r's first column is empty; its second is set.
    expect(surface, 25, 13, kit::colour::well);
    expect(surface, 26, 13, kit::colour::text);
    // The caret after the 17 columns of "ridge", over the capitals' 8 rows.
    expect(surface, 42, 13, kit::colour::well);
    expect(surface, 42, 14, kit::colour::text);
    expect(surface, 42, 21, kit::colour::text);
    expect(surface, 42, 22, kit::colour::well);
    expect(surface, 43, 14, kit::colour::well);

    // Without the focus there is no caret.
    look.focused = false;
    surface = black(140, 40);
    kit::draw_search(canvas_on(surface), rect, look);
    expect(surface, 42, 14, kit::colour::well);

    // Focused on an empty field: the placeholder, and the caret at the start.
    look.field = {};
    look.focused = true;
    surface = black(140, 40);
    kit::draw_search(canvas_on(surface), rect, look);
    expect(surface, 25, 14, kit::colour::text);
    expect(surface, 25, 13, kit::colour::hint);
}

void a_long_text_scrolls_so_its_caret_shows() {
    kit::SearchLook look;
    look.magnifier = false;
    look.focused = true;
    look.field = {"abcdefghijklmnopqrstuvwxyzabcdefghij", 36};
    const kit::Rect rect{10, 40, 60, 16};
    // 126 columns of text in 52, less the caret's column: moved 75 left.
    OA_CHECK(kit::field_scroll(fonts(), rect, look) == 75);
    renderer::Surface surface = black(80, 80);
    kit::draw_search(canvas_on(surface), rect, look);
    expect(surface, 65, 44, kit::colour::text);
    expect(surface, 65, 51, kit::colour::text);
    // Nothing of the text is drawn left of its room.
    for (int32_t y = 43; y < 51; ++y)
        for (int32_t x = 11; x < 14; ++x)
            expect(surface, x, y, kit::colour::well);

    look.field.caret = 0;
    OA_CHECK(kit::field_scroll(fonts(), rect, look) == 0);
    // A caret inside a character counts from that character's start.
    look.field = {"ab\xe4\xb8\xad", 3};
    OA_CHECK(kit::field_scroll(fonts(), rect, look) == 0);
}

void typing_moves_the_caret_drawn() {
    kit::SearchLook look;
    look.placeholder = "Search mods";
    look.focused = true;
    OA_CHECK(kit::insert_text(look.field, "ridge"));
    OA_CHECK(kit::edit_text(look.field, kit::Key::left));
    OA_CHECK(kit::edit_text(look.field, kit::Key::left));
    OA_CHECK(kit::insert_text(look.field, "X"));
    OA_CHECK(look.field.text == "ridXge" && look.field.caret == 4);
    renderer::Surface surface = black(140, 100);
    kit::draw_search(canvas_on(surface), {10, 70, 120, 16}, look);
    // The caret after "ridX", 11 columns from the text's left at 25.
    expect(surface, 36, 74, kit::colour::text);
    expect(surface, 37, 74, kit::colour::well);
    OA_CHECK(kit::edit_text(look.field, kit::Key::backspace));
    OA_CHECK(look.field.text == "ridge" && look.field.caret == 3);
}

// ---- Tabs ----

void tabs_draw_with_a_count_and_with_none() {
    kit::TabsLook look;
    look.captions = {"MODS", "MAPS", "UPDATES"};
    look.counts = {0, 0, 2};
    const kit::Rect strip{10, 10, 300, kit::compact_metrics.tab_height};
    const std::vector<kit::Rect> rects = kit::tab_rects(fonts(), strip, look);
    OA_CHECK(rects.size() == 3u);
    // MODS is 15 columns and three of tracking, and 10 either side.
    OA_CHECK(rects[0].x == 10 && rects[0].y == 10 && rects[0].width == 38 && rects[0].height == 18);
    OA_CHECK(rects[1].x == 48 && rects[1].width == 36);
    // UPDATES and its count: 26, 4 apart, and a pill 12 wide.
    OA_CHECK(rects[2].x == 84 && rects[2].width == 62);

    renderer::Surface surface = black(320, 40);
    kit::draw_tabs(canvas_on(surface), strip, look);
    // The selected tab: its caption in the text colour over the accent rule.
    expect(surface, 20, 13, kit::colour::text);
    expect(surface, 10, 26, kit::colour::accent);
    expect(surface, 47, 27, kit::colour::accent);
    expect(surface, 10, 25, kBlack);
    // The others: hint captions over the strip's hairline.
    expect(surface, 58, 13, kit::colour::hint);
    expect(surface, 50, 27, kit::colour::rule);
    expect(surface, 50, 26, kBlack);
    expect(surface, 94, 13, kit::colour::hint);
    expect(surface, 300, 27, kit::colour::rule);
    // The count: a pill of the lock colour with its digit in on_accent.
    expect(surface, 124, 12, kit::colour::lock);
    expect(surface, 135, 23, kit::colour::lock);
    expect(surface, 128, 13, kit::colour::on_accent);
    expect(surface, 123, 12, kBlack);

    // Hovered, a tab's caption is in the text colour.
    look.hovered = 1;
    surface = black(320, 40);
    kit::draw_tabs(canvas_on(surface), strip, look);
    expect(surface, 58, 13, kit::colour::text);
    expect(surface, 94, 13, kit::colour::hint);

    // No count draws no pill; a count of 0 none either.
    look.counts = {};
    look.selected = 2;
    look.hovered.reset();
    OA_CHECK(kit::tab_rects(fonts(), strip, look)[2].width == 46);
    surface = black(320, 40);
    kit::draw_tabs(canvas_on(surface), strip, look);
    expect(surface, 124, 12, kBlack);
    expect(surface, 84, 26, kit::colour::accent);
    expect(surface, 129, 27, kit::colour::accent);
    expect(surface, 130, 27, kit::colour::rule);
    expect(surface, 20, 13, kit::colour::hint);
}

// ---- Cards ----

/// Checks a cell's place and size.
bool cell_is(const kit::Rect& cell, int32_t x, int32_t y, int32_t width, int32_t height) {
    return cell.x == x && cell.y == y && cell.width == width && cell.height == height;
}

void cards_lie_in_a_grid() {
    OA_CHECK(kit::card_height(120) == 152);
    OA_CHECK(kit::card_height(124) == 156);
    OA_CHECK(kit::card_height(8) == 40);
    OA_CHECK(kit::card_height(0) == 40);

    // As many as fit at 120, at three widths.
    std::vector<kit::Rect> cells = kit::card_cells({0, 0, 640, 400}, 7);
    OA_CHECK(cells.size() == 7u);
    OA_CHECK(cell_is(cells[0], 0, 0, 124, 156));
    OA_CHECK(cell_is(cells[1], 130, 0, 123, 155));
    OA_CHECK(cell_is(cells[4], 517, 0, 123, 155));
    OA_CHECK(cell_is(cells[5], 0, 162, 124, 156));
    OA_CHECK(cell_is(cells[6], 130, 162, 123, 155));
    cells = kit::card_cells({10, 20, 250, 400}, 3);
    OA_CHECK(cells.size() == 3u);
    OA_CHECK(cell_is(cells[0], 10, 20, 122, 154));
    OA_CHECK(cell_is(cells[1], 138, 20, 122, 154));
    OA_CHECK(cell_is(cells[2], 10, 180, 122, 154));
    cells = kit::card_cells({0, 0, 100, 400}, 2);
    OA_CHECK(cells.size() == 2u);
    OA_CHECK(cell_is(cells[0], 0, 0, 100, 132));
    OA_CHECK(cell_is(cells[1], 0, 138, 100, 132));
    OA_CHECK(kit::card_cells({0, 0, 640, 400}, 0).empty());

    // Exactly 3, 4 and 6 columns.
    cells = kit::card_cells({0, 0, 640, 600}, 4, 3);
    OA_CHECK(cells.size() == 4u);
    OA_CHECK(cell_is(cells[0], 0, 0, 210, 242));
    OA_CHECK(cell_is(cells[1], 216, 0, 209, 241));
    OA_CHECK(cell_is(cells[2], 431, 0, 209, 241));
    OA_CHECK(cell_is(cells[3], 0, 248, 210, 242));
    cells = kit::card_cells({0, 0, 640, 600}, 8, 4);
    OA_CHECK(cells.size() == 8u);
    OA_CHECK(cell_is(cells[0], 0, 0, 156, 188));
    OA_CHECK(cell_is(cells[2], 324, 0, 155, 187));
    OA_CHECK(cell_is(cells[3], 485, 0, 155, 187));
    OA_CHECK(cell_is(cells[7], 485, 194, 155, 187));
    cells = kit::card_cells({0, 0, 640, 600}, 7, 6);
    OA_CHECK(cells.size() == 7u);
    OA_CHECK(cell_is(cells[0], 0, 0, 102, 134));
    OA_CHECK(cell_is(cells[3], 324, 0, 102, 134));
    OA_CHECK(cell_is(cells[4], 432, 0, 101, 133));
    OA_CHECK(cell_is(cells[5], 539, 0, 101, 133));
    OA_CHECK(cell_is(cells[6], 0, 140, 102, 134));
    // Fewer cards than columns keep the columns' widths.
    cells = kit::card_cells({0, 0, 640, 600}, 2, 6);
    OA_CHECK(cells.size() == 2u);
    OA_CHECK(cell_is(cells[1], 108, 0, 102, 134));
    // More columns than the area has room for still place every card.
    cells = kit::card_cells({0, 0, 10, 600}, 3, static_cast<std::size_t>(-1));
    OA_CHECK(cells.size() == 3u);
    cells = kit::card_cells({0, 0, 0, 600}, 2);
    OA_CHECK(cells.size() == 2u);
    OA_CHECK(cell_is(cells[0], 0, 0, 0, 40));
}

void a_card_shows_its_placeholder_chips_and_lines() {
    kit::CardLook look;
    look.placeholder = "Loading preview";
    look.title = "Isle of Ashes";
    look.subtitle = "4 - 16x16";
    look.chips = {
        kit::ChipLook{"BASE", kit::ChipState::get},
        kit::ChipLook{"RIDGE", kit::ChipState::installed},
    };
    const kit::Rect rect{10, 10, 120, kit::card_height(120)};
    renderer::Surface surface = black(140, 170);
    kit::draw_card(canvas_on(surface), rect, look);
    expect(surface, 10, 10, kit::colour::control_border);
    expect(surface, 11, 11, kit::colour::list_selected);
    // The preview, 112 square, 4 in: the well and the placeholder centred.
    expect(surface, 14, 14, kit::colour::well);
    expect(surface, 13, 14, kit::colour::list_selected);
    expect(surface, 125, 125, kit::colour::well);
    expect(surface, 126, 126, kit::colour::list_selected);
    expect(surface, 45, 65, kit::colour::hint);
    // The chips at the preview's bottom left, 4 in and 4 apart.
    expect(surface, 18, 110, kit::colour::control_hover);
    expect(surface, 40, 121, kit::colour::control_hover);
    expect(surface, 45, 110, kit::colour::accent);
    expect(surface, 17, 110, kit::colour::well);
    // The title and the subtitle under the preview.
    expect(surface, 14, 133, kit::colour::text);
    expect(surface, 14, 147, kit::colour::hint);

    look.selected = true;
    kit::draw_card(canvas_on(surface), rect, look);
    expect(surface, 10, 10, kit::colour::accent);
    look.selected = false;
    look.hovered = true;
    kit::draw_card(canvas_on(surface), rect, look);
    expect(surface, 10, 10, kit::colour::control_hover);

    // A picture fills the preview instead of the placeholder.
    const OneColourPicture preview(kit::colour::online);
    look.picture = preview.picture();
    kit::draw_card(canvas_on(surface), rect, look);
    expect(surface, 45, 65, kit::colour::online);
    expect(surface, 14, 14, kit::colour::online);
}

void a_greyed_card_fades_into_the_panel() {
    kit::CardLook look;
    look.placeholder = "Loading preview";
    look.title = "Isle of Ashes";
    look.chips = {kit::ChipLook{"BASE", kit::ChipState::get}};
    const kit::Rect rect{10, 10, 120, kit::card_height(120)};
    renderer::Surface plain = black(140, 170);
    kit::draw_card(canvas_on(plain), rect, look);
    renderer::blend_source_rect(
        plain, {0, 0, 1, {}}, rect, kit::rgb(kit::colour::panel), kit::locked_fade
    );
    look.greyed = true;
    renderer::Surface greyed = black(140, 170);
    kit::draw_card(canvas_on(greyed), rect, look);
    same(plain, greyed);
    expect(
        greyed, 11, 11, blended(kit::colour::list_selected, kit::colour::panel, kit::locked_fade)
    );

    // A greyed card still takes a press.
    kit::DisplayList list;
    kit::add_card(list, rect, look, 3, "maps.card.isle");
    OA_CHECK(list.controls.size() == 1u && list.controls[0].enabled);
    OA_CHECK(kit::hit(list, {20, 20}) == 3);
}

void the_arrows_move_through_a_grid_of_cards() {
    const std::vector<kit::Rect> cells = kit::card_cells({0, 0, 400, 400}, 6, 3);
    OA_CHECK(cells.size() == 6u);
    kit::DisplayList list;
    for (std::size_t index = 0; index < cells.size(); ++index) {
        kit::CardLook look;
        look.title = "Map " + std::to_string(index + 1);
        const auto id = static_cast<kit::ControlId>(index + 1);
        kit::add_card(list, cells[index], look, id, "maps.card-" + std::to_string(index + 1));
        list.tab_order.push_back(id);
    }
    OA_CHECK(kit::name_problem(list).empty());
    OA_CHECK(kit::focus_toward(list, 1, kit::Direction::right) == 2);
    OA_CHECK(kit::focus_toward(list, 2, kit::Direction::right) == 3);
    OA_CHECK(kit::focus_toward(list, 3, kit::Direction::right) == kit::no_control);
    OA_CHECK(kit::focus_toward(list, 1, kit::Direction::down) == 4);
    OA_CHECK(kit::focus_toward(list, 2, kit::Direction::down) == 5);
    OA_CHECK(kit::focus_toward(list, 6, kit::Direction::up) == 3);
    OA_CHECK(kit::focus_toward(list, 6, kit::Direction::left) == 5);
    OA_CHECK(kit::focus_toward(list, 4, kit::Direction::left) == kit::no_control);
    OA_CHECK(kit::focus_toward(list, 4, kit::Direction::down) == kit::no_control);

    // Left and Right move the focus between cards; Enter goes to the card.
    kit::Interaction interaction;
    interaction.focus_shown = true;
    interaction.focused = 5;
    const kit::KeyOutcome right = kit::key(interaction, list, kit::Key::right);
    OA_CHECK(right.result == kit::KeyResult::redraw && interaction.focused == 6);
    const kit::KeyOutcome up = kit::key(interaction, list, kit::Key::up);
    OA_CHECK(up.result == kit::KeyResult::redraw && interaction.focused == 3);
    const kit::KeyOutcome enter = kit::key(interaction, list, kit::Key::enter);
    OA_CHECK(enter.result == kit::KeyResult::to_control && enter.control == 3);
}

// ---- Hover cards ----

kit::HoverCardLook presence_card() {
    kit::HoverCardLook look;
    look.mark = true;
    look.title = "Open Annihilation 0.8.0";
    look.aside = "Windows x64";
    look.rows = {
        {"MOD", "Ridge 4.8 rev 3", kit::colour::text},
        {"RULES", "Differ from the host's", kit::colour::lock},
        {"", "Dev mode off", kit::colour::accent},
    };
    look.block = {"economy.wind", "ai.income"};
    return look;
}

void a_hover_card_shows_its_header_rows_and_block() {
    kit::HoverCardLook look = presence_card();
    // The header is the widest line: 9 + 6 + 70 + 6 + 39, and 6 either side.
    // Six lines of 12, and 6 over and under.
    const kit::Point size = kit::hover_card_size(fonts(), look);
    OA_CHECK(size.x == 142 && size.y == 84);
    const kit::Rect rect{40, 40, size.x, size.y};
    renderer::Surface surface = black(240, 180);
    kit::draw_hover_card(canvas_on(surface), rect, look);
    expect(surface, 40, 40, kit::colour::lock);
    expect(surface, 181, 123, kit::colour::lock);
    expect(surface, 41, 41, kit::colour::band);
    // The small OA mark, the title after it and the aside at the right.
    expect(surface, 46, 48, kit::colour::band);
    expect(surface, 47, 48, kit::colour::accent);
    expect(surface, 61, 47, kit::colour::text);
    expect(surface, 138, 47, kit::colour::hint);
    // Labels in a column 23 wide, values after it in their colours.
    expect(surface, 46, 59, kit::colour::hint);
    expect(surface, 69, 59, kit::colour::text);
    expect(surface, 46, 71, kit::colour::hint);
    expect(surface, 69, 71, kit::colour::lock);
    // A row without a label draws its value from the left.
    expect(surface, 46, 83, kit::colour::accent);
    // The block in the quiet colour.
    expect(surface, 46, 95, kit::colour::quiet);
    expect(surface, 46, 107, kit::colour::quiet);
    // No arrow.
    expect(surface, 60, 39, kBlack);

    // A fixed width is kept.
    look.width = 100;
    OA_CHECK(kit::hover_card_size(fonts(), look).x == 100);
    OA_CHECK(kit::hover_card_size(fonts(), look).y == 84);
    // No header: the lines alone.
    kit::HoverCardLook bare;
    bare.block = {"economy.wind"};
    const kit::Point bare_size = kit::hover_card_size(fonts(), bare);
    OA_CHECK(bare_size.x == 58 && bare_size.y == 24);
}

void a_hover_card_points_its_arrow_from_each_side() {
    kit::HoverCardLook look = presence_card();
    const kit::Rect rect{40, 40, 142, 84};

    look.arrow = kit::Side::top;
    look.arrow_at = 20;
    renderer::Surface surface = black(240, 180);
    kit::draw_hover_card(canvas_on(surface), rect, look);
    expect(surface, 60, 35, kit::colour::lock);
    expect(surface, 59, 35, kBlack);
    expect(surface, 61, 35, kBlack);
    expect(surface, 60, 34, kBlack);
    expect(surface, 56, 39, kit::colour::lock);
    expect(surface, 55, 39, kBlack);
    expect(surface, 64, 39, kit::colour::lock);
    expect(surface, 65, 39, kBlack);

    look.arrow = kit::Side::bottom;
    surface = black(240, 180);
    kit::draw_hover_card(canvas_on(surface), rect, look);
    expect(surface, 60, 124, kit::colour::lock);
    expect(surface, 56, 124, kit::colour::lock);
    expect(surface, 55, 124, kBlack);
    expect(surface, 60, 128, kit::colour::lock);
    expect(surface, 59, 128, kBlack);
    expect(surface, 60, 129, kBlack);

    look.arrow = kit::Side::left;
    look.arrow_at = 10;
    surface = black(240, 180);
    kit::draw_hover_card(canvas_on(surface), rect, look);
    expect(surface, 35, 50, kit::colour::lock);
    expect(surface, 35, 49, kBlack);
    expect(surface, 34, 50, kBlack);
    expect(surface, 39, 46, kit::colour::lock);
    expect(surface, 39, 45, kBlack);
    expect(surface, 39, 54, kit::colour::lock);
    expect(surface, 39, 55, kBlack);

    look.arrow = kit::Side::right;
    surface = black(240, 180);
    kit::draw_hover_card(canvas_on(surface), rect, look);
    expect(surface, 182, 46, kit::colour::lock);
    expect(surface, 186, 50, kit::colour::lock);
    expect(surface, 186, 49, kBlack);
    expect(surface, 187, 50, kBlack);
}

/// Checks a rectangle's place and size.
bool rect_is(const kit::Rect& rect, int32_t x, int32_t y, int32_t width, int32_t height) {
    return rect.x == x && rect.y == y && rect.width == width && rect.height == height;
}

void a_hover_card_stays_inside_its_frame() {
    const kit::Rect frame{0, 0, 640, 480};
    const kit::Point size{100, 60};
    // Below the anchor, the arrow's depth under it.
    OA_CHECK(rect_is(kit::hover_card_rect({100, 100, 20, 20}, frame, size), 100, 125, 100, 60));
    // At the bottom edge it goes over the anchor.
    OA_CHECK(rect_is(kit::hover_card_rect({100, 440, 20, 20}, frame, size), 100, 375, 100, 60));
    // At the right edge it moves left; at the left edge right.
    OA_CHECK(rect_is(kit::hover_card_rect({600, 100, 20, 20}, frame, size), 540, 125, 100, 60));
    OA_CHECK(
        rect_is(kit::hover_card_rect({10, 100, 20, 20}, {50, 0, 590, 480}, size), 50, 125, 100, 60)
    );
    // At the top edge, an anchor over the frame puts it at the frame's top.
    OA_CHECK(rect_is(kit::hover_card_rect({100, -40, 20, 20}, frame, size), 100, 0, 100, 60));
    // No room below or above: as low as the frame allows.
    OA_CHECK(
        rect_is(kit::hover_card_rect({100, 40, 20, 20}, {0, 0, 640, 100}, size), 100, 40, 100, 60)
    );
    // Larger than the frame: at its top left.
    OA_CHECK(rect_is(kit::hover_card_rect({100, 100, 20, 20}, frame, {700, 500}), 0, 0, 700, 500));
}

void a_hover_card_waits_for_the_pointer_to_settle() {
    OA_CHECK(kit::hover_card_delay_ms == 400);
    kit::HoverTimer timer;
    OA_CHECK(!kit::hover_card_due(timer, 5, 1000));
    OA_CHECK(timer.over == 5 && timer.since_ms == 1000);
    OA_CHECK(!kit::hover_card_due(timer, 5, 1399));
    OA_CHECK(kit::hover_card_due(timer, 5, 1400));
    OA_CHECK(kit::hover_card_due(timer, 5, 2000));
    // Another control starts the wait again.
    OA_CHECK(!kit::hover_card_due(timer, 6, 2100));
    OA_CHECK(!kit::hover_card_due(timer, 6, 2499));
    OA_CHECK(kit::hover_card_due(timer, 6, 2500));
    // No control ends it, and coming back waits again.
    OA_CHECK(!kit::hover_card_due(timer, kit::no_control, 2600));
    OA_CHECK(timer.over == kit::no_control);
    OA_CHECK(!kit::hover_card_due(timer, 6, 2700));
    OA_CHECK(!kit::hover_card_due(timer, 6, 3099));
    OA_CHECK(kit::hover_card_due(timer, 6, 3100));
    // Two timers keep their own waits.
    kit::HoverTimer other;
    OA_CHECK(!kit::hover_card_due(other, 6, 3100));
    OA_CHECK(kit::hover_card_due(timer, 6, 3101));
    OA_CHECK(!kit::hover_card_due(other, 6, 3101));
}

// ---- Links ----

void a_link_in_each_state() {
    kit::LinkLook look{"MORE MAPS..."};
    OA_CHECK(kit::link_width(fonts(), look) == 42);
    const kit::Rect rect{10, 10, 42, kit::compact_metrics.small_line};
    renderer::Surface surface = black(60, 30);
    kit::draw_link(canvas_on(surface), rect, look);
    expect(surface, 10, 11, kit::colour::accent);
    look.hovered = true;
    kit::draw_link(canvas_on(surface), rect, look);
    expect(surface, 10, 11, kit::colour::accent_light);
    look.enabled = false;
    kit::draw_link(canvas_on(surface), rect, look);
    expect(surface, 10, 11, kit::colour::switch_idle);
}

// ---- Display lists ----

/// Every add_ names its controls with what automation reads, and paint of
/// the list draws what the direct draws draw.
void paint_matches_each_direct_draw(int32_t scale) {
    const int32_t width = 460 * scale;
    const int32_t height = 300 * scale;
    renderer::Surface direct = black(width, height);
    renderer::Surface painted = black(width, height);
    kit::DisplayList list;
    const kit::Canvas on_direct = canvas_on(direct, scale);

    const OneColourPicture badge(kit::colour::online);
    kit::ListRowLook row;
    row.badge = badge.picture();
    row.title = "Ridge";
    row.by = "by the Ridge team";
    row.lines = {"Balance overhaul"};
    row.aside = {"4.8 rev 3", "41.0 MB"};
    row.chip = kit::ChipLook{"UPDATE", kit::ChipState::update};
    row.tags = {kit::ChipLook{"SIM", kit::ChipState::filter}};
    row.selected = true;
    const kit::Rect row_rect{4, 4, 400, 48};
    kit::draw_list_row(on_direct, row_rect, row);
    kit::add_list_row(list, row_rect, row, 1, "library.row.ridge");

    const kit::ChipLook chip{"ALL", kit::ChipState::filter, true};
    const kit::Rect chip_rect{4, 56, kit::chip_width(fonts(), chip), 12};
    kit::draw_chip(on_direct, chip_rect, chip);
    kit::add_chip(list, chip_rect, chip, 2, "library.filter.all");
    const kit::ChipLook inside{"BASE", kit::ChipState::get};
    const kit::Rect inside_rect{60, 56, kit::chip_width(fonts(), inside), 12};
    kit::draw_chip(on_direct, inside_rect, inside);
    kit::add_chip(list, inside_rect, inside, kit::no_control, "");

    kit::SearchLook search;
    search.placeholder = "Search mods";
    search.field = {"ridge", 2};
    search.focused = true;
    const kit::Rect search_rect{4, 72, 160, 16};
    kit::draw_search(on_direct, search_rect, search);
    kit::add_search(list, search_rect, search, 3, "library.search");

    kit::TabsLook tabs;
    tabs.captions = {"MODS", "MAPS", "UPDATES"};
    tabs.counts = {0, 0, 2};
    tabs.selected = 1;
    tabs.hovered = 2;
    const kit::Rect strip{4, 92, 300, 18};
    kit::draw_tabs(on_direct, strip, tabs);
    const std::array<kit::ControlId, 3> tab_ids{4, 5, 6};
    const std::array<std::string, 3> tab_names{
        "library.tab.mods", "library.tab.maps", "library.tab.updates"
    };
    kit::add_tabs(list, fonts(), strip, tabs, tab_ids, tab_names);

    kit::CardLook card;
    card.placeholder = "Loading preview";
    card.title = "Isle of Ashes";
    card.subtitle = "4 - 16x16";
    card.chips = {kit::ChipLook{"BASE", kit::ChipState::get}};
    card.greyed = true;
    const kit::Rect card_rect{4, 114, 120, kit::card_height(120)};
    kit::draw_card(on_direct, card_rect, card);
    kit::add_card(list, card_rect, card, 7, "maps.card.isle");

    kit::LinkLook link{"MORE MAPS...", true, true};
    const kit::Rect link_rect{140, 114, kit::link_width(fonts(), link), 12};
    kit::draw_link(on_direct, link_rect, link);
    kit::add_link(list, link_rect, link, 8, "library.more-maps");

    kit::HoverCardLook hover = presence_card();
    hover.arrow = kit::Side::left;
    hover.arrow_at = 12;
    const kit::Point size = kit::hover_card_size(fonts(), hover);
    const kit::Rect hover_rect{200, 140, size.x, size.y};
    kit::draw_hover_card(on_direct, hover_rect, hover);
    kit::add_hover_card(list, hover_rect, hover);

    kit::paint(canvas_on(painted, scale), list);
    same(direct, painted);

    OA_CHECK(list.items.size() == 8u);
    OA_CHECK(list.controls.size() == 8u);
    OA_CHECK(kit::name_problem(list).empty());

    const kit::Control* control = kit::control_of(list, 1);
    OA_CHECK(control != nullptr && control->name == "library.row.ridge");
    OA_CHECK(control->kind == kit::ControlKind::list_item && !control->steps);
    OA_CHECK(control->checked && control->enabled && control->text == "Ridge");

    control = kit::control_of(list, 2);
    OA_CHECK(control != nullptr && control->name == "library.filter.all");
    OA_CHECK(control->kind == kit::ControlKind::button && !control->steps);
    OA_CHECK(control->checked && control->enabled && control->text == "ALL");

    control = kit::control_of(list, 3);
    OA_CHECK(control != nullptr && control->name == "library.search");
    OA_CHECK(control->kind == kit::ControlKind::text_field && control->steps);
    OA_CHECK(!control->checked && control->text == "ridge");

    const std::array<std::string, 3> tab_texts{"MODS", "MAPS", "UPDATES 2"};
    const std::vector<kit::Rect> tab_places = kit::tab_rects(fonts(), strip, tabs);
    for (std::size_t index = 0; index < 3; ++index) {
        control = kit::control_of(list, tab_ids[index]);
        OA_CHECK(control != nullptr && control->name == tab_names[index]);
        OA_CHECK(control->kind == kit::ControlKind::tab && !control->steps);
        OA_CHECK(control->checked == (index == 1));
        OA_CHECK(control->text == tab_texts[index]);
        OA_CHECK(rect_is(
            control->rect,
            tab_places[index].x,
            tab_places[index].y,
            tab_places[index].width,
            tab_places[index].height
        ));
    }

    control = kit::control_of(list, 7);
    OA_CHECK(control != nullptr && control->name == "maps.card.isle");
    OA_CHECK(control->kind == kit::ControlKind::list_item && !control->steps);
    OA_CHECK(control->enabled && control->text == "Isle of Ashes");

    control = kit::control_of(list, 8);
    OA_CHECK(control != nullptr && control->name == "library.more-maps");
    OA_CHECK(control->kind == kit::ControlKind::link && !control->steps);
    OA_CHECK(control->enabled && control->text == "MORE MAPS...");

    // The items carry their roles in the order they were added.
    const std::array<kit::Role, 8> roles{
        kit::Role::list_row,
        kit::Role::chip,
        kit::Role::chip,
        kit::Role::field,
        kit::Role::tabs,
        kit::Role::card,
        kit::Role::link,
        kit::Role::hover_card,
    };
    for (std::size_t index = 0; index < roles.size(); ++index)
        OA_CHECK(list.items[index].role == roles[index]);
}

/// A disabled chip takes no press, and a disabled link none either.
void a_disabled_control_takes_no_press() {
    kit::DisplayList list;
    kit::add_chip(list, {0, 0, 30, 12}, {"GET", kit::ChipState::get, false, false, true}, 1, "c");
    kit::add_link(list, {0, 20, 30, 12}, {"BACK", false, false}, 2, "back");
    OA_CHECK(!list.controls[0].enabled && !list.controls[1].enabled);
    OA_CHECK(kit::hit(list, {5, 5}) == kit::no_control);
    OA_CHECK(kit::hit(list, {5, 25}) == kit::no_control);
}

} // namespace

int main() {
    a_short_row_lies_as_the_mods_row();
    a_full_row_cuts_its_summary_before_the_aside();
    a_blank_badge_and_tags();
    chips_take_every_state();
    a_search_field_shows_its_placeholder_and_magnifier();
    a_focused_field_shows_its_text_and_caret();
    a_long_text_scrolls_so_its_caret_shows();
    typing_moves_the_caret_drawn();
    tabs_draw_with_a_count_and_with_none();
    cards_lie_in_a_grid();
    a_card_shows_its_placeholder_chips_and_lines();
    a_greyed_card_fades_into_the_panel();
    the_arrows_move_through_a_grid_of_cards();
    a_hover_card_shows_its_header_rows_and_block();
    a_hover_card_points_its_arrow_from_each_side();
    a_hover_card_stays_inside_its_frame();
    a_hover_card_waits_for_the_pointer_to_settle();
    a_link_in_each_state();
    paint_matches_each_direct_draw(1);
    paint_matches_each_direct_draw(2);
    a_disabled_control_takes_no_press();
    return oa::test::check_exit_status();
}
