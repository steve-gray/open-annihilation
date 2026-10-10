// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/catalogue/catalogue.hpp"

#include "oa/base/sha256.hpp"
#include "oa/base/signing/ed25519.hpp"
#include "oa/formats/json.hpp"
#include "oa/formats/oamod/package_keys.hpp"
#include "oa/formats/url.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::data::catalogue {
namespace {

namespace json = formats::json;
namespace oamod = formats::oamod;
namespace sha = base::sha256;
namespace signing = base::signing;
namespace url = formats::url;

/// The most bytes one package may name. 2^32 itself is allowed.
constexpr int64_t max_package_bytes = int64_t{1} << 32;

/// The highest release number. 2^31-1.
constexpr int64_t max_release = 2147483647;

/// The most mirrors one catalogue names.
constexpr std::size_t max_mirrors = 8;

/// The most bytes of a map's title, and of a name under compatible.
constexpr std::size_t max_map_name_bytes = 64;
constexpr std::size_t max_compatible_name_bytes = 256;

/// The most digits kept from a coverage number's significand.
constexpr int max_coverage_digits = 18;

/// Sets `error` when the caller asked for one, and reports failure.
bool refuse(std::string* error, std::string message) {
    if (error)
        *error = std::move(message);
    return false;
}

/// The catalogue's bytes as text. An empty span is an empty view.
std::string_view bytes_as_text(std::span<const uint8_t> bytes) {
    if (bytes.empty())
        return {};
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// Reports whether every object in the tree names each of its members once.
bool members_unique(const json::Json& value) {
    if (value.type() == json::JsonType::array) {
        for (const json::Json& element : value.elements()) {
            if (!members_unique(element))
                return false;
        }
        return true;
    }
    if (value.type() != json::JsonType::object)
        return true;
    const auto names = value.names();
    for (std::size_t index = 0; index < names.size(); ++index) {
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (names[earlier] == names[index])
                return false;
        }
    }
    for (const json::Json& child : value.values()) {
        if (!members_unique(child))
            return false;
    }
    return true;
}

/// Reads `kind` as one of the three package kinds.
bool parse_kind(std::string_view text, Kind& kind) noexcept {
    if (text == "oalang") {
        kind = Kind::oalang;
        return true;
    }
    if (text == "oamod") {
        kind = Kind::oamod;
        return true;
    }
    if (text == "oamap") {
        kind = Kind::oamap;
        return true;
    }
    return false;
}

/// Reports whether `text` is 64 lower-case hexadecimal digits.
bool lower_hex(std::string_view text) noexcept {
    if (text.size() != sha::hex_size)
        return false;
    for (const char value : text) {
        const bool digit = value >= '0' && value <= '9';
        const bool hex = value >= 'a' && value <= 'f';
        if (!digit && !hex)
            return false;
    }
    return true;
}

/// The last path segment of a reference.
std::string_view last_segment(std::string_view path) noexcept {
    const auto slash = path.rfind('/');
    if (slash == std::string_view::npos)
        return path;
    return path.substr(slash + 1);
}

/// Reports whether `file`'s last segment is the package's hash and kind.
bool file_named(std::string_view file, const sha::Digest& digest, Kind kind) {
    const auto hex = sha::to_hex(digest);
    std::string expected(hex.data(), hex.size());
    expected += '.';
    expected += kind_name(kind);
    return last_segment(file) == expected;
}

/// Keeps a text member when it is present and within the bounds.
bool optional_text(
    const json::Json& object,
    std::string_view name,
    std::size_t min_bytes,
    std::size_t max_bytes,
    std::string& out,
    std::string& reason
) {
    const json::Json* value = object.find(name);
    if (!value)
        return true;
    const std::string* text = value->string();
    if (!text || text->size() < min_bytes || text->size() > max_bytes) {
        reason = std::string(name) + " is not " + std::to_string(min_bytes) + " to " +
                 std::to_string(max_bytes) + " bytes";
        return false;
    }
    out = *text;
    return true;
}

/// Requires a text member within the bounds.
bool require_text(
    const json::Json& object,
    std::string_view name,
    std::size_t min_bytes,
    std::size_t max_bytes,
    std::string& out,
    std::string& reason
) {
    if (!object.find(name)) {
        reason = std::string(name) + " is missing";
        return false;
    }
    return optional_text(object, name, min_bytes, max_bytes, out, reason);
}

/// Reads a list of tags, or of language tags when `language` is set.
bool read_text_list(
    const json::Json& object,
    std::string_view name,
    std::size_t max_count,
    bool language,
    std::vector<std::string>& out,
    std::string& reason
) {
    const json::Json* value = object.find(name);
    if (!value)
        return true;
    if (value->type() != json::JsonType::array) {
        reason = std::string(name) + " is not a list";
        return false;
    }
    if (value->elements().size() > max_count) {
        reason = std::string(name) + " lists more than " + std::to_string(max_count);
        return false;
    }
    for (const json::Json& element : value->elements()) {
        const std::string* text = element.string();
        const bool ok =
            text && (language ? valid_package_key(Kind::oalang, *text) : oamod::tag_valid(*text));
        if (!ok) {
            reason = std::string(language ? "locale \"" : "tag \"");
            if (text)
                reason += *text;
            reason +=
                language ? std::string("\" is not a language tag") : std::string("\" is not a tag");
            return false;
        }
        out.push_back(*text);
    }
    return true;
}

/// Reads `requires` when the package names it.
bool read_requires(const json::Json& object, Package& package, std::string& reason) {
    const json::Json* value = object.find("requires");
    if (!value)
        return true;
    if (value->type() != json::JsonType::object) {
        reason = "requires is not an object";
        return false;
    }
    if (const json::Json* engine = value->find("engine")) {
        const std::string* text = engine->string();
        if (!text) {
            reason = "requires.engine is not text";
            return false;
        }
        std::string error;
        auto range = oamod::parse_engine_range(*text, &error);
        if (!range) {
            reason = "requires.engine " + error;
            return false;
        }
        package.requires_engine_text = *text;
        package.requires_engine = std::move(*range);
    }
    if (const json::Json* base = value->find("base")) {
        const std::string* text = base->string();
        if (!text) {
            reason = "requires.base is not text";
            return false;
        }
        package.requires_base = *text;
    }
    return true;
}

/// Reports whether a hack id is 1 to 64 bytes of the hack-id alphabet.
bool valid_hack_id(std::string_view text) noexcept {
    if (text.empty() || text.size() > 64)
        return false;
    for (const char value : text) {
        const bool word = (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9');
        if (!word && value != '.' && value != '-')
            return false;
    }
    return true;
}

/// Reads `hacks` when the package names it.
bool read_hacks(const json::Json& object, Package& package, std::string& reason) {
    const json::Json* value = object.find("hacks");
    if (!value)
        return true;
    if (value->type() != json::JsonType::object) {
        reason = "hacks is not an object";
        return false;
    }
    const auto whole = [](const json::Json* field,
                          std::optional<int32_t>& out,
                          std::string& reason,
                          std::string_view name) {
        if (!field)
            return true;
        const auto number = field->integer();
        if (!number || *number < 0 || *number > max_release) {
            reason = std::string(name) + " is not a whole number";
            return false;
        }
        out = static_cast<int32_t>(*number);
        return true;
    };
    if (!whole(value->find("sim"), package.hacks_sim, reason, "hacks.sim") ||
        !whole(value->find("view"), package.hacks_view, reason, "hacks.view"))
        return false;
    const json::Json* ids = value->find("ids");
    if (!ids)
        return true;
    if (ids->type() != json::JsonType::array) {
        reason = "hacks.ids is not a list";
        return false;
    }
    if (ids->elements().size() > 256) {
        reason = "hacks.ids lists more than 256";
        return false;
    }
    for (const json::Json& element : ids->elements()) {
        const std::string* text = element.string();
        if (!text || !valid_hack_id(*text)) {
            reason = "hack id";
            if (text)
                reason += " \"" + *text + "\"";
            reason += " is not a hack id";
            return false;
        }
        package.hack_ids.push_back(*text);
    }
    return true;
}

/// Reads decimal digits into a coverage significand, stopping after 18 digits.
bool take_digits(std::string_view text, std::size_t& index, uint64_t& significand, int& digits) {
    const std::size_t start = index;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
        if (digits >= max_coverage_digits)
            return false;
        significand = significand * 10 + static_cast<uint64_t>(text[index] - '0');
        ++digits;
        ++index;
    }
    return index > start;
}

/// Reads a coverage number from its JSON spelling. The value is from 0 to 1.
bool coverage_number(std::string_view text, double& out) {
    if (text.empty() || text.front() == '-' || text.front() == '+')
        return false;
    std::size_t index = 0;
    if (text[index] < '0' || text[index] > '9')
        return false;
    uint64_t significand = 0;
    int digits = 0;
    if (text[index] == '0')
        ++index;
    else if (!take_digits(text, index, significand, digits))
        return false;
    const int integer_digits = digits;
    int fraction_digits = 0;
    if (index < text.size() && text[index] == '.') {
        ++index;
        if (!take_digits(text, index, significand, digits))
            return false;
        fraction_digits = digits - integer_digits;
    }
    int exponent = 0;
    if (index < text.size() && (text[index] == 'e' || text[index] == 'E')) {
        ++index;
        int sign = 1;
        if (index < text.size() && (text[index] == '+' || text[index] == '-')) {
            if (text[index] == '-')
                sign = -1;
            ++index;
        }
        if (index >= text.size() || text[index] < '0' || text[index] > '9')
            return false;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
            if (exponent > 10000)
                return false;
            exponent = exponent * 10 + (text[index] - '0');
            ++index;
        }
        exponent *= sign;
    }
    if (index != text.size())
        return false;
    if (significand == 0) {
        out = 0;
        return true;
    }
    const long long power = static_cast<long long>(exponent) - fraction_digits;
    if (power >= 0) {
        if (significand == 1 && power == 0) {
            out = 1;
            return true;
        }
        return false;
    }
    const long long denominator = -power;
    if (denominator > max_coverage_digits) {
        // At most 18 significand digits, so this is below 1.
        out = 0;
        if (denominator <= 350) {
            double value = static_cast<double>(significand);
            for (long long step = 0; step < denominator; ++step)
                value /= 10;
            out = value;
        }
        return true;
    }
    uint64_t scale = 1;
    for (long long step = 0; step < denominator; ++step)
        scale *= 10;
    if (significand > scale)
        return false;
    out = static_cast<double>(significand) / static_cast<double>(scale);
    return true;
}

/// Reads one side of a map's `<width>x<height>`. No leading zero, 1 to 256.
bool map_side(std::string_view text, int32_t& out) noexcept {
    if (text.empty() || text.size() > 3 || (text.size() > 1 && text.front() == '0'))
        return false;
    int value = 0;
    for (const char byte : text) {
        if (byte < '0' || byte > '9')
            return false;
        value = value * 10 + (byte - '0');
    }
    if (value < 1 || value > 256)
        return false;
    out = value;
    return true;
}

/// Reads a map's size, one `x` between the width and the height.
bool parse_map_size(std::string_view text, int32_t& width, int32_t& height) noexcept {
    const auto mark = text.find('x');
    if (mark == std::string_view::npos || text.find('x', mark + 1) != std::string_view::npos)
        return false;
    return map_side(text.substr(0, mark), width) && map_side(text.substr(mark + 1), height);
}

/// Reads one map. A map that names another package fails the whole entry.
bool read_map(
    const json::Json& value, std::string_view package_id, MapEntry& entry, std::string& reason
) {
    if (value.type() != json::JsonType::object) {
        reason = "map is not an object";
        return false;
    }
    const json::Json* map = value.find("map");
    const std::string* map_text = map ? map->string() : nullptr;
    const auto at = map_text ? map_text->find('@') : std::string::npos;
    const bool one_at = map_text && at != std::string::npos && at != 0 &&
                        map_text->find('@', at + 1) == std::string::npos;
    const std::string_view suffix =
        one_at ? std::string_view(*map_text).substr(at + 1) : std::string_view{};
    if (!one_at || suffix != package_id) {
        reason = "map names another package";
        return false;
    }
    entry.map = *map_text;
    entry.stem = map_text->substr(0, at);
    if (!require_text(value, "name", 1, max_map_name_bytes, entry.name, reason)) {
        reason = "map name is not 1 to 64 bytes";
        return false;
    }
    const json::Json* players = value.find("players");
    const auto player_count = players ? players->integer() : std::nullopt;
    if (!player_count || *player_count < 1 || *player_count > 10) {
        reason = "players is not 1 to 10";
        return false;
    }
    entry.players = static_cast<int32_t>(*player_count);
    const json::Json* size = value.find("size");
    const std::string* size_text = size ? size->string() : nullptr;
    if (!size_text || !parse_map_size(*size_text, entry.width, entry.height)) {
        reason = "map size is not width x height";
        return false;
    }
    if (const json::Json* preview = value.find("preview")) {
        const std::string* text = preview->string();
        const bool png =
            text && text->size() >= 4 && text->compare(text->size() - 4, 4, ".png") == 0;
        if (!text || !valid_reference(*text) || !png) {
            reason = "preview is not a picture reference";
            return false;
        }
        entry.preview = *text;
    }
    if (const json::Json* compatible = value.find("compatible")) {
        if (compatible->type() != json::JsonType::object) {
            reason = "compatible is not names and booleans";
            return false;
        }
        const auto names = compatible->names();
        const auto values = compatible->values();
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (names[index].empty() || names[index].size() > max_compatible_name_bytes) {
                reason = "compatible name is not 1 to 256 bytes";
                return false;
            }
            const auto flag = values[index].boolean();
            if (!flag) {
                reason = "compatible is not names and booleans";
                return false;
            }
            entry.compatible.emplace_back(names[index], *flag);
        }
    }
    return true;
}

/// Reads the fields only a language pack carries. Other kinds ignore them.
bool read_language(const json::Json& value, Package& package, std::string& reason) {
    if (const json::Json* coverage = value.find("coverage")) {
        const std::string* text = coverage->number_text();
        double number = 0;
        if (!text || !coverage_number(*text, number)) {
            reason = "coverage is not a number from 0 to 1";
            return false;
        }
        package.coverage = number;
    }
    if (!optional_text(value, "english_name", 1, 64, package.english_name, reason) ||
        !optional_text(value, "word", 1, 32, package.word, reason))
        return false;
    if (!read_text_list(value, "locales", 16, true, package.locales, reason) ||
        !read_text_list(value, "fallbacks", 8, true, package.fallbacks, reason))
        return false;
    if (const json::Json* needs = value.find("needs")) {
        const std::string* text = needs->string();
        if (!text || (*text != "game-fonts" && *text != "modern-fonts")) {
            reason = "needs is not game-fonts or modern-fonts";
            return false;
        }
        package.needs = *text;
    }
    return true;
}

/// Reads one package. The first broken rule is the reason the entry is left out.
bool read_package(const json::Json& value, Package& package, std::string& reason) {
    if (value.type() != json::JsonType::object) {
        reason = "a package is not an object";
        return false;
    }
    const json::Json* kind_value = value.find("kind");
    const std::string* kind_text = kind_value ? kind_value->string() : nullptr;
    Kind kind = Kind::oamod;
    if (!kind_text || !parse_kind(*kind_text, kind)) {
        reason = "unknown kind";
        return false;
    }
    package.kind = kind;
    const json::Json* id_value = value.find("id");
    const std::string* id_text = id_value ? id_value->string() : nullptr;
    if (!id_text || !valid_package_key(kind, *id_text)) {
        const std::string shown = id_text ? *id_text : std::string{};
        if (id_text && id_text->find('@') != std::string::npos)
            reason = "id \"" + shown + "\" is not a package key";
        else if (kind == Kind::oalang)
            reason = "id \"" + shown + "\" is not a language tag";
        else
            reason = "id \"" + shown + "\" is not lower-case kebab-case";
        return false;
    }
    package.id = *id_text;
    if (!require_text(value, "name", 1, 64, package.name, reason))
        return false;
    const json::Json* release = value.find("release");
    const auto release_number = release ? release->integer() : std::nullopt;
    if (!release_number || *release_number < 1 || *release_number > max_release) {
        reason = "release is not a whole number from 1 to 2147483647";
        return false;
    }
    package.release = *release_number;
    const json::Json* size = value.find("size");
    const auto size_number = size ? size->integer() : std::nullopt;
    if (!size_number || *size_number < 1 || *size_number > max_package_bytes) {
        reason = "size is not a whole number of bytes from 1 to 4294967296";
        return false;
    }
    package.size = static_cast<uint64_t>(*size_number);
    const json::Json* hash = value.find("sha256");
    const std::string* hash_text = hash ? hash->string() : nullptr;
    if (!hash_text || !lower_hex(*hash_text)) {
        reason = "sha256 is not 64 lower-case hex digits";
        return false;
    }
    package.sha256 = *sha::parse_hex(*hash_text);
    const json::Json* file = value.find("file");
    const std::string* file_text = file ? file->string() : nullptr;
    if (!file_text || !valid_reference(*file_text)) {
        reason = "file is not a reference";
        return false;
    }
    if (!file_named(*file_text, package.sha256, kind)) {
        reason = "file not named by sha256";
        return false;
    }
    package.file = *file_text;
    if (kind == Kind::oamod) {
        if (!require_text(value, "version", 1, 32, package.version, reason))
            return false;
    } else if (!optional_text(value, "version", 1, 32, package.version, reason)) {
        return false;
    }
    const json::Json* revision = value.find("revision");
    if (kind == Kind::oamod || revision) {
        const auto number = revision ? revision->integer() : std::nullopt;
        if (!number || *number < 1 || *number > 65535) {
            reason = "revision is not a whole number from 1 to 65535";
            return false;
        }
        package.revision = static_cast<int32_t>(*number);
    }
    if (!optional_text(value, "publisher", 1, 64, package.publisher, reason) ||
        !optional_text(value, "author", 1, 64, package.author, reason) ||
        !optional_text(value, "summary", 0, 200, package.summary, reason) ||
        !optional_text(value, "notes", 0, 2000, package.notes, reason))
        return false;
    if (const json::Json* homepage = value.find("homepage")) {
        const std::string* text = homepage->string();
        if (!text || !oamod::homepage_valid(*text)) {
            reason = "homepage is not an address the game can open";
            return false;
        }
        package.homepage = *text;
    }
    if (!read_text_list(value, "tags", 16, false, package.tags, reason))
        return false;
    if (const json::Json* badge = value.find("badge")) {
        const std::string* text = badge->string();
        const bool png =
            text && text->size() >= 4 && text->compare(text->size() - 4, 4, ".png") == 0;
        if (!text || !valid_reference(*text) || !png) {
            reason = "badge is not a picture reference";
            return false;
        }
        package.badge = *text;
    }
    if (!read_requires(value, package, reason))
        return false;
    if (const json::Json* sim_hash = value.find("sim_hash")) {
        const std::string* text = sim_hash->string();
        const auto digest = text ? sha::parse_hex(*text) : std::nullopt;
        if (!digest) {
            reason = "sim_hash is not 64 hex digits";
            return false;
        }
        package.sim_hash = *digest;
    }
    if (!read_hacks(value, package, reason))
        return false;
    if (kind == Kind::oalang && !read_language(value, package, reason))
        return false;
    if (kind == Kind::oamap) {
        const json::Json* maps = value.find("maps");
        if (!maps || maps->type() != json::JsonType::array || maps->elements().empty()) {
            reason = "map pack names no map";
            return false;
        }
        for (const json::Json& map : maps->elements()) {
            MapEntry entry;
            if (!read_map(map, package.id, entry, reason))
                return false;
            package.maps.push_back(std::move(entry));
        }
    }
    return true;
}

/// Keeps a skip reason until max_problems reasons are kept.
void keep_problem(Catalogue& catalogue, std::size_t index, std::string_view reason) {
    if (catalogue.problems.size() >= max_problems)
        return;
    catalogue.problems.push_back("packages[" + std::to_string(index) + "]: " + std::string(reason));
}

/// The package key folded to lower case, for the duplicate check.
std::string fold_key(std::string_view id) {
    std::string folded(id);
    for (char& value : folded) {
        if (value >= 'A' && value <= 'Z')
            value = static_cast<char>(value - 'A' + 'a');
    }
    return folded;
}

/// Reads the signing keys. Unknown members of a key object are ignored.
bool read_keys(
    const json::Json& array, std::vector<registry::RegistryKey>& out, std::string& error
) {
    if (array.elements().size() > registry::max_registry_keys) {
        error = "keys names more than 8";
        return false;
    }
    for (std::size_t index = 0; index < array.elements().size(); ++index) {
        const json::Json& value = array.elements()[index];
        const std::string where = "keys[" + std::to_string(index) + "]";
        if (value.type() != json::JsonType::object) {
            error = where + " is not an object";
            return false;
        }
        const json::Json* id_value = value.find("id");
        const json::Json* public_value = value.find("public");
        const std::string* id_text = id_value ? id_value->string() : nullptr;
        const std::string* public_text = public_value ? public_value->string() : nullptr;
        if (!id_text || !registry::valid_key_id(*id_text)) {
            error = where + " has no key id";
            return false;
        }
        const auto parsed = public_text ? signing::parse_public_key(*public_text) : std::nullopt;
        if (!parsed) {
            error = where + " is not a public key";
            return false;
        }
        for (const registry::RegistryKey& existing : out) {
            if (existing.id == *id_text) {
                error = where + " repeats an id";
                return false;
            }
            if (existing.key == *parsed) {
                error = where + " repeats a public key";
                return false;
            }
        }
        out.push_back(registry::RegistryKey{*id_text, *parsed});
    }
    return true;
}

/// Reads the mirror list. Each address is http and has no query.
bool read_mirrors(const json::Json& array, std::vector<url::Url>& out, std::string& error) {
    if (array.elements().size() > max_mirrors) {
        error = "mirrors names more than 8";
        return false;
    }
    for (std::size_t index = 0; index < array.elements().size(); ++index) {
        const std::string* text = array.elements()[index].string();
        const auto parsed = text ? url::parse_http_url(*text) : std::nullopt;
        if (!text || !parsed || url::has_query(*parsed)) {
            error = "mirrors[" + std::to_string(index) + "] is not an http address without a query";
            return false;
        }
        out.push_back(*parsed);
    }
    return true;
}

/// Reads one UTC member into the text and the seconds.
bool read_time(
    const json::Json& object,
    std::string_view name,
    std::string& text_out,
    int64_t& seconds,
    std::string& error
) {
    const json::Json* value = object.find(name);
    const std::string* text = value ? value->string() : nullptr;
    const auto parsed = text ? parse_utc_time(*text) : std::nullopt;
    if (!parsed) {
        error = std::string(name) + " is not a UTC time";
        return false;
    }
    text_out = *text;
    seconds = *parsed;
    return true;
}

/// One package that passed its own rules, with its index in the array.
struct Kept {
    Package package;
    std::size_t index = 0;
};

} // namespace

bool read_catalogue(std::span<const uint8_t> bytes, Catalogue& out, std::string* error) {
    json::JsonError json_error;
    const auto root = json::parse_json(bytes_as_text(bytes), json_error);
    if (!root)
        return refuse(error, "the catalogue is not JSON");
    if (root->type() != json::JsonType::object)
        return refuse(error, "the catalogue is not a JSON object");
    if (!members_unique(*root))
        return refuse(error, "a member is named twice");

    Catalogue loaded;
    const json::Json* version = root->find("catalogue");
    const auto version_number = version ? version->integer() : std::nullopt;
    if (!version_number || *version_number != catalogue_version)
        return refuse(error, "catalogue is not 1");
    const json::Json* registry_value = root->find("registry");
    const std::string* registry_text = registry_value ? registry_value->string() : nullptr;
    if (!registry_text || !registry::valid_registry_id(*registry_text))
        return refuse(error, "registry is not a registry id");
    loaded.registry = *registry_text;
    const json::Json* sequence = root->find("sequence");
    if (!sequence)
        return refuse(error, "sequence is missing");
    const auto sequence_number = sequence->integer();
    if (!sequence_number || *sequence_number < 0)
        return refuse(error, "sequence is not a whole number at least 0");
    loaded.sequence = *sequence_number;
    std::string time_error;
    if (!read_time(*root, "generated", loaded.generated_text, loaded.generated, time_error) ||
        !read_time(*root, "expires", loaded.expires_text, loaded.expires, time_error))
        return refuse(error, std::move(time_error));
    if (loaded.expires <= loaded.generated)
        return refuse(error, "expires is not after generated");
    if (const json::Json* downloads = root->find("downloads")) {
        const std::string* text = downloads->string();
        const auto parsed = text ? url::parse_http_url(*text) : std::nullopt;
        if (!parsed)
            return refuse(error, "downloads is not an http address");
        loaded.downloads = url::url_text(*parsed);
    }
    if (const json::Json* keys = root->find("keys")) {
        if (keys->type() != json::JsonType::array)
            return refuse(error, "keys is not a list");
        std::string key_error;
        if (!read_keys(*keys, loaded.keys, key_error))
            return refuse(error, std::move(key_error));
    }
    if (const json::Json* mirrors = root->find("mirrors")) {
        if (mirrors->type() != json::JsonType::array)
            return refuse(error, "mirrors is not a list");
        std::string mirror_error;
        if (!read_mirrors(*mirrors, loaded.mirrors, mirror_error))
            return refuse(error, std::move(mirror_error));
    }
    const json::Json* packages = root->find("packages");
    if (!packages || packages->type() != json::JsonType::array)
        return refuse(error, "packages is missing");
    if (packages->elements().size() > max_packages)
        return refuse(error, "packages lists more than 20000");

    std::vector<Kept> kept;
    kept.reserve(packages->elements().size());
    for (std::size_t index = 0; index < packages->elements().size(); ++index) {
        Package package;
        std::string reason;
        if (!read_package(packages->elements()[index], package, reason)) {
            keep_problem(loaded, index, reason);
            continue;
        }
        kept.push_back(Kept{std::move(package), index});
    }
    std::map<std::string, std::vector<std::size_t>> by_key;
    for (std::size_t index = 0; index < kept.size(); ++index)
        by_key[fold_key(kept[index].package.id)].push_back(index);
    std::vector<bool> drop(kept.size());
    for (const auto& [key, indexes] : by_key) {
        if (indexes.size() < 2)
            continue;
        for (const std::size_t index : indexes) {
            drop[index] = true;
            keep_problem(loaded, kept[index].index, "same key \"" + key + "\"");
        }
    }
    for (std::size_t index = 0; index < kept.size(); ++index) {
        if (!drop[index])
            loaded.packages.push_back(std::move(kept[index].package));
    }
    out = std::move(loaded);
    return true;
}

} // namespace oa::data::catalogue
