// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Every word the Library shows, in English, and the functions that write its
// sizes, ages, versions, rules hashes and engine requirements. A pattern's
// places are written {name}; a translation keeps them and may move them.
// The texts are looked up whole through TextHooks before their places are
// filled, so a translator finds every one of them here.
#pragma once

#include "oa/base/sha256.hpp"
#include "oa/formats/oamod/package_keys.hpp"
#include "oa/ui/library/library.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace oa::ui::library {

namespace words {

// Tabs and filters.
inline constexpr std::string_view tab_mods = "Mods";
inline constexpr std::string_view tab_maps = "Maps";
inline constexpr std::string_view tab_languages = "Languages";
inline constexpr std::string_view tab_languages_short = "Lang";
inline constexpr std::string_view tab_updates = "Updates";
inline constexpr std::string_view tab_updates_short = "Upd";
inline constexpr std::string_view filter_all = "All";
inline constexpr std::string_view filter_installed = "Installed";
inline constexpr std::string_view filter_updates = "Updates";

// A row's state.
inline constexpr std::string_view state_get = "GET";
inline constexpr std::string_view state_installed = "INSTALLED";
inline constexpr std::string_view state_update = "UPDATE";
inline constexpr std::string_view state_playing = "PLAYING";

// Buttons.
inline constexpr std::string_view action_get = "GET";
inline constexpr std::string_view action_update = "UPDATE";
inline constexpr std::string_view action_update_all = "UPDATE ALL";
inline constexpr std::string_view action_cancel = "CANCEL";
inline constexpr std::string_view action_retry = "RETRY";
inline constexpr std::string_view action_roll_back = "ROLL BACK";
inline constexpr std::string_view action_play_now = "PLAY NOW";
inline constexpr std::string_view action_open_folder = "OPEN FOLDER";
inline constexpr std::string_view action_homepage = "HOMEPAGE";
inline constexpr std::string_view action_details = "DETAILS";
inline constexpr std::string_view action_back = "BACK";
inline constexpr std::string_view action_settings = "SETTINGS…";
inline constexpr std::string_view action_close = "CLOSE";

// Why GET or UPDATE is off.
inline constexpr std::string_view blocked_in_match = "Not during a game";
inline constexpr std::string_view blocked_downloads_off =
    "Downloads from {registry} are off (Settings › Downloads)";
inline constexpr std::string_view blocked_engine = "Needs Open Annihilation {range}";
inline constexpr std::string_view blocked_base = "Needs the base game {base}";
inline constexpr std::string_view blocked_hacks = "Needs hacks this OA lacks: {hacks}";
/// The missing hacks named, then how many more are not.
inline constexpr std::string_view hacks_and_more = "{hacks} · {count} more";
/// Between two names of a list.
inline constexpr std::string_view list_separator = ", ";

// Sizes, binary units.
/// A number with one decimal.
inline constexpr std::string_view one_decimal = "{whole}.{tenth}";
inline constexpr std::string_view size_kb = "{size} KB";
inline constexpr std::string_view size_mb = "{size} MB";
inline constexpr std::string_view size_gb = "{size} GB";
/// A download's progress: the bytes so far as a number, the whole size with its unit.
inline constexpr std::string_view progress = "{done} of {total}";

// Ages.
inline constexpr std::string_view age_just_now = "just now";
inline constexpr std::string_view age_minutes = "{count} min ago";
inline constexpr std::string_view age_hours = "{count} h ago";
inline constexpr std::string_view age_days = "{count} days ago";
inline constexpr std::string_view header_list_from = "list from {age}";
inline constexpr std::string_view header_no_list = "no list yet";
inline constexpr std::string_view header_offline = "{list} · offline";

// Versions.
inline constexpr std::string_view version_with_revision = "{version} · rev {revision}";
inline constexpr std::string_view version_with_maps = "{version} · {maps}";
inline constexpr std::string_view revision_long = "revision {revision}";
inline constexpr std::string_view revision_short = "rev {revision}";
inline constexpr std::string_view map_count_one = "{count} map";
inline constexpr std::string_view map_count = "{count} maps";

// Engine requirements, one comparison each, joined with list_separator.
inline constexpr std::string_view engine_at_least = "{version} or later";
inline constexpr std::string_view engine_above = "later than {version}";
inline constexpr std::string_view engine_at_most = "{version} or earlier";
inline constexpr std::string_view engine_below = "before {version}";
inline constexpr std::string_view engine_exactly = "exactly {version}";
inline constexpr std::string_view engine_at_least_short = "{version}+";
inline constexpr std::string_view engine_above_short = "after {version}";
inline constexpr std::string_view engine_at_most_short = "up to {version}";
inline constexpr std::string_view engine_below_short = "before {version}";
inline constexpr std::string_view engine_exactly_short = "{version} only";

// The facts of an entry's details: labels, then values.
inline constexpr std::string_view fact_size = "Size";
inline constexpr std::string_view fact_base = "Base";
inline constexpr std::string_view fact_needs = "Needs";
inline constexpr std::string_view fact_hacks = "Hacks";
inline constexpr std::string_view fact_rules = "Rules";
inline constexpr std::string_view fact_maps = "Maps";
inline constexpr std::string_view fact_coverage = "Coverage";
inline constexpr std::string_view fact_from = "From";
inline constexpr std::string_view size_with_download = "{size} · {download} to download";
inline constexpr std::string_view size_download_short = "{download} to download";
inline constexpr std::string_view base_supported = "Total Annihilation 3.1c";
inline constexpr std::string_view base_supported_short = "TA 3.1c";
inline constexpr std::string_view needs_engine = "Open Annihilation {range}";
inline constexpr std::string_view needs_engine_short = "OA {range}";
inline constexpr std::string_view hacks_counts = "{game} change the game · {view} view only";
inline constexpr std::string_view hacks_counts_short = "{game} game · {view} view";
inline constexpr std::string_view hacks_none = "none";
/// The hack counts, then the missing hacks in blocked_hacks' words.
inline constexpr std::string_view hacks_with_missing = "{counts} · {missing}";
inline constexpr std::string_view hacks_with_missing_short = "{counts} · {count} missing";
inline constexpr std::string_view rules_same = "{from} → {to} same";
inline constexpr std::string_view rules_same_short = "unchanged";
inline constexpr std::string_view rules_changes =
    "{from} → {to} · changes the game's rules; saved games from {old} won't load under {new}";
inline constexpr std::string_view rules_changes_short = "changes · {old} saves won't load";
inline constexpr std::string_view rules_unknown =
    "unknown · saved games from {old} may not load under {new}";
inline constexpr std::string_view rules_unknown_short = "unknown · saves may not load";
inline constexpr std::string_view coverage_percent = "{percent}%";
inline constexpr std::string_view not_reviewed = "Not reviewed by the OA team";
inline constexpr std::string_view from_not_reviewed = "{registry} · Not reviewed by the OA team";
inline constexpr std::string_view from_own = "Your own copy";

// The line under an entry's name.
inline constexpr std::string_view status_revision_available =
    "Revision {revision} available · {size} to download";
inline constexpr std::string_view status_version_available =
    "Version {version} available · {size} to download";
inline constexpr std::string_view status_installed = "Installed";
inline constexpr std::string_view status_playing = "Playing";
inline constexpr std::string_view status_not_installed = "Not installed · {size}";
inline constexpr std::string_view status_waiting = "Waiting to download";
inline constexpr std::string_view status_downloading = "Downloading · {progress}";
inline constexpr std::string_view status_downloading_unsized = "Downloading";
inline constexpr std::string_view status_verifying = "Verifying";
inline constexpr std::string_view status_checking = "Checking";
inline constexpr std::string_view status_installing = "Installing";
inline constexpr std::string_view status_failed = "Failed";
inline constexpr std::string_view byline = "by {author}";

// The note on updating while a mod is played.
inline constexpr std::string_view note_finishes = "Finishes when you leave {name}.";
inline constexpr std::string_view note_playing_other =
    "You are playing {playing}. Updating {name} doesn't restart the game.";

// The download queue in the footer.
inline constexpr std::string_view queue_package = "{name} {version}";
inline constexpr std::string_view queue_downloading = "Downloading {package} · {progress}";
inline constexpr std::string_view queue_downloading_unsized = "Downloading {package}";
inline constexpr std::string_view queue_waiting = "Waiting to download {package}";
inline constexpr std::string_view queue_verifying = "Verifying {name}";
inline constexpr std::string_view queue_checking = "Checking {name}";
inline constexpr std::string_view queue_installing = "Installing {name}";
inline constexpr std::string_view queue_held = "Waiting · finishes when you leave {name}";
inline constexpr std::string_view queue_installed = "Installed {name}";
inline constexpr std::string_view queue_updated_changes =
    "Updated {name} · {new} changes the game's rules · ROLL BACK restores {old}";
inline constexpr std::string_view queue_updated_unknown =
    "Updated {name} · {new} may change the game's rules · ROLL BACK restores {old}";
inline constexpr std::string_view queue_failed_retry = "{problem} · RETRY";
inline constexpr std::string_view queue_then = "Then {package} · {phase}";
inline constexpr std::string_view queue_more = "{line} · {count} more";
inline constexpr std::string_view queue_percent = "{name} {percent}%";
inline constexpr std::string_view queue_rules_changed = "{name} · rules changed";
inline constexpr std::string_view queue_phase = "{name} · {phase}";
inline constexpr std::string_view phase_waiting = "waiting";
inline constexpr std::string_view phase_downloading = "downloading";
inline constexpr std::string_view phase_verifying = "verifying";
inline constexpr std::string_view phase_checking = "checking";
inline constexpr std::string_view phase_installing = "installing";
inline constexpr std::string_view phase_held = "after you leave it";
inline constexpr std::string_view phase_failed = "failed";
inline constexpr std::string_view phase_installed = "installed";

// The screens (screen.hpp): the header.
/// The title at Compact.
inline constexpr std::string_view title_library = "Library";
/// The title's first words at Regular and Large; title_library follows them.
inline constexpr std::string_view title_open_annihilation = "Open Annihilation";
/// The header's right at Regular and Large: this build's version, then header_age_text.
inline constexpr std::string_view header_version_list = "{version} · {list}";

// The search and the filters.
inline constexpr std::string_view search_mods = "Search mods, authors, tags";
inline constexpr std::string_view search_maps = "Search maps, authors, tags";
inline constexpr std::string_view search_languages = "Search languages";
inline constexpr std::string_view search_updates = "Search updates";
/// A tag in Compact's filter drop-down.
inline constexpr std::string_view filter_tag = "Tag: {tag} ({count})";
/// A tag in the filter pane, with how many entries have it.
inline constexpr std::string_view tag_with_count = "{tag} ({count})";
/// The drop-down holding the tags that do not fit, and the filter pane's heading over them.
inline constexpr std::string_view tags_menu = "TAGS";
/// The Compact details page's drop-down holding the actions after the first three.
inline constexpr std::string_view more_menu = "MORE";
/// How many updates the Updates tab shows.
inline constexpr std::string_view update_count_one = "{count} update";
inline constexpr std::string_view update_count_many = "{count} updates";

// A row: the version and size under the name at Compact.
inline constexpr std::string_view row_version_size = "{version} · {size}";
/// The line above, from a registry the player added.
inline constexpr std::string_view row_from_registry = "{line} · {registry}";

// An empty list, and a list the last fetch could not bring up to date.
inline constexpr std::string_view empty_matches = "Nothing matches \"{query}\".";
inline constexpr std::string_view empty_updates = "No updates.";
inline constexpr std::string_view empty_no_registries =
    "Nothing here yet. Turn a registry on in Settings › Downloads.";
inline constexpr std::string_view empty_tab = "Nothing to show.";
inline constexpr std::string_view offline_list =
    "Showing the list from {age}. OA couldn't reach the registries.";

// The details.
/// The Compact details page's way back to the list, with the tab's name.
inline constexpr std::string_view back_to_tab = "‹ {tab}";
/// After a fact's value that is met.
inline constexpr std::string_view fact_met = "✓";

} // namespace words

/// One place of a pattern and its value.
using Place = std::pair<std::string_view, std::string_view>;

/// Looks a text up in the language shown.
///
/// @param text how texts are looked up
/// @param english the text, in English as text.hpp writes it
/// @return the text to show; the English when the hooks give no lookup
[[nodiscard]] std::string_view shown(const TextHooks& text, std::string_view english);

/// Looks a pattern up whole and fills its places.
///
/// A place no value names stays as it is written. Values are not looked up.
///
/// @param text how texts are looked up
/// @param pattern the pattern, in English, with places written {name}
/// @param places each place's name and value
/// @return the text
[[nodiscard]] std::string
filled(const TextHooks& text, std::string_view pattern, std::initializer_list<Place> places);

/// Writes a size in binary units.
///
/// Below 1 MiB in whole KB, at least 1; below 100 MiB in MB to one decimal;
/// below 1 GiB in whole MB; above in GB to one decimal. Halves round up:
/// 43,011,223 bytes is "41.0 MB", and 197,132,288 is "188 MB".
///
/// @param bytes the size
/// @param text how texts are looked up
/// @return the size
[[nodiscard]] std::string size_text(uint64_t bytes, const TextHooks& text = {});

/// Writes a download's progress, both numbers in the unit and precision of the whole size.
///
/// @param done the bytes downloaded
/// @param total the whole size, in bytes
/// @param text how texts are looked up
/// @return the progress, such as "18.2 of 64.0 MB"
[[nodiscard]] std::string progress_text(uint64_t done, uint64_t total, const TextHooks& text = {});

/// Writes how long ago something happened.
///
/// Below a minute, or a negative age, "just now"; below an hour in whole
/// minutes; below 48 hours in whole hours; else in whole days.
///
/// @param seconds the age
/// @param text how texts are looked up
/// @return the age, such as "4 min ago"
[[nodiscard]] std::string age_text(int64_t seconds, const TextHooks& text = {});

/// Writes the header's note on the package list: how old it is, and whether the last fetch failed.
///
/// @param inputs the inputs
/// @param text how texts are looked up
/// @return "list from 4 min ago", "no list yet", or either with " · offline"
[[nodiscard]] std::string header_age_text(const Inputs& inputs, const TextHooks& text = {});

/// Writes a rules hash's first four bytes as two groups of hexadecimal digits.
///
/// @param digest the hash
/// @return the text, such as "9c1f 4e0b"
[[nodiscard]] std::string rules_text(const base::sha256::Digest& digest);

/// Writes a listing's version, its revision above 1, and a map pack's number of maps.
///
/// @param listing the listing
/// @param text how texts are looked up
/// @return the text, such as "4.8 · rev 3" or "1.2 · 2 maps"
[[nodiscard]] std::string version_text(const Listing& listing, const TextHooks& text = {});

/// Writes an installed package's version and its revision above 1.
///
/// @param installed the package
/// @param text how texts are looked up
/// @return the text, such as "4.8 · rev 3"
[[nodiscard]] std::string version_text(const Installed& installed, const TextHooks& text = {});

/// What an update changes from and to, as the Rules fact, the queue and the update question name them.
struct ChangeTexts {
    std::string from{};       ///< "revision 3", or the version "4.8"
    std::string to{};         ///< "revision 4", or the version "4.9"
    std::string from_short{}; ///< "rev 3", or the version "4.8"
    std::string to_short{};   ///< "rev 4", or the version "4.9"
};

/// Names both sides of an update: by revision when it keeps the version
/// text, else by version.
///
/// @param installed the installed package
/// @param update its update
/// @param text how texts are looked up
/// @return the names
[[nodiscard]] ChangeTexts
change_texts(const Installed& installed, const UpdateNote& update, const TextHooks& text = {});

/// Describes an engine requirement in words.
///
/// @param range the requirement
/// @param text how texts are looked up
/// @return the words, such as "0.8.0 or later"
[[nodiscard]] std::string
engine_text(const formats::oamod::EngineRange& range, const TextHooks& text = {});

/// Describes an engine requirement in a few characters.
///
/// @param range the requirement
/// @param text how texts are looked up
/// @return the words, such as "0.8.0+"
[[nodiscard]] std::string
engine_short_text(const formats::oamod::EngineRange& range, const TextHooks& text = {});

/// Names a tab.
///
/// @param tab the tab
/// @param compact the short name, for a small screen
/// @param text how texts are looked up
/// @return the name
[[nodiscard]] std::string_view tab_text(Tab tab, bool compact, const TextHooks& text = {});

/// Names a filter.
///
/// @param filter the filter
/// @param text how texts are looked up
/// @return the name
[[nodiscard]] std::string_view filter_text(Filter filter, const TextHooks& text = {});

/// Names a row's state, as its chip shows it.
///
/// @param state the state
/// @param text how texts are looked up
/// @return the name
[[nodiscard]] std::string_view state_text(State state, const TextHooks& text = {});

/// Names a button.
///
/// @param action what the button asks for
/// @param text how texts are looked up
/// @return the button's words
[[nodiscard]] std::string_view action_text(Action action, const TextHooks& text = {});

} // namespace oa::ui::library
