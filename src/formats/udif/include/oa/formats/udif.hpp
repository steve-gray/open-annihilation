// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// UDIF disk images: the `koly` trailer, the resource-plist block tables
// (`mish`) and the chunks they name. Zlib chunks and uncompressed chunks
// are read. Other chunk kinds are refused, with a message that names the
// kind. The image is read through a hook, so this module does not open
// files.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace oa::formats::udif {

/// Bytes read from an image. `read` copies `count` bytes from `offset`.
struct ReadHooks {
    /// Passed to `read`.
    void* context{};
    /// Copies `count` bytes from `offset` into `out`.
    ///
    /// @param context the value stored beside this function
    /// @param offset the first byte, from the start of the image file
    /// @param out the buffer, with room for `count` bytes
    /// @param count how many bytes to copy
    /// @return true when `out` holds those bytes
    bool (*read)(void* context, uint64_t offset, uint8_t* out, size_t count){};
};

/// One UDIF image, addressed as a raw disk of 512-byte sectors.
class Image {
  public:

    /// An image that is not open.
    Image();

    /// Releases the block map.
    ~Image();

    /// An image owns its block map, so it is not copied.
    Image(const Image&) = delete;

    /// An image owns its block map, so it is not copied.
    Image& operator=(const Image&) = delete;

    /// Moves the block map.
    ///
    /// @param other the image to move from
    Image(Image&& other) noexcept;

    /// Moves the block map.
    ///
    /// @param other the image to move from
    /// @return this image
    Image& operator=(Image&& other) noexcept;

    /// Reads the trailer and the block map.
    ///
    /// A second call on an open image does nothing and succeeds. A call that
    /// fails leaves the image unopened. Kinds other than zlib, raw, zeroes
    /// and the ignored filler are refused here, before any chunk is inflated.
    ///
    /// @param hooks how the file's bytes are read; the hooks outlive the image
    /// @param file_bytes the file's size, in bytes
    /// @param[out] error why it could not be opened; may be null
    /// @return true when `read` can be used
    [[nodiscard]] bool open(ReadHooks hooks, uint64_t file_bytes, std::string* error);

    /// Returns the disk the image expands to.
    ///
    /// @return the size in bytes, or 0 when the image is not open
    [[nodiscard]] uint64_t disk_bytes() const;

    /// Copies disk bytes into `out`.
    ///
    /// A range past the end of the disk fails. Bytes no chunk covers are
    /// zeroes. Zlib chunks are inflated the first time they are touched.
    ///
    /// @param offset the first disk byte
    /// @param out the buffer to fill
    /// @param[out] error why the bytes could not be read; may be null
    /// @return true when `out` holds the disk bytes
    [[nodiscard]] bool read(uint64_t offset, std::span<uint8_t> out, std::string* error);

  private:

    struct State;
    std::unique_ptr<State> state_;
};

} // namespace oa::formats::udif
