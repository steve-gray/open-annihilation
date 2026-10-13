// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The disk-image content source: the fixture's game folder is chosen, a
// file streams and resumes, and a file of the wrong size is refused.

#include "oa/app/content_copy.hpp"
#include "oa/app/disk_image_source.hpp"
#include "oa/base/sha256.hpp"
#include "oa/test/check.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::string read_text(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    OA_CHECK(file.good());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    const fs::path fixture = OA_UDIF_FIXTURE;
    oa::app::DiskImageOptions options;
    options.require_known_installer = false;
    oa::app::DiskImageContentSource source(fixture, oa::app::copy_check_bundles(), options);
    std::string error;
    if (!source.open(&error)) {
        std::fprintf(stderr, "open: %s\n", error.c_str());
        OA_CHECK(false);
    } else {
        oa::app::SourceDescription described;
        OA_CHECK(
            oa::app::describe_source(source, oa::app::copy_check_bundles(), described, &error)
        );
        OA_CHECK(described.bundles.size() == 2);
        OA_CHECK(described.matched.size() == 3);
        OA_CHECK(described.missing.empty());
        bool saw_rev = false;
        bool saw_zrb = false;
        bool saw_campaign = false;
        for (const oa::app::MatchedContentFile& file : described.matched) {
            if (file.copy_name == "rev31.gp3" && file.bundle_id == "totala")
                saw_rev = true;
            if (file.copy_name == "Data/2.zrb")
                saw_zrb = true;
            if (file.copy_name == "totala4.hpi" && file.bundle_id == "campaign")
                saw_campaign = true;
        }
        OA_CHECK(saw_rev && saw_zrb && saw_campaign);

        std::vector<oa::app::ContentSourceFile> listed;
        OA_CHECK(source.list(listed, &error));
        int rev_names = 0;
        bool notes = false;
        for (const oa::app::ContentSourceFile& file : listed) {
            if (file.name == "rev31.gp3")
                ++rev_names;
            if (file.name == "notes.txt")
                notes = true;
            OA_CHECK(file.name.find("not-the-game") == std::string::npos);
        }
        OA_CHECK(rev_names == 1);
        OA_CHECK(notes);

        const fs::path scratch = fs::temp_directory_path() / "oa-g20-disk";
        std::error_code failure;
        fs::remove_all(scratch, failure);
        fs::create_directories(scratch, failure);
        OA_CHECK(!failure);
        const fs::path dest = scratch / "rev31.gp3";
        oa::app::StreamRequest request;
        request.name = "rev31.gp3";
        request.destination = dest;
        request.chunk_bytes = 4;
        oa::app::StreamResult result;
        OA_CHECK(source.stream_file(request, result, &error));
        OA_CHECK(read_text(dest) == "rev31-fixture\n");
        OA_CHECK(result.bytes == std::string("rev31-fixture\n").size());

        fs::remove(dest, failure);
        {
            std::ofstream partial(dest, std::ios::binary);
            partial << "rev3";
        }
        OA_CHECK(source.stream_file(request, result, &error));
        OA_CHECK(read_text(dest) == "rev31-fixture\n");

        std::atomic<bool> stop{true};
        const fs::path stopped = scratch / "stopped.gp3";
        request.destination = stopped;
        request.stop = &stop;
        OA_CHECK(source.stream_file(request, result, &error));
        OA_CHECK(result.stopped);
        OA_CHECK(!fs::exists(stopped, failure));

        oa::base::sha256::Digest digest{};
        OA_CHECK(source.hash_file("rev31.gp3", digest, &error));
        const auto expect = oa::base::sha256::digest_of(
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>("rev31-fixture\n"), 14)
        );
        OA_CHECK(digest == expect);
        fs::remove_all(scratch, failure);
    }

    const fs::path wrong = fs::temp_directory_path() / "oa-g20-not-an-image";
    {
        std::ofstream file(wrong, std::ios::binary);
        file << "nope";
    }
    oa::app::DiskImageContentSource refused(wrong, oa::app::copy_check_bundles());
    OA_CHECK(!refused.open(&error));
    OA_CHECK(error == "This file is not the disk image for older Macs");

    oa::app::DiskImageContentSource known(fixture, oa::app::copy_check_bundles());
    OA_CHECK(!known.open(&error));
    OA_CHECK(error == "This file is not the disk image for older Macs");

    std::ostringstream captured;
    std::streambuf* previous = std::cout.rdbuf(captured.rdbuf());
    const int status = oa::app::run_installer_check(wrong);
    std::cout.rdbuf(previous);
    OA_CHECK(status == 1);
    OA_CHECK(captured.str().find("installer check: FAILED:") != std::string::npos);
    OA_CHECK(captured.str().find("not the disk image for older Macs") != std::string::npos);
    std::error_code failure;
    fs::remove(wrong, failure);

    return oa::test::check_exit_status();
}
