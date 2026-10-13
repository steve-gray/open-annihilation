// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Block sharing for one file (file_clone.hpp). APFS uses clonefile, btrfs
// and XFS use the FICLONE ioctl, and ReFS uses a duplicate-extents control.
// Anything else, including a share that the volume refuses, leaves no
// destination file behind.
#include "oa/platform/file_clone.hpp"

#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#ifndef FSCTL_DUPLICATE_EXTENTS_TO_FILE
#define FSCTL_DUPLICATE_EXTENTS_TO_FILE 0x00098344u
#endif
// The structure XP-era headers omit. HANDLE then three 64-bit offsets, with
// the padding a 32-bit build inserts before the first offset. Used even when
// the header has its own, so the layout does not depend on which one.
typedef struct OaDuplicateExtents {
    HANDLE FileHandle;
    LARGE_INTEGER SourceFileOffset;
    LARGE_INTEGER TargetFileOffset;
    LARGE_INTEGER ByteCount;
} OaDuplicateExtents;
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <sys/clonefile.h>
#include <sys/mount.h>
#ifndef CLONE_NOFOLLOW
#define CLONE_NOFOLLOW 0x0001u
#endif
#elif defined(__linux__)
#include <sys/ioctl.h>
#include <sys/statfs.h>
#ifndef FICLONE
#define FICLONE _IOW(0x94, 9, int)
#endif
#endif

namespace oa::platform {
namespace {

void assign_error(std::string* error, const std::string& text) {
    if (error != nullptr)
        *error = text;
}

#if defined(_WIN32)

bool same_volume_path(const wchar_t* one, const wchar_t* other) noexcept {
    while (*one != L'\0' && *other != L'\0') {
        wchar_t first = *one++;
        wchar_t second = *other++;
        if (first >= L'A' && first <= L'Z')
            first = static_cast<wchar_t>(first - L'A' + L'a');
        if (second >= L'A' && second <= L'Z')
            second = static_cast<wchar_t>(second - L'A' + L'a');
        if (first != second)
            return false;
    }
    return *one == *other;
}

bool volume_is_refs(const wchar_t* root) noexcept {
    wchar_t name[MAX_PATH]{};
    if (GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, name, MAX_PATH) == 0)
        return false;
    return same_volume_path(name, L"ReFS");
}

bool windows_can_share(
    const std::filesystem::path& source, const std::filesystem::path& directory
) noexcept {
    wchar_t source_root[MAX_PATH]{};
    wchar_t directory_root[MAX_PATH]{};
    if (GetVolumePathNameW(source.c_str(), source_root, MAX_PATH) == 0)
        return false;
    if (GetVolumePathNameW(directory.c_str(), directory_root, MAX_PATH) == 0)
        return false;
    if (!same_volume_path(source_root, directory_root))
        return false;
    return volume_is_refs(directory_root);
}

struct WinHandle {
    HANDLE value = INVALID_HANDLE_VALUE;

    ~WinHandle() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }

    WinHandle() = default;
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
};

bool windows_share(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::string* error
) {
    WinHandle input;
    input.value = CreateFileW(
        source.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (input.value == INVALID_HANDLE_VALUE) {
        assign_error(error, "the file's blocks could not be shared");
        return false;
    }
    WinHandle output;
    output.value = CreateFileW(
        destination.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (output.value == INVALID_HANDLE_VALUE) {
        assign_error(error, "the file's blocks could not be shared");
        return false;
    }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(input.value, &size) == 0) {
        CloseHandle(output.value);
        output.value = INVALID_HANDLE_VALUE;
        DeleteFileW(destination.c_str());
        assign_error(error, "the file's blocks could not be shared");
        return false;
    }
    OaDuplicateExtents extents{};
    extents.FileHandle = input.value;
    extents.SourceFileOffset.QuadPart = 0;
    extents.TargetFileOffset.QuadPart = 0;
    extents.ByteCount = size;
    DWORD returned = 0;
    const BOOL shared = DeviceIoControl(
        output.value,
        FSCTL_DUPLICATE_EXTENTS_TO_FILE,
        &extents,
        sizeof(extents),
        nullptr,
        0,
        &returned,
        nullptr
    );
    if (shared == 0) {
        CloseHandle(output.value);
        output.value = INVALID_HANDLE_VALUE;
        DeleteFileW(destination.c_str());
        assign_error(error, "the file's blocks could not be shared");
        return false;
    }
    return true;
}

#elif defined(__APPLE__) || defined(__linux__)

bool same_device(const std::filesystem::path& one, const std::filesystem::path& other) noexcept {
    struct stat one_info{};
    struct stat other_info{};
    if (::stat(one.c_str(), &one_info) != 0)
        return false;
    if (::stat(other.c_str(), &other_info) != 0)
        return false;
    return one_info.st_dev == other_info.st_dev;
}

#if defined(__APPLE__)

bool volume_shares_blocks(const std::filesystem::path& path) noexcept {
    struct statfs info{};
    if (::statfs(path.c_str(), &info) != 0)
        return false;
    return std::strcmp(info.f_fstypename, "apfs") == 0;
}

#else

/// btrfs and XFS, the Linux volumes whose ioctl shares a file's blocks.
bool volume_shares_blocks(const std::filesystem::path& path) noexcept {
    struct statfs info{};
    if (::statfs(path.c_str(), &info) != 0)
        return false;
    const auto kind = static_cast<unsigned long>(info.f_type);
    constexpr unsigned long btrfs = 0x9123683EUL;
    constexpr unsigned long xfs = 0x58465342UL;
    return kind == btrfs || kind == xfs;
}

struct OwnedFd {
    int value = -1;

    ~OwnedFd() {
        if (value >= 0)
            ::close(value);
    }

    OwnedFd() = default;
    OwnedFd(const OwnedFd&) = delete;
    OwnedFd& operator=(const OwnedFd&) = delete;
};

#endif

#endif

} // namespace

bool blocks_can_be_shared(
    const std::filesystem::path& source, const std::filesystem::path& destination_directory
) noexcept {
    try {
#if defined(_WIN32)
        return windows_can_share(source, destination_directory);
#elif defined(__APPLE__) || defined(__linux__)
        if (!same_device(source, destination_directory))
            return false;
        return volume_shares_blocks(source) && volume_shares_blocks(destination_directory);
#else
        (void)source;
        (void)destination_directory;
        return false;
#endif
    } catch (...) {
        return false;
    }
}

bool share_file_blocks(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::string* error
) noexcept {
    try {
#if defined(_WIN32)
        return windows_share(source, destination, error);
#elif defined(__APPLE__)
        if (::clonefile(source.c_str(), destination.c_str(), CLONE_NOFOLLOW) != 0) {
            assign_error(error, "the file's blocks could not be shared");
            return false;
        }
        return true;
#elif defined(__linux__)
        OwnedFd input;
        input.value = ::open(source.c_str(), O_RDONLY | O_CLOEXEC);
        if (input.value < 0) {
            assign_error(error, "the file's blocks could not be shared");
            return false;
        }
        OwnedFd output;
        output.value = ::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (output.value < 0) {
            assign_error(error, "the file's blocks could not be shared");
            return false;
        }
        if (::ioctl(output.value, FICLONE, input.value) != 0) {
            const int descriptor = output.value;
            output.value = -1;
            ::close(descriptor);
            ::unlink(destination.c_str());
            assign_error(error, "the file's blocks could not be shared");
            return false;
        }
        return true;
#else
        (void)source;
        (void)destination;
        assign_error(error, "the file's blocks could not be shared");
        return false;
#endif
    } catch (...) {
        assign_error(error, "the file's blocks could not be shared");
        return false;
    }
}

} // namespace oa::platform
