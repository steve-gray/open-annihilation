// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Reads one Windows installer: the setup loader's offset table, the setup
// header, and each installed file's pieces. A piece is checked, then undone,
// and only then handed to the caller. The installer is not run.

#include "oa/formats/inno_setup/installer.hpp"

#include "oa/formats/lzma.hpp"
#include "oa/formats/pe.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <zlib.h>

namespace oa::formats::inno_setup {
namespace {

constexpr uint64_t k_max_string = uint64_t{32} << 20;
constexpr uint64_t k_max_block = uint64_t{64} << 20;
constexpr uint64_t k_max_slice = uint64_t{256} << 20;
constexpr uint64_t k_max_file = uint64_t{4} << 30;
constexpr uint32_t k_max_count = 1000000;
constexpr uint32_t k_max_parts = 10000;
constexpr uint64_t k_prefix_bytes = uint64_t{2} << 20;
constexpr uint32_t k_crc_chunk = 4096;
constexpr size_t k_ident_bytes = 64;
constexpr size_t k_table_bytes = 44;
constexpr uint32_t k_no_location = 0xffffffffu;
constexpr uint16_t k_encrypted = uint16_t{1} << 6;
constexpr uint16_t k_compressed = uint16_t{1} << 7;
constexpr uint16_t k_transformed = uint16_t{1} << 4;
constexpr uint8_t k_lzma2 = 4;
constexpr uint64_t k_filetime_epoch = 0x019DB1DED53E8000ull;
constexpr char k_version_text[] = "Inno Setup Setup Data (5.5.7) (u)";

constexpr uint8_t k_loader_magic[][12] = {
    {'r', 'D', 'l', 'P', 't', 'S', 0xcd, 0xe6, 0xd7, 0x7b, 0x0b, '*'},
    {'n', 'S', '5', 'W', '7', 'd', 'T', 0x83, 0xaa, 0x1b, 0x0f, 'j'},
};
constexpr uint8_t k_chunk_magic[4] = {'z', 'l', 'b', 0x1a};

void set_error(std::string* error, std::string_view text) {
    if (error != nullptr)
        *error = std::string(text);
}

[[nodiscard]] bool fail(std::string* error, std::string_view text) {
    set_error(error, text);
    return false;
}

[[nodiscard]] bool fits(uint64_t total, uint64_t offset, uint64_t size) noexcept {
    return offset <= total && size <= total - offset;
}

[[nodiscard]] uint32_t load_u32(const uint8_t* bytes) noexcept {
    return uint32_t{bytes[0]} | (uint32_t{bytes[1]} << 8) | (uint32_t{bytes[2]} << 16) |
           (uint32_t{bytes[3]} << 24);
}

[[nodiscard]] uint32_t crc32_of(std::span<const uint8_t> bytes) noexcept {
    uLong crc = crc32(0L, Z_NULL, 0);
    if (!bytes.empty())
        crc = crc32(crc, bytes.data(), static_cast<uInt>(bytes.size()));
    return static_cast<uint32_t>(crc);
}

/// SHA-1 of the bytes a piece claims to hold, before the installer's wrapping
/// is undone. FIPS 180-1, so a piece can be checked against the installer.
class Sha1 {
  public:

    void update(std::span<const uint8_t> bytes) noexcept {
        message_bytes_ += bytes.size();
        absorb(bytes);
    }

    [[nodiscard]] std::array<uint8_t, 20> finish() noexcept {
        const uint64_t bits = message_bytes_ * 8;
        const uint8_t mark = 0x80;
        absorb(std::span<const uint8_t>(&mark, 1));
        const uint8_t zero = 0;
        while (filled_ != 56)
            absorb(std::span<const uint8_t>(&zero, 1));
        uint8_t length[8];
        for (int shift = 0; shift < 8; ++shift)
            length[shift] = static_cast<uint8_t>(bits >> (56 - 8 * shift));
        absorb(length);
        std::array<uint8_t, 20> digest{};
        const uint32_t words[5] = {h0_, h1_, h2_, h3_, h4_};
        for (size_t word = 0; word < 5; ++word) {
            digest[word * 4] = static_cast<uint8_t>(words[word] >> 24);
            digest[word * 4 + 1] = static_cast<uint8_t>(words[word] >> 16);
            digest[word * 4 + 2] = static_cast<uint8_t>(words[word] >> 8);
            digest[word * 4 + 3] = static_cast<uint8_t>(words[word]);
        }
        return digest;
    }

  private:

    static uint32_t rotl(uint32_t value, int bits) noexcept {
        return (value << bits) | (value >> (32 - bits));
    }

    void process(const uint8_t* block) noexcept {
        uint32_t words[80];
        for (int index = 0; index < 16; ++index) {
            words[index] = (uint32_t{block[index * 4]} << 24) |
                           (uint32_t{block[index * 4 + 1]} << 16) |
                           (uint32_t{block[index * 4 + 2]} << 8) | uint32_t{block[index * 4 + 3]};
        }
        for (int index = 16; index < 80; ++index) {
            const uint32_t mixed =
                words[index - 3] ^ words[index - 8] ^ words[index - 14] ^ words[index - 16];
            words[index] = rotl(mixed, 1);
        }
        uint32_t a = h0_;
        uint32_t b = h1_;
        uint32_t c = h2_;
        uint32_t d = h3_;
        uint32_t e = h4_;
        for (int index = 0; index < 80; ++index) {
            uint32_t f = 0;
            uint32_t k = 0;
            if (index < 20) {
                f = (b & c) | (~b & d);
                k = 0x5a827999u;
            } else if (index < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1u;
            } else if (index < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdcu;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6u;
            }
            const uint32_t next = rotl(a, 5) + f + e + k + words[index];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = next;
        }
        h0_ += a;
        h1_ += b;
        h2_ += c;
        h3_ += d;
        h4_ += e;
    }

    void absorb(std::span<const uint8_t> bytes) noexcept {
        size_t at = 0;
        if (filled_ > 0) {
            const size_t room = 64 - filled_;
            const size_t take = std::min(bytes.size(), room);
            std::copy(
                bytes.begin(),
                bytes.begin() + static_cast<std::ptrdiff_t>(take),
                block_.begin() + static_cast<std::ptrdiff_t>(filled_)
            );
            filled_ += take;
            at += take;
            if (filled_ == 64) {
                process(block_.data());
                filled_ = 0;
            }
        }
        while (at + 64 <= bytes.size()) {
            process(bytes.data() + at);
            at += 64;
        }
        if (at < bytes.size()) {
            const size_t take = bytes.size() - at;
            std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.end(), block_.begin());
            filled_ = take;
        }
    }

    uint32_t h0_{0x67452301u};
    uint32_t h1_{0xefcdab89u};
    uint32_t h2_{0x98badcfeu};
    // The third word is the complement of the second.
    uint32_t h3_{static_cast<uint32_t>(~uint32_t{0xefcdab89u})};
    uint32_t h4_{0xc3d2e1f0u};
    std::array<uint8_t, 64> block_{};
    size_t filled_{0};
    uint64_t message_bytes_{0};
};

/// A cursor over a buffer that has already been read in. It never reads past
/// that buffer.
class Cursor {
  public:

    Cursor(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool u8(uint8_t& value) noexcept {
        if (!fits(bytes_.size(), at_, 1))
            return false;
        value = bytes_[static_cast<size_t>(at_)];
        at_ += 1;
        return true;
    }

    [[nodiscard]] bool u16(uint16_t& value) noexcept {
        if (!fits(bytes_.size(), at_, 2))
            return false;
        const uint8_t* data = bytes_.data() + static_cast<size_t>(at_);
        value = static_cast<uint16_t>(data[0] | (uint16_t{data[1]} << 8));
        at_ += 2;
        return true;
    }

    [[nodiscard]] bool u32(uint32_t& value) noexcept {
        if (!fits(bytes_.size(), at_, 4))
            return false;
        value = load_u32(bytes_.data() + static_cast<size_t>(at_));
        at_ += 4;
        return true;
    }

    [[nodiscard]] bool u64(uint64_t& value) noexcept {
        uint32_t low = 0;
        uint32_t high = 0;
        if (!u32(low) || !u32(high))
            return false;
        value = uint64_t{low} | (uint64_t{high} << 32);
        return true;
    }

    [[nodiscard]] bool skip(uint64_t count) noexcept {
        if (!fits(bytes_.size(), at_, count))
            return false;
        at_ += count;
        return true;
    }

    [[nodiscard]] bool take(size_t count, uint8_t* dest) noexcept {
        if (!fits(bytes_.size(), at_, count))
            return false;
        std::copy(
            bytes_.begin() + static_cast<std::ptrdiff_t>(at_),
            bytes_.begin() + static_cast<std::ptrdiff_t>(at_ + count),
            dest
        );
        at_ += count;
        return true;
    }

    [[nodiscard]] bool skip_flags(int bits) noexcept {
        const int bytes = (bits + 7) / 8;
        return skip(static_cast<uint64_t>(bytes));
    }

    [[nodiscard]] bool skip_blob() noexcept {
        uint32_t length = 0;
        if (!u32(length) || uint64_t{length} > k_max_string)
            return false;
        return skip(length);
    }

  private:

    std::span<const uint8_t> bytes_;
    uint64_t at_{0};
};

[[nodiscard]] bool utf16_to_utf8(std::span<const uint8_t> raw, std::string& text) {
    text.clear();
    if (raw.size() % 2 != 0)
        return false;
    for (size_t index = 0; index < raw.size(); index += 2) {
        const uint16_t unit = static_cast<uint16_t>(raw[index] | (uint16_t{raw[index + 1]} << 8));
        uint32_t code = unit;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (index + 4 > raw.size())
                return false;
            const uint16_t low =
                static_cast<uint16_t>(raw[index + 2] | (uint16_t{raw[index + 3]} << 8));
            if (low < 0xdc00 || low > 0xdfff)
                return false;
            code = 0x10000u + ((uint32_t{unit} - 0xd800u) << 10) + (uint32_t{low} - 0xdc00u);
            index += 2;
        } else if (unit >= 0xdc00 && unit <= 0xdfff) {
            return false;
        }
        if (code < 0x80u) {
            text.push_back(static_cast<char>(code));
        } else if (code < 0x800u) {
            text.push_back(static_cast<char>(0xc0u | (code >> 6)));
            text.push_back(static_cast<char>(0x80u | (code & 0x3fu)));
        } else if (code < 0x10000u) {
            text.push_back(static_cast<char>(0xe0u | (code >> 12)));
            text.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3fu)));
            text.push_back(static_cast<char>(0x80u | (code & 0x3fu)));
        } else {
            text.push_back(static_cast<char>(0xf0u | (code >> 18)));
            text.push_back(static_cast<char>(0x80u | ((code >> 12) & 0x3fu)));
            text.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3fu)));
            text.push_back(static_cast<char>(0x80u | (code & 0x3fu)));
        }
    }
    return true;
}

[[nodiscard]] bool read_text(Cursor& cursor, std::string& text) {
    uint32_t length = 0;
    if (!cursor.u32(length) || uint64_t{length} > k_max_string || length % 2 != 0)
        return false;
    std::vector<uint8_t> raw(length);
    if (length > 0 && !cursor.take(length, raw.data()))
        return false;
    return utf16_to_utf8(raw, text);
}

[[nodiscard]] bool read_at(std::ifstream& input, uint64_t offset, std::span<uint8_t> dest) {
    if (dest.empty())
        return true;
    if (offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
        return false;
    if (dest.size() > static_cast<size_t>(std::numeric_limits<std::streamsize>::max()))
        return false;
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset));
    if (!input)
        return false;
    input.read(reinterpret_cast<char*>(dest.data()), static_cast<std::streamsize>(dest.size()));
    return input.gcount() == static_cast<std::streamsize>(dest.size());
}

[[nodiscard]] bool lzma_decode(
    oa::formats::lzma::Kind kind,
    std::span<const uint8_t> encoded,
    std::vector<uint8_t>& plain,
    uint64_t cap
) {
    using oa::formats::lzma::Decoder;
    using oa::formats::lzma::Feed;
    using oa::formats::lzma::FeedStatus;
    Decoder decoder(kind);
    plain.clear();
    size_t at = 0;
    std::vector<uint8_t> room(64 * 1024);
    bool ended = false;
    for (int step = 0; step < 10000000; ++step) {
        if (ended)
            return at == encoded.size();
        const size_t remaining = encoded.size() - at;
        const size_t offer = std::min(remaining, size_t{64} * 1024);
        const Feed fed = decoder.feed(
            std::span<const uint8_t>(encoded.data() + at, offer),
            std::span<uint8_t>(room.data(), room.size())
        );
        at += fed.consumed;
        if (fed.produced > cap || plain.size() > cap - fed.produced)
            return false;
        plain.insert(
            plain.end(), room.begin(), room.begin() + static_cast<std::ptrdiff_t>(fed.produced)
        );
        if (fed.status == FeedStatus::finished) {
            if (decoder.finish() != FeedStatus::finished)
                return false;
            ended = true;
            continue;
        }
        if (fed.status == FeedStatus::error)
            return false;
        if (fed.consumed == 0 && fed.produced == 0)
            return false;
        if (at == encoded.size() && fed.status == FeedStatus::need_input) {
            if (decoder.finish() != FeedStatus::finished)
                return false;
            ended = true;
        }
    }
    return false;
}

[[nodiscard]] bool
lzma1_decode(std::span<const uint8_t> payload, std::vector<uint8_t>& plain, uint64_t cap) {
    if (payload.size() < 5)
        return false;
    std::vector<uint8_t> wrapped;
    wrapped.reserve(payload.size() + 8);
    wrapped.insert(wrapped.end(), payload.begin(), payload.begin() + 5);
    wrapped.insert(wrapped.end(), 8, 0xff);
    wrapped.insert(wrapped.end(), payload.begin() + 5, payload.end());
    return lzma_decode(oa::formats::lzma::Kind::lzma, wrapped, plain, cap);
}

/// The setup header and the data-entry block: a checksum of the next five
/// bytes, then checksummed chunks of the payload, then LZMA when the block
/// says it is compressed.
[[nodiscard]] bool read_block(
    std::ifstream& input,
    uint64_t file_size,
    uint64_t where,
    std::vector<uint8_t>& plain,
    uint64_t& next,
    std::string* error
) {
    if (!fits(file_size, where, 9))
        return fail(error, "the setup header could not be read");
    uint8_t head[9];
    if (!read_at(input, where, head))
        return fail(error, "the file could not be read");
    const uint32_t expect = load_u32(head);
    const uint32_t stored = load_u32(head + 4);
    const uint8_t compressed = head[8];
    if (crc32_of(std::span<const uint8_t>(head + 4, 5)) != expect)
        return fail(error, "the setup header could not be read");
    if (uint64_t{stored} > k_max_block || !fits(file_size, where + 9, stored))
        return fail(error, "the setup header could not be read");
    std::vector<uint8_t> blob(stored);
    if (stored > 0 && !read_at(input, where + 9, blob))
        return fail(error, "the file could not be read");
    std::vector<uint8_t> payload;
    size_t at = 0;
    while (at < blob.size()) {
        if (blob.size() - at < 4)
            return fail(error, "the setup header could not be read");
        const uint32_t chunk_crc = load_u32(blob.data() + at);
        at += 4;
        const size_t take = std::min(size_t{k_crc_chunk}, blob.size() - at);
        const std::span<const uint8_t> piece(blob.data() + at, take);
        if (crc32_of(piece) != chunk_crc)
            return fail(error, "the setup header could not be read");
        payload.insert(payload.end(), piece.begin(), piece.end());
        at += take;
    }
    next = where + 9 + stored;
    if (compressed == 0) {
        if (payload.size() > k_max_block)
            return fail(error, "the setup header could not be read");
        plain = std::move(payload);
        return true;
    }
    if (!lzma1_decode(payload, plain, k_max_block))
        return fail(error, "the setup header could not be read");
    return true;
}

struct RawFile {
    std::string before;
    std::string after;
    uint32_t location{k_no_location};
};

struct RawData {
    uint32_t first{};
    uint32_t last{};
    uint32_t start{};
    uint64_t suboffset{};
    uint64_t original{};
    uint64_t compressed{};
    std::array<uint8_t, 20> sha1{};
    uint64_t filetime{};
    uint16_t flags{};
};

struct ScriptCall {
    bool dependency{};
    std::string path;
    uint32_t parts{};
    uint64_t wrapped{};
    uint64_t inflated{};
};

[[nodiscard]] bool is_hex(char byte) noexcept {
    return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
}

[[nodiscard]] bool parse_u64(std::string_view text, uint64_t& value) noexcept {
    if (text.empty())
        return false;
    uint64_t parsed = 0;
    for (char byte : text) {
        if (byte < '0' || byte > '9')
            return false;
        const uint64_t digit = static_cast<uint64_t>(byte - '0');
        if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

/// The installed path is recorded as a script call on the first piece.
[[nodiscard]] bool parse_before(std::string_view text, ScriptCall& call) {
    constexpr std::string_view dependency = "before_install_dependency('";
    constexpr std::string_view install = "before_install('";
    if (text.starts_with(dependency)) {
        call.dependency = true;
        text.remove_prefix(dependency.size());
    } else if (text.starts_with(install)) {
        call.dependency = false;
        text.remove_prefix(install.size());
    } else {
        return false;
    }
    if (text.size() < 32)
        return false;
    for (size_t index = 0; index < 32; ++index) {
        if (!is_hex(text[index]))
            return false;
    }
    text.remove_prefix(32);
    constexpr std::string_view gap = "', '";
    if (!text.starts_with(gap))
        return false;
    text.remove_prefix(gap.size());
    const size_t end = text.find('\'');
    if (end == std::string_view::npos)
        return false;
    call.path.assign(text.data(), end);
    text.remove_prefix(end);
    constexpr std::string_view comma = "', ";
    if (!text.starts_with(comma) || text.size() < comma.size() + 2 || text.back() != ')')
        return false;
    text.remove_prefix(comma.size());
    text.remove_suffix(1);
    uint64_t parts = 0;
    if (!parse_u64(text, parts) || parts == 0 || parts > k_max_parts)
        return false;
    call.parts = static_cast<uint32_t>(parts);
    return true;
}

/// Each piece records the wrapped size and the size once that wrapping is undone.
[[nodiscard]] bool parse_after(std::string_view text, ScriptCall& call) {
    constexpr std::string_view dependency = "after_install_dependency('";
    constexpr std::string_view install = "after_install('";
    if (text.starts_with(dependency))
        text.remove_prefix(dependency.size());
    else if (text.starts_with(install))
        text.remove_prefix(install.size());
    else
        return false;
    if (text.size() < 32)
        return false;
    for (size_t index = 0; index < 32; ++index) {
        if (!is_hex(text[index]))
            return false;
    }
    text.remove_prefix(32);
    constexpr std::string_view comma = "', ";
    if (!text.starts_with(comma))
        return false;
    text.remove_prefix(comma.size());
    const size_t split = text.find(", ");
    if (split == std::string_view::npos || text.empty() || text.back() != ')')
        return false;
    uint64_t wrapped = 0;
    uint64_t inflated = 0;
    if (!parse_u64(text.substr(0, split), wrapped))
        return false;
    text.remove_prefix(split + 2);
    text.remove_suffix(1);
    if (!parse_u64(text, inflated))
        return false;
    call.wrapped = wrapped;
    call.inflated = inflated;
    return true;
}

[[nodiscard]] bool usable_path(std::string_view raw, std::string& path) {
    path.clear();
    if (raw.empty() || raw.size() > 4096)
        return false;
    std::string folded;
    folded.reserve(raw.size());
    for (unsigned char byte : raw) {
        if (byte == '\\')
            folded.push_back('/');
        else if (byte == '\0')
            return false;
        else
            folded.push_back(static_cast<char>(byte));
    }
    if (folded.front() == '/' || folded.back() == '/')
        return false;
    if (folded.size() >= 2 && folded[1] == ':' &&
        ((folded[0] >= 'A' && folded[0] <= 'Z') || (folded[0] >= 'a' && folded[0] <= 'z')))
        return false;
    size_t start = 0;
    while (start <= folded.size()) {
        const size_t slash = folded.find('/', start);
        const std::string_view part = std::string_view(folded).substr(
            start, slash == std::string::npos ? std::string::npos : slash - start
        );
        if (part.empty() || part == "." || part == "..")
            return false;
        if (!path.empty())
            path.push_back('/');
        path.append(part);
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    return !path.empty();
}

[[nodiscard]] int64_t unix_seconds(uint64_t filetime) noexcept {
    if (filetime < k_filetime_epoch)
        return 0;
    const uint64_t seconds = (filetime - k_filetime_epoch) / 10000000ull;
    if (seconds > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return 0;
    return static_cast<int64_t>(seconds);
}

[[nodiscard]] bool same_path(std::string_view first, std::string_view second) noexcept {
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

[[nodiscard]] bool loader_magic(const uint8_t* bytes) noexcept {
    for (const auto& magic : k_loader_magic) {
        if (std::equal(std::begin(magic), std::end(magic), bytes))
            return true;
    }
    return false;
}

struct Offsets {
    uint64_t header{};
    uint64_t data{};
};

[[nodiscard]] bool
read_offsets(std::ifstream& input, uint64_t file_size, Offsets& offsets, std::string* error) {
    const uint64_t prefix_len = std::min(file_size, k_prefix_bytes);
    std::vector<uint8_t> prefix(static_cast<size_t>(prefix_len));
    if (!prefix.empty() && !read_at(input, 0, prefix))
        return fail(error, "the file could not be read");
    oa::formats::pe::NumericResource key;
    key.type = 10;
    key.id = 11111;
    const oa::formats::pe::ResourceLookup found = oa::formats::pe::find_resource(prefix, key);
    if (found.error != oa::formats::pe::Error::none || found.range.size != k_table_bytes ||
        !fits(prefix.size(), found.range.offset, found.range.size))
        return fail(error, "the setup loader was not found");
    const uint8_t* table = prefix.data() + static_cast<size_t>(found.range.offset);
    if (!loader_magic(table))
        return fail(error, "the setup loader was not found");
    const uint32_t revision = load_u32(table + 12);
    const uint32_t total = load_u32(table + 16);
    const uint32_t exe_offset = load_u32(table + 20);
    const uint32_t header = load_u32(table + 32);
    const uint32_t data = load_u32(table + 36);
    const uint32_t stored_crc = load_u32(table + 40);
    if (crc32_of(std::span<const uint8_t>(table, 40)) != stored_crc || revision != 1)
        return fail(error, "the setup loader is damaged");
    if (uint64_t{total} == 0 || uint64_t{total} > file_size || uint64_t{exe_offset} > file_size)
        return fail(error, "the setup loader is damaged");
    if (!fits(file_size, header, k_ident_bytes) || data > file_size)
        return fail(error, "a length is outside the file");
    offsets.header = header;
    offsets.data = data;
    return true;
}

[[nodiscard]] bool version_matches(const uint8_t* ident) noexcept {
    static_assert(sizeof(k_version_text) == 34);
    std::array<uint8_t, k_ident_bytes> expected{};
    for (size_t index = 0; index < sizeof(k_version_text) - 1; ++index)
        expected[index] = static_cast<uint8_t>(k_version_text[index]);
    return std::equal(expected.begin(), expected.end(), ident);
}

[[nodiscard]] bool skip_leading(Cursor& cursor, const uint32_t* counts) {
    const auto times = [&](uint32_t count, bool (*skip)(Cursor&)) {
        for (uint32_t index = 0; index < count; ++index) {
            if (!skip(cursor))
                return false;
        }
        return true;
    };
    if (!times(counts[0], [](Cursor& current) {
            for (int blob = 0; blob < 10; ++blob) {
                if (!current.skip_blob())
                    return false;
            }
            return current.skip(4 + 16 + 1);
        }))
        return false;
    if (!times(counts[1], [](Cursor& current) {
            return current.skip_blob() && current.skip_blob() && current.skip(4);
        }))
        return false;
    if (!times(counts[2], [](Cursor& current) { return current.skip_blob(); }))
        return false;
    if (!times(counts[3], [](Cursor& current) {
            for (int blob = 0; blob < 4; ++blob) {
                if (!current.skip_blob())
                    return false;
            }
            return current.skip(20) && current.skip_flags(1) && current.skip(1 + 8);
        }))
        return false;
    if (!times(counts[4], [](Cursor& current) {
            for (int blob = 0; blob < 5; ++blob) {
                if (!current.skip_blob())
                    return false;
            }
            return current.skip(8 + 4 + 1 + 20) && current.skip_flags(5) && current.skip(8);
        }))
        return false;
    if (!times(counts[5], [](Cursor& current) {
            for (int blob = 0; blob < 6; ++blob) {
                if (!current.skip_blob())
                    return false;
            }
            return current.skip(4 + 1 + 20) && current.skip_flags(5);
        }))
        return false;
    if (!times(counts[6], [](Cursor& current) {
            if (!current.skip_blob())
                return false;
            for (int blob = 0; blob < 6; ++blob) {
                if (!current.skip_blob())
                    return false;
            }
            return current.skip(4 + 20 + 2) && current.skip_flags(5);
        }))
        return false;
    return true;
}

struct Piece {
    uint32_t start{};
    uint64_t suboffset{};
    uint64_t original{};
    uint64_t compressed{};
    uint64_t inflated{};
    std::array<uint8_t, 20> sha1{};
    uint16_t flags{};
};

struct Listed {
    File file;
    std::vector<Piece> pieces;
};

[[nodiscard]] bool parse_header(
    std::span<const uint8_t> primary,
    std::span<const uint8_t> secondary,
    uint8_t& compression,
    std::vector<RawFile>& files,
    std::vector<RawData>& entries,
    std::string* error
) {
    Cursor cursor(primary);
    for (int index = 0; index < 28; ++index) {
        if (!cursor.skip_blob())
            return fail(error, "the setup header could not be read");
    }
    for (int index = 0; index < 3; ++index) {
        if (!cursor.skip_blob())
            return fail(error, "the setup header could not be read");
    }
    if (!cursor.skip_blob())
        return fail(error, "the setup header could not be read");
    uint32_t counts[16];
    for (uint32_t& count : counts) {
        if (!cursor.u32(count) || count > k_max_count)
            return fail(error, "the setup header could not be read");
    }
    if (!cursor.skip(20 + 8) || !cursor.skip(1) || !cursor.skip(20 + 8) || !cursor.skip(8 + 4))
        return fail(error, "the setup header could not be read");
    for (int index = 0; index < 5; ++index) {
        if (!cursor.skip(1))
            return fail(error, "the setup header could not be read");
    }
    if (!cursor.u8(compression))
        return fail(error, "the setup header could not be read");
    if (!cursor.skip(1 + 1 + 1 + 1) || !cursor.skip(8) || !cursor.skip(6))
        return fail(error, "the setup header could not be read");
    if (!skip_leading(cursor, counts))
        return fail(error, "the file list could not be read");
    files.resize(counts[7]);
    for (RawFile& file : files) {
        for (int skipped = 0; skipped < 4; ++skipped) {
            if (!cursor.skip_blob())
                return fail(error, "the file list could not be read");
        }
        for (int condition = 0; condition < 4; ++condition) {
            if (!cursor.skip_blob())
                return fail(error, "the file list could not be read");
        }
        if (!read_text(cursor, file.after) || !read_text(cursor, file.before))
            return fail(error, "the file list could not be read");
        if (!cursor.skip(20) || !cursor.u32(file.location))
            return fail(error, "the file list could not be read");
        if (!cursor.skip(4 + 8 + 2) || !cursor.skip_flags(32) || !cursor.skip(1))
            return fail(error, "the file list could not be read");
    }
    const uint64_t table_bytes = uint64_t{counts[8]} * 74;
    if (secondary.size() != table_bytes)
        return fail(error, "the file list could not be read");
    Cursor data_cursor(secondary);
    entries.resize(counts[8]);
    for (RawData& entry : entries) {
        if (!data_cursor.u32(entry.first) || !data_cursor.u32(entry.last) ||
            !data_cursor.u32(entry.start) || !data_cursor.u64(entry.suboffset) ||
            !data_cursor.u64(entry.original) || !data_cursor.u64(entry.compressed))
            return fail(error, "the file list could not be read");
        if (!data_cursor.take(entry.sha1.size(), entry.sha1.data()) ||
            !data_cursor.u64(entry.filetime) || !data_cursor.skip(8) ||
            !data_cursor.u16(entry.flags))
            return fail(error, "the file list could not be read");
    }
    return true;
}

[[nodiscard]] bool group_files(
    const std::vector<RawFile>& files,
    const std::vector<RawData>& entries,
    std::vector<Listed>& listed,
    std::string* error
) {
    struct OpenGroup {
        bool dependency{};
        std::string path;
        uint32_t expected{};
        int64_t modified{};
        bool timed{};
        std::vector<Piece> pieces;
    };

    std::optional<OpenGroup> group;
    const auto close_group = [&]() {
        if (!group)
            return true;
        if (group->pieces.size() != group->expected)
            return false;
        if (!group->dependency) {
            Listed item;
            if (!usable_path(group->path, item.file.path))
                return false;
            uint64_t total = 0;
            for (const Piece& piece : group->pieces) {
                if (piece.inflated > k_max_file - total)
                    return false;
                total += piece.inflated;
            }
            item.file.size = total;
            item.file.modified = group->modified;
            item.pieces = std::move(group->pieces);
            listed.push_back(std::move(item));
        }
        group.reset();
        return true;
    };
    const auto add_part = [&](uint32_t location, uint64_t wrapped, uint64_t inflated) {
        if (!group || location == k_no_location || location >= entries.size())
            return false;
        const RawData& data = entries[location];
        if (data.first != data.last || data.original != wrapped || inflated > k_max_slice)
            return false;
        if (data.suboffset > k_max_slice || data.original > k_max_slice - data.suboffset)
            return false;
        if (data.compressed > k_max_block)
            return false;
        Piece piece;
        piece.start = data.start;
        piece.suboffset = data.suboffset;
        piece.original = data.original;
        piece.compressed = data.compressed;
        piece.inflated = inflated;
        piece.sha1 = data.sha1;
        piece.flags = data.flags;
        if (!group->timed) {
            group->modified = unix_seconds(data.filetime);
            group->timed = true;
        }
        group->pieces.push_back(piece);
        if (group->pieces.size() == group->expected)
            return close_group();
        return group->pieces.size() < group->expected;
    };
    for (const RawFile& file : files) {
        ScriptCall started;
        ScriptCall sizes;
        const bool is_before = !file.before.empty() && parse_before(file.before, started);
        const bool is_after = !file.after.empty() && parse_after(file.after, sizes);
        if (!file.before.empty() && !is_before)
            return fail(error, "a file's parts do not line up");
        if (!file.after.empty() && !is_after)
            return fail(error, "a file's parts do not line up");
        if (is_before) {
            if (group)
                return fail(error, "a file's parts do not line up");
            if (!is_after)
                return fail(error, "a file's parts do not line up");
            group = OpenGroup{};
            group->dependency = started.dependency;
            group->path = std::move(started.path);
            group->expected = started.parts;
            if (!add_part(file.location, sizes.wrapped, sizes.inflated))
                return fail(error, "a file's parts do not line up");
        } else if (group) {
            if (!is_after || !add_part(file.location, sizes.wrapped, sizes.inflated))
                return fail(error, "a file's parts do not line up");
        }
    }
    if (group)
        return fail(error, "a file's parts do not line up");
    return true;
}

/// Undoes one piece's wrapping into `plain`. Nothing is returned until the
/// installer's checksum matches and the unwrapped size is the one declared.
class Inflater {
  public:

    Inflater() = default;
    Inflater(const Inflater&) = delete;
    Inflater& operator=(const Inflater&) = delete;

    ~Inflater() {
        if (started_)
            inflateEnd(&stream_);
    }

    [[nodiscard]] bool start() noexcept {
        stream_ = z_stream{};
        if (inflateInit(&stream_) != Z_OK)
            return false;
        started_ = true;
        return true;
    }

    [[nodiscard]] bool
    feed(std::span<const uint8_t> input, std::vector<uint8_t>& out, uint64_t cap) {
        size_t at = 0;
        while (at < input.size()) {
            if (ended_)
                return false;
            uint8_t room[32 * 1024];
            const size_t offer = std::min(input.size() - at, size_t{1} << 20);
            stream_.next_in = const_cast<Bytef*>(input.data() + at);
            stream_.avail_in = static_cast<uInt>(offer);
            stream_.next_out = room;
            stream_.avail_out = static_cast<uInt>(sizeof room);
            const int status = inflate(&stream_, Z_NO_FLUSH);
            const size_t consumed = offer - stream_.avail_in;
            const size_t produced = sizeof room - stream_.avail_out;
            at += consumed;
            if (produced > cap || out.size() > cap - produced)
                return false;
            out.insert(out.end(), room, room + produced);
            if (status == Z_STREAM_END) {
                ended_ = true;
                return at == input.size() && stream_.avail_in == 0;
            }
            if (status != Z_OK)
                return false;
            if (consumed == 0 && produced == 0)
                return false;
        }
        return true;
    }

    [[nodiscard]] bool finish(std::vector<uint8_t>& out, uint64_t cap) {
        if (ended_)
            return true;
        for (int step = 0; step < 1000000; ++step) {
            uint8_t room[32 * 1024];
            stream_.next_in = nullptr;
            stream_.avail_in = 0;
            stream_.next_out = room;
            stream_.avail_out = static_cast<uInt>(sizeof room);
            const int status = inflate(&stream_, Z_FINISH);
            const size_t produced = sizeof room - stream_.avail_out;
            if (produced > cap || out.size() > cap - produced)
                return false;
            out.insert(out.end(), room, room + produced);
            if (status == Z_STREAM_END) {
                ended_ = true;
                return true;
            }
            if (produced == 0 || (status != Z_OK && status != Z_BUF_ERROR))
                return false;
        }
        return false;
    }

  private:

    z_stream stream_{};
    bool started_{false};
    bool ended_{false};
};

[[nodiscard]] bool decode_lzma2(
    std::ifstream& input,
    uint64_t at,
    uint64_t compressed,
    uint64_t need,
    std::vector<uint8_t>& original,
    std::string* error
) {
    oa::formats::lzma::Decoder decoder(oa::formats::lzma::Kind::lzma2);
    original.clear();
    uint64_t remaining = compressed;
    bool ended = false;
    std::vector<uint8_t> chunk(64 * 1024);
    std::vector<uint8_t> room(64 * 1024);
    for (int step = 0; step < 10000000 && !ended; ++step) {
        const uint64_t offer64 = std::min(remaining, uint64_t{chunk.size()});
        const size_t offer = static_cast<size_t>(offer64);
        if (offer > 0 && !read_at(input, at, std::span<uint8_t>(chunk.data(), offer)))
            return fail(error, "the file could not be read");
        at += offer;
        remaining -= offer;
        const oa::formats::lzma::Feed fed = decoder.feed(
            std::span<const uint8_t>(chunk.data(), offer),
            std::span<uint8_t>(room.data(), room.size())
        );
        if (fed.produced > need || original.size() > need - fed.produced)
            return fail(error, "the file could not be read");
        original.insert(
            original.end(), room.begin(), room.begin() + static_cast<std::ptrdiff_t>(fed.produced)
        );
        // Bytes this feed did not take are read again with the next one.
        if (fed.consumed > offer)
            return fail(error, "the file could not be read");
        const uint64_t unused = offer - fed.consumed;
        at -= unused;
        remaining += unused;
        if (fed.status == oa::formats::lzma::FeedStatus::finished) {
            if (decoder.finish() != oa::formats::lzma::FeedStatus::finished)
                return fail(error, "the file could not be read");
            ended = true;
        } else if (fed.status == oa::formats::lzma::FeedStatus::error) {
            return fail(error, "the file could not be read");
        } else if (fed.consumed == 0 && fed.produced == 0) {
            return fail(error, "the file could not be read");
        } else if (remaining == 0 && fed.status == oa::formats::lzma::FeedStatus::need_input) {
            if (decoder.finish() != oa::formats::lzma::FeedStatus::finished)
                return fail(error, "the file could not be read");
            ended = true;
        }
    }
    if (!ended || remaining != 0 || original.size() != need)
        return fail(error, "the file could not be read");
    return true;
}

[[nodiscard]] bool undo_piece(
    std::ifstream& input,
    uint64_t file_size,
    uint64_t data_offset,
    uint8_t compression,
    const Piece& piece,
    std::vector<uint8_t>& plain,
    std::string* error
) {
    if ((piece.flags & k_encrypted) != 0)
        return fail(error, "this part is encrypted");
    if ((piece.flags & k_transformed) != 0)
        return fail(error, "this part is stored in a form this reader does not undo");
    const uint64_t need = piece.suboffset + piece.original;
    if (data_offset > std::numeric_limits<uint64_t>::max() - piece.start)
        return fail(error, "a length is outside the file");
    const uint64_t magic_at = data_offset + piece.start;
    if (!fits(file_size, magic_at, 4) || piece.compressed > file_size - (magic_at + 4))
        return fail(error, "a length is outside the file");
    uint8_t magic[4];
    if (!read_at(input, magic_at, magic))
        return fail(error, "the file could not be read");
    if (!std::equal(std::begin(k_chunk_magic), std::end(k_chunk_magic), magic))
        return fail(error, "the file could not be read");
    std::vector<uint8_t> original;
    if ((piece.flags & k_compressed) != 0) {
        if (compression != k_lzma2)
            return fail(error, "this part is stored in a form this reader does not undo");
        if (!decode_lzma2(input, magic_at + 4, piece.compressed, need, original, error))
            return false;
    } else {
        if (piece.compressed != need)
            return fail(error, "the file could not be read");
        original.resize(static_cast<size_t>(need));
        if (need > 0 &&
            !read_at(input, magic_at + 4, std::span<uint8_t>(original.data(), original.size())))
            return fail(error, "the file could not be read");
    }
    const size_t sub = static_cast<size_t>(piece.suboffset);
    const size_t window_size = static_cast<size_t>(piece.original);
    const uint8_t* bytes = window_size == 0 ? nullptr : original.data() + sub;
    const std::span<const uint8_t> wrapped(bytes, window_size);
    Sha1 sha;
    sha.update(wrapped);
    if (sha.finish() != piece.sha1)
        return fail(error, "the file's checksum does not match");
    Inflater inflater;
    if (!inflater.start())
        return fail(error, "the file could not be read");
    plain.clear();
    if (!inflater.feed(wrapped, plain, piece.inflated) || !inflater.finish(plain, piece.inflated) ||
        plain.size() != piece.inflated)
        return fail(error, "the file could not be read");
    return true;
}

enum class Emit { done, stopped, failed };

[[nodiscard]] Emit emit_plain(
    std::span<const uint8_t> plain,
    uint64_t& skip,
    const ReadRequest& request,
    ReadResult& result,
    std::string* error
) {
    size_t at = 0;
    if (skip > 0) {
        const uint64_t drop = std::min(skip, static_cast<uint64_t>(plain.size()));
        at = static_cast<size_t>(drop);
        skip -= drop;
    }
    while (at < plain.size()) {
        if (request.stop != nullptr && request.stop->load()) {
            result.stopped = true;
            return Emit::stopped;
        }
        const size_t take = std::min(plain.size() - at, size_t{64} * 1024);
        const std::span<const uint8_t> block(plain.data() + at, take);
        if (request.write != nullptr && !request.write(request.context, block)) {
            if (error != nullptr && error->empty())
                set_error(error, "the file could not be written");
            return Emit::failed;
        }
        result.produced += take;
        at += take;
    }
    return Emit::done;
}

} // namespace

struct Reader::State {
    std::filesystem::path path;
    uint64_t file_size{};
    uint64_t data_offset{};
    uint8_t compression{};
    std::vector<Listed> files;
};

Reader::Reader() = default;
Reader::~Reader() = default;

bool Reader::open(const std::filesystem::path& path, std::string* error) {
    state_.reset();
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return fail(error, "the file could not be read");
    input.seekg(0, std::ios::end);
    const auto file_end = input.tellg();
    if (file_end < 0)
        return fail(error, "the file could not be read");
    const uint64_t file_size = static_cast<uint64_t>(file_end);
    Offsets offsets;
    if (!read_offsets(input, file_size, offsets, error))
        return false;
    uint8_t ident[k_ident_bytes];
    if (!read_at(input, offsets.header, ident))
        return fail(error, "the file could not be read");
    if (!version_matches(ident))
        return fail(error, "this setup data is not the version this reader opens");
    std::vector<uint8_t> primary;
    uint64_t second_at = 0;
    if (!read_block(input, file_size, offsets.header + k_ident_bytes, primary, second_at, error))
        return false;
    std::vector<uint8_t> secondary;
    uint64_t after_entries = 0;
    if (!read_block(input, file_size, second_at, secondary, after_entries, error))
        return false;
    uint8_t compression = 0;
    std::vector<RawFile> files;
    std::vector<RawData> entries;
    if (!parse_header(primary, secondary, compression, files, entries, error))
        return false;
    std::vector<Listed> listed;
    if (!group_files(files, entries, listed, error))
        return false;
    auto state = std::make_unique<State>();
    state->path = path;
    state->file_size = file_size;
    state->data_offset = offsets.data;
    state->compression = compression;
    state->files = std::move(listed);
    state_ = std::move(state);
    return true;
}

bool Reader::list(std::vector<File>& files, std::string* error) const {
    files.clear();
    if (!state_)
        return fail(error, "the installer is not open");
    files.reserve(state_->files.size());
    for (const Listed& item : state_->files)
        files.push_back(item.file);
    return true;
}

bool Reader::read(const ReadRequest& request, ReadResult& result, std::string* error) {
    result = {};
    if (!state_)
        return fail(error, "the installer is not open");
    const Listed* found = nullptr;
    for (const Listed& item : state_->files) {
        if (same_path(item.file.path, request.name)) {
            found = &item;
            break;
        }
    }
    if (found == nullptr)
        return fail(error, "the file could not be found");
    if (request.skip > found->file.size)
        return fail(error, "the file could not be read");
    if (request.stop != nullptr && request.stop->load() && request.skip == 0) {
        result.stopped = true;
        return true;
    }
    std::ifstream input(state_->path, std::ios::binary);
    if (!input)
        return fail(error, "the file could not be read");
    uint64_t skip = request.skip;
    for (const Piece& piece : found->pieces) {
        std::vector<uint8_t> plain;
        if (!undo_piece(
                input,
                state_->file_size,
                state_->data_offset,
                state_->compression,
                piece,
                plain,
                error
            ))
            return false;
        const Emit emitted = emit_plain(plain, skip, request, result, error);
        if (emitted == Emit::failed)
            return false;
        if (emitted == Emit::stopped)
            return true;
    }
    if (skip != 0 || result.produced + request.skip != found->file.size)
        return fail(error, "the file could not be read");
    return true;
}

} // namespace oa::formats::inno_setup
