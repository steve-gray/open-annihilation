// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Map packs installed under Maps: each manifest read once per key, and one
// map checked against the base game.

#include "map_packs.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/app/package_install/origin.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/base/sha256.hpp"
#include "oa/data/defs/layout.hpp"
#include "oa/data/map_pack/map_name.hpp"
#include "oa/formats/hpi.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace oa::app {

namespace fs = std::filesystem;
namespace pack = oa::data::map_pack;
namespace fit = oa::data::map_fit;

namespace {

/// Returns a digest as 64 lower-case hex digits.
///
/// @param digest the digest
/// @return the text
std::string hex_of(const oa::base::sha256::Digest& digest) {
    const auto text = oa::base::sha256::to_hex(digest);
    return std::string(text.data(), text.size());
}

/// Reads a file whole.
///
/// @param file the file
/// @param[out] bytes its bytes
/// @return true when the file was read
bool read_bytes(const fs::path& file, std::vector<uint8_t>& bytes) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    bytes.assign(std::istreambuf_iterator<char>{in}, {});
    return static_cast<bool>(in) || in.eof();
}

/// Finds a relative path in a folder, each part without case.
///
/// @param folder the folder
/// @param relative the path, '/' between parts
/// @return the file; nothing when a part is missing
std::optional<fs::path> find_entry(const fs::path& folder, std::string_view relative) {
    fs::path at = folder;
    std::string part;
    const auto step = [&](std::string_view name) {
        if (name.empty())
            return true;
        const auto found = entry_without_case(at, name);
        if (!found)
            return false;
        at = *found;
        return true;
    };
    for (const char character : relative) {
        if (character == '/' || character == '\\') {
            if (!step(part))
                return std::nullopt;
            part.clear();
        } else {
            part.push_back(character);
        }
    }
    if (!step(part))
        return std::nullopt;
    return at;
}

/// Returns a file's size, or 0 when it cannot be read.
///
/// @param file the file
/// @return its size in bytes
uint64_t size_of(const fs::path& file) {
    std::error_code error;
    const auto size = fs::file_size(file, error);
    if (error)
        return 0;
    return static_cast<uint64_t>(size);
}

/// A fit that failed before a map could be checked.
///
/// @param detail why
/// @return the fit
fit::Fit store_failure(std::string detail) {
    fit::Fit unfit;
    fit::Failure failure{};
    failure.rule = fit::Rule::complete;
    failure.subject = "the base game";
    failure.detail = std::move(detail);
    unfit.failures.push_back(std::move(failure));
    return unfit;
}

/// One map of a parsed manifest.
///
/// @param folder the pack's folder
/// @param manifest the manifest
/// @param map the map
/// @param key the pack's key
/// @param registry the catalogue registry; empty otherwise
/// @param release the catalogue release; 0 otherwise
/// @return the map
PackMap map_of(
    const fs::path& folder,
    const pack::Manifest& manifest,
    const pack::MapEntry& map,
    const std::string& key,
    const std::string& registry,
    int64_t release
) {
    PackMap listed{};
    listed.name = pack::pack_map_name(map.stem, manifest.id);
    listed.id = manifest.id;
    listed.stem = map.stem;
    listed.title = map.title;
    listed.description = map.description;
    listed.size = map.size;
    listed.pack_name = manifest.name;
    listed.pack_version = manifest.version;
    listed.sha256 = key;
    listed.registry = registry;
    listed.players = map.players;
    listed.release = release;
    listed.folder = folder;
    listed.files = map.files;
    const std::string terrain = "maps/" + map.stem + ".tnt";
    if (const auto file = find_entry(folder, terrain))
        listed.terrain_bytes = size_of(*file);
    if (!map.preview.empty())
        if (const auto file = find_entry(folder, map.preview))
            listed.preview = *file;
    return listed;
}

} // namespace

MapPacks::MapPacks(fs::path maps_folder) : maps_folder_(std::move(maps_folder)) {
}

void MapPacks::set_manifest_watch(void (*watch)(void*), void* context) {
    watch_ = watch;
    watch_context_ = context;
}

void MapPacks::ensure() const {
    if (!ready_)
        const_cast<MapPacks*>(this)->refresh();
}

std::span<const PackMap> MapPacks::maps() const {
    ensure();
    return maps_;
}

const PackMap* MapPacks::find(std::string_view name) const {
    ensure();
    for (const PackMap& map : maps_)
        if (map.name == name)
            return &map;
    return nullptr;
}

std::vector<std::string> MapPacks::problems() const {
    ensure();
    return problems_;
}

void MapPacks::refresh() {
    maps_.clear();
    problems_.clear();
    ready_ = true;
    std::error_code error;
    if (!fs::is_directory(maps_folder_, error)) {
        cache_.clear();
        return;
    }
    std::vector<std::string> seen;
    for (fs::directory_iterator
             entry{maps_folder_, fs::directory_options::skip_permission_denied, error},
         end;
         !error && entry != end;
         entry.increment(error)) {
        const fs::path path = entry->path();
        const std::string name = path_to_utf8(path.filename());
        if (name.empty() || name.front() == '.')
            continue;
        std::error_code status_error;
        const fs::file_status status = fs::symlink_status(path, status_error);
        if (status_error || !fs::is_directory(status))
            continue;
        seen.push_back(name);
        try {
            const auto origin = package_install::read_origin(path);
            std::string key;
            std::vector<uint8_t> yaml;
            bool yaml_read = false;
            if (origin && origin->sha256) {
                key = hex_of(*origin->sha256);
            } else {
                const auto manifest_path = entry_without_case(path, pack::manifest_file);
                if (!manifest_path || !read_bytes(*manifest_path, yaml)) {
                    problems_.push_back(name + ": its oamap.yaml cannot be read");
                    continue;
                }
                yaml_read = true;
                key = hex_of(oa::base::sha256::digest_of(yaml));
            }
            const auto cached = std::find_if(cache_.begin(), cache_.end(), [&](const auto& item) {
                return item.first == name;
            });
            if (cached != cache_.end() && cached->second.key == key) {
                maps_.insert(maps_.end(), cached->second.maps.begin(), cached->second.maps.end());
                continue;
            }
            if (!yaml_read) {
                const auto manifest_path = entry_without_case(path, pack::manifest_file);
                if (!manifest_path || !read_bytes(*manifest_path, yaml)) {
                    problems_.push_back(name + ": its oamap.yaml cannot be read");
                    if (cached != cache_.end())
                        cache_.erase(cached);
                    continue;
                }
            }
            if (watch_ != nullptr)
                watch_(watch_context_);
            pack::Manifest manifest;
            std::vector<pack::Problem> parse_problems;
            if (!pack::read_manifest(yaml, pack::ManifestUse::package, manifest, parse_problems)) {
                std::string reason = name + ":";
                if (parse_problems.empty())
                    reason += " its oamap.yaml cannot be read";
                for (const pack::Problem& one : parse_problems)
                    reason += " " + pack::describe(one);
                problems_.push_back(std::move(reason));
                if (cached != cache_.end())
                    cache_.erase(cached);
                continue;
            }
            std::string registry;
            int64_t release = 0;
            if (origin && origin->kind == package_install::OriginKind::catalogue) {
                registry = origin->registry;
                release = origin->release;
            }
            Cached kept{};
            kept.key = key;
            for (const pack::MapEntry& map : manifest.maps)
                kept.maps.push_back(map_of(path, manifest, map, key, registry, release));
            maps_.insert(maps_.end(), kept.maps.begin(), kept.maps.end());
            if (cached != cache_.end())
                cached->second = std::move(kept);
            else
                cache_.emplace_back(name, std::move(kept));
        } catch (const std::exception&) {
            problems_.push_back(name + ": its oamap.yaml cannot be read");
        }
    }
    std::erase_if(cache_, [&](const auto& item) {
        return std::find(seen.begin(), seen.end(), item.first) == seen.end();
    });
    std::sort(maps_.begin(), maps_.end(), [](const PackMap& left, const PackMap& right) {
        return left.name < right.name;
    });
}

fit::Fit check_base_game_fit(
    const fs::path& game_dir, const fs::path& folder, const pack::MapEntry& map, std::string_view id
) {
    try {
        const GameInstall install = inspect_game_install(game_dir);
        if (!usable(install) || install.folders.empty())
            return store_failure("the base game cannot be read");
        oa::AssetStore store(install.folders);
        for (const fs::path& archive : install.archives) {
            std::string error;
            if (!store.try_mount(archive, &error))
                return store_failure(
                    error.empty() ? "an archive of the base game cannot be read" : error
                );
        }
        oa::PackLayerSpec spec{};
        spec.kind = oa::PackLayerKind::folder;
        spec.location = folder;
        spec.files = fit::layer_files(map, id);
        spec.label = std::string(id);
        std::string error;
        if (!store.mount_pack_layer(std::move(spec), &error))
            return store_failure(error.empty() ? "the map's files cannot be read" : error);
        const oa::data::defs::DataLayout layout{};
        const fit::GameNames names = fit::collect_game_names(store, layout);
        const std::unique_ptr<fit::MapFiles> files = fit::mounted_map_files(store);
        if (!files)
            return store_failure("the map's files cannot be read");
        return fit::check_map(store, names, *files, map, id, layout);
    } catch (const std::exception& error) {
        return store_failure(error.what());
    } catch (...) {
        return store_failure("the base game cannot be read");
    }
}

} // namespace oa::app
