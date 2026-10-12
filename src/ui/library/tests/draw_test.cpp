// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's screens drawn in the installed game's fonts, over the
// fixture with Ridge selected: Compact in the 640x480 window at 1x, Regular
// in 1920x1080 and Large in 2560x1440 at 2x, and Compact's details page.
// Each is laid out and drawn twice into a picture with a margin round the
// window: the two drawings are byte for byte the same, the margin keeps its
// colour, and every pixel of the window is drawn. In the game's fonts every
// text fits and every part lies in the window. The same holds for the
// Active tab at Compact, Regular and Large, the Updates tab at Compact and
// Large, the Registries tab at Compact, Regular and Large, and Languages at
// Compact. The Registries pictures dress the fixture's built-in registry as
// Core Prime on that copy only. The Languages picture gives the fixture's
// Portugues (Brasil) pack the author "OA team" and an installed copy, on
// that drawing only, so the row shows the OA mark under its check. The
// characters the game's fonts lack are drawn in the modern fonts beside the
// test, as the game draws them. The Active pictures and the Maps pictures
// dress the fixture's maps with made-up minimaps on that drawing only, so
// the tiles, the row and the details show those pictures and not a file
// from the game. Given --frames DIR, it writes
// library-<class>.png, library-details-compact.png, library-active-<class>.png,
// library-updates-<class>.png, library-registries-<class>.png,
// library-languages-compact.png, library-maps-one-large.png and
// library-maps-several-large.png there.
//
//   oa-ui-library-draw-test --data [--frames DIR]

#include "oa/formats/png.hpp"
#include "oa/platform/text_font.hpp"
#include "oa/present/game_text.hpp"
#include "oa/test/check.hpp"
#include "oa/test/game_assets.hpp"
#include "oa/test/game_data.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/library/library.hpp"
#include "oa/ui/library/screen.hpp"
#include "screen_fixture.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace kit = oa::ui::kit;
namespace lib = oa::ui::library;
namespace renderer = oa::ui::frontend_renderer;
namespace fixture = screen_fixture;
namespace png = oa::formats::png;
namespace text_font = oa::platform::text_font;

/// The points of margin drawn round the window, which nothing may touch.
constexpr int32_t margin_points = 4;
/// The colour the picture starts as; the window's face covers it wherever it draws.
constexpr kit::Colour untouched{0x12, 0x34, 0x56};

/// Draws a line in the modern fonts, as the game does for the characters
/// its fonts lack: each face at its size beside the game font it stands in
/// for, letters without smoothing.
///
/// @param context the font stack
/// @param text the line, in UTF-8
/// @param face the game font it stands in for
/// @param scale screen pixels to a game pixel
/// @param size the text size, in percent
/// @return the line's mask; null when the fonts cannot draw it
std::shared_ptr<const oa::present::TextMask> draw_modern(
    void* context, std::string_view text, oa::present::TextFace face, int32_t scale, int32_t size
) {
    auto& stack = *static_cast<text_font::FontStack*>(context);
    if (text.empty() || text.size() > text_font::max_text_bytes)
        return nullptr;
    const bool label = face == oa::present::TextFace::label;
    const int32_t face_pixels =
        face == oa::present::TextFace::message  ? text_font::message_log_pixel_size
        : face == oa::present::TextFace::status ? text_font::status_readout_pixel_size
                                                : text_font::label_pixel_size;
    text_font::Style style;
    style.pixel_size = std::clamp(
        oa::present::face_pixel_size(face_pixels, scale, size),
        int32_t{1},
        text_font::max_pixel_size
    );
    style.weight = label ? text_font::Weight::regular : text_font::Weight::bold;
    style.rendering = text_font::Rendering::mono;
    const auto drawn = stack.draw(text, style);
    const auto placements = stack.layout(text, style);
    const auto characters = text_font::decode_utf8(text);
    if (!drawn || !placements || !characters)
        return nullptr;
    auto mask = std::make_shared<oa::present::TextMask>();
    mask->width = drawn->width;
    mask->height = drawn->height;
    mask->baseline = drawn->baseline;
    mask->origin = drawn->origin;
    mask->advance = drawn->advance;
    mask->alpha = drawn->alpha;
    std::size_t placed = 0;
    int32_t pen = 0;
    for (const char32_t character : *characters) {
        if (!text_font::is_invisible(character) && placed < placements->size()) {
            const auto& placement = (*placements)[placed++];
            pen = placement.pen + placement.advance;
        }
        mask->character_ends.push_back(pen);
    }
    return mask;
}

/// One picture the test draws.
struct Scene {
    std::string name{};           ///< the file's name after "library-"
    kit::Viewport window{};       ///< the window it is laid out for
    bool details_page{};          ///< Compact's details page instead of the list
    lib::Tab tab{lib::Tab::mods}; ///< the tab shown
    /// Dress the maps with made-up minimaps. The fixture's own inputs stay.
    bool previews{};
    std::string select_registry{}; ///< selected after the tab, when select_key is set
    std::string select_key{};
};

/// A drawing of a scene: the picture with its margin, and its scale.
struct Drawing {
    renderer::Surface surface{}; ///< the picture, the margin round the window
    int32_t scale{1};            ///< pixels a point
    kit::Point window{};         ///< the window's size, in points
};

/// Dresses the Languages picture: the fixture's built-in language pack names
/// its author, and an installed copy of it is added, on this drawing only.
/// The row then shows the OA mark under its check. The fixture's own inputs
/// stay as they were.
///
/// @param[in,out] library the scene's library
void dress_languages(lib::Library& library) {
    for (lib::Listing& listing : library.inputs.listings) {
        if (listing.registry == fixture::core && listing.key == "pt-BR")
            listing.author = "OA team";
    }
    lib::Installed pack;
    pack.kind = lib::Kind::language;
    pack.key = "pt-BR";
    pack.folder = "pt-BR";
    pack.name = "Portugues (Brasil)";
    pack.version = "1.0";
    pack.revision = 1;
    pack.author = "OA team";
    pack.registry = std::string(fixture::core);
    pack.release = 1;
    library.inputs.installed.push_back(std::move(pack));
    lib::refresh(library, std::move(library.inputs));
}

/// Dresses the Registries pictures: the built-in card carries Core Prime's
/// short name, title, description and author, and the added card a sample of
/// its own. The fixture itself stays Fixture Core. Other tabs are not dressed.
///
/// @param[in,out] library the scene's library
void dress_registries(lib::Library& library) {
    for (lib::Registry& registry : library.inputs.registries) {
        if (registry.id == fixture::core) {
            registry.name = "Core Prime";
            registry.title = "Core Prime - Official Open Annihilation Packages";
            registry.description =
                "The CorePrime.net registry provides packages and content curated by the Open "
                "Annihilation team, and checked for compatibility, as well as official releases "
                "from some leading mods.";
            registry.address = "http://downloads.coreprime.net/v1/catalogue.json";
            registry.author = "Open Annihilation";
            registry.packages = 1;
            registry.checked_ago = 300;
            lib::SigningKey signing_key;
            signing_key.fingerprint = "21fe · 31df · a154 · a261";
            signing_key.public_key = "ed25519:11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo=";
            registry.keys = {std::move(signing_key)};
        } else if (registry.id == fixture::added) {
            registry.title = "Example Mods - Sample packages";
            registry.description = "A sample registry of mods, including the made-up mod Ridge.";
            registry.address = "https://mods.example.org/oa/registry.yaml";
            registry.packages = 31;
            registry.checked_ago = 2 * 60 * 60;
        }
    }
}

/// Returns a solid minimap wider than it is tall. The pixels are made here:
/// nothing is read from the game's files.
///
/// @param red red
/// @param green green
/// @param blue blue
/// @return an 8 by 2 picture
lib::MapPicture solid_minimap(uint8_t red, uint8_t green, uint8_t blue) {
    lib::MapPicture picture;
    picture.width = 8;
    picture.height = 2;
    picture.rgba.assign(8U * 2U * 4U, 0);
    for (std::size_t index = 0; index + 3 < picture.rgba.size(); index += 4) {
        picture.rgba[index] = red;
        picture.rgba[index + 1] = green;
        picture.rgba[index + 2] = blue;
        picture.rgba[index + 3] = 255;
    }
    return picture;
}

/// Dresses the map pictures. Installed maps gain a minimap and a long
/// description, so the Active tiles show both and a From link. Shore Pack is
/// one map, not installed, so its row and its details show that minimap. Desert
/// Pack has several maps and no badge, so the Maps row shows the first and the
/// details show the grid. The fixture's own inputs stay as they were.
///
/// @param[in,out] library the scene's library
void dress_map_previews(lib::Library& library) {
    const lib::MapPicture red = solid_minimap(180, 40, 30);
    const lib::MapPicture blue = solid_minimap(30, 70, 160);
    const lib::MapPicture green = solid_minimap(40, 140, 50);
    const std::string note =
        "Black sand, white water and a reef that runs further than one line of this tile can "
        "hold, then the same shore again so the line is cut.";
    for (lib::Listing& listing : library.inputs.listings) {
        if (listing.key == "desert-pack" && listing.maps.size() == 2) {
            listing.maps[0].preview = blue;
            listing.maps[0].description = "Dry and flat.";
            listing.maps[1].preview = green;
            listing.maps[1].description = "A table of stone.";
        }
    }
    lib::Listing shore = fixture::listing(
        fixture::core,
        lib::Kind::map_pack,
        "shore",
        "Shore Pack",
        "1.0",
        fixture::mib,
        "One cove",
        {}
    );
    lib::MapLine cove;
    cove.name = "Cove";
    cove.players = 3;
    cove.size = "10x10";
    cove.description = note;
    cove.preview = red;
    shore.maps.push_back(std::move(cove));
    library.inputs.listings.push_back(std::move(shore));
    if (library.inputs.map_packs.size() >= 2) {
        library.inputs.map_packs[0].maps[0].preview = green;
        library.inputs.map_packs[0].maps[0].description = note;
        library.inputs.map_packs[0].maps[1].preview = blue;
        library.inputs.map_packs[0].maps[1].description = note;
        library.inputs.map_packs[1].maps[0].preview = red;
        library.inputs.map_packs[1].maps[0].description = note;
    }
    lib::refresh(library, std::move(library.inputs));
}

/// Lays a scene out and draws it.
///
/// @param scene the scene
/// @param fonts the game's fonts
/// @return the drawing
Drawing draw_scene(const Scene& scene, const kit::Fonts& fonts) {
    lib::Library library = fixture::fixture_library();
    if (scene.tab == lib::Tab::registries)
        dress_registries(library);
    else if (scene.tab == lib::Tab::languages)
        dress_languages(library);
    if (scene.previews)
        dress_map_previews(library);
    lib::select(library, fixture::ridge_id());
    lib::set_tab(library, scene.tab);
    if (!scene.select_key.empty())
        lib::select(library, {scene.select_registry, lib::Kind::map_pack, scene.select_key});
    lib::ScreenState state;
    if (scene.details_page) {
        lib::open_details(library);
        state.details_page = true;
    }
    const kit::Frame frame = kit::frame_of(scene.window);
    const kit::DisplayList list = lib::library_layout(library, state, frame, fonts);
    Drawing drawing;
    drawing.scale = frame.scale_percent / 100;
    drawing.window = lib::window_size(frame);
    const int32_t width = (drawing.window.x + 2 * margin_points) * drawing.scale;
    const int32_t height = (drawing.window.y + 2 * margin_points) * drawing.scale;
    drawing.surface.width = static_cast<uint32_t>(width);
    drawing.surface.height = static_cast<uint32_t>(height);
    drawing.surface.rgb.resize(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3
    );
    for (std::size_t at = 0; at < drawing.surface.rgb.size(); at += 3) {
        drawing.surface.rgb[at] = untouched.r;
        drawing.surface.rgb[at + 1] = untouched.g;
        drawing.surface.rgb[at + 2] = untouched.b;
    }
    kit::Canvas canvas;
    canvas.surface = &drawing.surface;
    canvas.placement = {
        margin_points * drawing.scale, margin_points * drawing.scale, drawing.scale, {}
    };
    canvas.fonts = &fonts;
    lib::draw_library(canvas, list);

    for (const std::string& problem : fixture::outside_window(list, drawing.window)) {
        std::fprintf(
            stderr, "%s: %s lies outside the window\n", scene.name.c_str(), problem.c_str()
        );
        ++oa::test::failed_checks();
    }
    for (const std::string& problem : fixture::texts_not_fitting(list, fonts)) {
        std::fprintf(stderr, "%s: %s does not fit\n", scene.name.c_str(), problem.c_str());
        ++oa::test::failed_checks();
    }
    return drawing;
}

/// Tells whether a pixel of a drawing keeps the colour it started as.
///
/// @param drawing the drawing
/// @param x the column, in pixels
/// @param y the row, in pixels
/// @return true when nothing drew there
bool kept(const Drawing& drawing, int32_t x, int32_t y) {
    const std::size_t at =
        (static_cast<std::size_t>(y) * drawing.surface.width + static_cast<std::size_t>(x)) * 3;
    return drawing.surface.rgb[at] == untouched.r && drawing.surface.rgb[at + 1] == untouched.g &&
           drawing.surface.rgb[at + 2] == untouched.b;
}

/// Checks that the margin keeps its colour and that every pixel of the window was drawn.
///
/// @param scene the scene
/// @param drawing its drawing
void check_edges(const Scene& scene, const Drawing& drawing) {
    const int32_t left = margin_points * drawing.scale;
    const int32_t right = left + drawing.window.x * drawing.scale;
    const int32_t top = margin_points * drawing.scale;
    const int32_t bottom = top + drawing.window.y * drawing.scale;
    std::size_t outside = 0;
    std::size_t blank = 0;
    for (int32_t y = 0; y < static_cast<int32_t>(drawing.surface.height); ++y) {
        for (int32_t x = 0; x < static_cast<int32_t>(drawing.surface.width); ++x) {
            const bool in_window = x >= left && x < right && y >= top && y < bottom;
            const bool untouched_here = kept(drawing, x, y);
            if (!in_window && !untouched_here)
                ++outside;
            if (in_window && untouched_here)
                ++blank;
        }
    }
    if (outside != 0)
        std::fprintf(
            stderr, "%s: %zu pixels drawn outside the window\n", scene.name.c_str(), outside
        );
    if (blank != 0)
        std::fprintf(stderr, "%s: %zu pixels of the window not drawn\n", scene.name.c_str(), blank);
    OA_CHECK(outside == 0);
    OA_CHECK(blank == 0);
}

/// Writes a drawing's window as a PNG file.
///
/// @param drawing the drawing
/// @param path the file
/// @return true when it was written
bool write_window(const Drawing& drawing, const std::filesystem::path& path) {
    const auto width = static_cast<std::size_t>(drawing.window.x * drawing.scale);
    const auto height = static_cast<std::size_t>(drawing.window.y * drawing.scale);
    const auto margin = static_cast<std::size_t>(margin_points * drawing.scale);
    std::vector<uint8_t> rows;
    rows.reserve(width * height * 3);
    for (std::size_t y = 0; y < height; ++y) {
        const std::size_t start = ((y + margin) * drawing.surface.width + margin) * 3;
        rows.insert(
            rows.end(),
            drawing.surface.rgb.begin() + static_cast<std::ptrdiff_t>(start),
            drawing.surface.rgb.begin() + static_cast<std::ptrdiff_t>(start + width * 3)
        );
    }
    png::Header header;
    header.width = static_cast<uint32_t>(width);
    header.height = static_cast<uint32_t>(height);
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

/// Returns a window's viewport at a scale.
///
/// @param width the canvas's width, in pixels
/// @param height the canvas's height, in pixels
/// @param percent the scale, as a percentage
/// @return the viewport
kit::Viewport window_of(int32_t width, int32_t height, int32_t percent) {
    kit::Viewport viewport;
    viewport.width = width;
    viewport.height = height;
    viewport.scale_percent = percent;
    return viewport;
}

} // namespace

int main(int argc, char** argv) {
    std::optional<std::filesystem::path> frames;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--frames" && index + 1 < argc)
            frames = std::filesystem::path(argv[++index]);
        else if (argument != oa::test::kGameDataSwitch) {
            std::fprintf(stderr, "draw: unexpected argument %s\n", argv[index]);
            return 2;
        }
    }
    oa::AssetStore assets = oa::test::require_game_assets("the Library's pictures");
    const kit::Fonts fonts = kit::load_game_fonts(assets);
    const std::unique_ptr<text_font::FontStack> modern =
        text_font::FontStack::open(text_font::bundled_font_directory());
    if (modern) {
        oa::present::GameTextHooks hooks;
        hooks.context = modern.get();
        hooks.draw = draw_modern;
        oa::present::set_game_text_hooks(hooks);
    } else {
        std::printf(
            "draw: no modern fonts beside the test; the characters the game's fonts lack are not "
            "drawn\n"
        );
    }
    if (frames) {
        std::error_code error;
        std::filesystem::create_directories(*frames, error);
        if (error) {
            std::fprintf(stderr, "draw: cannot make %s\n", frames->string().c_str());
            return 1;
        }
    }

    const std::vector<Scene> scenes{
        {"compact", window_of(640, 480, 100), false},
        {"regular", window_of(1920, 1080, 200), false},
        {"large", window_of(2560, 1440, 200), false},
        {"details-compact", window_of(640, 480, 100), true},
        {"active-compact", window_of(640, 480, 100), false, lib::Tab::active, true},
        {"active-regular", window_of(1920, 1080, 200), false, lib::Tab::active, true},
        {"active-large", window_of(2560, 1440, 200), false, lib::Tab::active, true},
        {"updates-compact", window_of(640, 480, 100), false, lib::Tab::updates},
        {"updates-large", window_of(2560, 1440, 200), false, lib::Tab::updates},
        {"registries-compact", window_of(640, 480, 100), false, lib::Tab::registries},
        {"registries-regular", window_of(1920, 1080, 200), false, lib::Tab::registries},
        {"registries-large", window_of(2560, 1440, 200), false, lib::Tab::registries},
        {"languages-compact", window_of(640, 480, 100), false, lib::Tab::languages},
        {"maps-one-large",
         window_of(2560, 1440, 200),
         false,
         lib::Tab::maps,
         true,
         std::string(fixture::core),
         "shore"},
        {"maps-several-large",
         window_of(2560, 1440, 200),
         false,
         lib::Tab::maps,
         true,
         std::string(fixture::added),
         "desert-pack"},
    };
    for (const Scene& scene : scenes) {
        const Drawing first = draw_scene(scene, fonts);
        const Drawing second = draw_scene(scene, fonts);
        if (first.surface.rgb != second.surface.rgb)
            std::fprintf(stderr, "%s: two drawings differ\n", scene.name.c_str());
        OA_CHECK(first.surface.rgb == second.surface.rgb);
        check_edges(scene, first);
        if (frames) {
            const std::filesystem::path path = *frames / ("library-" + scene.name + ".png");
            const bool written = write_window(first, path);
            if (!written)
                std::fprintf(stderr, "draw: cannot write %s\n", path.string().c_str());
            OA_CHECK(written);
        }
    }
    oa::present::set_game_text_hooks({});
    return oa::test::check_exit_status();
}
