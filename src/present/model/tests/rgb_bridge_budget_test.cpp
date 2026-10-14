// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The model bridge stays within its byte budget while a zoom eases out and
// back, grows a bounded number of times, releases after the view is small
// again, and turns an allocation failure into a refused begin.
#include "oa/present/model/rgb_bridge.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <optional>
#include <vector>

namespace {

int failures = 0;

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x);             \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

using namespace oa::present::model;

// The zoom ease of Runtime::step_match_zoom: 12 a second, a frame at 60 a
// second, arrived within a ten-thousandth of the target's logarithm.
constexpr float kZoomLerpHz = 12.0F;
constexpr double kZoomArrival = 1.0e-4;
constexpr float kFrameSeconds = 1.0F / 60.0F;
constexpr float kFurthestZoom = 1.0F / 64.0F;

// Battlefields of a 1920x1080 and a 3840x2160 window: the side column is 128
// and the bars are 32 each.
constexpr int32_t kWide1080 = 1792;
constexpr int32_t kHigh1080 = 1016;
constexpr int32_t kWide2160 = 3712;
constexpr int32_t kHigh2160 = 2096;

int handler_runs = 0;

void remembering_handler() {
    ++handler_runs;
    throw std::bad_alloc();
}

oa::Palette ramp_palette() {
    oa::Palette palette{};
    for (int i = 0; i < OA_PALETTE_COLORS; ++i)
        palette.entries[i] = {
            static_cast<uint8_t>(i), static_cast<uint8_t>(255 - i), static_cast<uint8_t>(i / 2), 0
        };
    return palette;
}

struct Frame {
    std::vector<uint8_t> rgb;
    int width;
    int height;

    Frame(int w, int h) : rgb(static_cast<std::size_t>(w * h * 3), 0), width(w), height(h) {}

    RgbFrame view() { return {rgb.data(), width, height, width * 3}; }
};

float ease_zoom(float zoom, float target) {
    const double from = std::log(static_cast<double>(zoom > 0.0F ? zoom : 1.0F));
    const double to = std::log(static_cast<double>(target));
    const double eased =
        from + (to - from) * (1.0 - std::exp(-static_cast<double>(kZoomLerpHz) * kFrameSeconds));
    if (std::abs(to - eased) < kZoomArrival)
        return target;
    return static_cast<float>(std::exp(eased));
}

std::size_t picture_bytes(const RgbBridge& bridge) {
    return bridge.pixels.capacity() + bridge.baseline.capacity();
}

void ease_within_budget(int32_t wide, int32_t high, uint64_t budget, bool expect_release) {
    RgbBridge bridge;
    bridge.byte_budget = budget;
    const oa::Palette palette = ramp_palette();
    Frame frame(1, 1);
    const oa::Rect32 area{0, 0, wide - 1, high - 1};
    float zoom = 1.0F;
    std::size_t peak = 0;
    const uint32_t growths_before = bridge.buffer_growths;
    for (int step = 0; step < 10000 && zoom != kFurthestZoom; ++step) {
        zoom = ease_zoom(zoom, kFurthestZoom);
        const bool held = bridge_begin(bridge, frame.view(), area, zoom, palette);
        const std::size_t held_bytes = picture_bytes(bridge);
        CHECK(held_bytes <= budget);
        if (held_bytes > peak)
            peak = held_bytes;
        if (!held)
            CHECK(bridge.refused_bytes > budget || bridge.refused_bytes == 0);
    }
    CHECK(zoom == kFurthestZoom);
    for (int step = 0; step < 10000 && zoom != 1.0F; ++step) {
        zoom = ease_zoom(zoom, 1.0F);
        const bool held = bridge_begin(bridge, frame.view(), area, zoom, palette);
        CHECK(picture_bytes(bridge) <= budget);
        if (!held)
            CHECK(bridge.refused_bytes > budget || bridge.refused_bytes == 0);
        (void)held;
    }
    CHECK(zoom == 1.0F);
    CHECK(bridge.buffer_growths - growths_before <= 16);
    const std::size_t before_release = picture_bytes(bridge);
    for (uint32_t step = 0; step < bridge_release_frames; ++step)
        CHECK(bridge_begin(bridge, frame.view(), area, 1.0F, palette));
    const std::size_t after_release = picture_bytes(bridge);
    CHECK(after_release <= budget);
    if (expect_release) {
        CHECK(peak > after_release);
        CHECK(before_release > after_release || after_release < peak);
    }
}

void test_budget_numbers() {
    constexpr uint64_t gibibyte = uint64_t{1} << 30;
    const uint64_t narrow =
        bridge_byte_budget(uint64_t{16} * gibibyte, uint64_t{3} * gibibyte / 2, true);
    CHECK(narrow == (uint64_t{3} * gibibyte / 2) / bridge_memory_share);
    CHECK(narrow < uint64_t{2} * gibibyte);
    const uint64_t wide = bridge_byte_budget(uint64_t{16} * gibibyte, std::nullopt, false);
    CHECK(wide == uint64_t{16} * gibibyte / bridge_memory_share);
    const uint64_t bogus =
        bridge_byte_budget(uint64_t{100} * gibibyte, uint64_t{100} * gibibyte, true);
    CHECK(bogus == bridge_address_space / bridge_memory_share);
    CHECK(bogus < uint64_t{2} * gibibyte);
    CHECK(bridge_byte_budget(0, std::nullopt, true) == bridge_address_cap);
    CHECK(bridge_byte_budget(0, std::nullopt, false) == bridge_unknown_budget);
    CHECK(
        bridge_byte_budget(uint64_t{4} * gibibyte, uint64_t{64} << 20, true) ==
        (uint64_t{64} << 20) / bridge_memory_share
    );
    CHECK(bridge_byte_budget(0, uint64_t{0}, true) == 0);
}

void test_easing() {
    const uint64_t narrow_budget = bridge_byte_budget(uint64_t{4} << 30, uint64_t{64} << 20, true);
    const uint64_t wide_budget = bridge_byte_budget(uint64_t{512} << 20, std::nullopt, false);
    CHECK(narrow_budget == uint64_t{8} << 20);
    CHECK(wide_budget == uint64_t{64} << 20);
    ease_within_budget(kWide1080, kHigh1080, narrow_budget, true);
    ease_within_budget(kWide2160, kHigh2160, narrow_budget, false);
    ease_within_budget(kWide1080, kHigh1080, wide_budget, true);
    ease_within_budget(kWide2160, kHigh2160, wide_budget, true);
}

void test_allocation_failure_draws_dots() {
    const auto previous = std::set_new_handler(remembering_handler);
    handler_runs = 0;
    RgbBridge bridge;
    bridge.byte_budget = uint64_t{64} << 20;
    const oa::Palette palette = ramp_palette();
    Frame frame(1, 1);
    bridge_fail_allocations(true);
    bool threw = false;
    bool began = true;
    try {
        began =
            bridge_begin(bridge, frame.view(), {0, 0, kWide1080 - 1, kHigh1080 - 1}, 1.0F, palette);
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    bridge_fail_allocations(false);
    CHECK(!began);
    CHECK(!threw);
    CHECK(handler_runs == 0);
    CHECK(bridge_begin(bridge, frame.view(), {0, 0, 0, 0}, 1.0F, palette));
    CHECK(bridge.surface.width == 1);
    CHECK(bridge.surface.height == 1);
    std::set_new_handler(previous);
}

void test_span_overflow_refuses() {
    RgbBridge bridge;
    const oa::Palette palette = ramp_palette();
    Frame frame(1, 1);
    const bool began = bridge_begin(
        bridge,
        frame.view(),
        {std::numeric_limits<int32_t>::min(), 0, std::numeric_limits<int32_t>::max(), 0},
        1.0F,
        palette
    );
    CHECK(!began);
}

} // namespace

int main() {
    test_budget_numbers();
    test_easing();
    test_allocation_failure_draws_dots();
    test_span_overflow_refuses();
    if (failures != 0)
        std::fprintf(stderr, "%d checks failed\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
