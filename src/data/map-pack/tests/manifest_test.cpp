// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Map-pack manifests: the standard's example read as a packed index, a
// source that names only a stem, a manifest written and read back, each
// rule refused with its key and line, and map names split and fitted.

#include "oa/data/map_pack/manifest.hpp"
#include "oa/data/map_pack/map_name.hpp"
#include "oa/test/check.hpp"

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace pack = oa::data::map_pack;

using pack::Manifest;
using pack::ManifestUse;
using pack::MapEntry;
using pack::Problem;

/// Views a text's bytes.
///
/// @param text the text
/// @return its bytes
std::span<const uint8_t> as_bytes(std::string_view text) {
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

/// The 1-based line of the first line that holds a marker.
///
/// @param text the text
/// @param marker the marker
/// @return the line, or 0 when the marker is absent
uint32_t line_of(std::string_view text, std::string_view marker) {
    uint32_t line = 1;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t end = text.find('\n', at);
        const std::size_t stop = end == std::string_view::npos ? text.size() : end;
        if (text.substr(at, stop - at).find(marker) != std::string_view::npos)
            return line;
        if (end == std::string_view::npos)
            break;
        at = end + 1;
        ++line;
    }
    return 0;
}

/// Replaces every occurrence of a piece of text.
///
/// @param text the text
/// @param from the piece to replace
/// @param to the replacement
/// @return the text, with every piece replaced
std::string replace_all(std::string text, std::string_view from, std::string_view to) {
    std::size_t at = 0;
    while ((at = text.find(from, at)) != std::string::npos) {
        text.replace(at, from.size(), to);
        at += to.size();
    }
    return text;
}

/// Replaces the first occurrence of a piece of text.
///
/// @param text the text
/// @param from the piece to replace
/// @param to the replacement
/// @return the text, with that piece replaced
std::string replaced(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    OA_CHECK(at != std::string::npos);
    if (at == std::string::npos)
        return text;
    text.replace(at, from.size(), to);
    return text;
}

/// Tells whether a problem with a key was reported on a line.
///
/// @param problems the problems
/// @param key the key
/// @param line the line
/// @return true when one problem has that key and line
bool problem_at(const std::vector<Problem>& problems, std::string_view key, uint32_t line) {
    for (const Problem& problem : problems)
        if (problem.key == key && problem.line == line)
            return true;
    return false;
}

/// Prints every problem, so a failed check shows what was reported.
///
/// @param problems the problems
void show_problems(const std::vector<Problem>& problems) {
    for (const Problem& problem : problems)
        std::fprintf(stderr, "  %s\n", pack::describe(problem).c_str());
}

/// Reads a manifest and checks that one rule was refused at a marker's line.
///
/// The manifest handed in is left as it was.
///
/// @param yaml the manifest
/// @param use how to read it
/// @param key the problem's key
/// @param marker a piece of text on the problem's line
void expect_refused(
    std::string_view yaml, ManifestUse use, std::string_view key, std::string_view marker
) {
    Manifest manifest;
    manifest.id = "sentinel";
    MapEntry kept;
    kept.stem = "sentinel";
    manifest.maps.push_back(kept);
    std::vector<Problem> problems;
    const bool read = pack::read_manifest(as_bytes(yaml), use, manifest, problems);
    const uint32_t line = line_of(yaml, marker);
    const bool found = problem_at(problems, key, line);
    if (read || manifest.id != "sentinel" || manifest.maps.size() != 1 ||
        manifest.maps[0].stem != "sentinel" || !found) {
        std::fprintf(
            stderr,
            "expected %.*s at line %u (read %d, id %s)\n",
            static_cast<int>(key.size()),
            key.data(),
            line,
            read ? 1 : 0,
            manifest.id.c_str()
        );
        show_problems(problems);
    }
    OA_CHECK(!read);
    OA_CHECK(manifest.id == "sentinel");
    OA_CHECK(manifest.maps.size() == 1);
    OA_CHECK(manifest.maps[0].stem == "sentinel");
    OA_CHECK(found);
}

/// A packed manifest that passes every rule.
///
/// @return the manifest's text
std::string valid_manifest() {
    return R"(oamap: 1
id: "archipelago"
name: "Archipelago Pack"
version: "1.2"
description: "Islands and shoals."
author:
  name: "Core Prime"
  email: "maps@example.com"
packaging:
  revision: 1
  date: "2026-11-01"
  packager: "oa-tool 0.8"
requires:
  base: "ta-3.1c"
  engine: ">= 0.8.0"
homepage: "https://example.com/archipelago"
tags:
  - "islands"
  - "water"
maps:
  - stem: "isle_of_ashes"
    name: "Isle of Ashes"
    players: 4
    size: "16x16"
    description: "A ring of ash."
    preview: "previews/isle_of_ashes.png"
    files:
      - "maps/isle_of_ashes.ota"
      - "maps/isle_of_ashes.tnt"
      - "features/archipelago/islands.tdf"
      - "anims/islands.gaf"
      - "objects3d/wreck.3do"
)";
}

/// The design's example, with both maps' entries completed.
///
/// @return the manifest's text
std::string design_example() {
    return R"(oamap: 1
id: archipelago
name: Archipelago Pack
version: "1.2"
author: {name: Core Prime}
packaging: {revision: 1, date: 2026-11-01, packager: oa-tool 0.8}
requires: {base: ta-3.1c, engine: ">= 0.8.0"}
maps:
 - stem: isle_of_ashes
   name: Isle of Ashes
   players: 4
   size: 16x16
   preview: previews/isle_of_ashes.png
   files:
    - maps/isle_of_ashes.ota
    - maps/isle_of_ashes.tnt
    - features/archipelago/islands.tdf
    - anims/islands.gaf
    - objects3d/wreck.3do
 - stem: black_shoals
   name: Black Shoals
   players: 4
   size: 12x12
   files:
    - maps/black_shoals.ota
    - maps/black_shoals.tnt
    - features/archipelago/islands.tdf
    - anims/islands.gaf
    - objects3d/wreck.3do
)";
}

/// Reads a manifest that should pass, or prints why it did not.
///
/// @param yaml the manifest
/// @param use how to read it
/// @param[out] manifest the manifest
/// @return true when it was read
bool read_ok(std::string_view yaml, ManifestUse use, Manifest& manifest) {
    std::vector<Problem> problems;
    const bool read = pack::read_manifest(as_bytes(yaml), use, manifest, problems);
    if (!read)
        show_problems(problems);
    OA_CHECK(read);
    OA_CHECK(problems.empty());
    return read;
}

/// The design's example reads in package use, both maps complete.
void test_design_example() {
    Manifest manifest;
    if (!read_ok(design_example(), ManifestUse::package, manifest))
        return;
    OA_CHECK(manifest.format == pack::format_version);
    OA_CHECK(manifest.id == "archipelago");
    OA_CHECK(manifest.name == "Archipelago Pack");
    OA_CHECK(manifest.version == "1.2");
    OA_CHECK(manifest.description.empty());
    OA_CHECK(manifest.homepage.empty());
    OA_CHECK(manifest.tags.empty());
    OA_CHECK(manifest.author.name == "Core Prime");
    OA_CHECK(manifest.author.email.empty());
    OA_CHECK(manifest.packaging.revision == 1);
    OA_CHECK(manifest.packaging.date == "2026-11-01");
    OA_CHECK(manifest.packaging.packager == "oa-tool 0.8");
    OA_CHECK(manifest.requires_base == "ta-3.1c");
    OA_CHECK(manifest.requires_engine == ">= 0.8.0");
    OA_CHECK(manifest.maps.size() == 2);

    const MapEntry& isle = manifest.maps[0];
    OA_CHECK(isle.stem == "isle_of_ashes");
    OA_CHECK(isle.title == "Isle of Ashes");
    OA_CHECK(isle.players == 4);
    OA_CHECK(isle.size == "16x16");
    OA_CHECK(isle.preview == "previews/isle_of_ashes.png");
    OA_CHECK(isle.description.empty());
    OA_CHECK(isle.files.size() == 5);
    OA_CHECK(isle.files[0] == "maps/isle_of_ashes.ota");
    OA_CHECK(isle.files[1] == "maps/isle_of_ashes.tnt");
    OA_CHECK(isle.files[2] == "features/archipelago/islands.tdf");
    OA_CHECK(isle.files[3] == "anims/islands.gaf");
    OA_CHECK(isle.files[4] == "objects3d/wreck.3do");

    const MapEntry& shoals = manifest.maps[1];
    OA_CHECK(shoals.stem == "black_shoals");
    OA_CHECK(shoals.title == "Black Shoals");
    OA_CHECK(shoals.players == 4);
    OA_CHECK(shoals.size == "12x12");
    OA_CHECK(shoals.preview.empty());
    OA_CHECK(shoals.files.size() == 5);
    OA_CHECK(shoals.files[2] == isle.files[2]);
}

/// A header with only a stem reads as a source and is refused as a package.
void test_source_and_package() {
    const std::string yaml = R"(oamap: 1
id: archipelago
name: Archipelago Pack
version: "1.2"
author: {name: Core Prime}
packaging: {revision: 1, date: 2026-11-01, packager: oa-tool 0.8}
requires: {base: ta-3.1c, engine: ">= 0.8.0"}
maps: [{stem: isle_of_ashes}]
)";
    Manifest manifest;
    if (!read_ok(yaml, ManifestUse::source, manifest))
        return;
    OA_CHECK(manifest.maps.size() == 1);
    OA_CHECK(manifest.maps[0].stem == "isle_of_ashes");
    OA_CHECK(manifest.maps[0].title.empty());
    OA_CHECK(manifest.maps[0].files.empty());
    OA_CHECK(manifest.id == "archipelago");
    expect_refused(yaml, ManifestUse::package, "maps[0].name", "stem: isle_of_ashes");
    expect_refused(yaml, ManifestUse::package, "maps[0].files", "stem: isle_of_ashes");
    expect_refused(yaml, ManifestUse::package, "maps[0].players", "stem: isle_of_ashes");
    expect_refused(yaml, ManifestUse::package, "maps[0].size", "stem: isle_of_ashes");
}

/// A written manifest reads back as the same manifest.
void test_round_trip() {
    Manifest manifest;
    manifest.format = pack::format_version;
    manifest.id = "archipelago";
    manifest.name = "Archipelago Pack";
    manifest.version = "1.2";
    manifest.description = std::string(120, 'c');
    manifest.homepage = "https://example.com/archipelago";
    manifest.author = {"Core Prime", "maps@example.com"};
    manifest.packaging = {1, "2026-11-01", "oa-tool 0.8"};
    manifest.tags = {"islands", "water"};
    manifest.requires_base = "ta-3.1c";
    manifest.requires_engine = ">= 0.0.1";

    MapEntry isle;
    isle.stem = "isle_of_ashes";
    isle.title = "Isle \"of\" Ashes";
    isle.description = "Say \"hello\"";
    isle.players = 2;
    isle.size = "1x1";
    isle.preview = "previews/isle_of_ashes.png";
    isle.files = {
        "maps/isle_of_ashes.ota",
        "maps/isle_of_ashes.tnt",
        "features/shared/rocks.tdf",
    };
    MapEntry shoals;
    shoals.stem = "black_shoals";
    shoals.title = std::string(pack::most_title_characters, 'B');
    shoals.description = std::string(pack::most_description_bytes, 'd');
    shoals.players = pack::most_players;
    shoals.size = "128x128";
    shoals.files = {
        "maps/black_shoals.ota",
        "maps/black_shoals.tnt",
        "features/shared/rocks.tdf",
    };
    manifest.maps = {isle, shoals};

    const std::string yaml = pack::write_manifest(manifest);
    OA_CHECK(yaml.find("engine: \">= 0.0.1\"") != std::string::npos);
    OA_CHECK(yaml.find("features/shared/rocks.tdf") != std::string::npos);

    Manifest read;
    if (!read_ok(yaml, ManifestUse::package, read)) {
        std::fprintf(stderr, "%s\n", yaml.c_str());
        return;
    }
    OA_CHECK(read.format == manifest.format);
    OA_CHECK(read.id == manifest.id);
    OA_CHECK(read.name == manifest.name);
    OA_CHECK(read.version == manifest.version);
    OA_CHECK(read.description == manifest.description);
    OA_CHECK(read.homepage == manifest.homepage);
    OA_CHECK(read.tags == manifest.tags);
    OA_CHECK(read.author.name == manifest.author.name);
    OA_CHECK(read.author.email == manifest.author.email);
    OA_CHECK(read.packaging.revision == manifest.packaging.revision);
    OA_CHECK(read.packaging.date == manifest.packaging.date);
    OA_CHECK(read.packaging.packager == manifest.packaging.packager);
    OA_CHECK(read.requires_base == manifest.requires_base);
    OA_CHECK(read.requires_engine == manifest.requires_engine);
    OA_CHECK(read.maps.size() == 2);
    OA_CHECK(read.maps[0].stem == isle.stem);
    OA_CHECK(read.maps[0].title == isle.title);
    OA_CHECK(read.maps[0].description == isle.description);
    OA_CHECK(read.maps[0].players == isle.players);
    OA_CHECK(read.maps[0].size == isle.size);
    OA_CHECK(read.maps[0].preview == isle.preview);
    OA_CHECK(read.maps[0].files == isle.files);
    OA_CHECK(read.maps[1].stem == shoals.stem);
    OA_CHECK(read.maps[1].title == shoals.title);
    OA_CHECK(read.maps[1].description == shoals.description);
    OA_CHECK(read.maps[1].players == shoals.players);
    OA_CHECK(read.maps[1].size == shoals.size);
    OA_CHECK(read.maps[1].preview.empty());
    OA_CHECK(read.maps[1].files == shoals.files);
    OA_CHECK(read.maps[0].files[2] == read.maps[1].files[2]);
}

/// Each header and index rule, refused once, with its key and line.
void test_each_rule() {
    const std::string valid = valid_manifest();
    expect_refused(
        replaced(valid, "oamap: 1", "oamap: 2"), ManifestUse::package, "oamap", "oamap: 2"
    );
    expect_refused(
        replaced(valid, "id: \"archipelago\"", "bonus: \"no\"\nid: \"archipelago\""),
        ManifestUse::package,
        "bonus",
        "bonus: \"no\""
    );
    expect_refused(
        replaced(valid, "id: \"archipelago\"", "id: \"Archipelago\""),
        ManifestUse::package,
        "id",
        "id: \"Archipelago\""
    );
    expect_refused(
        replaced(
            valid,
            "id: \"archipelago\"",
            "id: \"" + std::string(pack::most_id_bytes + 1, 'a') + "\""
        ),
        ManifestUse::package,
        "id",
        "id: \""
    );
    expect_refused(
        replaced(valid, "name: \"Archipelago Pack\"", "name: \"A\\nB\""),
        ManifestUse::package,
        "name",
        "name: \"A\\nB\""
    );
    expect_refused(
        replaced(valid, "name: \"Archipelago Pack\"", "name: \"" + std::string(65, 'N') + "\""),
        ManifestUse::package,
        "name",
        "name: \""
    );
    expect_refused(
        replaced(valid, "version: \"1.2\"", "version: \"\""),
        ManifestUse::package,
        "version",
        "version: \"\""
    );
    expect_refused(
        replaced(valid, "Islands and shoals.", std::string(121, 'c')),
        ManifestUse::package,
        "description",
        "description: \""
    );
    expect_refused(
        replaced(valid, "description: \"Islands and shoals.\"", "description: \"A\\nB\""),
        ManifestUse::package,
        "description",
        "description: \"A\\nB\""
    );
    {
        const std::string no_email = replaced(valid, "  email: \"maps@example.com\"\n", "");
        Manifest manifest;
        OA_CHECK(read_ok(no_email, ManifestUse::package, manifest));
        OA_CHECK(manifest.author.email.empty());
    }
    expect_refused(
        replaced(valid, "author:\n  name: \"Core Prime\"\n  email: \"maps@example.com\"\n", ""),
        ManifestUse::package,
        "author",
        "oamap: 1"
    );
    expect_refused(
        replaced(valid, "name: \"Core Prime\"", "name: \"\""),
        ManifestUse::package,
        "author.name",
        "name: \"\""
    );
    expect_refused(
        replaced(valid, "maps@example.com", "nope"),
        ManifestUse::package,
        "author.email",
        "email: \"nope\""
    );
    expect_refused(
        replaced(valid, "revision: 1", "revision: 0"),
        ManifestUse::package,
        "packaging.revision",
        "revision: 0"
    );
    expect_refused(
        replaced(valid, "2026-11-01", "2026-02-29"),
        ManifestUse::package,
        "packaging.date",
        "date: \"2026-02-29\""
    );
    expect_refused(
        replaced(valid, "packager: \"oa-tool 0.8\"", "packager: \"" + std::string(65, 'p') + "\""),
        ManifestUse::package,
        "packaging.packager",
        "packager: \""
    );
    expect_refused(
        replaced(valid, "base: \"ta-3.1c\"", "base: \"ta-3.1c\"\n  catalogue: 1"),
        ManifestUse::package,
        "requires.catalogue",
        "catalogue: 1"
    );
    expect_refused(
        replaced(valid, "base: \"ta-3.1c\"", "base: \"ta-3.1\""),
        ManifestUse::package,
        "requires.base",
        "base: \"ta-3.1\""
    );
    expect_refused(
        replaced(valid, "https://example.com/archipelago", "notaurl"),
        ManifestUse::package,
        "homepage",
        "homepage: \"notaurl\""
    );
    expect_refused(
        replaced(valid, "- \"water\"", "- \"Water\""),
        ManifestUse::package,
        "tags[1]",
        "- \"Water\""
    );
    expect_refused(
        replaced(valid, "engine: \">= 0.8.0\"", "engine: \"nope\""),
        ManifestUse::package,
        "requires.engine",
        "engine: \"nope\""
    );
    {
        const std::string future =
            replaced(valid, "engine: \">= 0.8.0\"", "engine: \">= 9999.0.0\"");
        Manifest manifest;
        if (read_ok(future, ManifestUse::package, manifest))
            OA_CHECK(manifest.requires_engine == ">= 9999.0.0");
    }
    expect_refused(
        valid.substr(0, valid.find("\nmaps:\n")) + "\n", ManifestUse::package, "maps", "oamap: 1"
    );
    expect_refused(
        replaced(valid, "stem: \"isle_of_ashes\"", "stem: \".hidden\""),
        ManifestUse::package,
        "maps[0].stem",
        "stem: \".hidden\""
    );
    expect_refused(
        replaced(valid, "players: 4", "players: 1"),
        ManifestUse::package,
        "maps[0].players",
        "players: 1"
    );
    expect_refused(
        replaced(valid, "size: \"16x16\"", "size: \"129x16\""),
        ManifestUse::package,
        "maps[0].size",
        "size: \"129x16\""
    );
    expect_refused(
        replaced(valid, "name: \"Isle of Ashes\"", "name: \"" + std::string(65, 'T') + "\""),
        ManifestUse::package,
        "maps[0].name",
        "name: \"" + std::string(65, 'T') + "\""
    );
    expect_refused(
        replaced(
            valid,
            "description: \"A ring of ash.\"",
            "description: \"" + std::string(pack::most_description_bytes + 1, 'd') + "\""
        ),
        ManifestUse::package,
        "maps[0].description",
        std::string(pack::most_description_bytes + 1, 'd')
    );
    expect_refused(
        replaced(
            valid, "preview: \"previews/isle_of_ashes.png\"", "preview: \"pictures/isle.png\""
        ),
        ManifestUse::package,
        "maps[0].preview",
        "preview: \"pictures/isle.png\""
    );
    expect_refused(
        valid.substr(0, valid.find("\nmaps:\n")) + "\nmaps:\n  - \"not a map\"\n",
        ManifestUse::package,
        "maps[0]",
        "\"not a map\""
    );
    expect_refused(
        replaced(valid, "players: 4", "players: 4\n    note: \"x\""),
        ManifestUse::package,
        "maps[0].note",
        "note: \"x\""
    );
    expect_refused(
        valid.substr(0, valid.find("\nmaps:\n")) + "\nmaps: []\n",
        ManifestUse::package,
        "maps",
        "maps: []"
    );

    const std::string second =
        std::string{"  - stem: \"Isle_Of_Ashes\"\n"} +
        "    name: \"Other\"\n    players: 2\n    size: \"2x2\"\n    files:\n"
        "      - \"maps/Isle_Of_Ashes.ota\"\n"
        "      - \"maps/Isle_Of_Ashes.tnt\"\n";
    expect_refused(valid + second, ManifestUse::package, "maps[1].stem", "Isle_Of_Ashes");

    const std::string other_ota = replaced(
        valid, "objects3d/wreck.3do\"", "objects3d/wreck.3do\"\n      - \"maps/black_shoals.ota\""
    );
    expect_refused(other_ota, ManifestUse::package, "maps[0].files[5]", "maps/black_shoals.ota");

    const std::string no_tnt = replaced(valid, "      - \"maps/isle_of_ashes.tnt\"\n", "");
    expect_refused(no_tnt, ManifestUse::package, "maps[0].files", "maps/isle_of_ashes.ota");

    const std::string three = replaced(
        valid,
        "objects3d/wreck.3do\"",
        "objects3d/wreck.3do\"\n      - \"features/a:\\nb\\\\c.tdf\""
    );
    {
        Manifest manifest;
        manifest.id = "sentinel";
        std::vector<Problem> problems;
        const bool read =
            pack::read_manifest(as_bytes(three), ManifestUse::package, manifest, problems);
        if (read || problems.size() != 3 || manifest.id != "sentinel") {
            std::fprintf(
                stderr, "three-rule file: read %d, %zu problems\n", read ? 1 : 0, problems.size()
            );
            show_problems(problems);
        }
        OA_CHECK(!read);
        OA_CHECK(manifest.id == "sentinel");
        OA_CHECK(problems.size() == 3);
        bool backslash = false;
        bool colon = false;
        bool control = false;
        for (const Problem& problem : problems) {
            OA_CHECK(problem.key == "maps[0].files[5]");
            backslash = backslash || problem.message.find("backslash") != std::string::npos;
            colon = colon || problem.message.find("colon") != std::string::npos;
            control = control || problem.message.find("control") != std::string::npos;
        }
        OA_CHECK(backslash);
        OA_CHECK(colon);
        OA_CHECK(control);
    }

    const std::string several = replaced(
        replaced(replaced(valid, "id: \"archipelago\"", "id: \"NO\""), "players: 4", "players: 1"),
        "homepage: \"https://example.com/archipelago\"",
        "bonus: \"no\"\nhomepage: \"https://example.com/archipelago\""
    );
    {
        Manifest manifest;
        manifest.id = "sentinel";
        std::vector<Problem> problems;
        OA_CHECK(!pack::read_manifest(as_bytes(several), ManifestUse::package, manifest, problems));
        OA_CHECK(manifest.id == "sentinel");
        OA_CHECK(problem_at(problems, "id", line_of(several, "id: \"NO\"")));
        OA_CHECK(problem_at(problems, "maps[0].players", line_of(several, "players: 1")));
        OA_CHECK(problem_at(problems, "bonus", line_of(several, "bonus: \"no\"")));
        if (problems.size() < 3)
            show_problems(problems);
        OA_CHECK(problems.size() >= 3);
    }

    const std::string documents = "oamap: 1\n---\nid: \"archipelago\"\n";
    {
        Manifest manifest;
        manifest.id = "sentinel";
        std::vector<Problem> problems;
        OA_CHECK(
            !pack::read_manifest(as_bytes(documents), ManifestUse::package, manifest, problems)
        );
        OA_CHECK(manifest.id == "sentinel");
        OA_CHECK(problem_at(problems, "document", line_of(documents, "---")));
        OA_CHECK(!problems.empty());
        OA_CHECK(problems[0].message.find("exactly one document") != std::string::npos);
    }
}

/// A joined name that is one byte over the limit is refused, and one that
/// fills the limit is read.
void test_name_limit() {
    const std::string stem(pack::most_stem_bytes, 'a');
    const std::string too_long_id(pack::most_map_name_bytes - pack::most_stem_bytes, 'b');
    const std::string fitting_id(pack::most_map_name_bytes - pack::most_stem_bytes - 1, 'b');
    OA_CHECK(pack::valid_stem(stem));
    OA_CHECK(pack::valid_pack_id(too_long_id));
    OA_CHECK(pack::valid_pack_id(fitting_id));
    const std::string too_long = pack::pack_map_name(stem, too_long_id);
    const std::string fitting = pack::pack_map_name(stem, fitting_id);
    OA_CHECK(too_long.size() == pack::most_map_name_bytes + 1);
    OA_CHECK(fitting.size() == pack::most_map_name_bytes);
    OA_CHECK(!pack::map_name_fits(too_long));
    OA_CHECK(pack::map_name_fits(fitting));

    const auto with_names = [&](std::string_view id) {
        std::string yaml =
            replaced(valid_manifest(), "id: \"archipelago\"", "id: \"" + std::string{id} + "\"");
        return replace_all(std::move(yaml), "isle_of_ashes", stem);
    };
    const std::string yaml = with_names(too_long_id);
    expect_refused(yaml, ManifestUse::package, "maps[0].stem", "stem: \"" + stem + "\"");
    {
        Manifest manifest;
        manifest.id = "sentinel";
        std::vector<Problem> problems;
        OA_CHECK(!pack::read_manifest(as_bytes(yaml), ManifestUse::package, manifest, problems));
        bool named = false;
        for (const Problem& problem : problems) {
            if (problem.key != "maps[0].stem")
                continue;
            const std::string message = too_long + " is " + std::to_string(too_long.size()) +
                                        " bytes; a map's name must fit in " +
                                        std::to_string(pack::most_map_name_bytes) +
                                        " bytes to travel in the battle room and in saved games";
            named = problem.message == message;
        }
        if (!named)
            show_problems(problems);
        OA_CHECK(named);
    }

    const std::string fits = with_names(fitting_id);
    Manifest manifest;
    if (read_ok(fits, ManifestUse::package, manifest)) {
        OA_CHECK(manifest.id == fitting_id);
        OA_CHECK(manifest.maps[0].stem == stem);
        OA_CHECK(pack::map_name_fits(pack::pack_map_name(manifest.maps[0].stem, manifest.id)));
    }
}

/// Names split at the last @, and only when both pieces could be written.
void test_split_and_fit() {
    const std::optional<pack::PackMapName> split =
        pack::split_pack_map_name("isle_of_ashes@archipelago");
    OA_CHECK(split.has_value());
    if (split) {
        OA_CHECK(split->stem == "isle_of_ashes");
        OA_CHECK(split->id == "archipelago");
    }
    OA_CHECK(!pack::split_pack_map_name("a@b@archipelago"));
    OA_CHECK(!pack::split_pack_map_name("@archipelago"));
    OA_CHECK(!pack::split_pack_map_name("isle@"));
    OA_CHECK(!pack::split_pack_map_name("isle@Archipelago"));
    OA_CHECK(!pack::split_pack_map_name("isle_of_ashes"));

    OA_CHECK(pack::map_name_fits(std::string(pack::most_map_name_bytes, 'm')));
    OA_CHECK(!pack::map_name_fits(std::string(pack::most_map_name_bytes + 1, 'm')));
    OA_CHECK(pack::pack_map_name("isle_of_ashes", "archipelago") == "isle_of_ashes@archipelago");
}

/// describe prints the manifest name, the line, the column, the key and the message.
void test_describe() {
    const Problem problem{12, 5, "maps[0].stem", "too long"};
    OA_CHECK(pack::describe(problem) == "oamap.yaml:12:5: maps[0].stem: too long");
}

/// A packed manifest of 256 source stems is read, and 257 is refused.
void test_map_count() {
    std::string header = R"(oamap: 1
id: "archipelago"
name: "Archipelago Pack"
version: "1"
author:
  name: "Core Prime"
packaging:
  revision: 1
  date: "2026-11-01"
  packager: "oa-tool 0.8"
maps:
)";
    std::string within = header;
    for (int index = 0; index < static_cast<int>(pack::most_maps); ++index)
        within += "  - stem: \"m" + std::to_string(index) + "\"\n";
    Manifest manifest;
    if (read_ok(within, ManifestUse::source, manifest))
        OA_CHECK(manifest.maps.size() == pack::most_maps);
    std::string over = within + "  - stem: \"mextra\"\n";
    expect_refused(over, ManifestUse::source, "maps", "stem: \"m0\"");
}

} // namespace

int main() {
    test_design_example();
    test_source_and_package();
    test_round_trip();
    test_each_rule();
    test_name_limit();
    test_split_and_fit();
    test_describe();
    test_map_count();
    return oa::test::check_exit_status();
}
