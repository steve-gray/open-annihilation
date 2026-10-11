// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The notice shown at the start when a language is only available as a
// download: its model and the display list a question of the kit builds
// from it. The screen that shows it is the application's.
#pragma once

#include "oa/ui/kit/layout.hpp"

#include <string>

namespace oa::ui::engine_settings {

/// What the missing-language notice shows. Texts are finished: the layout
/// looks an English line up and leaves a line that is not English as given.
struct LanguageNotice {
    std::string tag{};            ///< the language's BCP-47 tag
    std::string title{};          ///< the language's own name, or its English name
    std::string own_body{};       ///< the body in the language; empty when the engine holds none
    std::string english_body{};   ///< the body in English
    std::string status{};         ///< a line under the body, or two joined by a line break
    std::string download_label{}; ///< Download, or Try again after a failure
    std::string progress_text{};  ///< how far a download has come; empty for none
    /// How far a download has come, from 0 to 1. Below 0, no bar is shown.
    float progress{-1.F};
    bool download_enabled{}; ///< false greys Download and it takes no press
    bool failed{};           ///< the status is a failure, drawn in the lock colour
    friend bool operator==(const LanguageNotice&, const LanguageNotice&) = default;
};

/// Lays the notice out as a question: the badge and "Language", the title
/// large, the language's body, the English body dimmed, the status, a
/// progress bar while one is named, then Use English and the download
/// button. Download is the primary and takes the focus first. Tab goes to
/// Use English and then back to it. A download that is not enabled is grey
/// and takes no press.
///
/// English texts are looked up. A text that is not English is drawn as given.
///
/// @param notice what the notice shows
/// @param frame the frame it is laid out on; its size class sets the width
/// @return the display list, in points from the notice's top left corner
[[nodiscard]] oa::ui::kit::DisplayList
layout_language_notice(const LanguageNotice& notice, const oa::ui::kit::Frame& frame);

} // namespace oa::ui::engine_settings
