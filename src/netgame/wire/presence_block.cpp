// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/presence_block.hpp"

namespace oa::netgame {

namespace {

/// Writes the five presence bytes at at, with the flags masked to the bits
/// this engine reads.
///
/// @param[out] at the revision byte
/// @param presence the presence; its revision is not 0
void write_presence(uint8_t* at, const PresenceBlock& presence) noexcept {
    at[0] = presence.revision;
    at[1] = presence.major;
    at[2] = presence.minor;
    at[3] = presence.patch;
    at[4] = static_cast<uint8_t>(presence.flags & presence_flag::known);
}

/// Reads one dot-separated decimal number of 0 to 255.
///
/// @param version the text still to read
/// @param[out] value the number
/// @param last true when no dot follows the number
/// @return true when the number was there and, unless last, a dot follows it
bool take_number(std::string_view& version, uint8_t& value, bool last) noexcept {
    if (version.empty() || version.front() < '0' || version.front() > '9')
        return false;
    uint32_t number = 0;
    while (!version.empty() && version.front() >= '0' && version.front() <= '9') {
        const uint32_t digit = static_cast<uint32_t>(version.front() - '0');
        if (number > 25 || (number == 25 && digit > 5))
            return false;
        number = number * 10u + digit;
        version.remove_prefix(1);
    }
    value = static_cast<uint8_t>(number);
    if (last)
        return version.empty();
    if (version.empty() || version.front() != '.')
        return false;
    version.remove_prefix(1);
    return true;
}

} // namespace

PresenceBlock presence_version(std::string_view version, std::string_view label) noexcept {
    PresenceBlock block{};
    block.revision = presence_revision;
    uint8_t major = 0;
    uint8_t minor = 0;
    uint8_t patch = 0;
    const bool parsed = take_number(version, major, false) && take_number(version, minor, false) &&
                        take_number(version, patch, true) && patch != presence_dev_patch;
    if (!parsed || !label.empty()) {
        block.patch = presence_dev_patch;
        if (!parsed)
            return block;
    }
    block.major = major;
    block.minor = minor;
    if (label.empty())
        block.patch = patch;
    return block;
}

void stamp_presence(PlayerInfoRecord& record, const PresenceBlock& block) noexcept {
    if (block.revision == 0)
        return;
    write_presence(
        record.info_tail + (player_info_presence_revision_offset - player_info_tail_offset), block
    );
}

void mark_presence(uint8_t* block, const PresenceBlock& presence) noexcept {
    if (presence.revision == 0)
        return;
    write_presence(block + player_info_presence_revision_offset, presence);
}

std::optional<PresenceBlock> read_presence(const uint8_t* block) noexcept {
    if (!sent_by_open_annihilation(block) || block[player_info_presence_revision_offset] < 1)
        return std::nullopt;
    PresenceBlock presence{};
    presence.revision = block[player_info_presence_revision_offset];
    presence.major = block[player_info_oa_version_offset];
    presence.minor = block[player_info_oa_version_offset + 1];
    presence.patch = block[player_info_oa_version_offset + 2];
    presence.flags =
        static_cast<uint8_t>(block[player_info_presence_flags_offset] & presence_flag::known);
    return presence;
}

} // namespace oa::netgame
