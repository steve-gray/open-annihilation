// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A map folder becomes a .oamap. The index is computed from each map's OTA
// and TNT: feature sections in the folder, then each section's sprite or
// model, burn weapon and links. The manifest is written first, then every
// other file in byte order of its path, to a sibling .part file that is
// renamed into place only after the archive reads back.

#include "pack_map.hpp"

#include "pack_names.hpp"

#include "oa/app/package_install.hpp"
#include "oa/app/package_install/oamod.hpp"
#include "oa/base/sha256.hpp"
#include "oa/base/text/line_break.hpp"
#include "oa/data/defs/layout.hpp"
#include "oa/data/map_fit/map_fit.hpp"
#include "oa/data/map_pack/manifest.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/formats/oamod.hpp"
#include "oa/formats/ota.hpp"
#include "oa/formats/png.hpp"
#include "oa/formats/tdf.hpp"
#include "oa/formats/tnt.hpp"
#include "oa/formats/zip/stream.hpp"
#include "oa/formats/zip/writer.hpp"
#include "oa/platform/files.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace oa::tool {
namespace {

namespace fs = std::filesystem;
namespace zip = oa::formats::zip;
namespace install = oa::app::package_install;
namespace sha256 = oa::base::sha256;
namespace map_pack = oa::data::map_pack;
namespace map_fit = oa::data::map_fit;
namespace tdf = oa::formats::tdf;
namespace tnt = oa::formats::tnt;
namespace ota = oa::formats::ota;
namespace png = oa::formats::png;

/// Bytes read from one file and handed to the writer at a time.
constexpr std::size_t read_piece = std::size_t{1} << 20;

/// How an entry the walk met is left out of the index.
enum class Omit : uint8_t {
    none,  ///< indexed
    entry, ///< this entry is left out; a folder is still entered
    tree,  ///< this entry and everything under it are left out
};

/// One regular file in the source folder.
struct Source {
    std::string name; ///< path inside the folder, '/' between parts
    fs::path path;    ///< where its bytes are read
    uint64_t bytes{}; ///< uncompressed size
};

/// One file the package will hold. Memory is the bytes when path is empty.
struct Piece {
    detail::Item item;
    std::vector<uint8_t> memory;
};

/// The minimap share a preview keeps, at the minimap's own resolution.
struct Crop {
    uint32_t width{};
    uint32_t height{};
    std::vector<uint8_t> pixels;
};

/// One map after its files have been walked, before its preview is encoded.
struct BuiltMap {
    map_pack::MapEntry entry;
    std::vector<std::string> needs;
    std::optional<Crop> crop;
};

using Item = detail::Item;

/// A file opened for a positioned read or write.
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
};

/// Removes a path unless release() was called.
class RemoveUnless {
  public:

    /// Takes a path to remove.
    ///
    /// @param path the path
    explicit RemoveUnless(fs::path path) : path_(std::move(path)) {}

    RemoveUnless(const RemoveUnless&) = delete;
    RemoveUnless& operator=(const RemoveUnless&) = delete;

    /// Removes the path, when it is still held.
    ~RemoveUnless() { remove(); }

    /// Leaves the path in place.
    void release() { path_.clear(); }

    /// Removes the path now.
    void remove() {
        if (path_.empty())
            return;
        std::error_code error;
        fs::remove(path_, error);
        path_.clear();
    }

  private:

    fs::path path_{};
};

/// Returns a path's UTF-8 spelling, with '/' between parts.
std::string utf8_of(const fs::path& path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

/// Returns a text with its ASCII letters lowered.
std::string folded(std::string_view text) {
    std::string lowered(text);
    for (char& character : lowered)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    return lowered;
}

/// Tells whether left sorts before right in unsigned byte order.
bool bytes_before(std::string_view left, std::string_view right) {
    const auto* left_bytes = reinterpret_cast<const uint8_t*>(left.data());
    const auto* right_bytes = reinterpret_cast<const uint8_t*>(right.data());
    return std::lexicographical_compare(
        left_bytes, left_bytes + left.size(), right_bytes, right_bytes + right.size()
    );
}

/// Returns the first '/'-separated part of a relative path.
std::string_view first_part(std::string_view path) {
    return path.substr(0, path.find('/'));
}

/// Returns the last '/'-separated part of a relative path.
std::string_view last_part(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

/// Tells whether any part of a path is `folded_name`.
bool any_part_is(std::string_view path, std::string_view folded_name) {
    std::size_t start = 0;
    while (start < path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::string_view part = slash == std::string_view::npos
                                          ? path.substr(start)
                                          : path.substr(start, slash - start);
        if (folded(part) == folded_name)
            return true;
        if (slash == std::string_view::npos)
            return false;
        start = slash + 1;
    }
    return false;
}

/// Says whether a relative path is one the walk leaves out.
///
/// __MACOSX and .backup are left out only as the first part. .DS_Store and
/// AppleDouble names are left out at any depth. .git and .oa-origin.yaml are
/// left out at any depth.
Omit omit_of(std::string_view relative) {
    const std::string first = folded(first_part(relative));
    if (first == "__macosx" || first == ".backup")
        return Omit::tree;
    if (any_part_is(relative, ".git") || any_part_is(relative, ".oa-origin.yaml"))
        return Omit::tree;
    const std::string last = folded(last_part(relative));
    if (last == ".ds_store" || last.starts_with("._"))
        return Omit::entry;
    return Omit::none;
}

/// Returns the path of an entry inside the folder, or refuses one that is not.
std::string relative_of(const fs::path& folder, const fs::path& path) {
    const std::string text = utf8_of(path.lexically_relative(folder));
    if (text.empty() || text == "." || text == ".." || text.starts_with("../") ||
        text.starts_with("/"))
        throw Failure("the folder holds a path outside it");
    return text;
}

/// Tells whether a file's extension is one that is stored, without case.
bool stored_extension(std::string_view name) {
    const std::string_view base = last_part(name);
    const std::size_t dot = base.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 >= base.size())
        return false;
    const std::string extension = folded(base.substr(dot + 1));
    static constexpr std::string_view stored[] = {
        "png",
        "jpg",
        "jpeg",
        "gif",
        "ogg",
        "mp3",
        "zip",
        "gz",
        "bz2",
        "xz",
        "7z",
        "oamod",
        "oalang",
        "oamap",
    };
    for (const std::string_view one : stored)
        if (extension == one)
            return true;
    return false;
}

/// Returns how an item is stored.
zip::Method method_of(const Item& item) {
    if (item.folder || item.bytes == 0 || stored_extension(item.name))
        return zip::Method::stored;
    return zip::Method::deflated;
}

/// Tells whether a file name ends in an extension, compared without case.
bool ends_with_extension(std::string_view name, std::string_view extension) {
    return name.size() > extension.size() && folded(name).ends_with(extension);
}

/// Tells whether an OTA schema type is Network 1, 2, 3 or 4.
bool network_schema(std::string_view type) {
    const std::string folded_type = folded(type);
    for (const std::string_view name : ota::multiplayer_schema_types)
        if (folded_type == folded(name))
            return true;
    return false;
}

/// Returns a map side in the game's units of 512 pixels, at least 1.
uint32_t map_side(uint32_t attribute_cells) {
    const uint32_t side = attribute_cells / 32;
    return side < 1 ? 1 : side;
}

/// Writes bytes at an offset, seeking with the 64-bit file boundary.
bool write_at(void* context, uint64_t offset, std::span<const uint8_t> bytes) {
    auto& file = *static_cast<FilePos*>(context);
    auto* handle = reinterpret_cast<oa::platform::FileHandle*>(file.stream);
    if (file.stream == nullptr ||
        offset > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        file.files.seek(
            nullptr, handle, static_cast<int64_t>(offset), oa::platform::SeekOrigin::begin
        ) != 0)
        return false;
    std::size_t done = 0;
    while (done < bytes.size()) {
        const std::size_t wrote =
            std::fwrite(bytes.data() + done, 1, bytes.size() - done, file.stream);
        if (wrote == 0)
            return false;
        done += wrote;
    }
    return true;
}

/// Reads bytes at an offset.
bool read_at(void* context, uint64_t offset, std::span<uint8_t> bytes) {
    auto& file = *static_cast<FilePos*>(context);
    auto* handle = reinterpret_cast<oa::platform::FileHandle*>(file.stream);
    if (file.stream == nullptr ||
        offset > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        file.files.seek(
            nullptr, handle, static_cast<int64_t>(offset), oa::platform::SeekOrigin::begin
        ) != 0)
        return false;
    std::size_t done = 0;
    while (done < bytes.size()) {
        const std::size_t read =
            std::fread(bytes.data() + done, 1, bytes.size() - done, file.stream);
        if (read == 0)
            return false;
        done += read;
    }
    return true;
}

/// Returns a zip failure's text.
std::string zip_failure(const zip::ZipError& error) {
    std::string text = zip::zip_status_message(error.status);
    if (!error.entry.empty()) {
        text += ": ";
        text += error.entry;
    }
    return text;
}

/// Opens a file for reading and measures it.
uint64_t open_read(const fs::path& path, FilePos& file) {
    file.stream = oa::platform::open_file(path, "rb");
    if (file.stream == nullptr)
        throw Failure("cannot read " + utf8_of(path));
    file.files = oa::platform::stdio_files();
    auto* handle = reinterpret_cast<oa::platform::FileHandle*>(file.stream);
    if (file.files.seek(nullptr, handle, 0, oa::platform::SeekOrigin::end) != 0)
        throw Failure("cannot read " + utf8_of(path));
    const int64_t end = file.files.tell(nullptr, handle);
    if (end < 0)
        throw Failure("cannot read " + utf8_of(path));
    if (file.files.seek(nullptr, handle, 0, oa::platform::SeekOrigin::begin) != 0)
        throw Failure("cannot read " + utf8_of(path));
    return static_cast<uint64_t>(end);
}

/// Reads a whole file.
std::vector<uint8_t> read_file(const fs::path& path) {
    FilePos file;
    const uint64_t measured = open_read(path, file);
    if (measured > std::numeric_limits<std::size_t>::max())
        throw Failure("cannot read " + utf8_of(path));
    std::vector<uint8_t> buffer(static_cast<std::size_t>(measured));
    if (measured > 0 && std::fread(buffer.data(), 1, buffer.size(), file.stream) != buffer.size())
        throw Failure("cannot read " + utf8_of(path));
    return buffer;
}

/// Reads a manifest file, refusing one over 256 KiB.
std::vector<uint8_t> read_manifest_bytes(const Source& manifest) {
    if (manifest.bytes > oa::formats::oamod::max_input_bytes)
        throw Failure(manifest.name + " is larger than 256 KiB");
    const std::vector<uint8_t> bytes = read_file(manifest.path);
    if (bytes.size() != manifest.bytes)
        throw Failure("cannot read " + manifest.name);
    return bytes;
}

/// Prints each manifest problem on its own line.
void print_problems(Output& output, const std::vector<map_pack::Problem>& problems) {
    for (const map_pack::Problem& problem : problems)
        output.err << map_pack::describe(problem) << '\n';
}

/// Renames part onto output. On Windows a file that exists is removed first.
void replace_output(const fs::path& part, const fs::path& output) {
#ifdef _WIN32
    std::error_code removed;
    fs::remove(output, removed);
#endif
    std::error_code error;
    fs::rename(part, output, error);
    if (error)
        throw Failure("cannot replace " + utf8_of(output) + ": " + error.message());
}

/// Indexes the folder's regular files, refusing a link, a special file or a case clash.
std::map<std::string, Source> index_folder(const fs::path& folder) {
    std::error_code error;
    const fs::file_status root = fs::symlink_status(folder, error);
    if (error)
        throw Failure("cannot read the folder: " + error.message());
    if (install::is_link_or_junction(folder))
        throw Failure("the folder is a link");
    if (!fs::is_directory(root))
        throw Failure("the folder is not a folder");

    std::map<std::string, Source> files;
    fs::recursive_directory_iterator cursor(folder, fs::directory_options::none, error);
    if (error)
        throw Failure("cannot read the folder: " + error.message());
    const fs::recursive_directory_iterator end;
    while (cursor != end) {
        const fs::directory_entry& entry = *cursor;
        const std::string relative = relative_of(folder, entry.path());
        if (install::is_link_or_junction(entry.path()))
            throw Failure("the folder holds a link: " + relative);
        const fs::file_status status = entry.symlink_status(error);
        if (error)
            throw Failure("cannot read " + relative + ": " + error.message());
        const bool directory = fs::is_directory(status);
        if (!directory && !fs::is_regular_file(status))
            throw Failure("the folder holds a special file: " + relative);
        const Omit omit = omit_of(relative);
        if (omit == Omit::tree)
            cursor.disable_recursion_pending();
        if (omit == Omit::none && !directory) {
            const auto size = entry.file_size(error);
            if (error)
                throw Failure("cannot read " + relative + ": " + error.message());
            const std::string key = folded(relative);
            const auto clash = files.find(key);
            if (clash != files.end())
                throw Failure(
                    "the folder holds two names that differ only in case: " + clash->second.name +
                    " and " + relative
                );
            Source source;
            source.name = relative;
            source.path = entry.path();
            source.bytes = static_cast<uint64_t>(size);
            files.emplace(key, std::move(source));
        }
        cursor.increment(error);
        if (error)
            throw Failure("cannot read the folder: " + error.message());
    }
    return files;
}

/// Finds a source file by its folded path.
const Source*
find_source(const std::map<std::string, Source>& files, std::string_view folded_path) {
    const auto found = files.find(std::string{folded_path});
    return found == files.end() ? nullptr : &found->second;
}

/// Returns the stems to pack, in the order their entries are written.
std::vector<std::string>
choose_stems(const map_pack::Manifest& manifest, const std::map<std::string, Source>& files) {
    if (!manifest.maps.empty()) {
        std::vector<std::string> stems;
        stems.reserve(manifest.maps.size());
        for (const map_pack::MapEntry& map : manifest.maps)
            stems.push_back(map.stem);
        return stems;
    }
    std::vector<std::string> discovered;
    for (const auto& [key, source] : files) {
        if (!key.starts_with("maps/") || !key.ends_with(".ota"))
            continue;
        if (key.find('/', 5) != std::string::npos)
            continue;
        const std::vector<uint8_t> bytes = read_file(source.path);
        const std::string text(bytes.begin(), bytes.end());
        const ota::ParseResult parsed = ota::parse(text);
        if (!parsed.ok() || !parsed.metadata)
            continue;
        bool played = false;
        for (const ota::Schema& schema : parsed.metadata->schemas) {
            if (network_schema(schema.type) && !schema.start_positions.empty())
                played = true;
        }
        if (played)
            discovered.push_back(source.name);
    }
    std::sort(
        discovered.begin(),
        discovered.end(),
        [](const std::string& left, const std::string& right) { return bytes_before(left, right); }
    );
    std::vector<std::string> stems;
    for (const std::string& path : discovered) {
        const std::string_view base = last_part(path);
        constexpr std::string_view extension = ".ota";
        stems.emplace_back(base.substr(0, base.size() - extension.size()));
    }
    return stems;
}

/// Requires a listed map's OTA and TNT, and returns them in disk spelling.
std::pair<const Source*, const Source*>
require_map_files(const std::map<std::string, Source>& files, const std::string& stem) {
    const std::string ota_key = folded("maps/" + stem + ".ota");
    const std::string tnt_key = folded("maps/" + stem + ".tnt");
    const Source* ota_file = find_source(files, ota_key);
    const Source* tnt_file = find_source(files, tnt_key);
    if (ota_file == nullptr)
        throw Failure("the map '" + stem + "' has no maps/" + stem + ".ota");
    if (tnt_file == nullptr)
        throw Failure("the map '" + stem + "' has no maps/" + stem + ".tnt");
    return {ota_file, tnt_file};
}

/// One feature section and the file that defined it.
struct Section {
    std::string name;
    std::string file;
    const tdf::Block* block{};
};

/// Parses every feature TDF once. A section defined twice stops the pack.
std::map<std::string, Section> feature_index(
    const std::map<std::string, Source>& files, std::vector<tdf::OwnedDocument>& documents
) {
    std::vector<const Source*> td_files;
    for (const auto& [key, source] : files) {
        if (key.starts_with("features/") && key.ends_with(".tdf"))
            td_files.push_back(&source);
    }
    documents.reserve(td_files.size());
    std::map<std::string, Section> sections;
    for (const Source* source : td_files) {
        const std::vector<uint8_t> bytes = read_file(source->path);
        const std::string text(bytes.begin(), bytes.end());
        tdf::OwnedDocument document;
        tdf::ParseError error{};
        if (!document.parse(text, &error))
            throw Failure(source->name + " could not be read: " + tdf::describe(error));
        documents.push_back(std::move(document));
        const tdf::Block* root = documents.back().root();
        for (uint32_t index = 0; index < tdf::child_count(root); ++index) {
            const tdf::Block* child = tdf::child_at(root, index);
            if (child == nullptr || child->name == nullptr || child->name[0] == '\0')
                continue;
            const std::string name = child->name;
            const std::string key = folded(name);
            const auto existing = sections.find(key);
            if (existing != sections.end())
                throw Failure(
                    "the feature '" + name + "' is defined in " + existing->second.file + " and " +
                    source->name
                );
            sections.emplace(key, Section{name, source->name, child});
        }
    }
    return sections;
}

/// Notes a name the game must supply, keeping the first spelling.
void add_need(std::vector<std::string>& needs, std::string name) {
    const std::string key = folded(name);
    for (const std::string& have : needs)
        if (folded(have) == key)
            return;
    needs.push_back(std::move(name));
}

/// Remembers a file the map loads, once.
void add_file(std::map<std::string, std::string>& files, const std::string& path) {
    files.emplace(folded(path), path);
}

/// Walks the feature names a map places and fills its files and its needs.
void walk_uses(
    const map_fit::MapUses& uses,
    const std::map<std::string, Section>& sections,
    const std::map<std::string, Source>& sources,
    std::map<std::string, std::string>& files,
    std::vector<std::string>& needs
) {
    std::vector<std::string> pending = uses.features;
    std::vector<std::string> seen;
    for (std::size_t at = 0; at < pending.size(); ++at) {
        const std::string name = pending[at];
        const std::string key = folded(name);
        bool again = false;
        for (const std::string& earlier : seen) {
            if (earlier == key)
                again = true;
        }
        if (again)
            continue;
        seen.push_back(key);
        const auto section = sections.find(key);
        if (section == sections.end()) {
            add_need(needs, name);
            continue;
        }
        add_file(files, section->second.file);
        const map_fit::SectionReferences references =
            map_fit::references_of(*section->second.block);
        const auto take_file = [&](const std::string& path) {
            if (path.empty())
                return;
            if (const Source* source = find_source(sources, folded(path)))
                add_file(files, source->name);
            else
                add_need(needs, path);
        };
        take_file(references.gaf);
        take_file(references.model);
        if (!references.burn_weapon.empty())
            add_need(needs, references.burn_weapon);
        for (const std::string& link : references.features)
            pending.push_back(link);
    }
    for (const std::string& unit : uses.units)
        add_need(needs, unit);
}

/// Sorts needs by folded path, then by the spelling first kept.
void sort_needs(std::vector<std::string>& needs) {
    std::sort(needs.begin(), needs.end(), [](const std::string& left, const std::string& right) {
        const std::string folded_left = folded(left);
        const std::string folded_right = folded(right);
        if (folded_left != folded_right)
            return bytes_before(folded_left, folded_right);
        return bytes_before(left, right);
    });
}

/// Returns the minimap share the game's map pictures show, from the top-left.
std::optional<Crop>
crop_minimap(const tnt::Minimap& minimap, uint32_t attribute_width, uint32_t attribute_height) {
    if (minimap.width == 0 || minimap.height == 0)
        return std::nullopt;
    if (minimap.palette_indices.size() < uint64_t{minimap.width} * minimap.height)
        return std::nullopt;
    const int64_t world_width = int64_t{attribute_width} * 16;
    const int64_t world_height = int64_t{attribute_height} * 16;
    const int64_t playable_width = world_width - 32;
    const int64_t playable_height = world_height - 128;
    if (playable_width <= 0 || playable_height <= 0)
        return std::nullopt;
    int64_t used_width = minimap.width;
    int64_t used_height = minimap.height;
    if (playable_width >= playable_height)
        used_height = int64_t{minimap.height} * playable_height / playable_width;
    else
        used_width = int64_t{minimap.width} * playable_width / playable_height;
    if (used_width < 1 || used_height < 1)
        return std::nullopt;
    if (used_width > minimap.width)
        used_width = minimap.width;
    if (used_height > minimap.height)
        used_height = minimap.height;
    Crop crop;
    crop.width = static_cast<uint32_t>(used_width);
    crop.height = static_cast<uint32_t>(used_height);
    crop.pixels.resize(static_cast<std::size_t>(crop.width) * crop.height);
    for (uint32_t row = 0; row < crop.height; ++row) {
        const uint8_t* from =
            minimap.palette_indices.data() + static_cast<std::size_t>(row) * minimap.width;
        uint8_t* to = crop.pixels.data() + static_cast<std::size_t>(row) * crop.width;
        std::copy_n(from, crop.width, to);
    }
    return crop;
}

/// Builds one map's entry from its OTA, its TNT and the feature walk.
BuiltMap build_map(
    const std::string& stem,
    const Source& ota_file,
    const Source& tnt_file,
    const std::map<std::string, Section>& sections,
    const std::map<std::string, Source>& sources
) {
    const std::vector<uint8_t> ota_bytes = read_file(ota_file.path);
    const std::vector<uint8_t> tnt_bytes = read_file(tnt_file.path);
    const std::string ota_text(ota_bytes.begin(), ota_bytes.end());
    const ota::ParseResult ota_parsed = ota::parse(ota_text);
    if (!ota_parsed.ok() || !ota_parsed.metadata) {
        std::string why = "could not be read";
        if (ota_parsed.error && !ota_parsed.error->message.empty())
            why = ota_parsed.error->message;
        throw Failure(stem + ": " + why);
    }
    const tnt::ParseResult tnt_parsed = tnt::parse(tnt_bytes);
    if (!tnt_parsed.ok() || !tnt_parsed.map) {
        std::string why = "could not be read";
        if (tnt_parsed.error && !tnt_parsed.error->message.empty())
            why = tnt_parsed.error->message;
        throw Failure(stem + ": " + why);
    }
    map_fit::MapUses uses;
    std::string error;
    if (!map_fit::uses_of(ota_bytes, tnt_bytes, oa::data::defs::DataLayout{}, uses, &error))
        throw Failure(stem + ": " + error);

    const ota::MapMetadata& metadata = *ota_parsed.metadata;
    BuiltMap built;
    built.entry.stem = stem;
    built.entry.title = metadata.mission_name.empty() ? stem : metadata.mission_name;
    const std::size_t kept = oa::base::text::whole_character_bytes(
        metadata.mission_description, map_pack::most_description_bytes
    );
    built.entry.description = metadata.mission_description.substr(0, kept);

    std::size_t most_starts = 0;
    bool any_network = false;
    for (const ota::Schema& schema : metadata.schemas) {
        if (!network_schema(schema.type))
            continue;
        any_network = true;
        most_starts = std::max(most_starts, schema.start_positions.size());
    }
    if (!any_network)
        throw Failure(stem + " has no Network schema");
    if (most_starts < 2 || most_starts > static_cast<std::size_t>(map_pack::most_players))
        throw Failure(
            stem + " has " + std::to_string(most_starts) +
            " start positions in its Network schemas; a map is for 2 to 10 players"
        );
    built.entry.players = static_cast<int32_t>(most_starts);

    const uint32_t width = map_side(tnt_parsed.map->attribute_width);
    const uint32_t height = map_side(tnt_parsed.map->attribute_height);
    if (width > static_cast<uint32_t>(map_pack::most_map_side) ||
        height > static_cast<uint32_t>(map_pack::most_map_side))
        throw Failure(
            stem + " is " + std::to_string(width) + "x" + std::to_string(height) +
            "; a map's side is at most " + std::to_string(map_pack::most_map_side)
        );
    built.entry.size = std::to_string(width) + "x" + std::to_string(height);
    if (tnt_parsed.map->minimap)
        built.crop = crop_minimap(
            *tnt_parsed.map->minimap,
            tnt_parsed.map->attribute_width,
            tnt_parsed.map->attribute_height
        );

    std::map<std::string, std::string> extra;
    walk_uses(uses, sections, sources, extra, built.needs);
    std::vector<std::string> walked;
    walked.reserve(extra.size());
    for (const auto& [key, path] : extra) {
        if (key == folded(ota_file.name) || key == folded(tnt_file.name))
            continue;
        walked.push_back(path);
    }
    std::sort(walked.begin(), walked.end(), [](const std::string& left, const std::string& right) {
        const std::string folded_left = folded(left);
        const std::string folded_right = folded(right);
        if (folded_left != folded_right)
            return bytes_before(folded_left, folded_right);
        return bytes_before(left, right);
    });
    built.entry.files.push_back(ota_file.name);
    built.entry.files.push_back(tnt_file.name);
    built.entry.files.insert(built.entry.files.end(), walked.begin(), walked.end());
    sort_needs(built.needs);
    return built;
}

/// Reads the game's palette. The file itself is never packed.
std::vector<uint8_t> load_palette(const fs::path& game_dir) {
    try {
        oa::AssetStore store(game_dir);
        store.discover(oa::DiscoveryPlan{});
        const oa::AssetData data = store.read("palettes/palette.pal");
        if (data.bytes.size() != oa::PaletteBytes{}.size())
            throw Failure("palettes/palette.pal is not a palette of 256 colours");
        return data.bytes;
    } catch (const Failure&) {
        throw;
    } catch (const std::exception& error) {
        throw Failure(std::string("the game's palette could not be read: ") + error.what());
    }
}

/// Writes one minimap share as an 8-bit palette PNG.
std::vector<uint8_t>
preview_png(const std::vector<uint8_t>& palette, const Crop& crop, const std::string& stem) {
    std::array<png::Rgb, oa::palette_color_count> colours{};
    for (std::size_t index = 0; index < colours.size(); ++index) {
        colours[index].r = palette[index * oa::palette_entry_bytes];
        colours[index].g = palette[index * oa::palette_entry_bytes + 1];
        colours[index].b = palette[index * oa::palette_entry_bytes + 2];
    }
    png::Header header{};
    header.width = crop.width;
    header.height = crop.height;
    header.bit_depth = 8;
    header.color_type = png::ColorType::palette;
    header.interlace = png::Interlace::none;
    png::Image image{};
    image.header = header;
    image.palette = colours;
    image.rows = crop.pixels;
    std::vector<uint8_t> encoded;
    if (!png::write(image, &encoded))
        throw Failure("the preview for " + stem + " could not be written");
    return encoded;
}

/// Writes one piece into the archive.
void write_piece(zip::StreamWriter& writer, const Piece& piece) {
    zip::ZipError error{};
    const Item& item = piece.item;
    if (!writer.begin_entry(item.name, item.bytes, method_of(item), error))
        throw Failure(zip_failure(error));
    if (!piece.memory.empty()) {
        if (!writer.write(piece.memory, error))
            throw Failure(zip_failure(error));
    } else if (item.bytes > 0) {
        FilePos input;
        const uint64_t measured = open_read(item.path, input);
        if (measured != item.bytes)
            throw Failure("cannot read " + item.name);
        std::vector<uint8_t> buffer(std::min(item.bytes, static_cast<uint64_t>(read_piece)));
        uint64_t left = item.bytes;
        while (left > 0) {
            const std::size_t want =
                static_cast<std::size_t>(std::min(left, static_cast<uint64_t>(buffer.size())));
            if (std::fread(buffer.data(), 1, want, input.stream) != want)
                throw Failure("cannot read " + item.name);
            if (!writer.write(std::span<const uint8_t>(buffer.data(), want), error))
                throw Failure(zip_failure(error));
            left -= want;
        }
    }
    if (!writer.end_entry(error))
        throw Failure(zip_failure(error));
}

/// Writes the package to part and returns how many bytes it holds.
uint64_t write_part(const fs::path& part, const std::vector<Piece>& pieces) {
    FilePos output;
    output.stream = oa::platform::open_file(part, "wb");
    if (output.stream == nullptr)
        throw Failure("cannot write " + utf8_of(part));
    output.files = oa::platform::stdio_files();
    zip::StreamWriter writer({&output, write_at});
    for (const Piece& piece : pieces)
        write_piece(writer, piece);
    zip::ZipError error{};
    if (!writer.finish(error))
        throw Failure(zip_failure(error));
    if (std::fflush(output.stream) != 0)
        throw Failure("cannot write " + utf8_of(part));
    return writer.bytes_written();
}

/// Returns the SHA-256 of a file, refusing a size other than `expected`.
sha256::Digest digest_file(const fs::path& path, uint64_t expected) {
    FilePos file;
    const uint64_t size = open_read(path, file);
    if (size != expected)
        throw Failure("the package's size changed while it was read");
    sha256::Hasher hasher{};
    std::vector<uint8_t> piece(read_piece);
    uint64_t left = size;
    while (left > 0) {
        const std::size_t want =
            static_cast<std::size_t>(std::min(left, static_cast<uint64_t>(piece.size())));
        if (std::fread(piece.data(), 1, want, file.stream) != want)
            throw Failure("cannot read " + utf8_of(path));
        sha256::update(hasher, std::span<const uint8_t>(piece.data(), want));
        left -= want;
    }
    return sha256::finish(hasher);
}

/// Reads the part back: the manifest first, in package use, and every listed file present.
void verify_part(
    const fs::path& path,
    const std::string& yaml,
    const std::vector<std::string>& names,
    Output& output
) {
    FilePos file;
    const uint64_t bytes = open_read(path, file);
    zip::StreamDirectory directory;
    zip::ZipError error{};
    if (!zip::read_stream_directory({&file, read_at}, bytes, {}, directory, error))
        throw Failure(zip_failure(error));
    if (directory.entries.size() != names.size())
        throw Failure("the package's files are not the maps' files");
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (directory.entries[index].directory || directory.entries[index].name != names[index])
            throw Failure("the package's files are not the maps' files");
    }
    if (directory.entries.empty() ||
        directory.entries.front().name != std::string{map_pack::manifest_file})
        throw Failure("the package holds no oamap.yaml first");
    std::vector<uint8_t> manifest;
    if (!zip::read_stream_entry(
            {&file, read_at},
            bytes,
            directory.entries.front(),
            directory.entries.front().bytes,
            manifest,
            error
        ))
        throw Failure(zip_failure(error));
    const std::string written(manifest.begin(), manifest.end());
    if (written != yaml)
        throw Failure("the package's oamap.yaml is not the index that was written");
    map_pack::Manifest read;
    std::vector<map_pack::Problem> problems;
    if (!map_pack::read_manifest(manifest, map_pack::ManifestUse::package, read, problems)) {
        print_problems(output, problems);
        throw Failure("oamap.yaml has errors");
    }
    for (const map_pack::MapEntry& map : read.maps) {
        for (const std::string& listed : map.files) {
            bool present = false;
            for (const zip::StreamEntry& entry : directory.entries) {
                if (entry.name == listed)
                    present = true;
            }
            if (!present)
                throw Failure("the package is missing " + listed);
        }
        if (!map.preview.empty()) {
            bool present = false;
            for (const zip::StreamEntry& entry : directory.entries) {
                if (entry.name == map.preview)
                    present = true;
            }
            if (!present)
                throw Failure("the package is missing " + map.preview);
        }
    }
}

/// Prints one map's name, its file count and what it needs from the game.
void print_map(std::ostream& out, const std::string& id, const BuiltMap& map) {
    out << map.entry.stem << '@' << id << ": " << map.entry.files.size()
        << " files; needs from the game: ";
    if (map.needs.empty()) {
        out << "nothing\n";
        return;
    }
    for (std::size_t index = 0; index < map.needs.size(); ++index) {
        if (index != 0)
            out << ", ";
        out << map.needs[index];
    }
    out << '\n';
}

/// Prints the folder's files that were not packed.
void print_left(std::ostream& out, const std::vector<std::string>& left) {
    out << "left out: ";
    if (left.empty()) {
        out << "nothing\n";
        return;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (index != 0)
            out << ", ";
        out << left[index];
    }
    out << '\n';
}

/// Returns the package's name when --out was not given.
std::string default_name(const map_pack::Manifest& manifest) {
    return manifest.id + "-" + install::oamod::version_folder_part(manifest.version) + "-r" +
           std::to_string(manifest.packaging.revision) + ".oamap";
}

/// Adds a memory piece, replacing a file of the same path.
void put_memory(
    std::map<std::string, Piece>& pieces, std::string name, std::vector<uint8_t> bytes
) {
    Piece piece;
    piece.item.name = name;
    piece.item.bytes = bytes.size();
    piece.memory = std::move(bytes);
    pieces.insert_or_assign(folded(name), std::move(piece));
}

/// Adds a source file once, in its disk spelling.
void put_source(std::map<std::string, Piece>& pieces, const Source& source) {
    const std::string key = folded(source.name);
    if (pieces.contains(key))
        return;
    Piece piece;
    piece.item.name = source.name;
    piece.item.path = source.path;
    piece.item.bytes = source.bytes;
    pieces.emplace(key, std::move(piece));
}

} // namespace

int pack_map(const PackMapRequest& request, Output& output) {
    if (!request.game_dir)
        throw Failure("map packs need the game's palette for previews; pass --game-dir");

    const std::map<std::string, Source> sources = index_folder(request.folder);
    const Source* manifest_file = find_source(sources, map_pack::manifest_file);
    if (manifest_file == nullptr)
        throw Failure("the folder holds no oamap.yaml");
    const std::vector<uint8_t> manifest_bytes = read_manifest_bytes(*manifest_file);
    map_pack::Manifest header;
    std::vector<map_pack::Problem> problems;
    if (!map_pack::read_manifest(manifest_bytes, map_pack::ManifestUse::source, header, problems)) {
        print_problems(output, problems);
        throw Failure("oamap.yaml has errors");
    }

    const std::vector<std::string> stems = choose_stems(header, sources);
    if (stems.empty())
        throw Failure("the folder holds no map");
    for (const std::string& stem : stems)
        require_map_files(sources, stem);

    std::vector<tdf::OwnedDocument> documents;
    const std::map<std::string, Section> sections = feature_index(sources, documents);

    std::vector<BuiltMap> maps;
    maps.reserve(stems.size());
    for (const std::string& stem : stems) {
        const auto [ota_file, tnt_file] = require_map_files(sources, stem);
        maps.push_back(build_map(stem, *ota_file, *tnt_file, sections, sources));
    }

    const std::vector<uint8_t> palette = load_palette(*request.game_dir);
    for (BuiltMap& map : maps) {
        if (!map.crop)
            continue;
        std::vector<uint8_t> encoded = preview_png(palette, *map.crop, map.entry.stem);
        map.entry.preview = "previews/" + map.entry.stem + ".png";
        map.crop->pixels = std::move(encoded);
    }

    map_pack::Manifest packed = header;
    packed.maps.clear();
    for (const BuiltMap& map : maps)
        packed.maps.push_back(map.entry);
    const std::string yaml = map_pack::write_manifest(packed);
    problems.clear();
    map_pack::Manifest reread;
    const std::vector<uint8_t> yaml_bytes(yaml.begin(), yaml.end());
    if (!map_pack::read_manifest(yaml_bytes, map_pack::ManifestUse::package, reread, problems)) {
        print_problems(output, problems);
        throw Failure("oamap.yaml has errors");
    }

    std::map<std::string, Piece> piece_index;
    put_memory(piece_index, std::string{map_pack::manifest_file}, yaml_bytes);
    for (const BuiltMap& map : maps) {
        for (const std::string& path : map.entry.files) {
            const Source* source = find_source(sources, folded(path));
            if (source == nullptr)
                throw Failure("the package is missing " + path);
            put_source(piece_index, *source);
        }
        if (!map.entry.preview.empty())
            put_memory(piece_index, map.entry.preview, map.crop->pixels);
    }

    std::vector<Piece> pieces;
    pieces.reserve(piece_index.size());
    for (auto& [key, piece] : piece_index)
        pieces.push_back(std::move(piece));
    std::sort(pieces.begin(), pieces.end(), [](const Piece& left, const Piece& right) {
        const bool left_manifest = left.item.name == map_pack::manifest_file;
        const bool right_manifest = right.item.name == map_pack::manifest_file;
        if (left_manifest != right_manifest)
            return left_manifest;
        return bytes_before(left.item.name, right.item.name);
    });

    std::vector<Item> names;
    names.reserve(pieces.size());
    std::vector<std::string> entry_names;
    entry_names.reserve(pieces.size());
    for (const Piece& piece : pieces) {
        names.push_back(piece.item);
        entry_names.push_back(piece.item.name);
    }
    detail::check_names(names);

    fs::path destination;
    if (request.out)
        destination = *request.out;
    else {
        const std::string name = default_name(header);
        if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
            !install::portable_name_problem(name).empty())
            throw Failure("the package's name is not a file name");
        destination = name;
    }
    if (!ends_with_extension(folded(utf8_of(destination.filename())), ".oamap"))
        throw Failure("a map folder is packed as a .oamap file");

    std::error_code error;
    // Follow a link: /tmp is one, and --out may name a package there.
    if (!destination.parent_path().empty() && !fs::is_directory(destination.parent_path(), error))
        throw Failure("cannot write " + utf8_of(destination));
    const fs::file_status existing = fs::symlink_status(destination, error);
    if (!error && fs::exists(existing)) {
        if (fs::is_directory(existing))
            throw Failure("the output is a folder: " + utf8_of(destination));
        if (!request.force)
            throw Failure("the output already exists: " + utf8_of(destination));
    }

    std::vector<std::string> left;
    for (const auto& [key, source] : sources) {
        if (key == std::string{map_pack::manifest_file})
            continue;
        if (piece_index.contains(key))
            continue;
        left.push_back(source.name);
    }
    std::sort(
        left.begin(), left.end(), [](const std::string& left_name, const std::string& right_name) {
            return bytes_before(left_name, right_name);
        }
    );

    fs::path part = destination;
    part += ".part";
    RemoveUnless part_file(part);
    const uint64_t written = write_part(part, pieces);
    verify_part(part, yaml, entry_names, output);
    replace_output(part, destination);
    part_file.release();
    RemoveUnless output_file(destination);
    const sha256::Digest digest = digest_file(destination, written);
    output_file.release();

    for (const BuiltMap& map : maps)
        print_map(output.out, header.id, map);
    print_left(output.out, left);
    const auto hex = sha256::to_hex(digest);
    output.out << "packed " << utf8_of(destination) << ": " << pieces.size() << " files, "
               << written << " bytes, sha256 ";
    output.out.write(hex.data(), static_cast<std::streamsize>(hex.size()));
    output.out << '\n';
    return exit_done;
}

} // namespace oa::tool
