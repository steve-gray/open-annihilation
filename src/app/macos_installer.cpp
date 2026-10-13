// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The macOS installer as a source of game files. The payload is a cpio
// stream read once: each game file is written as it passes, and the rest of
// the payload is left where it is.

#include "oa/app/content_copy.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/base/sha256.hpp"
#include "oa/formats/cpio.hpp"
#include "oa/formats/xar.hpp"
#include "oa/platform/files.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <ostream>
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

namespace fs = std::filesystem;
namespace bundles = oa::data::content_bundles;

constexpr std::size_t k_read_bytes = 1024 * 1024;

struct OwnedFile {
    std::FILE* file = nullptr;

    ~OwnedFile() { close(); }

    OwnedFile() = default;

    explicit OwnedFile(std::FILE* opened) : file(opened) {}

    OwnedFile(const OwnedFile&) = delete;
    OwnedFile& operator=(const OwnedFile&) = delete;

    OwnedFile(OwnedFile&& other) noexcept : file(other.file) { other.file = nullptr; }

    OwnedFile& operator=(OwnedFile&& other) noexcept {
        if (this != &other) {
            close();
            file = other.file;
            other.file = nullptr;
        }
        return *this;
    }

    void close() noexcept {
        if (file != nullptr)
            std::fclose(file);
        file = nullptr;
    }
};

void set_error(std::string* error, std::string_view text) {
    if (error != nullptr)
        *error = std::string(text);
}

std::string player_error(std::string_view detail) {
    std::string text = "The game could not be copied into Open Annihilation";
    if (!detail.empty()) {
        text.append(": ");
        text.append(detail);
    }
    text.push_back('.');
    return text;
}

void say_line(std::ostream* news, const char* line) {
    if (news != nullptr)
        *news << "open-annihilation: " << line << '\n';
}

std::string sentence(std::string_view detail) {
    if (detail.empty())
        return "The installer could not be read.";
    std::string text(detail);
    if (text.front() >= 'a' && text.front() <= 'z')
        text.front() = static_cast<char>(text.front() - 'a' + 'A');
    if (text.back() != '.')
        text.push_back('.');
    return text;
}

template <typename Clock>
[[nodiscard]] int64_t seconds_of_clock(typename Clock::time_point time) {
    const auto count = [](auto system_time) {
        return std::chrono::floor<std::chrono::seconds>(system_time).time_since_epoch().count();
    };
    if constexpr (requires { Clock::to_sys(time); })
        return static_cast<int64_t>(count(Clock::to_sys(time)));
    else {
        const auto utc = Clock::to_utc(time);
        using Utc = typename decltype(utc)::clock;
        return static_cast<int64_t>(count(Utc::to_sys(utc)));
    }
}

template <typename Clock>
[[nodiscard]] typename Clock::time_point time_of_clock(int64_t seconds) {
    const std::chrono::sys_seconds system{std::chrono::seconds{seconds}};
    if constexpr (requires { Clock::from_sys(system); }) {
        return std::chrono::time_point_cast<typename Clock::duration>(Clock::from_sys(system));
    } else {
        using Utc = typename decltype(Clock::to_utc(typename Clock::time_point{}))::clock;
        return std::chrono::time_point_cast<typename Clock::duration>(
            Clock::from_utc(Utc::from_sys(system))
        );
    }
}

[[nodiscard]] int64_t modified_seconds(fs::file_time_type time) {
    return seconds_of_clock<fs::file_time_type::clock>(time);
}

[[nodiscard]] fs::file_time_type time_from_seconds(int64_t seconds) {
    return time_of_clock<fs::file_time_type::clock>(seconds);
}

[[nodiscard]] char fold_byte(char byte) noexcept {
    if (byte == '\\')
        return '/';
    if (byte >= 'A' && byte <= 'Z')
        return static_cast<char>(byte - 'A' + 'a');
    return byte;
}

[[nodiscard]] bool folded_equal(std::string_view one, std::string_view other) noexcept {
    if (one.size() != other.size())
        return false;
    for (std::size_t index = 0; index < one.size(); ++index) {
        if (fold_byte(one[index]) != fold_byte(other[index]))
            return false;
    }
    return true;
}

[[nodiscard]] bool has_separator(std::string_view path) noexcept {
    for (char byte : path) {
        if (byte == '/' || byte == '\\')
            return true;
    }
    return false;
}

[[nodiscard]] std::string_view last_component(std::string_view path) noexcept {
    std::size_t start = 0;
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (path[index] == '/' || path[index] == '\\')
            start = index + 1;
    }
    return path.substr(start);
}

[[nodiscard]] bool names_match(std::string_view listed, const bundles::ContentFile& file) noexcept {
    if (folded_equal(listed, file.copy_name))
        return true;
    for (std::string_view source_name : file.source_names) {
        if (folded_equal(listed, source_name))
            return true;
    }
    if (has_separator(listed))
        return false;
    if (folded_equal(listed, last_component(file.copy_name)))
        return true;
    for (std::string_view source_name : file.source_names) {
        if (folded_equal(listed, last_component(source_name)))
            return true;
    }
    return false;
}

[[nodiscard]] bool safe_copy_name(std::string_view name) noexcept {
    if (name.empty() || name.front() == '/' || name.front() == '\\')
        return false;
    std::string part;
    for (char byte : name) {
        if (byte == '/' || byte == '\\') {
            if (part.empty() || part == "." || part == "..")
                return false;
            part.clear();
        } else {
            part.push_back(byte);
        }
    }
    return !part.empty() && part != "." && part != "..";
}

[[nodiscard]] fs::path stored_path(const fs::path& folder, std::string_view copy_name) {
    fs::path dest = folder;
    std::string part;
    for (char byte : copy_name) {
        if (byte == '/' || byte == '\\') {
            if (!part.empty()) {
                dest /= path_from_utf8(part);
                part.clear();
            }
        } else {
            part.push_back(byte);
        }
    }
    if (!part.empty())
        dest /= path_from_utf8(part);
    return dest;
}

[[nodiscard]] fs::path partial_path(const fs::path& dest) {
    fs::path partial = dest;
    partial += ".part";
    return partial;
}

[[nodiscard]] uint64_t narrow_size(uintmax_t value) noexcept {
    constexpr auto limit = static_cast<uintmax_t>(std::numeric_limits<uint64_t>::max());
    if (value > limit)
        return std::numeric_limits<uint64_t>::max();
    return static_cast<uint64_t>(value);
}

[[nodiscard]] uint32_t narrow_count(std::size_t count) noexcept {
    constexpr auto limit = static_cast<std::size_t>(std::numeric_limits<uint32_t>::max());
    if (count > limit)
        return std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(count);
}

void add_bytes(uint64_t& total, uint64_t more) noexcept {
    if (total > std::numeric_limits<uint64_t>::max() - more)
        total = std::numeric_limits<uint64_t>::max();
    else
        total += more;
}

[[nodiscard]] bool seek_file(std::FILE* file, uint64_t offset) noexcept {
#if defined(_WIN32)
    return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    return ::fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

[[nodiscard]] bool truncate_file(std::FILE* file, uint64_t length) noexcept {
#if defined(_WIN32)
    return _chsize_s(_fileno(file), static_cast<__int64>(length)) == 0;
#else
    if (length > static_cast<uint64_t>(std::numeric_limits<off_t>::max()))
        return false;
    return ::ftruncate(::fileno(file), static_cast<off_t>(length)) == 0;
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

[[nodiscard]] bool read_full(std::FILE* file, uint8_t* dest, std::size_t bytes) noexcept {
    std::size_t got = 0;
    while (got < bytes) {
        const std::size_t read = std::fread(dest + got, 1, bytes - got, file);
        if (read == 0)
            return false;
        got += read;
    }
    return true;
}

[[nodiscard]] bool hash_file(std::FILE* file, oa::base::sha256::Digest& digest) {
    if (!seek_file(file, 0))
        return false;
    oa::base::sha256::Hasher hasher;
    std::vector<uint8_t> buffer(k_read_bytes);
    while (true) {
        const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), file);
        if (read > 0)
            oa::base::sha256::update(hasher, std::span<const uint8_t>(buffer.data(), read));
        if (read < buffer.size())
            break;
    }
    if (std::ferror(file) != 0)
        return false;
    digest = oa::base::sha256::finish(hasher);
    return true;
}

[[nodiscard]] uint64_t regular_size(const fs::path& path) {
    std::error_code failure;
    if (!fs::is_regular_file(path, failure))
        return 0;
    const auto size = fs::file_size(path, failure);
    if (failure)
        return 0;
    return narrow_size(size);
}

void remove_file(const fs::path& path) {
    std::error_code ignored;
    fs::remove(path, ignored);
}

[[nodiscard]] bool
file_matches_record(const fs::path& dest, const ContentRecordFile& recorded, uint64_t expected) {
    std::error_code failure;
    if (!fs::is_regular_file(dest, failure))
        return false;
    const auto size = fs::file_size(dest, failure);
    if (failure || narrow_size(size) != expected || recorded.size != expected)
        return false;
    const auto stamp = fs::last_write_time(dest, failure);
    if (failure)
        return false;
    return modified_seconds(stamp) == recorded.modified;
}

[[nodiscard]] const ContentRecordFile*
find_recorded(const ContentRecord& record, std::string_view copy_name) noexcept {
    for (const ContentRecordFile& item : record.files) {
        if (item.copy_name == copy_name)
            return &item;
    }
    return nullptr;
}

[[nodiscard]] fs::path existing_ancestor(fs::path directory) {
    std::error_code failure;
    while (!directory.empty()) {
        if (fs::is_directory(directory, failure))
            return directory;
        const fs::path parent = directory.parent_path();
        if (parent == directory)
            break;
        directory = parent;
    }
    return {};
}

[[nodiscard]] FreeSpace query_free(const CopyRequest& request, const fs::path& directory) {
    const fs::path existing = existing_ancestor(directory);
    if (request.free_space != nullptr)
        return request.free_space(request.free_space_context, existing);
    FreeSpace report;
    if (existing.empty())
        return report;
    try {
        const fs::space_info info = fs::space(existing);
        report.bytes = narrow_size(info.available);
        report.known = true;
    } catch (const std::exception&) {
        report.known = false;
    }
    return report;
}

[[nodiscard]] uint64_t chunk_size(uint64_t requested) noexcept {
    if (requested == 0 || requested > content_copy_chunk_bytes)
        return content_copy_chunk_bytes;
    return requested;
}

enum class Job : uint8_t { list, copy, stream, check };

enum class Halt : uint8_t { none, stop, fail };

struct Slot {
    const bundles::Bundle* bundle{};
    const bundles::ContentFile* file{};
    std::size_t bundle_index{};
    bool seen{};
    bool complete{};
    fs::path dest{};
    fs::path partial{};
    uint64_t prefix{};
    oa::base::sha256::Digest digest{};
    bool have_digest{};
};

struct Active {
    bool on{};
    std::size_t slot{std::numeric_limits<std::size_t>::max()};
    OwnedFile part{};
    bool comparing{};
    bool hashing_only{};
    bool rewound{};
    uint64_t prefix{};
    uint64_t filled{};
    oa::base::sha256::Hasher hasher{};
};

struct Pump {
    std::span<const bundles::Bundle> bundles{};
    std::string origin_text{};
    Job job{Job::list};
    const CopyRequest* request{};
    CopyResult* result{};
    std::vector<ContentSourceFile>* listed{};
    const StreamRequest* stream_request{};
    StreamResult* stream_result{};
    std::string* error{};
    std::vector<Slot> slots{};
    std::vector<ContentRecord> records{};
    Active active{};
    Halt halt{Halt::none};
    bool told{};
    bool found_stream{};
    bool no_space{};
    uint64_t chunk{content_copy_chunk_bytes};
    uint64_t done_bytes{};
    uint32_t done_files{};
    uint64_t bytes_total{};

    void fail(std::string_view text) {
        halt = Halt::fail;
        set_error(error, text);
    }

    [[nodiscard]] std::size_t match_index(std::string_view name, uint64_t size) const {
        std::vector<std::string_view> parts;
        std::size_t start = 0;
        for (std::size_t index = 0; index <= name.size(); ++index) {
            if (index != name.size() && name[index] != '/' && name[index] != '\\')
                continue;
            if (index > start)
                parts.push_back(name.substr(start, index - start));
            start = index + 1;
        }
        std::size_t drive = std::numeric_limits<std::size_t>::max();
        for (std::size_t index = 0; index < parts.size(); ++index) {
            if (folded_equal(parts[index], "drive_c")) {
                drive = index;
                break;
            }
        }
        if (drive == std::numeric_limits<std::size_t>::max())
            return std::numeric_limits<std::size_t>::max();
        for (std::size_t from = drive + 1; from < parts.size(); ++from) {
            std::string suffix;
            for (std::size_t index = from; index < parts.size(); ++index) {
                if (!suffix.empty())
                    suffix.push_back('/');
                suffix.append(parts[index]);
            }
            for (std::size_t slot = 0; slot < slots.size(); ++slot) {
                if (slots[slot].file->size != size)
                    continue;
                if (names_match(suffix, *slots[slot].file))
                    return slot;
            }
        }
        return std::numeric_limits<std::size_t>::max();
    }

    void note_progress(const Slot& slot, bool finished) {
        if (request == nullptr || request->on_progress == nullptr || result == nullptr)
            return;
        CopyProgress progress;
        progress.bytes_done = done_bytes;
        if (!finished && active.on)
            add_bytes(progress.bytes_done, active.filled);
        progress.bytes_total = bytes_total;
        progress.files_done = done_files;
        progress.files_total = narrow_count(slots.size());
        progress.bundle_id = slot.bundle->id;
        progress.copy_name = slot.file->copy_name;
        progress.file_finished = finished;
        request->on_progress(request->progress_context, progress);
    }

    [[nodiscard]] bool stopped_now() const noexcept {
        const std::atomic<bool>* flag = nullptr;
        if (job == Job::copy && request != nullptr)
            flag = request->stop;
        if (job == Job::stream && stream_request != nullptr)
            flag = stream_request->stop;
        return flag != nullptr && flag->load();
    }

    [[nodiscard]] bool prepare_slots() {
        if (bundles.empty())
            return true;
        records.assign(bundles.size(), ContentRecord{});
        if (job == Job::copy) {
            for (std::size_t index = 0; index < bundles.size(); ++index) {
                const fs::path record_path = bundle_folder(request->user_folder, bundles[index]) /
                                             std::string(content_record_name);
                std::string problem;
                records[index] = read_content_record(record_path, &problem);
                if (records[index].io_error) {
                    fail(problem.empty() ? "the record could not be read" : problem);
                    return false;
                }
                if (!(records[index].valid && records[index].bundle_id == bundles[index].id))
                    records[index].files.clear();
                records[index].bundle_id = std::string(bundles[index].id);
                records[index].source = origin_text;
                records[index].valid = true;
                records[index].present = true;
            }
        }
        for (std::size_t bundle_index = 0; bundle_index < bundles.size(); ++bundle_index) {
            const bundles::Bundle& bundle = bundles[bundle_index];
            for (const bundles::ContentFile& file : bundle.files) {
                Slot slot;
                slot.bundle = &bundle;
                slot.file = &file;
                slot.bundle_index = bundle_index;
                add_bytes(bytes_total, file.size);
                if (job == Job::copy) {
                    if (!safe_copy_name(file.copy_name)) {
                        fail("a file name cannot be stored");
                        return false;
                    }
                    slot.dest =
                        stored_path(bundle_folder(request->user_folder, bundle), file.copy_name);
                    slot.partial = partial_path(slot.dest);
                    const ContentRecordFile* recorded =
                        find_recorded(records[bundle_index], file.copy_name);
                    slot.complete =
                        recorded != nullptr && file_matches_record(slot.dest, *recorded, file.size);
                    if (!slot.complete) {
                        slot.prefix = regular_size(slot.partial);
                        if (slot.prefix > file.size) {
                            remove_file(slot.partial);
                            slot.prefix = 0;
                        }
                    }
                }
                slots.push_back(std::move(slot));
            }
        }
        if (job != Job::copy)
            return true;
        uint64_t need_bytes = 0;
        for (const Slot& slot : slots) {
            if (!slot.complete)
                add_bytes(need_bytes, slot.file->size);
        }
        if (need_bytes == 0)
            return true;
        const uint64_t need = bytes_to_make_room_for(need_bytes, false);
        const FreeSpace free = query_free(*request, request->user_folder);
        if (!room_is_enough(free.bytes, free.known, need)) {
            no_space = true;
            fail("short space");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool open_active(std::size_t index) {
        Slot& slot = slots[index];
        if (job == Job::check) {
            active = {};
            active.on = true;
            active.slot = index;
            active.hashing_only = true;
            return true;
        }
        if (job == Job::stream) {
            if (found_stream)
                return true;
            if (!names_match(stream_request->name, *slot.file) &&
                stream_request->name != slot.file->copy_name)
                return true;
            found_stream = true;
            active = {};
            active.on = true;
            active.slot = index;
            active.prefix = regular_size(stream_request->destination);
            if (active.prefix > slot.file->size) {
                remove_file(stream_request->destination);
                active.prefix = 0;
            }
            const char* mode = active.prefix == 0 ? "wb" : "r+b";
            active.part = OwnedFile(oa::platform::open_file(stream_request->destination, mode));
            if (active.part.file == nullptr) {
                fail("the copy could not be written");
                return false;
            }
            active.comparing = active.prefix > 0;
            return true;
        }
        if (slot.complete) {
            result->files_skipped =
                narrow_count(static_cast<std::size_t>(result->files_skipped) + 1);
            add_bytes(done_bytes, slot.file->size);
            done_files = narrow_count(static_cast<std::size_t>(done_files) + 1);
            note_progress(slot, true);
            return true;
        }
        if (stopped_now()) {
            halt = Halt::stop;
            return false;
        }
        std::error_code failure;
        fs::create_directories(slot.dest.parent_path(), failure);
        if (failure) {
            fail("the folder could not be created");
            return false;
        }
        remove_file(slot.dest);
        active = {};
        active.on = true;
        active.slot = index;
        active.prefix = slot.prefix;
        const char* mode = active.prefix == 0 ? "wb" : "r+b";
        active.part = OwnedFile(oa::platform::open_file(slot.partial, mode));
        if (active.part.file == nullptr) {
            fail("the copy could not be written");
            return false;
        }
        active.comparing = active.prefix > 0;
        if (!told) {
            say_line(request->news, "Copying the game into Open Annihilation...");
            told = true;
        }
        return true;
    }

    [[nodiscard]] bool write_bytes(std::span<const uint8_t> bytes) {
        if (bytes.empty())
            return true;
        errno = 0;
        if (std::fwrite(bytes.data(), 1, bytes.size(), active.part.file) != bytes.size()) {
            if (disk_is_full(errno)) {
                no_space = true;
                fail("the disk filled while the game was copied");
                return false;
            }
            fail("the copy could not be written");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool take_bytes(std::span<const uint8_t> bytes) {
        while (!bytes.empty()) {
            if (stopped_now()) {
                if (!active.hashing_only && active.part.file != nullptr &&
                    !sync_file(active.part.file)) {
                    fail("the copy could not be stored");
                    return false;
                }
                halt = Halt::stop;
                return false;
            }
            std::size_t step = std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(chunk));
            if (active.comparing) {
                const uint64_t remain = active.prefix - active.filled;
                step = std::min<std::size_t>(step, static_cast<std::size_t>(remain));
                std::vector<uint8_t> have(step);
                const std::size_t got = std::fread(have.data(), 1, step, active.part.file);
                if (std::ferror(active.part.file) != 0) {
                    fail("the copy could not be read");
                    return false;
                }
                std::size_t same = 0;
                while (same < got && have[same] == bytes[same])
                    ++same;
                oa::base::sha256::update(active.hasher, bytes.first(same));
                active.filled += same;
                if (same < step) {
                    active.rewound = true;
                    active.comparing = false;
                    if (!truncate_file(active.part.file, active.filled) ||
                        !seek_file(active.part.file, active.filled)) {
                        fail("the copy could not be written");
                        return false;
                    }
                    const auto rest = bytes.subspan(same, step - same);
                    oa::base::sha256::update(active.hasher, rest);
                    if (!write_bytes(rest))
                        return false;
                    active.filled += rest.size();
                } else if (active.filled >= active.prefix) {
                    active.comparing = false;
                }
            } else {
                const auto piece = bytes.first(step);
                oa::base::sha256::update(active.hasher, piece);
                if (!active.hashing_only && !write_bytes(piece))
                    return false;
                active.filled += step;
            }
            bytes = bytes.subspan(step);
            if (job == Job::copy)
                note_progress(slots[active.slot], false);
            if (job == Job::stream && stream_request->on_chunk != nullptr)
                stream_request->on_chunk(stream_request->chunk_context, active.filled);
        }
        return true;
    }

    [[nodiscard]] bool remember(Slot& slot, int64_t modified) {
        ContentRecord& record = records[slot.bundle_index];
        ContentRecordFile stored;
        stored.copy_name = std::string(slot.file->copy_name);
        stored.size = slot.file->size;
        stored.modified = modified;
        stored.digest = slot.digest;
        bool replaced = false;
        for (ContentRecordFile& item : record.files) {
            if (item.copy_name == stored.copy_name) {
                item = stored;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            record.files.push_back(std::move(stored));
        const fs::path record_path =
            bundle_folder(request->user_folder, *slot.bundle) / std::string(content_record_name);
        std::string problem;
        if (!write_content_record(record_path, record, &problem)) {
            fail(problem.empty() ? "the record could not be written" : problem);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool stamp(const fs::path& path, int64_t modified, int64_t& stored) {
        std::error_code failure;
        fs::last_write_time(path, time_from_seconds(modified), failure);
        if (failure) {
            fail("the copied file's time could not be set");
            return false;
        }
        const auto read_back = fs::last_write_time(path, failure);
        if (failure) {
            fail("the copied file's time could not be set");
            return false;
        }
        stored = modified_seconds(read_back);
        return true;
    }

    [[nodiscard]] bool finish_active() {
        if (!active.on)
            return true;
        Slot& slot = slots[active.slot];
        if (active.filled != slot.file->size) {
            fail("the installer could not be read");
            return false;
        }
        slot.digest = oa::base::sha256::finish(active.hasher);
        slot.have_digest = true;
        if (active.hashing_only) {
            active.on = false;
            return true;
        }
        if (job == Job::stream) {
            if (!sync_file(active.part.file)) {
                fail("the copy could not be stored");
                return false;
            }
            active.part.close();
            stream_result->digest = slot.digest;
            stream_result->bytes = slot.file->size;
            active.on = false;
            return true;
        }
        if (!sync_file(active.part.file)) {
            if (disk_is_full(errno)) {
                no_space = true;
                fail("the disk filled while the game was copied");
                return false;
            }
            fail("the copy could not be stored");
            return false;
        }
        active.part.close();
        int64_t stored_time = 0;
        if (!stamp(slot.partial, cpio_modified, stored_time))
            return false;
        std::error_code failure;
        fs::rename(slot.partial, slot.dest, failure);
        if (failure) {
            remove_file(slot.dest);
            fs::rename(slot.partial, slot.dest, failure);
            if (failure) {
                fail("a copied file could not be stored");
                return false;
            }
        }
        if (!stamp(slot.dest, cpio_modified, stored_time))
            return false;
        if (!remember(slot, stored_time))
            return false;
        const uint64_t fresh = active.rewound ? slot.file->size : slot.file->size - active.prefix;
        add_bytes(result->bytes_copied, fresh);
        result->files_copied = narrow_count(static_cast<std::size_t>(result->files_copied) + 1);
        add_bytes(done_bytes, slot.file->size);
        done_files = narrow_count(static_cast<std::size_t>(done_files) + 1);
        note_progress(slot, true);
        active.on = false;
        return true;
    }

    int64_t cpio_modified{};
};

bool on_begin(void* context, const oa::formats::cpio::File& file, bool& take) {
    auto* pump = static_cast<Pump*>(context);
    take = false;
    if (pump->halt != Halt::none || file.trailer || !file.regular)
        return true;
    if (!oa::formats::cpio::name_is_safe(file.name))
        return true;
    const std::size_t index = pump->match_index(file.name, file.bytes);
    if (index == std::numeric_limits<std::size_t>::max())
        return true;
    Slot& slot = pump->slots[index];
    if (slot.seen) {
        pump->fail("the installer names a file twice");
        return false;
    }
    slot.seen = true;
    if (pump->job == Job::list) {
        ContentSourceFile listed;
        listed.name = std::string(slot.file->copy_name);
        listed.size = file.bytes;
        listed.modified = file.modified;
        pump->listed->push_back(std::move(listed));
        return true;
    }
    if (pump->job == Job::stream) {
        const bool named = pump->stream_request->name == slot.file->copy_name ||
                           names_match(pump->stream_request->name, *slot.file);
        if (!named || pump->found_stream)
            return true;
    }
    pump->cpio_modified = file.modified;
    if (!pump->open_active(index))
        return false;
    take = pump->active.on;
    return true;
}

bool on_data(void* context, std::span<const uint8_t> bytes) {
    auto* pump = static_cast<Pump*>(context);
    if (!pump->active.on)
        return true;
    return pump->take_bytes(bytes);
}

bool on_end(void* context) {
    auto* pump = static_cast<Pump*>(context);
    return pump->finish_active();
}

[[nodiscard]] bool feed_cpio(
    oa::formats::cpio::Reader& reader,
    const oa::formats::cpio::Visit& visit,
    std::span<const uint8_t> bytes,
    Pump& pump
) {
    std::string problem;
    if (reader.feed(bytes, visit, &problem))
        return true;
    if (reader.halted() && pump.halt == Halt::stop)
        return false;
    if (pump.halt == Halt::none)
        pump.fail(problem.empty() ? "the installer could not be read" : problem);
    return false;
}

[[nodiscard]] bool read_gzip(
    std::FILE* file,
    uint64_t bytes,
    oa::formats::cpio::Reader& reader,
    const oa::formats::cpio::Visit& visit,
    Pump& pump
) {
    oa::formats::xar::GzipReader gzip;
    std::vector<uint8_t> input(k_read_bytes);
    std::vector<uint8_t> output(k_read_bytes);
    uint64_t left = bytes;
    bool finished = false;
    while (!finished) {
        if (left == 0) {
            const oa::formats::xar::GzipFeed fed = gzip.feed({}, output);
            if (fed.failed || !fed.finished) {
                pump.fail("the installer could not be read");
                return false;
            }
            if (fed.produced > 0 &&
                !feed_cpio(
                    reader, visit, std::span<const uint8_t>(output.data(), fed.produced), pump
                ))
                return pump.halt == Halt::stop;
            finished = true;
            break;
        }
        const std::size_t want = static_cast<std::size_t>(std::min<uint64_t>(left, input.size()));
        if (!read_full(file, input.data(), want)) {
            pump.fail("the installer could not be read");
            return false;
        }
        left -= want;
        std::size_t used = 0;
        while (used < want) {
            const oa::formats::xar::GzipFeed fed =
                gzip.feed(std::span<const uint8_t>(input.data() + used, want - used), output);
            if (fed.failed) {
                pump.fail("the installer could not be read");
                return false;
            }
            if (fed.produced > 0 &&
                !feed_cpio(
                    reader, visit, std::span<const uint8_t>(output.data(), fed.produced), pump
                ))
                return pump.halt == Halt::stop;
            if (fed.consumed == 0 && fed.produced == 0 && !fed.finished) {
                pump.fail("the installer could not be read");
                return false;
            }
            used += fed.consumed;
            if (fed.finished) {
                if (used != want || left != 0) {
                    pump.fail("the installer could not be read");
                    return false;
                }
                finished = true;
                break;
            }
        }
    }
    return true;
}

[[nodiscard]] bool read_plain(
    std::FILE* file,
    uint64_t bytes,
    oa::formats::cpio::Reader& reader,
    const oa::formats::cpio::Visit& visit,
    Pump& pump
) {
    std::vector<uint8_t> input(k_read_bytes);
    uint64_t left = bytes;
    while (left > 0) {
        const std::size_t want = static_cast<std::size_t>(std::min<uint64_t>(left, input.size()));
        if (!read_full(file, input.data(), want)) {
            pump.fail("the installer could not be read");
            return false;
        }
        left -= want;
        if (!feed_cpio(reader, visit, std::span<const uint8_t>(input.data(), want), pump))
            return pump.halt == Halt::stop;
    }
    return true;
}

} // namespace

struct InstallerWalk {
    [[nodiscard]] static bool run(MacosInstallerSource& source, Pump& pump) {
        pump.bundles = source.bundles_;
        pump.origin_text = source.origin();
        if (!pump.prepare_slots())
            return false;
        if (source.bundles_.empty())
            return true;
        OwnedFile input(oa::platform::open_file(source.file_, "rb"));
        if (input.file == nullptr) {
            pump.fail("the installer could not be read");
            return false;
        }
        if (!seek_file(input.file, source.scripts_at_)) {
            pump.fail("the installer could not be read");
            return false;
        }
        bool gzip = source.outer_gzip_;
        if (!gzip && source.scripts_bytes_ >= 2) {
            uint8_t magic[2];
            if (!read_full(input.file, magic, 2) || !seek_file(input.file, source.scripts_at_)) {
                pump.fail("the installer could not be read");
                return false;
            }
            gzip = magic[0] == 0x1f && magic[1] == 0x8b;
        }
        oa::formats::cpio::Reader reader;
        oa::formats::cpio::Visit visit;
        visit.context = &pump;
        visit.begin = on_begin;
        visit.data = on_data;
        visit.end = on_end;
        const bool streamed =
            gzip ? read_gzip(input.file, source.scripts_bytes_, reader, visit, pump)
                 : read_plain(input.file, source.scripts_bytes_, reader, visit, pump);
        if (!streamed)
            return pump.halt == Halt::stop;
        std::string problem;
        if (reader.halted())
            return pump.halt == Halt::stop;
        if (!reader.finish(&problem)) {
            pump.fail(problem.empty() ? "the installer could not be read" : problem);
            return false;
        }
        if (pump.job == Job::stream && !pump.found_stream) {
            pump.fail("the file could not be found");
            return false;
        }
        return true;
    }
};

MacosInstallerSource::MacosInstallerSource(
    fs::path file, std::span<const bundles::Bundle> bundles, bool require_known
)
    : file_(std::move(file)), bundles_(bundles), require_known_(require_known) {
}

std::string MacosInstallerSource::origin() const {
    return path_to_utf8(file_);
}

bool MacosInstallerSource::ensure(std::string* error, std::ostream* news) {
    if (opened_) {
        if (!ready_)
            set_error(error, open_error_);
        return ready_;
    }
    opened_ = true;
    std::error_code failure;
    if (!fs::is_regular_file(file_, failure)) {
        open_error_ = "the installer could not be read";
        set_error(error, open_error_);
        return false;
    }
    const uint64_t size = narrow_size(fs::file_size(file_, failure));
    if (failure) {
        open_error_ = "the installer could not be read";
        set_error(error, open_error_);
        return false;
    }
    OwnedFile input(oa::platform::open_file(file_, "rb"));
    if (input.file == nullptr) {
        open_error_ = "the installer could not be read";
        set_error(error, open_error_);
        return false;
    }
    if (require_known_) {
        const bundles::Installer* known = bundles::find_installer(size);
        if (known == nullptr || known->kind != bundles::InstallerKind::macos) {
            open_error_ = "this file is not the recognised macOS installer";
            set_error(error, open_error_);
            return false;
        }
        say_line(news, "Checking the installer...");
        oa::base::sha256::Digest digest{};
        if (!hash_file(input.file, digest)) {
            open_error_ = "the installer could not be read";
            set_error(error, open_error_);
            return false;
        }
        if (!bundles::known_installer_hash(*known, digest)) {
            open_error_ = "this file is not the recognised macOS installer";
            set_error(error, open_error_);
            return false;
        }
    }
    uint8_t header[28];
    if (!seek_file(input.file, 0) || !read_full(input.file, header, sizeof header)) {
        open_error_ = "the installer could not be read";
        set_error(error, open_error_);
        return false;
    }
    oa::formats::xar::Archive archive;
    oa::formats::xar::Fault fault = oa::formats::xar::Fault::ok;
    if (!oa::formats::xar::read_header(header, archive, fault)) {
        open_error_ = "the installer could not be read";
        set_error(error, open_error_);
        return false;
    }
    if (archive.toc_compressed_bytes >
        static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
        open_error_ = "the installer could not be read";
        set_error(error, open_error_);
        return false;
    }
    std::vector<uint8_t> toc(static_cast<std::size_t>(archive.toc_compressed_bytes));
    if (!seek_file(input.file, archive.header_bytes) ||
        !read_full(input.file, toc.data(), toc.size()) ||
        !oa::formats::xar::read_toc(toc, archive.toc_bytes, archive.entries, fault)) {
        open_error_ = "the installer could not be read";
        set_error(error, open_error_);
        return false;
    }
    const oa::formats::xar::Entry* scripts =
        oa::formats::xar::find_entry(archive.entries, "package.pkg/Scripts");
    if (scripts == nullptr || scripts->directory ||
        scripts->encoding == oa::formats::xar::Encoding::unsupported) {
        open_error_ = "the installer could not be read";
        set_error(error, open_error_);
        return false;
    }
    if (scripts->heap_offset > std::numeric_limits<uint64_t>::max() - archive.heap_start) {
        open_error_ = "the installer could not be read";
        set_error(error, open_error_);
        return false;
    }
    scripts_at_ = archive.heap_start + scripts->heap_offset;
    scripts_bytes_ = scripts->stored_bytes;
    outer_gzip_ = scripts->encoding == oa::formats::xar::Encoding::gzip;
    if (scripts_at_ > size || scripts_bytes_ > size - scripts_at_) {
        open_error_ = "the installer could not be read";
        set_error(error, open_error_);
        return false;
    }
    ready_ = true;
    return true;
}

bool MacosInstallerSource::open(std::string* error) {
    return ensure(error, nullptr);
}

bool MacosInstallerSource::list(std::vector<ContentSourceFile>& files, std::string* error) {
    files.clear();
    if (!ensure(error, nullptr))
        return false;
    Pump pump;
    pump.job = Job::list;
    pump.listed = &files;
    pump.error = error;
    return InstallerWalk::run(*this, pump);
}

bool MacosInstallerSource::stream_file(
    const StreamRequest& request, StreamResult& result, std::string* error
) {
    result = {};
    if (!ensure(error, nullptr))
        return false;
    Pump pump;
    pump.job = Job::stream;
    pump.stream_request = &request;
    pump.stream_result = &result;
    pump.error = error;
    pump.chunk = chunk_size(request.chunk_bytes);
    if (!InstallerWalk::run(*this, pump)) {
        if (pump.halt == Halt::stop) {
            result.stopped = true;
            result.bytes = pump.active.filled;
            return true;
        }
        result.no_space = pump.no_space;
        return false;
    }
    return true;
}

bool MacosInstallerSource::copy_together(const CopyRequest& request, CopyResult& result) {
    result = {};
    try {
        std::string problem;
        if (!ensure(&problem, request.news)) {
            result.status = CopyStatus::failed;
            result.error = player_error(problem.empty() ? open_error_ : problem);
            return true;
        }
        Pump pump;
        pump.job = Job::copy;
        pump.request = &request;
        pump.result = &result;
        pump.error = &problem;
        pump.chunk = chunk_size(request.chunk_bytes);
        if (!InstallerWalk::run(*this, pump)) {
            if (pump.no_space || problem == "short space") {
                result.status = CopyStatus::no_space;
                result.error = "There is not enough free space to copy the game into Open "
                               "Annihilation. Free some space and start the game again.";
                return true;
            }
            if (pump.halt == Halt::stop) {
                result.status = CopyStatus::stopped;
                result.error = "The copy into Open Annihilation stopped. Start the game again to "
                               "continue it.";
                return true;
            }
            result.status = CopyStatus::failed;
            result.error =
                player_error(problem.empty() ? "the installer could not be read" : problem);
            return true;
        }
        if (pump.told)
            say_line(request.news, "Copied the game into Open Annihilation.");
        result.status = CopyStatus::finished;
        return true;
    } catch (const std::exception& failure) {
        result.status = CopyStatus::failed;
        result.error = player_error(failure.what());
        return true;
    }
}

int MacosInstallerSource::write_check(std::ostream& out) {
    out << "Checking the macOS installer...\n";
    std::string problem;
    if (!ensure(&problem, nullptr)) {
        out << "check-installer: FAILED: " << sentence(problem.empty() ? open_error_ : problem)
            << '\n';
        return 1;
    }
    Pump pump;
    pump.job = Job::check;
    pump.error = &problem;
    if (!InstallerWalk::run(*this, pump)) {
        out << "check-installer: FAILED: "
            << sentence(problem.empty() ? "the installer could not be read" : problem) << '\n';
        return 1;
    }
    out << "macOS installer\n";
    bool missing = false;
    bool unknown = false;
    for (const Slot& slot : pump.slots) {
        if (!slot.seen || !slot.have_digest) {
            out << "missing " << slot.bundle->id << ' ' << slot.file->copy_name << '\n';
            missing = true;
            continue;
        }
        const auto hex = oa::base::sha256::to_hex(slot.digest);
        out << slot.bundle->id << ' ' << slot.file->copy_name << ' ' << slot.file->size << ' ';
        out.write(hex.data(), static_cast<std::streamsize>(hex.size()));
        out << '\n';
        if (!bundles::known_hash(*slot.file, slot.digest))
            unknown = true;
    }
    if (missing || unknown) {
        out << "check-installer: FAILED: "
            << (missing ? "A game file is missing."
                        : "A game file is not the one this installer is known by.")
            << '\n';
        return 1;
    }
    out << "check-installer: passed\n";
    return 0;
}

int report_macos_installer(
    const fs::path& file,
    std::span<const bundles::Bundle> bundles,
    bool require_known,
    std::ostream& out
) {
    MacosInstallerSource source(file, bundles, require_known);
    return source.write_check(out);
}

int run_macos_installer_check(const fs::path& file) {
    return report_macos_installer(file, bundles::bundles(), true, std::cout);
}

} // namespace oa::app
