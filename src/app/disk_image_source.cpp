// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The disk image for older Macs, read as a content source. The image is
// recognised by the size and digest in the bundle table, then the game
// folder is the directory the table's files vote for.
#include "oa/app/disk_image_source.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/formats/hfsplus.hpp"
#include "oa/formats/udif.hpp"
#include "oa/platform/files.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#include <stdio.h>
#else
#include <unistd.h>
#endif

namespace oa::app {
namespace {

namespace bundles = oa::data::content_bundles;
namespace fs = std::filesystem;

constexpr std::size_t k_list_limit = 100000;
constexpr uint32_t k_root_id = 2;

struct ListedFile {
    std::string name;
    uint64_t size{};
    int64_t modified{};
    uint32_t id{};
};

struct FileRead {
    std::FILE* file{};
    uint64_t size{};
};

void fail(std::string* error, const char* message) {
    if (error != nullptr)
        *error = message;
}

[[nodiscard]] uint64_t narrow_size(uintmax_t value) noexcept {
    constexpr auto limit = static_cast<uintmax_t>(std::numeric_limits<uint64_t>::max());
    if (value > limit)
        return std::numeric_limits<uint64_t>::max();
    return static_cast<uint64_t>(value);
}

[[nodiscard]] bool seek_file(std::FILE* file, uint64_t offset) noexcept {
#if defined(_WIN32)
    return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    return ::fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

[[nodiscard]] bool sync_file(std::FILE* file) noexcept {
    if (std::fflush(file) != 0)
        return false;
#if defined(_WIN32)
    return _commit(_fileno(file)) == 0;
#else
    return ::fsync(::fileno(file)) == 0;
#endif
}

[[nodiscard]] bool disk_is_full(int number) noexcept {
#if defined(EDQUOT)
    if (number == EDQUOT)
        return true;
#endif
    return number == ENOSPC;
}

[[nodiscard]] bool stopped_now(const std::atomic<bool>* stop) noexcept {
    return stop != nullptr && stop->load();
}

[[nodiscard]] uint64_t chunk_size(uint64_t requested) noexcept {
    if (requested == 0 || requested > content_copy_chunk_bytes)
        return content_copy_chunk_bytes;
    return requested;
}

[[nodiscard]] uint64_t regular_size(const fs::path& path) noexcept {
    std::error_code failure;
    if (!fs::is_regular_file(path, failure))
        return 0;
    const auto size = fs::file_size(path, failure);
    if (failure)
        return 0;
    return narrow_size(size);
}

void remove_file(const fs::path& path) {
    std::error_code failure;
    fs::remove(path, failure);
}

[[nodiscard]] bool ascii_equal(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        unsigned char one = static_cast<unsigned char>(left[index]);
        unsigned char other = static_cast<unsigned char>(right[index]);
        if (one >= 'A' && one <= 'Z')
            one = static_cast<unsigned char>(one - 'A' + 'a');
        if (other >= 'A' && other <= 'Z')
            other = static_cast<unsigned char>(other - 'A' + 'a');
        if (one != other)
            return false;
    }
    return true;
}

[[nodiscard]] std::string_view last_component(std::string_view path) noexcept {
    std::size_t start = 0;
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (path[index] == '/' || path[index] == '\\')
            start = index + 1;
    }
    return path.substr(start);
}

[[nodiscard]] int directory_steps(std::string_view path) noexcept {
    int steps = 0;
    for (char mark : path) {
        if (mark == '/' || mark == '\\')
            ++steps;
    }
    return steps;
}

[[nodiscard]] bool contains_folded(std::string_view text, std::string_view needle) noexcept {
    if (needle.size() > text.size())
        return false;
    for (std::size_t index = 0; index + needle.size() <= text.size(); ++index) {
        if (ascii_equal(text.substr(index, needle.size()), needle))
            return true;
    }
    return false;
}

[[nodiscard]] bool read_image(void* context, uint64_t offset, uint8_t* out, std::size_t count) {
    auto* file = static_cast<FileRead*>(context);
    if (file == nullptr || file->file == nullptr)
        return false;
    if (offset > file->size || static_cast<uint64_t>(count) > file->size - offset)
        return false;
    if (!seek_file(file->file, offset))
        return false;
    return std::fread(out, 1, count, file->file) == count;
}

[[nodiscard]] bool read_disk(void* context, uint64_t offset, uint8_t* out, std::size_t count) {
    auto* image = static_cast<oa::formats::udif::Image*>(context);
    std::string error;
    return image != nullptr && image->read(offset, std::span<uint8_t>(out, count), &error);
}

struct OwnedFile {
    std::FILE* file = nullptr;

    ~OwnedFile() {
        if (file != nullptr)
            std::fclose(file);
    }

    OwnedFile() = default;

    explicit OwnedFile(std::FILE* opened) : file(opened) {}

    OwnedFile(const OwnedFile&) = delete;
    OwnedFile& operator=(const OwnedFile&) = delete;
};

} // namespace

struct DiskImageContentSource::State {
    FileRead file{};
    oa::formats::udif::Image image;
    oa::formats::hfsplus::Volume volume;
    std::vector<ListedFile> listed;

    ~State() {
        if (file.file != nullptr)
            std::fclose(file.file);
    }

    [[nodiscard]] const oa::formats::hfsplus::CatalogItem* find_id(uint32_t id) const {
        for (const oa::formats::hfsplus::CatalogItem& item : volume.catalog()) {
            if (item.id == id && (item.kind == oa::formats::hfsplus::CatalogItem::Kind::folder ||
                                  item.kind == oa::formats::hfsplus::CatalogItem::Kind::file))
                return &item;
        }
        return nullptr;
    }

    [[nodiscard]] std::string folder_path(uint32_t id) const {
        std::vector<std::string> parts;
        for (int hop = 0; hop < 1024 && id != k_root_id && id != 1 && id != 0; ++hop) {
            const oa::formats::hfsplus::CatalogItem* item = find_id(id);
            if (item == nullptr)
                break;
            parts.push_back(item->name);
            id = item->parent;
        }
        std::string path;
        for (std::size_t index = parts.size(); index > 0; --index) {
            if (!path.empty())
                path.push_back('/');
            path += parts[index - 1];
        }
        return path;
    }

    [[nodiscard]] bool under_root(uint32_t id, uint32_t root, std::string& relative) const {
        std::vector<std::string> parts;
        for (int hop = 0; hop < 1024; ++hop) {
            if (id == root)
                break;
            const oa::formats::hfsplus::CatalogItem* item = find_id(id);
            if (item == nullptr)
                return false;
            parts.push_back(item->name);
            id = item->parent;
            if (id == 0 || id == 1)
                return false;
        }
        if (id != root)
            return false;
        relative.clear();
        for (std::size_t index = parts.size(); index > 0; --index) {
            if (!relative.empty())
                relative.push_back('/');
            relative += parts[index - 1];
        }
        return true;
    }

    [[nodiscard]] const ListedFile* find_listed(std::string_view name) const {
        for (const ListedFile& item : listed) {
            if (ascii_equal(item.name, name))
                return &item;
        }
        return nullptr;
    }
};

DiskImageContentSource::DiskImageContentSource(
    fs::path image, std::span<const bundles::Bundle> bundles, DiskImageOptions options
)
    : image_(std::move(image)), bundles_(bundles), options_(options) {
}

DiskImageContentSource::~DiskImageContentSource() = default;

std::string DiskImageContentSource::origin() const {
    return path_to_utf8(image_);
}

fs::path DiskImageContentSource::disk_root() const {
    return {};
}

bool DiskImageContentSource::open(std::string* error) {
    if (state_ != nullptr)
        return true;
    std::error_code failure;
    const auto size_raw = fs::file_size(image_, failure);
    if (failure || !fs::is_regular_file(image_, failure)) {
        fail(error, "The disk image could not be read");
        return false;
    }
    const uint64_t size = narrow_size(size_raw);
    if (options_.require_known_installer) {
        const bundles::Installer* installer = bundles::find_installer(size);
        if (installer == nullptr || installer->kind != bundles::InstallerKind::older_mac) {
            fail(error, "This file is not the disk image for older Macs");
            return false;
        }
    }
    auto state = std::make_unique<State>();
    state->file.file = oa::platform::open_file(image_, "rb");
    state->file.size = size;
    if (state->file.file == nullptr) {
        fail(error, "The disk image could not be read");
        return false;
    }
    if (options_.require_known_installer) {
        const bundles::Installer* installer = bundles::find_installer(size);
        oa::base::sha256::Hasher hasher;
        std::vector<uint8_t> buffer(1024 * 1024);
        uint64_t got = 0;
        while (got < size) {
            const uint64_t step = std::min<uint64_t>(buffer.size(), size - got);
            const std::size_t want = static_cast<std::size_t>(step);
            const std::size_t read = std::fread(buffer.data(), 1, want, state->file.file);
            if (read != want) {
                fail(error, "The disk image could not be read");
                return false;
            }
            oa::base::sha256::update(hasher, std::span<const uint8_t>(buffer.data(), read));
            got += read;
        }
        if (installer == nullptr ||
            !bundles::known_installer_hash(*installer, oa::base::sha256::finish(hasher))) {
            fail(error, "This file is not the disk image for older Macs");
            return false;
        }
    }
    oa::formats::udif::ReadHooks file_hooks;
    file_hooks.context = &state->file;
    file_hooks.read = &read_image;
    if (!state->image.open(file_hooks, size, error))
        return false;
    oa::formats::hfsplus::ReadHooks disk_hooks;
    disk_hooks.context = &state->image;
    disk_hooks.read = &read_disk;
    if (!state->volume.open_disk(disk_hooks, state->image.disk_bytes(), error))
        return false;

    struct Tally {
        uint32_t id{};
        int votes{};
        int score{};
    };

    std::vector<Tally> tallies;
    for (const oa::formats::hfsplus::CatalogItem& item : state->volume.catalog()) {
        if (item.kind != oa::formats::hfsplus::CatalogItem::Kind::file || item.symlink)
            continue;
        for (const bundles::Bundle& bundle : bundles_) {
            for (const bundles::ContentFile& file : bundle.files) {
                if (file.size != item.data_bytes ||
                    !ascii_equal(item.name, last_component(file.copy_name)))
                    continue;
                uint32_t ancestor = item.parent;
                bool reached = true;
                const int steps = directory_steps(file.copy_name);
                for (int step = 0; step < steps; ++step) {
                    const oa::formats::hfsplus::CatalogItem* folder = state->find_id(ancestor);
                    if (folder == nullptr ||
                        folder->kind != oa::formats::hfsplus::CatalogItem::Kind::folder) {
                        reached = false;
                        break;
                    }
                    ancestor = folder->parent;
                }
                if (!reached || ancestor == 0)
                    continue;
                Tally* tally = nullptr;
                for (Tally& candidate : tallies) {
                    if (candidate.id == ancestor) {
                        tally = &candidate;
                        break;
                    }
                }
                if (tally == nullptr) {
                    const std::string path = state->folder_path(ancestor);
                    Tally added;
                    added.id = ancestor;
                    added.score = (contains_folded(path, "drive_c") ? 2 : 0) +
                                  (contains_folded(path, "Contents/Resources/game") ? 1 : 0);
                    tallies.push_back(added);
                    tally = &tallies.back();
                }
                ++tally->votes;
            }
        }
    }
    const Tally* best = nullptr;
    for (const Tally& tally : tallies) {
        if (best == nullptr || tally.votes > best->votes ||
            (tally.votes == best->votes && tally.score > best->score))
            best = &tally;
    }
    if (best == nullptr || best->votes == 0) {
        fail(error, "This disk image does not contain the game files");
        return false;
    }
    const uint32_t root = best->id;
    for (const oa::formats::hfsplus::CatalogItem& item : state->volume.catalog()) {
        if (item.kind != oa::formats::hfsplus::CatalogItem::Kind::file || item.symlink)
            continue;
        if (state->listed.size() >= k_list_limit) {
            fail(error, "The disk image could not be read");
            return false;
        }
        ListedFile listed;
        if (!state->under_root(item.id, root, listed.name) || listed.name.empty())
            continue;
        listed.size = item.data_bytes;
        listed.modified = item.modified;
        listed.id = item.id;
        state->listed.push_back(std::move(listed));
    }
    state_ = std::move(state);
    return true;
}

bool DiskImageContentSource::list(std::vector<ContentSourceFile>& files, std::string* error) {
    if (!open(error))
        return false;
    files.clear();
    files.reserve(state_->listed.size());
    for (const ListedFile& item : state_->listed) {
        ContentSourceFile file;
        file.name = item.name;
        file.size = item.size;
        file.modified = item.modified;
        files.push_back(std::move(file));
    }
    return true;
}

bool DiskImageContentSource::hash_file(
    std::string_view name, oa::base::sha256::Digest& digest, std::string* error
) {
    if (!open(error))
        return false;
    const ListedFile* listed = state_->find_listed(name);
    if (listed == nullptr) {
        fail(error, "The file could not be found");
        return false;
    }
    oa::base::sha256::Hasher hasher;
    std::vector<uint8_t> buffer(64 * 1024);
    uint64_t got = 0;
    while (got < listed->size) {
        const uint64_t step = std::min<uint64_t>(buffer.size(), listed->size - got);
        const std::size_t want = static_cast<std::size_t>(step);
        if (!state_->volume.read_file(
                listed->id, got, std::span<uint8_t>(buffer.data(), want), error
            )) {
            if (error != nullptr && error->empty())
                fail(error, "The file could not be read");
            return false;
        }
        oa::base::sha256::update(hasher, std::span<const uint8_t>(buffer.data(), want));
        got += want;
    }
    digest = oa::base::sha256::finish(hasher);
    return true;
}

bool DiskImageContentSource::stream_file(
    const StreamRequest& request, StreamResult& result, std::string* error
) {
    result = {};
    if (!open(error))
        return false;
    const ListedFile* listed = state_->find_listed(request.name);
    if (listed == nullptr) {
        fail(error, "The file could not be found");
        return false;
    }
    const uint64_t source_size = listed->size;
    uint64_t prefix = regular_size(request.destination);
    if (prefix > source_size) {
        remove_file(request.destination);
        prefix = 0;
    }
    if (stopped_now(request.stop) && prefix == 0) {
        result.stopped = true;
        return true;
    }
    const uint64_t chunk = chunk_size(request.chunk_bytes);
    oa::base::sha256::Hasher hasher;
    auto read_source = [&](uint64_t offset, std::span<uint8_t> out) {
        return state_->volume.read_file(listed->id, offset, out, error);
    };
    if (prefix > 0) {
        OwnedFile existing(oa::platform::open_file(request.destination, "rb"));
        if (existing.file == nullptr) {
            fail(error, "The copy could not be read");
            return false;
        }
        std::vector<uint8_t> from_source(64 * 1024);
        std::vector<uint8_t> from_dest(from_source.size());
        uint64_t got = 0;
        bool different = false;
        while (got < prefix) {
            const uint64_t step = std::min<uint64_t>(from_source.size(), prefix - got);
            const std::size_t want = static_cast<std::size_t>(step);
            if (!read_source(got, std::span<uint8_t>(from_source.data(), want))) {
                if (error != nullptr && error->empty())
                    fail(error, "The file could not be read");
                return false;
            }
            const std::size_t dest_read = std::fread(from_dest.data(), 1, want, existing.file);
            if (std::ferror(existing.file) != 0) {
                fail(error, "The copy could not be read");
                return false;
            }
            if (dest_read != want || std::memcmp(from_dest.data(), from_source.data(), want) != 0) {
                different = true;
                break;
            }
            oa::base::sha256::update(hasher, std::span<const uint8_t>(from_source.data(), want));
            got += want;
        }
        if (different) {
            remove_file(request.destination);
            prefix = 0;
            hasher = oa::base::sha256::Hasher{};
        } else if (prefix == source_size) {
            result.digest = oa::base::sha256::finish(hasher);
            result.bytes = source_size;
            return true;
        }
    }
    const char* mode = prefix == 0 ? "wb" : "ab";
    OwnedFile destination(oa::platform::open_file(request.destination, mode));
    if (destination.file == nullptr) {
        fail(error, "The copy could not be written");
        return false;
    }
    std::vector<uint8_t> buffer(static_cast<std::size_t>(chunk));
    uint64_t written = prefix;
    while (written < source_size) {
        if (stopped_now(request.stop)) {
            if (!sync_file(destination.file)) {
                fail(error, "The copy could not be stored");
                return false;
            }
            result.stopped = true;
            result.bytes = written;
            return true;
        }
        const uint64_t step = std::min<uint64_t>(chunk, source_size - written);
        const std::size_t want = static_cast<std::size_t>(step);
        if (!read_source(written, std::span<uint8_t>(buffer.data(), want))) {
            if (error != nullptr && error->empty())
                fail(error, "The file could not be read");
            return false;
        }
        oa::base::sha256::update(hasher, std::span<const uint8_t>(buffer.data(), want));
        errno = 0;
        if (std::fwrite(buffer.data(), 1, want, destination.file) != want) {
            if (disk_is_full(errno)) {
                fail(error, "The disk filled while the game was copied");
                result.no_space = true;
            } else {
                fail(error, "The copy could not be written");
            }
            result.bytes = written;
            return false;
        }
        written += want;
        if (request.on_chunk != nullptr)
            request.on_chunk(request.chunk_context, written);
    }
    if (!sync_file(destination.file)) {
        if (disk_is_full(errno)) {
            fail(error, "The disk filled while the game was copied");
            result.no_space = true;
        } else {
            fail(error, "The copy could not be stored");
        }
        result.bytes = written;
        return false;
    }
    result.digest = oa::base::sha256::finish(hasher);
    result.bytes = source_size;
    return true;
}

int run_older_mac_installer_check(const fs::path& image) {
    std::cout << "installer check: " << path_to_utf8(image) << '\n';
    DiskImageContentSource source(image, bundles::bundles());
    SourceDescription described;
    std::string error;
    if (!describe_source(source, bundles::bundles(), described, &error)) {
        std::cout << "installer check: FAILED: " << error << '\n';
        return 1;
    }
    // A bundle with no file in the image is not part of this installer.
    if (described.bundles.empty()) {
        std::cout << "installer check: FAILED: This disk image does not contain the game files\n";
        return 1;
    }
    bool matches = true;
    for (const std::string& id : described.bundles) {
        std::cout << "bundle " << id << '\n';
        const bundles::Bundle* bundle = bundles::find_bundle(id);
        if (bundle == nullptr) {
            matches = false;
            continue;
        }
        for (const bundles::ContentFile& item : bundle->files) {
            const MatchedContentFile* matched = nullptr;
            for (const MatchedContentFile& candidate : described.matched) {
                if (candidate.bundle_id == id && candidate.copy_name == item.copy_name) {
                    matched = &candidate;
                    break;
                }
            }
            if (matched == nullptr) {
                matches = false;
                std::cout << "missing " << item.copy_name << '\n';
                continue;
            }
            oa::base::sha256::Digest digest{};
            if (!source.hash_file(matched->source_name, digest, &error)) {
                std::cout << "installer check: FAILED: " << error << '\n';
                return 1;
            }
            const bool known = bundles::known_hash(item, digest);
            if (!known)
                matches = false;
            const auto hex = oa::base::sha256::to_hex(digest);
            std::cout << "  " << item.copy_name << ' ' << matched->size << ' '
                      << std::string(hex.data(), hex.size()) << (known ? " ok" : " differs")
                      << '\n';
        }
    }
    if (!matches) {
        std::cout << "installer check: FAILED: a file is missing or differs\n";
        return 1;
    }
    std::cout << "installer check: passed\n";
    return 0;
}

} // namespace oa::app
