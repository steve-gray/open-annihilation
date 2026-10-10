// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// describe_oamap and check_map_fit. The manifest is read in package use.
// Each target is the store the game would build, and each map is mounted
// from the pack for that check alone.

#include "check_map.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/app/package_install/origin.hpp"
#include "oa/base/sha256.hpp"
#include "oa/data/map_fit/map_fit.hpp"
#include "oa/data/map_pack/manifest.hpp"
#include "oa/data/map_pack/map_name.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/formats/oamod.hpp"
#include "oa/formats/zip/stream.hpp"
#include "oa/platform/files.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::tool {
namespace {

namespace fs = std::filesystem;
namespace app = oa::app;
namespace install = oa::app::package_install;
namespace json = oa::formats::json;
namespace map_fit = oa::data::map_fit;
namespace map_pack = oa::data::map_pack;
namespace sha256 = oa::base::sha256;
namespace zip = oa::formats::zip;

/// The base game's catalogue key.
constexpr std::string_view base_key = "ta-3.1c";

/// Bytes hashed from the pack at a time.
constexpr std::size_t read_piece = std::size_t{1} << 16;

/// One target's result for one map.
struct TargetOutcome {
    std::string key{};  ///< the catalogue key
    bool checked{};     ///< the map was checked against this target
    map_fit::Fit fit{}; ///< the rules it broke; empty when it fits
};

/// One map and the targets it was checked against, in order.
struct MapOutcome {
    map_pack::MapEntry map{};             ///< the manifest entry
    std::vector<TargetOutcome> targets{}; ///< one per target that was reached
};

/// A file opened for a positioned read.
struct FilePos {
    std::FILE* stream{};
    oa::platform::Files files{};

    FilePos() = default;
    FilePos(const FilePos&) = delete;
    FilePos& operator=(const FilePos&) = delete;

    /// Closes the file.
    ~FilePos() {
        if (stream != nullptr)
            std::fclose(stream);
    }

    /// Returns the hooks a streamed zip read uses.
    ///
    /// @return the hooks; they read through this object, which must outlive them
    [[nodiscard]] zip::SourceHooks source() { return zip::SourceHooks{this, &read_at}; }

    /// Reads bytes at an offset.
    ///
    /// @param context the FilePos
    /// @param offset where the bytes start
    /// @param bytes where they are written
    /// @return true when every byte was read
    static bool read_at(void* context, uint64_t offset, std::span<uint8_t> bytes) {
        auto* file = static_cast<FilePos*>(context);
        if (file->stream == nullptr || file->files.seek == nullptr ||
            offset > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
            return false;
        auto* handle = reinterpret_cast<oa::platform::FileHandle*>(file->stream);
        if (file->files.seek(
                nullptr, handle, static_cast<int64_t>(offset), oa::platform::SeekOrigin::begin
            ) != 0)
            return false;
        std::size_t done = 0;
        while (done < bytes.size()) {
            const std::size_t got =
                std::fread(bytes.data() + done, 1, bytes.size() - done, file->stream);
            if (got == 0)
                return false;
            done += got;
        }
        return true;
    }
};

/// Drops a pack layer when the check of one map ends.
struct MountedLayer {
    oa::AssetStore& store; ///< the store the layer was mounted on
    bool on{};             ///< a layer is mounted

    /// Unmounts the layer.
    ~MountedLayer() {
        if (on)
            store.unmount_pack_layer();
    }
};

/// Returns a path's file name in UTF-8.
///
/// @param path the path
/// @return the file name
std::string file_name_of(const fs::path& path) {
    return app::path_to_utf8(path.filename());
}

/// Folds ASCII letters in a zip name to lower case.
///
/// @param text the name
/// @return the folded name
std::string fold_ascii(std::string_view text) {
    std::string folded;
    folded.reserve(text.size());
    for (const unsigned char byte : text) {
        folded.push_back(
            byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte - 'A' + 'a')
                                       : static_cast<char>(byte)
        );
    }
    return folded;
}

/// Makes a sentence one line.
///
/// @param text the text
/// @return the text, with line breaks turned into spaces
std::string one_line(std::string text) {
    for (char& character : text)
        if (character == '\n' || character == '\r')
            character = ' ';
    return text;
}

/// Returns the enumerator's name.
///
/// @param rule the rule
/// @return isolated, adds_only, new_names or complete
std::string_view rule_id(map_fit::Rule rule) noexcept {
    switch (rule) {
    case map_fit::Rule::isolated:
        return "isolated";
    case map_fit::Rule::adds_only:
        return "adds_only";
    case map_fit::Rule::new_names:
        return "new_names";
    case map_fit::Rule::complete:
        return "complete";
    }
    return "complete";
}

/// Writes one string member when the text is not empty.
///
/// @param[in,out] writer the object
/// @param key the member's name
/// @param text the text
void write_text(json::JsonWriter& writer, std::string_view key, std::string_view text) {
    if (text.empty())
        return;
    writer.key(key);
    writer.string(text);
}

/// Writes one fact as `key: value` when the text is not empty.
///
/// @param[in,out] output where the line is written
/// @param key the fact's name
/// @param text the text
void write_line(Output& output, std::string_view key, std::string_view text) {
    if (text.empty())
        return;
    output.out << key << ": " << text << '\n';
}

/// Reads oamap.yaml from a pack through the streamed reader.
///
/// @param pack the pack
/// @param[out] bytes the manifest; left empty when it cannot be read
/// @return true when the manifest was read
bool read_manifest_bytes(const fs::path& pack, std::vector<uint8_t>& bytes) {
    bytes.clear();
    std::error_code error;
    const auto size = fs::file_size(pack, error);
    if (error || size > static_cast<uintmax_t>(std::numeric_limits<uint64_t>::max()))
        return false;
    FilePos file;
    file.stream = oa::platform::open_file(pack, "rb");
    if (file.stream == nullptr)
        return false;
    file.files = oa::platform::stdio_files();
    zip::ZipError zip_error{};
    zip::StreamDirectory directory;
    if (!zip::read_stream_directory(
            file.source(), static_cast<uint64_t>(size), {}, directory, zip_error
        ))
        return false;
    const std::string wanted = fold_ascii(map_pack::manifest_file);
    const zip::StreamEntry* entry = nullptr;
    for (const zip::StreamEntry& candidate : directory.entries) {
        if (fold_ascii(candidate.name) == wanted) {
            entry = &candidate;
            break;
        }
    }
    if (entry == nullptr)
        return false;
    return zip::read_stream_entry(
        file.source(),
        static_cast<uint64_t>(size),
        *entry,
        oa::formats::oamod::max_input_bytes,
        bytes,
        zip_error
    );
}

/// Reads the manifest, appending one problem per broken rule.
///
/// @param bytes the manifest's bytes
/// @param[out] manifest the manifest, when it was read
/// @param[in,out] problems receives the sentences
/// @return true when the manifest was read
bool read_pack_manifest(
    std::span<const uint8_t> bytes, map_pack::Manifest& manifest, std::vector<std::string>& problems
) {
    std::vector<map_pack::Problem> found;
    if (map_pack::read_manifest(bytes, map_pack::ManifestUse::package, manifest, found))
        return true;
    if (found.empty())
        problems.emplace_back("oamap.yaml cannot be read.");
    for (const map_pack::Problem& problem : found)
        problems.push_back(map_pack::describe(problem));
    return false;
}

/// Writes requires.base and requires.engine when the manifest has them.
///
/// @param manifest the manifest
/// @param json the object; null writes text
/// @param[in,out] output receives the text when `json` is null
void write_requires(const map_pack::Manifest& manifest, json::JsonWriter* json, Output& output) {
    if (manifest.requires_base.empty() && manifest.requires_engine.empty())
        return;
    if (json == nullptr) {
        write_line(output, "requires.base", manifest.requires_base);
        write_line(output, "requires.engine", manifest.requires_engine);
        return;
    }
    json->key("requires");
    json->begin_object();
    write_text(*json, "base", manifest.requires_base);
    write_text(*json, "engine", manifest.requires_engine);
    json->end_object();
}

/// Writes packaging.revision, date and packager.
///
/// @param manifest the manifest
/// @param json the object; null writes text
/// @param[in,out] output receives the text when `json` is null
void write_packaging(const map_pack::Manifest& manifest, json::JsonWriter* json, Output& output) {
    const map_pack::Packaging& packaging = manifest.packaging;
    const bool has_revision = packaging.revision != 0;
    if (!has_revision && packaging.date.empty() && packaging.packager.empty())
        return;
    if (json == nullptr) {
        if (has_revision)
            output.out << "packaging.revision: " << packaging.revision << '\n';
        write_line(output, "packaging.date", packaging.date);
        write_line(output, "packaging.packager", packaging.packager);
        return;
    }
    json->key("packaging");
    json->begin_object();
    if (has_revision) {
        json->key("revision");
        json->integer(packaging.revision);
    }
    write_text(*json, "date", packaging.date);
    write_text(*json, "packager", packaging.packager);
    json->end_object();
}

/// The first eight hex digits of the pack's SHA-256, after its id.
///
/// @param pack the pack
/// @param id the pack's id
/// @return the layer's label
std::string layer_label(const fs::path& pack, std::string_view id) {
    std::string label(id);
    std::FILE* stream = oa::platform::open_file(pack, "rb");
    if (stream == nullptr)
        return label;
    sha256::Hasher hasher{};
    std::vector<uint8_t> buffer(read_piece);
    bool failed = false;
    while (true) {
        const std::size_t got = std::fread(buffer.data(), 1, buffer.size(), stream);
        if (got > 0)
            sha256::update(hasher, std::span<const uint8_t>(buffer.data(), got));
        if (got < buffer.size()) {
            failed = std::ferror(stream) != 0;
            break;
        }
    }
    std::fclose(stream);
    if (failed)
        return label;
    const auto hex = sha256::to_hex(sha256::finish(hasher));
    label.push_back(' ');
    label.append(hex.data(), 8);
    return label;
}

/// The catalogue key of one mod.
///
/// A key given with the folder is kept. Otherwise a catalogue origin record
/// supplies `<id>@<release>`. With neither, the profile's id is used and a
/// warning says it has no release.
///
/// @param target the mod
/// @param install the mod as the game would load it
/// @param[in,out] output receives the warning
/// @return the key
std::string mod_key(const MapTarget& target, const app::GameInstall& install, Output& output) {
    if (!target.key.empty())
        return target.key;
    if (const std::optional<install::Origin> origin = install::read_origin(target.folder)) {
        if (origin->kind == install::OriginKind::catalogue && !origin->catalogue_id.empty() &&
            origin->release >= 1)
            return origin->catalogue_id + "@" + std::to_string(origin->release);
    }
    std::string id = install.profile ? install.profile->id : std::string{};
    if (id.empty())
        id = file_name_of(target.folder);
    output.err << "warning: " << id
               << " has no release, so its catalogue key is the profile's id\n";
    return id;
}

/// Says why a target cannot be played, naming it.
///
/// @param key the target's key, or its folder when it has none yet
/// @param install what inspecting it found
/// @return the sentence
std::string target_problem(std::string_view key, const app::GameInstall& install) {
    std::string detail;
    if (!install.profile_errors.empty()) {
        detail = install.profile_errors.front();
        for (std::size_t index = 1; index < install.profile_errors.size(); ++index)
            detail += "; " + install.profile_errors[index];
    } else {
        detail = app::describe_install_problem(install);
    }
    return one_line(std::string(key) + " cannot be used: " + detail);
}

/// Writes one map's line, and each failure under it.
///
/// @param[in,out] output where the lines are written
/// @param map the map's `stem@id` name
/// @param key the target's key
/// @param fit the result
void write_fit_line(
    Output& output, std::string_view map, std::string_view key, const map_fit::Fit& fit
) {
    output.out << map << "  " << key << "  " << (fit.fits() ? "fits" : "does not fit") << '\n';
    if (fit.fits())
        return;
    for (const map_fit::Failure& failure : fit.failures)
        output.out << "  " << map_fit::describe(failure) << '\n';
}

/// Writes the maps member.
///
/// @param[in,out] writer the object
/// @param id the pack's id
/// @param maps the maps and their results
void write_maps(
    json::JsonWriter& writer, std::string_view id, const std::vector<MapOutcome>& maps
) {
    writer.key("maps");
    writer.begin_array();
    for (const MapOutcome& outcome : maps) {
        const map_pack::MapEntry& map = outcome.map;
        writer.begin_object();
        writer.key("map");
        writer.string(map_pack::pack_map_name(map.stem, id));
        writer.key("name");
        writer.string(map.title);
        writer.key("players");
        writer.integer(static_cast<int64_t>(map.players));
        writer.key("size");
        writer.string(map.size);
        write_text(writer, "preview", map.preview);
        writer.key("compatible");
        writer.begin_object();
        for (const TargetOutcome& target : outcome.targets) {
            if (!target.checked)
                continue;
            writer.key(target.key);
            writer.boolean(target.fit.fits());
        }
        writer.end_object();
        writer.key("failures");
        writer.begin_object();
        for (const TargetOutcome& target : outcome.targets) {
            if (!target.checked || target.fit.fits())
                continue;
            writer.key(target.key);
            writer.begin_array();
            for (const map_fit::Failure& failure : target.fit.failures) {
                writer.begin_object();
                writer.key("rule");
                writer.string(rule_id(failure.rule));
                writer.key("subject");
                writer.string(failure.subject);
                writer.key("detail");
                writer.string(failure.detail);
                writer.end_object();
            }
            writer.end_array();
        }
        writer.end_object();
        writer.end_object();
    }
    writer.end_array();
}

/// Checks every map against one target.
///
/// @param request the pack and the game
/// @param folder the mod folder; empty for the base game
/// @param given_key the key from the command; empty to take it from the folder
/// @param id the pack's id
/// @param label the mounted layer's name
/// @param manifest the manifest
/// @param[in,out] maps receives each map's result for this target
/// @param json null when the lines are written as the maps are checked
/// @param[in,out] output receives those lines, and a warning when the key has no release
/// @param[in,out] problems receives a base-game failure and a target that cannot be used
/// @return false when the target cannot be used and the remaining targets should stop
bool check_target(
    const MapCheckRequest& request,
    const fs::path& folder,
    const std::string& given_key,
    std::string_view id,
    const std::string& label,
    const map_pack::Manifest& manifest,
    std::vector<MapOutcome>& maps,
    json::JsonWriter* json,
    Output& output,
    std::vector<std::string>& problems
) {
    const bool base = folder.empty();
    app::ModChoice choice{};
    if (!base)
        choice.folder = folder;
    const app::GameInstall install =
        app::inspect_game_install(request.game_dir, {}, app::demo_1997, choice);
    MapTarget named{folder, given_key};
    const std::string key = base ? std::string(base_key) : mod_key(named, install, output);
    if (!install.profile_errors.empty() || !install.problem.empty() || !install.folder ||
        install.archives.empty() || install.folders.empty()) {
        const std::string who = key.empty() ? file_name_of(folder) : key;
        problems.push_back(target_problem(who, install));
        return false;
    }

    try {
        oa::AssetStore store(install.folders);
        for (const fs::path& candidate : install.archives) {
            const fs::path archive =
                candidate.is_absolute() ? candidate : request.game_dir / candidate;
            store.mount(archive);
        }
        const oa::data::defs::DataLayout layout = app::data_layout_of(install.profile.get());
        const map_fit::GameNames names = map_fit::collect_game_names(store, layout);
        for (std::size_t index = 0; index < maps.size() && index < manifest.maps.size(); ++index) {
            const map_pack::MapEntry& map = manifest.maps[index];
            map_fit::Fit fit;
            {
                MountedLayer layer{store};
                oa::PackLayerSpec spec;
                spec.kind = oa::PackLayerKind::zip;
                spec.location = request.pack;
                spec.files = map_fit::layer_files(map, id);
                spec.label = label;
                std::string error;
                if (!store.mount_pack_layer(spec, &error)) {
                    fit.failures.push_back(
                        {map_fit::Rule::complete, map_pack::pack_map_name(map.stem, id), error}
                    );
                } else {
                    layer.on = true;
                    const std::unique_ptr<map_fit::MapFiles> files =
                        map_fit::mounted_map_files(store);
                    fit = map_fit::check_map(store, names, *files, map, id, layout);
                }
            }
            if (base && !fit.fits()) {
                problems.push_back(
                    map_pack::pack_map_name(map.stem, id) + " does not fit " +
                    std::string(base_key) + ": " + map_fit::describe(fit.failures.front())
                );
            }
            if (json == nullptr)
                write_fit_line(output, map_pack::pack_map_name(map.stem, id), key, fit);
            maps[index].targets.push_back(TargetOutcome{key, true, std::move(fit)});
        }
    } catch (const std::exception& error) {
        const char* what = error.what();
        problems.push_back(
            key + " cannot be used: " + (what == nullptr ? std::string("it cannot be read") : what)
        );
        return false;
    }
    return true;
}

} // namespace

bool describe_oamap(
    std::span<const uint8_t> manifest_bytes,
    json::JsonWriter* json,
    Output& output,
    std::vector<std::string>& problems
) {
    map_pack::Manifest manifest;
    if (!read_pack_manifest(manifest_bytes, manifest, problems))
        return false;
    if (json == nullptr) {
        write_line(output, "summary", manifest.description);
        write_line(output, "author", manifest.author.name);
        write_line(output, "homepage", manifest.homepage);
        if (!manifest.tags.empty()) {
            json::JsonWriter tags;
            tags.begin_array();
            for (const std::string& tag : manifest.tags)
                tags.string(tag);
            tags.end_array();
            output.out << "tags: " << tags.text() << '\n';
        }
    } else {
        write_text(*json, "summary", manifest.description);
        write_text(*json, "author", manifest.author.name);
        write_text(*json, "homepage", manifest.homepage);
        if (!manifest.tags.empty()) {
            json->key("tags");
            json->begin_array();
            for (const std::string& tag : manifest.tags)
                json->string(tag);
            json->end_array();
        }
    }
    write_requires(manifest, json, output);
    write_packaging(manifest, json, output);
    return true;
}

bool check_map_fit(
    const MapCheckRequest& request,
    json::JsonWriter* json,
    Output& output,
    std::vector<std::string>& problems
) {
    const std::size_t before = problems.size();
    std::vector<uint8_t> bytes;
    map_pack::Manifest manifest;
    if (!read_manifest_bytes(request.pack, bytes) ||
        !read_pack_manifest(bytes, manifest, problems)) {
        return false;
    }
    std::vector<MapOutcome> maps(manifest.maps.size());
    for (std::size_t index = 0; index < manifest.maps.size(); ++index)
        maps[index].map = manifest.maps[index];
    const std::string label = layer_label(request.pack, manifest.id);
    if (!check_target(
            request, {}, {}, manifest.id, label, manifest, maps, json, output, problems
        )) {
        if (json != nullptr)
            write_maps(*json, manifest.id, maps);
        return false;
    }
    for (const MapTarget& mod : request.mods) {
        if (!check_target(
                request,
                mod.folder,
                mod.key,
                manifest.id,
                label,
                manifest,
                maps,
                json,
                output,
                problems
            ))
            break;
    }
    if (json != nullptr)
        write_maps(*json, manifest.id, maps);
    return problems.size() == before;
}

} // namespace oa::tool
