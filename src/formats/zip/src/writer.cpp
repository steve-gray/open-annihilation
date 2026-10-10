// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The streaming zip writer: local headers completed in place, stored or
// deflated entries, and the 64-bit extension where a size, offset or count
// needs it. The bytes depend on the entries, the options and zlib's
// deflate, not on the clock or the platform.

#include "oa/formats/zip/writer.hpp"

#include "records.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <utility>

#include <zlib.h>

namespace oa::formats::zip {

using namespace detail;
using base::bytes::store_le16;
using base::bytes::store_le32;

namespace {

/// Version 2.0, which stored and deflated entries need.
constexpr uint16_t version_stored = 20;
/// Version 4.5, which the 64-bit extension needs.
constexpr uint16_t version_zip64 = 45;
/// The time field of every entry: 00:00:00.
constexpr uint16_t fixed_time = 0;
/// The date field of every entry: 1980-01-01.
constexpr uint16_t fixed_date = 1U | (1U << 5U);
/// The highest byte value of 7-bit ASCII.
constexpr uint8_t last_ascii_byte = 127;
/// zlib's raw deflate: a negative window size omits the zlib wrapper.
constexpr int deflate_window_bits = -15;
/// zlib's default memory level for deflate.
constexpr int deflate_memory_level = 8;
/// The most compressed bytes held, and written, at one time.
constexpr size_t output_piece_bytes = size_t{64} << 10;
/// A declared size from here up reserves the 64-bit sizes: a deflated stream
/// can grow a little past its input, and must still fit the header written
/// before any byte of it.
constexpr uint64_t zip64_size_at = 0xF000'0000ULL;
/// An offset or a 32-bit size from here up is the sentinel, so the value
/// lives in the 64-bit extension.
constexpr uint64_t zip64_offset_at = 0xFFFF'FFFFULL;
/// A count from here up fills the end record's 16-bit field, which is then
/// the sentinel.
constexpr uint64_t zip64_count_at = 0xFFFFULL;
/// The 64-bit end record's signature and size field. The size counts what
/// follows them.
constexpr uint64_t zip64_end_leading_bytes = 12;
/// The local header's 64-bit extra field: its header and both sizes.
constexpr size_t local_zip64_extra_bytes = extra_header_bytes + 16;
/// The central record's 64-bit extra field: its header, both sizes and the
/// local header's offset.
constexpr size_t central_zip64_extra_bytes = extra_header_bytes + 24;
/// Bytes handed to one zlib crc32 call, whose length is an unsigned int.
constexpr size_t crc_piece_bytes = size_t{1} << 30;

static_assert(output_piece_bytes <= UINT_MAX);
static_assert(crc_piece_bytes <= UINT_MAX);

/// Appends a little-endian 16-bit field.
///
/// @param[in,out] out the bytes to append to
/// @param value the value
void append_u16(std::vector<uint8_t>& out, uint16_t value) {
    const size_t at = out.size();
    out.resize(at + 2);
    store_le16(out.data() + at, value);
}

/// Appends a little-endian 32-bit field.
///
/// @param[in,out] out the bytes to append to
/// @param value the value
void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    const size_t at = out.size();
    out.resize(at + 4);
    store_le32(out.data() + at, value);
}

/// Appends a little-endian 64-bit field.
///
/// @param[in,out] out the bytes to append to
/// @param value the value
void append_u64(std::vector<uint8_t>& out, uint64_t value) {
    append_u32(out, static_cast<uint32_t>(value));
    append_u32(out, static_cast<uint32_t>(value >> 32U));
}

/// Writes a little-endian 64-bit value into eight bytes.
///
/// @param[out] bytes the first of eight bytes
/// @param value the value
void store_u64(uint8_t* bytes, uint64_t value) noexcept {
    store_le32(bytes, static_cast<uint32_t>(value));
    store_le32(bytes + 4, static_cast<uint32_t>(value >> 32U));
}

/// Tells whether bytes are well-formed UTF-8: no overlong form, no
/// surrogate and nothing above U+10FFFF. The streamed reader uses the same
/// rule.
///
/// @param text the bytes
/// @return true when they are
bool valid_utf8(std::string_view text) noexcept {
    constexpr uint32_t highest_code_point = 0x10FFFF;
    constexpr uint32_t first_surrogate = 0xD800;
    constexpr uint32_t last_surrogate = 0xDFFF;
    size_t at = 0;
    while (at < text.size()) {
        const auto lead = static_cast<unsigned char>(text[at]);
        size_t length = 0;
        uint32_t code = 0;
        uint32_t least = 0;
        if (lead < 0x80U) {
            ++at;
            continue;
        }
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
            code = lead & 0x1FU;
            least = 0x80;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
            code = lead & 0x0FU;
            least = 0x800;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
            code = lead & 0x07U;
            least = 0x10000;
        } else {
            return false;
        }
        if (text.size() - at < length)
            return false;
        for (size_t index = 1; index < length; ++index) {
            const auto next = static_cast<unsigned char>(text[at + index]);
            if ((next & 0xC0U) != 0x80U)
                return false;
            code = (code << 6U) | (next & 0x3FU);
        }
        if (code < least || code > highest_code_point ||
            (code >= first_surrogate && code <= last_surrogate))
            return false;
        at += length;
    }
    return true;
}

/// Returns a name with its ASCII capitals lowered, for the uniqueness check.
///
/// @param name the entry's name
/// @return the folded name
std::string fold_ascii(std::string_view name) {
    std::string folded{name};
    for (char& character : folded) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }
    return folded;
}

/// Returns the general purpose flags of an entry.
///
/// @param name the entry's name
/// @return the UTF-8 flag when the name leaves 7-bit ASCII, otherwise none
uint16_t flags_for(std::string_view name) noexcept {
    const bool ascii = std::all_of(name.begin(), name.end(), [](char character) {
        return static_cast<uint8_t>(character) <= last_ascii_byte;
    });
    return ascii ? uint16_t{0} : flag_utf8_name;
}

/// Folds the running CRC-32 over the next bytes.
///
/// @param[in,out] crc the CRC-32 so far, as zlib keeps it
/// @param bytes the next bytes
void crc_update(uint32_t& crc, std::span<const uint8_t> bytes) noexcept {
    uLong value = crc;
    while (!bytes.empty()) {
        const size_t piece = std::min(bytes.size(), crc_piece_bytes);
        value = crc32(value, bytes.data(), static_cast<uInt>(piece));
        bytes = bytes.subspan(piece);
    }
    crc = static_cast<uint32_t>(value);
}

} // namespace

struct StreamWriter::DeflateStream {
    z_stream stream{};
    bool active{false};
};

StreamWriter::StreamWriter(OutputHooks output, WriterOptions options)
    : output_(output), options_(options) {
}

StreamWriter::~StreamWriter() {
    close_deflate();
}

uint64_t StreamWriter::bytes_written() const noexcept {
    return written_;
}

bool StreamWriter::stuck(ZipError& error) const {
    if (stuck_status_ == ZipStatus::ok)
        return false;
    error = ZipError{stuck_status_, stuck_offset_, stuck_entry_};
    return true;
}

bool StreamWriter::fail_at(
    ZipError& error, ZipStatus status, uint64_t offset, std::string_view entry, bool remember
) {
    error = ZipError{status, offset, std::string{entry}};
    if (remember) {
        stuck_status_ = status;
        stuck_offset_ = offset;
        stuck_entry_ = error.entry;
    }
    return false;
}

bool StreamWriter::emit(std::span<const uint8_t> bytes, ZipError& error) {
    size_t at = 0;
    while (at < bytes.size()) {
        const size_t piece = std::min(bytes.size() - at, output_piece_bytes);
        const auto slice = bytes.subspan(at, piece);
        if (output_.write_at == nullptr || !output_.write_at(output_.context, written_, slice))
            return fail_at(error, ZipStatus::write_failed, written_, open_name_, true);
        written_ += piece;
        at += piece;
    }
    return true;
}

bool StreamWriter::patch(uint64_t offset, std::span<const uint8_t> bytes, ZipError& error) {
    if (bytes.empty())
        return true;
    if (output_.write_at == nullptr || !output_.write_at(output_.context, offset, bytes))
        return fail_at(error, ZipStatus::write_failed, offset, open_name_, true);
    return true;
}

bool StreamWriter::check_name(
    std::string_view name, uint64_t bytes, Method method, std::string& folded, ZipError& error
) {
    if (!name_is_safe(name))
        return fail_at(error, ZipStatus::unsafe_name, written_, name, false);
    if (name.size() > max_name_bytes)
        return fail_at(error, ZipStatus::name_too_long, written_, name, false);
    if (!valid_utf8(name))
        return fail_at(error, ZipStatus::bad_name_encoding, written_, name, false);
    folded = fold_ascii(name);
    if (folded_names_.contains(folded))
        return fail_at(error, ZipStatus::duplicate_name, written_, name, false);
    if (method != Method::stored && method != Method::deflated)
        return fail_at(error, ZipStatus::unsupported_method, written_, name, false);
    if (name.back() == '/' && (bytes != 0 || method != Method::stored))
        return fail_at(error, ZipStatus::size_mismatch, written_, name, false);
    return true;
}

bool StreamWriter::open_deflate(ZipError& error) {
    deflate_ = std::make_unique<DeflateStream>();
    // Level 9, raw deflate, the default memory and strategy: the same zlib
    // then gives the same bytes. Another deflate implementation may not.
    const int begun = deflateInit2(
        &deflate_->stream,
        options_.deflate_level,
        Z_DEFLATED,
        deflate_window_bits,
        deflate_memory_level,
        Z_DEFAULT_STRATEGY
    );
    if (begun != Z_OK) {
        deflate_.reset();
        return fail_at(error, ZipStatus::write_failed, written_, open_name_, true);
    }
    deflate_->active = true;
    return true;
}

bool StreamWriter::deflate_span(
    std::span<const uint8_t> data, bool finish_stream, ZipError& error
) {
    if (deflate_ == nullptr || !deflate_->active)
        return fail_at(error, ZipStatus::write_failed, written_, open_name_, true);
    z_stream& stream = deflate_->stream;
    const int flush = finish_stream ? Z_FINISH : Z_NO_FLUSH;
    stream.next_in =
        data.empty() ? Z_NULL : const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    std::array<uint8_t, output_piece_bytes> piece{};
    int result = Z_OK;
    do {
        stream.next_out = piece.data();
        stream.avail_out = static_cast<uInt>(piece.size());
        result = deflate(&stream, flush);
        if (result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR)
            return fail_at(error, ZipStatus::write_failed, written_, open_name_, true);
        const size_t produced = piece.size() - stream.avail_out;
        if (produced != 0) {
            if (!emit(std::span<const uint8_t>{piece.data(), produced}, error))
                return false;
            compressed_ += produced;
        }
        if (result == Z_STREAM_END)
            return true;
    } while (stream.avail_out == 0);
    if (finish_stream || stream.avail_in != 0)
        return fail_at(error, ZipStatus::write_failed, written_, open_name_, true);
    return true;
}

void StreamWriter::close_deflate() noexcept {
    if (deflate_ == nullptr)
        return;
    if (deflate_->active)
        deflateEnd(&deflate_->stream);
    deflate_.reset();
}

bool StreamWriter::write_local_header(std::string_view name, ZipError& error) {
    std::vector<uint8_t> header{};
    header.reserve(local_bytes + name.size() + local_zip64_extra_bytes);
    header.insert(header.end(), local_signature.begin(), local_signature.end());
    append_u16(header, version_);
    append_u16(header, flags_);
    append_u16(header, static_cast<uint16_t>(method_));
    append_u16(header, fixed_time);
    append_u16(header, fixed_date);
    append_u32(header, 0);
    if (zip64_entry_) {
        append_u32(header, zip64_sentinel_32);
        append_u32(header, zip64_sentinel_32);
    } else {
        append_u32(header, 0);
        append_u32(header, 0);
    }
    append_u16(header, static_cast<uint16_t>(name.size()));
    append_u16(header, zip64_entry_ ? static_cast<uint16_t>(local_zip64_extra_bytes) : 0);
    header.insert(header.end(), name.begin(), name.end());
    if (zip64_entry_) {
        append_u16(header, zip64_extra_id);
        append_u16(header, 16);
        append_u64(header, 0);
        append_u64(header, 0);
    }
    return emit(header, error);
}

bool StreamWriter::finish_local_header(ZipError& error) {
    std::array<uint8_t, 4> crc_bytes{};
    store_le32(crc_bytes.data(), crc_);
    if (!patch(local_offset_ + local_crc32, crc_bytes, error))
        return false;
    if (!zip64_entry_) {
        if (compressed_ >= zip64_offset_at || uncompressed_ >= zip64_offset_at ||
            local_offset_ >= zip64_offset_at)
            return fail_at(error, ZipStatus::too_large, local_offset_, open_name_, true);
        std::array<uint8_t, 8> sizes{};
        store_le32(sizes.data(), static_cast<uint32_t>(compressed_));
        store_le32(sizes.data() + 4, static_cast<uint32_t>(uncompressed_));
        return patch(local_offset_ + local_compressed_bytes, sizes, error);
    }
    std::array<uint8_t, 16> sizes{};
    store_u64(sizes.data(), uncompressed_);
    store_u64(sizes.data() + 8, compressed_);
    const uint64_t extra_data = local_offset_ + local_bytes + name_bytes_ + extra_header_bytes;
    return patch(extra_data, sizes, error);
}

void StreamWriter::keep_central_record() {
    const uint32_t narrow_compressed =
        zip64_entry_ ? zip64_sentinel_32 : static_cast<uint32_t>(compressed_);
    const uint32_t narrow_bytes =
        zip64_entry_ ? zip64_sentinel_32 : static_cast<uint32_t>(uncompressed_);
    const uint32_t narrow_offset =
        zip64_entry_ ? zip64_sentinel_32 : static_cast<uint32_t>(local_offset_);
    central_.insert(central_.end(), central_signature.begin(), central_signature.end());
    append_u16(central_, version_);
    append_u16(central_, version_);
    append_u16(central_, flags_);
    append_u16(central_, static_cast<uint16_t>(method_));
    append_u16(central_, fixed_time);
    append_u16(central_, fixed_date);
    append_u32(central_, crc_);
    append_u32(central_, narrow_compressed);
    append_u32(central_, narrow_bytes);
    append_u16(central_, name_bytes_);
    append_u16(central_, zip64_entry_ ? static_cast<uint16_t>(central_zip64_extra_bytes) : 0);
    append_u16(central_, 0);
    append_u16(central_, 0);
    append_u16(central_, 0);
    append_u32(central_, 0);
    append_u32(central_, narrow_offset);
    central_.insert(central_.end(), open_name_.begin(), open_name_.end());
    if (zip64_entry_) {
        append_u16(central_, zip64_extra_id);
        append_u16(central_, 24);
        append_u64(central_, uncompressed_);
        append_u64(central_, compressed_);
        append_u64(central_, local_offset_);
    }
}

bool StreamWriter::begin_entry(
    std::string_view name, uint64_t bytes, Method method, ZipError& error
) {
    error = ZipError{};
    if (stuck(error))
        return false;
    if (finished_ || open_)
        return fail_at(error, ZipStatus::entry_open, written_, name, false);
    std::string folded{};
    if (!check_name(name, bytes, method, folded, error))
        return false;
    open_name_.assign(name.begin(), name.end());
    if (method == Method::deflated && !open_deflate(error))
        return false;
    method_ = method;
    declared_ = bytes;
    uncompressed_ = 0;
    compressed_ = 0;
    crc_ = 0;
    flags_ = flags_for(name);
    name_bytes_ = static_cast<uint16_t>(name.size());
    zip64_entry_ =
        options_.zip64 == Zip64::always || bytes >= zip64_size_at || written_ >= zip64_offset_at;
    version_ = zip64_entry_ ? version_zip64 : version_stored;
    local_offset_ = written_;
    if (!write_local_header(name, error)) {
        close_deflate();
        return false;
    }
    folded_names_.insert(std::move(folded));
    open_ = true;
    return true;
}

bool StreamWriter::write(std::span<const uint8_t> data, ZipError& error) {
    error = ZipError{};
    if (stuck(error))
        return false;
    if (!open_)
        return fail_at(error, ZipStatus::entry_open, written_, {}, false);
    if (data.empty())
        return true;
    if (data.size() > UINT64_MAX - uncompressed_)
        return fail_at(error, ZipStatus::too_large, local_offset_, open_name_, true);
    crc_update(crc_, data);
    uncompressed_ += data.size();
    if (method_ == Method::stored) {
        if (!emit(data, error))
            return false;
        compressed_ += data.size();
        return true;
    }
    while (!data.empty()) {
        const size_t piece = std::min(data.size(), output_piece_bytes);
        if (!deflate_span(data.first(piece), false, error))
            return false;
        data = data.subspan(piece);
    }
    return true;
}

bool StreamWriter::end_entry(ZipError& error) {
    error = ZipError{};
    if (stuck(error))
        return false;
    if (!open_)
        return fail_at(error, ZipStatus::entry_open, written_, {}, false);
    if (method_ == Method::deflated && !deflate_span({}, true, error)) {
        close_deflate();
        open_ = false;
        return false;
    }
    close_deflate();
    if (uncompressed_ != declared_) {
        open_ = false;
        return fail_at(error, ZipStatus::wrong_size, local_offset_, open_name_, true);
    }
    if (!finish_local_header(error)) {
        open_ = false;
        return false;
    }
    keep_central_record();
    ++entry_count_;
    open_ = false;
    open_name_.clear();
    return true;
}

bool StreamWriter::add_folder(std::string_view name, ZipError& error) {
    error = ZipError{};
    if (stuck(error))
        return false;
    if (name.empty() || name.back() != '/')
        return fail_at(error, ZipStatus::unsafe_name, written_, name, false);
    if (!begin_entry(name, 0, Method::stored, error))
        return false;
    return end_entry(error);
}

bool StreamWriter::finish(ZipError& error) {
    error = ZipError{};
    if (stuck(error))
        return false;
    if (open_ || finished_)
        return fail_at(error, ZipStatus::entry_open, written_, open_name_, false);
    const uint64_t directory_offset = written_;
    const uint64_t directory_bytes = central_.size();
    if (!emit(central_, error))
        return false;
    const bool zip64_end = options_.zip64 == Zip64::always || entry_count_ >= zip64_count_at ||
                           directory_offset >= zip64_offset_at ||
                           directory_bytes >= zip64_offset_at;
    if (zip64_end) {
        const uint64_t record_offset = written_;
        std::array<uint8_t, zip64_end_bytes> record{};
        std::copy(zip64_end_signature.begin(), zip64_end_signature.end(), record.begin());
        store_u64(record.data() + 4, zip64_end_bytes - zip64_end_leading_bytes);
        store_le16(record.data() + 12, version_zip64);
        store_le16(record.data() + 14, version_zip64);
        store_u64(record.data() + zip64_end_disk_entry_count, entry_count_);
        store_u64(record.data() + zip64_end_entry_count, entry_count_);
        store_u64(record.data() + zip64_end_directory_bytes, directory_bytes);
        store_u64(record.data() + zip64_end_directory_offset, directory_offset);
        if (!emit(record, error))
            return false;
        std::array<uint8_t, zip64_locator_bytes> locator{};
        std::copy(zip64_locator_signature.begin(), zip64_locator_signature.end(), locator.begin());
        store_u64(locator.data() + zip64_locator_end_offset, record_offset);
        store_le32(locator.data() + zip64_locator_disk_count, 1);
        if (!emit(locator, error))
            return false;
    }
    std::array<uint8_t, end_bytes> end{};
    std::copy(end_signature.begin(), end_signature.end(), end.begin());
    const uint16_t narrow_count =
        zip64_end ? zip64_sentinel_16 : static_cast<uint16_t>(entry_count_);
    const uint32_t narrow_bytes =
        zip64_end ? zip64_sentinel_32 : static_cast<uint32_t>(directory_bytes);
    const uint32_t narrow_offset =
        zip64_end ? zip64_sentinel_32 : static_cast<uint32_t>(directory_offset);
    store_le16(end.data() + end_disk_entry_count, narrow_count);
    store_le16(end.data() + end_entry_count, narrow_count);
    store_le32(end.data() + end_directory_bytes, narrow_bytes);
    store_le32(end.data() + end_directory_offset, narrow_offset);
    if (!emit(end, error))
        return false;
    finished_ = true;
    central_.clear();
    return true;
}

} // namespace oa::formats::zip
