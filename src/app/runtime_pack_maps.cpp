// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Pack maps in the skirmish picker and the match: one map's files mounted at
// a time, checked against the game and the mod played when they are mounted,
// and unmounted when another map is chosen, when the match ends and whenever
// the main menu shows.

#include "map_packs.hpp"

#include "oa/app/content/downloads.hpp"
#include "oa/app/package_install/origin.hpp"
#include "oa/app/runtime.hpp"
#include "oa/data/defs/layout.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/data/map_fit/map_fit.hpp"
#include "oa/data/map_pack/manifest.hpp"
#include "oa/data/map_pack/map_name.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/ui/frontend/savegame_dialogs.hpp"
#include "oa/ui/frontend_multiplayer/map_list.hpp"
#include "oa/ui/frontend_multiplayer/screens.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace oa::app {

namespace fit = oa::data::map_fit;
namespace pack = oa::data::map_pack;

// A saved game keeps the name of the map it was played on in its summary,
// with the terminating NUL. A pack map's name always fits there whole, so a
// saved game on a pack map loads that map by its name.
// clang-format off
static_assert(oa::data::map_pack::most_map_name_bytes < std::tuple_size_v<decltype(oa::ui::frontend::LoadSummary::mission)>, "a pack map's name cut short in a saved game names no map");
// clang-format on

struct Runtime::PackMapState {
    /// The game's and the mod's feature, weapon and unit names, collected at
    /// the first check: the run's archives never change.
    std::optional<fit::GameNames> game_names{};
    /// Each pack map's fit, by the pack's SHA-256 and the map's stem.
    std::map<std::pair<std::string, std::string>, fit::Fit> fits{};
    /// Why each refused map was refused: the first failure's description.
    std::map<std::string, std::string, std::less<>> refusals{};
    /// The name of the map whose files are mounted; empty when none is.
    std::string mounted_name{};
    /// The pack id of that map.
    std::string mounted_id{};
    /// The pack whose maps are not mounted while GET MAP installs it; empty
    /// when none is held (hold_pack_maps).
    std::string held_id{};
    /// The queue item that downloads and installs the held pack.
    uint64_t held_item{};
    /// Pack maps that fit the mod being played, in the index's order.
    std::vector<std::string> listed{};
    /// Packs already named in the log for a mod, as the pack id and the key.
    std::vector<std::pair<std::string, std::string>> omission_logged{};
    /// Those log lines, for a check to read.
    std::vector<std::string> omission_lines{};
    /// The catalogue generation the lists were last built from.
    uint64_t catalogue_generation{};
    /// True once the lists have been built from a catalogue snapshot.
    bool catalogue_noted{};
};

namespace {

/// The interface's words for a refused map; {game} and {reason} are filled.
constexpr std::string_view kRefusedText = "Doesn't fit {game}: {reason}";

/// The interface's name for the base game, when no mod is played.
constexpr std::string_view kBaseGameText = "No Mod";

/// The compatible table's key for the base game, when no mod is played.
constexpr std::string_view kBaseGameKey = "ta-3.1c";

/// The words a log line uses when the mod being played has no catalogue key.
constexpr std::string_view kUnkeyedMod = "the mod being played";

/// Why a held pack's map is not mounted: its download is being installed.
constexpr std::string_view kHeldReason = "its pack is being downloaded";

/// The number of hex digits of a pack's SHA-256 in its layer's label.
constexpr std::size_t kLabelDigestDigits = 8;

/// Returns the manifest entry of an installed pack map.
///
/// @param map the map, as the installed packs list it
/// @return the entry its fit check and its layer read
pack::MapEntry entry_of(const PackMap& map) {
    pack::MapEntry entry{};
    entry.stem = map.stem;
    entry.title = map.title;
    entry.description = map.description;
    entry.size = map.size;
    entry.players = map.players;
    entry.files = map.files;
    return entry;
}

/// A fit with one failure: the map could not be checked or mounted.
///
/// @param subject the map's name
/// @param detail what went wrong
/// @return the fit
fit::Fit unreadable_fit(const std::string& subject, std::string detail) {
    fit::Fit unfit;
    unfit.failures.push_back({fit::Rule::complete, subject, std::move(detail)});
    return unfit;
}

/// Fills each "{name}" of a text with its value.
///
/// @param text the text
/// @param fields each field's name and value
/// @return the text with its fields filled
std::string filled(
    std::string text, std::initializer_list<std::pair<std::string_view, std::string_view>> fields
) {
    for (const auto& [name, value] : fields) {
        const std::string field = "{" + std::string(name) + "}";
        for (std::size_t at = text.find(field); at != std::string::npos;
             at = text.find(field, at + value.size()))
            text.replace(at, field.size(), value);
    }
    return text;
}

} // namespace

void Runtime::destroy_pack_map_state(PackMapState* state) noexcept {
    delete state;
}

Runtime::PackMapState& Runtime::pack_map_state() {
    if (!pack_map_state_)
        pack_map_state_.reset(new PackMapState());
    return *pack_map_state_;
}

const PackMap* Runtime::pack_map(std::string_view name) {
    if (!pack::split_pack_map_name(name))
        return nullptr;
    return map_packs().find(name);
}

const fit::Fit& Runtime::pack_map_fit(const PackMap& map) {
    auto& state = pack_map_state();
    auto key = std::make_pair(map.sha256, map.stem);
    if (const auto found = state.fits.find(key); found != state.fits.end())
        return found->second;
    fit::Fit checked;
    try {
        const oa::data::defs::DataLayout& layout = oa::data::defs::data_layout();
        if (!state.game_names)
            state.game_names = fit::collect_game_names(assets_, layout);
        const pack::MapEntry entry = entry_of(map);
        const std::unique_ptr<fit::MapFiles> files =
            fit::folder_map_files(map.folder, entry, map.id);
        checked = files ? fit::check_map(assets_, *state.game_names, *files, entry, map.id, layout)
                        : unreadable_fit(map.name, "could not be read from its pack's folder");
    } catch (const std::exception& error) {
        checked = unreadable_fit(map.name, std::string("could not be checked: ") + error.what());
    }
    return state.fits.emplace(std::move(key), std::move(checked)).first->second;
}

void Runtime::drop_pack_map_fits() {
    if (pack_map_state_ == nullptr)
        return;
    auto& state = pack_map_state();
    state.fits.clear();
    state.game_names.reset();
}

bool Runtime::prepare_pack_map(std::string_view name, std::string* reason) {
    auto& state = pack_map_state();
    // GET MAP is replacing this pack's folder: none of its maps is mounted
    // until the install ends.
    if (const auto split = pack::split_pack_map_name(name); split && pack_maps_held(split->id)) {
        if (state.mounted_id == split->id)
            release_pack_map();
        if (reason != nullptr)
            *reason = std::string(kHeldReason);
        return false;
    }
    // A map already mounted stays as it is: a joiner asks every frame.
    if (!state.mounted_name.empty() && state.mounted_name == name && assets_.pack_layer_mounted())
        return true;
    release_pack_map();
    const PackMap* map = pack_map(name);
    if (map == nullptr) {
        if (reason != nullptr)
            *reason = "the map is not installed";
        return false;
    }
    const auto refuse = [&](const fit::Fit& unfit) {
        for (const fit::Failure& failure : unfit.failures)
            std::cerr << "open-annihilation: map " << name
                      << " does not fit: " << fit::describe(failure) << '\n';
        const std::string why = unfit.failures.empty() ? std::string("it does not fit")
                                                       : fit::describe(unfit.failures.front());
        if (const auto kept = state.refusals.find(name); kept != state.refusals.end())
            kept->second = why;
        else
            state.refusals.emplace(std::string(name), why);
        if (reason != nullptr)
            *reason = why;
        return false;
    };
    const fit::Fit& kept = pack_map_fit(*map);
    if (!kept.fits())
        return refuse(kept);

    const pack::MapEntry entry = entry_of(*map);
    oa::PackLayerSpec spec{};
    spec.kind = oa::PackLayerKind::folder;
    spec.location = map->folder;
    spec.files = fit::layer_files(entry, map->id);
    spec.label = map->id + " " + map->sha256.substr(0, kLabelDigestDigits);
    const std::size_t file_count = spec.files.size();
    std::string error;
    if (!assets_.mount_pack_layer(std::move(spec), &error))
        return refuse(unreadable_fit(
            map->name, "could not be mounted: " + (error.empty() ? std::string("no reason") : error)
        ));
    // The pack's folder may have changed since the kept check, so the files
    // as the store now shows them are checked whatever that check found.
    fit::Fit mounted;
    try {
        if (!state.game_names)
            state.game_names = fit::collect_game_names(assets_, oa::data::defs::data_layout());
        const std::unique_ptr<fit::MapFiles> files = fit::mounted_map_files(assets_);
        mounted = files ? fit::check_map(
                              assets_,
                              *state.game_names,
                              *files,
                              entry,
                              map->id,
                              oa::data::defs::data_layout()
                          )
                        : unreadable_fit(map->name, "could not be read as mounted");
    } catch (const std::exception& caught) {
        mounted = unreadable_fit(map->name, std::string("could not be checked: ") + caught.what());
    }
    if (!mounted.fits()) {
        std::ignore = assets_.unmount_pack_layer();
        return refuse(mounted);
    }
    state.mounted_name = map->name;
    state.mounted_id = map->id;
    if (const auto refused = state.refusals.find(name); refused != state.refusals.end())
        state.refusals.erase(refused);
    std::cerr << "open-annihilation: map pack " << map->id << ": mounted " << map->name << " ("
              << file_count << " files)\n";
    return true;
}

void Runtime::release_pack_map() {
    if (!assets_.pack_layer_mounted()) {
        if (pack_map_state_)
            pack_map_state_->mounted_name.clear();
        return;
    }
    const std::optional<std::string> label = assets_.pack_layer_label();
    if (!assets_.unmount_pack_layer())
        return;
    if (pack_map_state_ && !pack_map_state_->mounted_name.empty()) {
        std::cerr << "open-annihilation: map pack " << pack_map_state_->mounted_id << ": unmounted "
                  << pack_map_state_->mounted_name << '\n';
        pack_map_state_->mounted_name.clear();
        pack_map_state_->mounted_id.clear();
    } else {
        std::cerr << "open-annihilation: map pack layer " << label.value_or(std::string())
                  << ": unmounted\n";
    }
}

void Runtime::hold_pack_maps(std::string_view id, uint64_t item) {
    auto& state = pack_map_state();
    state.held_id = std::string(id);
    state.held_item = id.empty() ? 0 : item;
}

bool Runtime::pack_maps_held(std::string_view id) {
    if (!pack_map_state_ || pack_map_state_->held_id.empty() || pack_map_state_->held_id != id)
        return false;
    namespace content = oa::app::content;
    const uint64_t item = pack_map_state_->held_item;
    const std::vector<content::DownloadView> items = content_downloads().view();
    const auto found =
        std::find_if(items.begin(), items.end(), [item](const content::DownloadView& view) {
            return view.item == item;
        });
    const bool ended = found == items.end() || found->state == content::DownloadState::done ||
                       found->state == content::DownloadState::failed ||
                       found->state == content::DownloadState::cancelled;
    if (!ended)
        return true;
    std::cerr << "open-annihilation: map pack " << pack_map_state_->held_id
              << ": its maps may be mounted again\n";
    pack_map_state_->held_id.clear();
    pack_map_state_->held_item = 0;
    return false;
}

const PackMap* Runtime::mounted_pack_map() {
    if (!pack_map_state_ || pack_map_state_->mounted_name.empty() || !assets_.pack_layer_mounted())
        return nullptr;
    return map_packs().find(pack_map_state_->mounted_name);
}

std::optional<std::string> Runtime::pack_map_refusal(std::string_view name) const {
    if (!pack_map_state_)
        return std::nullopt;
    const auto found = pack_map_state_->refusals.find(name);
    if (found == pack_map_state_->refusals.end())
        return std::nullopt;
    return found->second;
}

std::optional<std::string> Runtime::map_refusal(std::string_view name) {
    return pack_map_refusal(name);
}

void Runtime::show_refused_map(std::string_view name, std::string_view reason) {
    const PackMap* map = pack_map(name);
    set_modal_text("MAPNAME", map != nullptr ? std::string_view(map->title) : name, 0);
    set_modal_text(
        "SIZE",
        map != nullptr
            ? map->size + "  " + translate_ui("Players") + ": " + std::to_string(map->players)
            : std::string(),
        0
    );
    const auto shown = picture();
    if (shown.value != 0) {
        release_picture(shown);
        set_picture({});
    }
    const std::string game = options_.mod_profile && !options_.mod_profile->name.empty()
                                 ? options_.mod_profile->name
                                 : std::string(oa::data::languages::interface_text(kBaseGameText));
    set_modal_text(
        "DESCRIPTION",
        filled(
            std::string(oa::data::languages::interface_text(kRefusedText)),
            {{"game", game}, {"reason", reason}}
        ),
        0
    );
    invalidate_menu();
}

std::string Runtime::played_map_key() const {
    if (!plays_mod())
        return std::string(kBaseGameKey);
    try {
        const std::optional<package_install::Origin> origin =
            package_install::read_origin(played_mod_folder());
        if (!origin || origin->kind != package_install::OriginKind::catalogue ||
            origin->catalogue_id.empty() || origin->release <= 0)
            return {};
        return origin->catalogue_id + "@" + std::to_string(origin->release);
    } catch (const std::exception&) {
        return {};
    }
}

const std::vector<std::string>& Runtime::listed_pack_maps() {
    if (pack_map_state_ == nullptr) {
        static const std::vector<std::string> none;
        return none;
    }
    return pack_map_state_->listed;
}

std::vector<std::string> Runtime::pack_map_omission_lines() const {
    if (pack_map_state_ == nullptr)
        return {};
    return pack_map_state_->omission_lines;
}

void Runtime::refresh_listed_pack_maps() {
    auto& state = pack_map_state();
    const std::string key = played_map_key();
    const std::string shown_key = key.empty() ? std::string(kUnkeyedMod) : key;

    // Copy the index out before the lists change. A later refresh of the
    // index would free the span maps() returns.
    std::vector<PackMap> installed;
    for (const PackMap& installed_map : map_packs().maps())
        installed.push_back(installed_map);

    std::vector<std::string> base;
    base.reserve(eligible_map_names_.size());
    for (const std::string& name : eligible_map_names_)
        if (!pack::split_pack_map_name(name))
            base.push_back(name);

    std::map<std::string, int, std::less<>> omitted;
    std::vector<std::string> listed;
    listed.reserve(installed.size());
    for (const PackMap& installed_map : installed) {
        if (std::find(base.begin(), base.end(), installed_map.name) != base.end())
            continue;
        const bool from_registry = !installed_map.registry.empty() && installed_map.release > 0;
        bool include = false;
        bool final_omit = false;
        if (from_registry) {
            switch (catalogue_map_fits(installed_map, key)) {
            case CatalogueMapFit::fits:
                include = true;
                break;
            case CatalogueMapFit::unfit:
                // A recorded false is final. The files are not asked, so
                // they cannot overrule the catalogue.
                final_omit = true;
                break;
            case CatalogueMapFit::unlisted:
                // The catalogue has not listed this release yet. The file
                // check keeps a map that fits until that table arrives, and
                // a miss here is not final, so it is not logged.
                include = pack_map_fit(installed_map).fits();
                break;
            case CatalogueMapFit::unchecked:
                // The key is empty, or the table has no row for it. The
                // files decide, and a miss is final for this mod.
                include = pack_map_fit(installed_map).fits();
                final_omit = !include;
                break;
            }
        } else {
            include = pack_map_fit(installed_map).fits();
            final_omit = !include;
        }
        if (include)
            listed.push_back(installed_map.name);
        else if (final_omit)
            ++omitted[installed_map.id];
    }

    for (const auto& [id, count] : omitted) {
        if (count <= 0)
            continue;
        const bool said = std::any_of(
            state.omission_logged.begin(),
            state.omission_logged.end(),
            [&](const std::pair<std::string, std::string>& kept) {
                return kept.first == id && kept.second == key;
            }
        );
        if (said)
            continue;
        state.omission_logged.emplace_back(id, key);
        const char* verb = count == 1 ? " map does not fit " : " maps do not fit ";
        const std::string line =
            "open-annihilation: map pack " + id + ": " + std::to_string(count) + verb + shown_key;
        std::cerr << line << '\n';
        state.omission_lines.push_back(line);
    }

    state.listed = listed;
    eligible_map_names_ = std::move(base);
    for (const std::string& name : state.listed)
        eligible_map_names_.push_back(name);
    if (!first_map_name_.empty() &&
        std::find(eligible_map_names_.begin(), eligible_map_names_.end(), first_map_name_) ==
            eligible_map_names_.end()) {
        if (!eligible_map_names_.empty())
            first_map_name_ = eligible_map_names_.front();
        else
            first_map_name_.clear();
    }

    if (screen_ == Screen::map_selection && map_modal_.active) {
        std::string highlighted;
        if (modal_map_index_ >= 0 &&
            static_cast<std::size_t>(modal_map_index_) < bound_map_names_.size())
            highlighted = bound_map_names_[static_cast<std::size_t>(modal_map_index_)];
        std::vector<std::string> names = copy_map_names();
        try {
            map_modal::sort_map_names(names);
        } catch (const std::exception& error) {
            std::cerr << "open-annihilation: map list: " << error.what() << '\n';
            names = copy_map_names();
        }
        std::ignore =
            std::stable_partition(names.begin(), names.end(), [](const std::string& name) {
                return !pack::split_pack_map_name(name);
            });
        map_modal_.names = names;
        bind_map_names(names);
        if (!highlighted.empty()) {
            const auto row =
                std::find(bound_map_names_.begin(), bound_map_names_.end(), highlighted);
            if (row != bound_map_names_.end()) {
                const auto place = static_cast<std::ptrdiff_t>(row - bound_map_names_.begin());
                if (place >= 0 &&
                    place <= static_cast<std::ptrdiff_t>(std::numeric_limits<int16_t>::max()))
                    set_selected_map_index(static_cast<int16_t>(place));
            }
        }
    }

    oa::ui::frontend_multiplayer::multiplayer_reload_maps();
}

void Runtime::label_bound_maps() {
    namespace mp = oa::ui::frontend_multiplayer;

    // Copy each title before the next find. The first find may refresh the
    // index, which frees the list a pointer into it would still name.
    struct Held {
        std::string key;
        std::string title;
        std::string pack_name;
        bool pack = false;
    };

    std::vector<Held> held;
    held.reserve(bound_map_names_.size());
    for (const std::string& name : bound_map_names_) {
        Held row;
        row.key = name;
        if (pack::split_pack_map_name(name)) {
            row.pack = true;
            if (const PackMap* map = map_packs().find(name)) {
                row.title = map->title;
                row.pack_name = map->pack_name;
            }
        }
        held.push_back(std::move(row));
    }
    std::vector<mp::MapListFact> facts;
    facts.reserve(held.size());
    for (const Held& row : held)
        facts.push_back(mp::MapListFact{row.key, row.title, {}, row.pack_name, row.pack});
    bound_map_labels_ = mp::map_list_texts(facts);
    if (bound_map_labels_.size() != bound_map_names_.size())
        bound_map_labels_ = bound_map_names_;
}

void Runtime::note_pack_catalogue() {
    const std::optional<uint64_t> generation = content_catalogue_generation();
    if (!generation)
        return;
    auto& state = pack_map_state();
    if (state.catalogue_noted && state.catalogue_generation == *generation)
        return;
    state.catalogue_noted = true;
    state.catalogue_generation = *generation;
    refresh_listed_pack_maps();
}

} // namespace oa::app
