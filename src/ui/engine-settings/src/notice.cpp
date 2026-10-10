// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A notice of Open Annihilation's own: the kit's notice under its old names,
// and the parts it draws as the dialog's layout lists them.

#include "oa/ui/engine_settings/notice.hpp"

#include "geometry.hpp"
#include "oa/ui/kit/components_more.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace oa::ui::engine_settings {

namespace kit = oa::ui::kit;

int32_t notice_height(const Notice& notice, const DialogFonts* fonts) {
    return kit::place_notice(notice, fonts).height;
}

std::vector<LayoutPart> notice_layout(const Notice& notice, const DialogFonts* fonts) {
    const kit::PlacedNotice placed = kit::place_notice(notice, fonts);
    std::vector<LayoutPart> parts;
    parts.push_back(LayoutPart{kit::notice_mark, {}, DialogFont::regular, 0, no_control});
    parts.push_back(
        LayoutPart{
            placed.title,
            std::string(geometry::shown_text(notice.title)),
            DialogFont::regular,
            kit::compact_metrics.heading_tracking,
            no_control
        }
    );
    for (const auto& line : placed.lines)
        parts.push_back(
            LayoutPart{
                line.rect,
                line.text,
                line.path ? DialogFont::regular : DialogFont::small,
                0,
                no_control
            }
        );
    parts.push_back(LayoutPart{placed.open_button, {}, DialogFont::small, 0, notice_open_control});
    parts.push_back(LayoutPart{placed.ok_button, {}, DialogFont::small, 0, notice_ok_control});
    return parts;
}

NoticeAction notice_pointer_move(Notice& notice, int32_t x, int32_t y, int32_t height) {
    return kit::notice_pointer_move(notice, {x, y}, height);
}

NoticeAction notice_pointer_down(Notice& notice, int32_t x, int32_t y, int32_t height) {
    return kit::notice_pointer_down(notice, {x, y}, height);
}

NoticeAction
notice_finger_down(Notice& notice, int32_t x, int32_t y, int32_t height, int32_t reach) {
    return kit::notice_finger_down(notice, {x, y}, height, reach);
}

NoticeAction notice_pointer_up(Notice& notice, int32_t x, int32_t y, int32_t height) {
    return kit::notice_pointer_up(notice, {x, y}, height);
}

NoticeAction notice_key(Notice& notice, DialogKey key) {
    return kit::notice_key(notice, static_cast<kit::Key>(key));
}

void draw_notice(
    oa::ui::frontend_renderer::Surface& target,
    const oa::ui::frontend_renderer::Placement& placement,
    const Notice& notice,
    const DialogFonts& fonts,
    const oa::ui::frontend_renderer::RgbaPicture& icon
) {
    kit::draw_notice({&target, placement, &fonts, icon}, notice, geometry::shown_text);
}

} // namespace oa::ui::engine_settings
