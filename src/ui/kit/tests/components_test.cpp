// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The kit's controls, pinned to literal colours, and paint against a direct
// draw. The geometry literals were computed with the settings dialog's
// functions at ecf703fb, the commit this ticket started from.

#include "oa/formats/fnt.hpp"
#include "oa/test/check.hpp"
#include "oa/ui/frontend_renderer.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/kit/components.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
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

/// The synthetic font from the settings pixel test: glyph b is 2 + b % 4
/// columns by 8 rows, set where (x + y + byte) % 3 is not 0.
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

void expect(
    const renderer::Surface& surface, int32_t x, int32_t y, uint8_t red, uint8_t green, uint8_t blue
) {
    const auto width = static_cast<int32_t>(surface.width);
    const auto height = static_cast<int32_t>(surface.height);
    if (x < 0 || y < 0 || x >= width || y >= height) {
        std::fprintf(stderr, "pixel %d,%d is outside %dx%d\n", x, y, width, height);
        OA_CHECK(false);
        return;
    }
    const std::size_t at =
        (static_cast<std::size_t>(y) * surface.width + static_cast<std::size_t>(x)) * 3;
    const uint8_t got_red = surface.rgb[at];
    const uint8_t got_green = surface.rgb[at + 1];
    const uint8_t got_blue = surface.rgb[at + 2];
    if (got_red != red || got_green != green || got_blue != blue) {
        std::fprintf(
            stderr,
            "pixel %d,%d is #%02x%02x%02x, expected #%02x%02x%02x\n",
            x,
            y,
            got_red,
            got_green,
            got_blue,
            red,
            green,
            blue
        );
        OA_CHECK(false);
    }
}

void same(const renderer::Surface& left, const renderer::Surface& right) {
    if (left.width != right.width || left.height != right.height || left.rgb != right.rgb) {
        const uint32_t width = left.width < right.width ? left.width : right.width;
        const uint32_t height = left.height < right.height ? left.height : right.height;
        bool reported = false;
        for (uint32_t y = 0; y < height && !reported; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const std::size_t at = (static_cast<std::size_t>(y) * left.width + x) * 3;
                const std::size_t other = (static_cast<std::size_t>(y) * right.width + x) * 3;
                if (at + 2 >= left.rgb.size() || other + 2 >= right.rgb.size() ||
                    left.rgb[at] != right.rgb[other] || left.rgb[at + 1] != right.rgb[other + 1] ||
                    left.rgb[at + 2] != right.rgb[other + 2]) {
                    std::fprintf(stderr, "surfaces differ at %u,%u\n", x, y);
                    reported = true;
                    break;
                }
            }
        }
        if (!reported)
            std::fprintf(
                stderr,
                "surfaces differ in size %ux%u and %ux%u\n",
                left.width,
                left.height,
                right.width,
                right.height
            );
    }
    OA_CHECK(left.width == right.width && left.height == right.height && left.rgb == right.rgb);
}

kit::ButtonLook
button(std::string caption, kit::ButtonStyle style, bool hovered, bool held, bool enabled = true) {
    return {std::move(caption), style, hovered, held, enabled};
}

constexpr kit::Rect kButtonArea{10, 10, 40, 17};

void pin_button(
    const kit::ButtonLook& look,
    uint8_t border_red,
    uint8_t border_green,
    uint8_t border_blue,
    uint8_t face_red,
    uint8_t face_green,
    uint8_t face_blue,
    uint8_t text_red,
    uint8_t text_green,
    uint8_t text_blue
) {
    renderer::Surface surface = black(80, 40);
    kit::draw_button(canvas_on(surface), kButtonArea, look);
    expect(surface, 10, 10, border_red, border_green, border_blue);
    expect(surface, 11, 11, face_red, face_green, face_blue);
    expect(surface, 28, 13, text_red, text_green, text_blue);
}

void buttons_match_the_five_looks() {
    pin_button(
        button("A", kit::ButtonStyle::accent, false, false),
        0xb6,
        0xe0,
        0x5a,
        0x9c,
        0xcc,
        0x3c,
        0x10,
        0x12,
        0x0d
    );
    pin_button(
        button("A", kit::ButtonStyle::accent, true, false),
        0xb6,
        0xe0,
        0x5a,
        0xb6,
        0xe0,
        0x5a,
        0x10,
        0x12,
        0x0d
    );
    pin_button(
        button("A", kit::ButtonStyle::accent, true, true),
        0xb6,
        0xe0,
        0x5a,
        0x8a,
        0xb8,
        0x30,
        0x10,
        0x12,
        0x0d
    );
    pin_button(
        button("A", kit::ButtonStyle::plain, false, false),
        0x3a,
        0x40,
        0x34,
        0x00,
        0x00,
        0x00,
        0xc9,
        0xcd,
        0xbf
    );
    pin_button(
        button("A", kit::ButtonStyle::plain, true, false),
        0x5b,
        0x63,
        0x52,
        0x20,
        0x24,
        0x1c,
        0xe7,
        0xe8,
        0xdf
    );
    pin_button(
        button("A", kit::ButtonStyle::plain, false, true),
        0x3a,
        0x40,
        0x34,
        0x20,
        0x24,
        0x1c,
        0xc9,
        0xcd,
        0xbf
    );
    pin_button(
        button("A", kit::ButtonStyle::plain, true, true, false),
        0x3a,
        0x40,
        0x34,
        0x00,
        0x00,
        0x00,
        0x7d,
        0x84,
        0x74
    );
    pin_button(
        button("A", kit::ButtonStyle::quiet, false, false),
        0x3a,
        0x40,
        0x34,
        0x00,
        0x00,
        0x00,
        0xc9,
        0xcd,
        0xbf
    );
    pin_button(
        button("A", kit::ButtonStyle::quiet, true, false),
        0x5b,
        0x63,
        0x52,
        0x20,
        0x24,
        0x1c,
        0xe7,
        0xe8,
        0xdf
    );
    pin_button(
        button("A", kit::ButtonStyle::quiet, true, true),
        0x5b,
        0x63,
        0x52,
        0x14,
        0x16,
        0x12,
        0xe7,
        0xe8,
        0xdf
    );
    pin_button(
        button("A", kit::ButtonStyle::inset, false, false),
        0x3a,
        0x40,
        0x34,
        0x17,
        0x1a,
        0x15,
        0xc9,
        0xcd,
        0xbf
    );
    pin_button(
        button("A", kit::ButtonStyle::inset, true, false),
        0x5b,
        0x63,
        0x52,
        0x20,
        0x24,
        0x1c,
        0xe7,
        0xe8,
        0xdf
    );
    pin_button(
        button("A", kit::ButtonStyle::inset, true, true),
        0x5b,
        0x63,
        0x52,
        0x14,
        0x16,
        0x12,
        0xe7,
        0xe8,
        0xdf
    );
}

void switch_levels_and_slider_match() {
    renderer::Surface off = black(80, 40);
    kit::draw_switch(canvas_on(off), {10, 10, 52, 16}, {false, false, false, "OFF", "ON"});
    expect(off, 10, 10, 0x3a, 0x40, 0x34);
    expect(off, 11, 11, 0x2c, 0x32, 0x26);

    renderer::Surface on = black(80, 40);
    kit::draw_switch(canvas_on(on), {10, 10, 52, 16}, {true, false, false, "OFF", "ON"});
    expect(on, 36, 11, 0x9c, 0xcc, 0x3c);

    renderer::Surface locked = black(80, 40);
    kit::draw_switch(canvas_on(locked), {10, 10, 52, 16}, {true, true, true, "OFF", "ON"});
    expect(locked, 36, 11, 0x5b, 0x63, 0x52);
    expect(locked, 10, 10, 0x5b, 0x63, 0x52);

    const kit::Rect strip{30, 30, 71, 16};
    kit::LevelsLook levels{{"", "", ""}, 23, 0, 2, false, false};
    renderer::Surface levels_surface = black(120, 60);
    kit::draw_levels(canvas_on(levels_surface), strip, levels);
    expect(levels_surface, 30, 30, 0x3a, 0x40, 0x34);
    expect(levels_surface, 31, 31, 0x9c, 0xcc, 0x3c);
    expect(levels_surface, 77, 31, 0x16, 0x18, 0x14);

    levels.hovered = true;
    renderer::Surface hovered_strip = black(120, 60);
    kit::draw_levels(canvas_on(hovered_strip), strip, levels);
    expect(hovered_strip, 30, 30, 0x5b, 0x63, 0x52);

    levels.hovered = false;
    levels.locked = true;
    renderer::Surface locked_strip = black(120, 60);
    kit::draw_levels(canvas_on(locked_strip), strip, levels);
    expect(locked_strip, 31, 31, 0x5b, 0x63, 0x52);

    const kit::Rect wide{10, 40, 100, 14};
    renderer::Surface slider = black(130, 110);
    kit::draw_slider(canvas_on(slider), wide, {2, 5, false, false});
    expect(slider, 57, 40, 0x10, 0x12, 0x0d);
    expect(slider, 58, 41, 0x9c, 0xcc, 0x3c);
    expect(slider, 13, 52, 0x3a, 0x40, 0x34);
    expect(slider, 11, 45, 0x9c, 0xcc, 0x3c);

    renderer::Surface narrow = black(40, 110);
    kit::draw_slider(canvas_on(narrow), {10, 80, 20, 14}, {0, 5, false, false});
    expect(narrow, 13, 92, 0x00, 0x00, 0x00);

    renderer::Surface slider_locked = black(130, 70);
    kit::draw_slider(canvas_on(slider_locked), wide, {2, 5, true, true});
    expect(slider_locked, 58, 41, 0x5b, 0x63, 0x52);
    expect(slider_locked, 11, 45, 0x3a, 0x40, 0x34);

    renderer::Surface slider_hot = black(130, 70);
    kit::draw_slider(canvas_on(slider_hot), wide, {2, 5, false, true});
    expect(slider_hot, 58, 41, 0xb6, 0xe0, 0x5a);
}

void choice_lock_and_scroll_match() {
    const kit::Rect field{40, 20, 80, 16};
    renderer::Surface closed = black(140, 50);
    kit::draw_choice(canvas_on(closed), field, {"Language", false, false});
    expect(closed, 40, 20, 0x3a, 0x40, 0x34);
    expect(closed, 41, 21, 0x12, 0x14, 0x10);
    expect(closed, 108, 26, 0xc9, 0xcd, 0xbf);

    renderer::Surface open = black(140, 50);
    kit::draw_choice(canvas_on(open), field, {"Language", false, true});
    expect(open, 40, 20, 0x9c, 0xcc, 0x3c);
    expect(open, 108, 26, 0x9c, 0xcc, 0x3c);

    renderer::Surface hot = black(140, 50);
    kit::draw_choice(canvas_on(hot), field, {"Language", true, false});
    expect(hot, 40, 20, 0x5b, 0x63, 0x52);

    kit::ChoiceMenuLook menu;
    menu.shown = {"a", "b", "c", "d", "e", "f", "g", "h"};
    menu.first = 2;
    menu.total = 10;
    menu.chosen = 2;
    menu.marked = 4;
    menu.focus_shown = true;
    const kit::Rect menu_rect{20, 46, 80, 130};
    renderer::Surface menu_surface = black(120, 200);
    kit::draw_choice_menu(canvas_on(menu_surface), menu_rect, menu);
    expect(menu_surface, 20, 46, 0x5b, 0x63, 0x52);
    expect(menu_surface, 26, 51, 0x9c, 0xcc, 0x3c);
    expect(menu_surface, 22, 64, 0x17, 0x1a, 0x15);
    expect(menu_surface, 21, 79, 0x9c, 0xcc, 0x3c);
    expect(menu_surface, 22, 80, 0x20, 0x24, 0x1c);
    expect(menu_surface, 97, 73, 0x5b, 0x63, 0x52);

    menu.focus_shown = false;
    renderer::Surface unfocused = black(120, 200);
    kit::draw_choice_menu(canvas_on(unfocused), menu_rect, menu);
    expect(unfocused, 21, 79, 0x20, 0x24, 0x1c);

    renderer::Surface lock = black(100, 40);
    kit::draw_lock(canvas_on(lock), {10, 10, 80, 16}, {"LOCK"});
    expect(lock, 65, 14, 0x00, 0x00, 0x00);
    expect(lock, 66, 14, 0xe0, 0xb0, 0x4f);
    expect(lock, 73, 13, 0xe0, 0xb0, 0x4f);

    const kit::Rect well{10, 10, 7, 40};
    const kit::Rect thumb{12, 14, 5, 20};
    renderer::Surface bar = black(30, 60);
    kit::draw_scroll_bar(canvas_on(bar), well, thumb, false);
    expect(bar, 10, 10, 0x3a, 0x40, 0x34);
    expect(bar, 11, 11, 0x12, 0x14, 0x10);
    expect(bar, 12, 14, 0x5b, 0x63, 0x52);

    renderer::Surface hot_bar = black(30, 60);
    kit::draw_scroll_bar(canvas_on(hot_bar), well, thumb, true);
    expect(hot_bar, 10, 10, 0x5b, 0x63, 0x52);
    expect(hot_bar, 12, 14, 0x7d, 0x84, 0x74);
}

renderer::RgbaPicture red_icon() {
    static const std::array<uint8_t, 16> pixels{
        255,
        0,
        0,
        255,
        255,
        0,
        0,
        255,
        255,
        0,
        0,
        255,
        255,
        0,
        0,
        255,
    };
    return {2, 2, pixels};
}

void oa_button_and_mark_match() {
    renderer::Surface idle = black(40, 40);
    kit::draw_oa_button(canvas_on(idle), 32, kit::OaButtonState::idle);
    expect(idle, 0, 0, 0x4a, 0x51, 0x43);
    expect(idle, 1, 1, 0x1b, 0x1e, 0x19);
    expect(idle, 6, 6, 0x9c, 0xcc, 0x3c);
    expect(idle, 11, 12, 0x9c, 0xcc, 0x3c);

    renderer::Surface pressed = black(40, 40);
    kit::draw_oa_button(canvas_on(pressed), 32, kit::OaButtonState::pressed);
    expect(pressed, 0, 0, 0x08, 0x09, 0x07);
    expect(pressed, 11, 12, 0xb6, 0xe0, 0x5a);

    renderer::Surface small = black(30, 30);
    kit::draw_oa_button(canvas_on(small), 24, kit::OaButtonState::idle);
    expect(small, 4, 4, 0x9c, 0xcc, 0x3c);
    expect(small, 7, 8, 0x1b, 0x1e, 0x19);
    expect(small, 8, 8, 0x9c, 0xcc, 0x3c);

    renderer::Surface small_hot = black(30, 30);
    kit::draw_oa_button(canvas_on(small_hot), 24, kit::OaButtonState::hovered);
    expect(small_hot, 8, 8, 0xb6, 0xe0, 0x5a);
    expect(small_hot, 1, 1, 0x20, 0x24, 0x1c);

    renderer::Surface small_held = black(30, 30);
    kit::draw_oa_button(canvas_on(small_held), 24, kit::OaButtonState::pressed);
    expect(small_held, 0, 0, 0x08, 0x09, 0x07);

    kit::Canvas with_icon = canvas_on(idle);
    with_icon.icon = red_icon();
    renderer::Surface icon_idle = black(40, 40);
    with_icon.surface = &icon_idle;
    kit::draw_oa_button(with_icon, 32, kit::OaButtonState::idle);
    expect(icon_idle, 3, 3, 0xff, 0x00, 0x00);

    renderer::Surface icon_hot = black(40, 40);
    with_icon.surface = &icon_hot;
    kit::draw_oa_button(with_icon, 32, kit::OaButtonState::hovered);
    expect(icon_hot, 1, 1, 0x9c, 0xcc, 0x3c);
    expect(icon_hot, 2, 2, 0x20, 0x24, 0x1c);
    expect(icon_hot, 3, 3, 0xff, 0x00, 0x00);

    renderer::Surface icon_held = black(40, 40);
    with_icon.surface = &icon_held;
    kit::draw_oa_button(with_icon, 32, kit::OaButtonState::pressed);
    expect(icon_held, 0, 0, 0x08, 0x09, 0x07);
    expect(icon_held, 3, 3, 0x14, 0x16, 0x12);
    expect(icon_held, 4, 4, 0xff, 0x00, 0x00);

    renderer::Surface icon_small = black(30, 30);
    with_icon.surface = &icon_small;
    kit::draw_oa_button(with_icon, 24, kit::OaButtonState::hovered);
    expect(icon_small, 1, 1, 0x9c, 0xcc, 0x3c);
    expect(icon_small, 3, 3, 0xff, 0x00, 0x00);

    renderer::Surface mark = black(40, 40);
    kit::draw_oa_mark(canvas_on(mark), 32);
    expect(mark, 0, 0, 0x00, 0x00, 0x00);
    expect(mark, 6, 6, 0x9c, 0xcc, 0x3c);

    renderer::Surface mark_icon = black(40, 40);
    with_icon.surface = &mark_icon;
    kit::draw_oa_mark(with_icon, 32);
    expect(mark_icon, 0, 0, 0xff, 0x00, 0x00);

    renderer::Surface nothing = black(8, 8);
    kit::draw_oa_mark(canvas_on(nothing), 0);
    expect(nothing, 0, 0, 0x00, 0x00, 0x00);
}

void clip_hides_what_it_misses() {
    const kit::ButtonLook look = button("A", kit::ButtonStyle::accent, false, false);
    renderer::Surface missed = black(80, 40);
    kit::Canvas canvas = canvas_on(missed);
    kit::draw_button(kit::clipped(canvas, {0, 0, 5, 5}), kButtonArea, look);
    expect(missed, 10, 10, 0x00, 0x00, 0x00);

    renderer::Surface edge = black(80, 40);
    kit::draw_button(kit::clipped(canvas_on(edge), {10, 10, 1, 1}), kButtonArea, look);
    expect(edge, 10, 10, 0xb6, 0xe0, 0x5a);
    expect(edge, 11, 11, 0x00, 0x00, 0x00);
}

void paint_matches_the_direct_draw(int32_t scale) {
    const int32_t side = 256 * scale;
    const kit::ButtonLook look = button("A", kit::ButtonStyle::accent, true, false);
    const kit::Rect area = kButtonArea;
    renderer::Surface direct = black(side, side);
    renderer::Surface painted = black(side, side);
    kit::draw_button(canvas_on(direct, scale), area, look);
    kit::DisplayList list;
    kit::add_button(list, area, look, 3, "ok");
    kit::paint(canvas_on(painted, scale), list);
    same(direct, painted);

    OA_CHECK(list.controls.size() == 1);
    OA_CHECK(list.controls[0].name == "ok");
    OA_CHECK(list.controls[0].kind == kit::ControlKind::button);
    OA_CHECK(list.controls[0].steps == false);
    OA_CHECK(list.controls[0].checked == false);
    OA_CHECK(list.controls[0].enabled == true);

    kit::DisplayList clipped_list;
    kit::add_button(clipped_list, area, look, 3, "ok");
    clipped_list.items[0].clip = {10, 10, 1, 1};
    renderer::Surface clipped_direct = black(side, side);
    renderer::Surface clipped_paint = black(side, side);
    kit::draw_button(kit::clipped(canvas_on(clipped_direct, scale), {10, 10, 1, 1}), area, look);
    kit::paint(canvas_on(clipped_paint, scale), clipped_list);
    same(clipped_direct, clipped_paint);
}

void paint_matches_each_control(int32_t scale) {
    const int32_t side = 400 * scale;
    const kit::SwitchLook toggle{true, false, false, "OFF", "ON"};
    const kit::Rect toggle_rect{10, 40, 52, 16};
    renderer::Surface direct = black(side, side);
    renderer::Surface painted = black(side, side);
    kit::draw_switch(canvas_on(direct, scale), toggle_rect, toggle);
    kit::DisplayList list;
    kit::add_switch(list, toggle_rect, toggle, 4, "sound");
    kit::paint(canvas_on(painted, scale), list);
    same(direct, painted);
    OA_CHECK(list.controls[0].name == "sound");
    OA_CHECK(list.controls[0].kind == kit::ControlKind::toggle);
    OA_CHECK(list.controls[0].steps == true);
    OA_CHECK(list.controls[0].checked == true);
    OA_CHECK(list.controls[0].text == "ON");

    const kit::LevelsLook levels{{"Off", "2x"}, 23, 0, 2, false, true};
    const kit::Rect strip{30, 70, 48, 16};
    direct = black(side, side);
    painted = black(side, side);
    list = {};
    kit::draw_levels(canvas_on(direct, scale), strip, levels);
    kit::add_levels(list, strip, levels, 5, "alias");
    kit::paint(canvas_on(painted, scale), list);
    same(direct, painted);
    OA_CHECK(list.controls[0].kind == kit::ControlKind::levels);
    OA_CHECK(list.controls[0].steps == true);
    OA_CHECK(list.controls[0].checked == false);
    OA_CHECK(list.controls[0].enabled == false);
    OA_CHECK(list.controls[0].text == "Off");
    OA_CHECK(list.controls[0].name == "alias");

    const kit::SliderLook slider{2, 5, false, true};
    const kit::Rect track{10, 100, 100, 14};
    direct = black(side, side);
    painted = black(side, side);
    list = {};
    kit::draw_slider(canvas_on(direct, scale), track, slider);
    kit::add_slider(list, track, slider, 6, "volume");
    kit::paint(canvas_on(painted, scale), list);
    same(direct, painted);
    OA_CHECK(list.controls[0].kind == kit::ControlKind::slider);
    OA_CHECK(list.controls[0].steps == true);
    OA_CHECK(list.controls[0].text == "2");
    OA_CHECK(list.controls[0].name == "volume");

    const kit::ChoiceLook choice{"English", true, false};
    const kit::Rect field{40, 130, 80, 16};
    direct = black(side, side);
    painted = black(side, side);
    list = {};
    kit::draw_choice(canvas_on(direct, scale), field, choice);
    kit::add_choice(list, field, choice, 7, "language");
    kit::paint(canvas_on(painted, scale), list);
    same(direct, painted);
    OA_CHECK(list.controls[0].kind == kit::ControlKind::choice);
    OA_CHECK(list.controls[0].steps == true);
    OA_CHECK(list.controls[0].checked == false);
    OA_CHECK(list.controls[0].text == "English");
    OA_CHECK(list.controls[0].name == "language");

    const kit::Rect ring{10, 10, 40, 17};
    direct = black(side, side);
    painted = black(side, side);
    list = {};
    kit::draw_focus_ring(canvas_on(direct, scale), ring, true);
    kit::add_focus_ring(list, ring, true);
    kit::paint(canvas_on(painted, scale), list);
    same(direct, painted);
    OA_CHECK(list.controls.empty());

    const kit::LockLook lock{"LOCK"};
    const kit::Rect lock_area{10, 10, 80, 16};
    direct = black(side, side);
    painted = black(side, side);
    list = {};
    kit::draw_lock(canvas_on(direct, scale), lock_area, lock);
    kit::add_lock(list, lock_area, lock);
    kit::paint(canvas_on(painted, scale), list);
    same(direct, painted);
    OA_CHECK(list.controls.empty());

    direct = black(side, side);
    painted = black(side, side);
    list = {};
    kit::draw_mark(canvas_on(direct, scale), kit::Mark::padlock, {12, 14}, kit::colour::lock);
    kit::add_mark(list, {12, 14}, kit::Mark::padlock, kit::colour::lock);
    kit::paint(canvas_on(painted, scale), list);
    same(direct, painted);
    OA_CHECK(list.controls.empty());

    kit::ChoiceMenuLook menu_look;
    menu_look.shown = {"a", "b", "c", "d", "e", "f", "g", "h"};
    menu_look.first = 2;
    menu_look.total = 10;
    menu_look.chosen = 2;
    menu_look.marked = 4;
    menu_look.focus_shown = true;
    const kit::Rect menu{20, 46, 80, 130};
    direct = black(side, side);
    painted = black(side, side);
    kit::Item menu_item;
    menu_item.role = kit::Role::choice_menu;
    menu_item.rect = menu;
    menu_item.look = menu_look;
    list = {};
    list.items.push_back(menu_item);
    kit::draw_choice_menu(canvas_on(direct, scale), menu, menu_look);
    kit::paint(canvas_on(painted, scale), list);
    same(direct, painted);

    const kit::Rect well{10, 10, 7, 40};
    const kit::Rect thumb{12, 14, 5, 20};
    direct = black(side, side);
    painted = black(side, side);
    kit::Item bar;
    bar.role = kit::Role::scroll_bar;
    bar.rect = well;
    bar.look = kit::ScrollBarLook{thumb, true};
    list = {};
    list.items.push_back(bar);
    kit::draw_scroll_bar(canvas_on(direct, scale), well, thumb, true);
    kit::paint(canvas_on(painted, scale), list);
    same(direct, painted);

    const kit::Point origin{4, 6};
    direct = black(side, side);
    painted = black(side, side);
    kit::Canvas shifted = canvas_on(direct, scale);
    shifted.placement.x = origin.x * scale;
    shifted.placement.y = origin.y * scale;
    kit::draw_oa_button(shifted, 32, kit::OaButtonState::pressed);
    kit::Item oa;
    oa.role = kit::Role::oa_button;
    oa.rect = {origin.x, origin.y, 32, 32};
    oa.look = kit::OaButtonLook{32, kit::OaButtonState::pressed};
    list = {};
    list.items.push_back(oa);
    kit::paint(canvas_on(painted, scale), list);
    same(direct, painted);

    direct = black(side, side);
    painted = black(side, side);
    shifted = canvas_on(direct, scale);
    shifted.placement.x = origin.x * scale;
    shifted.placement.y = origin.y * scale;
    shifted.icon = red_icon();
    kit::draw_oa_mark(shifted, 24);
    kit::Item mark;
    mark.role = kit::Role::oa_mark;
    mark.rect = {origin.x, origin.y, 24, 24};
    list = {};
    list.items.push_back(mark);
    kit::Canvas marked = canvas_on(painted, scale);
    marked.icon = red_icon();
    kit::paint(marked, list);
    same(direct, painted);
}

void later_items_cover_earlier_ones() {
    kit::DisplayList list;
    kit::Item first;
    first.role = kit::Role::fill;
    first.rect = {2, 2, 4, 4};
    first.colour = kit::colour::accent;
    kit::Item second = first;
    second.colour = kit::colour::text;
    list.items.push_back(first);
    list.items.push_back(second);
    renderer::Surface surface = black(16, 16);
    kit::paint(canvas_on(surface), list);
    expect(surface, 3, 3, 0xe7, 0xe8, 0xdf);

    list.items = {second, first};
    surface = black(16, 16);
    kit::paint(canvas_on(surface), list);
    expect(surface, 3, 3, 0x9c, 0xcc, 0x3c);
}

void geometry_matches_the_start_commit() {
    const kit::Rect track{10, 20, 100, 14};
    OA_CHECK(kit::knob_column(track, 0, 1) == 13);
    OA_CHECK(kit::knob_column(track, 4, 1) == 13);
    OA_CHECK(kit::knob_column(track, 0, 2) == 13);
    OA_CHECK(kit::knob_column(track, 1, 2) == 106);
    OA_CHECK(kit::knob_column(track, 0, 26) == 13);
    OA_CHECK(kit::knob_column(track, 25, 26) == 106);
    OA_CHECK(kit::knob_column(track, 1, 26) == 17);

    OA_CHECK(kit::stop_at(track, 13, 26) == 0);
    OA_CHECK(kit::stop_at(track, 106, 26) == 25);
    OA_CHECK(kit::stop_at(track, 17, 26) == 1);
    OA_CHECK(kit::stop_at(track, 0, 26) == 0);
    OA_CHECK(kit::stop_at(track, 1000, 26) == 25);
    OA_CHECK(kit::stop_at(track, 50, 1) == 0);
    OA_CHECK(kit::stop_at({10, 20, 7, 14}, 40, 5) == 0);

    const kit::Rect strip{100, 40, 71, 16};
    OA_CHECK(kit::level_at(strip, 3, 23, 100) == 0);
    OA_CHECK(kit::level_at(strip, 3, 23, 101) == 0);
    OA_CHECK(kit::level_at(strip, 3, 23, 124) == 1);
    OA_CHECK(kit::level_at(strip, 3, 23, 147) == 2);
    OA_CHECK(kit::level_at(strip, 3, 23, 200) == 2);
    OA_CHECK(kit::level_at(strip, 3, 23, 0) == 0);
    OA_CHECK(kit::level_at(strip, 0, 23, 124) == 0);

    OA_CHECK(kit::shown_choices(3) == 3);
    OA_CHECK(kit::shown_choices(12) == 8);
    OA_CHECK(kit::shown_choices(0) == 0);
    OA_CHECK(kit::shown_choices(8) == 8);

    const kit::Rect high = kit::choice_menu({200, 80, 200, 16}, 6);
    OA_CHECK(high.x == 200 && high.y == 96 && high.width == 200 && high.height == 98);
    const kit::Rect low = kit::choice_menu({200, 250, 200, 16}, 12);
    OA_CHECK(low.x == 200 && low.y == 120 && low.width == 200 && low.height == 130);
    const kit::Rect clamped = kit::choice_menu({200, 150, 200, 16}, 12);
    OA_CHECK(clamped.x == 200 && clamped.y == 28 && clamped.width == 200 && clamped.height == 130);

    const kit::Rect opened{200, 96, 200, 98};
    const kit::Rect first = kit::choice_item(opened, 0);
    OA_CHECK(first.x == 201 && first.y == 97 && first.width == 198 && first.height == 16);
    const kit::Rect third = kit::choice_item(opened, 2);
    OA_CHECK(third.x == 201 && third.y == 129 && third.width == 198 && third.height == 16);
}

} // namespace

int main() {
    buttons_match_the_five_looks();
    switch_levels_and_slider_match();
    choice_lock_and_scroll_match();
    oa_button_and_mark_match();
    clip_hides_what_it_misses();
    paint_matches_the_direct_draw(1);
    paint_matches_the_direct_draw(2);
    paint_matches_each_control(1);
    paint_matches_each_control(2);
    later_items_cover_earlier_ones();
    geometry_matches_the_start_commit();
    return oa::test::check_exit_status();
}
