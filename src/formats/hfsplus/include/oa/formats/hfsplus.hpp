// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// HFS+ volumes: the volume header, the catalogue B-tree (folders, files and
// threads) and the extents overflow tree. Data forks are read uncompressed.
// A file marked compressed is refused, with a message. The volume is read
// through a hook, so this module does not open files and does not depend
// on any other format.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace oa::formats::hfsplus {

/// Bytes read from a volume or a whole disk. `read` copies `count` bytes
/// from `offset`.
struct ReadHooks {
    /// Passed to `read`.
    void* context{};
    /// Copies `count` bytes from `offset` into `out`.
    ///
    /// @param context the value stored beside this function
    /// @param offset the first byte, from the start of the volume or disk
    /// @param out the buffer, with room for `count` bytes
    /// @param count how many bytes to copy
    /// @return true when `out` holds those bytes
    bool (*read)(void* context, uint64_t offset, uint8_t* out, size_t count){};
};

/// One catalogue record.
struct CatalogItem {
    /// What the record describes.
    enum class Kind : uint8_t {
        folder,        ///< a directory
        file,          ///< a file, including a symbolic link
        folder_thread, ///< the thread that names a directory
        file_thread,   ///< the thread that names a file
    };

    /// Which record this is.
    Kind kind{};
    /// The parent catalogue id. The root folder's parent is 1.
    uint32_t parent{};
    /// The catalogue id. A thread uses the id of the item it names.
    uint32_t id{};
    /// The name, in Unicode, without a volume name on the root.
    std::string name;
    /// The data fork's size, in bytes. Zero for a folder or a thread.
    uint64_t data_bytes{};
    /// The content modification time, seconds since 1970. Zero when the
    /// volume's date is earlier than 1970.
    int64_t modified{};
    /// True when the file is stored compressed and cannot be read.
    bool compressed{};
    /// True when the file is a symbolic link.
    bool symlink{};
};

/// One HFS+ volume.
class Volume {
  public:

    /// A volume that is not open.
    Volume();

    /// Releases the catalogue.
    ~Volume();

    /// A volume owns its catalogue, so it is not copied.
    Volume(const Volume&) = delete;

    /// A volume owns its catalogue, so it is not copied.
    Volume& operator=(const Volume&) = delete;

    /// Moves the catalogue.
    ///
    /// @param other the volume to move from
    Volume(Volume&& other) noexcept;

    /// Moves the catalogue.
    ///
    /// @param other the volume to move from
    /// @return this volume
    Volume& operator=(Volume&& other) noexcept;

    /// Reads a volume that starts at the hook's offset 0.
    ///
    /// The volume header is the `H+` or `HX` block at byte 1024. A second
    /// call on an open volume does nothing and succeeds.
    ///
    /// @param hooks how the volume's bytes are read; the hooks outlive the volume
    /// @param volume_bytes the volume's size, in bytes
    /// @param[out] error why it could not be opened; may be null
    /// @return true when the catalogue can be used
    [[nodiscard]] bool open(ReadHooks hooks, uint64_t volume_bytes, std::string* error);

    /// Finds an HFS+ volume on a whole disk and reads it.
    ///
    /// A bare volume, a GUID partition table and an Apple partition map are
    /// recognised. An APFS partition, and a disk with no HFS+ volume, are
    /// refused.
    ///
    /// @param hooks how the disk's bytes are read; the hooks outlive the volume
    /// @param disk_bytes the disk's size, in bytes
    /// @param[out] error why it could not be opened; may be null
    /// @return true when the catalogue can be used
    [[nodiscard]] bool open_disk(ReadHooks hooks, uint64_t disk_bytes, std::string* error);

    /// Returns every catalogue record, in the order the leaves store them.
    ///
    /// @return the records, empty when the volume is not open
    [[nodiscard]] const std::vector<CatalogItem>& catalog() const;

    /// Copies one file's data fork into `out`.
    ///
    /// A compressed file is refused and `out` is left untouched. A range
    /// past the end of the fork fails. A hole in the fork reads as zeroes.
    ///
    /// @param id the file's catalogue id
    /// @param offset the first byte of the data fork
    /// @param out the buffer to fill
    /// @param[out] error why the bytes could not be read; may be null
    /// @return true when `out` holds the fork's bytes
    [[nodiscard]] bool
    read_file(uint32_t id, uint64_t offset, std::span<uint8_t> out, std::string* error) const;

  private:

    struct State;
    std::unique_ptr<State> state_;
};

} // namespace oa::formats::hfsplus
