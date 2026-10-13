// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A cpio archive in the portable ASCII layouts: the old character format
// (odc, magic 070707) and the new ASCII format (newc, magic 070701, and
// the checksum form 070702). The archive is read a piece at a time. The
// caller keeps the bytes; nothing here opens a file or holds a file's data.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdint.h>
#include <string>
#include <string_view>

namespace oa::formats::cpio {

/// The longest file name, including the trailing NUL the header counts.
inline constexpr std::size_t max_name_bytes = 8192;

/// One file named by a header.
struct File {
    /// The name as recorded, without the trailing NUL.
    std::string name{};
    /// Size of the file's data, in bytes.
    uint64_t bytes{};
    /// Modification time, seconds since 1970.
    int64_t modified{};
    /// The mode bits the header stores.
    uint32_t mode{};
    /// Device the header records. odc stores one number; newc stores the
    /// major in the high half and the minor in the low half.
    uint64_t device{};
    /// Inode the header records. Two names with one device and inode are one file.
    uint64_t inode{};
    /// True for the archive's trailer entry.
    bool trailer{};
    /// True when the mode names a directory.
    bool directory{};
    /// True when the mode names a regular file.
    bool regular{};
    /// True when the mode names a symbolic link.
    bool symbolic_link{};
};

/// Called as headers and file bytes arrive. A null function is skipped.
/// A function that returns false stops the read; the reader then reports
/// that it was halted, which is not a broken archive.
struct Visit {
    /// Passed back to each function.
    void* context{};
    /// A header was read. Set `take` to receive the file's bytes. `file` is
    /// valid only for this call.
    bool (*begin)(void* context, const File& file, bool& take){};
    /// The next bytes of the file begun with `take` true, in order.
    bool (*data)(void* context, std::span<const uint8_t> bytes){};
    /// The file's bytes ended, including a file whose bytes were not taken.
    bool (*end)(void* context){};
};

/// Reads one odc or newc archive from successive pieces.
class Reader {
  public:

    Reader() = default;

    /// Takes the next bytes of the archive.
    ///
    /// A piece may end in the middle of a header or a file. Feeding an empty
    /// piece does nothing. After the trailer, only NUL bytes are accepted.
    ///
    /// @param input the next bytes, in order
    /// @param visit who is told about each file
    /// @param[out] error why the archive is broken; may be null; unset when
    ///        the visitor stops the read
    /// @return false when the archive is broken or the visitor stops the read
    [[nodiscard]] bool feed(std::span<const uint8_t> input, const Visit& visit, std::string* error);

    /// Reports whether the visitor stopped the read.
    ///
    /// @return true after a visit function returned false
    [[nodiscard]] bool halted() const noexcept { return halted_; }

    /// Reports whether the trailer has been read.
    ///
    /// @return true after the trailer entry
    [[nodiscard]] bool done() const noexcept { return phase_ == Phase::done; }

    /// Checks that the archive ended on its trailer.
    ///
    /// @param[out] error why it did not; may be null
    /// @return true when the trailer was read and the visitor did not stop
    [[nodiscard]] bool finish(std::string* error) const;

  private:

    enum class Phase : uint8_t { header, name, name_pad, data, data_pad, done };

    enum class HeaderFormat : uint8_t { unknown, odc, newc };

    /// Copies a failure into `error` and remembers it.
    ///
    /// @param error where the sentence goes; may be null
    /// @param text the sentence
    /// @return false
    bool fail(std::string* error, const char* text);

    /// Reads a finished header and starts its name.
    ///
    /// @param visit who is told, once the name is known
    /// @param error where a failure goes
    /// @return false when the header is not odc or newc
    [[nodiscard]] bool open_header(const Visit& visit, std::string* error);

    /// Tells `visit` about the file whose name just finished.
    ///
    /// @param visit who is told
    /// @param error where a failure goes
    /// @return false when the name is broken or the visitor stops
    [[nodiscard]] bool open_file(const Visit& visit, std::string* error);

    /// Checks a finished file and starts its padding, or the next header.
    ///
    /// @param visit who is told the file ended
    /// @param error where a failure goes
    /// @return false when the checksum is wrong or the visitor stops
    [[nodiscard]] bool finish_file(const Visit& visit, std::string* error);

    /// Starts the next header, or ends the archive on its trailer.
    ///
    /// @return true
    [[nodiscard]] bool next_header();

    uint8_t header_[110]{};
    std::size_t header_got_{};
    std::size_t header_need_{6};
    std::string name_{};
    std::size_t name_left_{};
    uint64_t data_left_{};
    uint64_t accepted_{};
    uint32_t checksum_expected_{};
    uint32_t checksum_got_{};
    bool check_checksum_{};
    bool taking_{};
    bool in_file_{};
    bool halted_{};
    HeaderFormat format_{HeaderFormat::unknown};
    Phase phase_{Phase::header};
    File file_{};
    bool failed_{};
};

/// Reports whether a recorded name is safe to use as a relative path.
///
/// A safe name is not empty, does not start with `/`, holds no NUL and no
/// backslash, and has no empty or `..` component. A `.` component is kept,
/// so a name that starts with `./` is safe.
///
/// @param name the name as the header records it, without the trailing NUL
/// @return true when every component can be used
[[nodiscard]] bool name_is_safe(std::string_view name) noexcept;

} // namespace oa::formats::cpio
