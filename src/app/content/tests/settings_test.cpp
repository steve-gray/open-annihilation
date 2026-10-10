// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Install IDs, the catalogue-update setting and emptying the downloads folder.
#include "oa/app/content/settings.hpp"

#include "oa/test/check.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace {

using oa::app::content::CheckForUpdates;
using oa::app::content::EmptyResult;
using oa::app::content::InstallIdState;
using oa::app::content::content_updates_key;
using oa::app::content::downloaded_bytes;
using oa::app::content::downloads_folder;
using oa::app::content::empty_downloads;
using oa::app::content::ensure_install_id;
using oa::app::content::install_id_key;
using oa::app::content::install_id_shown;
using oa::app::content::install_id_text_valid;
using oa::app::content::make_install_id_text;
using oa::app::content::read_check_for_updates;
using oa::app::content::read_install_id;
using oa::app::content::reset_install_id;
using oa::app::content::turn_install_id_off;
using oa::app::content::turn_install_id_on;
using oa::app::content::write_check_for_updates;
using Values = oa::platform::preferences::Values;

/// Bytes of one fixture file.
constexpr std::size_t mod_bytes = 100;
constexpr std::size_t language_bytes = 40;
constexpr std::size_t map_bytes = 25;
constexpr std::size_t mod_part_bytes = 10;
constexpr std::size_t language_part_bytes = 7;
constexpr std::size_t map_part_bytes = 3;
constexpr std::size_t busy_bytes = 15;
constexpr std::size_t queue_bytes = 11;
constexpr std::size_t notes_bytes = 4;
constexpr std::size_t stray_part_bytes = 6;
constexpr std::size_t nested_bytes = 50;
constexpr std::size_t outside_bytes = 9;
constexpr std::size_t directory_bytes = 8;

/// Reports whether every hyphen of an install ID sits where it must.
bool hyphens_in_place(std::string_view text) {
    return text.size() == 36 && text[8] == '-' && text[13] == '-' && text[18] == '-' &&
           text[23] == '-';
}

/// Writes `bytes` into a new file, and checks that the write succeeded.
void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary);
    OA_CHECK(static_cast<bool>(output));
    if (!bytes.empty())
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    OA_CHECK(static_cast<bool>(output));
}

/// An ID is made once, read back the same, and differs per registry.
void check_made_once() {
    Values values;
    bool changed = false;
    const std::optional<std::string> first = ensure_install_id(values, "ridge", changed);
    OA_CHECK(first.has_value());
    OA_CHECK(changed);
    if (!first)
        return;
    OA_CHECK(first->size() == 36);
    OA_CHECK(hyphens_in_place(*first));
    OA_CHECK(first->find_first_not_of("0123456789abcdef-") == std::string::npos);
    OA_CHECK(install_id_text_valid(*first));
    for (const std::size_t index :
         {std::size_t{0},
          std::size_t{8},
          std::size_t{13},
          std::size_t{18},
          std::size_t{23},
          std::size_t{35}}) {
        const bool hyphen = (*first)[index] == '-';
        OA_CHECK(hyphen == (index == 8 || index == 13 || index == 18 || index == 23));
    }

    changed = true;
    const std::optional<std::string> again = ensure_install_id(values, "ridge", changed);
    OA_CHECK(again == first);
    OA_CHECK(!changed);
    const auto stored = read_install_id(values, "ridge");
    OA_CHECK(stored.state == InstallIdState::on);
    OA_CHECK(stored.text == *first);
    OA_CHECK(values[install_id_key("ridge")] == *first);

    bool other_changed = false;
    const std::optional<std::string> other = ensure_install_id(values, "maps", other_changed);
    OA_CHECK(other_changed);
    OA_CHECK(other.has_value());
    OA_CHECK(other != first);
    OA_CHECK(install_id_key("ridge") == "open-annihilation.install-id.ridge");
    OA_CHECK(install_id_key("ridge").find('|') == std::string::npos);

    const std::optional<std::string> made = make_install_id_text();
    const std::optional<std::string> made_again = make_install_id_text();
    OA_CHECK(made.has_value() && made_again.has_value());
    OA_CHECK(made != made_again);
    OA_CHECK(made && install_id_text_valid(*made));
}

/// Off stays off, reset turns an ID on, and on makes a new one at the next need.
void check_off_reset_and_on() {
    Values values;
    bool changed = false;
    const std::optional<std::string> first = ensure_install_id(values, "ridge", changed);
    OA_CHECK(first.has_value());
    if (!first)
        return;

    turn_install_id_off(values, "ridge");
    OA_CHECK(values[install_id_key("ridge")] == "off");
    OA_CHECK(read_install_id(values, "ridge").state == InstallIdState::off);
    OA_CHECK(read_install_id(values, "ridge").text.empty());
    changed = true;
    const std::optional<std::string> while_off = ensure_install_id(values, "ridge", changed);
    OA_CHECK(!while_off.has_value());
    OA_CHECK(!changed);
    OA_CHECK(values[install_id_key("ridge")] == "off");

    OA_CHECK(reset_install_id(values, "ridge"));
    const auto reset = read_install_id(values, "ridge");
    OA_CHECK(reset.state == InstallIdState::on);
    OA_CHECK(install_id_text_valid(reset.text));
    OA_CHECK(reset.text != *first);
    OA_CHECK(reset.text != "off");

    const std::string before_second_reset = reset.text;
    OA_CHECK(reset_install_id(values, "ridge"));
    const auto reset_again = read_install_id(values, "ridge");
    OA_CHECK(reset_again.state == InstallIdState::on);
    OA_CHECK(install_id_text_valid(reset_again.text));
    OA_CHECK(reset_again.text != before_second_reset);

    turn_install_id_off(values, "ridge");
    const std::string gone = reset_again.text;
    turn_install_id_on(values, "ridge");
    OA_CHECK(values.find(install_id_key("ridge")) == values.end());
    OA_CHECK(read_install_id(values, "ridge").state == InstallIdState::not_made);
    changed = false;
    const std::optional<std::string> renewed = ensure_install_id(values, "ridge", changed);
    OA_CHECK(changed);
    OA_CHECK(renewed.has_value());
    OA_CHECK(renewed != gone);
    OA_CHECK(renewed && install_id_text_valid(*renewed));
}

/// A stored value that is not an ID reads as not made and is replaced.
void check_malformed() {
    Values values;
    values[install_id_key("ridge")] = "not-an-id";
    OA_CHECK(read_install_id(values, "ridge").state == InstallIdState::not_made);
    values[install_id_key("ridge")] = "7F3A90D2-4C18-4B0E-9A77-1D6E5AB0C91E";
    OA_CHECK(!install_id_text_valid(values[install_id_key("ridge")]));
    OA_CHECK(read_install_id(values, "ridge").state == InstallIdState::not_made);
    values[install_id_key("ridge")] = "OFF";
    OA_CHECK(read_install_id(values, "ridge").state == InstallIdState::not_made);

    std::string shifted = "7f3a90d2-4c18-4b0e-9a77-1d6e5ab0c91e";
    shifted[7] = '-';
    shifted[8] = 'a';
    OA_CHECK(shifted.size() == 36);
    OA_CHECK(!install_id_text_valid(shifted));
    values[install_id_key("ridge")] = shifted;
    OA_CHECK(read_install_id(values, "ridge").state == InstallIdState::not_made);
    OA_CHECK(read_install_id(values, "absent").state == InstallIdState::not_made);

    bool changed = false;
    const std::optional<std::string> replaced = ensure_install_id(values, "ridge", changed);
    OA_CHECK(changed);
    OA_CHECK(replaced.has_value());
    OA_CHECK(replaced && install_id_text_valid(*replaced));
    OA_CHECK(values[install_id_key("ridge")] == *replaced);

    const std::string sample = "7f3a90d2-4c18-4b0e-9a77-1d6e5ab0c91e";
    OA_CHECK(install_id_text_valid(sample));
    OA_CHECK(install_id_shown(sample) == "7f3a90d2 \u00b7 \u00b7 \u00b7 c91e");
    OA_CHECK(install_id_shown("not-an-id").empty());
    if (replaced)
        OA_CHECK(
            install_id_shown(*replaced) ==
            replaced->substr(0, 8) + " \u00b7 \u00b7 \u00b7 " + replaced->substr(32)
        );
}

/// The three update settings, the default and a stray word.
void check_updates() {
    Values values;
    OA_CHECK(read_check_for_updates(values) == CheckForUpdates::automatically);

    write_check_for_updates(values, CheckForUpdates::automatically);
    OA_CHECK(values[std::string(content_updates_key)] == "automatically");
    OA_CHECK(read_check_for_updates(values) == CheckForUpdates::automatically);

    write_check_for_updates(values, CheckForUpdates::library_only);
    OA_CHECK(values[std::string(content_updates_key)] == "library");
    OA_CHECK(read_check_for_updates(values) == CheckForUpdates::library_only);

    write_check_for_updates(values, CheckForUpdates::never);
    OA_CHECK(values[std::string(content_updates_key)] == "never");
    OA_CHECK(read_check_for_updates(values) == CheckForUpdates::never);

    values[std::string(content_updates_key)] = "sometimes";
    OA_CHECK(read_check_for_updates(values) == CheckForUpdates::automatically);
    values.erase(std::string(content_updates_key));
    OA_CHECK(read_check_for_updates(values) == CheckForUpdates::automatically);

    const std::filesystem::path data = "player-data";
    OA_CHECK(downloads_folder(data) == data / "content" / "downloads");
}

/// Only package files in the folder itself are measured, and only those not in use go.
void check_downloads() {
    const std::filesystem::path temporary =
        std::filesystem::temp_directory_path() /
        ("oa-content-settings-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{temporary};

    const std::filesystem::path folder = temporary / "downloads";
    const std::filesystem::path nested = folder / "kept-aside";
    const std::filesystem::path outside = temporary / "outside";
    const std::filesystem::path directory = folder / "folder.oamod";
    std::filesystem::create_directories(nested);
    std::filesystem::create_directories(outside);
    std::filesystem::create_directories(directory);

    write_file(folder / "aaa.oamod", std::string(mod_bytes, 'a'));
    write_file(folder / "bbb.oalang", std::string(language_bytes, 'b'));
    write_file(folder / "ccc.oamap", std::string(map_bytes, 'c'));
    write_file(folder / "ddd.oamod.part", std::string(mod_part_bytes, 'd'));
    write_file(folder / "eee.oalang.part", std::string(language_part_bytes, 'e'));
    write_file(folder / "fff.oamap.part", std::string(map_part_bytes, 'f'));
    write_file(folder / "busy.oamod", std::string(busy_bytes, 'g'));
    write_file(folder / "queue.yaml", std::string(queue_bytes, 'q'));
    write_file(folder / "notes.txt", std::string(notes_bytes, 'n'));
    write_file(folder / "foo.part", std::string(stray_part_bytes, 'p'));
    write_file(nested / "nested.oamod", std::string(nested_bytes, 'z'));
    write_file(outside / "nope.oamod", std::string(outside_bytes, 'o'));
    write_file(directory / "inside.txt", std::string(directory_bytes, 'i'));

    constexpr uint64_t expected = mod_bytes + language_bytes + map_bytes + mod_part_bytes +
                                  language_part_bytes + map_part_bytes + busy_bytes;
    OA_CHECK(downloaded_bytes(folder) == expected);
    OA_CHECK(downloaded_bytes(temporary / "missing") == 0);

    const std::filesystem::path in_use[] = {folder / "busy.oamod"};
    const EmptyResult emptied = empty_downloads(folder, in_use);
    OA_CHECK(emptied.removed_bytes == expected - busy_bytes);
    OA_CHECK(emptied.kept == 1);
    OA_CHECK(downloaded_bytes(folder) == busy_bytes);

    OA_CHECK(std::filesystem::is_regular_file(folder / "busy.oamod"));
    OA_CHECK(std::filesystem::is_regular_file(folder / "queue.yaml"));
    OA_CHECK(std::filesystem::file_size(folder / "queue.yaml") == queue_bytes);
    OA_CHECK(std::filesystem::is_regular_file(folder / "notes.txt"));
    OA_CHECK(std::filesystem::is_regular_file(folder / "foo.part"));
    OA_CHECK(std::filesystem::is_directory(directory));
    OA_CHECK(std::filesystem::is_regular_file(directory / "inside.txt"));
    OA_CHECK(std::filesystem::is_regular_file(nested / "nested.oamod"));
    OA_CHECK(std::filesystem::is_regular_file(outside / "nope.oamod"));
    OA_CHECK(!std::filesystem::exists(folder / "aaa.oamod"));
    OA_CHECK(!std::filesystem::exists(folder / "bbb.oalang"));
    OA_CHECK(!std::filesystem::exists(folder / "ccc.oamap"));
    OA_CHECK(!std::filesystem::exists(folder / "ddd.oamod.part"));
    OA_CHECK(!std::filesystem::exists(folder / "eee.oalang.part"));
    OA_CHECK(!std::filesystem::exists(folder / "fff.oamap.part"));

    const EmptyResult missing = empty_downloads(temporary / "missing", {});
    OA_CHECK(missing.removed_bytes == 0);
    OA_CHECK(missing.kept == 0);

    const std::filesystem::path by_name[] = {std::filesystem::path("busy.oamod")};
    const EmptyResult spared = empty_downloads(folder, by_name);
    OA_CHECK(spared.removed_bytes == 0);
    OA_CHECK(spared.kept == 1);
    OA_CHECK(std::filesystem::is_regular_file(folder / "busy.oamod"));
}

} // namespace

int main() {
    try {
        check_made_once();
        check_off_reset_and_on();
        check_malformed();
        check_updates();
        check_downloads();
    } catch (const std::exception& error) {
        std::cerr << "settings test: " << error.what() << '\n';
        return 1;
    }
    return oa::test::check_exit_status();
}
