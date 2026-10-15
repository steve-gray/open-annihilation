// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The map browser's model: every map a player could play, the base game's,
// the installed packs' and those a catalogue offers, as one list of cards;
// the filters over them, their order, the selected card and what its primary
// button does. It is built from plain inputs and reads no file, clock or
// network; the host fills the cards and acts on what the screen asks.
#pragma once

#include <cstddef>
#include <optional>
#include <stdint.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::map_browser {

/// One map the browser shows as a card.
struct MapCard {
    /// The game's name for the map: the stem for a base map, `<stem>@<id>`
    /// for a pack map, installed or online. Two cards never share one.
    std::string name{};
    std::string title{};         ///< the title players see; the name when the map gives none
    std::string pack_name{};     ///< the pack's name; empty for a map that is not from a pack
    std::string pack_version{};  ///< the pack's version; empty for a map that is not from a pack
    std::string registry_name{}; ///< the name of the registry the pack comes from; empty for none
    /// Where a game or mod map comes from, in English, as the overlay shows it:
    /// "TA 3.1c", a bundle's name, or the mod's name. Empty when the overlay
    /// uses the pack's name, "Downloaded", "Available" or "Downloading…".
    std::string source_name{};
    /// The map's width and height as its source writes them, in the game's
    /// size units: "16 x 16" from a base map's header, "16x16" from a pack.
    std::string size{};
    /// The preview cache's key for the map's picture: the map's name for a
    /// base map, the preview file for an installed pack, the picture's
    /// address for an online one. Empty when the map has no picture.
    std::string preview_key{};
    /// The counts the map is for, sorted, without repeats. A base map keeps
    /// each count its numplayers lists, from 2 to most_players. A pack map
    /// keeps its manifest's players as its one count. Empty when only
    /// players is set, which is then that one count.
    std::vector<int32_t> player_counts{};
    int32_t players{};         ///< the most players the map takes: the largest count
    uint64_t download_bytes{}; ///< the pack's size, in bytes, for a map online; 0 otherwise
    /// Where the map is.
    enum class Origin : uint8_t {
        game,      ///< the base game, or an expansion that is on
        mod,       ///< a map from the mod's own layers
        installed, ///< in a pack in the player's Maps folder
        online,    ///< in a pack a catalogue offers, not installed
    } origin{};
    /// Whether the map fits a game: TA 3.1c (base_fit), or the mod being
    /// played (mod_fit, which counts only while a mod plays).
    enum class Fit : uint8_t {
        fits,         ///< it fits
        does_not_fit, ///< it does not fit; fit_reason says why
        unknown,      ///< not known until the map is installed
    } base_fit{}, mod_fit{};
    std::string fit_reason{}; ///< why it does not fit, in words a player reads; empty when it fits
};

/// What the player narrows the cards to.
struct Filters {
    /// Which maps show, by where they are.
    enum class Source : uint8_t {
        all,       ///< every map
        game,      ///< the base game and every expansion that is on
        mod,       ///< maps from the mod's own layers
        downloads, ///< installed packs and packs a catalogue offers, together
    } source{};
    /// The players selector. None, or 0, is Any. Otherwise 2, 3, 4, or
    /// many_players_chip for 5–10. One choice: a map shows when that choice
    /// is among its counts.
    std::optional<int32_t> players{};
    /// The size selector. None is Any. Otherwise 'S', 'M' or 'L'. One choice.
    std::optional<char> size{};
    std::string query{}; ///< the search's text; empty matches every map
};

/// How many cards each source pill counts: every card the players selector,
/// the size selector and the search leave, whatever source is chosen.
struct SourceCounts {
    std::size_t all{};       ///< every such card
    std::size_t game{};      ///< origin game
    std::size_t mod{};       ///< origin mod
    std::size_t downloads{}; ///< origin installed or online
};

/// The map browser: its cards, the filters, what shows, the selection and
/// a download under way.
struct Browser {
    std::vector<MapCard> cards{}; ///< every map, as merge_cards gives them
    Filters filters{};            ///< what the player narrows them to
    /// The cards that show, as indices into cards, in the order shown
    /// (apply_filters).
    std::vector<std::size_t> shown{};
    std::optional<std::size_t>
        selected{}; ///< the selected card, an index into cards; none for none
    /// The picker that opened the browser, whose selection Select moves.
    enum class Opener : uint8_t {
        skirmish,    ///< the skirmish setup's map list
        battle_room, ///< the host's map list in the battle room
    } opener{};
    std::string mod_name{}; ///< the name of the mod being played; empty for TA 3.1c alone
    /// The name of the card whose pack downloads now; none while none does.
    std::optional<std::string> downloading{};
    float progress{};    ///< how far the download has come, from 0 to 1
    std::string error{}; ///< why the last download failed, in words a player reads; empty for none
};

/// The players a map takes at most, from 1.
inline constexpr int32_t most_players = 10;
/// The players chip that matches every map for 5 to most_players players.
inline constexpr int32_t many_players_chip = 5;
/// The longer side of a small map, at most, in the game's size units.
inline constexpr int32_t small_side_most = 10;
/// The longer side of a medium map, at most; a larger one is large.
inline constexpr int32_t medium_side_most = 20;

/// Merges the four sources' maps into one list of cards, each named once.
///
/// The game's maps come first, then the mod's, then the installed pack
/// maps, then those a catalogue offers, each in the order given, and each
/// card's origin is set by the list it came from. A card whose name an
/// earlier card has is left out, so an online map whose game name an
/// installed pack already provides, from any registry, shows once, as
/// installed (the Library offers its updates and other registries' copies),
/// and a second registry's copy of an online map shows once, the first
/// registry's. No map is dropped for any other reason.
///
/// @param game the base game's maps and those of the expansions that are on
/// @param mod the maps from the mod's own layers
/// @param installed the installed packs' maps
/// @param online the maps the catalogues offer
/// @return the cards; no two share a name
[[nodiscard]] std::vector<MapCard> merge_cards(
    std::vector<MapCard> game,
    std::vector<MapCard> mod,
    std::vector<MapCard> installed,
    std::vector<MapCard> online
);

/// Returns a map's width and height from its size text: the first two
/// numbers it holds, as in "16 x 16" or "16x16".
///
/// @param size the size text
/// @return the width and height; none when it holds fewer than two numbers
[[nodiscard]] std::optional<std::pair<int32_t, int32_t>> map_sides(std::string_view size) noexcept;

/// Returns a map's size chip: 'S' for a longer side of at most
/// small_side_most, 'M' for one of at most medium_side_most, 'L' above.
///
/// @param size the size text
/// @return the chip's letter; none when the size cannot be read
[[nodiscard]] std::optional<char> size_letter(std::string_view size) noexcept;

/// The player counts a base map keeps, and the most of them.
struct BaseMapPlayers {
    /// Each count the map is for. Those its numplayers lists, each from 2 to
    /// most_players, sorted and without repeats; or, when that lists none,
    /// the start-position count alone, which may lie outside that range.
    std::vector<int32_t> counts{};
    int32_t players{}; ///< the most players: the largest count
    /// True when counts came from numplayers. False when they are the
    /// start-position count alone.
    bool from_numplayers{};
};

/// Returns the player counts a base map keeps.
///
/// `numplayers` is the map's list, as "2, 4". Each whole number from 2 to
/// most_players is kept, sorted, without repeats, and players is the
/// largest. A number outside that range is left out. When the list is
/// missing or holds no such number, the map keeps `start_positions` alone:
/// the most start positions of a network schema, which is not read here.
///
/// @param numplayers the map's list; empty when it states none
/// @param start_positions the most start positions of a network schema
/// @return the counts, the most players, and whether numplayers named them
[[nodiscard]] BaseMapPlayers base_map_players(std::string_view numplayers, int32_t start_positions);

/// Returns the most players a card shows: the largest of its counts, or
/// players when it lists none.
///
/// @param card the card
/// @return the count the card and its details read
[[nodiscard]] int32_t shown_players(const MapCard& card) noexcept;

/// Tells whether a players chip matches a map. 2, 3 and 4 match when that
/// count is among the map's; many_players_chip matches when any count is
/// from 5 to most_players. A map that lists no counts is matched on
/// players alone.
///
/// @param chip the chip
/// @param card the map
/// @return true when it matches
[[nodiscard]] bool players_match(int32_t chip, const MapCard& card) noexcept;

/// Tells whether a search matches a card: its title, name, pack name or
/// source name holds the query, ignoring ASCII case. An empty query matches.
///
/// @param card the card
/// @param query the search's text
/// @return true when it matches
[[nodiscard]] bool query_matches(const MapCard& card, std::string_view query);

/// Returns the English name a card's overlay shows.
///
/// An installed map whose pack is named for the map (its title, or the stem
/// of its game name before '@', ignoring ASCII case) shows "Downloaded".
/// The same map, offered by a catalogue and not installed, shows "Available": it
/// is available to get. While that card's pack is downloading,
/// `downloading` shows "Downloading…". Any other card shows source_name
/// when that is set, and otherwise the pack's name. The name is the
/// catalogue's English key for "Downloaded", "Available", "Downloading…" and
/// "TA 3.1c", and the name as given for a bundle, a mod or a pack.
///
/// @param card the card
/// @param downloading true while this card's pack is downloading
/// @return the overlay's English name; empty when the card names no source
[[nodiscard]] std::string overlay_name(const MapCard& card, bool downloading = false);

/// Counts the cards each source pill shows. The players selector, the size
/// selector and the search narrow the count. The source filter does not, so
/// "All (N)" is how many the other selectors leave.
///
/// @param browser the browser
/// @return the four counts
[[nodiscard]] SourceCounts source_counts(const Browser& browser);

/// Fills what shows from the cards and the filters: the cards that match
/// the source, the players choice (a count the map is for; Any matches
/// every map), the size choice and the search. Every match is ordered by
/// title, ignoring ASCII case, then by name, wherever the map comes from.
/// A selection that no longer shows is cleared. No card is dropped from
/// the browser's list.
///
/// @param[in,out] browser the browser; its shown and selected change
void apply_filters(Browser& browser);

/// Tells whether a card fits: neither fit says it does not.
///
/// @param card the card
/// @return true unless a fit is does_not_fit
[[nodiscard]] bool fits(const MapCard& card) noexcept;

/// What the primary button says, and does.
enum class Primary : uint8_t {
    select,              ///< "Select": moves the picker's selection to the map
    download_and_select, ///< "Download and select": installs the pack first
    downloading,         ///< "Downloading…": a download is under way
};

/// The primary button of the selected card.
struct PrimaryAction {
    Primary kind{Primary::select}; ///< what it says and does
    bool enabled{};                ///< a press reaches it
    std::string reason{}; ///< why it is disabled for a map that does not fit; empty otherwise
};

/// Returns the primary button: disabled "Downloading…" while a download is
/// under way; disabled Select with no selection; for a map that does not
/// fit, disabled with its reason (Download and select for one online,
/// Select for another); Download and select for an online map that fits or
/// whose fit is unknown; Select for a game, mod or installed map that fits.
///
/// @param browser the browser
/// @return the button
[[nodiscard]] PrimaryAction primary_action(const Browser& browser);

/// Returns a map's game name as one word of an automation name: lower case,
/// every character outside a-z, 0-9 and the hyphen turned into a hyphen
/// ("isle_of_ashes\@archipelago" gives "isle-of-ashes-archipelago"), at most
/// most_word_bytes.
///
/// @param name the game name, in UTF-8
/// @return the word; "map" for an empty name
[[nodiscard]] std::string card_word(std::string_view name);

/// The most bytes card_word keeps, so a card's automation name stays under
/// the kit's 100 with its prefix and a count.
inline constexpr std::size_t most_word_bytes = 80;

} // namespace oa::ui::map_browser
