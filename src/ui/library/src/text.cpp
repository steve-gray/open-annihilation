// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's sizes, ages, versions, rules hashes and engine requirements,
// written from the patterns in text.hpp.
#include "oa/ui/library/text.hpp"

#include <cstddef>
#include <string>

namespace oa::ui::library {

namespace {

/// Bytes in a KiB, MiB and GiB.
constexpr uint64_t kib = uint64_t{1} << 10;
constexpr uint64_t mib = uint64_t{1} << 20;
constexpr uint64_t gib = uint64_t{1} << 30;

/// Below this size, MB are written to one decimal.
constexpr uint64_t one_decimal_mb_limit = 100 * mib;

/// Seconds in a minute, an hour and a day, and the age from which days are counted.
constexpr int64_t seconds_per_minute = 60;
constexpr int64_t seconds_per_hour = 60 * seconds_per_minute;
constexpr int64_t seconds_per_day = 24 * seconds_per_hour;
constexpr int64_t hours_limit = 2 * seconds_per_day;

/// How many hexadecimal digits of a rules hash its text shows, and how many make one group.
constexpr std::size_t rules_text_digits = 8;
constexpr std::size_t rules_group_digits = 4;

/// Divides, rounding halves up.
///
/// @param value the dividend
/// @param unit the divisor, not zero, at most 2^62
/// @return value / unit, rounded to the nearest whole number, halves up
uint64_t rounded(uint64_t value, uint64_t unit) {
    const uint64_t rest = value % unit;
    return value / unit + (2 * rest >= unit ? 1 : 0);
}

/// Divides to tenths, rounding halves up.
///
/// @param value the dividend
/// @param unit the divisor, not zero, at most 2^32
/// @return value / unit in tenths
uint64_t rounded_tenths(uint64_t value, uint64_t unit) {
    const uint64_t whole = value / unit;
    const uint64_t rest = value % unit;
    return whole * 10 + (rest * 10 + unit / 2) / unit;
}

/// Writes tenths as a number with one decimal.
///
/// @param tenths the number, in tenths
/// @param text how texts are looked up
/// @return such as "41.0"
std::string tenths_text(uint64_t tenths, const TextHooks& text) {
    const std::string whole = std::to_string(tenths / 10);
    const std::string tenth = std::to_string(tenths % 10);
    return filled(text, words::one_decimal, {{"whole", whole}, {"tenth", tenth}});
}

/// A size's number and its unit's pattern.
struct SizeParts {
    std::string number{};
    std::string_view pattern{};
};

/// How a size is written in the unit and precision a reference size takes.
enum class SizeScale : uint8_t { kilobytes, megabytes_tenths, megabytes, gigabytes_tenths };

/// Chooses how a size is written.
///
/// @param bytes the size
/// @return its scale
SizeScale scale_of(uint64_t bytes) {
    if (bytes < mib)
        return SizeScale::kilobytes;
    if (bytes < one_decimal_mb_limit)
        return SizeScale::megabytes_tenths;
    if (bytes < gib)
        return SizeScale::megabytes;
    return SizeScale::gigabytes_tenths;
}

/// Writes a size at a scale.
///
/// @param bytes the size
/// @param scale the unit and precision
/// @param at_least_one a size in KB is at least 1
/// @param text how texts are looked up
/// @return the number and the unit's pattern
SizeParts size_parts(uint64_t bytes, SizeScale scale, bool at_least_one, const TextHooks& text) {
    switch (scale) {
    case SizeScale::kilobytes: {
        uint64_t whole = rounded(bytes, kib);
        if (at_least_one && whole == 0)
            whole = 1;
        return {std::to_string(whole), words::size_kb};
    }
    case SizeScale::megabytes_tenths:
        return {tenths_text(rounded_tenths(bytes, mib), text), words::size_mb};
    case SizeScale::megabytes:
        return {std::to_string(rounded(bytes, mib)), words::size_mb};
    case SizeScale::gigabytes_tenths:
        return {tenths_text(rounded_tenths(bytes, gib), text), words::size_gb};
    }
    return {};
}

/// The words for one comparison of an engine requirement.
///
/// @param comparison the comparison
/// @param compact the short words
/// @return the pattern, with a {version} place
std::string_view comparison_pattern(formats::oamod::EngineComparison comparison, bool compact) {
    using formats::oamod::EngineComparison;
    switch (comparison) {
    case EngineComparison::at_least:
        return compact ? words::engine_at_least_short : words::engine_at_least;
    case EngineComparison::above:
        return compact ? words::engine_above_short : words::engine_above;
    case EngineComparison::at_most:
        return compact ? words::engine_at_most_short : words::engine_at_most;
    case EngineComparison::below:
        return compact ? words::engine_below_short : words::engine_below;
    case EngineComparison::exactly:
        return compact ? words::engine_exactly_short : words::engine_exactly;
    }
    return words::engine_at_least;
}

/// Describes an engine requirement.
///
/// @param range the requirement
/// @param compact the short words
/// @param text how texts are looked up
/// @return its comparisons, joined
std::string
range_text(const formats::oamod::EngineRange& range, bool compact, const TextHooks& text) {
    std::string joined;
    for (const auto& [comparison, version] : range.terms) {
        if (!joined.empty())
            joined += shown(text, words::list_separator);
        const std::string number = formats::oamod::engine_version_text(version);
        joined += filled(text, comparison_pattern(comparison, compact), {{"version", number}});
    }
    return joined;
}

/// Writes a version and its revision above 1.
///
/// @param version the version, as written
/// @param revision the revision; 0 when none
/// @param text how texts are looked up
/// @return such as "4.8 · rev 3", "4.8" or "rev 3"
std::string
version_and_revision(std::string_view version, int64_t revision, const TextHooks& text) {
    if (revision <= 1)
        return std::string(version);
    const std::string number = std::to_string(revision);
    if (version.empty())
        return filled(text, words::revision_short, {{"revision", number}});
    return filled(text, words::version_with_revision, {{"version", version}, {"revision", number}});
}

} // namespace

std::string_view shown(const TextHooks& text, std::string_view english) {
    if (text.shown == nullptr)
        return english;
    return text.shown(text.context, english);
}

std::string
filled(const TextHooks& text, std::string_view pattern, std::initializer_list<Place> places) {
    const std::string_view source = shown(text, pattern);
    std::string result;
    std::size_t at = 0;
    while (at < source.size()) {
        const std::size_t open = source.find('{', at);
        const std::size_t close = open == std::string_view::npos ? open : source.find('}', open);
        if (close == std::string_view::npos) {
            result.append(source.substr(at));
            break;
        }
        result.append(source.substr(at, open - at));
        const std::string_view name = source.substr(open + 1, close - open - 1);
        bool found = false;
        for (const auto& [place, value] : places) {
            if (place == name) {
                result.append(value);
                found = true;
                break;
            }
        }
        if (!found)
            result.append(source.substr(open, close - open + 1));
        at = close + 1;
    }
    return result;
}

std::string size_text(uint64_t bytes, const TextHooks& text) {
    const SizeParts parts = size_parts(bytes, scale_of(bytes), true, text);
    return filled(text, parts.pattern, {{"size", parts.number}});
}

std::string progress_text(uint64_t done, uint64_t total, const TextHooks& text) {
    const SizeScale scale = scale_of(total);
    const SizeParts done_parts = size_parts(done, scale, false, text);
    const std::string whole = size_text(total, text);
    return filled(text, words::progress, {{"done", done_parts.number}, {"total", whole}});
}

std::string age_text(int64_t seconds, const TextHooks& text) {
    if (seconds < seconds_per_minute)
        return std::string(shown(text, words::age_just_now));
    if (seconds < seconds_per_hour) {
        const std::string count = std::to_string(seconds / seconds_per_minute);
        return filled(text, words::age_minutes, {{"count", count}});
    }
    if (seconds < hours_limit) {
        const std::string count = std::to_string(seconds / seconds_per_hour);
        return filled(text, words::age_hours, {{"count", count}});
    }
    const std::string count = std::to_string(seconds / seconds_per_day);
    return filled(text, words::age_days, {{"count", count}});
}

std::string header_age_text(const Inputs& inputs, const TextHooks& text) {
    std::string line;
    if (inputs.list_age_seconds < 0) {
        line = shown(text, words::header_no_list);
    } else {
        const std::string age = age_text(inputs.list_age_seconds, text);
        line = filled(text, words::header_list_from, {{"age", age}});
    }
    if (inputs.offline)
        line = filled(text, words::header_offline, {{"list", line}});
    return line;
}

std::string rules_text(const base::sha256::Digest& digest) {
    const auto digits = base::sha256::to_hex(digest);
    std::string written;
    for (std::size_t index = 0; index < rules_text_digits; ++index) {
        if (index != 0 && index % rules_group_digits == 0)
            written += ' ';
        written += digits[index];
    }
    return written;
}

std::string version_text(const Listing& listing, const TextHooks& text) {
    std::string written = version_and_revision(listing.version, listing.revision, text);
    if (listing.kind != Kind::map_pack)
        return written;
    const std::string count = std::to_string(listing.maps.size());
    const std::string maps = filled(
        text, listing.maps.size() == 1 ? words::map_count_one : words::map_count, {{"count", count}}
    );
    if (written.empty())
        return maps;
    return filled(text, words::version_with_maps, {{"version", written}, {"maps", maps}});
}

std::string version_text(const Installed& installed, const TextHooks& text) {
    return version_and_revision(installed.version, installed.revision, text);
}

ChangeTexts
change_texts(const Installed& installed, const UpdateNote& update, const TextHooks& text) {
    ChangeTexts names;
    if (installed.version == update.to_version) {
        const std::string from = std::to_string(installed.revision);
        const std::string to = std::to_string(update.to_revision);
        names.from = filled(text, words::revision_long, {{"revision", from}});
        names.to = filled(text, words::revision_long, {{"revision", to}});
        names.from_short = filled(text, words::revision_short, {{"revision", from}});
        names.to_short = filled(text, words::revision_short, {{"revision", to}});
        return names;
    }
    names.from = installed.version;
    names.to = update.to_version;
    names.from_short = installed.version;
    names.to_short = update.to_version;
    return names;
}

std::string engine_text(const formats::oamod::EngineRange& range, const TextHooks& text) {
    return range_text(range, false, text);
}

std::string engine_short_text(const formats::oamod::EngineRange& range, const TextHooks& text) {
    return range_text(range, true, text);
}

std::string_view tab_text(Tab tab, bool compact, const TextHooks& text) {
    switch (tab) {
    case Tab::mods:
        return shown(text, words::tab_mods);
    case Tab::maps:
        return shown(text, words::tab_maps);
    case Tab::languages:
        return shown(text, compact ? words::tab_languages_short : words::tab_languages);
    case Tab::updates:
        return shown(text, compact ? words::tab_updates_short : words::tab_updates);
    }
    return {};
}

std::string_view filter_text(Filter filter, const TextHooks& text) {
    switch (filter) {
    case Filter::all:
        return shown(text, words::filter_all);
    case Filter::installed:
        return shown(text, words::filter_installed);
    case Filter::updates:
        return shown(text, words::filter_updates);
    }
    return {};
}

std::string_view state_text(State state, const TextHooks& text) {
    switch (state) {
    case State::get:
        return shown(text, words::state_get);
    case State::installed:
        return shown(text, words::state_installed);
    case State::update:
        return shown(text, words::state_update);
    case State::playing:
        return shown(text, words::state_playing);
    }
    return {};
}

std::string_view action_text(Action action, const TextHooks& text) {
    switch (action) {
    case Action::get:
        return shown(text, words::action_get);
    case Action::update:
        return shown(text, words::action_update);
    case Action::update_all:
        return shown(text, words::action_update_all);
    case Action::cancel:
        return shown(text, words::action_cancel);
    case Action::retry:
        return shown(text, words::action_retry);
    case Action::roll_back:
        return shown(text, words::action_roll_back);
    case Action::play_now:
        return shown(text, words::action_play_now);
    case Action::open_folder:
        return shown(text, words::action_open_folder);
    case Action::homepage:
        return shown(text, words::action_homepage);
    case Action::details:
        return shown(text, words::action_details);
    case Action::back:
        return shown(text, words::action_back);
    case Action::settings:
        return shown(text, words::action_settings);
    case Action::close:
        return shown(text, words::action_close);
    }
    return {};
}

} // namespace oa::ui::library
