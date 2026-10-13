// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// An Apple xar archive: the header, the zlib-compressed table of contents
// and the heap that follows it. A file in the heap is stored as it is, or
// wrapped in gzip. The archive is not opened here; the caller hands the
// bytes and reads a heap file from where the table says it lies.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdint.h>
#include <string>
#include <string_view>
#include <vector>

namespace oa::formats::xar {

/// The largest table of contents, compressed or inflated, in bytes.
inline constexpr uint64_t max_toc_bytes = uint64_t{32} << 20;
/// The most files the table may name.
inline constexpr std::size_t max_entries = 100000;
/// The longest single path component, in bytes.
inline constexpr std::size_t max_name_bytes = 1024;

/// Why a header, a table or a gzip stream could not be read.
enum class Fault : uint8_t {
    ok,
    truncated,        ///< a record or a stream ended early
    bad_magic,        ///< the file does not start with the xar mark
    bad_version,      ///< a header version this reader does not read
    header_size,      ///< the header's size is not one this reader accepts
    toc_too_large,    ///< the table claims more bytes than max_toc_bytes
    inflate_failed,   ///< zlib or gzip data that does not inflate
    checksum,         ///< a zlib or gzip checksum that does not match
    bad_toc,          ///< the table is not the xar document this reader reads
    unsafe_name,      ///< a path component that is empty, absolute or ".."
    bad_number,       ///< a size or offset that is not a plain decimal number
    too_many_entries, ///< more files than max_entries
    name_too_long,    ///< a path component longer than max_name_bytes
    unsupported,      ///< a gzip stream this reader refuses
};

/// How a heap file is stored.
enum class Encoding : uint8_t {
    stored,      ///< the heap bytes are the file
    gzip,        ///< the heap bytes are one gzip member
    unsupported, ///< some other encoding; the bytes are left unread
};

/// One file or directory named by the table.
struct Entry {
    /// Path from the archive root, with `/` between components.
    std::string path{};
    /// True when the table calls it a directory and it holds no data.
    bool directory{};
    /// How the heap bytes are stored. Ignored for a directory.
    Encoding encoding{Encoding::stored};
    /// Where the heap bytes start, counting from the start of the heap.
    uint64_t heap_offset{};
    /// Heap bytes the file occupies.
    uint64_t stored_bytes{};
    /// Uncompressed size, in bytes.
    uint64_t bytes{};
};

/// The header and the table. The heap itself is not copied.
struct Archive {
    /// Header length, in bytes. The table starts at this offset.
    uint16_t header_bytes{};
    /// The checksum the header names: 0 none, 1 SHA-1, 2 MD5. Not checked.
    uint32_t checksum_kind{};
    /// Compressed table length, in bytes.
    uint64_t toc_compressed_bytes{};
    /// Inflated table length, in bytes.
    uint64_t toc_bytes{};
    /// Where the heap starts, counting from the start of the file.
    uint64_t heap_start{};
    /// Files and directories, parents before their children.
    std::vector<Entry> entries{};
};

/// What one feed of a gzip member did.
struct GzipFeed {
    std::size_t consumed{}; ///< input bytes taken
    std::size_t produced{}; ///< uncompressed bytes written
    bool finished{};        ///< the member ended, checksum included
    bool failed{};          ///< the member is not gzip this reader accepts
};

/// Inflates one gzip member a piece at a time.
///
/// The input is the raw gzip bytes, header and trailer included. A feed
/// that is given no output room during the deflate leaves the input for
/// the next feed. `error()` is a static sentence after a failed feed.
class GzipReader {
  public:

    /// Prepares a reader for one member.
    GzipReader();

    /// Frees the deflate state.
    ~GzipReader();

    GzipReader(const GzipReader&) = delete;
    GzipReader& operator=(const GzipReader&) = delete;

    /// Inflates the next bytes of the member into `output`.
    ///
    /// @param input the next gzip bytes, which may be empty at the end
    /// @param output room for uncompressed bytes; empty only before deflate
    /// @return the bytes used, and whether the member finished or failed
    [[nodiscard]] GzipFeed feed(std::span<const uint8_t> input, std::span<uint8_t> output);

    /// Returns why the last feed failed.
    ///
    /// @return a static sentence, or an empty string when the last feed did not fail
    [[nodiscard]] const char* error() const noexcept;

  private:

    struct State;
    friend struct GzipDetail;
    std::unique_ptr<State> state_{};
};

/// Returns a short English description of a fault.
///
/// @param fault the fault
/// @return a static string; empty when `fault` is ok
[[nodiscard]] const char* fault_text(Fault fault) noexcept;

/// Reads the fixed header from the front of an archive.
///
/// `bytes` has to hold at least the 28-byte fixed header. A longer header
/// is accepted up to 4096 bytes and does not have to be present in `bytes`:
/// the table starts at `header_bytes`, and the heap at `heap_start`.
///
/// @param bytes the front of the archive
/// @param[out] archive the header fields; the file list is cleared
/// @param[out] fault why it could not be read; ok on success
/// @return true when the header was read
[[nodiscard]] bool
read_header(std::span<const uint8_t> bytes, Archive& archive, Fault& fault) noexcept;

/// Inflates a table of contents and reads its files.
///
/// @param compressed the zlib-wrapped table
/// @param uncompressed_bytes the inflated size the header names
/// @param[out] entries the files, replaced
/// @param[out] fault why it could not be read; ok on success
/// @return true when `entries` is the table
[[nodiscard]] bool read_toc(
    std::span<const uint8_t> compressed,
    uint64_t uncompressed_bytes,
    std::vector<Entry>& entries,
    Fault& fault
);

/// Reads the header and the table from an archive whose table is in `file`.
///
/// The heap may follow the table in `file` and is not inspected. `file` has
/// to reach the first heap byte, so the table is whole.
///
/// @param file the archive, at least through the end of the table
/// @param[out] archive the header and the files
/// @param[out] fault why it could not be read; ok on success
/// @return true when `archive` is the header and the table
[[nodiscard]] bool read_archive(std::span<const uint8_t> file, Archive& archive, Fault& fault);

/// Finds an entry by its exact path.
///
/// @param entries the table
/// @param path the path, with `/` between components
/// @return the entry, or null when no entry has that path
[[nodiscard]] const Entry*
find_entry(const std::vector<Entry>& entries, std::string_view path) noexcept;

/// Inflates one whole gzip member.
///
/// @param compressed the member, header and trailer included
/// @param[out] plain the uncompressed bytes, replaced
/// @param[out] fault why it could not be inflated; ok on success
/// @return true when `plain` is the member
[[nodiscard]] bool
inflate_gzip(std::span<const uint8_t> compressed, std::vector<uint8_t>& plain, Fault& fault);

} // namespace oa::formats::xar
