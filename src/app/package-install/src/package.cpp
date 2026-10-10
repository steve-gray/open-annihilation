// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A package read and checked before anything is written. What a folder holds
// is the kind's own read.

#include "files.hpp"
#include "package_file.hpp"

#include "oa/app/package_install.hpp"
#include "oa/platform/files.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace oa::app::package_install {

namespace fs = std::filesystem;
namespace zip = oa::formats::zip;

namespace detail {

PackageFile::~PackageFile() {
    if (stream != nullptr)
        std::fclose(stream);
}

bool PackageFile::open(const fs::path& file) {
    stream = oa::platform::open_file(file, "rb");
    if (stream == nullptr)
        return false;
    files = oa::platform::stdio_files();
    auto* handle = reinterpret_cast<oa::platform::FileHandle*>(stream);
    if (files.seek(nullptr, handle, 0, oa::platform::SeekOrigin::end) != 0)
        return false;
    const int64_t end = files.tell(nullptr, handle);
    if (end < 0)
        return false;
    bytes = static_cast<uint64_t>(end);
    return true;
}

zip::SourceHooks PackageFile::source() {
    return {
        this, [](void* context, uint64_t offset, std::span<uint8_t> out) {
            auto& file = *static_cast<PackageFile*>(context);
            auto* handle = reinterpret_cast<oa::platform::FileHandle*>(file.stream);
            if (file.stream == nullptr || offset > static_cast<uint64_t>(INT64_MAX) ||
                file.files.seek(
                    nullptr, handle, static_cast<int64_t>(offset), oa::platform::SeekOrigin::begin
                ) != 0)
                return false;
            std::size_t done = 0;
            while (done < out.size()) {
                const std::size_t read =
                    std::fread(out.data() + done, 1, out.size() - done, file.stream);
                if (read == 0)
                    return false;
                done += read;
            }
            return true;
        }
    };
}

} // namespace detail

namespace {

/// The folder macOS writes its file attributes in, when it zips a folder.
constexpr std::string_view mac_attributes_folder = "__macosx";
/// The file the Finder keeps a folder's look in.
constexpr std::string_view finder_file = ".ds_store";
/// What the name of a file of AppleDouble attributes starts with.
constexpr std::string_view apple_double_prefix = "._";

/// Returns a problem.
///
/// @param refusal why
/// @param subject what it names
/// @param detail English, for the log
/// @return the problem
Problem problem_of(Refusal refusal, std::string subject = {}, std::string detail = {}) {
    Problem problem{};
    problem.refusal = refusal;
    problem.subject = std::move(subject);
    problem.detail = std::move(detail);
    return problem;
}

/// Returns a result that refuses.
///
/// @param problem why
/// @return the result
PackageResult refused(Problem problem) {
    PackageResult result{};
    result.problem = std::move(problem);
    return result;
}

/// Returns the last '/'-separated part of a name, a folder's trailing '/' left out.
///
/// @param name the name
/// @return its last part
std::string_view last_part(std::string_view name) {
    if (!name.empty() && name.back() == '/')
        name.remove_suffix(1);
    const std::size_t separator = name.rfind('/');
    return separator == std::string_view::npos ? name : name.substr(separator + 1);
}

/// Returns the first '/'-separated part of a name.
///
/// @param name the name
/// @return its first part
std::string_view first_part(std::string_view name) {
    return name.substr(0, name.find('/'));
}

/// Tells whether an entry is one a system adds when it zips a folder, which
/// a package does not hold: macOS's __MACOSX folder, the Finder's .DS_Store
/// files and AppleDouble files.
///
/// @param name the entry's name
/// @return true when it is ignored
bool ignored_entry(std::string_view name) {
    const std::string first = detail::folded(first_part(name));
    const std::string last = detail::folded(last_part(name));
    return first == mac_attributes_folder || last == finder_file ||
           last.starts_with(apple_double_prefix);
}

/// Maps a zip status to the refusal it gives.
///
/// @param error the zip error
/// @return the problem
Problem problem_of_zip(const zip::ZipError& error) {
    const std::string detail = zip::zip_status_message(error.status);
    switch (error.status) {
    case zip::ZipStatus::no_end_record:
        return problem_of(Refusal::not_zip, {}, detail);
    case zip::ZipStatus::encrypted:
        return problem_of(Refusal::encrypted, error.entry, detail);
    case zip::ZipStatus::unsupported_method:
        return problem_of(Refusal::method, error.entry, detail);
    case zip::ZipStatus::unsafe_name:
    case zip::ZipStatus::duplicate_name:
    case zip::ZipStatus::name_too_long:
    case zip::ZipStatus::bad_name_encoding:
        return problem_of(Refusal::unsafe_name, error.entry, detail);
    case zip::ZipStatus::read_failed:
        return problem_of(Refusal::unreadable, {}, detail);
    default:
        return problem_of(Refusal::damaged, error.entry, detail);
    }
}

/// Reads one file of a package for its kind's manifest reader.
///
/// @param reader the open package
/// @param relative_name the file below the package's top, matched without case
/// @param most_bytes the most it may hold
/// @param[out] bytes the file, when it was read
/// @return missing, read or unreadable
/// The open package a manifest reader asks for another file through.
struct ManifestReader {
    detail::PackageFile* archive{};
    const Package* package{};
};

/// Reads one file of a package for its kind's manifest reader.
///
/// @param reader the open package
/// @param relative_name the file below the package's top, matched without case
/// @param most_bytes the most it may hold
/// @param[out] bytes the file, when it was read
/// @return missing, read or unreadable
EntryRead read_packaged_entry(
    void* reader, std::string_view relative_name, uint64_t most_bytes, std::vector<uint8_t>& bytes
) {
    const auto& state = *static_cast<const ManifestReader*>(reader);
    const std::string wanted = detail::folded(relative_name);
    for (const PackagedFile& packaged : state.package->files) {
        const zip::StreamEntry& entry = state.package->directory.entries[packaged.entry];
        if (entry.directory || detail::folded(packaged.name) != wanted)
            continue;
        if (entry.bytes > most_bytes)
            return EntryRead::unreadable;
        zip::ZipError error{};
        bytes.clear();
        if (!zip::read_stream_entry(
                state.archive->source(), state.archive->bytes, entry, most_bytes, bytes, error
            ))
            return EntryRead::unreadable;
        return EntryRead::read;
    }
    return EntryRead::missing;
}

/// Returns a byte with its ASCII letter lowered.
///
/// @param byte the byte
/// @return the byte
unsigned char lowered(char byte) noexcept {
    const auto value = static_cast<unsigned char>(byte);
    return value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value - 'A' + 'a') : value;
}

/// The names a package unpacks, kept as a tree of their parts compared
/// without case, to find two that differ only in case and a file and a
/// folder of one name, and to list the folders it makes. Each part is kept
/// once, under its folder, so the tree grows with the parts the package
/// names, never with the length of their paths.
class NameTree {
  public:

    /// Adds a path and every folder above it.
    ///
    /// @param path the path, '/' between its parts, without a trailing '/'
    /// @param folder it is a folder
    /// @param[in,out] folders receives each folder added for the first
    ///        time, its parents before it
    /// @return false when a part clashes with one added before
    bool add(std::string_view path, bool folder, std::vector<std::string>& folders) {
        uint32_t parent = 0;
        std::size_t start = 0;
        while (true) {
            const std::size_t end = std::min(path.find('/', start), path.size());
            const bool last = end == path.size();
            const bool part_folder = !last || folder;
            const auto [slot, added] = names_.try_emplace(
                Key{parent, std::string(path.substr(start, end - start))}, Name{next_, part_folder}
            );
            if (added) {
                ++next_;
                if (part_folder)
                    folders.emplace_back(path.substr(0, end));
            } else if (
                slot->first.part != path.substr(start, end - start) ||
                slot->second.folder != part_folder
            ) {
                return false;
            }
            if (last)
                return true;
            parent = slot->second.number;
            start = end + 1;
        }
    }

  private:

    /// A part under its folder.
    struct Key {
        uint32_t parent{}; ///< its folder's number; 0 for the package's top
        std::string part{};
    };

    /// Orders parts by their folder, then without case.
    struct Order {
        bool operator()(const Key& left, const Key& right) const noexcept {
            if (left.parent != right.parent)
                return left.parent < right.parent;
            return std::lexicographical_compare(
                left.part.begin(),
                left.part.end(),
                right.part.begin(),
                right.part.end(),
                [](char a, char b) { return lowered(a) < lowered(b); }
            );
        }
    };

    /// What a part names.
    struct Name {
        uint32_t number{}; ///< its own number, as a folder of later parts
        bool folder{};
    };

    std::map<Key, Name, Order> names_{};
    uint32_t next_{1};
};

} // namespace

PackageResult open_package(const fs::path& file, const PackageOptions& options) {
    const PackageKind* kind = kind_for_file(file);
    if (kind == nullptr)
        return refused(problem_of(Refusal::unknown_kind));
    try {
        detail::PackageFile archive;
        if (!archive.open(file))
            return refused(problem_of(Refusal::unreadable, {}, "the file cannot be opened"));
        Package package{};
        package.kind = kind;
        package.file = file;
        package.file_name = detail::utf8_of(file.filename());
        package.archive_bytes = archive.bytes;
        zip::ZipError error{};
        if (!zip::read_stream_directory(
                archive.source(), archive.bytes, {}, package.directory, error
            ))
            return refused(problem_of_zip(error));
        const auto& entries = package.directory.entries;
        std::vector<std::size_t> kept;
        for (std::size_t index = 0; index < entries.size(); ++index)
            if (!ignored_entry(entries[index].name))
                kept.push_back(index);
        // The manifest lies at the top, or in the one folder the top holds.
        const std::string manifest_name = detail::folded(kind->manifest);
        std::optional<std::size_t> manifest;
        for (const std::size_t index : kept)
            if (!entries[index].directory && detail::folded(entries[index].name) == manifest_name) {
                manifest = index;
                break;
            }
        if (!manifest && !kept.empty()) {
            const std::string_view top = first_part(entries[kept.front()].name);
            const bool one_folder = std::all_of(kept.begin(), kept.end(), [&](std::size_t index) {
                const std::string& name = entries[index].name;
                return name.size() > top.size() && name.starts_with(top) && name[top.size()] == '/';
            });
            if (one_folder) {
                package.root = std::string(top) + "/";
                for (const std::size_t index : kept)
                    if (!entries[index].directory &&
                        detail::folded(entries[index].name) ==
                            detail::folded(package.root) + manifest_name) {
                        manifest = index;
                        break;
                    }
            }
        }
        if (!manifest)
            return refused(problem_of(Refusal::no_manifest));
        package.manifest_entry = *manifest;
        NameTree names{};
        bool backup_left_out = false;
        for (const std::size_t index : kept) {
            const zip::StreamEntry& entry = entries[index];
            const std::string relative = entry.name.substr(package.root.size());
            if (relative.empty())
                continue;
            // A .backup of the package's own is ours to keep, never unpacked.
            if (detail::folded(first_part(relative)) == backup_folder_name) {
                backup_left_out = true;
                continue;
            }
            if (entry.symbolic_link || entry.special_file)
                return refused(problem_of(Refusal::link, relative));
            if (const std::string why = portable_name_problem(relative); !why.empty())
                return refused(problem_of(Refusal::unsafe_name, relative, why));
            // Every folder the path passes through, and the path itself.
            const bool folder = entry.directory;
            const std::string_view path =
                std::string_view(relative).substr(0, relative.size() - (folder ? 1 : 0));
            if (!names.add(path, folder, package.folders))
                return refused(problem_of(Refusal::case_clash, relative));
            if (package.folders.size() > max_package_folders)
                return refused(problem_of(Refusal::too_many_folders));
            package.files.push_back({index, relative});
            if (!folder)
                package.unpacked_bytes += entry.bytes;
        }
        package.backup_left_out = backup_left_out;
        if (package.unpacked_bytes > max_install_bytes) {
            Problem problem = problem_of(Refusal::too_large);
            problem.size_bytes = package.unpacked_bytes;
            problem.limit_bytes = max_install_bytes;
            return refused(std::move(problem));
        }
        if (package.unpacked_bytes > bomb_floor &&
            package.unpacked_bytes / bomb_ratio > package.archive_bytes) {
            Problem problem = problem_of(Refusal::bomb);
            problem.size_bytes = package.unpacked_bytes;
            problem.limit_bytes = package.archive_bytes;
            return refused(std::move(problem));
        }
        // The manifest, read whole: one byte past the kind's limit refuses it.
        std::vector<uint8_t> text;
        if (entries[*manifest].bytes > kind->manifest_most_bytes)
            return refused(problem_of(Refusal::manifest_too_large));
        if (!zip::read_stream_entry(
                archive.source(),
                archive.bytes,
                entries[*manifest],
                kind->manifest_most_bytes,
                text,
                error
            ))
            return refused(problem_of(
                Refusal::damaged, entries[*manifest].name, zip::zip_status_message(error.status)
            ));
        ManifestReader reader{&archive, &package};
        ManifestContext context{};
        context.options = &options;
        context.source = package.file_name + "/" + std::string(kind->manifest);
        context.reader = &reader;
        context.read_entry = read_packaged_entry;
        Problem problem{};
        if (kind->read_manifest == nullptr || !kind->read_manifest(text, context, package, problem))
            return refused(
                kind->read_manifest == nullptr
                    ? problem_of(Refusal::manifest_errors, {}, "the kind reads no manifest")
                    : std::move(problem)
            );
        PackageResult result{};
        result.package = std::move(package);
        return result;
    } catch (const std::exception& failure) {
        return refused(problem_of(Refusal::unreadable, {}, failure.what()));
    }
}

bool same_package(const InstalledPackage& left, const InstalledPackage& right) noexcept {
    return left.kind == right.kind && left.id == right.id && left.version == right.version &&
           left.revision == right.revision;
}

} // namespace oa::app::package_install
