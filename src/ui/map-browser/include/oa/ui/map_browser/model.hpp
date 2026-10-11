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
#include <set>
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
    std::string pack_name{};     ///< the pack's name; empty for a base map
    std::string pack_version{};  ///< the pack's version; empty for a base map
    std::string registry_name{}; ///< the name of the registry the pack comes from; empty for none
    /// The map's width and height as its source writes them, in the game's
    /// size units: "16 x 16" from a base map's header, "16x16" from a pack.
    std::string size{};
    /// The preview cache's key for the map's picture: the map's name for a
    /// base map, the preview file for an installed pack, the picture's
    /// address for an online one. Empty when the map has no picture.
    std::string preview_key{};
    int32_t players{};         ///< the most players the map takes
    uint64_t download_bytes{}; ///< the pack's size, in bytes, for a map online; 0 otherwise
    /// Where the map is.
    enum class Origin : uint8_t {
        base,      ///< in the base game
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
        base,      ///< the base game's
        installed, ///< the installed packs'
        online,    ///< those online, not installed
    } source{};
    /// The player chips that are on: 2, 3 and 4, and many_players_chip for
    /// 5–10. A map shows when any of them matches it; none on shows every map.
    std::set<int32_t> players{};
    /// The size chips that are on: 'S', 'M' and 'L'. A map shows when any of
    /// them matches it; none on shows every map.
    std::set<char> sizes{};
    std::string query{}; ///< the search's text; empty matches every map
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

/// Merges the three sources' maps into one list of cards, each named once.
///
/// The base maps come first, then the installed pack maps, then those
/// online, each in the order given, and each card's origin is set by the
/// list it came from. A card whose name an earlier card has is left out:
/// so an online map whose game name an installed pack already provides,
/// from any registry, shows once, as installed (the Library offers its
/// updates and other registries' copies), and a second registry's copy of
/// an online map shows once, the first registry's.
///
/// @param base the base game's maps
/// @param installed the installed packs' maps
/// @param online the maps the catalogues offer
/// @return the cards; no two share a name
[[nodiscard]] std::vector<MapCard>
merge_cards(std::vector<MapCard> base, std::vector<MapCard> installed, std::vector<MapCard> online);

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

/// Tells whether a players chip matches a map: 2, 3 and 4 a map whose most
/// players is that number, many_players_chip one for 5 to most_players.
///
/// @param chip the chip
/// @param players the map's most players
/// @return true when it matches
[[nodiscard]] bool players_match(int32_t chip, int32_t players) noexcept;

/// Tells whether a search matches a card: its title, name or pack name
/// holds the query, ignoring ASCII case. An empty query matches.
///
/// @param card the card
/// @param query the search's text
/// @return true when it matches
[[nodiscard]] bool query_matches(const MapCard& card, std::string_view query);

/// Fills what shows from the cards and the filters: the cards that match
/// the source, any player chip on, any size chip on and the search; the base
/// maps first, then the installed, then those online, each by title ignoring
/// ASCII case, then by name. A selection that no longer shows is cleared.
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
/// whose fit is unknown; Select for a base or installed map that fits.
///
/// @param browser the browser
/// @return the button
[[nodiscard]] PrimaryAction primary_action(const Browser& browser);

/// Returns a map's game name as one word of an automation name: lower case,
/// every character outside a-z, 0-9 and the hyphen turned into a hyphen
/// ("isle_of_ashes@archipelago" gives "isle-of-ashes-archipelago"), at most
/// most_word_bytes.
///
/// @param name the game name, in UTF-8
/// @return the word; "map" for an empty name
[[nodiscard]] std::string card_word(std::string_view name);

/// The most bytes card_word keeps, so a card's automation name stays under
/// the kit's 100 with its prefix and a count.
inline constexpr std::size_t most_word_bytes = 80;

} // namespace oa::ui::map_browser
