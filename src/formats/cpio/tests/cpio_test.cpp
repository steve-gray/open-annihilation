// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Small odc and newc archives built in the test, including padding and the
// newc checksum.

#include "oa/formats/cpio.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

using oa::formats::cpio::File;
using oa::formats::cpio::Reader;
using oa::formats::cpio::Visit;
using oa::formats::cpio::name_is_safe;

void put_octal(std::vector<uint8_t>& out, uint64_t value, int width) {
    for (int shift = (width - 1) * 3; shift >= 0; shift -= 3)
        out.push_back(static_cast<uint8_t>('0' + ((value >> shift) & 7u)));
}

void put_hex(std::vector<uint8_t>& out, uint64_t value, int width) {
    for (int shift = (width - 1) * 4; shift >= 0; shift -= 4) {
        const unsigned digit = static_cast<unsigned>((value >> shift) & 0xfu);
        out.push_back(static_cast<uint8_t>(digit < 10 ? '0' + digit : 'a' + digit - 10));
    }
}

void put_text(std::vector<uint8_t>& out, std::string_view text) {
    out.insert(out.end(), text.begin(), text.end());
}

void odc_file(
    std::vector<uint8_t>& out,
    uint32_t mode,
    std::string_view name,
    std::string_view data,
    uint64_t modified,
    uint64_t device = 0,
    uint64_t inode = 0
) {
    put_text(out, "070707");
    put_octal(out, device, 6);
    put_octal(out, inode, 6);
    put_octal(out, mode, 6);
    put_octal(out, 0, 6);
    put_octal(out, 0, 6);
    put_octal(out, 1, 6);
    put_octal(out, 0, 6);
    put_octal(out, modified, 11);
    put_octal(out, name.size() + 1, 6);
    put_octal(out, data.size(), 11);
    put_text(out, name);
    out.push_back(0);
    put_text(out, data);
}

void newc_file(
    std::vector<uint8_t>& out,
    const char* magic,
    uint32_t mode,
    std::string_view name,
    std::string_view data,
    uint32_t checksum,
    uint64_t inode = 0,
    uint64_t major = 0,
    uint64_t minor = 0
) {
    put_text(out, magic);
    put_hex(out, inode, 8);
    put_hex(out, mode, 8);
    put_hex(out, 0, 8);
    put_hex(out, 0, 8);
    put_hex(out, 1, 8);
    put_hex(out, 1700000000, 8);
    put_hex(out, data.size(), 8);
    put_hex(out, major, 8);
    put_hex(out, minor, 8);
    put_hex(out, 0, 8);
    put_hex(out, 0, 8);
    put_hex(out, name.size() + 1, 8);
    put_hex(out, checksum, 8);
    put_text(out, name);
    out.push_back(0);
    while (out.size() % 4 != 0)
        out.push_back(0xab);
    put_text(out, data);
    while (out.size() % 4 != 0)
        out.push_back(0xcd);
}

struct Collected {
    std::vector<std::string> names{};
    std::vector<std::string> data{};
    std::vector<uint64_t> devices{};
    std::vector<uint64_t> inodes{};
    std::string current{};
    bool taking{};
};

bool on_begin(void* context, const File& file, bool& take) {
    auto* collected = static_cast<Collected*>(context);
    collected->names.push_back(file.name);
    collected->devices.push_back(file.device);
    collected->inodes.push_back(file.inode);
    take = file.regular && !file.trailer;
    collected->taking = take;
    collected->current.clear();
    return true;
}

bool on_data(void* context, std::span<const uint8_t> bytes) {
    auto* collected = static_cast<Collected*>(context);
    collected->current.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

bool on_end(void* context) {
    auto* collected = static_cast<Collected*>(context);
    if (collected->taking)
        collected->data.push_back(collected->current);
    return true;
}

[[nodiscard]] bool
read_all(const std::vector<uint8_t>& bytes, Collected& collected, std::string& error) {
    Reader reader;
    Visit visit;
    visit.context = &collected;
    visit.begin = on_begin;
    visit.data = on_data;
    visit.end = on_end;
    std::size_t at = 0;
    while (at < bytes.size()) {
        const std::size_t step = std::min<std::size_t>(7, bytes.size() - at);
        if (!reader.feed(std::span<const uint8_t>(bytes).subspan(at, step), visit, &error))
            return false;
        at += step;
    }
    return reader.finish(&error);
}

void odc_round_trip() {
    std::vector<uint8_t> archive;
    odc_file(archive, 0100644, "rev31.gp3", "rev31-fixture\n", 1600000000);
    odc_file(archive, 0100644, "Data/2.zrb", "zrb-fixture\n", 1600000000);
    odc_file(archive, 0100644, "TRAILER!!!", "", 0);
    Collected collected;
    std::string error;
    OA_CHECK(read_all(archive, collected, error));
    OA_CHECK(error.empty());
    OA_CHECK(collected.names.size() == 2);
    OA_CHECK(collected.data.size() == 2);
    OA_CHECK(collected.names[0] == "rev31.gp3");
    OA_CHECK(collected.data[0] == "rev31-fixture\n");
    OA_CHECK(collected.data[1] == "zrb-fixture\n");
}

void newc_padding_and_checksum() {
    std::vector<uint8_t> archive;
    const std::string data = "XY";
    uint32_t sum = 0;
    for (unsigned char byte : data)
        sum += byte;
    newc_file(archive, "070701", 0100644, "ab", data, 0);
    newc_file(archive, "070701", 0100644, "TRAILER!!!", "", 0);
    Collected collected;
    std::string error;
    OA_CHECK(read_all(archive, collected, error));
    OA_CHECK(collected.data.size() == 1 && collected.data[0] == "XY");

    std::vector<uint8_t> checked;
    newc_file(checked, "070702", 0100644, "ab", data, sum);
    newc_file(checked, "070702", 0100644, "TRAILER!!!", "", 0);
    Collected good;
    OA_CHECK(read_all(checked, good, error));
    OA_CHECK(good.data.size() == 1 && good.data[0] == "XY");

    std::vector<uint8_t> bad;
    newc_file(bad, "070702", 0100644, "ab", data, sum + 1);
    newc_file(bad, "070702", 0100644, "TRAILER!!!", "", 0);
    Collected rejected;
    std::string checksum_error;
    OA_CHECK(!read_all(bad, rejected, checksum_error));
    OA_CHECK(checksum_error.find("checksum") != std::string::npos);
}

void device_and_inode() {
    std::vector<uint8_t> odc;
    odc_file(odc, 0100644, "rev31.gp3", "abc", 1, 7, 9);
    odc_file(odc, 0100644, "TRAILER!!!", "", 0);
    Collected odc_files;
    std::string error;
    OA_CHECK(read_all(odc, odc_files, error));
    OA_CHECK(odc_files.devices.size() == 1 && odc_files.devices[0] == 7);
    OA_CHECK(odc_files.inodes.size() == 1 && odc_files.inodes[0] == 9);

    std::vector<uint8_t> neu;
    newc_file(neu, "070701", 0100644, "ab", "XY", 0, 0x11, 0x22, 0x33);
    newc_file(neu, "070701", 0100644, "TRAILER!!!", "", 0);
    Collected neu_files;
    OA_CHECK(read_all(neu, neu_files, error));
    OA_CHECK(neu_files.inodes.size() == 1 && neu_files.inodes[0] == 0x11);
    const uint64_t device = (uint64_t{0x22} << 32) | 0x33;
    OA_CHECK(neu_files.devices.size() == 1 && neu_files.devices[0] == device);
}

void truncated_and_names() {
    std::vector<uint8_t> archive;
    odc_file(archive, 0100644, "only", "abc", 1);
    archive.resize(archive.size() / 2);
    Collected collected;
    std::string error;
    OA_CHECK(!read_all(archive, collected, error));
    OA_CHECK(error.find("early") != std::string::npos || error.find("broken") != std::string::npos);
    OA_CHECK(name_is_safe("./payload/file.txt"));
    OA_CHECK(name_is_safe("."));
    OA_CHECK(name_is_safe("./"));
    OA_CHECK(!name_is_safe(""));
    OA_CHECK(!name_is_safe("/tmp/x"));
    OA_CHECK(!name_is_safe("a/../b"));
    OA_CHECK(!name_is_safe("a\\b"));
    OA_CHECK(!name_is_safe("a//b"));
}

} // namespace

int main() {
    odc_round_trip();
    newc_padding_and_checksum();
    device_and_inode();
    truncated_and_names();
    return oa::test::check_exit_status();
}
