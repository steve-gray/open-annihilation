// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// check: the installer's own reading of a package, then the facts a
// catalogue lists, as text or as one JSON object. The file is hashed in
// pieces. Nothing here decides a package is installable except open_package.

#include "check.hpp"

#include "arguments.hpp"
#include "command.hpp"

#include "oa/app/package_install/oamod.hpp"
#include "oa/base/sha256.hpp"
#include "oa/data/mod_profile.hpp"
#include "oa/data/mod_profile/registry.hpp"
#include "oa/formats/oamod.hpp"
#include "oa/formats/zip/stream.hpp"
#include "oa/platform/files.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::tool {
namespace {

namespace fs = std::filesystem;
namespace install = oa::app::package_install;
namespace json = oa::formats::json;
namespace sha256 = oa::base::sha256;
namespace zip = oa::formats::zip;
namespace mod_profile = oa::data::mod_profile;
namespace oamod_format = oa::formats::oamod;

/// The catalogue report's check value: the shape of this object's keys.
constexpr int64_t check_format = 1;

/// Bytes hashed from the package file at a time.
constexpr std::size_t read_piece = std::size_t{1} << 20;

/// One hack turned on, in the registry's order.
struct HackOn {
    std::string id{};
    mod_profile::registry::Scope scope{};
    bool implemented{};
};

/// The fields of a language manifest this report writes.
struct LanguageFields {
    bool read{}; ///< the manifest's tree was read
    std::string tag{};
    std::string name{};
    std::string english_name{};
    std::string word{};
    std::string version{};
    std::string text_needs{};
    std::optional<std::vector<std::string>> locales{};
    std::optional<std::vector<std::string>> fallbacks{};
    std::optional<bool> unicode{};
    std::optional<int64_t> revision{}; ///< packaging.revision, when it is a whole number
};

/// A file opened for a positioned read.
struct FilePos {
    std::FILE* stream{};
    oa::platform::Files files{};

    FilePos() = default;
    FilePos(const FilePos&) = delete;
    FilePos& operator=(const FilePos&) = delete;

    /// Closes the file.
    ~FilePos() {
        if (stream != nullptr)
            std::fclose(stream);
    }

    /// Returns the hooks a streamed zip read uses.
    ///
    /// @return the hooks; they read through this object, which must outlive them
    [[nodiscard]] zip::SourceHooks source() { return zip::SourceHooks{this, &read_at}; }

    /// Reads bytes at an offset.
    ///
    /// @param context the FilePos
    /// @param offset where the bytes start
    /// @param bytes where they are written
    /// @return true when every byte was read
    static bool read_at(void* context, uint64_t offset, std::span<uint8_t> bytes) {
        auto* file = static_cast<FilePos*>(context);
        if (file->stream == nullptr ||
            offset > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
            return false;
        auto* handle = reinterpret_cast<oa::platform::FileHandle*>(file->stream);
        if (file->files.seek(
                nullptr, handle, static_cast<int64_t>(offset), oa::platform::SeekOrigin::begin
            ) != 0)
            return false;
        std::size_t done = 0;
        while (done < bytes.size()) {
            const std::size_t got =
                std::fread(bytes.data() + done, 1, bytes.size() - done, file->stream);
            if (got == 0)
                return false;
            done += got;
        }
        return true;
    }
};

/// The size and SHA-256 of a file read in pieces.
struct FileHash {
    uint64_t size{};
    sha256::Digest digest{};
};

/// Returns a path's file name in UTF-8.
///
/// @param path the path
/// @return the file name
std::string file_name_of(const fs::path& path) {
    const auto text = path.filename().generic_u8string();
    return {text.begin(), text.end()};
}

/// Returns a path made from UTF-8 text.
///
/// @param text the text
/// @return the path
fs::path path_of(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

/// Opens a file for reading.
///
/// @param path the file
/// @param[out] file receives the open file
/// @return true when it opened
bool open_read(const fs::path& path, FilePos& file) {
    file.stream = oa::platform::open_file(path, "rb");
    if (file.stream == nullptr)
        return false;
    file.files = oa::platform::stdio_files();
    return true;
}

/// Hashes a whole file in read_piece reads.
///
/// @param path the file
/// @return its size and SHA-256; nothing when it cannot be read
std::optional<FileHash> hash_file(const fs::path& path) {
    FilePos file;
    if (!open_read(path, file))
        return std::nullopt;
    sha256::Hasher hasher{};
    std::vector<uint8_t> buffer(read_piece);
    uint64_t size = 0;
    while (true) {
        const std::size_t got = std::fread(buffer.data(), 1, buffer.size(), file.stream);
        if (got > 0) {
            sha256::update(hasher, std::span<const uint8_t>(buffer.data(), got));
            size += got;
        }
        if (got < buffer.size()) {
            if (std::ferror(file.stream) != 0)
                return std::nullopt;
            break;
        }
    }
    return FileHash{size, sha256::finish(hasher)};
}

/// Writes a digest as 64 lower-case hex digits.
///
/// @param digest the digest
/// @return the digits
std::string hex_text(const sha256::Digest& digest) {
    const auto hex = sha256::to_hex(digest);
    return {hex.data(), hex.size()};
}

/// Reads a package's manifest from the file, a piece at a time.
///
/// @param package the package
/// @return the manifest's bytes; empty when they cannot be read
std::vector<uint8_t> read_packaged_manifest(const install::Package& package) {
    std::vector<uint8_t> bytes;
    if (package.kind == nullptr || package.manifest_entry >= package.directory.entries.size())
        return bytes;
    FilePos file;
    if (!open_read(package.file, file))
        return bytes;
    zip::ZipError error{};
    if (!zip::read_stream_entry(
            file.source(),
            package.archive_bytes,
            package.directory.entries[package.manifest_entry],
            package.kind->manifest_most_bytes,
            bytes,
            error
        ))
        bytes.clear();
    return bytes;
}

/// Returns the sentence the installer shows for a refusal.
///
/// @param kind the package's kind; null when the name is not a kind
/// @param problem the refusal
/// @return the sentence
std::string refusal_sentence(const install::PackageKind* kind, const install::Problem& problem) {
    std::string text = kind != nullptr ? install::refusal_text(*kind, problem)
                                       : install::oamod::refusal_text(problem);
    if (text.empty())
        text = problem.detail.empty() ? std::string("It cannot be read.") : problem.detail;
    return text;
}

/// Writes a list of strings.
///
/// @param[in,out] writer the JSON being written
/// @param lines the strings
void write_strings(json::JsonWriter& writer, std::span<const std::string> lines) {
    writer.begin_array();
    for (const std::string& line : lines)
        writer.string(line);
    writer.end_array();
}

/// Writes one string member when the text is not empty.
///
/// @param[in,out] writer the object
/// @param key the member's name
/// @param text the text
void write_text(json::JsonWriter& writer, std::string_view key, std::string_view text) {
    if (text.empty())
        return;
    writer.key(key);
    writer.string(text);
}

/// Writes a string list when the manifest has the key.
///
/// @param[in,out] writer the object
/// @param key the member's name
/// @param lines the list; nothing when the key is absent
void write_list(
    json::JsonWriter& writer,
    std::string_view key,
    const std::optional<std::vector<std::string>>& lines
) {
    if (!lines)
        return;
    writer.key(key);
    write_strings(writer, *lines);
}

/// Returns a mapping entry's string, when it is one.
///
/// @param node the entry; null for none
/// @return the text; empty when it is not a string
std::string string_of(const oamod_format::Node* node) {
    if (node == nullptr || node->kind != oamod_format::NodeKind::string)
        return {};
    return node->text;
}

/// Returns a sequence's strings, when the entry is a sequence.
///
/// @param node the entry; null for none
/// @return the strings; nothing when the entry is absent or not a sequence
std::optional<std::vector<std::string>> strings_of(const oamod_format::Node* node) {
    if (node == nullptr || node->kind != oamod_format::NodeKind::sequence)
        return std::nullopt;
    std::vector<std::string> lines;
    for (const oamod_format::Node& item : node->children)
        if (item.kind == oamod_format::NodeKind::string)
            lines.push_back(item.text);
    return lines;
}

/// Reads the language fields a catalogue lists from a manifest's tree.
///
/// @param manifest the language.yaml bytes
/// @return the fields; read is false when the text is not a manifest
LanguageFields read_language(std::span<const uint8_t> manifest) {
    LanguageFields fields{};
    oamod_format::Node root;
    oamod_format::ReadError error{};
    if (!oamod_format::read_document(manifest, root, error))
        return fields;
    fields.read = true;
    fields.tag = string_of(oamod_format::find_entry(root, "tag"));
    fields.name = string_of(oamod_format::find_entry(root, "name"));
    fields.english_name = string_of(oamod_format::find_entry(root, "english-name"));
    fields.word = string_of(oamod_format::find_entry(root, "word"));
    fields.version = string_of(oamod_format::find_entry(root, "version"));
    fields.locales = strings_of(oamod_format::find_entry(root, "locales"));
    fields.fallbacks = strings_of(oamod_format::find_entry(root, "fallbacks"));
    if (const oamod_format::Node* text = oamod_format::find_entry(root, "text"))
        fields.text_needs = string_of(oamod_format::find_entry(*text, "needs"));
    if (const oamod_format::Node* unicode = oamod_format::find_entry(root, "unicode");
        unicode != nullptr && unicode->kind == oamod_format::NodeKind::boolean)
        fields.unicode = unicode->boolean;
    if (const oamod_format::Node* packaging = oamod_format::find_entry(root, "packaging")) {
        const oamod_format::Node* revision = oamod_format::find_entry(*packaging, "revision");
        int64_t value = 0;
        if (revision != nullptr && revision->kind == oamod_format::NodeKind::number &&
            oamod_format::integer_value(revision->number, value))
            fields.revision = value;
    }
    return fields;
}

/// Writes a language pack's id, name, version and revision.
///
/// @param fields the manifest's fields
/// @param[in,out] writer the object
void write_oalang_identity(const LanguageFields& fields, json::JsonWriter& writer) {
    write_text(writer, "id", fields.tag);
    write_text(writer, "name", fields.name);
    write_text(writer, "version", fields.version);
    writer.key("revision");
    if (fields.revision)
        writer.integer(*fields.revision);
    else
        writer.null();
}

/// Writes a language pack's language object.
///
/// @param fields the manifest's fields
/// @param[in,out] writer the object
void write_oalang_language(const LanguageFields& fields, json::JsonWriter& writer) {
    writer.key("language");
    writer.begin_object();
    write_text(writer, "tag", fields.tag);
    write_text(writer, "name", fields.name);
    write_text(writer, "english-name", fields.english_name);
    write_text(writer, "word", fields.word);
    write_list(writer, "locales", fields.locales);
    write_list(writer, "fallbacks", fields.fallbacks);
    write_text(writer, "text-needs", fields.text_needs);
    if (fields.unicode) {
        writer.key("unicode");
        writer.boolean(*fields.unicode);
    }
    writer.end_object();
}

/// Returns the hacks a resolved profile turns on, in the registry's order.
///
/// @param effective the effective profile
/// @return one entry per hack that is on
std::vector<HackOn> hacks_on(const mod_profile::Value& effective) {
    const std::vector<mod_profile::HackState> states = mod_profile::hack_states(effective);
    const std::span<const mod_profile::registry::Entry* const> hacks =
        mod_profile::standard_hacks();
    std::vector<HackOn> on;
    const std::size_t count = states.size() < hacks.size() ? states.size() : hacks.size();
    for (std::size_t index = 0; index < count; ++index) {
        if (!states[index].on || hacks[index] == nullptr)
            continue;
        on.push_back(
            HackOn{std::string(hacks[index]->id), hacks[index]->scope, hacks[index]->implemented}
        );
    }
    return on;
}

/// Resolves a mod's profile as a catalogue compares it: no settings, and a
/// hack this build does not carry out yet accepted so the hashes still exist.
///
/// @param package the package
/// @return the resolution; nothing when the profile cannot be read or resolved
std::optional<mod_profile::Resolution> catalogue_resolution(const install::Package& package) {
    const std::vector<uint8_t> manifest = read_packaged_manifest(package);
    if (manifest.empty())
        return std::nullopt;
    mod_profile::ResolveOptions options{};
    options.accept_unimplemented_hacks = true;
    const std::string source = package.file_name + "/oamod.yaml";
    mod_profile::ResolveResult resolved = mod_profile::resolve_profile(manifest, source, options);
    if (!resolved.resolution)
        return std::nullopt;
    return std::move(*resolved.resolution);
}

/// Writes requires.base and requires.engine when the profile has them.
///
/// @param effective the effective profile, whose requires.base is as written
/// @param engine the engine requirement as written; empty when the profile has none
/// @param[in,out] writer the object
void write_requires(
    const mod_profile::Value& effective, std::string_view engine, json::JsonWriter& writer
) {
    const mod_profile::Value* required = mod_profile::find_member(effective, "requires");
    const mod_profile::Value* base =
        required != nullptr ? mod_profile::find_member(*required, "base") : nullptr;
    const bool has_base = base != nullptr && base->kind == mod_profile::ValueKind::string;
    if (!has_base && engine.empty())
        return;
    writer.key("requires");
    writer.begin_object();
    if (has_base)
        write_text(writer, "base", base->text);
    write_text(writer, "engine", engine);
    writer.end_object();
}

/// Writes packaging when the profile has it.
///
/// @param effective the effective profile
/// @param[in,out] writer the object
void write_packaging(const mod_profile::Value& effective, json::JsonWriter& writer) {
    const mod_profile::Value* packaging = mod_profile::find_member(effective, "packaging");
    if (packaging == nullptr || packaging->kind != mod_profile::ValueKind::map)
        return;
    const mod_profile::Value* revision = mod_profile::find_member(*packaging, "revision");
    int64_t revision_number = 0;
    const bool has_revision = revision != nullptr &&
                              revision->kind == mod_profile::ValueKind::number &&
                              oamod_format::integer_value(revision->number, revision_number);
    const mod_profile::Value* date = mod_profile::find_member(*packaging, "date");
    const mod_profile::Value* packager = mod_profile::find_member(*packaging, "packager");
    const bool has_date = date != nullptr && date->kind == mod_profile::ValueKind::string;
    const bool has_packager =
        packager != nullptr && packager->kind == mod_profile::ValueKind::string;
    if (!has_revision && !has_date && !has_packager)
        return;
    writer.key("packaging");
    writer.begin_object();
    if (has_revision) {
        writer.key("revision");
        writer.integer(revision_number);
    }
    if (has_date)
        write_text(writer, "date", date->text);
    if (has_packager)
        write_text(writer, "packager", packager->text);
    writer.end_object();
}

/// Writes one JSON value again, for a text line's array or number.
///
/// @param[in,out] writer the JSON being written
/// @param value the value
void write_json_value(json::JsonWriter& writer, const json::Json& value) {
    switch (value.type()) {
    case json::JsonType::null:
        writer.null();
        break;
    case json::JsonType::boolean:
        writer.boolean(value.boolean().value_or(false));
        break;
    case json::JsonType::number:
        if (const std::string* text = value.number_text())
            writer.raw(*text);
        else
            writer.null();
        break;
    case json::JsonType::string:
        writer.string(value.string() != nullptr ? *value.string() : std::string_view{});
        break;
    case json::JsonType::array:
        writer.begin_array();
        for (const json::Json& item : value.elements())
            write_json_value(writer, item);
        writer.end_array();
        break;
    case json::JsonType::object:
        writer.begin_object();
        {
            const std::span<const std::string> names = value.names();
            const std::span<const json::Json> values = value.values();
            const std::size_t count = names.size() < values.size() ? names.size() : values.size();
            for (std::size_t index = 0; index < count; ++index) {
                writer.key(names[index]);
                write_json_value(writer, values[index]);
            }
        }
        writer.end_object();
        break;
    }
}

/// Prints one fact, or each member of an object, as `key: value`.
///
/// @param[in,out] out where the lines are written
/// @param key the fact's name; empty at the report's top
/// @param value the fact
void print_fact(std::ostream& out, std::string_view key, const json::Json& value) {
    if (value.type() == json::JsonType::object) {
        const std::span<const std::string> names = value.names();
        const std::span<const json::Json> values = value.values();
        const std::size_t count = names.size() < values.size() ? names.size() : values.size();
        for (std::size_t index = 0; index < count; ++index) {
            std::string nested(key);
            if (!nested.empty())
                nested.push_back('.');
            nested += names[index];
            print_fact(out, nested, values[index]);
        }
        return;
    }
    out << key << ": ";
    if (value.type() == json::JsonType::string && value.string() != nullptr)
        out << *value.string();
    else if (value.type() == json::JsonType::boolean)
        out << (value.boolean().value_or(false) ? "true" : "false");
    else if (value.type() == json::JsonType::null)
        out << "null";
    else {
        json::JsonWriter writer;
        write_json_value(writer, value);
        out << writer.text();
    }
    out << '\n';
}

/// Prints a usage failure the way the dispatcher does.
///
/// @param output where it is printed
/// @param problem what is wrong
void report_usage(Output& output, std::string_view problem) {
    output.err << "oa-tool check: " << problem << '\n';
    output.err << "run 'oa-tool help check'\n";
}

/// Writes the report and returns the exit code.
///
/// @param file the package's path
/// @param hashed the file's size and SHA-256; nothing when it could not be read
/// @param opened what open_package returned
/// @param as_json print one JSON object
/// @param[in,out] output receives the report
/// @return exit_done when there is no problem, otherwise exit_failed
int report(
    const fs::path& file,
    const std::optional<FileHash>& hashed,
    const install::PackageResult& opened,
    bool as_json,
    Output& output
) {
    const install::PackageKind* kind = install::kind_for_file(file);
    const install::Package* package = opened.package ? &*opened.package : nullptr;
    if (package != nullptr && package->kind != nullptr)
        kind = package->kind;

    std::vector<std::string> problems;
    std::vector<std::string> warnings;
    if (package == nullptr) {
        problems.push_back(refusal_sentence(kind, opened.problem));
        for (const std::string& line : opened.problem.lines)
            if (!line.empty())
                problems.push_back(line);
    } else {
        warnings = package->warnings;
    }

    LanguageFields language{};
    const bool oalang = package != nullptr && kind != nullptr && kind->name == "oalang";
    if (oalang)
        language = read_language(read_packaged_manifest(*package));

    json::JsonWriter writer;
    writer.begin_object();
    writer.key("check");
    writer.integer(check_format);
    writer.key("ok");
    writer.boolean(problems.empty());
    writer.key("problems");
    write_strings(writer, problems);
    writer.key("warnings");
    write_strings(writer, warnings);
    writer.key("file");
    writer.string(file_name_of(file));
    if (kind != nullptr)
        write_text(writer, "kind", kind->name);
    if (oalang && language.read) {
        write_oalang_identity(language, writer);
    } else if (package != nullptr && !package->incoming.id.empty()) {
        write_text(writer, "id", package->incoming.id);
        write_text(writer, "name", package->incoming.name);
        write_text(writer, "version", package->incoming.version);
        writer.key("revision");
        writer.integer(package->incoming.revision);
    }
    if (hashed) {
        writer.key("size");
        writer.integer(static_cast<int64_t>(hashed->size));
        write_text(writer, "sha256", hex_text(hashed->digest));
    }
    if (package != nullptr) {
        writer.key("unpacked_size");
        writer.integer(static_cast<int64_t>(package->unpacked_bytes));
        writer.key("files");
        writer.integer(static_cast<int64_t>(package->files.size()));
        if (kind != nullptr && kind->name == "oamod")
            describe_oamod(*package, writer);
        else if (oalang && language.read)
            write_oalang_language(language, writer);
    }
    writer.end_object();

    if (as_json) {
        output.out << writer.text() << '\n';
    } else {
        json::JsonError error{};
        const std::optional<json::Json> parsed = json::parse_json(writer.text(), error);
        if (!parsed || parsed->type() != json::JsonType::object)
            output.out << writer.text() << '\n';
        else
            print_fact(output.out, {}, *parsed);
        output.out << "result: " << (problems.empty() ? "ok" : "refused") << '\n';
    }
    return problems.empty() ? exit_done : exit_failed;
}

} // namespace

void describe_oamod(const install::Package& package, json::JsonWriter& writer) {
    const std::optional<mod_profile::Resolution> resolved = catalogue_resolution(package);
    if (!resolved)
        return;
    const mod_profile::ModProfile& profile = resolved->profile;
    const mod_profile::Value& effective = resolved->effective;
    write_text(writer, "summary", profile.description);
    write_text(writer, "homepage", profile.homepage);
    if (!profile.tags.empty()) {
        writer.key("tags");
        write_strings(writer, profile.tags);
    }
    write_text(writer, "author", profile.author.name);
    write_requires(effective, profile.requires_engine, writer);
    write_text(writer, "sim_hash", hex_text(profile.sim_hash));
    write_text(writer, "full_hash", hex_text(profile.full_hash));

    const std::vector<HackOn> on = hacks_on(effective);
    int64_t sim = 0;
    int64_t view = 0;
    std::vector<std::string> ids;
    std::vector<std::string> unimplemented;
    ids.reserve(on.size());
    for (const HackOn& hack : on) {
        if (hack.scope == mod_profile::registry::Scope::sim)
            ++sim;
        else if (hack.scope == mod_profile::registry::Scope::view)
            ++view;
        ids.push_back(hack.id);
        if (!hack.implemented)
            unimplemented.push_back(hack.id);
    }
    writer.key("hacks");
    writer.begin_object();
    writer.key("sim");
    writer.integer(sim);
    writer.key("view");
    writer.integer(view);
    writer.key("ids");
    write_strings(writer, ids);
    writer.key("unimplemented");
    write_strings(writer, unimplemented);
    writer.end_object();
    write_packaging(effective, writer);
}

void describe_oalang(std::span<const uint8_t> manifest, json::JsonWriter& writer) {
    const LanguageFields fields = read_language(manifest);
    if (!fields.read)
        return;
    write_oalang_identity(fields, writer);
    write_oalang_language(fields, writer);
}

int run_check(std::span<const std::string> arguments, Output& output) {
    static constexpr OptionSpec options[] = {
        {"game-dir", true, false},
        {"accept-unimplemented-hacks", false, false},
        {"json", false, false},
    };
    std::string problem;
    const std::optional<Arguments> parsed = parse_arguments(arguments, options, problem);
    if (!parsed) {
        report_usage(output, problem);
        return exit_usage;
    }
    if (parsed->positional.size() != 1) {
        report_usage(
            output, "expected one package file, got " + std::to_string(parsed->positional.size())
        );
        return exit_usage;
    }
    const std::string* const game_dir = parsed->value("game-dir");
    if (game_dir != nullptr && game_dir->empty()) {
        report_usage(output, "option '--game-dir' needs a value");
        return exit_usage;
    }

    const fs::path file = path_of(parsed->positional[0]);
    const std::optional<FileHash> hashed = hash_file(file);
    install::PackageOptions package_options{};
    package_options.accept_unimplemented_hacks =
        parsed->flags.contains("accept-unimplemented-hacks");
    if (game_dir != nullptr)
        package_options.game_folder = path_of(*game_dir);
    const install::PackageResult opened = install::open_package(file, package_options);
    return report(file, hashed, opened, parsed->flags.contains("json"), output);
}

} // namespace oa::tool
