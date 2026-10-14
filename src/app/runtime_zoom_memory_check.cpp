// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-zoom-memory: a large battlefield eased out and back with a small
// model budget, then the main menu. The run must not end out of memory.
#include "oa/app/runtime.hpp"

#include "match_models.hpp"

#include "oa/present/model/rgb_bridge.hpp"
#include "oa/ui/engine_settings.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace oa::app {
namespace {

/// Nanoseconds a frame of the check stands for: 60 frames a second, as the
/// zoom ease in runtime_camera.cpp.
constexpr uint64_t kFrameNs = 1'000'000'000ULL / 60ULL;

/// Frames the zoom is given to settle, as the zoom checks give it.
constexpr int kSettleFrames = 240;

/// Times the view eases out and back.
constexpr int kCycles = 3;

/// The budget OA_RENDERED_UNITS_BUDGET must name for this check, in bytes.
constexpr uint64_t kSmallBudget = uint64_t{8} << 20;

void fail(const std::string& what) {
    throw std::runtime_error("zoom memory check: " + what);
}

} // namespace

void Runtime::check_zoom_memory() {
    namespace settings = oa::ui::engine_settings;
    if (rendered_units_budget() != kSmallBudget)
        fail("OA_RENDERED_UNITS_BUDGET is not 8 MiB");
    exercise_click(menu::resource_name(menu::Button::single_player));
    if (screen_ != Screen::single_player)
        fail("Single did not open");
    exercise_click(entry::resource_name(entry::Button::skirmish));
    if (screen_ != Screen::skirmish)
        fail("Skirmish did not open");
    exercise_click(skirmish::resource_name(skirmish::Button::start));
    if (screen_ != Screen::match || !match_ || !selected_tnt_)
        fail("Start did not enter a match");

    auto chosen = engine_settings();
    chosen.max_zoom_out = settings::ZoomOutLimit::one_thirty_second;
    chosen.zoomed_out_units = settings::ZoomedOutUnits::rendered;
    apply_engine_settings(chosen);
    match_layout_ = lay_out_match(1920, 1080);
    match_paused_ = true;
    stop_match_tracking();
    match_zoom_ = match_zoom_target_ = kDefaultBattlefieldZoom;

    const uint64_t budget = rendered_units_budget();
    std::size_t peak = 0;
    const auto watch = [&] {
        const auto& picture = match_models().bridge;
        const std::size_t held = picture.pixels.capacity() + picture.baseline.capacity();
        if (held > budget)
            fail("the units' picture grew past its budget");
        if (picture.buffer_growths > 48)
            fail("the units' picture grew on too many frames");
        if (held > peak)
            peak = held;
    };
    const auto ease_to = [&](float target) {
        match_zoom_target_ = target;
        for (int step = 0; step < kSettleFrames; ++step) {
            frame_time_ns_ += kFrameNs;
            move_match_camera();
            render_match_surface();
            watch();
            if (match_zoom_ == match_zoom_target_)
                break;
        }
        render_match_surface();
        watch();
        if (match_zoom_ != match_zoom_target_)
            fail("the zoom did not settle");
    };

    for (int cycle = 0; cycle < kCycles; ++cycle) {
        ease_to(least_match_zoom());
        ease_to(kDefaultBattlefieldZoom);
    }
    for (int step = 0; step < static_cast<int>(oa::present::model::bridge_release_frames); ++step) {
        frame_time_ns_ += kFrameNs;
        move_match_camera();
        render_match_surface();
        watch();
    }
    const std::size_t held =
        match_models().bridge.pixels.capacity() + match_models().bridge.baseline.capacity();
    if (peak > held && held > budget)
        fail("zooming back in left a picture past its budget");

    return_to_skirmish_menu();
    exercise_click(skirmish::resource_name(skirmish::Button::previous_menu));
    if (screen_ != Screen::single_player)
        fail("PrevMenu did not leave the skirmish");
    exercise_click(entry::resource_name(entry::Button::previous_menu));
    if (screen_ != Screen::main_menu)
        fail("PrevMenu did not return to the main menu");
    std::cout << "zoom memory check: main menu\n";
}

} // namespace oa::app
