// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// UDIF: a fixture image, and images built in memory for zlib, raw, zeroes,
// a refused kind, and a block map that does not parse.

#include "oa/formats/udif.hpp"
#include "oa/test/check.hpp"

#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

using oa::formats::udif::Image;
using oa::formats::udif::ReadHooks;

void put_be32(std::vector<uint8_t>& bytes, std::size_t at, uint32_t value) {
    bytes[at] = static_cast<uint8_t>(value >> 24);
    bytes[at + 1] = static_cast<uint8_t>(value >> 16);
    bytes[at + 2] = static_cast<uint8_t>(value >> 8);
    bytes[at + 3] = static_cast<uint8_t>(value);
}

void put_be64(std::vector<uint8_t>& bytes, std::size_t at, uint64_t value) {
    put_be32(bytes, at, static_cast<uint32_t>(value >> 32));
    put_be32(bytes, at + 4, static_cast<uint32_t>(value));
}

bool read_bytes(void* context, uint64_t offset, uint8_t* out, std::size_t count) {
    const auto* bytes = static_cast<const std::vector<uint8_t>*>(context);
    if (offset > bytes->size() || count > bytes->size() - static_cast<std::size_t>(offset))
        return false;
    std::memcpy(out, bytes->data() + static_cast<std::size_t>(offset), count);
    return true;
}

ReadHooks hooks_for(const std::vector<uint8_t>& bytes) {
    ReadHooks hooks;
    hooks.context = const_cast<std::vector<uint8_t>*>(&bytes);
    hooks.read = &read_bytes;
    return hooks;
}

std::vector<uint8_t> zlib_of(const std::vector<uint8_t>& plain) {
    uLongf bound = compressBound(static_cast<uLong>(plain.size()));
    std::vector<uint8_t> compressed(bound);
    const int result =
        compress2(compressed.data(), &bound, plain.data(), static_cast<uLong>(plain.size()), 9);
    OA_CHECK(result == Z_OK);
    compressed.resize(bound);
    return compressed;
}

std::string base64_of(const std::vector<uint8_t>& bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string text;
    uint32_t window = 0;
    int bits = 0;
    for (uint8_t byte : bytes) {
        window = (window << 8) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            text.push_back(alphabet[(window >> bits) & 0x3f]);
        }
    }
    if (bits > 0)
        text.push_back(alphabet[(window << (6 - bits)) & 0x3f]);
    while (text.size() % 4 != 0)
        text.push_back('=');
    return text;
}

struct BuiltRun {
    uint32_t kind{};
    uint64_t relative{};
    uint64_t count{};
    uint64_t offset{};
    uint64_t length{};
};

std::vector<uint8_t>
mish_table(uint64_t sector_start, uint64_t sector_count, const std::vector<BuiltRun>& runs) {
    std::vector<uint8_t> table(204 + runs.size() * 40, 0);
    put_be32(table, 0, 0x6d697368);
    put_be32(table, 4, 1);
    put_be64(table, 8, sector_start);
    put_be64(table, 16, sector_count);
    put_be32(table, 200, static_cast<uint32_t>(runs.size()));
    for (std::size_t index = 0; index < runs.size(); ++index) {
        const std::size_t at = 204 + index * 40;
        put_be32(table, at, runs[index].kind);
        put_be64(table, at + 8, runs[index].relative);
        put_be64(table, at + 16, runs[index].count);
        put_be64(table, at + 24, runs[index].offset);
        put_be64(table, at + 32, runs[index].length);
    }
    return table;
}

std::vector<uint8_t>
image_with(const std::vector<uint8_t>& payload, const std::string& xml, uint64_t sectors) {
    std::vector<uint8_t> image = payload;
    const uint64_t xml_at = image.size();
    image.insert(image.end(), xml.begin(), xml.end());
    const std::size_t trailer = image.size();
    image.resize(image.size() + 512, 0);
    put_be32(image, trailer, 0x6b6f6c79);
    put_be32(image, trailer + 4, 4);
    put_be32(image, trailer + 8, 512);
    put_be32(image, trailer + 56, 1);
    put_be32(image, trailer + 60, 1);
    put_be64(image, trailer + 216, xml_at);
    put_be64(image, trailer + 224, xml.size());
    put_be64(image, trailer + 492, sectors);
    return image;
}

std::string plist_with(const std::string& data) {
    return std::string(
               "<?xml version=\"1.0\"?>\n<plist><dict><key>blkx</key><array><dict>"
               "<key>Data</key><data>"
           ) +
           data + "</data></dict></array></dict></plist>";
}

std::vector<uint8_t> read_file(const char* path) {
    std::ifstream file(path, std::ios::binary);
    OA_CHECK(file.good());
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()
    );
}

} // namespace

int main() {
    const std::vector<uint8_t> fixture = read_file(OA_UDIF_FIXTURE);
    {
        Image image;
        std::string error;
        OA_CHECK(image.open(hooks_for(fixture), fixture.size(), &error));
        OA_CHECK(image.disk_bytes() == 24576ull * 512);
        std::vector<uint8_t> head(2048);
        OA_CHECK(image.read(0, head, &error));
        OA_CHECK(image.open(hooks_for(fixture), fixture.size(), &error));
    }

    std::vector<uint8_t> plain(512, 0x5a);
    plain[0] = 0x11;
    plain[511] = 0x22;
    const std::vector<uint8_t> compressed = zlib_of(plain);
    std::vector<uint8_t> raw(512, 0x44);
    std::vector<uint8_t> payload = compressed;
    payload.insert(payload.end(), raw.begin(), raw.end());
    const std::vector<BuiltRun> runs = {
        {0x80000005, 0, 1, 0, compressed.size()},
        {0x00000002, 1, 1, 0, 0},
        {0x00000001, 2, 1, compressed.size(), raw.size()},
        {0xffffffff, 3, 0, 0, 0},
    };
    const std::vector<uint8_t> good =
        image_with(payload, plist_with(base64_of(mish_table(0, 3, runs))), 3);
    {
        Image image;
        std::string error;
        OA_CHECK(image.open(hooks_for(good), good.size(), &error));
        std::vector<uint8_t> out(512 * 3);
        OA_CHECK(image.read(0, out, &error));
        OA_CHECK(std::memcmp(out.data(), plain.data(), 512) == 0);
        OA_CHECK(out[512] == 0 && out[1023] == 0);
        OA_CHECK(std::memcmp(out.data() + 1024, raw.data(), 512) == 0);
        std::vector<uint8_t> slice(4);
        OA_CHECK(image.read(510, slice, &error));
        OA_CHECK(slice[0] == 0x5a && slice[1] == 0x22 && slice[2] == 0 && slice[3] == 0);
        OA_CHECK(!image.read(1536, slice, &error));
    }

    {
        std::vector<uint8_t> zeros(512, 0);
        const std::vector<BuiltRun> zero_runs = {
            {0x00000000, 0, 1, 0, 0},
            {0xffffffff, 1, 0, 0, 0},
        };
        const std::vector<uint8_t> image_bytes =
            image_with({}, plist_with(base64_of(mish_table(0, 1, zero_runs))), 1);
        Image image;
        std::string error;
        OA_CHECK(image.open(hooks_for(image_bytes), image_bytes.size(), &error));
        std::vector<uint8_t> out(512, 1);
        OA_CHECK(image.read(0, out, &error));
        OA_CHECK(out == zeros);
    }

    {
        const std::vector<BuiltRun> refused = {
            {0x80000006, 0, 1, 0, 8},
            {0xffffffff, 1, 0, 0, 0},
        };
        std::vector<uint8_t> body(8, 1);
        const std::vector<uint8_t> image_bytes =
            image_with(body, plist_with(base64_of(mish_table(0, 1, refused))), 1);
        Image image;
        std::string error;
        OA_CHECK(!image.open(hooks_for(image_bytes), image_bytes.size(), &error));
        OA_CHECK(error.find("bzip2") != std::string::npos);
    }

    {
        const std::vector<BuiltRun> unknown = {
            {0x00000009, 0, 1, 0, 0},
            {0xffffffff, 1, 0, 0, 0},
        };
        const std::vector<uint8_t> image_bytes =
            image_with({}, plist_with(base64_of(mish_table(0, 1, unknown))), 1);
        Image image;
        std::string error;
        OA_CHECK(!image.open(hooks_for(image_bytes), image_bytes.size(), &error));
        OA_CHECK(error.find("unknown compression") != std::string::npos);
    }

    {
        Image image;
        std::string error;
        std::vector<uint8_t> short_file(100, 0);
        OA_CHECK(!image.open(hooks_for(short_file), short_file.size(), &error));
        std::vector<uint8_t> bad = good;
        bad[bad.size() - 512] = 0;
        OA_CHECK(!image.open(hooks_for(bad), bad.size(), &error));
        OA_CHECK(error.find("not a UDIF") != std::string::npos);
    }

    {
        std::vector<uint8_t> image_bytes = good;
        const std::size_t trailer = image_bytes.size() - 512;
        put_be64(image_bytes, trailer + 224, 50ull << 20);
        Image image;
        std::string error;
        OA_CHECK(!image.open(hooks_for(image_bytes), image_bytes.size(), &error));
        OA_CHECK(error.find("block map") != std::string::npos);
    }

    {
        const std::vector<uint8_t> image_bytes = image_with(payload, plist_with("!!!!"), 3);
        Image image;
        std::string error;
        OA_CHECK(!image.open(hooks_for(image_bytes), image_bytes.size(), &error));
        OA_CHECK(error.find("block map") != std::string::npos);
    }

    {
        const std::vector<BuiltRun> outside = {
            {0x00000001, 0, 1, 100000, 512},
            {0xffffffff, 1, 0, 0, 0},
        };
        const std::vector<uint8_t> image_bytes =
            image_with(raw, plist_with(base64_of(mish_table(0, 1, outside))), 1);
        Image image;
        std::string error;
        OA_CHECK(!image.open(hooks_for(image_bytes), image_bytes.size(), &error));
        OA_CHECK(error.find("block map") != std::string::npos);
    }

    {
        std::vector<uint8_t> junk(32, 0x33);
        const std::vector<BuiltRun> broken = {
            {0x80000005, 0, 1, 0, junk.size()},
            {0xffffffff, 1, 0, 0, 0},
        };
        const std::vector<uint8_t> image_bytes =
            image_with(junk, plist_with(base64_of(mish_table(0, 1, broken))), 1);
        Image image;
        std::string error;
        OA_CHECK(image.open(hooks_for(image_bytes), image_bytes.size(), &error));
        std::vector<uint8_t> out(512);
        OA_CHECK(!image.read(0, out, &error));
    }

    return oa::test::check_exit_status();
}
