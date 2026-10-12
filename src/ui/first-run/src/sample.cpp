// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The picture well's sample: the Library's Active tab, drawn live from a fixed
// Ridge model in the language the player chose. No picture file is shipped.

#include "oa/ui/first_run/first_run.hpp"

#include "oa/ui/kit/canvas.hpp"
#include "oa/ui/library/library.hpp"
#include "oa/ui/library/screen.hpp"

namespace oa::ui::first_run {
namespace {

/// Fills the sample the picture well draws: Ridge playing, one map, and the language in use.
///
/// @param own the language's name in itself
/// @param english the language's English name
/// @return the Library, on the Active tab
library::Library ridge_library(std::string_view own, std::string_view english) {
    library::Inputs inputs;
    library::Registry registry;
    registry.id = "coreprime";
    registry.name = "Core Prime";
    registry.title = "Core Prime";
    registry.built_in = true;
    registry.downloads_on = true;
    registry.on = true;
    inputs.registries.push_back(std::move(registry));

    library::Installed playing;
    playing.kind = library::Kind::mod;
    playing.key = "ridge";
    playing.name = "Ridge";
    playing.version = "1.0";
    playing.revision = 28;
    playing.registry = "coreprime";
    playing.release = 28;
    playing.playing = true;
    playing.author = "Open Annihilation";
    inputs.installed.push_back(std::move(playing));

    library::MapLine valley;
    valley.name = "Ridge Valley";
    valley.players = 2;
    valley.size = "16x16";
    valley.stem = "ridge-valley";
    library::InstalledMaps maps;
    maps.name = "Ridge maps";
    maps.key = "ridge-maps";
    maps.registry = "coreprime";
    maps.maps.push_back(std::move(valley));
    inputs.map_packs.push_back(std::move(maps));

    inputs.engine = {0, 8, 0};
    inputs.version_text = "0.8.0";
    inputs.language.name = std::string(own);
    if (english != own)
        inputs.language.english = std::string(english);

    library::Library library;
    library::refresh(library, std::move(inputs));
    library::set_tab(library, library::Tab::active);
    return library;
}

} // namespace

void draw_sample(const kit::Canvas& canvas, const kit::Rect& inset, const Model& model) {
    if (canvas.fonts == nullptr || inset.width < 8 || inset.height < 8)
        return;
    library::Library library = ridge_library(model.sample_own, model.sample_english);
    kit::Frame sample_frame;
    sample_frame.size_class = kit::SizeClass::regular;
    sample_frame.area = {0, 0, 720, 486};
    sample_frame.scale_percent = 100;
    const kit::Point window = library::window_size(sample_frame);
    if (window.x <= 0 || window.y <= 0)
        return;
    kit::Frame laid_frame = sample_frame;
    laid_frame.area = {0, 0, window.x, window.y};
    library::ScreenState state;
    const kit::DisplayList laid =
        library::library_layout(library, state, laid_frame, *canvas.fonts);
    const kit::Rect drawn{0, 0, window.x, window.y};
    kit::scale_blit(canvas, drawn, inset, [&](const kit::Canvas& into) {
        library::draw_library(into, laid);
    });
}

} // namespace oa::ui::first_run
