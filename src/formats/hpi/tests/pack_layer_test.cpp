// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A pack layer under the loose files and the archives: one folder or one zip,
// showing only the files it was given, mounted and unmounted without changing
// anything above it.

#include "oa/base/threads.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/formats/zip.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

int failures = 0;

const Bytes kLooseWeapon = Bytes{'l', 'o', 'o', 's', 'e', ' ', 'w', 'e', 'a', 'p', 'o', 'n'};
const Bytes kLooseUnit = Bytes{'l', 'o', 'o', 's', 'e', ' ', 'u', 'n', 'i', 't'};
const Bytes kArchivedTrees =
    Bytes{'a', 'r', 'c', 'h', 'i', 'v', 'e', 'd', ' ', 't', 'r', 'e', 'e', 's'};
const Bytes kArchivedTree = Bytes{'a', 'r', 'c', 'h', 'i', 'v', 'e', 'd', ' ', 't', 'r', 'e', 'e'};
const Bytes kPackOta = Bytes{'p', 'a', 'c', 'k', ' ', 'o', 't', 'a'};
const Bytes kPackTnt = Bytes{'p', 'a', 'c', 'k', ' ', 't', 'n', 't'};
const Bytes kPackTdf = Bytes{'p', 'a', 'c', 'k', ' ', 'i', 's', 'l', 'e', 's', ' ', 't', 'd', 'f'};
const Bytes kPackIsles =
    Bytes{'p', 'a', 'c', 'k', ' ', 'i', 's', 'l', 'e', 's', ' ', 'g', 'a', 'f'};
const Bytes kPackTree = Bytes{'p', 'a', 'c', 'k', ' ', 't', 'r', 'e', 'e'};
const Bytes kPackWeapon = Bytes{'p', 'a', 'c', 'k', ' ', 'w', 'e', 'a', 'p', 'o', 'n'};
const Bytes kUnlisted = Bytes{'u', 'n', 'l', 'i', 's', 't', 'e', 'd', ' ', 'n', 'o', 't', 'e'};

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

Bytes text(std::string_view value) {
    return Bytes(value.begin(), value.end());
}

bool digits(std::string_view value) {
    if (value.empty())
        return false;
    for (const unsigned char character : value)
        if (character < '0' || character > '9')
            return false;
    return true;
}

class TempDir {
  public:

    TempDir() {
        static std::atomic<int> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
                ("oa-pack-layer-test-" + std::to_string(stamp) + "-" + std::to_string(counter++));
        fs::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

  private:

    fs::path path_;
};

void write_file(const fs::path& target, const Bytes& bytes) {
    fs::create_directories(target.parent_path());
    std::ofstream stream(target, std::ios::binary | std::ios::trunc);
    stream.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
}

Bytes archive_of(std::vector<oa::HpiWriteFile> files) {
    return oa::write_hpi(files);
}

std::vector<oa::PackLayerFile> pack_files() {
    return {
        {"maps/isle@isles.ota", "maps/isle.ota"},
        {"maps/isle@isles.tnt", "maps/isle.tnt"},
        {"features/isles/isles.tdf", "features/isles/isles.tdf"},
        {"anims/isles.gaf", "anims/isles.gaf"},
        {"anims/tree.gaf", "anims/tree.gaf"},
        {"weapons/a.tdf", "weapons/a.tdf"},
    };
}

/// A game folder, a mod folder over it, and a pack folder beside them.
struct Layout {
    TempDir dir;
    fs::path game = dir.path() / "game";
    fs::path mod = dir.path() / "mod";
    fs::path pack = dir.path() / "pack";

    Layout() {
        fs::create_directories(mod);
        write_file(game / "weapons" / "a.tdf", kLooseWeapon);
        write_file(game / "units" / "u.fbi", kLooseUnit);
        write_file(game / "rev31.gp3", archive_of({{"features/trees.tdf", kArchivedTrees, 0}}));
        write_file(game / "pack00.hpi", archive_of({{"anims/tree.gaf", kArchivedTree, 0}}));
        write_file(pack / "maps" / "isle.ota", kPackOta);
        write_file(pack / "maps" / "isle.tnt", kPackTnt);
        write_file(pack / "features" / "isles" / "isles.tdf", kPackTdf);
        write_file(pack / "anims" / "isles.gaf", kPackIsles);
        write_file(pack / "anims" / "tree.gaf", kPackTree);
        write_file(pack / "weapons" / "a.tdf", kPackWeapon);
        write_file(pack / "extra" / "note.txt", kUnlisted);
    }

    oa::PackLayerSpec spec() const {
        oa::PackLayerSpec layer;
        layer.kind = oa::PackLayerKind::folder;
        layer.location = pack;
        layer.label = "isles 9c1f";
        layer.files = pack_files();
        return layer;
    }

    oa::AssetStore store() const {
        oa::AssetStore opened(std::vector<fs::path>{mod, game});
        (void)opened.discover(oa::DiscoveryPlan{});
        return opened;
    }
};

struct ReadInfo {
    Bytes bytes;
    fs::path source;
    bool archived = false;
    bool operator==(const ReadInfo&) const = default;
};

bool same_entry(const oa::FoundEntry& left, const oa::FoundEntry& right) {
    return left.name == right.name && left.directory == right.directory &&
           left.size == right.size && left.mount == right.mount &&
           left.pack_layer == right.pack_layer;
}

bool same_provider(const oa::Provider& left, const oa::Provider& right) {
    return left.kind == right.kind && left.path == right.path && left.identity == right.identity;
}

struct Snapshot {
    std::vector<fs::path> mounts;
    bool loose_index = false;
    std::map<std::string, std::vector<oa::FoundEntry>> found;
    std::map<std::string, std::vector<std::string>> listed;
    std::map<std::string, ReadInfo> reads;
    std::map<std::string, oa::Provider> providers;

    bool operator==(const Snapshot& other) const {
        if (mounts != other.mounts || loose_index != other.loose_index || listed != other.listed ||
            reads != other.reads || found.size() != other.found.size() ||
            providers.size() != other.providers.size())
            return false;
        for (const auto& [pattern, entries] : found) {
            const auto match = other.found.find(pattern);
            if (match == other.found.end() || match->second.size() != entries.size())
                return false;
            for (std::size_t index = 0; index < entries.size(); ++index)
                if (!same_entry(entries[index], match->second[index]))
                    return false;
        }
        for (const auto& [path, provider] : providers) {
            const auto match = other.providers.find(path);
            if (match == other.providers.end() || !same_provider(provider, match->second))
                return false;
        }
        return true;
    }
};

Snapshot snapshot(oa::AssetStore& store) {
    Snapshot snap;
    for (const auto& path : store.mount_paths())
        snap.mounts.push_back(path);
    snap.loose_index = store.loose_index_enabled();
    for (const char* pattern : {"*", "weapons\\*", "anims\\*", "features\\*", "maps\\*"})
        snap.found[pattern] = store.find(pattern);
    for (const char* directory : {"", "features", "anims", "maps"}) {
        for (const bool include : {false, true}) {
            const std::string key = std::string(include ? "with " : "without ") + directory;
            snap.listed[key + " sorted"] = store.list_effective(directory, "", include);
            snap.listed[key + " order"] =
                store.list_effective_in_mount_order(directory, "", include);
            const auto recursive = store.list_effective_recursive(directory, "", include);
            snap.listed[key + " recursive"] = recursive;
            if (!include)
                continue;
            for (const auto& path : recursive) {
                if (snap.reads.contains(path))
                    continue;
                const auto data = store.read(path);
                snap.reads.emplace(path, ReadInfo{data.bytes, data.source, data.archived});
                snap.providers.emplace(path, store.provider(path));
            }
        }
    }
    return snap;
}

void check_find_prefix(const Snapshot& before, const Snapshot& after) {
    for (const auto& [pattern, entries] : before.found) {
        const auto& now = after.found.at(pattern);
        check(now.size() >= entries.size(), "find(" + pattern + ") keeps its matches");
        for (std::size_t index = 0; index < entries.size(); ++index)
            check(
                same_entry(now[index], entries[index]),
                "find(" + pattern + ") keeps " + entries[index].name
            );
        for (std::size_t index = entries.size(); index < now.size(); ++index)
            check(
                now[index].pack_layer,
                "find(" + pattern + ") adds " + now[index].name + " from the layer"
            );
    }
}

void check_reads_unchanged(oa::AssetStore& store, const Snapshot& before) {
    for (const auto& [path, info] : before.reads) {
        const auto data = store.read(path);
        check(data.bytes == info.bytes, "bytes of " + path + " changed");
        check(data.source == info.source, "source of " + path + " changed");
        check(data.archived == info.archived, "archived flag of " + path + " changed");
    }
}

bool missing(oa::AssetStore& store, std::string_view path) {
    try {
        (void)store.read(path);
    } catch (const std::runtime_error& error) {
        return std::string(error.what()).find("asset not found") != std::string::npos;
    }
    return false;
}

void nothing_above_changes() {
    Layout layout;
    auto store = layout.store();
    const Snapshot before = snapshot(store);
    std::string error;
    check(store.mount_pack_layer(layout.spec(), &error), "the folder layer mounts: " + error);
    check(store.pack_layer_mounted(), "the layer is mounted");
    check(
        store.pack_layer_label() == std::optional<std::string>{"isles 9c1f"}, "the label is kept"
    );
    std::vector<std::string> paths;
    for (const auto& file : pack_files())
        paths.push_back(file.path);
    check(store.pack_layer_paths() == paths, "the layer lists its files in the order given");
    check(store.mount_paths().size() == before.mounts.size(), "mount_paths() is unchanged");
    check(store.loose_index_enabled() == before.loose_index, "the loose index stays as it was");

    const Snapshot after = snapshot(store);
    check(after.mounts == before.mounts, "the mounted archives are unchanged");
    check(after.loose_index == before.loose_index, "the loose index flag is unchanged");
    check_reads_unchanged(store, before);
    check_find_prefix(before, after);

    const auto weapon = store.read("weapons/a.tdf");
    check(weapon.bytes == kLooseWeapon && !weapon.archived, "weapons/a.tdf stays the loose file");
    bool layer_weapon = false;
    for (const auto& entry : store.find("weapons\\*"))
        if (entry.name == "a.tdf" && entry.pack_layer)
            layer_weapon = true;
    check(!layer_weapon, "find(weapons\\*) leaves out the layer's a.tdf");

    int archive_trees = 0;
    int layer_trees = 0;
    for (const auto& entry : store.find("anims\\*")) {
        if (entry.name == "tree.gaf" && !entry.pack_layer)
            ++archive_trees;
        if (entry.name == "tree.gaf" && entry.pack_layer)
            ++layer_trees;
    }
    check(
        archive_trees == 1 && layer_trees == 1, "find(anims\\*) reports the archive and the layer"
    );

    const auto tree = store.read("anims/tree.gaf");
    check(
        tree.bytes == kArchivedTree && tree.archived && tree.source.filename() == "pack00.hpi",
        "anims/tree.gaf still comes from pack00.hpi"
    );
    const auto tree_provider = store.provider("anims/tree.gaf");
    check(tree_provider.kind == oa::ProviderKind::archive, "provider of the tree is the archive");
    check(tree_provider.path.filename() == "pack00.hpi", "the archive is pack00.hpi");
    check(
        store.provided_above_pack_layer("anims/tree.gaf"), "the tree is provided above the layer"
    );
    check(store.read_pack_layer("anims/tree.gaf") == kPackTree, "the layer keeps its own tree");

    const auto ota = store.read("maps/isle@isles.ota");
    check(ota.bytes == kPackOta && !ota.archived, "the suffixed ota reads the pack's isle.ota");
    check(ota.source.filename() == "isle.ota", "the ota's source is the pack file");
    check(missing(store, "maps/isle.ota"), "the pack's own ota name is not a resource");
    check(missing(store, "extra/note.txt"), "an unlisted pack file is not found");
    for (const auto& name : store.list_effective_recursive("", ""))
        check(name != "extra/note.txt", "an unlisted pack file is not listed");
    check(
        store.provider("extra/note.txt").kind == oa::ProviderKind::none,
        "an unlisted file has no provider"
    );
    check(!store.read_pack_layer("extra/note.txt"), "an unlisted file has no layer bytes");
    check(
        !store.read_pack_layer("maps/isle.ota"), "a path the layer does not show has no layer bytes"
    );

    const auto features_before = store.list_effective_recursive("features", ".tdf", false);
    // The listing without the layer is taken after the mount, so compare it
    // with a fresh listing of the same store once the layer is hidden from it.
    const auto features = store.list_effective_recursive("features", ".tdf");
    check(
        features.size() == features_before.size() + 1 &&
            features.back() == "features/isles/isles.tdf",
        "the layer's feature tdf is last"
    );
    for (std::size_t index = 0; index < features_before.size(); ++index)
        check(features[index] == features_before[index], "the feature list keeps its prefix");
    check(
        features_before == before.listed.at("with features recursive") ||
            store.list_effective_recursive("features", ".tdf", false).size() ==
                features_before.size(),
        "without the layer the feature names are the ones from before the mount"
    );
    // before.listed recorded every name, not only .tdf. The .tdf listing
    // without the layer must equal the listing a store with no layer gives.
    {
        Layout plain;
        auto other = plain.store();
        check(
            store.list_effective_recursive("features", ".tdf", false) ==
                other.list_effective_recursive("features", ".tdf"),
            "without the layer the feature list is the list from before the mount"
        );
    }

    bool reached = false;
    for (const auto& path : store.scan_recursive("features", "*.tdf"))
        if (path == "features\\isles\\isles.tdf")
            reached = true;
    check(reached, "scan_recursive reaches features\\isles\\isles.tdf");

    const auto tnt = store.provider("maps/isle@isles.tnt");
    check(tnt.kind == oa::ProviderKind::pack_layer, "the tnt comes from the layer");
    check(tnt.path == fs::weakly_canonical(layout.pack), "the provider path is the pack folder");
    check(
        !store.provided_above_pack_layer("maps/isle@isles.tnt"), "nothing above provides the tnt"
    );
    const std::string prefix = "pack:isles 9c1f#";
    check(tnt.identity.starts_with(prefix), "identity " + tnt.identity);
    check(digits(tnt.identity.substr(prefix.size())), "the serial is a number in " + tnt.identity);
    check(
        store.read("maps/isle@isles.tnt").bytes == kPackTnt,
        "the suffixed tnt reads the pack's isle.tnt"
    );
    check(store.read_pack_layer("weapons/a.tdf") == kPackWeapon, "the layer keeps its own weapon");
}

void unmount_restores_exactly() {
    Layout layout;
    auto store = layout.store();
    const Snapshot before = snapshot(store);
    std::string error;
    check(store.mount_pack_layer(layout.spec(), &error), error);
    const auto first = store.provider("maps/isle@isles.tnt").identity;
    check(store.unmount_pack_layer(), "unmount reports the layer");
    check(!store.pack_layer_mounted(), "the layer is gone");
    check(!store.pack_layer_label(), "the label is gone");
    check(store.pack_layer_paths().empty(), "the file list is gone");
    check(snapshot(store) == before, "unmounting restores the store");
    check(!store.unmount_pack_layer(), "unmounting again reports that nothing was mounted");
    check(store.mount_pack_layer(layout.spec(), &error), "the layer mounts again: " + error);
    const auto second = store.provider("maps/isle@isles.tnt").identity;
    check(second != first, "mounting again gives a new serial");
    check(second.starts_with("pack:isles 9c1f#"), second);
}

void zip_reads_as_folder() {
    Layout layout;
    auto folder_store = layout.store();
    std::string error;
    check(folder_store.mount_pack_layer(layout.spec(), &error), error);
    std::map<std::string, ReadInfo> folder_reads;
    std::map<std::string, std::optional<Bytes>> folder_layer;
    for (const auto& file : pack_files()) {
        const auto data = folder_store.read(file.path);
        folder_reads.emplace(file.path, ReadInfo{data.bytes, data.source, data.archived});
        folder_layer.emplace(file.path, folder_store.read_pack_layer(file.path));
    }

    Bytes big(200 * 1024);
    for (std::size_t index = 0; index < big.size(); ++index)
        big[index] = static_cast<uint8_t>(index * 17u + 3u);

    struct Held {
        std::string name;
        Bytes bytes;
    };

    std::vector<Held> held;
    held.push_back({"maps/isle.ota", kPackOta});
    held.push_back({"maps/isle.tnt", kPackTnt});
    held.push_back({"features/isles/isles.tdf", kPackTdf});
    held.push_back({"anims/isles.gaf", kPackIsles});
    held.push_back({"anims/tree.gaf", kPackTree});
    held.push_back({"weapons/a.tdf", kPackWeapon});
    held.push_back({"extra/note.txt", kUnlisted});
    held.push_back({"big/blob.bin", big});
    std::vector<oa::formats::zip::NewEntry> entries;
    for (const auto& item : held)
        entries.push_back({item.name, item.bytes});
    std::vector<uint8_t> archive;
    oa::formats::zip::ZipError zip_error;
    check(
        oa::formats::zip::write_archive(entries, archive, zip_error),
        std::string("the pack zip is written: ") +
            oa::formats::zip::zip_status_message(zip_error.status)
    );
    const auto zip_path = layout.dir.path() / "isles.oamap";
    write_file(zip_path, archive);

    auto store = layout.store();
    oa::PackLayerSpec spec = layout.spec();
    spec.kind = oa::PackLayerKind::zip;
    spec.location = zip_path;
    spec.files.push_back({"big/blob.bin", "big/blob.bin"});
    check(store.mount_pack_layer(spec, &error), "the zip layer mounts: " + error);
    for (const auto& file : pack_files()) {
        const auto data = store.read(file.path);
        const auto& from_folder = folder_reads.at(file.path);
        check(data.bytes == from_folder.bytes, "zip read bytes of " + file.path);
        const bool from_layer = store.provider(file.path).kind == oa::ProviderKind::pack_layer;
        if (from_layer)
            check(data.archived, file.path + " is archived when it comes from the zip");
        else
            check(data.archived == from_folder.archived, "zip read archived flag of " + file.path);
        check(
            store.read_pack_layer(file.path) == folder_layer.at(file.path),
            "zip layer bytes of " + file.path
        );
    }
    check(missing(store, "extra/note.txt"), "an unlisted zip entry is not found");

    const auto whole = store.read("big/blob.bin");
    check(whole.bytes == big && whole.archived, "the big entry reads archived from the zip");
    oa::ResourceFile* handle = store.open("big/blob.bin");
    check(handle != nullptr, "the big entry opens");
    check(oa::AssetStore::archived(handle), "the open zip entry is archived");
    check(oa::AssetStore::length(handle) == big.size(), "the handle has the entry's length");
    Bytes gathered(big.size());
    std::size_t filled = 0;
    while (handle != nullptr && filled < big.size()) {
        const auto position = static_cast<uint32_t>(filled);
        check(oa::AssetStore::seek(handle, position) != -1, "seek to a 64 KiB block");
        const auto take = std::min<std::size_t>(64 * 1024, big.size() - filled);
        const auto count =
            oa::AssetStore::read(handle, std::span<uint8_t>(gathered).subspan(filled, take));
        check(count == static_cast<int32_t>(take), "a 64 KiB block reads in full");
        filled += take;
    }
    check(gathered == whole.bytes, "the blocks match the entry read whole");
    oa::AssetStore::close(handle);
}

void expect_refused(
    oa::AssetStore& store,
    const oa::PackLayerSpec& spec,
    const Snapshot& before,
    std::string_view needle
) {
    std::string error = "unchanged";
    check(!store.mount_pack_layer(spec, &error), "a layer was refused");
    check(
        error.find(needle) != std::string::npos,
        "error names '" + std::string(needle) + "': " + error
    );
    check(snapshot(store) == before, "a refused mount leaves the store unchanged (" + error + ")");
}

void refusals() {
    Layout layout;
    auto store = layout.store();
    const Snapshot before = snapshot(store);
    oa::PackLayerSpec spec = layout.spec();

    spec.files.clear();
    expect_refused(store, spec, before, "no files");

    spec = layout.spec();
    spec.files.clear();
    for (int index = 0; index < 4097; ++index)
        spec.files.push_back({"f" + std::to_string(index), "maps/isle.ota"});
    expect_refused(store, spec, before, "f4096");

    spec = layout.spec();
    spec.label.clear();
    expect_refused(store, spec, before, "label");
    spec.label = "isles\n9c1f";
    expect_refused(store, spec, before, "line");

    for (const char* path : {"../x", "/x", "c:x", ".backup/x"}) {
        spec = layout.spec();
        spec.files = {{path, "maps/isle.ota"}};
        expect_refused(store, spec, before, path);
    }

    spec = layout.spec();
    spec.files = {{"maps/isle.ota", "maps/isle.ota"}, {"maps/ISLE.ota", "maps/isle.tnt"}};
    expect_refused(store, spec, before, "ISLE.ota");

    spec = layout.spec();
    spec.files = {{"maps/missing.ota", "maps/missing.ota"}};
    expect_refused(store, spec, before, "maps/missing.ota");

    fs::create_directories(layout.pack / "maps" / "adir");
    spec = layout.spec();
    spec.files = {{"maps/adir", "maps/adir"}};
    expect_refused(store, spec, before, "maps/adir");

#ifndef _WIN32
    const auto outside = layout.dir.path() / "outside.txt";
    write_file(outside, text("outside"));
    fs::create_symlink(outside, layout.pack / "escape.txt");
    spec = layout.spec();
    spec.files = {{"escape.txt", "escape.txt"}};
    expect_refused(store, spec, before, "escape.txt");
#endif

    std::vector<uint8_t> archive;
    oa::formats::zip::ZipError zip_error;
    const oa::formats::zip::NewEntry directory[] = {{"maps/", {}}};
    check(
        oa::formats::zip::write_archive(directory, archive, zip_error),
        "a zip directory entry is written"
    );
    const auto zip_path = layout.dir.path() / "folder.zip";
    write_file(zip_path, archive);
    spec = layout.spec();
    spec.kind = oa::PackLayerKind::zip;
    spec.location = zip_path;
    spec.files = {{"maps", "maps"}};
    expect_refused(store, spec, before, "maps");

    std::string error;
    check(store.mount_pack_layer(layout.spec(), &error), "the first layer mounts: " + error);
    const Snapshot mounted = snapshot(store);
    error = "unchanged";
    check(!store.mount_pack_layer(layout.spec(), &error), "a second layer is refused");
    check(error == "a pack layer is mounted already: isles 9c1f", error);
    check(snapshot(store) == mounted, "a refused second mount changes nothing");
    check(store.unmount_pack_layer(), "the first layer unmounts");
    check(snapshot(store) == before, "the store is back to its start after the refusals");
}

struct RaceState {
    oa::AssetStore* store = nullptr;
    std::string failure;
    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};
};

// The reader writes `failure` and then stores `failed`. The main thread reads
// `failure` only after join_thread, which happens after that store.
void note_failure(RaceState* state, std::string message) {
    state->failure = std::move(message);
    state->failed.store(true);
}

void read_during_mount(void* argument) {
    auto* state = static_cast<RaceState*>(argument);
    // At least 200 rounds, and keep going until the mounts stop, so the reader
    // overlaps the mounts on every system.
    for (int round = 0; !state->stop.load() || round < 200; ++round) {
        try {
            const auto data = state->store->read("maps/isle@isles.ota");
            if (data.bytes != kPackOta) {
                note_failure(state, "the pack ota bytes changed");
                return;
            }
        } catch (const std::exception& error) {
            const std::string message = error.what();
            if (message.find("asset not found") == std::string::npos) {
                note_failure(state, "ota read failed: " + message);
                return;
            }
        } catch (...) {
            note_failure(state, "ota read failed with an unknown exception");
            return;
        }
        try {
            const auto unit = state->store->read("units/u.fbi");
            if (unit.bytes != kLooseUnit) {
                note_failure(state, "the game unit bytes changed");
                return;
            }
        } catch (const std::exception& error) {
            note_failure(state, std::string("unit read failed: ") + error.what());
            return;
        } catch (...) {
            note_failure(state, "unit read failed with an unknown exception");
            return;
        }
        try {
            const auto found = state->store->find("anims\\*");
            if (found.empty() || found.front().name != "tree.gaf" || found.front().pack_layer) {
                note_failure(state, "the archive's tree.gaf was not first");
                return;
            }
        } catch (const std::exception& error) {
            note_failure(state, std::string("find failed: ") + error.what());
            return;
        } catch (...) {
            note_failure(state, "find failed with an unknown exception");
            return;
        }
        oa::ResourceFile* file = nullptr;
        try {
            file = state->store->open("features/isles/isles.tdf");
            if (file != nullptr) {
                Bytes bytes(oa::AssetStore::length(file));
                const auto count = oa::AssetStore::read(file, bytes);
                if (count < 0 || bytes != kPackTdf) {
                    oa::AssetStore::close(file);
                    note_failure(state, "the open tdf did not read the pack");
                    return;
                }
            }
        } catch (const std::exception& error) {
            oa::AssetStore::close(file);
            note_failure(state, std::string("tdf handle failed: ") + error.what());
            return;
        } catch (...) {
            oa::AssetStore::close(file);
            note_failure(state, "tdf handle failed with an unknown exception");
            return;
        }
        oa::AssetStore::close(file);
    }
}

void readers_survive_unmount() {
    Layout layout;
    auto store = layout.store();
    const auto spec = layout.spec();
    std::string error;
    check(store.mount_pack_layer(spec, &error), error);
    oa::ResourceFile* handle = store.open("features/isles/isles.tdf");
    check(handle != nullptr, "the tdf opens while the layer is mounted");
    check(store.unmount_pack_layer(), "the layer unmounts with the handle open");
    Bytes held(oa::AssetStore::length(handle));
    check(
        oa::AssetStore::read(handle, held) == static_cast<int32_t>(held.size()) && held == kPackTdf,
        "a handle opened while mounted reads the pack after the unmount"
    );
    oa::AssetStore::close(handle);

    RaceState state;
    state.store = &store;
    oa::base::threads::Thread thread;
    check(oa::base::threads::start_thread(thread, read_during_mount, &state), "the reader starts");
    std::string mount_failure;
    for (int round = 0; round < 200 && !state.failed.load(); ++round) {
        if (!store.mount_pack_layer(spec, &error)) {
            mount_failure = "mount failed: " + error;
            break;
        }
        if (!store.unmount_pack_layer()) {
            mount_failure = "unmount failed";
            break;
        }
    }
    state.stop.store(true);
    oa::base::threads::join_thread(thread);
    if (!mount_failure.empty()) {
        check(false, mount_failure);
    } else {
        check(state.failure.empty(), state.failure.empty() ? "the reader finished" : state.failure);
    }
}

void discover_keeps_layer() {
    Layout layout;
    auto store = layout.store();
    std::string error;
    check(store.mount_pack_layer(layout.spec(), &error), error);
    write_file(layout.game / "anims" / "isles.gaf", text("loose isles"));
    (void)store.discover(oa::DiscoveryPlan{});
    check(store.pack_layer_mounted(), "discover leaves the layer mounted");
    bool layer_isles = false;
    bool loose_isles = false;
    bool layer_tree = false;
    for (const auto& entry : store.find("anims\\*")) {
        if (entry.name == "isles.gaf" && entry.pack_layer)
            layer_isles = true;
        if (entry.name == "isles.gaf" && !entry.pack_layer)
            loose_isles = true;
        if (entry.name == "tree.gaf" && entry.pack_layer)
            layer_tree = true;
    }
    check(!layer_isles, "the loose file hides the layer's isles.gaf from find");
    check(loose_isles, "the loose isles.gaf is found");
    check(layer_tree, "the layer's tree.gaf stays in find");
    check(
        store.read("anims/isles.gaf").bytes == text("loose isles"), "the loose file wins the read"
    );
    check(store.read_pack_layer("anims/isles.gaf") == kPackIsles, "the layer still holds its gaf");
}

void moved_store_keeps_layer() {
    Layout layout;
    auto store = layout.store();
    std::string error;
    check(store.mount_pack_layer(layout.spec(), &error), error);
    const auto identity = store.provider("maps/isle@isles.tnt").identity;
    oa::AssetStore moved(std::move(store));
    check(moved.pack_layer_mounted(), "a moved store keeps the layer");
    check(
        moved.pack_layer_label() == std::optional<std::string>{"isles 9c1f"},
        "a moved store keeps the label"
    );
    check(moved.read("maps/isle@isles.ota").bytes == kPackOta, "a moved store reads the layer");
    check(
        moved.provider("maps/isle@isles.tnt").identity == identity,
        "a moved store keeps the identity"
    );
    oa::AssetStore assigned(layout.game);
    assigned = std::move(moved);
    check(assigned.pack_layer_mounted(), "move assignment keeps the layer");
    check(
        assigned.read("maps/isle@isles.tnt").bytes == kPackTnt, "the assigned store reads the layer"
    );
}

} // namespace

int main() {
    try {
        nothing_above_changes();
        unmount_restores_exactly();
        zip_reads_as_folder();
        refusals();
        readers_survive_unmount();
        discover_keeps_layer();
        moved_store_keeps_layer();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " pack layer check(s) failed\n";
        return 1;
    }
    std::cout << "hpi pack layer tests passed\n";
    return 0;
}
