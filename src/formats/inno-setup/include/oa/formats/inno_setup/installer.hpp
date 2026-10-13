// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Reads a Windows installer made by Inno Setup 5.5.7 (Unicode), the kind
// published as one file. It finds the setup loader's offsets, reads the
// setup header and the file list, and streams one installed file out of
// its pieces. Lengths and offsets are checked against the file. A malformed
// file fails without reading past its end. Nothing in the installer is run.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::formats::inno_setup {

/// One installed file, named as it is installed, relative to the install
/// folder. Pieces the installer joins are already one file here.
struct File {
    /// Path with `/` between parts. No drive and no `..`.
    std::string path;
    /// Size in bytes, after the installer's own wrapping is undone.
    uint64_t size{};
    /// Modification time, seconds since 1970. 0 when the installer has none.
    int64_t modified{};
};

/// What reading one installed file is asked to do.
struct ReadRequest {
    /// The path `list` reported.
    std::string_view name;
    /// Leading bytes to discard. The rest are handed to `write`, in order.
    uint64_t skip{};
    /// Receives the next bytes. Returns false to stop the read as a failure.
    /// Null discards the bytes.
    bool (*write)(void* context, std::span<const uint8_t> bytes){};
    /// Passed to `write`.
    void* context{};
    /// Set to stop between blocks. Null never stops. Bytes already handed to
    /// `write` stay handed over.
    const std::atomic<bool>* stop{};
};

/// How far one read got.
struct ReadResult {
    /// Bytes handed to `write`. A skipped prefix is not counted.
    uint64_t produced{};
    /// True when the stop flag was honoured.
    bool stopped{};
};

/// A Windows installer open for reading. The installer itself is only read.
class Reader {
  public:

    /// Prepares a reader with no file open.
    Reader();

    /// Closes the reader.
    ~Reader();

    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    Reader(Reader&&) noexcept = default;
    Reader& operator=(Reader&&) noexcept = default;

    /// Opens one installer and reads its file list.
    ///
    /// A second call reads the file again. On failure nothing stays open.
    ///
    /// @param path the installer
    /// @param[out] error why it could not be opened; may be null
    /// @return true when `list` and `read` can be used
    [[nodiscard]] bool open(const std::filesystem::path& path, std::string* error);

    /// Lists the installed files.
    ///
    /// @param[out] files the files, replaced with the listing
    /// @param[out] error why they could not be listed; may be null
    /// @return true when `files` is the listing
    [[nodiscard]] bool list(std::vector<File>& files, std::string* error) const;

    /// Streams one listed file.
    ///
    /// The installer's checksum is checked before any byte of a piece is
    /// handed to `write`. A piece that is encrypted, split across several
    /// files, or stored in a form this reader does not undo, fails.
    ///
    /// @param request which file, and where its bytes go
    /// @param[out] result how far the read got
    /// @param[out] error why it failed; may be null
    /// @return true when the file was finished or the stop was honoured
    [[nodiscard]] bool read(const ReadRequest& request, ReadResult& result, std::string* error);

  private:

    struct State;
    std::unique_ptr<State> state_;
};

} // namespace oa::formats::inno_setup
