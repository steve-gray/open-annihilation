// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The composed mount of a chosen base game and its expansions: the same
// archives, in the same order, as a scan of one folder that holds those
// files. Battle Tactics off keeps totala4.hpi inside the first ten plain
// archives. The base game alone mounts its own archives. Music and movies
// are found in the bundle folders, and a name's case does not matter.
#include "oa/app/game_directory.hpp"
#include "oa/audio/music_disc.hpp"
#include "oa/data/content_bundles.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"
#include "oa/ui/engine_settings.hpp"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace oa::app;
namespace bundles = oa::data::content_bundles;

constexpr std::string_view kEveryExpansion =
    "totala:campaign,core-contingency,battle-tactics,music,cavedog-units,cavedog-tilesets";

/// Compares two strings without regard to ASCII case.
///
/// @param left one string
/// @param right the other
/// @return true when they match
[[nodiscard]] bool same_ascii(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index])))
            return false;
    return true;
}

/// Reports whether a copy name is an archive discovery would mount.
///
/// @param name the copy name
/// @return true for an archive suffix
[[nodiscard]] bool archive_name(std::string_view name) {
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos)
        return false;
    const std::string_view extension = name.substr(dot);
    return same_ascii(extension, ".hpi") || same_ascii(extension, ".ufo") ||
           same_ascii(extension, ".ccx") || same_ascii(extension, ".gp3");
}

/// The bytes of one tiny archive.
///
/// @return a mountable archive
[[nodiscard]] std::vector<uint8_t> archive_bytes() {
    const std::vector<uint8_t> text{'o', 'k'};
    const oa::HpiWriteFile file{"readme.txt", text};
    return oa::write_hpi(std::span<const oa::HpiWriteFile>(&file, 1));
}

/// Writes bytes to a file, creating its folder.
///
/// @param path the file
/// @param bytes its contents
void write_bytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
    OA_CHECK(static_cast<bool>(out));
}

/// The folder one bundle is stored in under a player's folder.
///
/// @param user the player's folder
/// @param bundle the bundle
/// @return games/<id> or expansions/<base>/<id>
[[nodiscard]] fs::path managed_folder(const fs::path& user, const bundles::Bundle& bundle) {
    const fs::path id{std::string(bundle.id)};
    if (bundle.kind == bundles::Kind::base_game)
        return user / "games" / id;
    return user / "expansions" / std::string(bundle.extends[0]) / id;
}

/// Writes a record that lists every file of a bundle.
///
/// @param folder the bundle folder
/// @param bundle the bundle
void write_record(const fs::path& folder, const bundles::Bundle& bundle) {
    fs::create_directories(folder);
    std::ofstream out(folder / ".oa-content");
    const std::string hash(64, '0');
    out << "oa-content 1\n";
    out << "bundle " << bundle.id << '\n';
    out << "source G05\n";
    for (const bundles::ContentFile& file : bundle.files) {
        out << "file " << file.copy_name << '\n';
        out << "size 1\n";
        out << "modified 1\n";
        out << "sha256 " << hash << '\n';
    }
    OA_CHECK(static_cast<bool>(out));
}

/// Writes one bundle's files under `root`, under each file's copy name.
///
/// @param root the folder
/// @param bundle the bundle
/// @param archive the bytes of each archive
void place_bundle(
    const fs::path& root, const bundles::Bundle& bundle, const std::vector<uint8_t>& archive
) {
    for (const bundles::ContentFile& file : bundle.files) {
        if (archive_name(file.copy_name))
            write_bytes(root / path_from_utf8(file.copy_name), archive);
        else
            write_bytes(root / path_from_utf8(file.copy_name), std::vector<uint8_t>{'x'});
    }
}

/// The file names discovery mounted, in mount order.
///
/// @param archives the mounted archives
/// @return each archive's file name
[[nodiscard]] std::vector<std::string> file_names(const std::vector<fs::path>& archives) {
    std::vector<std::string> names;
    names.reserve(archives.size());
    for (const fs::path& archive : archives)
        names.push_back(path_to_utf8(archive.filename()));
    return names;
}

/// The file names a scan of one folder mounts.
///
/// @param folder the folder
/// @return the mounted file names, in mount order
[[nodiscard]] std::vector<std::string> scanned_names(const fs::path& folder) {
    oa::AssetStore store(folder);
    for (const auto& outcome : store.discover(discovery_plan_of(nullptr)))
        std::ignore = outcome.mounted;
    std::vector<std::string> names;
    const auto mounted = store.mount_paths();
    names.reserve(mounted.size());
    for (const fs::path& archive : mounted)
        names.push_back(path_to_utf8(archive.filename()));
    return names;
}

/// The plain archives among the mounted names, in mount order.
///
/// @param names the mounted file names
/// @return those whose suffix is .hpi
[[nodiscard]] std::vector<std::string> hpi_names(const std::vector<std::string>& names) {
    std::vector<std::string> hpis;
    for (const std::string& name : names)
        if (name.size() >= 4 && same_ascii(std::string_view(name).substr(name.size() - 4), ".hpi"))
            hpis.push_back(name);
    return hpis;
}

/// Mounts `content` from `user`.
///
/// @param user the player's folder
/// @param content `--content`; empty uses `values`
/// @param values the preferences
/// @param mod_dir a mod folder; empty for none
/// @return the composed mount
[[nodiscard]] std::optional<GameDirectory> mount_of(
    const fs::path& user,
    std::string_view content,
    const oa::platform::preferences::Values& values,
    const fs::path& mod_dir = {}
) {
    ManagedMount request{};
    request.user_folder = user;
    request.content = content;
    request.mod_dir = mod_dir;
    return compose_managed_content(request, values);
}

/// True when `names` holds `wanted`.
///
/// @param names the names
/// @param wanted the name
/// @return true when it is there
[[nodiscard]] bool contains(const std::vector<std::string>& names, std::string_view wanted) {
    for (const std::string& name : names)
        if (name == wanted)
            return true;
    return false;
}

void check_composed_mount(const fs::path& scratch) {
    const std::vector<uint8_t> archive = archive_bytes();
    const fs::path user = scratch / "player";
    const fs::path install = scratch / "install";
    std::vector<std::string> recorded;
    for (const bundles::Bundle& bundle : bundles::bundles()) {
        if (bundle.id == "totala-demo")
            continue;
        place_bundle(install, bundle, archive);
        const fs::path folder = managed_folder(user, bundle);
        place_bundle(folder, bundle, archive);
        write_record(folder, bundle);
        recorded.emplace_back(bundle.id);
    }

    const std::vector<std::string> scanned = scanned_names(install);
    const GameInstall inspected = inspect_game_install(install);
    OA_CHECK(file_names(inspected.archives) == scanned);

    const oa::platform::preferences::Values values;
    const std::optional<GameDirectory> every = mount_of(user, kEveryExpansion, values);
    OA_CHECK(every.has_value());
    if (!every)
        return;
    OA_CHECK(every->source == GameDirectorySource::managed);
    OA_CHECK(!every->mod_layered);
    OA_CHECK(file_names(every->archives) == scanned);
    OA_CHECK(every->folders.size() == 7);
    OA_CHECK(path_to_utf8(every->folders.back().filename()) == "totala");

    const std::vector<std::string> hpis = hpi_names(file_names(every->archives));
    const std::vector<std::string> first_ten{
        "tactics1.hpi",
        "tactics2.hpi",
        "tactics3.hpi",
        "tactics4.hpi",
        "tactics5.hpi",
        "tactics6.hpi",
        "tactics7.hpi",
        "tactics8.hpi",
        "totala1.hpi",
        "totala2.hpi",
    };
    OA_CHECK(hpis.size() == first_ten.size() + 2);
    OA_CHECK(
        hpis.size() >= first_ten.size() &&
        std::vector<std::string>(
            hpis.begin(), hpis.begin() + static_cast<std::ptrdiff_t>(first_ten.size())
        ) == first_ten
    );
    OA_CHECK(contains(hpis, "totala4.hpi") && contains(hpis, "worlds.hpi"));
    OA_CHECK(!hpis.empty() && hpis.back() == "worlds.hpi");

    const std::optional<GameDirectory> without_tactics = mount_of(
        user, "totala:campaign,core-contingency,music,cavedog-units,cavedog-tilesets", values
    );
    OA_CHECK(without_tactics.has_value());
    if (without_tactics) {
        const std::vector<std::string> plain = hpi_names(file_names(without_tactics->archives));
        std::size_t totala4 = plain.size();
        for (std::size_t index = 0; index < plain.size(); ++index)
            if (plain[index] == "totala4.hpi")
                totala4 = index;
        OA_CHECK(totala4 < 10);
        OA_CHECK(!contains(plain, "tactics1.hpi"));
    }

    const std::optional<GameDirectory> base = mount_of(user, "totala", values);
    OA_CHECK(base.has_value());
    if (base) {
        OA_CHECK(
            file_names(base->archives) ==
            std::vector<std::string>({"rev31.gp3", "totala1.hpi", "totala2.hpi"})
        );
        OA_CHECK(roots_hold_movies(base->folders));
    }
    OA_CHECK(roots_hold_movies(every->folders));

    bool music = false;
    for (const fs::path& root : every->folders) {
        const fs::path directory = oa::audio::music_disc_directory(root);
        if (oa::audio::music_disc_present(oa::audio::music_disc_scan(directory)))
            music = true;
    }
    OA_CHECK(music);

    const fs::path mod = scratch / "ridge";
    write_bytes(mod / "rev31.gp3", archive_bytes());
    const std::optional<GameDirectory> layered = mount_of(user, "totala", values, mod);
    OA_CHECK(layered.has_value());
    if (layered) {
        bool replaced = false;
        for (const fs::path& archive : layered->archives)
            if (path_to_utf8(archive.filename()) == "rev31.gp3") {
                std::error_code compare_failure;
                replaced = fs::equivalent(archive.parent_path(), mod, compare_failure);
            }
        OA_CHECK(replaced);
        OA_CHECK(layered->mod_layered);
        OA_CHECK(!layered->folders.empty() && layered->folders.front() == mod);
    }

    oa::platform::preferences::Values stored;
    stored[std::string(oa::ui::engine_settings::key::base_game)] = "totala";
    stored[std::string(oa::ui::engine_settings::key::expansions_prefix) + "totala"] =
        "campaign,battle-tactics";
    const std::optional<GameDirectory> tactics_on = mount_of(user, {}, stored);
    OA_CHECK(tactics_on.has_value());
    if (tactics_on)
        OA_CHECK(contains(file_names(tactics_on->archives), "tactics1.hpi"));
    stored[std::string(oa::ui::engine_settings::key::expansions_prefix) + "totala"] = "campaign";
    const std::optional<GameDirectory> tactics_off = mount_of(user, {}, stored);
    OA_CHECK(tactics_off.has_value());
    if (tactics_off) {
        const std::vector<std::string> plain = hpi_names(file_names(tactics_off->archives));
        OA_CHECK(!contains(plain, "tactics1.hpi"));
        OA_CHECK(contains(plain, "totala4.hpi"));
        OA_CHECK(plain.size() <= 10);
    }

    OA_CHECK(copied_content_bundles(user) == recorded);
    OA_CHECK(copied_content_bundles(user / "games" / "totala") == recorded);

    const fs::path plain = scratch / "plain";
    fs::create_directory(plain);
    std::ofstream(plain / "totala1.hpi") << "short";
    OA_CHECK(copied_content_bundles(plain).empty());

    const fs::path partial = scratch / "partial";
    const fs::path partial_game = partial / "games" / "totala";
    fs::create_directories(partial_game);
    {
        std::ofstream out(partial_game / ".oa-content");
        out << "oa-content 1\n";
        out << "bundle totala\n";
        out << "source G05\n";
        out << "file rev31.gp3\n";
        out << "size 1\n";
        out << "modified 1\n";
        out << "sha256 " << std::string(64, '0') << '\n';
        OA_CHECK(static_cast<bool>(out));
    }
    OA_CHECK(copied_content_bundles(partial).empty());
}

void check_case(const fs::path& scratch) {
    const bundles::Bundle* totala = bundles::find_bundle("totala");
    OA_CHECK(totala != nullptr);
    if (totala == nullptr)
        return;
    const fs::path user = scratch / "case-player";
    const fs::path folder = managed_folder(user, *totala);
    write_record(folder, *totala);
    write_bytes(folder / "TOTALA1.HPI", archive_bytes());
    write_bytes(folder / "DATA" / "1.ZRB", std::vector<uint8_t>{'m'});
    write_bytes(folder / "Music" / "2.mp3", std::vector<uint8_t>{'s'});

    const oa::platform::preferences::Values values;
    const std::optional<GameDirectory> mounted = mount_of(user, "totala", values);
    OA_CHECK(mounted.has_value());
    if (!mounted)
        return;
    OA_CHECK(contains(file_names(mounted->archives), "TOTALA1.HPI"));
    OA_CHECK(roots_hold_movies(mounted->folders));
    const fs::path music = oa::audio::music_disc_directory(mounted->installation);
    OA_CHECK(oa::audio::music_disc_present(oa::audio::music_disc_scan(music)));
    OA_CHECK(copied_content_bundles(user) == std::vector<std::string>{"totala"});
}

} // namespace

int main() {
    const fs::path scratch = oa::test::make_scratch_directory("G05-content-mount");
    try {
        check_composed_mount(scratch);
        check_case(scratch);
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "content mount: %s\n", failure.what());
        ++oa::test::failed_checks();
    }
    std::error_code failure;
    fs::remove_all(scratch, failure);
    if (oa::test::failed_checks() == 0)
        std::puts("content mount checks passed");
    return oa::test::check_exit_status();
}
