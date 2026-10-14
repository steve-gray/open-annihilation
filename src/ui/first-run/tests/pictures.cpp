// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Writes the manual's pictures of the first-run setup. Not a test. Each is
// a window of a size, laid out as the setup lays itself out there
// (first_run::setup_frame) and drawn at that scale.
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
#include <cstddef>
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

/// A smooth canvas drawn with the bundled fonts, at a scale of the point size.
struct Picture {
    int32_t width{};  ///< canvas pixels
    int32_t height{}; ///< canvas pixels
    int32_t points_width{};
    int32_t points_height{};
    paint::Canvas pixels;
    paint::Painter painter;
    oa::ui::kit::Canvas canvas{};
    oa::ui::kit::Fonts fonts{};

    Picture(text_font::FontStack* modern, int32_t point_width, int32_t point_height, int32_t scale)
        : width(std::max(point_width, int32_t{1}) * std::max(scale, int32_t{100}) / 100),
          height(std::max(point_height, int32_t{1}) * std::max(scale, int32_t{100}) / 100),
          points_width(point_width), points_height(point_height),
          pixels(paint::make_canvas(width, height)), painter(pixels) {
        canvas.smooth.painter = &painter;
        canvas.smooth.fonts = modern;
        canvas.smooth.scale_percent = std::max(scale, int32_t{100});
        canvas.fonts = &fonts;
    }

    /// A window's whole canvas, with the setup's frame drawn from its top left.
    ///
    /// @param modern the bundled fonts
    /// @param frame the setup's frame on the window
    /// @param pixel_width the window's width, in pixels
    /// @param pixel_height the window's height, in pixels
    Picture(
        text_font::FontStack* modern,
        const oa::ui::kit::Frame& frame,
        int32_t pixel_width,
        int32_t pixel_height
    )
        : width(std::max(pixel_width, int32_t{1})), height(std::max(pixel_height, int32_t{1})),
          points_width(frame.area.width), points_height(frame.area.height),
          pixels(paint::make_canvas(width, height)), painter(pixels) {
        canvas.smooth.painter = &painter;
        canvas.smooth.fonts = modern;
        canvas.smooth.scale_percent = std::max(frame.scale_percent, int32_t{100});
        canvas.fonts = &fonts;
        // The pixels the frame leaves at the right and bottom are the window's face.
        painter.fill_rect({0, 0, width, height}, paint::Rgba{0x1b, 0x1e, 0x19, 255});
    }
};

/// Writes a picture over a dark ground, so a clear pixel keeps that ground.
///
/// @param picture the canvas
/// @param path the file
/// @return true when it was written
bool write_png(const Picture& picture, const std::filesystem::path& path) {
    std::vector<uint8_t> rows;
    rows.resize(
        static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height) * 3
    );
    constexpr uint8_t kGroundR = 0x2a;
    constexpr uint8_t kGroundG = 0x30;
    constexpr uint8_t kGroundB = 0x24;
    for (int32_t y = 0; y < picture.height; ++y) {
        for (int32_t x = 0; x < picture.width; ++x) {
            const paint::Rgba pixel = paint::pixel_at(picture.pixels, x, y);
            const uint32_t cover = pixel.a;
            const uint32_t keep = 255U - cover;
            const std::size_t at =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(picture.width) +
                 static_cast<std::size_t>(x)) *
                3;
            rows[at] = static_cast<uint8_t>((pixel.r * cover + kGroundR * keep) / 255U);
            rows[at + 1] = static_cast<uint8_t>((pixel.g * cover + kGroundG * keep) / 255U);
            rows[at + 2] = static_cast<uint8_t>((pixel.b * cover + kGroundB * keep) / 255U);
        }
    }
    png::Header header;
    header.width = static_cast<uint32_t>(picture.width);
    header.height = static_cast<uint32_t>(picture.height);
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
    const int32_t left = std::max((picture.width - src_w) / 2, 0);
    const int32_t top = std::max((picture.height - src_h) / 2, 0);
    for (int32_t y = 0; y < src_h && top + y < picture.height; ++y) {
        for (int32_t x = 0; x < src_w && left + x < picture.width; ++x) {
            const std::size_t from =
                (static_cast<std::size_t>(y) * icon.picture.width + static_cast<std::size_t>(x)) *
                4U;
            const std::size_t to =
                (static_cast<std::size_t>(top + y) * static_cast<std::size_t>(picture.width) +
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

/// Two OA buttons inside a frame, so the buttons step stays on the canvas.
///
/// @param width the frame's width, in points
/// @param height the frame's height, in points
/// @return the buttons
MenuButtons menu_buttons_for(int32_t width, int32_t height) {
    constexpr int32_t kButton = 48;
    constexpr int32_t kInset = 12;
    constexpr int32_t kGap = 12;
    MenuButtons buttons;
    const int32_t side = std::min(kButton, std::max(height / 6, int32_t{16}));
    buttons.settings = {
        std::max(width - kInset - side, int32_t{0}),
        std::max(height - kInset - side, int32_t{0}),
        side,
        side
    };
    buttons.library = {
        std::max(buttons.settings.x - kGap - side, int32_t{0}), buttons.settings.y, side, side
    };
    return buttons;
}

/// The frame the setup lays out in on a window, as the pre-game window lays
/// it out: on the smooth form, never below the density.
///
/// @param width the window's width, in its points
/// @param height the window's height, in its points
/// @param density the window's pixels a point
/// @return the frame
oa::ui::kit::Frame window_frame(int32_t width, int32_t height, int32_t density) {
    return oa::ui::first_run::setup_frame(width * density, height * density, density * 100, true);
}

/// A rectangle of window pixels in a frame's points.
///
/// @param pixels the rectangle, in window pixels
/// @param frame the frame
/// @return the rectangle, in the frame's points
oa::ui::kit::Rect in_points(const oa::ui::kit::Rect& pixels, const oa::ui::kit::Frame& frame) {
    const int32_t percent = std::max(frame.scale_percent, int32_t{1});
    return {
        pixels.x * 100 / percent,
        pixels.y * 100 / percent,
        pixels.width * 100 / percent,
        pixels.height * 100 / percent,
    };
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

oa::ui::first_run::Session game_session() {
    oa::ui::first_run::Session session = language_session(false);
    session.model.walk = oa::ui::first_run::Walk::setup;
    session.model.leave = oa::ui::first_run::Leave::quit;
    session.model.step = oa::ui::first_run::Step::game;
    oa::ui::first_run::GameSource fuller;
    fuller.token = "full";
    fuller.title = "setup_total_annihilation.exe";
    fuller.detail = "~/Downloads";
    fuller.mark = oa::ui::first_run::SourceMark::gog;
    fuller.footnote = "1.0 GB";
    fuller.chips.push_back({"Total Annihilation 3.1c", true});
    fuller.chips.push_back({"Campaign", true});
    fuller.chips.push_back({"Core Contingency", true});
    fuller.chips.push_back({"Battle Tactics", true});
    fuller.chips.push_back({"Music", true});
    oa::ui::first_run::GameSource thinner;
    thinner.token = "thin";
    thinner.title = "setup_total_annihilation_2.exe";
    thinner.detail = "~/Downloads";
    thinner.mark = oa::ui::first_run::SourceMark::gog;
    thinner.footnote = "1.0 GB";
    thinner.chips.push_back({"Total Annihilation 3.1c", true});
    thinner.chips.push_back({"Music", true});
    session.model.sources.push_back(std::move(fuller));
    session.model.sources.push_back(std::move(thinner));
    session.model.source = 0;
    return session;
}

oa::ui::first_run::Session copying_session(bool checked) {
    oa::ui::first_run::Session session = game_session();
    session.model.step = oa::ui::first_run::Step::copying;
    session.model.copy.from = "Total Annihilation";
    session.model.copy.done = checked ? 1000 : 300;
    session.model.copy.whole = 1000;
    session.model.copy.running = !checked;
    session.model.copy.checked = checked;
    session.model.copy.overall = checked ? "38 of 38 · 100%" : "12 of 38 · 31%";
    if (!checked)
        session.model.copy.remaining = "about 40 seconds left";
    oa::ui::first_run::CopyBundle bundle;
    bundle.name = "Total Annihilation";
    bundle.file = checked ? std::string{} : std::string("totala1.hpi");
    bundle.amount = checked ? "All files match" : "31%";
    bundle.done = session.model.copy.done;
    bundle.whole = 1000;
    bundle.phase =
        checked ? oa::ui::first_run::CopyPhase::done : oa::ui::first_run::CopyPhase::copying;
    session.model.copy.bundles.push_back(std::move(bundle));
    // Music is already done, beside the bundle that is still copying, so the
    // two rows can be compared. Once the check is done both rows show a result.
    oa::ui::first_run::CopyBundle music;
    music.name = "Music";
    music.amount = "17 of 18 tracks";
    music.done = 1000;
    music.whole = 1000;
    music.phase = oa::ui::first_run::CopyPhase::done;
    music.note = true;
    session.model.copy.bundles.push_back(std::move(music));
    if (checked) {
        session.model.copy.verdict = "All files match";
        session.copy_continue_ms = 3000;
    }
    return session;
}

void draw_step(
    Picture& picture,
    const oa::ui::first_run::Session& session,
    const oa::ui::kit::Frame& frame,
    const renderer::RgbaPicture& icon
) {
    picture.canvas.icon = icon;
    picture.canvas.smooth.scale_percent = std::max(frame.scale_percent, int32_t{100});
    const auto scene = oa::ui::first_run::lay_out(session, frame, &picture.fonts);
    oa::ui::first_run::draw(picture.canvas, scene, session);
}

/// The visible part of an icon, so the header mark fills its square.
///
/// @param icon the decoded file
/// @return the cropped picture; the whole icon when nothing is transparent
Icon visible_icon(const Icon& icon) {
    const uint32_t width = icon.picture.width;
    const uint32_t height = icon.picture.height;
    if (width == 0 || height == 0 ||
        icon.pixels.size() < static_cast<std::size_t>(width) * height * 4U)
        return icon;
    uint32_t left = width;
    uint32_t top = height;
    uint32_t right = 0;
    uint32_t bottom = 0;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t alpha = icon.pixels[(static_cast<std::size_t>(y) * width + x) * 4U + 3U];
            if (alpha == 0)
                continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x + 1);
            bottom = std::max(bottom, y + 1);
        }
    }
    if (left >= right || top >= bottom)
        return icon;
    Icon part;
    part.pixels.resize(static_cast<std::size_t>(right - left) * (bottom - top) * 4U);
    const uint32_t part_width = right - left;
    for (uint32_t y = top; y < bottom; ++y) {
        const std::size_t from = (static_cast<std::size_t>(y) * width + left) * 4U;
        const std::size_t to = static_cast<std::size_t>(y - top) * part_width * 4U;
        std::copy_n(
            icon.pixels.begin() + static_cast<std::ptrdiff_t>(from),
            part_width * 4U,
            part.pixels.begin() + static_cast<std::ptrdiff_t>(to)
        );
    }
    part.picture.width = part_width;
    part.picture.height = bottom - top;
    part.picture.pixels = part.pixels;
    return part;
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
    Icon loaded_icon;
    const bool have_icon = decode_png(branding_file("open-annihilation-256.png"), &loaded_icon);
    if (!have_icon)
        std::cerr << "pictures: the app icon could not be read\n";
    Icon app_icon = have_icon ? visible_icon(loaded_icon) : Icon{};
    app_icon.picture.pixels = app_icon.pixels;
    const renderer::RgbaPicture icon = app_icon.picture;
    // The manual's pictures are a 1280 by 720 window, whose title bar names
    // the game, so the header has no mark and no name.
    const oa::ui::kit::Frame manual = window_frame(kWidth, kHeight, 1);
    const auto in_window = [](oa::ui::first_run::Session session) {
        session.model.windowed = true;
        return session;
    };

    {
        // The pre-game window has no game fonts yet. These three are that
        // window's language step, with a fixture pack on the carousel: two
        // windows, and full screen at 1920 by 1080, with the mark and the name.
        const struct Size {
            const char* name;
            int32_t width;
            int32_t height;
            bool windowed;
        } sizes[] = {
            {"language-640x480.png", 640, 480, true},
            {"language-1280x720.png", 1280, 720, true},
            {"language-1920x1080.png", 1920, 1080, false},
        };

        for (const Size& size : sizes) {
            const oa::ui::kit::Frame frame = window_frame(size.width, size.height, 1);
            Picture picture(fonts.get(), frame, size.width, size.height);
            picture.canvas.fonts = nullptr;
            oa::ui::first_run::Session session = language_session(false);
            session.model.walk = oa::ui::first_run::Walk::setup;
            session.model.leave = oa::ui::first_run::Leave::quit;
            session.model.windowed = size.windowed;
            draw_step(picture, session, frame, icon);
            if (!write_png(picture, directory / size.name))
                return 1;
        }
    }
    const auto manual_picture = [&] { return Picture(fonts.get(), manual, kWidth, kHeight); };
    {
        Picture picture = manual_picture();
        draw_step(picture, in_window(language_session(false)), manual, icon);
        if (!write_png(picture, directory / "welcome.png"))
            return 1;
    }
    {
        Picture picture = manual_picture();
        draw_step(picture, in_window(language_session(true)), manual, icon);
        if (!write_png(picture, directory / "welcome-offline.png"))
            return 1;
    }
    {
        Picture picture = manual_picture();
        auto session = in_window(language_session(false));
        session.model.step = oa::ui::first_run::Step::online;
        draw_step(picture, session, manual, icon);
        if (!write_png(picture, directory / "online.png"))
            return 1;
    }
    {
        Picture picture = manual_picture();
        // Letterbox bars are black. A captured menu smaller than the window
        // is centred on them; without one, the fallback covers this fill.
        oa::ui::kit::fill_rect(
            picture.canvas, {0, 0, manual.area.width, manual.area.height}, {0, 0, 0}
        );
        const bool backing = paint_backing(picture, directory / "menu-backing.png");
        // The buttons in the window's pixels, and in the setup's points.
        const MenuButtons buttons = menu_buttons();
        const oa::ui::kit::Rect library = in_points(buttons.library, manual);
        const oa::ui::kit::Rect settings = in_points(buttons.settings, manual);
        if (!backing) {
            oa::ui::kit::fill_rect(
                picture.canvas,
                {0, 0, manual.area.width, manual.area.height},
                oa::ui::kit::colour::panel
            );
            Icon library_icon;
            Icon settings_icon;
            const bool have_library =
                decode_png(branding_file("open-annihilation-mods-256.png"), &library_icon);
            const bool have_settings =
                decode_png(branding_file("open-annihilation-settings-256.png"), &settings_icon);
            if (!have_library || !have_settings)
                std::cerr << "pictures: an OA button icon could not be read\n";
            if (have_library)
                picture.canvas.icon = library_icon.picture;
            oa::ui::kit::OaButtonMark library_mark;
            library_mark.corner = oa::ui::kit::OaButtonCorner::count;
            library_mark.count = 2;
            oa::ui::kit::draw_menu_icon_button(
                oa::ui::kit::placed_at(picture.canvas, {library.x, library.y}),
                library.width,
                oa::ui::kit::OaButtonState::idle,
                false,
                library_mark
            );
            picture.canvas.icon = {};
            if (have_settings)
                picture.canvas.icon = settings_icon.picture;
            oa::ui::kit::draw_menu_icon_button(
                oa::ui::kit::placed_at(picture.canvas, {settings.x, settings.y}),
                settings.width,
                oa::ui::kit::OaButtonState::idle,
                false,
                {}
            );
            picture.canvas.icon = {};
        }
        oa::ui::first_run::Session session;
        session.model.step = oa::ui::first_run::Step::buttons;
        session.model.library_button = library;
        session.model.settings_button = settings;
        session.model.library_count = 2;
        session.pulse_ms = 400;
        draw_step(picture, session, manual, icon);
        if (!write_png(picture, directory / "buttons.png"))
            return 1;
    }
    {
        Picture picture = manual_picture();
        draw_step(picture, in_window(game_session()), manual, icon);
        if (!write_png(picture, directory / "game.png"))
            return 1;
    }
    {
        Picture picture = manual_picture();
        draw_step(picture, in_window(copying_session(false)), manual, icon);
        if (!write_png(picture, directory / "copying.png"))
            return 1;
    }
    {
        Picture picture = manual_picture();
        draw_step(picture, in_window(copying_session(true)), manual, icon);
        if (!write_png(picture, directory / "check.png"))
            return 1;
    }

    // Every step in windows of the sizes a window is checked at, and at twice
    // the density. Quit is a first start. Skip is a re-run.
    const struct Size {
        int32_t width;
        int32_t height;
        int32_t density;
        const char* folder;
    } sizes[] = {
        {640, 480, 1, "640x480"},
        {1280, 720, 1, "1280x720"},
        {1344, 838, 1, "1344x838"},
        {1920, 1080, 1, "1920x1080"},
        {640, 480, 2, "640x480-2x"},
        {1280, 720, 2, "1280x720-2x"},
        {1344, 838, 2, "1344x838-2x"},
    };

    for (const Size& size : sizes) {
        const std::filesystem::path folder = directory / "frames" / size.folder;
        std::filesystem::create_directories(folder, error);
        if (error)
            return 1;
        const oa::ui::kit::Frame frame = window_frame(size.width, size.height, size.density);
        const auto save = [&](oa::ui::first_run::Session session, const char* name) {
            session.model.windowed = true;
            Picture picture(
                fonts.get(), frame, size.width * size.density, size.height * size.density
            );
            draw_step(picture, session, frame, icon);
            return write_png(picture, folder / name);
        };
        oa::ui::first_run::Session skip_language = language_session(false);
        skip_language.model.leave = oa::ui::first_run::Leave::skip;
        oa::ui::first_run::Session quit_language = language_session(false);
        quit_language.model.walk = oa::ui::first_run::Walk::setup;
        quit_language.model.leave = oa::ui::first_run::Leave::quit;
        oa::ui::first_run::Session quit_game = game_session();
        oa::ui::first_run::Session skip_game = game_session();
        skip_game.model.leave = oa::ui::first_run::Leave::skip;
        oa::ui::first_run::Session quit_copy = copying_session(false);
        oa::ui::first_run::Session skip_copy = copying_session(false);
        skip_copy.model.leave = oa::ui::first_run::Leave::skip;
        oa::ui::first_run::Session quit_checked = copying_session(true);
        oa::ui::first_run::Session skip_online = language_session(false);
        skip_online.model.step = oa::ui::first_run::Step::online;
        skip_online.model.leave = oa::ui::first_run::Leave::skip;
        oa::ui::first_run::Session quit_online = skip_online;
        quit_online.model.walk = oa::ui::first_run::Walk::setup;
        quit_online.model.leave = oa::ui::first_run::Leave::quit;
        const MenuButtons placed = menu_buttons_for(frame.area.width, frame.area.height);
        oa::ui::first_run::Session buttons;
        buttons.model.step = oa::ui::first_run::Step::buttons;
        buttons.model.library_button = placed.library;
        buttons.model.settings_button = placed.settings;
        buttons.model.library_count = 2;
        if (!save(skip_language, "skip-language.png") ||
            !save(quit_language, "quit-language.png") || !save(quit_game, "quit-game.png") ||
            !save(skip_game, "skip-game.png") || !save(quit_copy, "quit-copying.png") ||
            !save(skip_copy, "skip-copying.png") || !save(quit_checked, "quit-checked.png") ||
            !save(skip_online, "skip-online.png") || !save(quit_online, "quit-online.png") ||
            !save(buttons, "buttons.png"))
            return 1;
    }
    return 0;
}
