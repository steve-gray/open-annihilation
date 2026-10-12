// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The picture well's sample: the Library's Active tab, drawn live from a fixed
// Ridge model in the language the player chose. No picture file is shipped.

#include "oa/ui/first_run/first_run.hpp"

#include "oa/ui/kit/canvas.hpp"
#include "oa/ui/library/library.hpp"
#include "oa/ui/library/screen.hpp"
#include "oa/ui/library/text.hpp"

#include <algorithm>
#include <string>

namespace oa::ui::first_run {
namespace {

/// Fills the sample the picture well draws: Ridge playing, one map, and the language in use.
///
/// @param own the language's name in itself
/// @param english the language's English name
/// @param text how the Library looks its words up; empty shows English
/// @return the Library, on the Active tab
library::Library
ridge_library(std::string_view own, std::string_view english, const library::TextHooks& text) {
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
    library.text = text;
    library::refresh(library, std::move(inputs));
    library::set_tab(library, library::Tab::active);
    return library;
}

} // namespace

std::string sample_words(const Model& model) {
    const library::Library sample =
        ridge_library(model.sample_own, model.sample_english, model.sample_text);
    std::string words;
    const auto add = [&](std::string_view text) {
        words.append(text);
        words.push_back('\n');
    };
    add(library::tab_text(library::Tab::active, false, sample.text));
    add(library::tab_text(library::Tab::mods, false, sample.text));
    add(library::state_text(library::State::playing, sample.text));
    add(library::shown(sample.text, library::words::action_unload_mod));
    add(library::shown(sample.text, library::words::registry_built_in));
    return words;
}

void draw_sample(
    const kit::Canvas& canvas, const kit::Rect& inset, const Model& model, kit::SizeClass size_class
) {
    if (canvas.fonts == nullptr || inset.width < 8 || inset.height < 8)
        return;
    library::Library library =
        ridge_library(model.sample_own, model.sample_english, model.sample_text);
    kit::Frame sample_frame;
    sample_frame.size_class = size_class;
    sample_frame.area = {0, 0, inset.width, inset.height};
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
    // The same scale on both axes. A well that is the Library's own size is
    // copied straight across, so its text is not squeezed over itself.
    int32_t place_w = inset.width;
    int32_t place_h = inset.height;
    if (window.x != inset.width || window.y != inset.height) {
        const int64_t fit_w = static_cast<int64_t>(inset.height) * window.x;
        const int64_t fit_h = static_cast<int64_t>(inset.width) * window.y;
        if (fit_h <= fit_w) {
            place_w = inset.width;
            place_h = static_cast<int32_t>(fit_h / std::max(window.x, int32_t{1}));
        } else {
            place_h = inset.height;
            place_w = static_cast<int32_t>(fit_w / std::max(window.y, int32_t{1}));
        }
        place_w = std::max(place_w, int32_t{1});
        place_h = std::max(place_h, int32_t{1});
    }
    const kit::Rect place{
        inset.x + (inset.width - place_w) / 2,
        inset.y + (inset.height - place_h) / 2,
        place_w,
        place_h,
    };
    kit::scale_blit(canvas, drawn, place, [&](const kit::Canvas& into) {
        library::draw_library(into, laid);
    });
}

} // namespace oa::ui::first_run
