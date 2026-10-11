// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The download check on the OA layer. Once a frame, tell_challenges reads the
// download queue and, on the main menu, shows one window for the item that
// is waiting on a check. OPEN THE CHECK opens the page. TRY AGAIN asks the
// queue to start that item again. CANCEL cancels that item and the same
// registry's items that are still waiting. A check that arrives during a
// game waits until the main menu. --check-challenge replaces the queue with
// the probe below.

#include "oa_layer.hpp"

#include "oa/app/runtime.hpp"
#include "oa/formats/url.hpp"
#include "oa/ui/engine_settings/challenge.hpp"
#include "oa/ui/kit/theme.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {

namespace {

namespace settings = oa::ui::engine_settings;
namespace kit = oa::ui::kit;
namespace content = oa::app::content;

/// The screen's name on the OA layer.
constexpr std::string_view challenge_screen_name = "challenge";

/// The queue --check-challenge installs in place of the download queue.
struct ChallengeProbe {
    uint64_t (*generation)(void* context) = nullptr;
    std::vector<content::DownloadView> (*items)(void* context) = nullptr;
    void (*cancel_item)(void* context, uint64_t item) = nullptr;
    void (*retry_item)(void* context, uint64_t item) = nullptr;
    void* context = nullptr;
    bool installed = false; ///< true while the check's queue stands in
};

/// The queue reading the window was last built from.
struct ChallengeHold {
    bool have_generation = false;              ///< generation has been read at least once
    uint64_t generation = 0;                   ///< the queue's generation at that reading
    std::vector<content::DownloadView> rows{}; ///< that reading of the queue
    uint64_t shown_item = 0;                   ///< the item the window is following; 0 for none
    bool dismissed = false;                    ///< CANCEL closed this item's current code
    uint64_t dismissed_item = 0;               ///< the item CANCEL closed
    std::string dismissed_code{};              ///< the code CANCEL closed
};

ChallengeProbe probe;
ChallengeHold hold;

/// The page address without its scheme and query: host, port when it is not
/// the scheme's default, and path.
///
/// @param page the page the queue was given
/// @return the address the window shows; empty when the page is not an address
std::string page_address(std::string_view page) {
    const std::optional<oa::formats::url::Url> parsed = oa::formats::url::parse_web_url(page);
    if (!parsed)
        return {};
    std::string path = parsed->target;
    const std::size_t query = path.find('?');
    if (query != std::string::npos)
        path.erase(query);
    std::string address = parsed->host;
    const bool default_port =
        (parsed->scheme == oa::formats::url::Scheme::http && parsed->port == 80) ||
        (parsed->scheme == oa::formats::url::Scheme::https && parsed->port == 443);
    if (!default_port) {
        address.push_back(':');
        address += std::to_string(parsed->port);
    }
    address += path;
    return address;
}

/// Returns the row of an item.
///
/// @param rows the queue
/// @param item the item
/// @return the row; null when the queue has none
const content::DownloadView* row_of(const std::vector<content::DownloadView>& rows, uint64_t item) {
    for (const content::DownloadView& row : rows)
        if (row.item == item)
            return &row;
    return nullptr;
}

/// Tells whether CANCEL closed this row's current code.
///
/// @param row the row
/// @return true when the window stays closed for it
bool suppressed(const content::DownloadView& row) {
    if (!hold.dismissed || row.item != hold.dismissed_item)
        return false;
    if (row.challenge)
        return row.challenge->code == hold.dismissed_code;
    return true;
}

/// Forgets a CANCEL once that item has moved on or its code has changed.
void release_dismissed() {
    if (!hold.dismissed)
        return;
    const content::DownloadView* row = row_of(hold.rows, hold.dismissed_item);
    const bool moved_on = row == nullptr || row->state == content::DownloadState::cancelled ||
                          row->state == content::DownloadState::downloading ||
                          row->state == content::DownloadState::done ||
                          (row->challenge && row->challenge->code != hold.dismissed_code);
    if (moved_on)
        hold.dismissed = false;
}

/// The item the window follows, and whether its check has run out.
struct ChallengeSubject {
    const content::DownloadView* row = nullptr; ///< the item; null when the window closes
    bool expired = false;                       ///< the check ran out
};

/// Returns the item the window follows.
///
/// A waiting check wins. Otherwise the item the window was following, once
/// its check has run out. With none followed, the newest check that ran out.
///
/// @return that item
ChallengeSubject challenge_subject() {
    const content::DownloadView* waiting = nullptr;
    for (const content::DownloadView& row : hold.rows) {
        if (row.state == content::DownloadState::challenge && row.challenge && !suppressed(row)) {
            waiting = &row;
            break;
        }
    }
    if (waiting != nullptr)
        return {waiting, false};

    const content::DownloadView* tracked =
        hold.shown_item == 0 ? nullptr : row_of(hold.rows, hold.shown_item);
    if (tracked != nullptr) {
        if (!suppressed(*tracked) && tracked->state == content::DownloadState::failed &&
            tracked->failure == content::DownloadFailure::challenge_expired)
            return {tracked, true};
        return {};
    }

    const content::DownloadView* newest_expired = nullptr;
    for (const content::DownloadView& row : hold.rows) {
        if (row.state == content::DownloadState::failed &&
            row.failure == content::DownloadFailure::challenge_expired && !suppressed(row))
            newest_expired = &row;
    }
    return {newest_expired, newest_expired != nullptr};
}

/// The check as a screen of the OA layer: modal over a backdrop, centred,
/// named challenge. OPEN THE CHECK, TRY AGAIN and CANCEL are the host's.
class ChallengeScreen final : public LayerScreen {
  public:

    /// What the window asks of the runtime.
    struct Answers {
        std::function<void()> open{};      ///< OPEN THE CHECK
        std::function<void()> try_again{}; ///< TRY AGAIN
        std::function<void()> cancel{};    ///< CANCEL and Escape
    };

    /// Makes the screen and remembers it as the one window.
    ///
    /// @param layer the layer it shows on
    /// @param answers what its buttons ask
    ChallengeScreen(OaLayer& layer, Answers answers) : layer_(layer), answers_(std::move(answers)) {
        live = this;
    }

    /// Forgets itself if it is still the one window.
    ~ChallengeScreen() override {
        if (live == this)
            live = nullptr;
    }

    /// Returns "challenge".
    ///
    /// @return the name
    [[nodiscard]] std::string_view name() const override { return challenge_screen_name; }

    /// Puts the latest check into the window. The focus stays where the
    /// player left it, unless the button it started on has changed.
    ///
    /// @param window the check
    /// @param verify_url the page OPEN THE CHECK opens
    void apply(settings::ChallengeWindow window, std::string verify_url) {
        const kit::ControlId before = settings::challenge_focus_control(model_.window);
        const bool same =
            model_.window.registry_name == window.registry_name &&
            model_.window.code == window.code && model_.window.address == window.address &&
            model_.window.status == window.status &&
            model_.window.can_open_browser == window.can_open_browser && verify_url_ == verify_url;
        model_.window = std::move(window);
        verify_url_ = std::move(verify_url);
        if (!model_.interaction.focus_shown ||
            settings::challenge_focus_control(model_.window) != before)
            settings::challenge_settle(model_);
        if (!same)
            ++revision_;
    }

    /// Returns the page OPEN THE CHECK opens.
    ///
    /// @return the page
    [[nodiscard]] std::string_view verify_url() const noexcept { return verify_url_; }

    /// Returns where the window shows: centred on the main menu, at the
    /// view's size class. Empty during a game, off the main menu, or before
    /// the fonts are ready.
    ///
    /// @param placed_on what the screen is placed on
    /// @return its place
    [[nodiscard]] LayerPlacement placement(const LayerView& placed_on) const override {
        if (placed_on.match || placed_on.game_screen != Screen::main_menu ||
            layer_.screen_fonts() == nullptr)
            return {};
        const kit::SizeClass size_class = placed_on.frame.size_class;
        return centred_placement(
            placed_on,
            settings::challenge_width(size_class),
            settings::challenge_height(model_.window, size_class, layer_.screen_fonts())
        );
    }

    /// Lays the window out at the view's size class.
    ///
    /// @param placed_on what the screen is placed on
    void lay_out(const LayerView& placed_on) override {
        const kit::SizeClass size_class =
            placed_on.match ? kit::SizeClass::compact : placed_on.frame.size_class;
        if (model_.size_class == size_class)
            return;
        model_.size_class = size_class;
        ++revision_;
    }

    /// Tells that the window takes every input.
    ///
    /// @return true
    [[nodiscard]] bool modal() const override { return true; }

    /// Tells that what lies under the window darkens.
    ///
    /// @return true
    [[nodiscard]] bool backdrop() const override { return true; }

    /// Draws the window.
    ///
    /// @param canvas where it draws
    void draw(const kit::Canvas& canvas) const override {
        settings::draw_challenge(canvas, model_);
    }

    /// Takes a pointer's move, press or release. A finger's press takes the
    /// nearer button within reach.
    ///
    /// @param kind pointer_move, pointer_down or pointer_up
    /// @param button the pointer's button; 1 is the left
    /// @param at the pointer, in the window's points
    /// @param finger_reach a finger's reach, in the window's points; 0 for a mouse
    /// @return what the window did
    LayerAnswer
    pointer(ScreenInputKind kind, uint8_t button, kit::Point at, int32_t finger_reach) override {
        const kit::Fonts* fonts = layer_.screen_fonts();
        settings::ChallengeAction action = settings::ChallengeAction::none;
        switch (kind) {
        case ScreenInputKind::pointer_move:
            action = settings::challenge_pointer_move(model_, at, fonts);
            break;
        case ScreenInputKind::pointer_down:
            if (button == 1) {
                action = finger_reach > 0
                             ? settings::challenge_finger_down(model_, at, finger_reach, fonts)
                             : settings::challenge_pointer_down(model_, at, fonts);
            }
            break;
        case ScreenInputKind::pointer_up:
            if (button == 1)
                action = settings::challenge_pointer_up(model_, at, fonts);
            break;
        default:
            break;
        }
        return take(action);
    }

    /// Takes a key. Enter presses the focused button. Escape cancels.
    ///
    /// @param pressed the key
    /// @return what the window did
    LayerAnswer key(kit::Key pressed, uint32_t /*sdl_key*/) override {
        return take(settings::challenge_key(model_, pressed, layer_.screen_fonts()));
    }

    /// Takes a turn of the wheel, which does nothing under the window.
    ///
    /// @return none
    LayerAnswer wheel(kit::Point /*at*/, float /*notches*/) override { return LayerAnswer::none; }

    /// Returns the window's display list.
    ///
    /// @return the list
    [[nodiscard]] kit::DisplayList display_list() const override {
        return settings::challenge_list(model_, layer_.screen_fonts());
    }

    /// Returns the window's hover, press and focus.
    ///
    /// @return the interaction
    [[nodiscard]] kit::Interaction interaction() const override { return model_.interaction; }

    /// Has nothing to advance: the host refreshes it from the queue.
    ///
    /// @return none
    LayerAnswer tick() override { return LayerAnswer::none; }

    /// Returns the window's revision.
    ///
    /// @return the revision
    [[nodiscard]] uint64_t revision() const override { return revision_; }

    /// Leaves the queue's items as they are. CANCEL runs before the layer
    /// takes the window off; closing it for any other reason cancels nothing.
    void close(bool /*by_key*/) override {}

    /// The window that is showing or waiting, or null.
    static ChallengeScreen* live;

  private:

    /// Carries out what a press asked.
    ///
    /// @param action what the press asked
    /// @return what the screen answers
    LayerAnswer take(settings::ChallengeAction action) {
        switch (action) {
        case settings::ChallengeAction::none:
            return LayerAnswer::none;
        case settings::ChallengeAction::redraw:
            ++revision_;
            return LayerAnswer::redraw;
        case settings::ChallengeAction::open:
            if (answers_.open)
                answers_.open();
            return LayerAnswer::none;
        case settings::ChallengeAction::try_again:
            if (answers_.try_again)
                answers_.try_again();
            return LayerAnswer::none;
        case settings::ChallengeAction::cancel:
            if (answers_.cancel)
                answers_.cancel();
            return LayerAnswer::close;
        }
        return LayerAnswer::none;
    }

    OaLayer& layer_;                 ///< the layer it shows on
    Answers answers_;                ///< what its buttons ask
    settings::ChallengeModel model_; ///< the check and the pointer's place
    std::string verify_url_{};       ///< the page OPEN THE CHECK opens
    uint64_t revision_{};            ///< counts the changes to what it shows
};

ChallengeScreen* ChallengeScreen::live = nullptr;

/// Tells whether a notice or a question is the screen the player is looking at.
///
/// @param layer the OA layer
/// @return true when the challenge waits for that screen to close
bool notice_or_question_showing(OaLayer& layer) {
    LayerScreen* top = layer.top();
    if (top == nullptr)
        return false;
    return dynamic_cast<NoticeScreen*>(top) != nullptr ||
           dynamic_cast<QuestionScreen*>(top) != nullptr;
}

} // namespace

void Runtime::set_challenge_probe(
    uint64_t (*generation)(void* context),
    std::vector<content::DownloadView> (*items)(void* context),
    void (*cancel_item)(void* context, uint64_t item),
    void (*retry_item)(void* context, uint64_t item),
    void* context
) {
    probe.generation = generation;
    probe.items = items;
    probe.cancel_item = cancel_item;
    probe.retry_item = retry_item;
    probe.context = context;
    probe.installed = generation != nullptr && items != nullptr;
}

void Runtime::tell_challenges() {
    if (probe.installed) {
        const uint64_t generation = probe.generation(probe.context);
        if (!hold.have_generation || hold.generation != generation) {
            hold.have_generation = true;
            hold.generation = generation;
            hold.rows = probe.items(probe.context);
        }
    } else if (!content_) {
        return;
    } else {
        content::Downloads& downloads = content_downloads();
        const uint64_t generation = downloads.generation();
        if (!hold.have_generation || hold.generation != generation) {
            hold.have_generation = true;
            hold.generation = generation;
            hold.rows = downloads.view();
        }
    }

    release_dismissed();
    const ChallengeSubject subject = challenge_subject();
    if (subject.row != nullptr)
        hold.shown_item = subject.row->item;
    else if (screen_ == Screen::main_menu)
        hold.shown_item = 0;

    // A game, and every screen but the main menu, shows nothing. Closing
    // here is not a CANCEL: the same item and code open again on the menu.
    if (screen_ != Screen::main_menu) {
        if (ChallengeScreen::live != nullptr)
            oa_layer().close(ChallengeScreen::live);
        return;
    }
    if (subject.row == nullptr) {
        if (ChallengeScreen::live != nullptr)
            oa_layer().close(ChallengeScreen::live);
        return;
    }

    const content::DownloadView& row = *subject.row;
    std::string verify_url;
    std::string code;
    if (row.challenge) {
        verify_url = row.challenge->verify_url;
        code = row.challenge->code;
    }
    settings::ChallengeWindow window;
    window.registry_name = row.target.registry_name;
    window.code = std::move(code);
    window.address = page_address(verify_url);
    window.status = subject.expired ? settings::ChallengeWindow::Status::expired
                                    : settings::ChallengeWindow::Status::waiting;
    window.can_open_browser = web_address_available();

    if (ChallengeScreen::live == nullptr) {
        ChallengeScreen::Answers answers;
        answers.open = [this] {
            if (ChallengeScreen::live != nullptr)
                open_web_link(ChallengeScreen::live->verify_url());
        };
        answers.try_again = [this] {
            const uint64_t item = hold.shown_item;
            if (probe.installed) {
                if (probe.retry_item != nullptr)
                    probe.retry_item(probe.context, item);
                return;
            }
            if (content_)
                content_downloads().retry(item);
        };
        answers.cancel = [this] {
            const uint64_t item = hold.shown_item;
            std::string registry;
            std::string code;
            const content::DownloadView* challenged = row_of(hold.rows, item);
            if (challenged != nullptr) {
                registry = challenged->target.registry;
                if (challenged->challenge)
                    code = challenged->challenge->code;
            }
            std::vector<uint64_t> waiting_same;
            for (const content::DownloadView& queued : hold.rows) {
                if (queued.item != item && queued.target.registry == registry &&
                    queued.state == content::DownloadState::waiting)
                    waiting_same.push_back(queued.item);
            }
            const auto cancel_item = [this](uint64_t id) {
                if (probe.installed) {
                    if (probe.cancel_item != nullptr)
                        probe.cancel_item(probe.context, id);
                    return;
                }
                if (content_)
                    content_downloads().cancel(id);
            };
            cancel_item(item);
            for (const uint64_t waiting : waiting_same)
                cancel_item(waiting);
            hold.dismissed = true;
            hold.dismissed_item = item;
            hold.dismissed_code = std::move(code);
        };
        auto screen = std::make_unique<ChallengeScreen>(oa_layer(), std::move(answers));
        screen->apply(std::move(window), std::move(verify_url));
        if (notice_or_question_showing(oa_layer()))
            oa_layer().show_when_free(std::move(screen));
        else
            oa_layer().push(std::move(screen));
        return;
    }
    ChallengeScreen::live->apply(std::move(window), std::move(verify_url));
}

} // namespace oa::app
