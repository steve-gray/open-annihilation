// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A map pack installed into a scratch Maps folder: a valid two-map pack, each
// refusal of a pack that does not hold what it lists, a preview that is not a
// PNG or is too large, and an engine this build does not meet; a catalogue
// release of the same registry replaced with no question, keeping .backup; a
// file of another version and another registry, each asking; and a fit check
// that refuses through the hooks, changing nothing.

#include "oa/app/package_install.hpp"
#include "oa/app/package_install/oamap.hpp"
#include "oa/base/sha256.hpp"
#include "oa/data/map_fit/map_fit.hpp"
#include "oa/data/map_pack/manifest.hpp"
#include "oa/formats/png.hpp"
#include "oa/formats/zip.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace install = oa::app::package_install;
namespace oamap = install::oamap;
namespace pack = oa::data::map_pack;
namespace png = oa::formats::png;
namespace zip = oa::formats::zip;
using install::Change;

/// The pack the tests install.
constexpr std::string_view kPackId = "ridge-pack";

/// A scratch folder, deleted when the test ends.
class Scratch {
  public:

    Scratch() : path_(oa::test::make_scratch_directory("oa-package-install-oamap-")) {}

    ~Scratch() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    [[nodiscard]] const fs::path& path() const { return path_; }

  private:

    fs::path path_;
};

/// One file of a package.
struct Piece {
    std::string name{};
    std::vector<uint8_t> bytes{};
};

/// Returns text as bytes.
///
/// @param text the text
/// @return its bytes
std::vector<uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

/// Writes a stored RGB PNG.
///
/// @param width its width
/// @param height its height
/// @return the file; empty when it could not be written
std::vector<uint8_t> png_rgb(uint32_t width, uint32_t height) {
    png::Header header{};
    header.width = width;
    header.height = height;
    header.bit_depth = 8;
    header.color_type = png::ColorType::rgb;
    const std::vector<uint8_t> rows(static_cast<std::size_t>(width) * height * 3);
    png::Image image{};
    image.header = header;
    image.rows = rows;
    std::vector<uint8_t> out;
    OA_CHECK(png::write_stored(image, &out));
    return out;
}

/// A two-map pack's manifest.
///
/// @param version its version
/// @param revision its packaging revision
/// @param engine its engine requirement; empty when it names none
/// @param extra a file the north map lists that the package may omit
/// @return the manifest
pack::Manifest manifest_of(
    std::string_view version, int64_t revision, std::string_view engine, std::string extra = {}
) {
    pack::Manifest manifest;
    manifest.format = pack::format_version;
    manifest.id = std::string(kPackId);
    manifest.name = "Ridge Pack";
    manifest.version = std::string(version);
    manifest.author = {"Ridge", {}};
    manifest.packaging = {revision, "2026-10-11", "Open Annihilation"};
    manifest.requires_base = "ta-3.1c";
    manifest.requires_engine = std::string(engine);
    const auto map = [](std::string stem, std::string title) {
        pack::MapEntry entry;
        entry.stem = std::move(stem);
        entry.title = std::move(title);
        entry.players = 2;
        entry.size = "4x4";
        entry.preview = "previews/" + entry.stem + ".png";
        entry.files = {
            "maps/" + entry.stem + ".ota",
            "maps/" + entry.stem + ".tnt",
            "features/ridge/marker.tdf",
        };
        return entry;
    };
    manifest.maps = {map("north", "North"), map("south", "South")};
    if (!extra.empty())
        manifest.maps[0].files.push_back(std::move(extra));
    return manifest;
}

/// The files a manifest lists, and the manifest itself.
///
/// @param manifest the manifest
/// @param preview the preview both maps name
/// @return the package's files
std::vector<Piece> pieces_of(const pack::Manifest& manifest, std::vector<uint8_t> preview) {
    std::vector<Piece> pieces;
    pieces.push_back({"oamap.yaml", bytes_of(pack::write_manifest(manifest))});
    pieces.push_back({"features/ridge/marker.tdf", bytes_of("[ridge marker]\n{\n}\n")});
    for (const pack::MapEntry& map : manifest.maps) {
        pieces.push_back({"maps/" + map.stem + ".ota", bytes_of("ota\n")});
        pieces.push_back({"maps/" + map.stem + ".tnt", bytes_of("tnt\n")});
        pieces.push_back({map.preview, preview});
    }
    return pieces;
}

/// Writes a stored archive.
///
/// @param file where it goes
/// @param pieces its files
void write_zip(const fs::path& file, const std::vector<Piece>& pieces) {
    std::vector<zip::NewEntry> entries;
    entries.reserve(pieces.size());
    for (const Piece& piece : pieces)
        entries.push_back({piece.name, piece.bytes});
    std::vector<uint8_t> archive;
    zip::ZipError error{};
    OA_CHECK(zip::write_archive(entries, archive, error));
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(
        reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size())
    );
    OA_CHECK(static_cast<bool>(out));
}

/// Returns the SHA-256 of a file.
///
/// @param file the file
/// @return its digest
oa::base::sha256::Digest file_hash(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>{in}, {}};
    return oa::base::sha256::digest_of(bytes);
}

/// A catalogue origin for the pack.
///
/// @param registry the registry
/// @param release the release
/// @param file the package, whose hash is recorded; empty records none yet
/// @return the origin
install::Origin catalogue_origin(std::string registry, int64_t release, const fs::path& file) {
    install::Origin origin{};
    origin.kind = install::OriginKind::catalogue;
    origin.registry = std::move(registry);
    origin.catalogue_id = std::string(kPackId);
    origin.release = release;
    origin.installed = "2000-01-01";
    if (!file.empty())
        origin.sha256 = file_hash(file);
    return origin;
}

/// A file origin with the date the checks record.
///
/// @return the origin
install::Origin file_origin() {
    install::Origin origin{};
    origin.installed = "2000-01-01";
    return origin;
}

/// What an install did.
struct Installed {
    install::InstallPlan plan{};
    install::ChangeResult result{};
    install::Problem problem{};
};

/// Opens a package, plans it and, when asked, puts the change in place.
///
/// @param file the package
/// @param maps the Maps folder
/// @param origin where it came from
/// @param commit the change to make; nothing plans only
/// @return what it did
Installed open_and_plan(
    const fs::path& file,
    const fs::path& maps,
    const install::Origin& origin,
    const install::Change* commit
) {
    Installed done{};
    const install::PackageKind* kind = install::find_kind("oamap");
    OA_CHECK(kind != nullptr);
    if (kind == nullptr)
        return done;
    const auto opened = install::open_package(file);
    if (!opened.package) {
        done.problem = opened.problem;
        return done;
    }
    install::Incoming incoming = opened.package->incoming;
    incoming.origin = origin;
    done.plan = oamap::plan_install(incoming, install::folder_hooks(*kind, maps));
    if (commit == nullptr)
        return done;
    const std::string folder =
        *commit == Change::install && !done.plan.alongside.empty() && done.plan.target.empty()
            ? done.plan.alongside
            : done.plan.target;
    const install::InstalledPackage expected = done.plan.replacing || *commit != Change::install
                                                   ? done.plan.installed
                                                   : install::InstalledPackage{};
    install::Unpacking unpacking;
    if (!unpacking.start(*opened.package, maps, folder, expected, origin, done.problem))
        return done;
    auto step = zip::StreamStep::more;
    while (step == zip::StreamStep::more)
        step = unpacking.step(uint64_t{1} << 20, done.problem);
    if (step != zip::StreamStep::done) {
        unpacking.cancel();
        return done;
    }
    install::ChangeOptions options{};
    options.expected = expected;
    options.first_retry_ms = 0;
    done.result = install::commit_change(*kind, maps, folder, *commit, options);
    install::Discarder discarder;
    discarder.add(done.result.discards);
    discarder.add(unpacking.discards());
    while (discarder.step()) {
    }
    return done;
}

/// Requires a refusal whose sentence holds a phrase, and that Maps is untouched.
///
/// @param scratch the scratch folder
/// @param pieces the package
/// @param phrase the sentence holds this
/// @param refusal why
void expect_refused(
    const Scratch& scratch,
    std::vector<Piece> pieces,
    std::string_view phrase,
    install::Refusal refusal
) {
    const fs::path file = scratch.path() / "refused.oamap";
    const fs::path maps = scratch.path() / "Maps";
    write_zip(file, pieces);
    const auto opened = install::open_package(file);
    OA_CHECK(!opened.package);
    OA_CHECK(opened.problem.refusal == refusal);
    const std::string sentence = oamap::refusal_text(opened.problem);
    OA_CHECK(sentence.find(phrase) != std::string::npos);
    if (sentence.find(phrase) == std::string::npos)
        std::fprintf(stderr, "refusal: %s\n", sentence.c_str());
    OA_CHECK(!fs::exists(maps));
}

/// The fit hook's count, and the failure it answers with once.
struct HookState {
    int calls{};
    oa::data::map_fit::Fit failure{};
};

/// Answers the fit check: the first map fails, and the next fits.
///
/// @param context the hook's state
/// @return the fit
oa::data::map_fit::Fit
fail_first(void* context, const fs::path&, const pack::MapEntry&, std::string_view) {
    auto* state = static_cast<HookState*>(context);
    ++state->calls;
    if (state->calls == 1)
        return state->failure;
    return {};
}

void test_installs(const Scratch& scratch) {
    const fs::path maps = scratch.path() / "install" / "Maps";
    const fs::path packages = scratch.path() / "install";
    const std::vector<uint8_t> preview = png_rgb(1, 1);
    const pack::Manifest first_manifest = manifest_of("1.0", 1, ">= 0.0.1");
    const fs::path first_file = packages / "ridge-pack-1.0.oamap";
    write_zip(first_file, pieces_of(first_manifest, preview));
    const Change install_change = Change::install;
    const install::Origin first_origin = catalogue_origin("ridge", 1, first_file);
    const Installed first = open_and_plan(first_file, maps, first_origin, &install_change);
    OA_CHECK(first.plan.kind == install::PlanKind::install && !first.plan.replacing);
    OA_CHECK(first.result.changed);
    const fs::path folder = maps / std::string(kPackId);
    OA_CHECK(fs::exists(folder / "oamap.yaml"));
    OA_CHECK(fs::exists(folder / "maps" / "north.ota"));
    OA_CHECK(fs::exists(folder / "maps" / "south.ota"));
    OA_CHECK(!fs::exists(folder / ".backup"));
    {
        const auto record = install::read_origin(folder);
        OA_CHECK(
            record && record->kind == install::OriginKind::catalogue &&
            record->registry == "ridge" && record->sha256 == file_hash(first_file)
        );
    }

    const pack::Manifest second_manifest = manifest_of("1.1", 1, ">= 0.0.1");
    const fs::path second_file = packages / "ridge-pack-1.1.oamap";
    write_zip(second_file, pieces_of(second_manifest, preview));
    const Change replace_change = Change::replace;
    const Installed replaced = open_and_plan(
        second_file, maps, catalogue_origin("ridge", 2, second_file), &replace_change
    );
    OA_CHECK(replaced.plan.kind == install::PlanKind::install && replaced.plan.replacing);
    OA_CHECK(replaced.result.changed && replaced.result.backup_kept);
    OA_CHECK(fs::exists(folder / ".backup" / "oamap.yaml"));
    {
        const auto kept = install::read_origin(folder / ".backup");
        OA_CHECK(
            kept && kept->kind == install::OriginKind::catalogue && kept->registry == "ridge" &&
            kept->sha256 == file_hash(first_file)
        );
    }
    const auto held = oamap::read_installed(folder);
    OA_CHECK(held.kind == install::FolderKind::package && held.version == "1.1");

    const pack::Manifest third_manifest = manifest_of("2.0", 1, {});
    const fs::path third_file = packages / "ridge-pack-2.0.oamap";
    write_zip(third_file, pieces_of(third_manifest, preview));
    const Installed asked = open_and_plan(third_file, maps, file_origin(), nullptr);
    OA_CHECK(asked.plan.kind == install::PlanKind::ask_version);
    const install::PackagePrompt version_prompt = oamap::question_prompt(
        asked.plan,
        [&] {
            install::Incoming incoming{};
            incoming.name = "Ridge Pack";
            incoming.version = "2.0";
            incoming.revision = 1;
            incoming.origin = file_origin();
            return incoming;
        }(),
        "ridge-pack-2.0.oamap",
        maps,
        false
    );
    OA_CHECK(!version_prompt.prompt.paragraphs.empty());
    OA_CHECK(
        version_prompt.prompt.paragraphs[0].text.find("Replace it with 2.0") != std::string::npos
    );
    OA_CHECK(held.version == "1.1");

    const Installed other =
        open_and_plan(third_file, maps, catalogue_origin("harbour", 1, third_file), nullptr);
    OA_CHECK(other.plan.kind == install::PlanKind::ask_version);
    install::Incoming harbour{};
    harbour.id = std::string(kPackId);
    harbour.name = "Ridge Pack";
    harbour.version = "2.0";
    harbour.origin = catalogue_origin("harbour", 1, {});
    const install::PackagePrompt registry_prompt =
        oamap::question_prompt(other.plan, harbour, "ridge-pack-2.0.oamap", maps, false);
    OA_CHECK(!registry_prompt.prompt.paragraphs.empty());
    OA_CHECK(
        registry_prompt.prompt.paragraphs[0].text.find("from ridge") != std::string::npos &&
        registry_prompt.prompt.paragraphs[0].text.find("from harbour") != std::string::npos
    );
    OA_CHECK(oamap::read_installed(folder).version == "1.1");
}

void test_refusals(const Scratch& scratch) {
    const std::vector<uint8_t> preview = png_rgb(1, 1);
    const pack::Manifest good = manifest_of("1.0", 1, {});

    auto missing = pieces_of(manifest_of("1.0", 1, {}, "features/ridge/absent.tdf"), preview);
    missing.erase(
        std::remove_if(
            missing.begin(),
            missing.end(),
            [](const Piece& piece) { return piece.name == "features/ridge/absent.tdf"; }
        ),
        missing.end()
    );
    expect_refused(
        scratch,
        missing,
        "The pack lists features/ridge/absent.tdf, which it does not hold.",
        install::Refusal::missing_entry
    );

    auto stray = pieces_of(good, preview);
    stray.push_back({"features/unused.tdf", bytes_of("unused\n")});
    expect_refused(
        scratch,
        stray,
        "The pack holds features/unused.tdf, which no map uses.",
        install::Refusal::stray_file
    );

    auto outside = pieces_of(good, preview);
    outside.push_back({"sounds/x.wav", bytes_of("wav\n")});
    expect_refused(
        scratch,
        outside,
        "The pack holds sounds/x.wav, which no map uses",
        install::Refusal::outside_folder
    );

    auto not_png = pieces_of(good, bytes_of("not a png"));
    expect_refused(
        scratch,
        not_png,
        "The preview previews/north.png is not a PNG.",
        install::Refusal::preview_unreadable
    );

    auto wide = pieces_of(good, png_rgb(1025, 1));
    expect_refused(
        scratch,
        wide,
        "The preview previews/north.png is larger than 1024 pixels on a side.",
        install::Refusal::preview_too_big
    );

    auto unmet = pieces_of(manifest_of("1.0", 1, ">= 9999.0.0"), preview);
    expect_refused(
        scratch,
        unmet,
        "needs Open Annihilation 9999.0.0 or later; this is Open Annihilation 0.8.0",
        install::Refusal::engine_unmet
    );
}

void test_fit_hook(const Scratch& scratch) {
    const fs::path maps = scratch.path() / "fit" / "Maps";
    const fs::path file = scratch.path() / "fit" / "ridge-pack.oamap";
    const std::vector<uint8_t> preview = png_rgb(1, 1);
    write_zip(file, pieces_of(manifest_of("1.0", 1, {}), preview));
    const auto opened = install::open_package(file);
    OA_CHECK(opened.package.has_value());
    if (!opened.package)
        return;
    const install::PackageKind* kind = opened.package->kind;
    OA_CHECK(kind != nullptr && kind->check_staged != nullptr);
    if (kind == nullptr || kind->check_staged == nullptr)
        return;
    install::Problem problem{};
    install::Unpacking unpacking;
    const install::InstalledPackage expected{};
    OA_CHECK(unpacking.start(*opened.package, maps, kPackId, expected, file_origin(), problem));
    auto step = zip::StreamStep::more;
    while (step == zip::StreamStep::more)
        step = unpacking.step(uint64_t{1} << 20, problem);
    OA_CHECK(step == zip::StreamStep::done);
    install::PackageOptions skipped{};
    OA_CHECK(kind->check_staged(unpacking.staging(), *opened.package, skipped, problem));

    oa::data::map_fit::Failure failure{};
    failure.rule = oa::data::map_fit::Rule::new_names;
    failure.subject = "DragonsTeeth";
    failure.detail = "features/corpses/walls.tdf";
    HookState state{};
    state.failure.failures.push_back(failure);
    oamap::FitHooks hooks{};
    hooks.context = &state;
    hooks.check_base_fit = &fail_first;
    install::PackageOptions options{};
    options.context = &hooks;
    install::Problem unfit{};
    OA_CHECK(!kind->check_staged(unpacking.staging(), *opened.package, options, unfit));
    OA_CHECK(state.calls == 2);
    OA_CHECK(unfit.refusal == install::Refusal::unfit);
    const std::string sentence = oa::data::map_fit::describe(failure);
    OA_CHECK(oamap::refusal_text(unfit).find(sentence) != std::string::npos);
    unpacking.cancel();
    install::Discarder discarder;
    discarder.add(unpacking.discards());
    while (discarder.step()) {
    }
    OA_CHECK(!fs::exists(maps / std::string(kPackId)));
}

} // namespace

int main() {
    const Scratch scratch;
    test_installs(scratch);
    test_refusals(scratch);
    test_fit_hook(scratch);
    return oa::test::check_exit_status();
}
