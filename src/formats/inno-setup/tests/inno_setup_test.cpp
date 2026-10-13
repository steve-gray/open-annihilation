// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A small Inno Setup 5.5.7 installer, built around tests/data/setup-body.bin,
// is read back: a stored piece, an LZMA2 piece joined onto it, and files that
// are truncated or are not this version. The body is made by
// tests/make_fixture.py. This test does not run that script, and the body
// holds no game data.

#include "oa/formats/inno_setup/installer.hpp"
#include "oa/test/check.hpp"
#include "oa/test/pe_image.hpp"
#include "oa/test/scratch_directory.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <zlib.h>

namespace {

namespace fs = std::filesystem;
using oa::formats::inno_setup::File;
using oa::formats::inno_setup::ReadRequest;
using oa::formats::inno_setup::ReadResult;
using oa::formats::inno_setup::Reader;

struct Collected {
    std::string bytes;
};

bool collect(void* context, std::span<const uint8_t> bytes) {
    auto* collected = static_cast<Collected*>(context);
    collected->bytes.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

void store_u32(uint8_t* bytes, uint32_t value) {
    for (size_t index = 0; index < 4; ++index)
        bytes[index] = static_cast<uint8_t>(value >> (index * 8));
}

uint32_t checksum(const uint8_t* bytes, size_t size) {
    const uLong start = crc32(0L, Z_NULL, 0);
    if (size == 0)
        return static_cast<uint32_t>(start);
    return static_cast<uint32_t>(crc32(start, bytes, static_cast<uInt>(size)));
}

/// A directory removed when the test ends.
struct Scratch {
    fs::path path;

    Scratch() : path(oa::test::make_scratch_directory("oa-g18-inno")) {}

    ~Scratch() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

std::vector<uint8_t> read_bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    OA_CHECK(file.good());
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()
    );
}

/// Wraps the fixture body in a program file and writes the offset table.
std::vector<uint8_t> assemble() {
    const fs::path data(OA_INNO_TEST_DATA);
    const std::vector<uint8_t> body = read_bytes(data / "setup-body.bin");
    std::ifstream layout(data / "layout.txt");
    std::string line;
    OA_CHECK(static_cast<bool>(std::getline(layout, line)));
    uint64_t ident = 0;
    const auto parsed = std::from_chars(line.data(), line.data() + line.size(), ident);
    OA_CHECK(parsed.ec == std::errc{});
    OA_CHECK(ident < body.size());

    oa::test::pe_image::Resource resource;
    resource.type_id = 10;
    resource.id = 11111;
    resource.language = 0;
    resource.data.assign(44, 0);
    oa::test::pe_image::Image image = oa::test::pe_image::build({resource});
    OA_CHECK(image.data + 44 <= image.bytes.size());
    const uint64_t data_offset = image.bytes.size();
    image.bytes.insert(image.bytes.end(), body.begin(), body.end());
    const uint64_t header = data_offset + ident;
    OA_CHECK(image.bytes.size() <= 0xffffffffu);
    OA_CHECK(header <= 0xffffffffu);

    uint8_t* table = image.bytes.data() + image.data;
    const uint8_t magic[12] = {'r', 'D', 'l', 'P', 't', 'S', 0xcd, 0xe6, 0xd7, 0x7b, 0x0b, '*'};
    std::copy(std::begin(magic), std::end(magic), table);
    const auto file_size = static_cast<uint32_t>(image.bytes.size());
    store_u32(table + 12, 1);
    store_u32(table + 16, file_size);
    store_u32(table + 20, file_size);
    store_u32(table + 24, 0);
    store_u32(table + 28, 0);
    store_u32(table + 32, static_cast<uint32_t>(header));
    store_u32(table + 36, static_cast<uint32_t>(data_offset));
    store_u32(table + 40, checksum(table, 40));
    return image.bytes;
}

void write_bytes(const fs::path& path, std::span<const uint8_t> bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    OA_CHECK(file.good());
    if (!bytes.empty())
        file.write(
            reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
        );
    OA_CHECK(file.good());
}

const File* find_file(const std::vector<File>& files, std::string_view name) {
    for (const File& file : files) {
        if (file.path == name)
            return &file;
    }
    return nullptr;
}

void expect_one(Reader& reader, std::string_view name, std::string_view text, uint64_t skip) {
    Collected collected;
    ReadRequest request;
    request.name = name;
    request.skip = skip;
    request.write = collect;
    request.context = &collected;
    ReadResult result;
    std::string error;
    OA_CHECK(reader.read(request, result, &error));
    OA_CHECK(error.empty());
    OA_CHECK(!result.stopped);
    OA_CHECK(result.produced == text.size());
    OA_CHECK(collected.bytes == text);
}

void expect_good(const fs::path& path) {
    Reader closed;
    std::vector<File> none;
    std::string closed_error;
    OA_CHECK(!closed.list(none, &closed_error));
    OA_CHECK(!closed_error.empty());

    Reader reader;
    std::string error;
    OA_CHECK(reader.open(path, &error));
    OA_CHECK(error.empty());
    std::vector<File> files;
    OA_CHECK(reader.list(files, &error));
    OA_CHECK(files.size() == 2);
    const File* readme = find_file(files, "docs/readme.txt");
    const File* notes = find_file(files, "notes.txt");
    OA_CHECK(readme != nullptr && notes != nullptr);
    if (readme != nullptr) {
        OA_CHECK(readme->size == 6);
        OA_CHECK(readme->modified == 1600000000);
    }
    if (notes != nullptr) {
        OA_CHECK(notes->size == 4);
        OA_CHECK(notes->modified == 0);
    }
    OA_CHECK(find_file(files, "support/ridge.dll") == nullptr);

    expect_one(reader, "docs/readme.txt", "ridge\n", 0);
    expect_one(reader, "notes.txt", "abcd", 0);
    expect_one(reader, "notes.txt", "cd", 2);

    Collected collected;
    ReadRequest missing;
    missing.name = "missing.txt";
    missing.write = collect;
    missing.context = &collected;
    ReadResult result;
    OA_CHECK(!reader.read(missing, result, &error));

    std::atomic<bool> stop{true};
    ReadRequest halted;
    halted.name = "notes.txt";
    halted.stop = &stop;
    halted.write = collect;
    halted.context = &collected;
    collected.bytes.clear();
    OA_CHECK(reader.read(halted, result, &error));
    OA_CHECK(result.stopped);
    OA_CHECK(result.produced == 0);
    OA_CHECK(collected.bytes.empty());
}

void expect_rejected(const fs::path& path) {
    Reader reader;
    std::string error;
    OA_CHECK(!reader.open(path, &error));
    OA_CHECK(!error.empty());
    std::vector<File> files;
    OA_CHECK(!reader.list(files, &error));
}

void expect_bad(const Scratch& scratch, const std::vector<uint8_t>& good) {
    const fs::path empty = scratch.path / "empty.bin";
    write_bytes(empty, {});
    expect_rejected(empty);

    std::vector<uint8_t> zeros(static_cast<size_t>(2) << 20, 0);
    const fs::path blank = scratch.path / "zeros.bin";
    write_bytes(blank, zeros);
    expect_rejected(blank);

    const fs::path cut = scratch.path / "cut.bin";
    const size_t half = std::max<size_t>(good.size() / 2, 1);
    write_bytes(cut, std::span<const uint8_t>(good.data(), half));
    expect_rejected(cut);

    std::string layout;
    {
        std::ifstream file(fs::path(OA_INNO_TEST_DATA) / "layout.txt");
        OA_CHECK(static_cast<bool>(std::getline(file, layout)));
    }
    uint64_t ident = 0;
    const auto parsed = std::from_chars(layout.data(), layout.data() + layout.size(), ident);
    OA_CHECK(parsed.ec == std::errc{});
    std::error_code failure;
    const auto body_size = fs::file_size(fs::path(OA_INNO_TEST_DATA) / "setup-body.bin", failure);
    OA_CHECK(!failure);
    OA_CHECK(body_size <= good.size());
    OA_CHECK(ident < body_size);
    const uint64_t header = good.size() - static_cast<uint64_t>(body_size) + ident;

    std::vector<uint8_t> wrong_version = good;
    OA_CHECK(header < wrong_version.size());
    wrong_version[static_cast<size_t>(header)] =
        static_cast<uint8_t>(wrong_version[static_cast<size_t>(header)] ^ 0xff);
    const fs::path version = scratch.path / "version.bin";
    write_bytes(version, wrong_version);
    Reader reader;
    std::string error;
    OA_CHECK(!reader.open(version, &error));
    OA_CHECK(error == "this setup data is not the version this reader opens");

    std::vector<uint8_t> wrong_block = good;
    const uint64_t block = header + 64;
    OA_CHECK(block < wrong_block.size());
    wrong_block[static_cast<size_t>(block)] =
        static_cast<uint8_t>(wrong_block[static_cast<size_t>(block)] ^ 0xff);
    const fs::path damaged = scratch.path / "block.bin";
    write_bytes(damaged, wrong_block);
    OA_CHECK(!reader.open(damaged, &error));
    OA_CHECK(error == "the setup header could not be read");
}

} // namespace

int main() {
    const Scratch scratch;
    const std::vector<uint8_t> good = assemble();
    const fs::path path = scratch.path / "setup.bin";
    write_bytes(path, good);
    expect_good(path);
    expect_bad(scratch, good);
    return oa::test::check_exit_status();
}
