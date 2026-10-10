// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's model: one list of every package the player can get or has
// installed, built from plain inputs, with its tabs, filters, search,
// selection, details and download queue lines. It draws nothing and reads no
// file, clock or network; the app fills the inputs and the screens show the
// result. Its words are in text.hpp.
#pragma once

#include "oa/base/sha256.hpp"
#include "oa/formats/oamod/package_keys.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::library {

/// What kind of package an entry is.
enum class Kind : uint8_t {
    mod,      ///< an .oamod
    map_pack, ///< an .oamap
    language, ///< an .oalang
};

/// A registry the Library lists packages from.
///
/// `built_in` is true when the registry ships with Open Annihilation (its
/// descriptor is in the package's registries folder). It is the only thing
/// the model reads to decide whether a registry is built in.
struct Registry {
    std::string id{};   ///< the registry's id
    std::string name{}; ///< the name a player sees
    bool built_in{};    ///< shipped with Open Annihilation; its downloads never ask
    /// False when the registry needs an install ID and the player turned it off.
    bool downloads_on{true};
    /// Why downloads are off, in the player's words; empty when they are on.
    std::string downloads_off_reason{};
};

/// One map of a map pack, as its catalogue entry lists it.
struct MapLine {
    std::string name{}; ///< the title a player sees
    int32_t players{};  ///< how many players it is for
    std::string size{}; ///< its size as the catalogue writes it, such as "16x16"
};

/// A package's badge picture, 8-bit RGBA, rows top to bottom.
struct Badge {
    std::vector<uint8_t> rgba{}; ///< width * height * 4 bytes
    uint32_t width{};            ///< in pixels, at most 64
    uint32_t height{};           ///< in pixels, at most 64
};

/// One package a registry's catalogue lists.
struct Listing {
    std::string registry{}; ///< the registry's id
    Kind kind{Kind::mod};
    std::string key{};     ///< the package key, unique in its registry
    std::string name{};    ///< the name a player sees
    std::string version{}; ///< the version as written; empty when the kind has none
    int64_t revision{};    ///< the packaging revision; 0 when it names none
    int64_t release{};     ///< the registry's release number
    std::string publisher{};
    std::string author{};
    std::string summary{};
    std::string homepage{}; ///< an address the game can open; empty for none
    std::vector<std::string> tags{};
    /// The engine requirement as written, such as ">= 0.8.0"; empty for none.
    std::string requires_engine{};
    std::string base{}; ///< the base game the package names, such as "ta-3.1c"; empty for none
    std::optional<base::sha256::Digest> sim_hash{}; ///< the rules hash, when the entry names one
    uint32_t game_hacks{};                          ///< how many hacks change the game
    uint32_t view_hacks{};               ///< how many hacks change only what a player sees
    std::vector<std::string> hack_ids{}; ///< the hacks the mod turns on, in catalogue order
    uint64_t size{};                     ///< the package's size, in bytes
    std::optional<double> coverage{};    ///< a language pack's coverage, 0 to 1
    std::vector<MapLine> maps{};         ///< a map pack's maps
    std::optional<uint64_t> downloads{}; ///< how many times it was downloaded, when known
    std::optional<Badge> badge{};        ///< the badge, when the app has it
};

/// The version a package keeps from before its last update.
struct Kept {
    std::string version{}; ///< as written
    int64_t revision{};    ///< 0 when unknown
};

/// One package installed in the player's folder, with the release it matched.
struct Installed {
    Kind kind{Kind::mod};
    std::string key{};                           ///< the manifest's id
    std::filesystem::path folder{};              ///< the installed folder
    std::string name{};                          ///< the name a player sees
    std::string version{};                       ///< as written
    int64_t revision{};                          ///< the packaging revision; 0 when it names none
    std::string registry{};                      ///< where it came from; empty for the player's own
    int64_t release{};                           ///< the release it matched; 0 for the player's own
    std::optional<base::sha256::Digest> rules{}; ///< a mod's rules hash, when it resolves
    bool playing{};                              ///< the mod being played
    std::optional<Kept> kept{};                  ///< the copy ROLL BACK restores
    std::optional<Badge> badge{};                ///< the badge, when the app has it
};

/// A newer release of an installed package, in the same registry.
struct UpdateNote {
    /// Whether the update changes the rules saved games are tied to.
    enum class Rules : uint8_t {
        same,    ///< the rules hash stays, or the kind has no rules
        changes, ///< both hashes are known and differ
        unknown, ///< a mod is missing one of the two hashes
    };
    /// Why this Open Annihilation cannot install the update.
    enum class Blocked : uint8_t {
        none,   ///< it can
        engine, ///< the engine requirement is not met
        hacks,  ///< a hack is missing or not carried out
        base,   ///< it names another base game
    };

    std::string registry{};
    Kind kind{Kind::mod};
    std::string key{};
    int64_t to_release{};                             ///< the newer release
    std::string to_version{};                         ///< its version, as written
    int64_t to_revision{};                            ///< its revision; 0 when it names none
    uint64_t size{};                                  ///< what the update downloads, in bytes
    std::optional<base::sha256::Digest> from_rules{}; ///< the installed mod's rules hash
    std::optional<base::sha256::Digest> to_rules{};   ///< the newer release's rules hash
    Rules rules{Rules::same};
    Blocked blocked{Blocked::none};
    /// The engine requirement in words, the base, or the missing hacks; empty when not blocked.
    std::string blocked_detail{};
};

/// One item of the download queue.
struct QueueItem {
    /// Where the item is.
    enum class Phase : uint8_t {
        waiting,     ///< queued, or paused
        downloading, ///< its bytes are arriving
        verifying,   ///< its SHA-256 is being checked
        checking,    ///< the installer is checking it fits this Open Annihilation
        installing,  ///< it is being put in place
        held,        ///< installed, waiting until the player leaves the mod being played
        failed,      ///< it stopped; `problem` says why
        installed,   ///< it is in place
    };

    std::string registry{};
    Kind kind{Kind::mod};
    std::string key{};
    std::string name{};    ///< the name a player sees
    std::string version{}; ///< as written; empty when the kind has none
    Phase phase{Phase::waiting};
    uint64_t done_bytes{};  ///< bytes downloaded so far
    uint64_t total_bytes{}; ///< the package's size; 0 when unknown
    /// Why a failed item stopped, in the player's words; empty otherwise.
    std::string problem{};
    bool retryable{}; ///< a failed item can be tried again
    /// For an item queued as an update, whether it changes the rules; same for a GET.
    UpdateNote::Rules update_rules{UpdateNote::Rules::same};
    /// For an item queued as an update, what ROLL BACK restores, as
    /// change_texts gives it ("revision 3" or "4.8"); empty for a GET.
    std::string update_from{};
    /// For an item queued as an update, what it installs ("revision 4" or "4.9"); empty for a GET.
    std::string update_to{};
};

/// Everything the Library is built from.
struct Inputs {
    std::vector<Registry> registries{};
    std::vector<Listing> listings{};
    std::vector<Installed> installed{};
    std::vector<UpdateNote> updates{};
    std::vector<QueueItem> queue{};         ///< in the order the items were queued
    formats::oamod::EngineVersion engine{}; ///< this build's release
    std::string version_text{};             ///< this build's version as the header shows it
    int64_t list_age_seconds{-1};           ///< how old the newest list is; -1 for none
    bool offline{};                         ///< the last fetch could not reach a registry
    bool in_match{};                        ///< a game is being played
};

/// One of the Library's tabs.
enum class Tab : uint8_t {
    mods,
    maps,
    languages,
    updates, ///< every entry with an update, of every kind
};

/// Which entries a tab shows, apart from its kind.
enum class Filter : uint8_t {
    all,
    installed, ///< installed, playing and with an update
    updates,   ///< with an update
};

/// What an entry's row says about it.
enum class State : uint8_t {
    get,       ///< not installed
    installed, ///< installed and up to date
    update,    ///< installed, with a newer release
    playing,   ///< installed, the mod being played, up to date
};

/// Which entry: a package of one registry, or of the player's own (registry empty).
struct EntryId {
    std::string registry{};
    Kind kind{Kind::mod};
    std::string key{};

    /// Orders ids by registry, kind and key.
    friend auto operator<=>(const EntryId&, const EntryId&) = default;
};

/// One row of the Library.
///
/// The pointers point into the owning Library's `inputs` and stay valid until
/// the next refresh. `listing` is null for a package no registry lists, and
/// `installed` for one that is not installed.
struct Entry {
    EntryId id{};
    State state{State::get};
    const Listing* listing{};
    const Installed* installed{};
    const UpdateNote* update{}; ///< the update offered; null for none
    const QueueItem* queued{};  ///< its latest queue item; null for none
    /// GET or UPDATE is allowed. Always true for an INSTALLED or PLAYING entry.
    bool can_act{true};
    /// Why GET or UPDATE is off, in the player's words; empty when it is allowed.
    std::string blocked{};
    /// The registry ships with Open Annihilation: its GET and UPDATE never ask.
    bool built_in{};
    /// The registry was added by the player and is labelled "Not reviewed by the OA team".
    bool not_reviewed{};
    std::string registry_name{}; ///< the registry's name; empty for the player's own
    /// The registry the same kind and key is installed from, when GET would replace it.
    std::string replaces_registry_name{};
};

/// How the model looks up its words in the language shown.
///
/// The model writes every text from a pattern in text.hpp, English with
/// `{place}` holes. A screen that translates gives a lookup here; the model
/// looks each pattern up whole and then fills its places.
struct TextHooks {
    void* context{};
    /// Returns the text to show for an English pattern, its places still
    /// unfilled; the view must outlive the program's texts. Null shows English.
    std::string_view (*shown)(void* context, std::string_view english){};
};

/// The Library's whole state.
struct Library {
    Inputs inputs{};
    std::vector<Entry> entries{};
    Tab tab{Tab::mods};
    Filter filter{Filter::all};
    std::string tag{};                  ///< the tag shown; empty for every tag
    std::string query{};                ///< the search, at most max_query_bytes
    std::vector<std::size_t> visible{}; ///< indexes into `entries`, in the order shown
    std::optional<EntryId> selected{};  ///< one of the visible entries; none when none shows
    bool details_open{};                ///< the selected entry's details page is open
    TextHooks text{};                   ///< how texts are looked up; empty for English
};

/// The longest search kept, in bytes.
inline constexpr std::size_t max_query_bytes = 64;

/// How many missing hacks a text names before it counts the rest.
inline constexpr std::size_t named_missing_hacks = 3;

/// The base game every Open Annihilation plays.
inline constexpr std::string_view supported_base = "ta-3.1c";

/// Rebuilds the entries from new inputs.
///
/// Makes one entry per listing, joined with the package installed from the
/// same registry, kind and key, and one per installed package no listing
/// names. Keeps the tab, filter, tag, query and selection. When the selected
/// entry no longer shows, the first visible entry is selected and the
/// details close.
///
/// @param[in,out] library the Library
/// @param inputs the new inputs, which the Library keeps
void refresh(Library& library, Inputs inputs);

/// Shows a tab, keeping the filter, tag and query.
///
/// @param[in,out] library the Library
/// @param tab the tab
void set_tab(Library& library, Tab tab);

/// Shows the entries a filter keeps.
///
/// @param[in,out] library the Library
/// @param filter the filter
void set_filter(Library& library, Filter filter);

/// Shows the entries whose listing has a tag.
///
/// @param[in,out] library the Library
/// @param tag the tag; empty for every tag
void set_tag(Library& library, std::string_view tag);

/// Searches the entries, keeping at most max_query_bytes cut at a character boundary.
///
/// @param[in,out] library the Library
/// @param query the words to find; empty for every entry
void set_query(Library& library, std::string_view query);

/// Selects a visible entry. An entry that is not visible leaves the selection as it was.
///
/// @param[in,out] library the Library
/// @param id the entry
void select(Library& library, const EntryId& id);

/// Moves the selection through the visible entries, stopping at either end.
///
/// With nothing selected, selects the first visible entry.
///
/// @param[in,out] library the Library
/// @param delta how many rows to move; negative moves up
void move_selection(Library& library, int delta);

/// Opens the selected entry's details; nothing happens with nothing selected.
///
/// @param[in,out] library the Library
void open_details(Library& library);

/// Closes the details.
///
/// @param[in,out] library the Library
void close_details(Library& library);

/// Finds an entry by its id.
///
/// @param library the Library
/// @param id the entry
/// @return the entry, or null when there is none
[[nodiscard]] const Entry* find_entry(const Library& library, const EntryId& id);

/// Returns the selected entry.
///
/// @param library the Library
/// @return the entry, or null when nothing is selected
[[nodiscard]] const Entry* selected_entry(const Library& library);

/// Returns the name a player sees for an entry: its listing's, else its installed package's.
///
/// @param entry the entry
/// @return the name
[[nodiscard]] std::string_view entry_name(const Entry& entry);

/// Lists the tags of the entries a tab shows under its filter, with how many have each.
///
/// The tag and the search are not applied. The Updates tab ignores the filter.
///
/// @param library the Library
/// @return each tag and its count, sorted by tag
[[nodiscard]] std::vector<std::pair<std::string, std::size_t>> tags_in_tab(const Library& library);

/// Counts the updates that can be installed: the number on the Updates tab and the OA button.
///
/// @param library the Library
/// @return how many UPDATE entries can act
[[nodiscard]] std::size_t update_count(const Library& library);

/// How well one search word was found: the best field it is in.
enum class MatchClass : uint8_t {
    name,    ///< in the name
    tag,     ///< a tag that starts with it, or is it
    author,  ///< in the author or the publisher
    summary, ///< in the summary
};

/// How an entry ranks for a search.
struct Rank {
    MatchClass worst{MatchClass::name}; ///< the largest class among the words
    uint32_t total{};                   ///< the sum of the words' classes
    bool name_starts{};                 ///< the name starts with the first word
};

/// Ranks an entry for a search.
///
/// Letters A to Z are folded to lower case, in the query and the fields;
/// other bytes compare as they are. The query is split on spaces, and each
/// word takes the class of the best field it is found in.
///
/// @param entry the entry
/// @param query the search; empty matches every entry
/// @return the rank, or nothing when a word is in no field
[[nodiscard]] std::optional<Rank> rank(const Entry& entry, std::string_view query);

/// What a button asks for.
enum class Action : uint8_t {
    get,
    update,
    update_all,
    cancel,
    retry,
    roll_back,
    play_now,
    open_folder,
    homepage,
    details,
    back,
    settings,
    close,
};

/// One button of an entry's details.
struct ActionButton {
    Action action{Action::get};
    bool enabled{};
    std::string_view text{}; ///< the button's words, from text.hpp, looked up
};

/// Lists an entry's buttons, in the order they are shown.
///
/// GET or UPDATE first, or CANCEL while it is queued, or RETRY when its
/// download failed and can be tried again; then PLAY NOW for an installed mod
/// that is not being played, ROLL BACK when a copy is kept, OPEN FOLDER when
/// installed, and HOMEPAGE when the listing has one.
///
/// @param library the Library
/// @param entry one of its entries
/// @return the buttons
[[nodiscard]] std::vector<ActionButton> entry_actions(const Library& library, const Entry& entry);

/// How a fact's value is marked.
enum class Mark : uint8_t {
    none,
    good, ///< met
    warn, ///< not met, or a change the player should know of
};

/// One line of an entry's details, in a long form and a short form.
struct Fact {
    std::string long_label{};
    std::string long_value{};
    std::string short_label{};
    std::string short_value{};
    Mark mark{Mark::none};
};

/// Lists an entry's facts: Size, Base, Needs, Hacks, Rules, Maps, Coverage and From, those it has.
///
/// An update's Rules fact says whether it changes the game's rules and
/// which saved games stop loading.
///
/// @param library the Library
/// @param entry one of its entries
/// @return the facts, in that order
[[nodiscard]] std::vector<Fact> facts(const Library& library, const Entry& entry);

/// Describes an entry in one line under its name.
///
/// Its queue item's phase while queued, else why it is blocked, else the
/// update offered, Installed, Playing, or Not installed with its size.
///
/// @param library the Library
/// @param entry one of its entries
/// @return the line
[[nodiscard]] std::string status_line(const Library& library, const Entry& entry);

/// Names who made an entry's package: "by" its author, else its publisher.
///
/// @param entry the entry
/// @param text how texts are looked up
/// @return the line; empty for the player's own packages and when the listing names neither
[[nodiscard]] std::string byline(const Entry& entry, const TextHooks& text = {});

/// Says how an update of the selected mod meets the mod being played.
///
/// @param library the Library
/// @return the note; empty when it has none
[[nodiscard]] std::string playing_note(const Library& library);

/// The download queue as the footer shows it.
struct QueueLines {
    std::string first{};   ///< the item that matters most now
    std::string second{};  ///< the next item, and how many more
    std::string compact{}; ///< one short line for a small screen
};

/// Describes the download queue.
///
/// The first line is the item downloading; else the first verifying,
/// checking or installing; else the first that failed; else the first held;
/// else the first waiting; else the last installed. The second is the first
/// other item still to finish, in queue order, and how many more there are.
///
/// @param library the Library
/// @return the lines; empty when the queue is
[[nodiscard]] QueueLines queue_lines(const Library& library);

} // namespace oa::ui::library
