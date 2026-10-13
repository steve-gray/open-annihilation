// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Packing and checking an expansion. The test table accepts the fixture
// folder and the package it becomes. The commands the game runs pass no
// table, so the same folder and the same package are refused, and a
// --game-dir on either command is a usage error.

#include "expansion.hpp"

#include "oa/data/content_bundles/expansion_fixture.hpp"

#include "command.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace fixture = oa::test::expansion_fixture;

struct Scratch {
    fs::path path;

    Scratch() : path(oa::test::make_scratch_directory("oa-tool-expansion")) {}

    ~Scratch() {
        std::error_code error;
        fs::remove_all(path, error);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
};

void write_text(const fs::path& file, std::string_view text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

struct Captured {
    int status = 0;
    std::string out;
    std::string err;
};

Captured run_pack(const std::vector<std::string>& arguments) {
    std::ostringstream out;
    std::ostringstream err;
    oa::tool::Output output{out, err};
    Captured captured;
    captured.status = oa::tool::run_pack(arguments, output);
    captured.out = out.str();
    captured.err = err.str();
    return captured;
}

Captured run_check(const std::vector<std::string>& arguments) {
    std::ostringstream out;
    std::ostringstream err;
    oa::tool::Output output{out, err};
    Captured captured;
    captured.status = oa::tool::run_check(arguments, output);
    captured.out = out.str();
    captured.err = err.str();
    return captured;
}

void test_pack_and_check() {
    Scratch scratch;
    const fs::path folder = scratch.path / "folder";
    write_text(folder / "oaexp.yaml", fixture::manifest_text());
    write_text(folder / "unit.fbi", "abcd");
    const fs::path packed = scratch.path / "cavedog-units.oaexp";

    std::ostringstream packed_out;
    std::ostringstream packed_err;
    oa::tool::Output output{packed_out, packed_err};
    const int packed_status =
        oa::tool::pack_expansion(folder, &fixture::test_bundle(), packed.string(), false, output);
    OA_CHECK(packed_status == oa::tool::exit_done);
    OA_CHECK(packed_out.str().find("packed ") != std::string::npos);
    OA_CHECK(fs::exists(packed));

    std::ostringstream checked_out;
    std::ostringstream checked_err;
    oa::tool::Output check_output{checked_out, checked_err};
    const int checked =
        oa::tool::check_expansion_package(packed, &fixture::test_bundle(), check_output);
    OA_CHECK(checked == oa::tool::exit_done);
    OA_CHECK(checked_out.str().find("result: ok") != std::string::npos);

    const fs::path refused = scratch.path / "refused.oaexp";
    const Captured builtin_pack = run_pack({"--out", refused.string(), folder.string()});
    OA_CHECK(builtin_pack.status == oa::tool::exit_failed);
    OA_CHECK(!fs::exists(refused));

    const Captured builtin_check = run_check({packed.string()});
    OA_CHECK(builtin_check.status == oa::tool::exit_failed);

    const Captured pack_usage = run_pack({"--game-dir", "somewhere", folder.string()});
    OA_CHECK(pack_usage.status == oa::tool::exit_usage);

    const Captured check_usage = run_check({"--game-dir", "somewhere", packed.string()});
    OA_CHECK(check_usage.status == oa::tool::exit_usage);
}

} // namespace

int main() {
    test_pack_and_check();
    return oa::test::check_exit_status();
}
