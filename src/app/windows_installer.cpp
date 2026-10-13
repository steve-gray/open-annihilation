// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A Windows installer as a source of game files (content_copy.hpp). The
// installer is recognised by its size and SHA-256, then its installed files
// are listed and one named file is streamed into the copy. The installer is
// only read: nothing in it is run, and its bytes are not written out beside
// the copy.

#include "oa/app/content_copy.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/base/sha256.hpp"
#include "oa/formats/inno_setup/installer.hpp"
#include "oa/platform/files.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
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
namespace inno = oa::formats::inno_setup;

constexpr std::string_view k_unrecognised = "This installer is not recognised.";
constexpr std::string_view k_unreadable = "This installer could not be read.";
constexpr uint64_t k_hash_chunk = uint64_t{1024} * 1024;

void set_error(std::string* error, std::string_view text) {
    if (error != nullptr)
        *error = std::string(text);
}

struct HeldFile {
    std::FILE* file = nullptr;

    ~HeldFile() {
        if (file != nullptr)
            std::fclose(file);
    }

    HeldFile() = default;

    explicit HeldFile(std::FILE* opened) : file(opened) {}

    HeldFile(const HeldFile&) = delete;
    HeldFile& operator=(const HeldFile&) = delete;
};

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

[[nodiscard]] bool names_equal(std::string_view first, std::string_view second) noexcept {
    if (first.size() != second.size())
        return false;
    for (size_t index = 0; index < first.size(); ++index) {
        unsigned char a = static_cast<unsigned char>(first[index]);
        unsigned char b = static_cast<unsigned char>(second[index]);
        if (a == '\\')
            a = '/';
        if (b == '\\')
            b = '/';
        if (a >= 'A' && a <= 'Z')
            a = static_cast<unsigned char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<unsigned char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

[[nodiscard]] bool hash_whole(
    const fs::path& path, uint64_t size, oa::base::sha256::Digest& digest, std::string* error
) {
    HeldFile input(oa::platform::open_file(path, "rb"));
    if (input.file == nullptr) {
        set_error(error, k_unreadable);
        return false;
    }
    oa::base::sha256::Hasher hasher;
    std::vector<uint8_t> buffer(static_cast<size_t>(k_hash_chunk));
    uint64_t got = 0;
    while (got < size) {
        const uint64_t step = std::min<uint64_t>(buffer.size(), size - got);
        const size_t want = static_cast<size_t>(step);
        const size_t read = std::fread(buffer.data(), 1, want, input.file);
        if (read != want) {
            set_error(error, k_unreadable);
            return false;
        }
        oa::base::sha256::update(hasher, std::span<const uint8_t>(buffer.data(), read));
        got += read;
    }
    digest = oa::base::sha256::finish(hasher);
    return true;
}

[[nodiscard]] uint64_t regular_size(const fs::path& path) {
    std::error_code failure;
    if (!fs::is_regular_file(path, failure) || failure)
        return 0;
    const auto size = fs::file_size(path, failure);
    if (failure || size > std::numeric_limits<uint64_t>::max())
        return 0;
    return static_cast<uint64_t>(size);
}

void remove_file(const fs::path& path) {
    std::error_code ignored;
    fs::remove(path, ignored);
}

/// Bytes of one installed file, compared with a copy already on disk and then
/// appended. A checksum failure never reaches this: the reader checks a piece
/// before it calls back.
struct Sink {
    const StreamRequest* request{};
    uint64_t prefix{};
    uint64_t compared{};
    uint64_t written{};
    bool mismatch{};
    bool stopped{};
    bool no_space{};
    bool failed{};
    std::string problem;
    oa::base::sha256::Hasher hasher{};
    std::FILE* existing{};
    std::FILE* destination{};

    ~Sink() {
        if (existing != nullptr)
            std::fclose(existing);
        if (destination != nullptr)
            std::fclose(destination);
    }

    void close_files() {
        if (existing != nullptr) {
            std::fclose(existing);
            existing = nullptr;
        }
        if (destination != nullptr) {
            std::fclose(destination);
            destination = nullptr;
        }
    }

    void rewind_copy() {
        close_files();
        prefix = 0;
        compared = 0;
        written = 0;
        mismatch = false;
        stopped = false;
        no_space = false;
        failed = false;
        problem.clear();
        hasher = {};
    }
};

bool take_bytes(void* context, std::span<const uint8_t> bytes) {
    auto* sink = static_cast<Sink*>(context);
    size_t at = 0;
    while (at < bytes.size() && sink->compared < sink->prefix) {
        uint8_t held[4096];
        const uint64_t room = std::min<uint64_t>(sizeof held, bytes.size() - at);
        const uint64_t step = std::min<uint64_t>(room, sink->prefix - sink->compared);
        const size_t want = static_cast<size_t>(step);
        const size_t got = std::fread(held, 1, want, sink->existing);
        if (std::ferror(sink->existing) != 0) {
            sink->failed = true;
            sink->problem = "the copy could not be read";
            return false;
        }
        if (got != want || !std::equal(held, held + got, bytes.data() + at)) {
            sink->mismatch = true;
            return false;
        }
        oa::base::sha256::update(sink->hasher, std::span<const uint8_t>(bytes.data() + at, got));
        sink->compared += got;
        at += got;
    }
    if (at == bytes.size())
        return true;
    if (sink->request->stop != nullptr && sink->request->stop->load()) {
        sink->stopped = true;
        return false;
    }
    if (sink->destination == nullptr) {
        const char* mode = sink->prefix == 0 ? "wb" : "ab";
        sink->destination = oa::platform::open_file(sink->request->destination, mode);
        if (sink->destination == nullptr) {
            sink->failed = true;
            sink->problem = "the copy could not be written";
            return false;
        }
    }
    const size_t want = bytes.size() - at;
    errno = 0;
    const size_t put = std::fwrite(bytes.data() + at, 1, want, sink->destination);
    if (put != want) {
        if (disk_is_full(errno)) {
            sink->no_space = true;
            sink->problem = "the disk filled while the game was copied";
        } else {
            sink->failed = true;
            sink->problem = "the copy could not be written";
        }
        return false;
    }
    oa::base::sha256::update(sink->hasher, std::span<const uint8_t>(bytes.data() + at, put));
    sink->written += put;
    if (sink->request->on_chunk != nullptr)
        sink->request->on_chunk(sink->request->chunk_context, sink->prefix + sink->written);
    return true;
}

struct HashSink {
    oa::base::sha256::Hasher hasher{};
};

bool hash_bytes(void* context, std::span<const uint8_t> bytes) {
    auto* sink = static_cast<HashSink*>(context);
    oa::base::sha256::update(sink->hasher, bytes);
    return true;
}

[[nodiscard]] bool
listed_size(inno::Reader& reader, std::string_view name, uint64_t& size, std::string* error) {
    std::vector<inno::File> files;
    if (!reader.list(files, error))
        return false;
    for (const inno::File& file : files) {
        if (names_equal(file.path, name)) {
            size = file.size;
            return true;
        }
    }
    set_error(error, "the file could not be found");
    return false;
}

} // namespace

struct WindowsInstallerSource::State {
    fs::path path;
    std::unique_ptr<inno::Reader> reader;
    bool opened{};
};

WindowsInstallerSource::WindowsInstallerSource(
    fs::path file, [[maybe_unused]] std::span<const bundles::Bundle> bundles
)
    : state_(std::make_unique<State>()) {
    state_->path = std::move(file);
}

WindowsInstallerSource::~WindowsInstallerSource() = default;

std::string WindowsInstallerSource::origin() const {
    return path_to_utf8(state_->path);
}

bool WindowsInstallerSource::open(std::string* error) {
    if (state_->opened)
        return true;
    std::error_code failure;
    if (!fs::is_regular_file(state_->path, failure) || failure) {
        set_error(error, k_unreadable);
        return false;
    }
    const auto raw = fs::file_size(state_->path, failure);
    if (failure || raw > std::numeric_limits<uint64_t>::max()) {
        set_error(error, k_unreadable);
        return false;
    }
    const uint64_t size = static_cast<uint64_t>(raw);
    const bundles::Installer* known = bundles::find_installer(size);
    if (known == nullptr || known->kind != bundles::InstallerKind::windows) {
        set_error(error, k_unrecognised);
        return false;
    }
    oa::base::sha256::Digest digest{};
    if (!hash_whole(state_->path, size, digest, error))
        return false;
    if (!bundles::known_installer_hash(*known, digest)) {
        set_error(error, k_unrecognised);
        return false;
    }
    auto reader = std::make_unique<inno::Reader>();
    if (!reader->open(state_->path, nullptr)) {
        set_error(error, k_unreadable);
        return false;
    }
    state_->reader = std::move(reader);
    state_->opened = true;
    return true;
}

bool WindowsInstallerSource::list(std::vector<ContentSourceFile>& files, std::string* error) {
    files.clear();
    if (!open(error))
        return false;
    std::vector<inno::File> listed;
    if (!state_->reader->list(listed, nullptr)) {
        set_error(error, k_unreadable);
        return false;
    }
    files.reserve(listed.size());
    for (const inno::File& item : listed) {
        ContentSourceFile file;
        file.name = item.path;
        file.size = item.size;
        file.modified = item.modified;
        files.push_back(std::move(file));
    }
    return true;
}

bool WindowsInstallerSource::stream_file(
    const StreamRequest& request, StreamResult& result, std::string* error
) {
    result = {};
    if (!open(error))
        return false;
    uint64_t file_size = 0;
    if (!listed_size(*state_->reader, request.name, file_size, error))
        return false;
    uint64_t prefix = regular_size(request.destination);
    if (prefix > file_size) {
        remove_file(request.destination);
        prefix = 0;
    }
    if (request.stop != nullptr && request.stop->load() && prefix == 0) {
        result.stopped = true;
        return true;
    }

    bool retried = false;
    Sink sink;
    sink.request = &request;
    sink.prefix = prefix;
    if (prefix > 0) {
        sink.existing = oa::platform::open_file(request.destination, "rb");
        if (sink.existing == nullptr) {
            set_error(error, "the copy could not be read");
            return false;
        }
    }

    for (;;) {
        inno::ReadRequest read;
        read.name = request.name;
        read.write = take_bytes;
        read.context = &sink;
        inno::ReadResult produced;
        std::string detail;
        const bool ok = state_->reader->read(read, produced, &detail);
        (void)produced;
        if (sink.mismatch && !retried) {
            retried = true;
            sink.rewind_copy();
            remove_file(request.destination);
            continue;
        }
        if (sink.stopped) {
            if (sink.destination != nullptr && !sync_file(sink.destination)) {
                if (disk_is_full(errno)) {
                    result.no_space = true;
                    set_error(error, "the disk filled while the game was copied");
                    return false;
                }
                set_error(error, "the copy could not be stored");
                return false;
            }
            result.stopped = true;
            result.bytes = sink.prefix + sink.written;
            return true;
        }
        if (sink.no_space) {
            result.no_space = true;
            set_error(error, sink.problem);
            return false;
        }
        if (sink.failed || sink.mismatch) {
            set_error(error, sink.problem.empty() ? "the copy could not be written" : sink.problem);
            return false;
        }
        if (!ok) {
            set_error(error, detail.empty() ? "the file could not be read" : detail);
            return false;
        }
        if (sink.destination == nullptr && sink.prefix == 0) {
            sink.destination = oa::platform::open_file(request.destination, "wb");
            if (sink.destination == nullptr) {
                set_error(error, "the copy could not be written");
                return false;
            }
        }
        if (sink.destination != nullptr && !sync_file(sink.destination)) {
            if (disk_is_full(errno)) {
                result.no_space = true;
                set_error(error, "the disk filled while the game was copied");
                return false;
            }
            set_error(error, "the copy could not be stored");
            return false;
        }
        result.digest = oa::base::sha256::finish(sink.hasher);
        result.bytes = file_size;
        return true;
    }
}

bool WindowsInstallerSource::hash_file(
    std::string_view name, oa::base::sha256::Digest& digest, std::string* error
) {
    if (!open(error))
        return false;
    HashSink sink;
    inno::ReadRequest read;
    read.name = name;
    read.write = hash_bytes;
    read.context = &sink;
    inno::ReadResult produced;
    std::string detail;
    if (!state_->reader->read(read, produced, &detail) || produced.stopped) {
        set_error(error, detail.empty() ? "the file could not be read" : detail);
        return false;
    }
    digest = oa::base::sha256::finish(sink.hasher);
    return true;
}

int run_windows_installer_check(const fs::path& file) {
    WindowsInstallerSource source(file, bundles::bundles());
    std::string problem;
    if (!source.open(&problem)) {
        std::cout << (problem.empty() ? std::string(k_unreadable) : problem) << '\n';
        std::cout << "installer check: FAILED\n";
        return 1;
    }
    SourceDescription described;
    if (!describe_source(source, bundles::bundles(), described, &problem)) {
        std::cout << (problem.empty() ? std::string(k_unreadable) : problem) << '\n';
        std::cout << "installer check: FAILED\n";
        return 1;
    }
    std::cout << "Windows installer: " << path_to_utf8(file) << '\n';
    if (described.bundles.empty()) {
        std::cout << "installer check: FAILED\n";
        return 1;
    }
    bool passed = true;
    for (const std::string& id : described.bundles) {
        std::cout << "bundle " << id << '\n';
        const bundles::Bundle* bundle = bundles::find_bundle(id);
        if (bundle == nullptr) {
            passed = false;
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
                std::cout << "  " << std::string(item.copy_name) << " missing\n";
                passed = false;
                continue;
            }
            oa::base::sha256::Digest digest{};
            if (!source.hash_file(matched->source_name, digest, &problem)) {
                std::cout << "  " << std::string(item.copy_name) << " could not be read\n";
                passed = false;
                continue;
            }
            const auto hex = oa::base::sha256::to_hex(digest);
            std::cout << "  " << std::string(item.copy_name) << ' ';
            std::cout.write(hex.data(), static_cast<std::streamsize>(hex.size()));
            if (bundles::known_hash(item, digest)) {
                std::cout << " ok\n";
            } else {
                std::cout << " does not match\n";
                passed = false;
            }
        }
    }
    if (!passed) {
        std::cout << "installer check: FAILED\n";
        return 1;
    }
    std::cout << "installer check: passed\n";
    return 0;
}

} // namespace oa::app
