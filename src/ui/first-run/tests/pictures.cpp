// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Writes the manual's four pictures of the first-run setup. Not a test.
// Usage: oa-ui-first-run-pictures DIRECTORY

#include "oa/ui/first_run/first_run.hpp"

#include "oa/formats/png.hpp"
#include "oa/platform/text_font.hpp"
#include "oa/present/game_text.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/kit/canvas.hpp"
#include "oa/ui/kit/components.hpp"
#include "oa/ui/paint/painter.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace paint = oa::ui::paint;
namespace png = oa::formats::png;
namespace present = oa::present;
namespace renderer = oa::ui::frontend_renderer;
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

/// The settings game text uses while the pictures measure. Outlines would
/// make a line wider than the smooth text that draws it.
present::TextSettings smooth_settings(void* /*context*/) {
    present::TextSettings settings;
    settings.style.modern_fonts = true;
    settings.style.outline = false;
    settings.style.shadow = false;
    settings.style.background = false;
    settings.style.size = present::game_font_text_size;
    return settings;
}

/// Measures a line as the smooth canvas draws it: 12-point bold for the
/// regular font, 10-point regular for the small one.
std::shared_ptr<const present::TextMask> smooth_mask(
    void* context, std::string_view text, present::TextFace face, int32_t scale, int32_t size
) {
    if (context == nullptr || text.empty())
        return nullptr;
    auto& stack = *static_cast<text_font::FontStack*>(context);
    const bool small = face == present::TextFace::status;
    int32_t pixel_size = (small ? 10 : 12) * std::max(scale, int32_t{1});
    if (size > 0 && size != 100)
        pixel_size = std::max(1, (pixel_size * size + 50) / 100);
    pixel_size = std::clamp(pixel_size, 1, text_font::max_pixel_size);
    text_font::Style style;
    style.pixel_size = pixel_size;
    style.weight =
        face == present::TextFace::message ? text_font::Weight::bold : text_font::Weight::regular;
    style.rendering = text_font::Rendering::antialiased;
    const auto placements = stack.layout(text, style);
    const auto characters = text_font::decode_utf8(text);
    if (!placements || !characters)
        return nullptr;
    auto mask = std::make_shared<present::TextMask>();
    std::size_t placed = 0;
    int32_t pen = 0;
    for (const char32_t character : *characters) {
        if (!text_font::is_invisible(character) && placed < placements->size()) {
            const auto& placement = (*placements)[placed++];
            pen = placement.pen + placement.advance;
        }
        mask->character_ends.push_back(pen);
    }
    mask->advance = pen;
    mask->width = std::max(pen, int32_t{0});
    mask->height = std::max(pixel_size, 1);
    mask->baseline = mask->height;
    return mask;
}

/// Installs the measure for one run of the pictures, and takes it down after.
struct MeasureHooks {
    explicit MeasureHooks(text_font::FontStack* stack) {
        present::GameTextHooks hooks;
        hooks.context = stack;
        hooks.settings = smooth_settings;
        hooks.draw = smooth_mask;
        present::set_game_text_hooks(hooks);
    }

    ~MeasureHooks() { present::set_game_text_hooks({}); }

    MeasureHooks(const MeasureHooks&) = delete;
    MeasureHooks& operator=(const MeasureHooks&) = delete;
};

/// An icon kept alive for the canvas that draws it.
struct Icon {
    std::vector<uint8_t> pixels{};
    renderer::RgbaPicture picture{};
};

/// Reads a PNG as 8-bit RGBA.
///
/// @param path the file
/// @param[out] icon the picture; unchanged when the file cannot be read
/// @return true when the picture has pixels
bool decode_png(const std::filesystem::path& path, Icon* icon) {
    std::ifstream input(path, std::ios::binary);
    const std::vector<uint8_t> file(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>()
    );
    if (!input || file.empty())
        return false;
    png::Messages messages;
    png::Info info;
    if (!png::read_info(file, messages, &info))
        return false;
    const png::Header& header = info.header;
    if (header.width == 0 || header.height == 0 || header.width > 4096 || header.height > 4096)
        return false;
    png::Transforms transforms;
    transforms.strip_16 = header.bit_depth == 16;
    transforms.unpack = header.bit_depth < 8;
    const std::size_t stride = png::row_bytes(header, transforms);
    if (stride == 0 || header.height > std::numeric_limits<std::size_t>::max() / stride)
        return false;
    std::vector<uint8_t> rows(static_cast<std::size_t>(header.height) * stride);
    const png::Progress progress = png::read_image(file, info, transforms, messages, rows);
    if (progress != png::Progress::rows && progress != png::Progress::end)
        return false;
    const auto channels = png::channel_count(header.color_type);
    if (channels == 0)
        return false;
    Icon decoded;
    decoded.pixels.resize(
        static_cast<std::size_t>(header.width) * static_cast<std::size_t>(header.height) * 4U
    );
    for (uint32_t y = 0; y < header.height; ++y) {
        for (uint32_t x = 0; x < header.width; ++x) {
            const uint8_t* sample = rows.data() + static_cast<std::size_t>(y) * stride +
                                    static_cast<std::size_t>(x) * channels;
            uint8_t* pixel =
                decoded.pixels.data() + (static_cast<std::size_t>(y) * header.width + x) * 4U;
            switch (header.color_type) {
            case png::ColorType::grey:
                pixel[0] = pixel[1] = pixel[2] = sample[0];
                pixel[3] = 255;
                break;
            case png::ColorType::grey_alpha:
                pixel[0] = pixel[1] = pixel[2] = sample[0];
                pixel[3] = sample[1];
                break;
            case png::ColorType::rgb:
                pixel[0] = sample[0];
                pixel[1] = sample[1];
                pixel[2] = sample[2];
                pixel[3] = 255;
                break;
            case png::ColorType::rgb_alpha:
                pixel[0] = sample[0];
                pixel[1] = sample[1];
                pixel[2] = sample[2];
                pixel[3] = sample[3];
                break;
            case png::ColorType::palette: {
                const png::Rgb colour =
                    sample[0] < info.palette_entries ? info.palette[sample[0]] : png::Rgb{};
                pixel[0] = colour.r;
                pixel[1] = colour.g;
                pixel[2] = colour.b;
                pixel[3] = 255;
                break;
            }
            }
        }
    }
    decoded.picture.width = header.width;
    decoded.picture.height = header.height;
    decoded.picture.pixels = decoded.pixels;
    *icon = std::move(decoded);
    icon->picture.pixels = icon->pixels;
    return true;
}

/// Paints a picture into the window, centred when it is smaller.
///
/// @param picture the canvas
/// @param path the file
/// @return true when the picture covered the canvas
bool paint_backing(Picture& picture, const std::filesystem::path& path) {
    Icon icon;
    if (!decode_png(path, &icon))
        return false;
    const int32_t src_w = static_cast<int32_t>(icon.picture.width);
    const int32_t src_h = static_cast<int32_t>(icon.picture.height);
    if (src_w <= 0 || src_h <= 0)
        return false;
    const int32_t left = std::max((kWidth - src_w) / 2, 0);
    const int32_t top = std::max((kHeight - src_h) / 2, 0);
    for (int32_t y = 0; y < src_h && top + y < kHeight; ++y) {
        for (int32_t x = 0; x < src_w && left + x < kWidth; ++x) {
            const std::size_t from =
                (static_cast<std::size_t>(y) * icon.picture.width + static_cast<std::size_t>(x)) *
                4U;
            const std::size_t to =
                (static_cast<std::size_t>(top + y) * static_cast<std::size_t>(kWidth) +
                 static_cast<std::size_t>(left + x)) *
                4U;
            picture.pixels.rgba[to] = icon.pixels[from];
            picture.pixels.rgba[to + 1] = icon.pixels[from + 1];
            picture.pixels.rgba[to + 2] = icon.pixels[from + 2];
            picture.pixels.rgba[to + 3] = 255;
        }
    }
    return true;
}

/// Walks up from a directory until a relative path is a file.
///
/// @param start where the walk begins
/// @param relative the file sought under each directory
/// @return the file; empty when none of the directories holds it
std::filesystem::path
climb_for(std::filesystem::path start, const std::filesystem::path& relative) {
    std::error_code ignored;
    for (int32_t step = 0; step < 12 && !start.empty(); ++step) {
        const std::filesystem::path candidate = start / relative;
        if (std::filesystem::is_regular_file(candidate, ignored))
            return candidate;
        const std::filesystem::path parent = start.parent_path();
        if (parent == start)
            break;
        start = parent;
    }
    return {};
}

/// A branding picture. The compiler records this file relative to the build
/// directory, so that path does not name the repository when the tool runs.
///
/// @param file_name the file under branding/
/// @return the file; empty when it cannot be found
std::filesystem::path branding_file(std::string_view file_name) {
    const std::filesystem::path relative = std::filesystem::path("branding") / file_name;
    std::error_code ignored;
    const std::filesystem::path from_here =
        climb_for(std::filesystem::current_path(ignored), relative);
    if (!from_here.empty())
        return from_here;
    return climb_for(text_font::bundled_font_directory(), relative);
}

/// Drops the clear margin around an icon, as the menu's buttons do.
///
/// @param icon the icon; unchanged when it has no clear margin
void crop_visible(Icon* icon) {
    if (icon == nullptr || icon->picture.width == 0 || icon->picture.height == 0)
        return;
    const uint32_t width = icon->picture.width;
    const uint32_t height = icon->picture.height;
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
    if (icon->pixels.size() / row_bytes < height)
        return;
    uint32_t left = width;
    uint32_t top = height;
    uint32_t right = 0;
    uint32_t bottom = 0;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            if (icon->pixels[(static_cast<std::size_t>(y) * width + x) * 4U + 3U] == 0)
                continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x + 1U);
            bottom = std::max(bottom, y + 1U);
        }
    }
    if (left >= right || top >= bottom ||
        (left == 0 && top == 0 && right == width && bottom == height))
        return;
    Icon cropped;
    cropped.picture.width = right - left;
    cropped.picture.height = bottom - top;
    const std::size_t cropped_row = static_cast<std::size_t>(cropped.picture.width) * 4U;
    cropped.pixels.resize(cropped_row * cropped.picture.height);
    for (uint32_t y = top; y < bottom; ++y) {
        const uint8_t* from =
            icon->pixels.data() + (static_cast<std::size_t>(y) * width + left) * 4U;
        uint8_t* into = cropped.pixels.data() + static_cast<std::size_t>(y - top) * cropped_row;
        std::copy(from, from + cropped_row, into);
    }
    cropped.picture.pixels = cropped.pixels;
    *icon = std::move(cropped);
    icon->picture.pixels = icon->pixels;
}

/// Where the main menu's OA buttons sit in a 1280×720 window.
///
/// The menu's picture is 640 by 480. Letterboxed by the taller fit it is
/// drawn at one and a half, 960 wide, with 160-point bars. The buttons sit
/// on the picture's bottom right, 48 points square and 12 in from the edges.
struct MenuButtons {
    oa::ui::kit::Rect library{};
    oa::ui::kit::Rect settings{};
};

MenuButtons menu_buttons() {
    constexpr int32_t kMenuWidth = 640;
    constexpr int32_t kMenuHeight = 480;
    constexpr int32_t kButton = 48;
    constexpr int32_t kInset = 12;
    constexpr int32_t kButtonGap = 12;
    constexpr int32_t kBar = (kWidth - kMenuWidth * 3 / 2) / 2;
    const auto presented = [](int32_t along) { return kBar + along * 3 / 2; };
    const int32_t side = kButton * 3 / 2;
    const int32_t settings_x = kMenuWidth - kInset - kButton;
    const int32_t settings_y = kMenuHeight - kInset - kButton;
    const int32_t library_x = settings_x - kButtonGap - kButton;
    MenuButtons buttons;
    buttons.library = {presented(library_x), settings_y * 3 / 2, side, side};
    buttons.settings = {presented(settings_x), settings_y * 3 / 2, side, side};
    return buttons;
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
    const MeasureHooks measuring(fonts.get());

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
        // Letterbox bars are black. A captured menu smaller than the window
        // is centred on them; without one, the fallback covers this fill.
        oa::ui::kit::fill_rect(picture.canvas, {0, 0, kWidth, kHeight}, {0, 0, 0});
        const bool backing = paint_backing(picture, directory / "menu-backing.png");
        const MenuButtons buttons = menu_buttons();
        if (!backing) {
            oa::ui::kit::fill_rect(
                picture.canvas, {0, 0, kWidth, kHeight}, oa::ui::kit::colour::panel
            );
            Icon library_icon;
            Icon settings_icon;
            const bool have_library =
                decode_png(branding_file("open-annihilation-mods-256.png"), &library_icon);
            const bool have_settings =
                decode_png(branding_file("open-annihilation-settings-256.png"), &settings_icon);
            if (!have_library || !have_settings)
                std::cerr << "pictures: an OA button icon could not be read\n";
            if (have_library) {
                crop_visible(&library_icon);
                picture.canvas.icon = library_icon.picture;
            }
            oa::ui::kit::OaButtonMark library_mark;
            library_mark.corner = oa::ui::kit::OaButtonCorner::count;
            library_mark.count = 2;
            oa::ui::kit::draw_oa_button(
                oa::ui::kit::placed_at(picture.canvas, {buttons.library.x, buttons.library.y}),
                buttons.library.width,
                oa::ui::kit::OaButtonState::idle,
                library_mark
            );
            picture.canvas.icon = {};
            if (have_settings) {
                crop_visible(&settings_icon);
                picture.canvas.icon = settings_icon.picture;
            }
            oa::ui::kit::draw_oa_button(
                oa::ui::kit::placed_at(picture.canvas, {buttons.settings.x, buttons.settings.y}),
                buttons.settings.width,
                oa::ui::kit::OaButtonState::idle,
                false
            );
            picture.canvas.icon = {};
        }
        oa::ui::first_run::Session session;
        session.model.step = oa::ui::first_run::Step::buttons;
        session.model.library_button = buttons.library;
        session.model.settings_button = buttons.settings;
        session.model.library_count = 2;
        session.pulse_ms = 400;
        draw_step(picture, session);
        if (!write_png(picture, directory / "buttons.png"))
            return 1;
    }
    return 0;
}
