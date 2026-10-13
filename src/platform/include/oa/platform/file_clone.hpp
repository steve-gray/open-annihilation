// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Sharing a file's disk blocks with a second name, where the volume can do
// that, so a copy of the game takes no extra space. The bytes are always
// read from the source; this never writes the source, and it writes the
// destination only when the share succeeds.
#pragma once

#include <filesystem>
#include <string>

namespace oa::platform {

/// Reports whether a copy into `destination_directory` can share the source
/// file's disk blocks.
///
/// That is the same volume, and a file system that shares blocks: APFS,
/// btrfs, XFS or ReFS. `destination_directory` has to exist; a missing folder
/// is reported as unable to share. A failure to ask the system is the same.
///
/// @param source the file that would be copied
/// @param destination_directory the folder the copy would be written in
/// @return true when a share can be tried
[[nodiscard]] bool blocks_can_be_shared(
    const std::filesystem::path& source, const std::filesystem::path& destination_directory
) noexcept;

/// Gives `destination` the same disk blocks as `source`.
///
/// `destination` must not exist, and its folder must. The source is only
/// read. When the volume cannot share blocks, or the share fails, the
/// destination is left absent and the function returns false, so a byte copy
/// can write it afterwards.
///
/// @param source the file to share
/// @param destination the new name, which must not exist
/// @param[out] error why the blocks were not shared; may be null
/// @return true when `destination` names the same blocks
[[nodiscard]] bool share_file_blocks(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::string* error
) noexcept;

} // namespace oa::platform
