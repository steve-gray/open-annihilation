// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A content source that reads a base game out of the disk image for older
// Macs: the image is recognised by its size and SHA-256, the game folder
// inside it is found from the bundle table, and named files are streamed
// into their bundles. The image is not mounted.
#pragma once

#include "oa/app/content_copy.hpp"
#include "oa/base/sha256.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace oa::app {

/// How a disk image is recognised before it is read.
struct DiskImageOptions {
    /// True when the file must be the known image for older Macs. A test
    /// image sets this false.
    bool require_known_installer{true};
};

/// The game files inside the disk image for older Macs.
class DiskImageContentSource final : public ContentSource {
  public:

    /// Takes the image the player named and the table its files are matched
    /// against. The table has to outlive the source.
    ///
    /// @param image the disk image
    /// @param bundles the bundles a file of the right name and size belongs to
    /// @param options whether the image must be the known one
    DiskImageContentSource(
        std::filesystem::path image,
        std::span<const oa::data::content_bundles::Bundle> bundles,
        DiskImageOptions options = {}
    );

    /// Closes the image.
    ~DiskImageContentSource() override;

    /// A source owns the open image, so it is not copied.
    DiskImageContentSource(const DiskImageContentSource&) = delete;

    /// A source owns the open image, so it is not copied.
    DiskImageContentSource& operator=(const DiskImageContentSource&) = delete;

    /// The image the player named, as the copy's record keeps it.
    ///
    /// @return that path, as text
    [[nodiscard]] std::string origin() const override;

    /// Recognises the image, when required, and finds the game folder.
    ///
    /// @param[out] error why it could not be opened; may be null
    /// @return true when list and stream_file can be used
    [[nodiscard]] bool open(std::string* error) override;

    /// Lists the game folder's files.
    ///
    /// @param[out] files the files, replaced with the listing
    /// @param[out] error why they could not be listed; may be null
    /// @return true when `files` is the listing
    [[nodiscard]] bool list(std::vector<ContentSourceFile>& files, std::string* error) override;

    /// Writes one listed file to `request.destination`, hashing it on the way.
    ///
    /// @param request what to write, and when to stop
    /// @param[out] result how far the write got
    /// @param[out] error why it failed; may be null
    /// @return true when the file was finished or the stop was honoured
    [[nodiscard]] bool
    stream_file(const StreamRequest& request, StreamResult& result, std::string* error) override;

    /// The image has no directory of loose files.
    ///
    /// @return an empty path
    [[nodiscard]] std::filesystem::path disk_root() const override;

    /// Hashes one listed file without writing it.
    ///
    /// @param name the listed name
    /// @param[out] digest the file's SHA-256
    /// @param[out] error why it could not be hashed; may be null
    /// @return true when `digest` is the file's digest
    [[nodiscard]] bool
    hash_file(std::string_view name, oa::base::sha256::Digest& digest, std::string* error);

  private:

    struct State;
    std::filesystem::path image_;
    std::span<const oa::data::content_bundles::Bundle> bundles_{};
    DiskImageOptions options_{};
    std::unique_ptr<State> state_;
};

/// Reads the known disk image and prints each bundle file's size and SHA-256.
///
/// The run does nothing else: it does not copy the image and it does not
/// open a window. A bundle with no file in the image is left out. A file of
/// a bundle that was found, when it is missing or its digest is not one the
/// table knows, fails the check.
///
/// @param image the disk image
/// @return 0 when every bundle file matches, 1 otherwise
[[nodiscard]] int run_installer_check(const std::filesystem::path& image);

} // namespace oa::app
