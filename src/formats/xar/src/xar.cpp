// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The xar header, its zlib table of contents and one gzip member. The
// deflate itself is the zip reader's inflater; the zlib and gzip wrappers
// are read here.

#include "oa/formats/xar.hpp"

#include "oa/formats/zip/inflate.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <zlib.h>

namespace oa::formats::xar {
namespace {

constexpr uint32_t k_magic = 0x78617221;
constexpr std::size_t k_fixed_header = 28;
constexpr uint16_t k_max_header = 4096;
constexpr uint32_t k_gzip_header_cap = 65536;

[[nodiscard]] bool be16(std::span<const uint8_t> bytes, std::size_t at, uint16_t& value) noexcept {
    if (at + 2 > bytes.size())
        return false;
    value = static_cast<uint16_t>((uint16_t{bytes[at]} << 8) | bytes[at + 1]);
    return true;
}

[[nodiscard]] bool be32(std::span<const uint8_t> bytes, std::size_t at, uint32_t& value) noexcept {
    if (at + 4 > bytes.size())
        return false;
    value = (uint32_t{bytes[at]} << 24) | (uint32_t{bytes[at + 1]} << 16) |
            (uint32_t{bytes[at + 2]} << 8) | uint32_t{bytes[at + 3]};
    return true;
}

[[nodiscard]] bool be64(std::span<const uint8_t> bytes, std::size_t at, uint64_t& value) noexcept {
    if (at + 8 > bytes.size())
        return false;
    value = 0;
    for (std::size_t index = 0; index < 8; ++index)
        value = (value << 8) | bytes[at + index];
    return true;
}

[[nodiscard]] bool le32(std::span<const uint8_t> bytes, std::size_t at, uint32_t& value) noexcept {
    if (at + 4 > bytes.size())
        return false;
    value = uint32_t{bytes[at]} | (uint32_t{bytes[at + 1]} << 8) | (uint32_t{bytes[at + 2]} << 16) |
            (uint32_t{bytes[at + 3]} << 24);
    return true;
}

void fail(Fault& fault, Fault why) noexcept {
    fault = why;
}

[[nodiscard]] bool inflate_zlib(
    std::span<const uint8_t> compressed,
    uint64_t uncompressed_bytes,
    std::vector<uint8_t>& plain,
    Fault& fault
) {
    plain.clear();
    if (uncompressed_bytes > max_toc_bytes) {
        fail(fault, Fault::toc_too_large);
        return false;
    }
    if (compressed.size() < 6) {
        fail(fault, Fault::truncated);
        return false;
    }
    const unsigned method = compressed[0];
    const unsigned flag_byte = compressed[1];
    if (((method << 8) + flag_byte) % 31 != 0 || (method & 0x0f) != 8 || (method >> 4) > 7) {
        fail(fault, Fault::inflate_failed);
        return false;
    }
    if ((flag_byte & 0x20) != 0) {
        fail(fault, Fault::inflate_failed);
        return false;
    }
    plain.assign(static_cast<std::size_t>(uncompressed_bytes), 0);
    oa::formats::zip::Inflater inflater;
    if (!inflater.start()) {
        plain.clear();
        fail(fault, Fault::inflate_failed);
        return false;
    }
    const oa::formats::zip::Inflater::Pass pass = inflater.pass(compressed.subspan(2), plain);
    inflater.finish();
    if (pass.failed || !pass.ended || pass.produced != plain.size()) {
        plain.clear();
        fail(fault, Fault::inflate_failed);
        return false;
    }
    if (pass.consumed + 6 != compressed.size()) {
        plain.clear();
        fail(fault, Fault::inflate_failed);
        return false;
    }
    uint32_t stored = 0;
    if (!be32(compressed, 2 + pass.consumed, stored)) {
        plain.clear();
        fail(fault, Fault::truncated);
        return false;
    }
    const uLong sum =
        ::adler32(::adler32(0L, Z_NULL, 0), plain.data(), static_cast<uInt>(plain.size()));
    if (stored != static_cast<uint32_t>(sum)) {
        plain.clear();
        fail(fault, Fault::checksum);
        return false;
    }
    fault = Fault::ok;
    return true;
}

[[nodiscard]] int hex_value(char digit) noexcept {
    if (digit >= '0' && digit <= '9')
        return digit - '0';
    if (digit >= 'a' && digit <= 'f')
        return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F')
        return digit - 'A' + 10;
    return -1;
}

[[nodiscard]] bool append_utf8(std::string& dest, uint32_t code) {
    if (code == 0 || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
        return false;
    if (code < 0x80) {
        dest.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        dest.push_back(static_cast<char>(0xc0 | (code >> 6)));
        dest.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    } else if (code < 0x10000) {
        dest.push_back(static_cast<char>(0xe0 | (code >> 12)));
        dest.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        dest.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    } else {
        dest.push_back(static_cast<char>(0xf0 | (code >> 18)));
        dest.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
        dest.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        dest.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
    return true;
}

[[nodiscard]] bool append_unescaped(std::string& dest, std::string_view text, Fault& fault) {
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] != '&') {
            dest.push_back(text[index]);
            ++index;
            continue;
        }
        const std::size_t semi = text.find(';', index + 1);
        if (semi == std::string_view::npos) {
            fail(fault, Fault::bad_toc);
            return false;
        }
        const std::string_view ref = text.substr(index + 1, semi - (index + 1));
        if (ref == "amp")
            dest.push_back('&');
        else if (ref == "lt")
            dest.push_back('<');
        else if (ref == "gt")
            dest.push_back('>');
        else if (ref == "quot")
            dest.push_back('"');
        else if (ref == "apos")
            dest.push_back('\'');
        else if (!ref.empty() && ref.front() == '#') {
            uint32_t code = 0;
            std::size_t digit = 1;
            int base = 10;
            if (ref.size() > 1 && (ref[1] == 'x' || ref[1] == 'X')) {
                base = 16;
                digit = 2;
            }
            if (digit >= ref.size()) {
                fail(fault, Fault::bad_toc);
                return false;
            }
            for (; digit < ref.size(); ++digit) {
                const int value =
                    base == 16 ? hex_value(ref[digit])
                               : (ref[digit] >= '0' && ref[digit] <= '9' ? ref[digit] - '0' : -1);
                if (value < 0 || code > (0x10ffffu - static_cast<uint32_t>(value)) /
                                            static_cast<uint32_t>(base)) {
                    fail(fault, Fault::bad_toc);
                    return false;
                }
                code = code * static_cast<uint32_t>(base) + static_cast<uint32_t>(value);
            }
            if (!append_utf8(dest, code)) {
                fail(fault, Fault::bad_toc);
                return false;
            }
        } else {
            fail(fault, Fault::bad_toc);
            return false;
        }
        index = semi + 1;
    }
    return true;
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\n' ||
                             text.front() == '\r'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' ||
                             text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

[[nodiscard]] bool parse_u64(std::string_view text, uint64_t& value) noexcept {
    text = trim(text);
    if (text.empty())
        return false;
    value = 0;
    for (char digit : text) {
        if (digit < '0' || digit > '9')
            return false;
        const uint64_t next = value * 10 + static_cast<uint64_t>(digit - '0');
        if (next < value)
            return false;
        value = next;
    }
    return true;
}

struct TocNode {
    std::size_t parent{std::numeric_limits<std::size_t>::max()};
    std::string component{};
    bool directory{false};
    bool saw_type{false};
    bool saw_data{false};
    bool saw_offset{false};
    bool saw_length{false};
    bool saw_size{false};
    Encoding encoding{Encoding::stored};
    uint64_t offset{};
    uint64_t length{};
    uint64_t bytes{};
};

struct TocFrame {
    std::string tag{};
    std::size_t node{std::numeric_limits<std::size_t>::max()};
    bool file_element{false};
};

struct TocParser {
    std::string_view xml{};
    std::size_t at{};
    std::vector<TocNode> nodes{};
    std::vector<TocFrame> stack{};
    Fault fault{Fault::ok};

    [[nodiscard]] bool failed() const noexcept { return fault != Fault::ok; }

    [[nodiscard]] std::size_t current_node() const noexcept {
        return stack.empty() ? std::numeric_limits<std::size_t>::max() : stack.back().node;
    }

    [[nodiscard]] bool skip_until(std::string_view mark) {
        const std::size_t found = xml.find(mark, at);
        if (found == std::string_view::npos) {
            fault = Fault::bad_toc;
            return false;
        }
        at = found + mark.size();
        return true;
    }

    [[nodiscard]] bool read_name(std::string& name) {
        name.clear();
        if (at >= xml.size() ||
            !((xml[at] >= 'A' && xml[at] <= 'Z') || (xml[at] >= 'a' && xml[at] <= 'z') ||
              xml[at] == '_' || xml[at] == ':')) {
            fault = Fault::bad_toc;
            return false;
        }
        const std::size_t start = at;
        ++at;
        while (at < xml.size()) {
            const char byte = xml[at];
            const bool ok = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                            (byte >= '0' && byte <= '9') || byte == '_' || byte == ':' ||
                            byte == '-' || byte == '.';
            if (!ok)
                break;
            ++at;
        }
        name.assign(xml.substr(start, at - start));
        return true;
    }

    void skip_space() {
        while (at < xml.size() &&
               (xml[at] == ' ' || xml[at] == '\t' || xml[at] == '\n' || xml[at] == '\r'))
            ++at;
    }

    [[nodiscard]] bool read_attr(std::string& key, std::string& value) {
        if (!read_name(key))
            return false;
        skip_space();
        if (at >= xml.size() || xml[at] != '=') {
            fault = Fault::bad_toc;
            return false;
        }
        ++at;
        skip_space();
        if (at >= xml.size() || (xml[at] != '"' && xml[at] != '\'')) {
            fault = Fault::bad_toc;
            return false;
        }
        const char quote = xml[at];
        ++at;
        const std::size_t start = at;
        while (at < xml.size() && xml[at] != quote)
            ++at;
        if (at >= xml.size()) {
            fault = Fault::bad_toc;
            return false;
        }
        value.clear();
        if (!append_unescaped(value, xml.substr(start, at - start), fault))
            return false;
        ++at;
        return true;
    }

    [[nodiscard]] Encoding encoding_of(std::string_view style) const noexcept {
        if (style.empty() || style == "application/octet-stream")
            return Encoding::stored;
        if (style == "application/x-gzip")
            return Encoding::gzip;
        return Encoding::unsupported;
    }

    [[nodiscard]] bool open_tag() {
        std::string name;
        if (!read_name(name))
            return false;
        std::string style;
        bool saw_style = false;
        bool closing = false;
        while (true) {
            skip_space();
            if (at >= xml.size()) {
                fault = Fault::bad_toc;
                return false;
            }
            if (xml[at] == '>') {
                ++at;
                break;
            }
            if (xml[at] == '/') {
                closing = true;
                ++at;
                skip_space();
                if (at >= xml.size() || xml[at] != '>') {
                    fault = Fault::bad_toc;
                    return false;
                }
                ++at;
                break;
            }
            std::string key;
            std::string value;
            if (!read_attr(key, value))
                return false;
            if (key == "style") {
                style = std::move(value);
                saw_style = true;
            }
        }
        if (name == "file") {
            if (nodes.size() >= max_entries) {
                fault = Fault::too_many_entries;
                return false;
            }
            TocNode node;
            for (std::size_t index = stack.size(); index > 0; --index) {
                if (stack[index - 1].file_element) {
                    node.parent = stack[index - 1].node;
                    break;
                }
            }
            nodes.push_back(std::move(node));
            TocFrame frame;
            frame.tag = std::move(name);
            frame.node = nodes.size() - 1;
            frame.file_element = true;
            stack.push_back(std::move(frame));
        } else {
            if (name == "encoding" && saw_style) {
                const std::size_t node = current_node();
                if (node != std::numeric_limits<std::size_t>::max())
                    nodes[node].encoding = encoding_of(style);
            }
            TocFrame frame;
            frame.tag = std::move(name);
            frame.node = current_node();
            stack.push_back(std::move(frame));
        }
        if (closing)
            return close_top();
        return true;
    }

    [[nodiscard]] bool note_text(std::string_view text) {
        if (stack.empty())
            return true;
        const TocFrame& frame = stack.back();
        if (frame.node == std::numeric_limits<std::size_t>::max())
            return true;
        TocNode& node = nodes[frame.node];
        if (frame.tag == "name") {
            if (node.component.size() >= max_name_bytes) {
                fault = Fault::name_too_long;
                return false;
            }
            if (!append_unescaped(node.component, text, fault))
                return false;
            if (node.component.size() > max_name_bytes) {
                fault = Fault::name_too_long;
                return false;
            }
            return true;
        }
        if (frame.tag == "type") {
            const std::string_view kind = trim(text);
            if (kind.empty())
                return true;
            node.saw_type = true;
            node.directory = kind == "directory";
            return true;
        }
        if (frame.tag == "length" || frame.tag == "offset" || frame.tag == "size") {
            uint64_t number = 0;
            if (!parse_u64(text, number)) {
                fault = Fault::bad_number;
                return false;
            }
            if (frame.tag == "length") {
                node.length = number;
                node.saw_length = true;
                node.saw_data = true;
            } else if (frame.tag == "offset") {
                node.offset = number;
                node.saw_offset = true;
                node.saw_data = true;
            } else {
                node.bytes = number;
                node.saw_size = true;
                node.saw_data = true;
            }
        }
        return true;
    }

    [[nodiscard]] bool close_top() {
        if (stack.empty()) {
            fault = Fault::bad_toc;
            return false;
        }
        const TocFrame frame = stack.back();
        stack.pop_back();
        if (!frame.file_element)
            return true;
        TocNode& node = nodes[frame.node];
        if (node.saw_data) {
            node.directory = false;
            if (!node.saw_length || !node.saw_offset || !node.saw_size) {
                fault = Fault::bad_toc;
                return false;
            }
            if (node.length > std::numeric_limits<uint64_t>::max() - node.offset) {
                fault = Fault::bad_number;
                return false;
            }
        } else if (!node.saw_type || !node.directory) {
            fault = Fault::bad_toc;
            return false;
        }
        return true;
    }

    [[nodiscard]] bool close_tag() {
        std::string name;
        if (!read_name(name))
            return false;
        skip_space();
        if (at >= xml.size() || xml[at] != '>') {
            fault = Fault::bad_toc;
            return false;
        }
        ++at;
        if (stack.empty() || stack.back().tag != name) {
            fault = Fault::bad_toc;
            return false;
        }
        return close_top();
    }

    [[nodiscard]] bool markup() {
        if (xml.substr(at).starts_with("<!--")) {
            at += 4;
            return skip_until("-->");
        }
        if (xml.substr(at).starts_with("<?")) {
            at += 2;
            return skip_until("?>");
        }
        if (xml.substr(at).starts_with("<![CDATA[")) {
            at += 9;
            const std::size_t end = xml.find("]]>", at);
            if (end == std::string_view::npos) {
                fault = Fault::bad_toc;
                return false;
            }
            const bool noted = note_text(xml.substr(at, end - at));
            at = end + 3;
            return noted;
        }
        if (at < xml.size() && xml[at] == '/') {
            ++at;
            return close_tag();
        }
        return open_tag();
    }

    [[nodiscard]] bool parse() {
        if (!xml.empty() && static_cast<uint8_t>(xml[0]) == 0xef) {
            if (xml.size() < 3 || static_cast<uint8_t>(xml[1]) != 0xbb ||
                static_cast<uint8_t>(xml[2]) != 0xbf) {
                fault = Fault::bad_toc;
                return false;
            }
            at = 3;
        }
        while (at < xml.size()) {
            if (xml[at] == '<') {
                ++at;
                if (!markup())
                    return false;
                continue;
            }
            const std::size_t start = at;
            while (at < xml.size() && xml[at] != '<')
                ++at;
            if (!note_text(xml.substr(start, at - start)))
                return false;
        }
        if (!stack.empty()) {
            fault = Fault::bad_toc;
            return false;
        }
        return true;
    }
};

[[nodiscard]] bool component_ok(std::string_view name, Fault& fault) noexcept {
    if (name.empty() || name == "." || name == ".." || name.size() > max_name_bytes) {
        fault = name.size() > max_name_bytes ? Fault::name_too_long : Fault::unsafe_name;
        return false;
    }
    for (char byte : name) {
        if (byte == '/' || byte == '\\' || byte == '\0') {
            fault = Fault::unsafe_name;
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool
build_entries(const std::vector<TocNode>& nodes, std::vector<Entry>& entries, Fault& fault) {
    entries.clear();
    entries.reserve(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const TocNode& node = nodes[index];
        if (!component_ok(node.component, fault))
            return false;
        std::vector<std::size_t> chain;
        for (std::size_t at = index; at != std::numeric_limits<std::size_t>::max();) {
            chain.push_back(at);
            if (chain.size() > nodes.size()) {
                fault = Fault::bad_toc;
                return false;
            }
            at = nodes[at].parent;
        }
        Entry entry;
        for (std::size_t step = chain.size(); step > 0; --step) {
            if (!entry.path.empty())
                entry.path.push_back('/');
            const std::string& part = nodes[chain[step - 1]].component;
            if (!component_ok(part, fault))
                return false;
            entry.path.append(part);
        }
        entry.directory = node.directory;
        entry.encoding = node.directory ? Encoding::stored : node.encoding;
        entry.heap_offset = node.directory ? 0 : node.offset;
        entry.stored_bytes = node.directory ? 0 : node.length;
        entry.bytes = node.directory ? 0 : node.bytes;
        entries.push_back(std::move(entry));
    }
    fault = Fault::ok;
    return true;
}

} // namespace

const char* fault_text(Fault fault) noexcept {
    switch (fault) {
    case Fault::ok:
        return "";
    case Fault::truncated:
        return "the archive ended early";
    case Fault::bad_magic:
        return "the file is not a xar archive";
    case Fault::bad_version:
        return "the xar archive is a version this reader does not read";
    case Fault::header_size:
        return "the xar header is not a size this reader accepts";
    case Fault::toc_too_large:
        return "the xar table is larger than this reader accepts";
    case Fault::inflate_failed:
        return "the compressed data does not inflate";
    case Fault::checksum:
        return "a checksum does not match";
    case Fault::bad_toc:
        return "the xar table could not be read";
    case Fault::unsafe_name:
        return "a path in the xar table is not safe";
    case Fault::bad_number:
        return "a size in the xar table is not a number";
    case Fault::too_many_entries:
        return "the xar table names too many files";
    case Fault::name_too_long:
        return "a path in the xar table is too long";
    case Fault::unsupported:
        return "the gzip data is not one this reader accepts";
    }
    return "the archive could not be read";
}

bool read_header(std::span<const uint8_t> bytes, Archive& archive, Fault& fault) noexcept {
    archive.header_bytes = 0;
    archive.checksum_kind = 0;
    archive.toc_compressed_bytes = 0;
    archive.toc_bytes = 0;
    archive.heap_start = 0;
    archive.entries.clear();
    if (bytes.size() < k_fixed_header) {
        fault = Fault::truncated;
        return false;
    }
    uint32_t magic = 0;
    uint16_t header_bytes = 0;
    uint16_t version = 0;
    uint64_t compressed = 0;
    uint64_t uncompressed = 0;
    uint32_t checksum = 0;
    if (!be32(bytes, 0, magic) || !be16(bytes, 4, header_bytes) || !be16(bytes, 6, version) ||
        !be64(bytes, 8, compressed) || !be64(bytes, 16, uncompressed) ||
        !be32(bytes, 24, checksum)) {
        fault = Fault::truncated;
        return false;
    }
    if (magic != k_magic) {
        fault = Fault::bad_magic;
        return false;
    }
    if (header_bytes < k_fixed_header || header_bytes > k_max_header) {
        fault = Fault::header_size;
        return false;
    }
    if (version != 1) {
        fault = Fault::bad_version;
        return false;
    }
    if (compressed == 0 || uncompressed == 0) {
        fault = Fault::truncated;
        return false;
    }
    if (compressed > max_toc_bytes || uncompressed > max_toc_bytes) {
        fault = Fault::toc_too_large;
        return false;
    }
    if (compressed > std::numeric_limits<uint64_t>::max() - header_bytes) {
        fault = Fault::bad_number;
        return false;
    }
    archive.header_bytes = header_bytes;
    archive.checksum_kind = checksum;
    archive.toc_compressed_bytes = compressed;
    archive.toc_bytes = uncompressed;
    archive.heap_start = uint64_t{header_bytes} + compressed;
    fault = Fault::ok;
    return true;
}

bool read_toc(
    std::span<const uint8_t> compressed,
    uint64_t uncompressed_bytes,
    std::vector<Entry>& entries,
    Fault& fault
) {
    entries.clear();
    std::vector<uint8_t> plain;
    if (!inflate_zlib(compressed, uncompressed_bytes, plain, fault))
        return false;
    TocParser parser;
    parser.xml = std::string_view(reinterpret_cast<const char*>(plain.data()), plain.size());
    if (!parser.parse()) {
        entries.clear();
        fault = parser.fault == Fault::ok ? Fault::bad_toc : parser.fault;
        return false;
    }
    if (!build_entries(parser.nodes, entries, fault)) {
        entries.clear();
        return false;
    }
    return true;
}

bool read_archive(std::span<const uint8_t> file, Archive& archive, Fault& fault) {
    if (!read_header(file, archive, fault))
        return false;
    if (archive.heap_start > file.size()) {
        fault = Fault::truncated;
        return false;
    }
    const auto toc =
        file.subspan(archive.header_bytes, static_cast<std::size_t>(archive.toc_compressed_bytes));
    if (!read_toc(toc, archive.toc_bytes, archive.entries, fault))
        return false;
    return true;
}

const Entry* find_entry(const std::vector<Entry>& entries, std::string_view path) noexcept {
    for (const Entry& entry : entries) {
        if (entry.path == path)
            return &entry;
    }
    return nullptr;
}

struct GzipReader::State {
    enum class Stage : uint8_t {
        magic,
        extra_len,
        extra,
        name,
        comment,
        hcrc,
        inflate,
        trailer,
        done
    };

    oa::formats::zip::Inflater inflater{};
    Stage stage{Stage::magic};
    std::vector<uint8_t> stash{};
    uint8_t flags{};
    uint16_t extra_left{};
    uint32_t crc{};
    uint32_t isize{};
    uint8_t trailer[8]{};
    uint8_t trailer_got{};
    uint32_t header_seen{};
    bool inflate_on{};
    bool broken{};
    const char* why{""};

    void fail(const char* text) noexcept {
        broken = true;
        why = text;
        if (inflate_on) {
            inflater.finish();
            inflate_on = false;
        }
    }
};

struct GzipDetail {

    static constexpr uint8_t k_ftext = 1;
    static constexpr uint8_t k_fhcrc = 2;
    static constexpr uint8_t k_fextra = 4;
    static constexpr uint8_t k_fname = 8;
    static constexpr uint8_t k_fcomment = 16;
    static constexpr uint8_t k_reserved = 0xe0;

    static void arm_after_header(GzipReader::State& state) {
        if ((state.flags & k_fextra) != 0)
            state.stage = GzipReader::State::Stage::extra_len;
        else if ((state.flags & k_fname) != 0)
            state.stage = GzipReader::State::Stage::name;
        else if ((state.flags & k_fcomment) != 0)
            state.stage = GzipReader::State::Stage::comment;
        else if ((state.flags & k_fhcrc) != 0)
            state.stage = GzipReader::State::Stage::hcrc;
        else
            state.stage = GzipReader::State::Stage::inflate;
    }

    [[nodiscard]] static bool take_exact(
        GzipReader::State& state,
        std::span<const uint8_t> input,
        std::size_t& used,
        std::size_t need,
        uint8_t* dest
    ) {
        if (state.header_seen > k_gzip_header_cap) {
            state.fail("the gzip data is not one this reader accepts");
            return false;
        }
        std::vector<uint8_t> got = std::move(state.stash);
        state.stash.clear();
        while (got.size() < need && used < input.size()) {
            got.push_back(input[used]);
            ++used;
            if (state.header_seen + got.size() > k_gzip_header_cap) {
                state.fail("the gzip data is not one this reader accepts");
                return false;
            }
        }
        if (got.size() < need) {
            state.stash = std::move(got);
            return false;
        }
        if (dest != nullptr) {
            for (std::size_t index = 0; index < need; ++index)
                dest[index] = got[index];
        }
        if (got.size() > need)
            state.stash.assign(got.begin() + static_cast<std::ptrdiff_t>(need), got.end());
        state.header_seen += static_cast<uint32_t>(need);
        return true;
    }

    [[nodiscard]] static bool
    take_until_nul(GzipReader::State& state, std::span<const uint8_t> input, std::size_t& used) {
        while (used < input.size()) {
            const uint8_t byte = input[used];
            ++used;
            ++state.header_seen;
            if (state.header_seen > k_gzip_header_cap) {
                state.fail("the gzip data is not one this reader accepts");
                return false;
            }
            if (byte == 0)
                return true;
        }
        return false;
    }

    static void note_plain(GzipReader::State& state, std::span<const uint8_t> plain) {
        std::size_t at = 0;
        while (at < plain.size()) {
            const std::size_t step = std::min<std::size_t>(plain.size() - at, std::size_t{1} << 30);
            state.crc = static_cast<uint32_t>(
                ::crc32(state.crc, plain.data() + at, static_cast<uInt>(step))
            );
            at += step;
        }
        state.isize += static_cast<uint32_t>(plain.size());
    }
};

GzipReader::GzipReader() : state_(new (std::nothrow) State) {
}

GzipReader::~GzipReader() = default;

const char* GzipReader::error() const noexcept {
    if (state_ == nullptr)
        return "the gzip data does not inflate";
    return state_->why;
}

GzipFeed GzipReader::feed(std::span<const uint8_t> input, std::span<uint8_t> output) {
    GzipFeed done{};
    if (state_ == nullptr) {
        done.failed = true;
        return done;
    }
    State& state = *state_;
    if (state.broken) {
        done.failed = true;
        return done;
    }
    if (state.stage == State::Stage::done) {
        if (!input.empty()) {
            state.fail("the gzip data does not inflate");
            done.failed = true;
        } else {
            done.finished = true;
        }
        return done;
    }
    std::size_t used = 0;
    std::size_t produced = 0;
    while (!state.broken && state.stage != State::Stage::done) {
        if (state.stage == State::Stage::magic) {
            uint8_t fixed[10];
            if (!GzipDetail::take_exact(state, input, used, sizeof fixed, fixed))
                break;
            if (fixed[0] != 0x1f || fixed[1] != 0x8b || fixed[2] != 8 ||
                (fixed[3] & GzipDetail::k_reserved) != 0) {
                state.fail("the gzip data is not one this reader accepts");
                break;
            }
            (void)GzipDetail::k_ftext;
            state.flags = fixed[3];
            GzipDetail::arm_after_header(state);
            continue;
        }
        if (state.stage == State::Stage::extra_len) {
            uint8_t pair[2];
            if (!GzipDetail::take_exact(state, input, used, sizeof pair, pair))
                break;
            state.extra_left = static_cast<uint16_t>(pair[0] | (uint16_t{pair[1]} << 8));
            state.stage = State::Stage::extra;
            continue;
        }
        if (state.stage == State::Stage::extra) {
            if (state.extra_left == 0) {
                if ((state.flags & GzipDetail::k_fname) != 0)
                    state.stage = State::Stage::name;
                else if ((state.flags & GzipDetail::k_fcomment) != 0)
                    state.stage = State::Stage::comment;
                else if ((state.flags & GzipDetail::k_fhcrc) != 0)
                    state.stage = State::Stage::hcrc;
                else
                    state.stage = State::Stage::inflate;
                continue;
            }
            const std::size_t room = input.size() - used;
            const std::size_t step = std::min<std::size_t>(state.extra_left, room);
            if (step == 0)
                break;
            used += step;
            state.header_seen += static_cast<uint32_t>(step);
            state.extra_left = static_cast<uint16_t>(state.extra_left - step);
            if (state.header_seen > k_gzip_header_cap) {
                state.fail("the gzip data is not one this reader accepts");
                break;
            }
            continue;
        }
        if (state.stage == State::Stage::name || state.stage == State::Stage::comment) {
            if (!GzipDetail::take_until_nul(state, input, used))
                break;
            if (state.stage == State::Stage::name && (state.flags & GzipDetail::k_fcomment) != 0)
                state.stage = State::Stage::comment;
            else if ((state.flags & GzipDetail::k_fhcrc) != 0)
                state.stage = State::Stage::hcrc;
            else
                state.stage = State::Stage::inflate;
            continue;
        }
        if (state.stage == State::Stage::hcrc) {
            uint8_t pair[2];
            if (!GzipDetail::take_exact(state, input, used, sizeof pair, pair))
                break;
            state.stage = State::Stage::inflate;
            continue;
        }
        if (state.stage == State::Stage::inflate) {
            if (!state.stash.empty()) {
                state.fail("the gzip data does not inflate");
                break;
            }
            if (!state.inflate_on) {
                if (!state.inflater.start()) {
                    state.fail("the gzip data does not inflate");
                    break;
                }
                state.inflate_on = true;
            }
            const auto pass = state.inflater.pass(input.subspan(used), output.subspan(produced));
            used += pass.consumed;
            if (pass.produced > 0)
                GzipDetail::note_plain(state, output.subspan(produced, pass.produced));
            produced += pass.produced;
            if (pass.failed) {
                state.fail("the gzip data does not inflate");
                break;
            }
            if (pass.ended) {
                state.inflater.finish();
                state.inflate_on = false;
                state.stage = State::Stage::trailer;
                continue;
            }
            break;
        }
        if (state.stage == State::Stage::trailer) {
            while (state.trailer_got < 8 && used < input.size()) {
                state.trailer[state.trailer_got] = input[used];
                ++state.trailer_got;
                ++used;
            }
            if (state.trailer_got < 8)
                break;
            uint32_t expect_crc = 0;
            uint32_t expect_size = 0;
            if (!le32(std::span<const uint8_t>(state.trailer, 8), 0, expect_crc) ||
                !le32(std::span<const uint8_t>(state.trailer, 8), 4, expect_size) ||
                expect_crc != state.crc || expect_size != state.isize) {
                state.fail("the gzip checksum does not match");
                break;
            }
            state.stage = State::Stage::done;
            if (used < input.size()) {
                state.fail("the gzip data does not inflate");
                break;
            }
            break;
        }
        state.fail("the gzip data does not inflate");
        break;
    }
    done.consumed = used;
    done.produced = produced;
    done.failed = state.broken;
    done.finished = !state.broken && state.stage == State::Stage::done;
    return done;
}

bool inflate_gzip(std::span<const uint8_t> compressed, std::vector<uint8_t>& plain, Fault& fault) {
    plain.clear();
    GzipReader reader;
    std::size_t at = 0;
    uint8_t room[4096];
    if (compressed.empty()) {
        fault = Fault::truncated;
        return false;
    }
    while (at < compressed.size()) {
        const GzipFeed fed = reader.feed(compressed.subspan(at), room);
        if (fed.failed) {
            const std::string_view why = reader.error() == nullptr ? "" : reader.error();
            if (why.find("checksum") != std::string_view::npos)
                fault = Fault::checksum;
            else if (why.find("not one this reader accepts") != std::string_view::npos)
                fault = Fault::unsupported;
            else
                fault = Fault::inflate_failed;
            plain.clear();
            return false;
        }
        plain.insert(plain.end(), room, room + fed.produced);
        if (fed.consumed == 0 && fed.produced == 0 && !fed.finished) {
            fault = Fault::truncated;
            plain.clear();
            return false;
        }
        at += fed.consumed;
        if (fed.finished) {
            if (at != compressed.size()) {
                fault = Fault::inflate_failed;
                plain.clear();
                return false;
            }
            fault = Fault::ok;
            return true;
        }
    }
    fault = Fault::truncated;
    plain.clear();
    return false;
}

} // namespace oa::formats::xar
