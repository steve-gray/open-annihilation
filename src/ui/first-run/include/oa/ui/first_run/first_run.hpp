// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The first-run setup: whether it shows, and the three steps a player walks
// (language, online features, the OA buttons). The host fills the model and
// applies what the player asks. This module reads no preference, clock or
// network.

#pragma once

#include "oa/ui/kit/first_run.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/library/library.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::first_run {

/// The preference that records a finished setup. Absent, or any other word, means it
/// has not been finished.
inline constexpr std::string_view preference_key = "open-annihilation.first-run";

/// The value a finished setup writes. Show again erases the key.
inline constexpr std::string_view preference_done = "done";

/// Why the setup stays hidden, or that it shows.
enum class Hold : uint8_t {
    show,             ///< the setup shows
    done,             ///< the preference is done
    headless_check,   ///< a headless check
    preferences_file, ///< the run named a preferences file
    benchmark,        ///< a benchmark
    stage,            ///< a stage run
    fark,             ///< a FARK run
    unattended,       ///< nobody is watching
};

/// The facts the gate reads. The host fills them from the command line and the preference.
struct GateFacts {
    bool force{};            ///< --first-run
    bool done{};             ///< the preference is done
    bool headless_check{};   ///< a headless check
    bool preferences_file{}; ///< --preferences-file
    bool benchmark{};        ///< a benchmark
    bool stage{};            ///< a stage file
    bool fark{};             ///< --fark
    bool unattended{};       ///< an unattended run, or a run nobody watches
};

/// Decides whether the setup shows.
///
/// --first-run shows it even when it is done and even in an automated run.
/// Otherwise a finished setup stays hidden, then a headless check, a preferences
/// file, a benchmark, a stage run, a FARK run and an unattended run, in that order.
///
/// @param facts the command line and the preference
/// @return show, or the first reason it stays hidden
[[nodiscard]] Hold decide(const GateFacts& facts) noexcept;

/// The words a log uses after "not showing: ". Empty when the setup shows.
///
/// @param hold what decide returned
/// @return the reason; empty for show
[[nodiscard]] std::string_view hold_reason(Hold hold) noexcept;

/// One step of the setup.
enum class Step : uint8_t {
    language, ///< welcome and the language carousel
    online,   ///< online features
    buttons,  ///< the OA buttons over the main menu
};

/// The word a stage line and a log use for a step: language, online or buttons.
///
/// @param step the step
/// @return the word
[[nodiscard]] std::string_view step_token(Step step) noexcept;

/// Reads a stage line's step word.
///
/// @param token language, online or buttons
/// @return the step; nothing for any other word
[[nodiscard]] std::optional<Step> step_named(std::string_view token) noexcept;

/// Where a language card's language comes from.
enum class CardKind : uint8_t {
    built_in,  ///< a language the game ships
    installed, ///< a language pack already installed
    pack,      ///< a language pack the catalogue lists
};

/// What a catalogue pack's card is doing.
enum class PackState : uint8_t {
    ready,       ///< it can be downloaded
    downloading, ///< bytes are arriving
    checking,    ///< the signature is being checked
    failed,      ///< the download did not finish
    offline,     ///< the catalogue cannot be read, or downloads are off
};

/// Why catalogue packs are not offered as downloads.
enum class CatalogueNote : uint8_t {
    none,          ///< packs that are listed can be downloaded
    unread,        ///< the catalogue could not be read
    downloads_off, ///< downloads are off
};

/// One language card.
struct Card {
    std::string tag{};                 ///< the language's tag
    std::string own_name{};            ///< its name in itself
    std::string english_name{};        ///< its name in English
    CardKind kind{CardKind::built_in}; ///< where it comes from
    PackState pack{PackState::ready};  ///< a pack's progress; ignored for the others
    bool current{};                    ///< the language the game shows
    std::string size_text{};           ///< a pack's size, as the player reads it
    std::string progress_done_text{};  ///< how much has arrived
    std::string progress_size_text{};  ///< how much the pack is
    int32_t progress_done{};           ///< the bar's filled part
    int32_t progress_whole{};          ///< the bar's whole; 0 fills nothing
    std::string failure{};             ///< why a download failed, already looked up
};

/// What the three steps show. The host keeps it current.
struct Model {
    Step step{Step::language};                    ///< the step on screen
    std::vector<Card> cards{};                    ///< the carousel, built-in languages then packs
    bool online_on{true};                         ///< Turn on online features is chosen
    CatalogueNote catalogue{CatalogueNote::none}; ///< why packs stay offline
    kit::Rect library_button{};                   ///< the Library button, in the screen's points
    kit::Rect settings_button{};                  ///< the Settings button, in the screen's points
    int32_t library_count{};                      ///< the number the Library button shows
    std::string sample_own{};                     ///< the sample Library's language, in itself
    std::string sample_english{};                 ///< that language's English name
    /// How the sample Library looks its words up. The same hook the live
    /// Library uses: each pattern from the Library's texts, whole, then filled.
    /// Empty shows those words in English.
    library::TextHooks sample_text{};
};

/// The setup's controls and motion between layouts.
struct Session {
    Model model{};                  ///< what the steps show
    kit::CarouselModel carousel{};  ///< which card is selected, and the move
    kit::CarouselDrag drag{};       ///< a press on the carousel
    kit::Interaction interaction{}; ///< the pointer and the focus
    uint32_t pulse_ms{};            ///< how long Start playing has been pulsing
    bool focus_ready{};             ///< the focus has been placed for this step
};

/// What the player asked the host to do.
enum class Ask : uint8_t {
    none,            ///< the picture may have changed; nothing was asked
    skip,            ///< Skip setup
    back,            ///< Back
    advance,         ///< Next, or Start playing
    pick,            ///< the selected language card
    cancel_download, ///< Cancel download
    retry_download,  ///< Retry
    choose_on,       ///< Turn on online features
    choose_off,      ///< Not now
};

/// What a key, a pointer or a tick did.
struct Outcome {
    Ask ask{Ask::none}; ///< what the host should do
    int32_t index{-1};  ///< the card, when ask is pick
    bool redraw{};      ///< what the screen draws changed
    bool pass{};        ///< the screen did not take it
};

/// A laid-out step.
struct Scene {
    int32_t width{};                         ///< the window's width, in points
    int32_t height{};                        ///< the window's height, in points
    kit::DisplayList list{};                 ///< what is drawn, and the controls
    kit::Rect well_inset{};                  ///< where the sample Library is scaled; empty for none
    bool sample{};                           ///< the well draws the sample Library
    kit::Rect carousel{};                    ///< the carousel's band and pager
    kit::Rect previous{};                    ///< the previous arrow
    kit::Rect next_arrow{};                  ///< the next arrow
    std::vector<kit::CarouselSlot> places{}; ///< the cards that show
    kit::Rect coach_library{};               ///< the Library coach card
    kit::Rect coach_settings{};              ///< the Settings coach card
    kit::Rect title{};        ///< the large centred title; empty when the step has none
    std::string title_text{}; ///< the title's words
    /// the class the sample Library is laid out at
    kit::SizeClass sample_class{kit::SizeClass::compact};
};

/// Skip setup.
inline constexpr kit::ControlId skip_id = 1;
/// Back.
inline constexpr kit::ControlId back_id = 2;
/// Next.
inline constexpr kit::ControlId next_id = 3;
/// The carousel's previous arrow.
inline constexpr kit::ControlId previous_id = 4;
/// The carousel's next arrow.
inline constexpr kit::ControlId next_card_id = 5;
/// Cancel download.
inline constexpr kit::ControlId cancel_id = 6;
/// Retry.
inline constexpr kit::ControlId retry_id = 7;
/// Turn on online features.
inline constexpr kit::ControlId on_id = 8;
/// Not now.
inline constexpr kit::ControlId off_id = 9;
/// Start playing.
inline constexpr kit::ControlId start_id = 10;
/// The first language card. Card n is card_id + n.
inline constexpr kit::ControlId card_id = 20;

/// Tells whether a download is running, so Next stays disabled.
///
/// @param model the model
/// @return true while a pack is downloading or being checked
[[nodiscard]] bool download_busy(const Model& model) noexcept;

/// Holds the carousel's count and selection inside the cards.
///
/// @param[in,out] session the session
void bind(Session& session) noexcept;

/// Lays one step out.
///
/// @param session the model and the carousel
/// @param frame the room, its class and its scale
/// @param fonts the fonts texts are measured in; null estimates them
/// @return the scene
[[nodiscard]] Scene
lay_out(const Session& session, const kit::Frame& frame, const kit::Fonts* fonts);

/// Advances the carousel's move and Start playing's pulse.
///
/// @param[in,out] session the session
/// @param dt_ms how much time passed, in milliseconds
void tick(Session& session, uint32_t dt_ms) noexcept;

/// Places the focus on the step's first control, once per step.
///
/// @param[in,out] session the session
/// @param list the scene's controls
void ensure_focus(Session& session, const kit::DisplayList& list);

/// Where a finger is.
enum class PointerKind : uint8_t {
    move, ///< the pointer moved
    down, ///< a press went down
    up,   ///< a press came up
};

/// Takes a pointer.
///
/// @param[in,out] session the session
/// @param scene the layout the press lands on
/// @param kind where the finger is
/// @param at the finger, in the screen's points
/// @param finger_reach how far a finger may lie from a control; 0 for a mouse
/// @return what the setup did
[[nodiscard]] Outcome pointer(
    Session& session, const Scene& scene, PointerKind kind, kit::Point at, int32_t finger_reach
);

/// Takes a key. Escape does nothing on the language step and goes back on the others.
///
/// @param[in,out] session the session
/// @param scene the layout
/// @param pressed the key
/// @return what the setup did
[[nodiscard]] Outcome key(Session& session, const Scene& scene, kit::Key pressed);

/// Words the sample Library looks up through the model's text hook: the Active
/// and Mods tabs, PLAYING, UNLOAD MOD and Built in, one a line.
///
/// The picture well draws the same Library. A hook that marks each text
/// changes these words; an empty hook leaves them in English.
///
/// @param model the sample's language and its text hook
/// @return the looked-up words
[[nodiscard]] std::string sample_words(const Model& model);

/// Draws a laid-out step. On the buttons step the dim leaves both buttons clear.
/// The sample Library is drawn into the picture well when the scene asks.
///
/// @param canvas where it is drawn
/// @param scene the layout
/// @param session the focus, the pulse and the sample's language
void draw(const kit::Canvas& canvas, const Scene& scene, const Session& session);

/// A coach card's width at a size class: 220, 260 or 300 points.
///
/// @param size_class the class
/// @return the width, in points
[[nodiscard]] int32_t coach_card_width(kit::SizeClass size_class) noexcept;

/// Where the two coach cards sit.
struct CoachCards {
    kit::Rect library{};  ///< the Library card
    kit::Rect settings{}; ///< the Settings card
};

/// Places the two coach cards so neither covers a button, the other card, or `blocked`.
///
/// Each card is tried on the right, then the left, then below, then above. The
/// Library card is placed first.
///
/// @param room the rectangle both cards must lie in
/// @param blocked a rectangle neither card may cover, such as the left panel; empty for none
/// @param library_button the Library button
/// @param settings_button the Settings button
/// @param card_width each card's width, in points
/// @param library_height the Library card's height, in points
/// @param settings_height the Settings card's height, in points
/// @return the two cards
[[nodiscard]] CoachCards place_coaches(
    const kit::Rect& room,
    const kit::Rect& blocked,
    const kit::Rect& library_button,
    const kit::Rect& settings_button,
    int32_t card_width,
    int32_t library_height,
    int32_t settings_height
) noexcept;

} // namespace oa::ui::first_run
