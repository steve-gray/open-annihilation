// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's drawing: its display list, through the kit's painter. Every
// colour is a kit token and every size a kit metric; nothing is drawn here
// but what the list holds.
#include "oa/ui/library/screen.hpp"

#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/layout.hpp"

namespace oa::ui::library {

void draw_library(const kit::Canvas& canvas, const kit::DisplayList& list) {
    kit::paint(canvas, list);
}

} // namespace oa::ui::library
