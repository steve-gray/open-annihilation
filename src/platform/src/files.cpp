// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/files.hpp"

#include "oa/base/threads.hpp"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <share.h>
#else
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace oa::platform {
namespace {

std::FILE* stream(FileHandle* file) noexcept {
    return reinterpret_cast<std::FILE*>(file);
}

FileHandle* stdio_open(void*, const char* path, const char* mode) {
    return reinterpret_cast<FileHandle*>(open_file(path, mode));
}

std::size_t stdio_read(void*, FileHandle* file, void* buffer, std::size_t size, std::size_t count) {
    return std::fread(buffer, size, count, stream(file));
}

std::size_t
stdio_write(void*, FileHandle* file, const void* buffer, std::size_t size, std::size_t count) {
    return std::fwrite(buffer, size, count, stream(file));
}

int64_t stdio_tell(void*, FileHandle* file) {
#if defined(_WIN32)
    return _ftelli64(stream(file));
#else
    return static_cast<int64_t>(ftello(stream(file)));
#endif
}

int32_t stdio_seek(void*, FileHandle* file, int64_t offset, SeekOrigin origin) {
    const int whence = origin == SeekOrigin::begin     ? SEEK_SET
                       : origin == SeekOrigin::current ? SEEK_CUR
                                                       : SEEK_END;
#if defined(_WIN32)
    return _fseeki64(stream(file), offset, whence);
#else
    return fseeko(stream(file), static_cast<off_t>(offset), whence);
#endif
}

void stdio_close(void*, FileHandle* file) {
    if (file) {
        std::fclose(stream(file));
    }
}

/// Holds the lock that keeps log lines whole without ever destroying it, so
/// that a thread still logging while the program exits finds it intact.
union LogLock {
    constexpr LogLock() : mutex() {}

    ~LogLock() {}

    LogLock(const LogLock&) = delete;
    LogLock& operator=(const LogLock&) = delete;
    base::threads::Mutex mutex;
};

constinit LogLock log_lock;

} // namespace

std::FILE* open_file(const char* path, const char* mode) noexcept {
#if defined(_WIN32)
    // The narrow path is read in the code page the system's narrow file calls
    // use, as std::filesystem::path reads it, and opened by its wide spelling,
    // which may be longer than 259 characters where long paths are on.
    try {
        return open_file(std::filesystem::path(path), mode);
    } catch (const std::exception&) {
        return nullptr;
    }
#else
    return std::fopen(path, mode);
#endif
}

std::FILE* open_file(const std::filesystem::path& path, const char* mode) noexcept {
#if defined(_WIN32)
    // A mode is a few ASCII letters; one too long to be valid opens nothing.
    constexpr std::size_t mode_capacity = 16;
    wchar_t wide_mode[mode_capacity]{};
    for (std::size_t index = 0; mode[index] != '\0'; ++index) {
        if (index + 1 >= mode_capacity)
            return nullptr;
        wide_mode[index] = static_cast<wchar_t>(static_cast<unsigned char>(mode[index]));
    }
    return _wfsopen(path.c_str(), wide_mode, _SH_DENYNO);
#else
    return std::fopen(path.c_str(), mode);
#endif
}

bool long_paths_turned_off() noexcept {
#if defined(_WIN32)
    // Windows 10, version 1607, and later say whether this program may open
    // long paths: the system's setting and the program's manifest both allow
    // them. Earlier systems never do.
    using AreLongPathsEnabled = BOOLEAN(WINAPI*)();
    const HMODULE system = GetModuleHandleW(L"ntdll.dll");
    const auto query =
        system != nullptr
            ? reinterpret_cast<AreLongPathsEnabled>(
                  reinterpret_cast<void*>(GetProcAddress(system, "RtlAreLongPathsEnabled"))
              )
            : nullptr;
    return query == nullptr || query() == FALSE;
#else
    return false;
#endif
}

std::size_t longest_path() noexcept {
#if defined(_WIN32)
    // The longest path the system's wide calls take, and the classic limit.
    constexpr std::size_t extended_path_characters = 32767;
    return long_paths_turned_off() ? MAX_PATH - 1 : extended_path_characters;
#else
    return PATH_MAX - 1;
#endif
}

Files stdio_files() noexcept {
    return {nullptr, stdio_open, stdio_read, stdio_write, stdio_tell, stdio_seek, stdio_close};
}

int log_message(const char* format, ...) noexcept {
    const base::threads::LockGuard guard(log_lock.mutex);
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    return written;
}

namespace {

std::atomic<unsigned long> temporary_sequence{0};

/// A path's UTF-8 spelling, for a refusal.
///
/// @param path the path
/// @return its UTF-8 spelling
std::string path_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

/// The code the last file call left, taken before another call replaces it.
///
/// @return the system's code
int last_error() noexcept {
#if defined(_WIN32)
    return static_cast<int>(GetLastError());
#else
    return errno;
#endif
}

/// Sets a refusal, including the system's account of it.
///
/// @param error where the refusal is written; may be null
/// @param action what failed
/// @param path the file it failed on
/// @param code the system's code, captured at the failure
void set_error(
    std::string* error, const char* action, const std::filesystem::path& path, int code
) {
    if (error == nullptr)
        return;
#if defined(_WIN32)
    const auto reason = std::system_category().message(code);
#else
    const auto reason = std::generic_category().message(code);
#endif
    *error = std::string(action) + ": " + path_text(path) + " (" + reason + ")";
}

/// Removes a temporary file when a replace does not finish.
struct TemporaryFile {
    std::filesystem::path path{};
    bool owned = false;

    TemporaryFile() = default;
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    ~TemporaryFile() {
        if (owned) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    }
};

} // namespace

bool replace_file(
    const std::filesystem::path& file, std::span<const uint8_t> bytes, std::string* error
) noexcept {
    // A failure before the rename leaves the previous file in place. Once the
    // rename has happened the temporary name is the file, so it must not be
    // removed; a folder sync that fails afterwards is still a success.
    try {
        if (file.has_parent_path())
            std::filesystem::create_directories(file.parent_path());
#if defined(_WIN32)
        const auto process = GetCurrentProcessId();
#else
        const auto process = static_cast<unsigned long>(::getpid());
#endif
        auto temporary = file;
        temporary += ".tmp-" + std::to_string(process) + "-" +
                     std::to_string(temporary_sequence.fetch_add(1));
        TemporaryFile cleanup;
        cleanup.path = temporary;
#if defined(_WIN32)
        HANDLE handle = CreateFileW(
            temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr
        );
        if (handle == INVALID_HANDLE_VALUE) {
            set_error(error, "cannot create the temporary file", temporary, last_error());
            return false;
        }
        cleanup.owned = true;
        std::size_t position = 0;
        bool complete = true;
        int failure = 0;
        while (position < bytes.size()) {
            const auto remaining = bytes.size() - position;
            const DWORD chunk = static_cast<DWORD>(std::min(remaining, std::size_t{1} << 30));
            DWORD written = 0;
            if (!WriteFile(handle, bytes.data() + position, chunk, &written, nullptr) ||
                written == 0) {
                failure = last_error();
                complete = false;
                break;
            }
            position += written;
        }
        if (complete && FlushFileBuffers(handle) == 0) {
            failure = last_error();
            complete = false;
        }
        if (CloseHandle(handle) == 0) {
            if (complete)
                failure = last_error();
            complete = false;
        }
        if (!complete) {
            set_error(error, "cannot write the temporary file", temporary, failure);
            return false;
        }
        // The replace is flushed through to the disk, so the folder needs no
        // sync of its own.
        if (!MoveFileExW(
                temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
            )) {
            set_error(error, "cannot replace the file", file, last_error());
            return false;
        }
#else
        const int descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor < 0) {
            set_error(error, "cannot create the temporary file", temporary, last_error());
            return false;
        }
        cleanup.owned = true;
        std::size_t position = 0;
        bool complete = true;
        int failure = 0;
        while (position < bytes.size()) {
            const auto written =
                ::write(descriptor, bytes.data() + position, bytes.size() - position);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0) {
                failure = last_error();
                complete = false;
                break;
            }
            position += static_cast<std::size_t>(written);
        }
        if (complete && ::fsync(descriptor) != 0) {
            failure = last_error();
            complete = false;
        }
        if (::close(descriptor) != 0) {
            if (complete)
                failure = last_error();
            complete = false;
        }
        if (!complete) {
            set_error(error, "cannot write the temporary file", temporary, failure);
            return false;
        }
        bool renamed = false;
        for (;;) {
            if (::rename(temporary.c_str(), file.c_str()) == 0) {
                renamed = true;
                break;
            }
            if (errno != EINTR)
                break;
        }
        if (!renamed) {
            set_error(error, "cannot replace the file", file, last_error());
            return false;
        }
#endif
        cleanup.owned = false;
#if !defined(_WIN32)
        const auto folder =
            file.has_parent_path() ? file.parent_path() : std::filesystem::path(".");
        const int folder_descriptor = ::open(folder.c_str(), O_RDONLY | O_DIRECTORY);
        if (folder_descriptor >= 0) {
            (void)::fsync(folder_descriptor);
            (void)::close(folder_descriptor);
        }
#endif
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            try {
                *error = exception.what();
            } catch (const std::exception&) {
            }
        }
        return false;
    } catch (...) {
        if (error != nullptr) {
            try {
                *error = "the file could not be replaced";
            } catch (const std::exception&) {
            }
        }
        return false;
    }
}

} // namespace oa::platform
