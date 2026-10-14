// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Writes the manual's pictures of the settings dialog: each section alone,
// unlocked, at Compact and 2x (960 by 648), in the installed game's fonts.
//
// Usage: oa-ui-engine-settings-pictures DIRECTORY
//
// The data test ui-engine-settings-pictures runs this into a scratch folder.
// Without OA_GAME_DIR it skips, as the other game-data tests do.

#include "oa/test/game_assets.hpp"
#include "oa/ui/engine_settings/dialog.hpp"

#include "oa/formats/png.hpp"
#include "oa/ui/frontend_renderer.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace png = oa::formats::png;
namespace renderer = oa::ui::frontend_renderer;
namespace settings = oa::ui::engine_settings;

using settings::Dialog;
using settings::Page;

constexpr int32_t kScale = 2;
constexpr std::string_view kVersion = "v0.8.0";

/// The file docs/images/settings uses for a section's top.
std::string picture_name(Page page) {
    const std::string word(settings::page_word(page));
    if (word == "controls" || word == "language" || word == "graphics" || word == "downloads")
        return word + "-1.png";
    return word + ".png";
}

/// Fills the rows a host supplies, at the defaults the manual shows.
void dress(Dialog& dialog) {
    // The main menu lists More in the Library… and REGISTRIES IN THE LIBRARY….
    dialog.library_link = true;
    dialog.user_folder = "/Users/player/Documents/Open Annihilation";
    dialog.game_files_summary = "3.1c · Core Contingency · Battle Tactics · music";
    dialog.game_files_sizes = "1.1 GB · 37 GB free on this tablet";
    dialog.game_files_location = "In the file manager: Open Annihilation \u203A Total Annihilation";
    dialog.game_files_device = "tablet";

    settings::InstallIdRow install;
    install.registry_id = "core-prime";
    install.registry_name = "Core Prime";
    install.on = true;
    settings::DownloadsOffer downloads;
    downloads.install_ids.push_back(std::move(install));
    settings::set_downloads_offer(dialog, downloads);

    const auto bundle = [](std::string_view id, std::string_view name, std::string_view size) {
        settings::GameContentRow row;
        row.id = std::string(id);
        row.name = std::string(name);
        row.size = std::string(size);
        row.check = "All files match";
        return row;
    };
    dialog.game_content.push_back(bundle("totala", "Total Annihilation", "188 MB"));
    dialog.game_content.push_back(bundle("cc", "Core Contingency", "42 MB"));
    dialog.game_content.push_back(bundle("bt", "Battle Tactics", "28 MB"));
    dialog.game_content.push_back(bundle("ta-music", "Total Annihilation Music", "61 MB"));
}

Dialog section_dialog(Page page) {
    settings::Inputs inputs;
    inputs.players_own_profile = true;
    inputs.macos = true;
    const settings::EngineSettings defaults = settings::default_settings(inputs);
    Dialog dialog;
    settings::open_dialog(
        dialog,
        defaults,
        defaults,
        {},
        kVersion,
        page,
        {},
        settings::highest_unit_limit,
        {},
        nullptr,
        true,
        true,
        true
    );
    dress(dialog);
    return dialog;
}

bool write_png(const renderer::Surface& surface, const std::filesystem::path& path) {
    png::Header header;
    header.width = surface.width;
    header.height = surface.height;
    header.bit_depth = 8;
    header.color_type = png::ColorType::rgb;
    std::vector<uint8_t> file;
    if (!png::write(png::Image{header, {}, surface.rgb}, &file))
        return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size())
    );
    return static_cast<bool>(output);
}

bool draw_section(
    const Dialog& dialog, const settings::DialogFonts& fonts, const std::filesystem::path& path
) {
    const int32_t width = settings::dialog_width * kScale;
    const int32_t height = settings::dialog_height * kScale;
    renderer::Surface surface;
    surface.width = static_cast<uint32_t>(width);
    surface.height = static_cast<uint32_t>(height);
    surface.rgb.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3, 0);
    settings::draw_dialog(surface, {0, 0, kScale}, dialog, fonts, {});
    if (surface.width != 960 || surface.height != 648) {
        std::cerr << "pictures: " << path.filename().string() << " is " << surface.width << " by "
                  << surface.height << ", not 960 by 648\n";
        return false;
    }
    if (!write_png(surface, path)) {
        std::cerr << "pictures: cannot write " << path.string() << '\n';
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: oa-ui-engine-settings-pictures DIRECTORY\n";
        return 2;
    }
    const std::filesystem::path directory(argv[1]);
    oa::AssetStore assets = oa::test::require_game_assets("the settings pictures");
    const settings::DialogFonts fonts = settings::load_dialog_fonts(assets);

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        std::cerr << "pictures: cannot make " << directory.string() << '\n';
        return 1;
    }

    bool ok = true;
    for (const Page page : settings::dialog_pages(settings::DialogKind::engine, true, true, true)) {
        Dialog dialog = section_dialog(page);
        if (!draw_section(dialog, fonts, directory / picture_name(page)))
            ok = false;
        if (page == Page::downloads) {
            dialog.scroll[static_cast<std::size_t>(Page::downloads)] =
                settings::scroll_limit(dialog);
            if (!draw_section(dialog, fonts, directory / "downloads-2.png"))
                ok = false;
        }
    }
    return ok ? 0 : 1;
}
