// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Small xar archives built in the test: the header, the table, stored and
// gzip files, and the refusals.

#include "oa/formats/xar.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

namespace {

using oa::formats::xar::Archive;
using oa::formats::xar::Encoding;
using oa::formats::xar::Entry;
using oa::formats::xar::Fault;
using oa::formats::xar::GzipReader;
using oa::formats::xar::find_entry;
using oa::formats::xar::inflate_gzip;
using oa::formats::xar::read_archive;
using oa::formats::xar::read_header;

void put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void put32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void put64(std::vector<uint8_t>& out, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8)
        out.push_back(static_cast<uint8_t>(value >> shift));
}

[[nodiscard]] std::vector<uint8_t> zlib_of(std::string_view text) {
    const auto* plain = reinterpret_cast<const Bytef*>(text.data());
    uLongf bound = compressBound(static_cast<uLong>(text.size()));
    std::vector<uint8_t> compressed(bound);
    const int result = compress2(
        compressed.data(), &bound, plain, static_cast<uLong>(text.size()), Z_DEFAULT_COMPRESSION
    );
    if (result != Z_OK)
        return {};
    compressed.resize(bound);
    return compressed;
}

[[nodiscard]] std::vector<uint8_t> gzip_of(std::string_view text, bool with_name) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) !=
        Z_OK)
        return {};
    Bytef name[] = {'n', 'o', 't', 'e', 0};
    gz_header header{};
    header.os = 3;
    if (with_name) {
        header.name = name;
        if (deflateSetHeader(&stream, &header) != Z_OK) {
            deflateEnd(&stream);
            return {};
        }
    }
    const uLong bound = deflateBound(&stream, static_cast<uLong>(text.size())) + 64;
    std::vector<uint8_t> compressed(bound);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(text.data()));
    stream.avail_in = static_cast<uInt>(text.size());
    stream.next_out = compressed.data();
    stream.avail_out = static_cast<uInt>(compressed.size());
    const int result = deflate(&stream, Z_FINISH);
    const std::size_t produced = stream.total_out;
    deflateEnd(&stream);
    if (result != Z_STREAM_END)
        return {};
    compressed.resize(produced);
    return compressed;
}

[[nodiscard]] std::vector<uint8_t>
archive_of(std::string_view xml, const std::vector<uint8_t>& heap) {
    const std::vector<uint8_t> toc = zlib_of(xml);
    std::vector<uint8_t> file;
    put32(file, 0x78617221);
    put16(file, 28);
    put16(file, 1);
    put64(file, toc.size());
    put64(file, xml.size());
    put32(file, 1);
    file.insert(file.end(), toc.begin(), toc.end());
    file.insert(file.end(), heap.begin(), heap.end());
    return file;
}

void header_and_entries() {
    const std::vector<uint8_t> gzip = gzip_of("hello gzip", false);
    const std::string heap_text = "stored-bytes";
    std::vector<uint8_t> heap(heap_text.begin(), heap_text.end());
    heap.insert(heap.end(), gzip.begin(), gzip.end());
    const std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                            "<!-- table -->"
                            "<xar><toc>"
                            "<checksum style=\"sha1\"><offset>0</offset><size>20</size></checksum>"
                            "<file id=\"1\"><name>package.pkg</name><type>directory</type>"
                            "<file id=\"2\"><name>Scripts</name><type>file</type><data>"
                            "<length>" +
                            std::to_string(heap_text.size()) + "</length><offset>0</offset><size>" +
                            std::to_string(heap_text.size()) +
                            "</size><encoding style=\"application/octet-stream\"/>"
                            "</data></file>"
                            "<file id=\"3\"><name>note</name><type>file</type><data>"
                            "<length>" +
                            std::to_string(gzip.size()) + "</length><offset>" +
                            std::to_string(heap_text.size()) +
                            "</offset><size>10</size><encoding style=\"application/x-gzip\"/>"
                            "</data></file></file>"
                            "<file id=\"4\"><name>other</name><type>file</type><data>"
                            "<length>1</length><offset>0</offset><size>1</size>"
                            "<encoding style=\"application/x-bzip2\"/>"
                            "</data></file>"
                            "</toc></xar>";
    const std::vector<uint8_t> file = archive_of(xml, heap);
    Archive archive;
    Fault fault = Fault::ok;
    OA_CHECK(read_archive(file, archive, fault));
    OA_CHECK(fault == Fault::ok);
    OA_CHECK(archive.header_bytes == 28);
    OA_CHECK(archive.heap_start == 28 + archive.toc_compressed_bytes);
    const Entry* scripts = find_entry(archive.entries, "package.pkg/Scripts");
    const Entry* note = find_entry(archive.entries, "package.pkg/note");
    const Entry* other = find_entry(archive.entries, "other");
    const Entry* directory = find_entry(archive.entries, "package.pkg");
    OA_CHECK(scripts != nullptr && scripts->encoding == Encoding::stored);
    OA_CHECK(scripts->stored_bytes == heap_text.size());
    OA_CHECK(note != nullptr && note->encoding == Encoding::gzip);
    OA_CHECK(other != nullptr && other->encoding == Encoding::unsupported);
    OA_CHECK(directory != nullptr && directory->directory);
    std::vector<uint8_t> plain;
    OA_CHECK(inflate_gzip(std::span<const uint8_t>(heap).subspan(heap_text.size()), plain, fault));
    OA_CHECK(std::string(plain.begin(), plain.end()) == "hello gzip");
}

void refusals() {
    Archive archive;
    Fault fault = Fault::ok;
    std::vector<uint8_t> bad(28, 0);
    bad[0] = 'n';
    bad[1] = 'o';
    bad[2] = 'p';
    bad[3] = 'e';
    OA_CHECK(!read_header(bad, archive, fault));
    OA_CHECK(fault == Fault::bad_magic);
    std::vector<uint8_t> huge;
    put32(huge, 0x78617221);
    put16(huge, 28);
    put16(huge, 1);
    put64(huge, 32);
    put64(huge, (uint64_t{32} << 20) + 1);
    put32(huge, 0);
    OA_CHECK(!read_header(huge, archive, fault));
    OA_CHECK(fault == Fault::toc_too_large);
    std::vector<uint8_t> short_file;
    put32(short_file, 0x78617221);
    put16(short_file, 28);
    put16(short_file, 1);
    put64(short_file, 40);
    put64(short_file, 40);
    put32(short_file, 0);
    OA_CHECK(!read_archive(short_file, archive, fault));
    OA_CHECK(fault == Fault::truncated);
    const std::string xml =
        "<xar><toc><file id=\"1\"><name>..</name><type>directory</type></file></toc></xar>";
    const std::vector<uint8_t> hostile = archive_of(xml, {});
    OA_CHECK(!read_archive(hostile, archive, fault));
    OA_CHECK(fault == Fault::unsafe_name);
}

void gzip_pieces_and_checksum() {
    const std::vector<uint8_t> member = gzip_of("abcdef", true);
    OA_CHECK(!member.empty());
    OA_CHECK(member.size() > 10 && member[3] != 0);
    GzipReader reader;
    std::string plain;
    std::size_t at = 0;
    while (at < member.size()) {
        uint8_t room[3];
        const std::size_t step = std::min<std::size_t>(5, member.size() - at);
        const auto fed = reader.feed(std::span<const uint8_t>(member).subspan(at, step), room);
        OA_CHECK(!fed.failed);
        plain.append(reinterpret_cast<const char*>(room), fed.produced);
        OA_CHECK(fed.consumed > 0 || fed.finished || fed.produced > 0);
        at += fed.consumed;
        if (fed.finished)
            break;
    }
    OA_CHECK(plain == "abcdef");
    std::vector<uint8_t> broken = member;
    broken.back() ^= 0xff;
    std::vector<uint8_t> out;
    Fault fault = Fault::ok;
    OA_CHECK(!inflate_gzip(broken, out, fault));
    OA_CHECK(fault == Fault::checksum || fault == Fault::inflate_failed);
    const uint8_t method[] = {0x1f, 0x8b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    OA_CHECK(!inflate_gzip(method, out, fault));
    OA_CHECK(fault == Fault::unsupported);
}

} // namespace

int main() {
    header_and_entries();
    refusals();
    gzip_pieces_and_checksum();
    return oa::test::check_exit_status();
}
