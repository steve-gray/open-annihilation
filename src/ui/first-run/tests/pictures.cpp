// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Writes the manual's four pictures of the first-run setup. Not a test.
// Usage: oa-ui-first-run-pictures DIRECTORY

#include "oa/ui/first_run/first_run.hpp"

#include "oa/formats/png.hpp"
#include "oa/platform/text_font.hpp"
#include "oa/ui/kit/canvas.hpp"
#include "oa/ui/kit/components.hpp"
#include "oa/ui/paint/painter.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace paint = oa::ui::paint;
namespace png = oa::formats::png;
namespace text_font = oa::platform::text_font;

constexpr int32_t kWidth = 1280;
constexpr int32_t kHeight = 720;

/// A smooth canvas at 1x, drawn with the bundled fonts.
struct Picture {
    paint::Canvas pixels;
    paint::Painter painter;
    oa::ui::kit::Canvas canvas{};
    oa::ui::kit::Fonts fonts{};

    explicit Picture(text_font::FontStack* modern)
        : pixels(paint::make_canvas(kWidth, kHeight)), painter(pixels) {
        canvas.smooth.painter = &painter;
        canvas.smooth.fonts = modern;
        canvas.smooth.scale_percent = 100;
        canvas.fonts = &fonts;
    }
};

/// Writes a picture over a dark ground, so a clear pixel keeps that ground.
///
/// @param picture the canvas
/// @param path the file
/// @return true when it was written
bool write_png(const Picture& picture, const std::filesystem::path& path) {
    std::vector<uint8_t> rows;
    rows.resize(static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight) * 3);
    constexpr uint8_t kGroundR = 0x2a;
    constexpr uint8_t kGroundG = 0x30;
    constexpr uint8_t kGroundB = 0x24;
    for (int32_t y = 0; y < kHeight; ++y) {
        for (int32_t x = 0; x < kWidth; ++x) {
            const paint::Rgba pixel = paint::pixel_at(picture.pixels, x, y);
            const uint32_t cover = pixel.a;
            const uint32_t keep = 255U - cover;
            const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(kWidth) +
                                    static_cast<std::size_t>(x)) *
                                   3;
            rows[at] = static_cast<uint8_t>((pixel.r * cover + kGroundR * keep) / 255U);
            rows[at + 1] = static_cast<uint8_t>((pixel.g * cover + kGroundG * keep) / 255U);
            rows[at + 2] = static_cast<uint8_t>((pixel.b * cover + kGroundB * keep) / 255U);
        }
    }
    png::Header header;
    header.width = static_cast<uint32_t>(kWidth);
    header.height = static_cast<uint32_t>(kHeight);
    header.bit_depth = 8;
    header.color_type = png::ColorType::rgb;
    std::vector<uint8_t> file;
    if (!png::write(png::Image{header, {}, rows}, &file))
        return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size())
    );
    return static_cast<bool>(output);
}

oa::ui::kit::Frame large_frame() {
    oa::ui::kit::Frame frame;
    frame.area = {0, 0, kWidth, kHeight};
    frame.size_class = oa::ui::kit::SizeClass::large;
    frame.scale_percent = 100;
    return frame;
}

oa::ui::first_run::Card
language_card(std::string tag, std::string own, std::string english, bool current) {
    oa::ui::first_run::Card card;
    card.tag = std::move(tag);
    card.own_name = std::move(own);
    card.english_name = std::move(english);
    card.kind = oa::ui::first_run::CardKind::built_in;
    card.current = current;
    return card;
}

oa::ui::first_run::Session language_session(bool offline) {
    oa::ui::first_run::Session session;
    session.model.step = oa::ui::first_run::Step::language;
    session.model.cards.push_back(language_card("en", "English", "English", true));
    session.model.cards.push_back(language_card("de", "Deutsch", "German", false));
    session.model.cards.push_back(language_card("es", "Español", "Spanish", false));
    session.model.cards.push_back(language_card("fr", "Français", "French", false));
    session.model.cards.push_back(language_card("it", "Italiano", "Italian", false));
    oa::ui::first_run::Card pack;
    pack.tag = "eo";
    pack.own_name = "Esperanto";
    pack.english_name = "Esperanto";
    pack.kind = oa::ui::first_run::CardKind::pack;
    pack.size_text = "1.2 MB";
    if (offline) {
        pack.pack = oa::ui::first_run::PackState::offline;
        session.model.catalogue = oa::ui::first_run::CatalogueNote::unread;
    }
    session.model.cards.push_back(std::move(pack));
    session.carousel.selected = 0;
    oa::ui::first_run::bind(session);
    session.model.sample_own = "English";
    session.model.sample_english = "English";
    return session;
}

void draw_step(Picture& picture, const oa::ui::first_run::Session& session) {
    const auto scene = oa::ui::first_run::lay_out(session, large_frame(), &picture.fonts);
    oa::ui::first_run::draw(picture.canvas, scene, session);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: oa-ui-first-run-pictures DIRECTORY\n";
        return 2;
    }
    const std::filesystem::path directory(argv[1]);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        std::cerr << "could not make " << directory << '\n';
        return 1;
    }
    auto fonts = text_font::FontStack::open(text_font::bundled_font_directory());
    if (fonts == nullptr) {
        std::cerr << "the bundled fonts could not be opened\n";
        return 1;
    }

    {
        Picture picture(fonts.get());
        draw_step(picture, language_session(false));
        if (!write_png(picture, directory / "welcome.png"))
            return 1;
    }
    {
        Picture picture(fonts.get());
        draw_step(picture, language_session(true));
        if (!write_png(picture, directory / "welcome-offline.png"))
            return 1;
    }
    {
        Picture picture(fonts.get());
        auto session = language_session(false);
        session.model.step = oa::ui::first_run::Step::online;
        draw_step(picture, session);
        if (!write_png(picture, directory / "online.png"))
            return 1;
    }
    {
        Picture picture(fonts.get());
        oa::ui::kit::fill_rect(picture.canvas, {0, 0, kWidth, kHeight}, oa::ui::kit::colour::list);
        constexpr int32_t kSide = 64;
        const int32_t settings_x = kWidth - 36 - kSide;
        const int32_t library_x = settings_x - 16 - kSide;
        constexpr int32_t kTop = 28;
        oa::ui::kit::OaButtonMark library_mark;
        library_mark.corner = oa::ui::kit::OaButtonCorner::count;
        library_mark.count = 2;
        oa::ui::kit::draw_oa_button(
            oa::ui::kit::placed_at(picture.canvas, {library_x, kTop}),
            kSide,
            oa::ui::kit::OaButtonState::idle,
            library_mark
        );
        oa::ui::kit::draw_oa_button(
            oa::ui::kit::placed_at(picture.canvas, {settings_x, kTop}),
            kSide,
            oa::ui::kit::OaButtonState::idle,
            false
        );
        oa::ui::first_run::Session session;
        session.model.step = oa::ui::first_run::Step::buttons;
        session.model.library_button = {library_x, kTop, kSide, kSide};
        session.model.settings_button = {settings_x, kTop, kSide, kSide};
        session.model.library_count = 2;
        session.pulse_ms = 400;
        draw_step(picture, session);
        if (!write_png(picture, directory / "buttons.png"))
            return 1;
    }
    return 0;
}
