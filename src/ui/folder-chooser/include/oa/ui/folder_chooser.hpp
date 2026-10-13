// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The in-engine locator: places, the folders and installers found, what each
// one is and which bundles it holds, the layout in the Game files screen's
// look as the kit's parts, the hit test, the focus order and what presses
// and keys ask of the app. Pure: no SDL and no app header.
#pragma once

#include "oa/ui/game_files.hpp"
#include "oa/ui/kit/layout.hpp"
#include <cstddef>
#include <stdint.h>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::folder_chooser {

using oa::ui::kit::Point;
using oa::ui::kit::Rect;
using oa::ui::game_files::TextMeasureHooks;
using oa::ui::game_files::Viewport;

/// Which band of the sidebar a place belongs to.
enum class PlaceGroup : uint8_t {
    places,     ///< Home, Desktop, Documents, Downloads, Applications
    found,      ///< installs and installers already recognised
    volumes,    ///< disks and other drives
    deck_games, ///< Steam, Heroic, Lutris and Bottles on this machine
};

/// One place in the sidebar.
struct Place {
    std::string label{};                  ///< "Downloads", "Steam · Proton"
    PlaceGroup group{PlaceGroup::places}; ///< which band
};

/// One file the recognition of a folder looks at: its name and its size.
struct ChildFile {
    std::string name{}; ///< the name, or a path with `/` from the folder
    uint64_t size{};    ///< bytes
};

/// What a folder or a file is, for the locator's column and its side panel.
struct SourceAccount {
    std::string what{};                 ///< the column: "folder", "GOG installer (macOS)"
    std::string title{};                ///< the side panel's heading
    std::string account{};              ///< the side panel's paragraph; empty for a plain file
    std::vector<std::string> bundles{}; ///< the bundles it holds, in table order
    bool directory{};                   ///< a folder, which opens
    bool recognised{};                  ///< a game folder or a known installer
    bool choosable{};                   ///< the player can use it as the game
};

/// Says what a folder or a file is, from its name, its size and, for a folder, the files
/// inside it.
///
/// A known installer is recognised by its size (the Commander Pack's three
/// installers, and the 1997 demo's installer when `demo_installer_bytes` is
/// that installer's size). A folder is a game when a file inside it matches
/// a bundle's file by name and size. The checksum of an installer is not
/// read here; it is checked when the game is read out of the installer.
///
/// @param name the file or folder name
/// @param size the file's size; ignored for a folder
/// @param directory it is a folder
/// @param children the folder's files, by name and size; empty for a file
/// @param demo_installer_bytes the 1997 demo installer's size, in bytes
/// @return the column, the panel and the bundles
[[nodiscard]] SourceAccount describe_source(
    std::string_view name,
    uint64_t size,
    bool directory,
    const std::vector<ChildFile>& children,
    uint64_t demo_installer_bytes
);

/// One row of the folder being looked at.
struct Entry {
    std::string name{};                 ///< the file or folder name, UTF-8
    std::string what{};                 ///< the column
    std::string size_label{};           ///< "2.4 GB"; empty when there is nothing to say
    std::string title{};                ///< the side panel's heading
    std::string account{};              ///< the side panel's paragraph
    std::vector<std::string> bundles{}; ///< the bundles it holds
    bool directory{};                   ///< opening it shows that folder
    bool recognised{};                  ///< drawn as something the locator knows
    bool choosable{};                   ///< Use this folder / Use this installer
};

/// What the app tells the locator.
struct Model {
    std::string version{}; ///< "v0.8", at the header's right
    std::string notice{};  ///< a remembered folder that has gone, or a dialog that failed
    bool deck{};           ///< Steam Deck: larger rows, pad hints, no system picker
    bool offers_dialog{};  ///< "Use the system picker…" (desktop, not Game Mode)
    /// macOS asks once before an app reads Desktop, Documents or Downloads.
    bool permission_note{};
    std::string path{};           ///< the folder shown, UTF-8
    std::string filter{};         ///< the filter's text; empty for none
    std::vector<Place> places{};  ///< the sidebar, in the order shown
    int32_t place{-1};            ///< the place whose folder is shown; -1 for none
    std::vector<Entry> entries{}; ///< the folder's rows, in the order shown
    int32_t selected{-1};         ///< the row the side panel describes; -1 for none
    bool has_parent{};            ///< the folder shown has a parent
    bool can_choose{};            ///< the accent button takes a press
    std::string choose_label{};   ///< "Use this folder" or "Use this installer"
};

/// What a press reaches.
enum class TargetKind : uint8_t {
    none,   ///< nothing
    place,  ///< a place in the sidebar; index: the place
    up,     ///< up one folder
    filter, ///< the filter
    entry,  ///< a row; index: the entry
    dialog, ///< Use the system picker…
    cancel, ///< Cancel
    choose, ///< Use this folder, or Use this installer
};

/// A target: its kind and which one. Each part that draws a target carries it as its kit
/// control number (control_id).
struct Target {
    TargetKind kind{TargetKind::none}; ///< what it is
    uint16_t index{};                  ///< which place or row
    /// Tells whether two targets are the same.
    friend bool operator==(const Target&, const Target&) = default;
};

/// Returns a target's number in the kit's display list: its kind in the bits above the low 16
/// and its index in them; kit::no_control for TargetKind::none.
///
/// @param target the target
/// @return its number
[[nodiscard]] oa::ui::kit::ControlId control_id(Target target) noexcept;

/// Returns the target a kit control number stands for (control_id's inverse).
///
/// @param id the number
/// @return the target; TargetKind::none for kit::no_control or a number no target has
[[nodiscard]] Target target_of(oa::ui::kit::ControlId id) noexcept;

/// A laid-out locator: the kit's display list of its parts in painting order, inside the
/// Game files screen's layout that paint_game_files paints, with the locator's own fields
/// around it.
struct Layout {
    oa::ui::game_files::Layout paint{}; ///< the parts, their controls and the scrolling region
    std::vector<Target> focus_order{};  ///< the order the focus moves in
    int32_t scroll_max_points{};        ///< how far the rows scroll, points
};

/// What the locator itself keeps between frames.
struct UiState {
    int32_t focus{-1};       ///< the focused entry of focus_order, -1 for none
    bool focus_shown{};      ///< the focus is drawn (keys or a pad were used)
    int32_t pressed{-1};     ///< the pressed target, by its place in the order laid out
    int32_t scroll_points{}; ///< how far the rows are scrolled, points
};

/// What the locator asks of the app.
enum class Command : uint8_t {
    none,           ///< nothing
    open_place,     ///< shows a place; index: the place
    parent_folder,  ///< shows the parent folder
    activate_entry, ///< opens a folder row, or selects a file row; index: the row
    select_entry,   ///< the side panel follows a row the focus reached; index: the row
    choose,         ///< uses the selection, or the folder shown when that is the game
    use_dialog,     ///< opens the system's folder dialog
    cancel,         ///< leaves without a folder
    toggle_filter,  ///< the filter takes what is typed, or stops
};

/// A command and which one.
struct Outcome {
    Command command{Command::none}; ///< what is asked
    uint16_t index{};               ///< which place or row
};

/// The keys the locator takes.
enum class Key : uint8_t {
    up,        ///< the focus up
    down,      ///< the focus down
    left,      ///< the focus left
    right,     ///< the focus right
    enter,     ///< presses the focused target (A: open)
    escape,    ///< up a folder, or cancel (B: back)
    tab,       ///< the next target
    back_tab,  ///< the previous target
    page_up,   ///< a page of rows up
    page_down, ///< a page of rows down
    choose,    ///< uses the folder or installer (X)
    filter,    ///< the filter (Y)
};

/// Lays the locator out: the sidebar of places, the folder's rows with what each one is,
/// the side panel of the selection, and the buttons or, on a Steam Deck, the pad hints.
/// Every control is at least 44 points high. The rows scroll; the sidebar, the panel and
/// the buttons stay put.
///
/// @param model what the app tells the locator
/// @param state what the locator keeps between frames
/// @param viewport the canvas
/// @param measure how text is measured
/// @return the layout
[[nodiscard]] Layout lay_out(
    const Model& model,
    const UiState& state,
    const Viewport& viewport,
    const TextMeasureHooks& measure
);

/// Returns the target under a point, by the kit's reach over the layout's controls: the top
/// control whose part, cut by its clip, holds the point, none when that one is disabled; else
/// the nearest enabled one within reach.
///
/// @param layout the laid-out locator
/// @param point canvas pixels
/// @param reach_px how far from a part a point still reaches it, canvas pixels
/// @return the target; kind none for none
[[nodiscard]] Target hit_test(const Layout& layout, Point point, int reach_px) noexcept;

/// Takes a press going down at a point: marks its target pressed.
///
/// @param layout the laid-out locator
/// @param[in,out] state what the locator keeps between frames
/// @param point canvas pixels
void press_down(const Layout& layout, UiState& state, Point point) noexcept;

/// Takes the press coming up at a point.
///
/// @param layout the laid-out locator
/// @param[in,out] state what the locator keeps between frames
/// @param point canvas pixels
/// @return the target's command when the press is still on its target; none otherwise
[[nodiscard]] Outcome press_up(const Layout& layout, UiState& state, Point point) noexcept;

/// Takes a key. The arrows move the focus by the kit's two-dimensional focus. Tab moves
/// through the focus order. A row the focus reaches is selected and scrolled into view.
/// Enter presses the focused target. Escape goes up a folder, or cancels when there is no
/// parent. The choose key uses the selection whatever has the focus. The filter key asks
/// the app to take typed text.
///
/// @param layout the laid-out locator
/// @param[in,out] state what the locator keeps between frames
/// @param key the key
/// @return what the key asks of the app
[[nodiscard]] Outcome key(const Layout& layout, UiState& state, Key key) noexcept;

/// Scrolls the rows, held within the layout's range.
///
/// @param layout the laid-out locator
/// @param[in,out] state what the locator keeps between frames
/// @param points how far, points; positive scrolls down
void scroll(const Layout& layout, UiState& state, int32_t points) noexcept;

} // namespace oa::ui::folder_chooser
