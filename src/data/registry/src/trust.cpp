// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/registry/trust.hpp"

#include "yaml_text.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace oa::data::registry {
namespace {

#include "pinned_keys.inc"

constexpr std::string_view loopback_host = "127.0.0.1";

char lower_letter(char letter) noexcept {
    return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
}

bool same_id(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lower_letter(left[index]) != lower_letter(right[index]))
            return false;
    }
    return true;
}

/// The name folded for comparison: ASCII spaces trimmed, ASCII letters lowered.
///
/// @param text the name
/// @return the folded name
std::string folded_name(std::string_view text) {
    while (!text.empty() && text.front() == ' ')
        text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ')
        text.remove_suffix(1);
    std::string folded;
    folded.reserve(text.size());
    for (char letter : text)
        folded.push_back(lower_letter(letter));
    return folded;
}

void collect_hosts(const Descriptor& descriptor, std::vector<std::string_view>& hosts) {
    if (!descriptor.catalogue.host.empty())
        hosts.push_back(descriptor.catalogue.host);
    for (const formats::url::Url& mirror : descriptor.mirrors) {
        if (!mirror.host.empty())
            hosts.push_back(mirror.host);
    }
    if (descriptor.api && !descriptor.api->host.empty())
        hosts.push_back(descriptor.api->host);
    if (descriptor.homepage && !descriptor.homepage->host.empty())
        hosts.push_back(descriptor.homepage->host);
}

bool uses_builtin_host(const Descriptor& descriptor, const Descriptor& builtin) {
    std::vector<std::string_view> fresh;
    std::vector<std::string_view> established;
    collect_hosts(descriptor, fresh);
    collect_hosts(builtin, established);
    for (std::string_view host : fresh) {
        for (std::string_view domain : established) {
            if (formats::url::host_within(host, domain))
                return true;
        }
    }
    return false;
}

bool same_key(
    const base::signing::PublicKey& left, const base::signing::PublicKey& right
) noexcept {
    return left == right;
}

AddedRegistry* find_added(PlayerRegistries& registries, std::string_view id) {
    for (AddedRegistry& added : registries.registries) {
        if (same_id(added.descriptor.id, id))
            return &added;
    }
    return nullptr;
}

const AddedRegistry* find_added(const PlayerRegistries& registries, std::string_view id) {
    for (const AddedRegistry& added : registries.registries) {
        if (same_id(added.descriptor.id, id))
            return &added;
    }
    return nullptr;
}

bool keys_are_readable(const std::vector<RegistryKey>& keys, std::string& error) {
    if (keys.size() > max_registry_keys) {
        error = "keys holds more than " + std::to_string(max_registry_keys) + " keys";
        return false;
    }
    for (std::size_t index = 0; index < keys.size(); ++index) {
        if (!valid_key_id(keys[index].id)) {
            error = "keys[" + std::to_string(index) + "].id must be 1 to 64 bytes of a key id";
            return false;
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (keys[earlier].id == keys[index].id) {
                error = "keys[" + std::to_string(index) + "] repeats the id " + keys[index].id;
                return false;
            }
            if (same_key(keys[earlier].key, keys[index].key)) {
                error = "keys[" + std::to_string(index) + "] repeats a public key";
                return false;
            }
        }
    }
    return true;
}

/// Tells whether a built-in file's keys are the pinned set for its id.
///
/// An id with no pinned row is trusted. A pinned text that is not a public
/// key cannot match.
///
/// @param descriptor the file's descriptor
/// @param pinned the pinned keys
/// @param[out] error why the keys do not match
/// @return true when the file may be used
bool pins_match(
    const Descriptor& descriptor, std::span<const PinnedKey> pinned, std::string& error
) {
    std::vector<const PinnedKey*> rows;
    for (const PinnedKey& pin : pinned) {
        if (same_id(pin.registry, descriptor.id))
            rows.push_back(&pin);
    }
    if (rows.empty())
        return true;
    if (rows.size() != descriptor.keys.size()) {
        error = "the keys are not the pinned keys";
        return false;
    }
    std::vector<uint8_t> used(descriptor.keys.size(), 0);
    for (const PinnedKey* pin : rows) {
        const auto parsed = base::signing::parse_public_key(pin->public_key);
        if (!parsed) {
            error = "a pinned key could not be read";
            return false;
        }
        bool found = false;
        for (std::size_t index = 0; index < descriptor.keys.size(); ++index) {
            if (used[index])
                continue;
            if (descriptor.keys[index].id == pin->key_id &&
                same_key(descriptor.keys[index].key, *parsed)) {
                used[index] = 1;
                found = true;
                break;
            }
        }
        if (!found) {
            error = "the keys are not the pinned keys";
            return false;
        }
    }
    return true;
}

void note(std::vector<std::string>* problems, std::string text) {
    if (problems != nullptr)
        problems->push_back(std::move(text));
}

} // namespace

std::span<const PinnedKey> pinned_keys() noexcept {
    return {pinned_key_table.begin(), pinned_key_table.end()};
}

std::vector<Descriptor> read_builtin_registries(
    const std::filesystem::path& folder,
    std::vector<std::string>* problems,
    std::span<const PinnedKey> pinned
) {
    std::vector<Descriptor> kept;
    try {
        std::error_code failure;
        if (!std::filesystem::is_directory(folder, failure)) {
            note(problems, path_text(folder) + " is not a folder of registries");
            return {};
        }
        std::vector<std::filesystem::path> files;
        failure.clear();
        std::filesystem::directory_iterator cursor(folder, failure);
        const std::filesystem::directory_iterator end;
        for (; !failure && cursor != end; cursor.increment(failure)) {
            std::error_code status_error;
            const bool regular = cursor->is_regular_file(status_error);
            if (status_error || !regular)
                continue;
            if (cursor->path().extension() != ".yaml")
                continue;
            files.push_back(cursor->path());
        }
        if (failure)
            note(problems, path_text(folder) + " could not be listed");
        std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
            return left.filename() < right.filename();
        });
        std::vector<std::string> seen;
        for (const std::filesystem::path& file : files) {
            std::vector<uint8_t> bytes;
            std::string failure_text;
            Descriptor descriptor;
            const FileRead read = read_file_bytes(file, bytes, failure_text, max_descriptor_bytes);
            if (read != FileRead::read || !read_descriptor(bytes, descriptor, &failure_text)) {
                note(
                    problems,
                    path_text(file) + " could not be read" +
                        (failure_text.empty() ? "" : ": " + failure_text)
                );
                continue;
            }
            if (!descriptor.is_signed()) {
                note(problems, path_text(file) + " is unsigned");
                continue;
            }
            bool repeated = false;
            for (const std::string& id : seen) {
                if (same_id(id, descriptor.id))
                    repeated = true;
            }
            if (repeated) {
                note(problems, path_text(file) + " repeats the id " + descriptor.id);
                continue;
            }
            if (!pins_match(descriptor, pinned, failure_text)) {
                note(problems, path_text(file) + ": " + failure_text);
                continue;
            }
            seen.push_back(descriptor.id);
            kept.push_back(std::move(descriptor));
        }
    } catch (const std::exception& exception) {
        note(problems, exception.what());
    } catch (...) {
        note(problems, "the registries folder could not be read");
    }
    return kept;
}

const char* refusal_text(Refusal refusal) noexcept {
    switch (refusal) {
    case Refusal::none:
        return "The registry was accepted.";
    case Refusal::unreadable:
        return "The registry could not be read.";
    case Refusal::built_in_id:
        return "This id is a registry that ships with the game.";
    case Refusal::built_in_name:
        return "This name is a registry that ships with the game.";
    case Refusal::built_in_key:
        return "A key is one a registry that ships with the game uses.";
    case Refusal::built_in_host:
        return "An address uses a host of a registry that ships with the game.";
    case Refusal::already_added:
        return "This registry is already added.";
    case Refusal::unsigned_needs_developer_mode:
        return "An unsigned registry needs Developer mode.";
    case Refusal::unsigned_needs_loopback:
        return "An unsigned registry's addresses must use the host 127.0.0.1.";
    case Refusal::too_many_registries:
        return "64 registries are already added.";
    case Refusal::not_added:
        return "This registry is not added.";
    case Refusal::built_in_mirror:
        return "A mirror cannot be added to a registry that ships with the game.";
    case Refusal::bad_mirror:
        return "A mirror must be an http address with no query.";
    case Refusal::mirror_listed:
        return "This mirror is already listed.";
    case Refusal::no_keys:
        return "The trusted keys cannot be empty.";
    }
    return "The registry was refused.";
}

bool loopback_only(const Descriptor& descriptor) noexcept {
    if (descriptor.catalogue.host != loopback_host)
        return false;
    for (const formats::url::Url& mirror : descriptor.mirrors) {
        if (mirror.host != loopback_host)
            return false;
    }
    if (descriptor.api && descriptor.api->host != loopback_host)
        return false;
    return true;
}

Refusal check_new_registry(const Descriptor& descriptor, const AddContext& context) {
    for (const Descriptor& builtin : context.built_ins) {
        if (same_id(descriptor.id, builtin.id))
            return Refusal::built_in_id;
    }
    const std::string name = folded_name(descriptor.name);
    for (const Descriptor& builtin : context.built_ins) {
        if (folded_name(builtin.name) == name)
            return Refusal::built_in_name;
    }
    for (const Descriptor& builtin : context.built_ins) {
        for (const RegistryKey& established : builtin.keys) {
            for (const RegistryKey& key : descriptor.keys) {
                if (same_key(key.key, established.key))
                    return Refusal::built_in_key;
            }
        }
    }
    for (const Descriptor& builtin : context.built_ins) {
        if (uses_builtin_host(descriptor, builtin))
            return Refusal::built_in_host;
    }
    if (context.player != nullptr && find_added(*context.player, descriptor.id) != nullptr)
        return Refusal::already_added;
    if (!descriptor.is_signed()) {
        if (!context.developer_mode)
            return Refusal::unsigned_needs_developer_mode;
        if (!loopback_only(descriptor))
            return Refusal::unsigned_needs_loopback;
    }
    if (context.player != nullptr && context.player->registries.size() >= max_added_registries)
        return Refusal::too_many_registries;
    return Refusal::none;
}

Refusal
check_new_mirror(std::string_view id, const formats::url::Url& url, const AddContext& context) {
    for (const Descriptor& builtin : context.built_ins) {
        if (same_id(builtin.id, id))
            return Refusal::built_in_mirror;
    }
    const AddedRegistry* added =
        context.player == nullptr ? nullptr : find_added(*context.player, id);
    if (added == nullptr)
        return Refusal::not_added;
    if (url.scheme != formats::url::Scheme::http || formats::url::has_query(url))
        return Refusal::bad_mirror;
    for (const formats::url::Url& mirror : added->descriptor.mirrors) {
        if (mirror == url)
            return Refusal::mirror_listed;
    }
    for (const formats::url::Url& mirror : added->player_mirrors) {
        if (mirror == url)
            return Refusal::mirror_listed;
    }
    return Refusal::none;
}

std::vector<RegistryInEffect>
registries_in_effect(std::span<const Descriptor> built_ins, const PlayerRegistries& player) {
    std::vector<RegistryInEffect> effect;
    effect.reserve(built_ins.size() + player.registries.size());
    for (const Descriptor& builtin : built_ins) {
        RegistryInEffect entry;
        entry.descriptor = builtin;
        entry.origin = Origin::built_in;
        entry.enabled = true;
        for (const std::string& disabled : player.disabled) {
            if (same_id(disabled, builtin.id))
                entry.enabled = false;
        }
        effect.push_back(std::move(entry));
    }
    for (const AddedRegistry& added : player.registries) {
        RegistryInEffect entry;
        entry.descriptor = added.descriptor;
        entry.origin = Origin::added;
        entry.enabled = added.enabled;
        entry.player_mirrors = added.player_mirrors;
        entry.url = added.url;
        for (const Descriptor& builtin : built_ins) {
            if (same_id(builtin.id, added.descriptor.id))
                entry.conflicting = true;
        }
        effect.push_back(std::move(entry));
    }
    return effect;
}

Refusal add_registry(
    PlayerRegistries& registries,
    const Descriptor& descriptor,
    std::string url,
    std::string date,
    const AddContext& context
) {
    if (!valid_date(date))
        return Refusal::unreadable;
    std::string stored_url;
    if (!url.empty()) {
        const auto parsed = formats::url::parse_web_url(url);
        if (!parsed)
            return Refusal::unreadable;
        stored_url = formats::url::url_text(*parsed);
    }
    std::string failure;
    if (!descriptor_stores(descriptor, failure))
        return Refusal::unreadable;
    AddContext checked = context;
    checked.player = &registries;
    const Refusal refusal = check_new_registry(descriptor, checked);
    if (refusal != Refusal::none)
        return refusal;
    AddedRegistry added;
    added.descriptor = descriptor;
    added.url = std::move(stored_url);
    added.added = std::move(date);
    added.enabled = true;
    registries.registries.push_back(std::move(added));
    return Refusal::none;
}

Refusal remove_registry(PlayerRegistries& registries, std::string_view id) {
    for (auto cursor = registries.registries.begin(); cursor != registries.registries.end();
         ++cursor) {
        if (!same_id(cursor->descriptor.id, id))
            continue;
        registries.registries.erase(cursor);
        return Refusal::none;
    }
    return Refusal::not_added;
}

Refusal set_enabled(
    PlayerRegistries& registries,
    std::span<const Descriptor> built_ins,
    std::string_view id,
    bool on
) {
    for (const Descriptor& builtin : built_ins) {
        if (!same_id(builtin.id, id))
            continue;
        if (on) {
            std::vector<std::string> kept;
            kept.reserve(registries.disabled.size());
            for (const std::string& disabled : registries.disabled) {
                if (!same_id(disabled, builtin.id))
                    kept.push_back(disabled);
            }
            registries.disabled = std::move(kept);
        } else {
            bool listed = false;
            for (const std::string& disabled : registries.disabled) {
                if (same_id(disabled, builtin.id))
                    listed = true;
            }
            if (!listed)
                registries.disabled.push_back(builtin.id);
        }
        return Refusal::none;
    }
    AddedRegistry* added = find_added(registries, id);
    if (added == nullptr)
        return Refusal::not_added;
    added->enabled = on;
    return Refusal::none;
}

Refusal add_player_mirror(
    PlayerRegistries& registries,
    std::string_view id,
    const formats::url::Url& url,
    const AddContext& context
) {
    AddContext checked = context;
    checked.player = &registries;
    const Refusal refusal = check_new_mirror(id, url, checked);
    if (refusal != Refusal::none)
        return refusal;
    AddedRegistry* added = find_added(registries, id);
    if (added == nullptr)
        return Refusal::not_added;
    added->player_mirrors.push_back(url);
    return Refusal::none;
}

Refusal replace_trusted_keys(
    PlayerRegistries& registries, std::string_view id, std::vector<RegistryKey> keys
) {
    AddedRegistry* added = find_added(registries, id);
    if (added == nullptr)
        return Refusal::not_added;
    if (keys.empty())
        return Refusal::no_keys;
    std::string failure;
    if (!keys_are_readable(keys, failure))
        return Refusal::unreadable;
    std::vector<RegistryKey> previous = added->descriptor.keys;
    added->descriptor.keys = keys;
    if (!descriptor_stores(added->descriptor, failure)) {
        added->descriptor.keys = std::move(previous);
        return Refusal::unreadable;
    }
    return Refusal::none;
}

} // namespace oa::data::registry
