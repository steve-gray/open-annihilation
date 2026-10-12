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

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
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

/// One signing key a registry's catalogue is checked against.
struct SigningKey {
    /// The fingerprint as the game shows one, "7001 · af50 · b971 · 5bb4".
    std::string fingerprint{};
    /// The full public key, "ed25519:" and its base64.
    std::string public_key{};
};

/// A registry the Library lists packages from.
///
/// `built_in` is true when the registry ships with Open Annihilation (its
/// descriptor is in the package's registries folder). It is the only thing
/// the model reads to decide whether a registry is built in.
struct Registry {
    std::string id{};          ///< the registry's id
    std::string name{};        ///< the short name: notes, "From", and downloads off
    std::string title{};       ///< the card's heading; name when empty
    std::string description{}; ///< the paragraph under the address and refresh lines
    bool built_in{};           ///< shipped with Open Annihilation; its downloads never ask
    /// False when the registry needs an install ID and the player turned it off.
    bool downloads_on{true};
    /// Why downloads are off, in the player's words; empty when they are on.
    std::string downloads_off_reason{};
    /// The registry's own switch: false when the player turned the registry off.
    bool on{true};
    /// Where the registry was read from: the address it was added from, or
    /// its catalogue's address when it was not added from one.
    std::string address{};
    std::size_t packages{}; ///< the packages the catalogue kept for it lists
    /// How long ago a refresh last found a usable catalogue, in seconds.
    /// None when no refresh ever has.
    std::optional<int64_t> checked_ago{};
    std::size_t mirrors{}; ///< the mirrors the player added
    bool unreachable{};    ///< the last refresh found nothing usable
    /// True while this registry is being asked for its catalogue.
    bool refreshing{};
    /// The keys its signatures are checked against, in the descriptor's order.
    std::vector<SigningKey> keys{};
    /// The descriptor's author. A built-in registry shows a maker's mark
    /// when this is Open Annihilation or Cavedog Entertainment. Last so
    /// existing positional aggregates keep compiling with it empty.
    std::string author{};
};

/// A map's minimap, 8-bit RGBA, rows top to bottom. It keeps its own shape:
/// the screen fits it into a box and does not stretch it. Wider than a badge.
struct MapPicture {
    std::vector<uint8_t> rgba{}; ///< width * height * 4 bytes
    uint32_t width{};            ///< in pixels
    uint32_t height{};           ///< in pixels
};

/// One map of a map pack, as its catalogue entry or its manifest lists it.
struct MapLine {
    std::string name{}; ///< the title a player sees
    int32_t players{};  ///< how many players it is for
    std::string size{}; ///< its size as the catalogue writes it, such as "16x16"
    /// The manifest's description; empty when the catalogue lists the map,
    /// which names none, and when the manifest leaves it unwritten.
    std::string description{};
    std::string stem{};        ///< the map's file name without its extension
    std::string preview_url{}; ///< the catalogue picture's address; empty for none
    /// The minimap, when the app has decoded it. Points stay valid until the
    /// next refresh, as a badge's do.
    std::optional<MapPicture> preview{};
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
    /// The day this release was published, `YYYY-MM-DD` as the catalogue
    /// writes it; empty when the catalogue names none.
    std::string published{};
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
    /// A language pack the game shows its words from now: the language
    /// chosen's, or one of its fallbacks'. False for every other kind.
    bool in_use{};
    std::optional<Kept> kept{};   ///< the copy ROLL BACK restores
    std::optional<Badge> badge{}; ///< the badge, when the app has it
    std::string author{};         ///< the manifest's author's name; empty when it names none
    /// The manifest's description; empty when it gives none. The details
    /// show it where the listing has no summary (entry_summary).
    std::string description{};
    /// A mod folder with no oamod.yaml, which layers over the game folder
    /// and plays by the game's own rules: its status line says so.
    bool without_manifest{};
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
    /// The catalogue release it installs; 0 when the queue did not name one.
    int64_t release{};
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

/// The mod being played, as the Active tab shows it when no installed
/// package is marked playing: a folder outside the player's Mods folder,
/// such as the game's own mods folder or a folder the player picked.
struct PlayingMod {
    std::string name{};           ///< the name a player sees
    std::string version{};        ///< as written; empty when it names none
    std::optional<Badge> badge{}; ///< the badge, when the app has it
};

/// The language the game shows its words in, as the Active tab names it.
struct LanguageInUse {
    /// The language's name in itself, such as "Deutsch"; empty for English.
    std::string name{};
    /// The name of the language pack that holds it; empty when no pack does
    /// and the game shows it from its own words.
    std::string pack{};
    std::string version{}; ///< the pack's version, as written; empty for none
    /// The name in English, such as "German". Empty when it is not known,
    /// and when it is the same as the name in itself.
    std::string english{};
    /// The pack's packaging revision; 0 when it names none.
    int64_t revision{};
};

/// One installed map pack, as the Active tab lists its maps.
struct InstalledMaps {
    std::string name{};          ///< the pack's name
    std::vector<MapLine> maps{}; ///< its maps, as its manifest lists them
    /// The package key; empty when the pack is not a package the Library can select.
    std::string key{};
    /// Where it came from; empty for the player's own, and when `key` is empty.
    std::string registry{};
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
    /// The command line chose this run's mod (--mod-dir or --base-game):
    /// PLAY NOW and SWITCH OFF are off, and a mod's details say why.
    bool mod_from_command_line{};
    bool can_check{}; ///< a registry is on that the refresh icon can ask for its list
    bool checking{};  ///< the registries are being asked for their lists now
    /// The mod being played; none when the game plays no mod. An installed
    /// mod marked playing is shown in its place.
    std::optional<PlayingMod> playing_mod{};
    LanguageInUse language{};               ///< the language the game shows its words in
    std::vector<InstalledMaps> map_packs{}; ///< every installed map pack, with its maps
    /// Why the last change of a registry was refused, in the player's words.
    /// The Registries tab shows it in amber; empty for none.
    std::string registries_notice{};
    /// The base game entry's badge: Total Annihilation's icon from the game
    /// folder in use. Empty shows the entry's letters. The entry's `badge`
    /// points here until the next refresh.
    std::optional<Badge> base_game_badge{};
};

/// One of the Library's tabs, in the order they are shown.
enum class Tab : uint8_t {
    active, ///< the mod being played, the language in use and the installed maps; no list
    mods,
    maps,
    languages,
    updates,    ///< every kind, opening on the entries with an update
    registries, ///< every registry, with its switch; no list of packages
};

/// How many tabs there are.
inline constexpr std::size_t tab_total = 6;

/// Which entries a tab shows, apart from its kind, in the order the filters are shown.
enum class Filter : uint8_t {
    all,
    newest,    ///< New: the tab's newest_count listings with the latest published day
    installed, ///< installed, playing and with an update
    updates,   ///< with an update
};

/// How many filters there are.
inline constexpr std::size_t filter_total = 4;

/// How many listings New keeps on a tab.
inline constexpr std::size_t newest_count = 10;

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

/// The base game entry's key.
///
/// A package key is lower-case kebab-case, letters, digits and hyphens, so
/// it cannot hold `@`. No package's id is this one.
inline constexpr std::string_view base_game_key = "@base-game";

/// Returns the base game entry's id: a mod, no registry, and base_game_key.
///
/// @return the id
[[nodiscard]] inline EntryId base_game_id() {
    return {{}, Kind::mod, std::string(base_game_key)};
}

/// Tells whether an id is the base game entry's.
///
/// @param id the id
/// @return true for a mod, an empty registry and base_game_key
[[nodiscard]] inline bool is_base_game(const EntryId& id) {
    return id.kind == Kind::mod && id.registry.empty() && id.key == base_game_key;
}

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
    /// A badge that is neither the listing's nor the installed copy's. The
    /// base game entry's points at Inputs::base_game_badge. Null otherwise,
    /// and valid until the next refresh.
    const Badge* badge{};
    /// The name the base game entry shows, in the language shown. Empty for
    /// a package, whose name comes from its listing or its installed copy.
    std::string shown_name{};
    /// The summary the base game entry shows, in the language shown. Empty
    /// for a package.
    std::string shown_summary{};
};

/// Which maker a trusted package or registry may show.
enum class PackageMaker : uint8_t {
    none,              ///< no mark
    open_annihilation, ///< the OA badge
    cavedog,           ///< the bone
};

/// Matches an author name to a maker.
///
/// ASCII case is ignored, and space or tab at either end is ignored. Open
/// Annihilation is "OpenAnnihilation", "Open Annihilation" or "OA team".
/// The bone is "Cavedog Entertainment". Any other name matches none. The
/// publisher is not a name this matches unless it is passed as the author.
///
/// @param author the author, as a manifest or a descriptor writes it
/// @return the maker; none when the name is not one of those
[[nodiscard]] PackageMaker package_maker(std::string_view author) noexcept;

/// Returns the maker an entry may show.
///
/// None unless the entry comes from a built-in registry, listed by one or
/// installed from one. The name is the listing's author when that listing
/// names one, otherwise the installed manifest's author. A listing author
/// that is not a maker does not fall through to the manifest. The publisher
/// is never read, so a package that names no author shows no mark.
///
/// @param entry the entry
/// @return the maker; none when the entry is not trusted or names none
[[nodiscard]] PackageMaker entry_maker(const Entry& entry) noexcept;

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
    Tab tab{Tab::active}; ///< the tab shown; Active until another is asked for
    /// Each tab's own filter, by Tab's order: Updates starts on Updates and
    /// the others on All. Active and Registries have no filters and ignore
    /// their own.
    std::array<Filter, tab_total> filters{
        Filter::all, Filter::all, Filter::all, Filter::all, Filter::updates, Filter::all
    };
    std::string tag{};                  ///< the tag shown; empty for every tag
    std::string query{};                ///< the search, at most max_query_bytes
    std::vector<std::size_t> visible{}; ///< indexes into `entries`, in the order shown
    std::optional<EntryId> selected{};  ///< one of the visible entries; none when none shows
    bool details_open{};                ///< the selected entry's details page is open
    TextHooks text{};                   ///< how texts are looked up; empty for English
    /// The base game's badge, scaled for the squares of the last layout.
    /// Those pictures point here until the next layout, which clears this.
    mutable std::deque<std::vector<uint8_t>> scaled_badge_pixels{};
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
/// names, then the base game entry (base_game_id), which is not a package.
/// Keeps the tab, each tab's filter, the tag, query and selection.
/// When the selected entry no longer shows, the first visible entry is
/// selected and the details close.
/// An update note is not attached when the installed copy, or a queue item
/// whose phase is installed, already names a catalogue release above 0 that
/// is at least the note's. A release of 0 names none, and a held item has
/// not finished, so neither retires the note.
///
/// @param[in,out] library the Library
/// @param inputs the new inputs, which the Library keeps
void refresh(Library& library, Inputs inputs);

/// Shows a tab with its own filter, keeping the tag and query.
///
/// The tag hides rows only while this tab's filter offers it. A tag left
/// from another tab is kept and applies again on a tab that offers it.
///
/// @param[in,out] library the Library
/// @param tab the tab
void set_tab(Library& library, Tab tab);

/// Shows the entries a filter keeps on the tab shown, which keeps it as its own.
///
/// @param[in,out] library the Library
/// @param filter the filter
void set_filter(Library& library, Filter filter);

/// Returns the filter of the tab shown.
///
/// @param library the Library
/// @return the filter
[[nodiscard]] Filter shown_filter(const Library& library);

/// Shows the entries whose listing has a tag, when the tab's filter offers it.
///
/// A tag the filter does not offer hides nothing. The tag is kept.
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

/// Returns the words the details show under an entry's name: its listing's
/// summary, else, as for a mod of the player's own, its installed
/// manifest's description.
///
/// @param entry the entry
/// @return the words; empty when neither gives any
[[nodiscard]] std::string_view entry_summary(const Entry& entry);

/// Lists the tags of the entries a tab shows under its filter, with how many have each.
///
/// The tag and the search are not applied.
///
/// @param library the Library
/// @return each tag and its count, sorted by tag
[[nodiscard]] std::vector<std::pair<std::string, std::size_t>> tags_in_tab(const Library& library);

/// Counts the entries a filter keeps on the tab shown, before the tag and the search.
///
/// Mods, Maps and Languages hold their kind and Updates every kind; All
/// keeps them all, New the newest_count listings with the latest published
/// day, Installed those installed, playing or with an update, and Updates
/// those with an update. The Active tab holds none. The base game entry is
/// never counted, on any tab or filter.
///
/// @param library the Library
/// @param filter the filter
/// @return how many entries it keeps
[[nodiscard]] std::size_t filter_count(const Library& library, Filter filter);

/// Counts what a tab lists under All, before the tag and the search, whichever tab is open.
///
/// Mods, Maps and Languages count their kind. Updates counts the updates
/// that can be installed (update_count). Active counts none. Registries
/// counts the registries listed, not the packages.
///
/// @param library the Library
/// @param tab the tab
/// @return how many
[[nodiscard]] std::size_t tab_count(const Library& library, Tab tab);

/// Names a filter with its count, as the filter column shows it.
///
/// @param library the Library
/// @param filter the filter
/// @return such as "Installed (4)", through the pattern filter_with_count
[[nodiscard]] std::string filter_caption(const Library& library, Filter filter);

/// Counts the updates that can be installed, as UPDATE ALL acts on them.
///
/// @param library the Library
/// @return how many UPDATE entries can act
[[nodiscard]] std::size_t update_count(const Library& library);

/// Counts the UPDATE entries the list shows, as the line over the Updates tab's list says.
///
/// @param library the Library
/// @return how many visible entries have an update
[[nodiscard]] std::size_t updates_shown(const Library& library);

/// One installed map, as the Active tab lists it on its own tile.
struct ActiveMap {
    std::string name{};    ///< its title
    std::string players{}; ///< how many players it is for, in words, such as "4 players"
    std::string size{};    ///< its size as written, such as "16x16"
    /// The manifest's description; empty when it was left unwritten.
    std::string description{};
    /// The title, players and size as one line, as automation reads it:
    /// "Ridgeline · 4 players · 16x16".
    std::string line{};
    std::string pack_name{}; ///< the pack it belongs to
    /// The pack's entry, when the Maps tab can select it. The key is empty
    /// when no entry has that pack.
    EntryId pack_id{};
    /// Its minimap, which points into the Library's inputs until the next
    /// refresh; null while it has not been decoded.
    const MapPicture* preview{};
    /// "From <pack>", the link under the description that opens the pack on
    /// the Maps tab. Empty when the pack holds this map alone: the tile's
    /// title is that link, and no From line is shown.
    std::string from{};
};

/// What the Active tab shows of the player's setup.
struct ActiveSetup {
    std::string mod_name{};    ///< the mod being played; empty when no mod is
    std::string mod_version{}; ///< its version, with a revision above 1; empty for none
    /// Its version and revision as the tile states them ("4.8 (rev 3)");
    /// empty when it names neither. The base game leaves it empty.
    std::string mod_release{};
    /// Who made it, as a byline ("by the Ridge team"); empty when nobody is named.
    std::string mod_author{};
    /// "Playing" while a mod is played; empty for the base game.
    std::string mod_state{};
    /// Its badge, which points into the Library's inputs until the next
    /// refresh. The base game's is Inputs::base_game_badge; null without one.
    const Badge* mod_badge{};
    /// The base game's title when no mod is played ("Base Game"); empty when one is.
    std::string base_title{};
    /// The base game's look ("Total Annihilation 3.1c"); empty when a mod is played.
    std::string base_look{};
    /// The mod's line as automation reads it: "Ridge 4.8 · rev 3", or
    /// "No mod: the game as it was made".
    std::string mod_line{};
    /// The language's name in itself; "English" when none other is named.
    std::string language_name{};
    /// The language's name in English, when it differs from the name in itself.
    std::string language_english{};
    /// The pack's name, when it differs from both of those; empty otherwise.
    std::string language_pack{};
    /// The pack's version and revision ("1.2 (rev 4)"); empty for the built-in language.
    std::string language_version{};
    /// The pack's coverage ("Coverage 92%"); empty when it names none, and for the built-in language.
    std::string language_coverage{};
    /// "Built in" when the game holds the language; empty for a pack.
    std::string language_note{};
    /// The pack's badge, which points into the Library's inputs until the
    /// next refresh; null without one, and for the built-in language.
    const Badge* language_badge{};
    /// The language's line: the pack and its version, "Deutsch 1.2", or "English · built in".
    std::string language_line{};
    /// The installed maps, ordered by title and then by pack, both folded
    /// to lower case. One tile each, not one tile per pack.
    std::vector<ActiveMap> maps{};
};

/// Describes the Active tab.
///
/// The mod is the installed mod marked playing, with its listing's name,
/// badge and author when a registry lists it, else Inputs::playing_mod,
/// else the base game. The language is the pack that holds the language
/// shown, with its name in itself and in English, its version, revision
/// and coverage, else the language the game holds itself. The maps are
/// every map of every installed pack, ordered by title and then by the
/// pack's name, folded to lower case. A map's pack is the map-pack entry
/// of the same key and registry, or, when the pack names no key, the
/// entry whose name is the pack's. A pack that holds one map leaves the
/// From line empty; a pack of several names itself there.
///
/// @param library the Library
/// @return the setup
[[nodiscard]] ActiveSetup active_setup(const Library& library);

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
    /// The mod being played's own in place of PLAY NOW: the game plays no
    /// mod from then on, once the player answers the Switch Mod question.
    switch_off,
    open_folder,
    homepage,
    details,
    back,
    check_now, ///< the header's refresh icon: the registries are asked for their lists again
    settings,
    close,
    /// ADD REGISTRY… on the Registries tab. The host opens the add-registry question.
    add_registry,
    /// ADD MIRROR… on a registry the player added. `ScreenResult::registry` names it.
    add_mirror,
    /// REMOVE on a registry the player added. `ScreenResult::registry` names it.
    /// The host removes it at once.
    remove_registry,
    /// A registry's switch. `ScreenResult::registry` names it and
    /// `ScreenResult::registry_on` is the half that was chosen.
    set_registry,
    /// REFRESH on one registry's row. `ScreenResult::registry` names it.
    /// The host asks that registry for its catalogue and its download counts.
    refresh_registry,
    /// An installed package's: asks first, then removes its folder and the
    /// copy ROLL BACK keeps (uninstall.hpp).
    uninstall,
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
/// that is not being played, or SWITCH OFF for the one that is, ROLL BACK
/// when a copy is kept, UNINSTALL and OPEN FOLDER when installed, and
/// HOMEPAGE when the listing has one. PLAY NOW, SWITCH OFF and ROLL BACK are
/// off during a game, and PLAY NOW and SWITCH OFF while the command line
/// chose the mod; UNINSTALL is off while uninstall_off_reason gives a reason.
/// The base game entry is not a package: while no mod is played it offers
/// nothing, and while one is it offers PLAY NOW alone, on and off by the
/// same rule as SWITCH OFF, and never UPDATE, ROLL BACK, UNINSTALL or
/// OPEN FOLDER.
///
/// @param library the Library
/// @param entry one of its entries
/// @return the buttons
[[nodiscard]] std::vector<ActionButton> entry_actions(const Library& library, const Entry& entry);

/// Tells whether PLAY NOW and SWITCH OFF can act, and so whether the Active
/// tab's UNLOAD MOD can: not during a game, and not while the command line
/// chose this run's mod.
///
/// @param library the Library
/// @return true when the switch can be asked for
[[nodiscard]] bool can_switch(const Library& library) noexcept;

/// Tells whether the header's refresh icon can be pressed: a registry is on
/// that it can ask, none is being asked now, and no game is being played.
///
/// @param library the Library
/// @return true when it can
[[nodiscard]] bool can_check_now(const Library& library) noexcept;

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

/// Lists an entry's facts: Installed, Latest, Author, Size, Base, Needs,
/// Hacks, Rules, Maps, Coverage and From, those it has.
///
/// Installed is the installed copy's version and revision, and Latest the
/// listing's, each as release_text writes them ("4.8 (rev 3)", "1.0" for a
/// revision of 0, "rev 3" without a version); a side with neither is left
/// out. Author is the listing's author, else its publisher, else the
/// installed manifest's author; left out when none names one. An update's
/// Rules fact says whether it changes the game's rules and which saved
/// games stop loading.
///
/// @param library the Library
/// @param entry one of its entries
/// @return the facts, in that order
[[nodiscard]] std::vector<Fact> facts(const Library& library, const Entry& entry);

/// Describes an entry in one line under its name.
///
/// Its queue item's phase while queued, else why it is blocked, else the
/// update offered, Installed, Playing, or Not installed with its size. An
/// update is "Revision 4 available" only for a revision above 0 of the
/// version installed, "Version 4.9 available" for another version, and
/// "Update available" when it keeps the version and names no revision.
///
/// @param library the Library
/// @param entry one of its entries
/// @return the line
[[nodiscard]] std::string status_line(const Library& library, const Entry& entry);

/// Names who made an entry's package, and how many times it was downloaded
/// when the registry has a count: "by" its author, else its publisher, then
/// " · 12,400 downloads". The count alone when the listing names no one.
///
/// @param entry the entry
/// @param text how texts are looked up
/// @return the line; empty for the player's own packages, and when the
///         listing names neither a person nor a count
[[nodiscard]] std::string byline(const Entry& entry, const TextHooks& text = {});

/// Says how an update of one mod meets the mod being played, and, on an
/// installed mod while the command line chose this run's mod, that it did,
/// before anything else.
///
/// @param library the Library
/// @param entry the entry the note is about
/// @return the note; empty when it has none
[[nodiscard]] std::string playing_note(const Library& library, const Entry& entry);

/// Says how an update of the selected mod meets the mod being played, and,
/// on an installed mod while the command line chose this run's mod, that it
/// did, before anything else. The Active tab selects nothing, so a note
/// about the mod being played uses the form that takes that entry.
///
/// @param library the Library
/// @return the note; empty when nothing is selected, or when it has none
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
