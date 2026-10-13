// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// HFS+: the fixture image's catalogue, a volume whose ninth block lives in
// the extents overflow tree, a compressed file, a truncated record, an
// APFS partition and an Apple partition map.

#include "oa/formats/hfsplus.hpp"
#include "oa/formats/udif.hpp"
#include "oa/test/check.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

using oa::formats::hfsplus::CatalogItem;
using oa::formats::hfsplus::ReadHooks;
using oa::formats::hfsplus::Volume;

void put_be16(std::vector<uint8_t>& bytes, std::size_t at, uint16_t value) {
    bytes[at] = static_cast<uint8_t>(value >> 8);
    bytes[at + 1] = static_cast<uint8_t>(value);
}

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

void put_le32(std::vector<uint8_t>& bytes, std::size_t at, uint32_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8);
    bytes[at + 2] = static_cast<uint8_t>(value >> 16);
    bytes[at + 3] = static_cast<uint8_t>(value >> 24);
}

void put_le64(std::vector<uint8_t>& bytes, std::size_t at, uint64_t value) {
    put_le32(bytes, at, static_cast<uint32_t>(value));
    put_le32(bytes, at + 4, static_cast<uint32_t>(value >> 32));
}

void append_be16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}

void append_be32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value >> 24));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}

void append_utf16(std::vector<uint8_t>& bytes, const uint16_t* units, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index)
        append_be16(bytes, units[index]);
}

std::vector<uint8_t> catalogue_key(uint32_t parent, const uint16_t* units, std::size_t count) {
    std::vector<uint8_t> key;
    append_be16(key, static_cast<uint16_t>(6 + count * 2));
    append_be32(key, parent);
    append_be16(key, static_cast<uint16_t>(count));
    append_utf16(key, units, count);
    return key;
}

std::vector<uint8_t>
folder_record(uint32_t parent, const uint16_t* name, std::size_t count, uint32_t id) {
    std::vector<uint8_t> record = catalogue_key(parent, name, count);
    const std::size_t data = record.size();
    record.resize(data + 88, 0);
    put_be16(record, data, 1);
    put_be32(record, data + 8, id);
    return record;
}

std::vector<uint8_t> file_record(
    uint32_t parent,
    const uint16_t* name,
    std::size_t count,
    uint32_t id,
    uint64_t logical,
    uint32_t extent_start,
    uint32_t extent_count,
    uint8_t owner_flags
) {
    std::vector<uint8_t> record = catalogue_key(parent, name, count);
    const std::size_t data = record.size();
    record.resize(data + 248, 0);
    put_be16(record, data, 2);
    put_be32(record, data + 8, id);
    put_be32(record, data + 16, 2082844800u + 50u);
    record[data + 41] = owner_flags;
    put_be64(record, data + 88, logical);
    put_be32(record, data + 88 + 12, extent_count);
    if (extent_count != 0) {
        put_be32(record, data + 88 + 16, extent_start);
        put_be32(record, data + 88 + 20, extent_count);
    }
    return record;
}

std::vector<uint8_t>
thread_record(uint32_t id, uint32_t parent, const uint16_t* name, std::size_t count) {
    std::vector<uint8_t> record = catalogue_key(id, nullptr, 0);
    append_be16(record, 4);
    append_be16(record, 0);
    append_be32(record, parent);
    append_be16(record, static_cast<uint16_t>(count));
    append_utf16(record, name, count);
    return record;
}

std::vector<uint8_t>
leaf_node(uint32_t node_size, const std::vector<std::vector<uint8_t>>& records) {
    std::vector<uint8_t> node(node_size, 0);
    node[8] = 0xff;
    put_be16(node, 10, static_cast<uint16_t>(records.size()));
    std::size_t cursor = 14;
    std::vector<uint16_t> offsets;
    for (const std::vector<uint8_t>& record : records) {
        offsets.push_back(static_cast<uint16_t>(cursor));
        std::memcpy(node.data() + cursor, record.data(), record.size());
        cursor += record.size();
    }
    offsets.push_back(static_cast<uint16_t>(cursor));
    for (std::size_t index = 0; index < offsets.size(); ++index)
        put_be16(node, node_size - 2 * (index + 1), offsets[index]);
    return node;
}

std::vector<uint8_t> header_node(uint32_t node_size, uint32_t total_nodes, uint32_t leaf_records) {
    std::vector<uint8_t> node(node_size, 0);
    node[8] = 1;
    put_be16(node, 14, 1);
    put_be32(node, 16, 1);
    put_be32(node, 20, leaf_records);
    put_be32(node, 24, 1);
    put_be32(node, 28, 1);
    put_be16(node, 32, static_cast<uint16_t>(node_size));
    put_be16(node, 34, 520);
    put_be32(node, 36, total_nodes);
    put_be32(node, 46, node_size);
    put_be32(node, 52, 6);
    return node;
}

void write_fork(
    std::vector<uint8_t>& volume, std::size_t at, uint64_t logical, uint32_t start, uint32_t count
) {
    put_be64(volume, at, logical);
    put_be32(volume, at + 12, count);
    put_be32(volume, at + 16, start);
    put_be32(volume, at + 20, count);
}

std::vector<uint8_t> overflow_volume() {
    constexpr uint32_t block = 512;
    constexpr uint32_t blocks = 64;
    std::vector<uint8_t> volume(blocks * block, 0);
    put_be16(volume, 1024, 0x482B);
    put_be16(volume, 1026, 4);
    put_be32(volume, 1024 + 40, block);
    put_be32(volume, 1024 + 44, blocks);
    write_fork(volume, 1024 + 192, 1024, 16, 2);
    write_fork(volume, 1024 + 272, 4096, 4, 8);

    const uint16_t vol_name[] = {'V', 'o', 'l'};
    const uint16_t span_name[] = {'s', 'p', 'a', 'n', '.', 'b', 'i', 'n'};
    const uint16_t pack_name[] = {'p', 'a', 'c', 'k', '.', 't', 'x', 't'};
    const uint16_t cafe_name[] = {'c', 'a', 'f', 0x00E9};
    const uint16_t bad_name[] = {0xD800};
    const std::vector<std::vector<uint8_t>> records = {
        folder_record(1, vol_name, 3, 2),
        file_record(2, span_name, 8, 16, 9 * 512, 24, 8, 0),
        file_record(2, pack_name, 8, 17, 4, 0, 0, 0x20),
        file_record(2, cafe_name, 4, 18, 0, 0, 0, 0),
        file_record(2, bad_name, 1, 19, 0, 0, 0, 0),
        thread_record(16, 2, span_name, 8),
    };
    const std::vector<uint8_t> catalog_header =
        header_node(2048, 2, static_cast<uint32_t>(records.size()));
    const std::vector<uint8_t> catalog_leaf = leaf_node(2048, records);
    std::memcpy(volume.data() + 4 * block, catalog_header.data(), catalog_header.size());
    std::memcpy(volume.data() + 4 * block + 2048, catalog_leaf.data(), catalog_leaf.size());

    std::vector<uint8_t> overflow_key;
    append_be16(overflow_key, 10);
    overflow_key.push_back(0);
    overflow_key.push_back(0);
    append_be32(overflow_key, 16);
    append_be32(overflow_key, 8);
    std::vector<uint8_t> overflow_record = overflow_key;
    overflow_record.resize(overflow_key.size() + 64, 0);
    put_be32(overflow_record, overflow_key.size(), 32);
    put_be32(overflow_record, overflow_key.size() + 4, 1);
    const std::vector<uint8_t> extents_header = header_node(512, 2, 1);
    const std::vector<uint8_t> extents_leaf = leaf_node(512, {overflow_record});
    std::memcpy(volume.data() + 16 * block, extents_header.data(), extents_header.size());
    std::memcpy(volume.data() + 16 * block + 512, extents_leaf.data(), extents_leaf.size());

    std::memset(volume.data() + 24 * block, 0x11, 8 * block);
    std::memset(volume.data() + 32 * block, 0x22, block);
    return volume;
}

std::vector<uint8_t> truncated_volume() {
    std::vector<uint8_t> volume(16 * 512, 0);
    put_be16(volume, 1024, 0x482B);
    put_be16(volume, 1026, 4);
    put_be32(volume, 1024 + 40, 512);
    put_be32(volume, 1024 + 44, 16);
    write_fork(volume, 1024 + 272, 1024, 4, 2);
    const uint16_t name[] = {'A'};
    std::vector<uint8_t> record = catalogue_key(1, name, 1);
    record.resize(record.size() + 4, 0);
    put_be16(record, record.size() - 4, 1);
    const std::vector<uint8_t> header = header_node(512, 2, 1);
    const std::vector<uint8_t> leaf = leaf_node(512, {record});
    std::memcpy(volume.data() + 4 * 512, header.data(), header.size());
    std::memcpy(volume.data() + 4 * 512 + 512, leaf.data(), leaf.size());
    return volume;
}

bool read_vector(void* context, uint64_t offset, uint8_t* out, std::size_t count) {
    const auto* bytes = static_cast<const std::vector<uint8_t>*>(context);
    if (offset > bytes->size() || count > bytes->size() - static_cast<std::size_t>(offset))
        return false;
    std::memcpy(out, bytes->data() + static_cast<std::size_t>(offset), count);
    return true;
}

ReadHooks hooks_for(const std::vector<uint8_t>& bytes) {
    ReadHooks hooks;
    hooks.context = const_cast<std::vector<uint8_t>*>(&bytes);
    hooks.read = &read_vector;
    return hooks;
}

const CatalogItem* find_named(const Volume& volume, const std::string& name) {
    for (const CatalogItem& item : volume.catalog()) {
        if (item.kind == CatalogItem::Kind::file && item.name == name)
            return &item;
    }
    return nullptr;
}

std::string path_of(const Volume& volume, uint32_t id) {
    std::vector<std::string> parts;
    for (int hop = 0; hop < 64; ++hop) {
        if (id == 2 || id == 1 || id == 0)
            break;
        const CatalogItem* found = nullptr;
        for (const CatalogItem& item : volume.catalog()) {
            if (item.id == id &&
                (item.kind == CatalogItem::Kind::file || item.kind == CatalogItem::Kind::folder)) {
                found = &item;
                break;
            }
        }
        if (found == nullptr)
            break;
        parts.push_back(found->name);
        id = found->parent;
    }
    std::string path;
    for (std::size_t index = parts.size(); index > 0; --index) {
        if (!path.empty())
            path.push_back('/');
        path += parts[index - 1];
    }
    return path;
}

std::vector<uint8_t> read_file(const char* path) {
    std::ifstream file(path, std::ios::binary);
    OA_CHECK(file.good());
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()
    );
}

bool read_udif(void* context, uint64_t offset, uint8_t* out, std::size_t count) {
    auto* image = static_cast<oa::formats::udif::Image*>(context);
    std::string error;
    return image->read(offset, std::span<uint8_t>(out, count), &error);
}

bool read_image_file(void* context, uint64_t offset, uint8_t* out, std::size_t count) {
    const auto* bytes = static_cast<const std::vector<uint8_t>*>(context);
    if (offset > bytes->size() || count > bytes->size() - static_cast<std::size_t>(offset))
        return false;
    std::memcpy(out, bytes->data() + static_cast<std::size_t>(offset), count);
    return true;
}

} // namespace

int main() {
    const std::vector<uint8_t> fixture = read_file(OA_UDIF_FIXTURE);
    oa::formats::udif::Image image;
    oa::formats::udif::ReadHooks file_hooks;
    file_hooks.context = const_cast<std::vector<uint8_t>*>(&fixture);
    file_hooks.read = &read_image_file;
    std::string error;
    OA_CHECK(image.open(file_hooks, fixture.size(), &error));
    Volume fixture_volume;
    ReadHooks disk_hooks;
    disk_hooks.context = &image;
    disk_hooks.read = &read_udif;
    if (!fixture_volume.open_disk(disk_hooks, image.disk_bytes(), &error)) {
        std::fprintf(stderr, "fixture: %s\n", error.c_str());
        OA_CHECK(false);
    } else {
        const CatalogItem* rev = nullptr;
        const CatalogItem* decoy = nullptr;
        const CatalogItem* packed = nullptr;
        int rev_count = 0;
        for (const CatalogItem& item : fixture_volume.catalog()) {
            if (item.kind != CatalogItem::Kind::file || item.name != "rev31.gp3")
                continue;
            ++rev_count;
            const std::string path = path_of(fixture_volume, item.id);
            if (path.find("drive_c") != std::string::npos)
                rev = &item;
            else
                decoy = &item;
            if (item.name == "packed.txt")
                packed = &item;
        }
        for (const CatalogItem& item : fixture_volume.catalog()) {
            if (item.kind == CatalogItem::Kind::file && item.name == "packed.txt")
                packed = &item;
        }
        OA_CHECK(rev_count == 2);
        OA_CHECK(rev != nullptr && decoy != nullptr);
        std::vector<uint8_t> bytes(rev->data_bytes);
        OA_CHECK(fixture_volume.read_file(rev->id, 0, bytes, &error));
        const std::string text(bytes.begin(), bytes.end());
        OA_CHECK(text == "rev31-fixture\n");
        std::vector<uint8_t> other(decoy->data_bytes);
        OA_CHECK(fixture_volume.read_file(decoy->id, 0, other, &error));
        OA_CHECK(std::string(other.begin(), other.end()) == "not-the-game\n");
        OA_CHECK(packed != nullptr && packed->compressed);
        std::vector<uint8_t> kept(4, 0xab);
        OA_CHECK(!fixture_volume.read_file(packed->id, 0, kept, &error));
        OA_CHECK(error.find("compressed") != std::string::npos);
        OA_CHECK(kept[0] == 0xab);
        const CatalogItem* span = find_named(fixture_volume, "span.bin");
        if (span != nullptr) {
            std::vector<uint8_t> body(static_cast<std::size_t>(span->data_bytes));
            OA_CHECK(fixture_volume.read_file(span->id, 0, body, &error));
            OA_CHECK(!body.empty() && body.front() == 'Y' && body.back() == 'Y');
        }
    }

    const std::vector<uint8_t> synthetic = overflow_volume();
    Volume volume;
    if (!volume.open(hooks_for(synthetic), synthetic.size(), &error)) {
        std::fprintf(stderr, "synthetic: %s\n", error.c_str());
        OA_CHECK(false);
    } else {
        const CatalogItem* span = find_named(volume, "span.bin");
        OA_CHECK(span != nullptr && span->modified == 50);
        std::vector<uint8_t> body(9 * 512);
        OA_CHECK(volume.read_file(span->id, 0, body, &error));
        OA_CHECK(body[0] == 0x11 && body[8 * 512 - 1] == 0x11);
        OA_CHECK(body[8 * 512] == 0x22 && body.back() == 0x22);
        const CatalogItem* pack = find_named(volume, "pack.txt");
        OA_CHECK(pack != nullptr && pack->compressed);
        std::vector<uint8_t> untouched(4, 7);
        OA_CHECK(!volume.read_file(pack->id, 0, untouched, &error));
        OA_CHECK(untouched[0] == 7);
        const CatalogItem* cafe = find_named(volume, "caf\xc3\xa9");
        OA_CHECK(cafe != nullptr);
        const CatalogItem* replaced = find_named(volume, "\xef\xbf\xbd");
        OA_CHECK(replaced != nullptr);
        bool thread = false;
        for (const CatalogItem& item : volume.catalog()) {
            if (item.kind == CatalogItem::Kind::file_thread && item.id == 16 && item.parent == 2 &&
                item.name == "span.bin")
                thread = true;
        }
        OA_CHECK(thread);
        std::vector<uint8_t> none;
        OA_CHECK(volume.read_file(cafe->id, 0, none, &error));
        OA_CHECK(!volume.read_file(cafe->id, 0, untouched, &error));
    }

    Volume bare_disk;
    OA_CHECK(bare_disk.open_disk(hooks_for(synthetic), synthetic.size(), &error));

    const std::vector<uint8_t> truncated = truncated_volume();
    Volume broken;
    OA_CHECK(!broken.open(hooks_for(truncated), truncated.size(), &error));
    OA_CHECK(error.find("truncated") != std::string::npos);

    std::vector<uint8_t> apfs(8 * 512, 0);
    std::memcpy(apfs.data() + 512, "EFI PART", 8);
    put_le32(apfs, 512 + 12, 92);
    put_le64(apfs, 512 + 72, 2);
    put_le32(apfs, 512 + 80, 1);
    put_le32(apfs, 512 + 84, 128);
    const uint8_t apfs_guid[] = {
        0xEF,
        0x57,
        0x34,
        0x7C,
        0x00,
        0x00,
        0xAA,
        0x11,
        0xAA,
        0x11,
        0x00,
        0x30,
        0x65,
        0x43,
        0xEC,
        0xAC
    };
    std::memcpy(apfs.data() + 1024, apfs_guid, 16);
    put_le64(apfs, 1024 + 32, 3);
    put_le64(apfs, 1024 + 40, 3);
    Volume apfs_volume;
    OA_CHECK(!apfs_volume.open_disk(hooks_for(apfs), apfs.size(), &error));
    OA_CHECK(error.find("APFS") != std::string::npos);

    std::vector<uint8_t> mapped(1024 + synthetic.size(), 0);
    put_be16(mapped, 0, 0x4552);
    put_be16(mapped, 2, 512);
    put_be16(mapped, 512, 0x504D);
    put_be32(mapped, 512 + 4, 1);
    put_be32(mapped, 512 + 8, 2);
    put_be32(mapped, 512 + 12, static_cast<uint32_t>(synthetic.size() / 512));
    std::memcpy(mapped.data() + 512 + 48, "Apple_HFS", 9);
    std::memcpy(mapped.data() + 1024, synthetic.data(), synthetic.size());
    Volume mapped_volume;
    if (!mapped_volume.open_disk(hooks_for(mapped), mapped.size(), &error)) {
        std::fprintf(stderr, "apm: %s\n", error.c_str());
        OA_CHECK(false);
    } else {
        const CatalogItem* span = find_named(mapped_volume, "span.bin");
        OA_CHECK(span != nullptr);
        std::vector<uint8_t> tail(512);
        OA_CHECK(mapped_volume.read_file(span->id, 8 * 512, tail, &error));
        OA_CHECK(tail[0] == 0x22 && tail[511] == 0x22);
    }

    return oa::test::check_exit_status();
}
