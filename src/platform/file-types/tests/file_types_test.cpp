// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The registration of the four file types: the texts it writes; on every system but Windows the
// XDG files in a scratch data folder, written once and only again when they differ, the
// database tools run once after a change (recording scripts on a scratch PATH), missing tools
// and a tool that hangs, each run again at the next start, and the starts that register
// nothing; on Windows the registry values under a scratch key of HKEY_CURRENT_USER, written
// once and only again when they differ, and a copy in the temporary folder that registers
// nothing. No test registers anything for the user who runs it.

#include "oa/platform/file_types.hpp"

#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <stdint.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlwapi.h>
#endif

#ifndef OA_PROCESS_SPAWNING
#error "OA_PROCESS_SPAWNING (0 or 1) says whether the game may start other programs"
#endif

namespace {

namespace fs = std::filesystem;
namespace file_types = oa::platform::file_types;

void test_desktop_exec_argument() {
    OA_CHECK(
        file_types::desktop_exec_argument("/opt/Open Annihilation/open-annihilation") ==
        "\"/opt/Open Annihilation/open-annihilation\""
    );
    // A quote, a backtick and a dollar sign take a backslash, which the string rule doubles.
    OA_CHECK(file_types::desktop_exec_argument("/a\"b") == "\"/a\\\\\"b\"");
    OA_CHECK(file_types::desktop_exec_argument("/a`b") == "\"/a\\\\`b\"");
    OA_CHECK(file_types::desktop_exec_argument("/a$b") == "\"/a\\\\$b\"");
    // A backslash takes one, and both are doubled: four in all.
    OA_CHECK(file_types::desktop_exec_argument("/a\\b") == "\"/a\\\\\\\\b\"");
    OA_CHECK(file_types::desktop_exec_argument("/100%") == "\"/100%%\"");
    OA_CHECK(file_types::desktop_exec_argument("/caf\xC3\xA9") == "\"/caf\xC3\xA9\"");
    OA_CHECK(file_types::desktop_exec_argument("/a\nb").empty());
    OA_CHECK(file_types::desktop_exec_argument("/a\x7F").empty());
    OA_CHECK(file_types::desktop_exec_argument("/caf\xE9").empty());
    OA_CHECK(file_types::desktop_exec_argument("/\xC0\xAF").empty());
}

void test_texts() {
    const std::string entry =
        file_types::desktop_entry(fs::path("/opt/Open Annihilation/open-annihilation"));
    OA_CHECK(
        entry == "[Desktop Entry]\n"
                 "Type=Application\n"
                 "Name=Open Annihilation\n"
                 "Comment=Plays Total Annihilation from your own game files\n"
                 "Exec=\"/opt/Open Annihilation/open-annihilation\" %f\n"
                 "TryExec=/opt/Open Annihilation/open-annihilation\n"
                 "Icon=net.coreprime.open-annihilation\n"
                 "Terminal=false\n"
                 "Categories=Game;StrategyGame;\n"
                 "MimeType=application/x-oamod;application/x-oalang;application/x-oamap;"
                 "application/x-oareg;\n"
                 "NoDisplay=true\n"
    );
    OA_CHECK(
        file_types::desktop_entry(fs::path("/a\\b")).find("TryExec=/a\\\\b\n") != std::string::npos
    );
    OA_CHECK(file_types::desktop_entry(fs::path("/a\tb")).empty());
    OA_CHECK(
        file_types::mime_package() ==
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
        "  <mime-type type=\"application/x-oamod\">\n"
        "    <comment>Open Annihilation mod</comment>\n"
        "    <sub-class-of type=\"application/zip\"/>\n"
        "    <glob pattern=\"*.oamod\"/>\n"
        "  </mime-type>\n"
        "  <mime-type type=\"application/x-oalang\">\n"
        "    <comment>Open Annihilation language pack</comment>\n"
        "    <sub-class-of type=\"application/zip\"/>\n"
        "    <glob pattern=\"*.oalang\"/>\n"
        "  </mime-type>\n"
        "  <mime-type type=\"application/x-oamap\">\n"
        "    <comment>Open Annihilation map pack</comment>\n"
        "    <sub-class-of type=\"application/zip\"/>\n"
        "    <glob pattern=\"*.oamap\"/>\n"
        "  </mime-type>\n"
        "  <mime-type type=\"application/x-oareg\">\n"
        "    <comment>Open Annihilation registry</comment>\n"
        "    <sub-class-of type=\"text/plain\"/>\n"
        "    <glob pattern=\"*.oareg\"/>\n"
        "  </mime-type>\n"
        "</mime-info>\n"
    );
#ifdef _WIN32
    const fs::path executable(L"C:\\Games\\Open Annihilation\\open-annihilation.exe");
    const std::string spelled = "C:\\Games\\Open Annihilation\\open-annihilation.exe";
#else
    const fs::path executable("/opt/Open Annihilation/open-annihilation");
    const std::string spelled = "/opt/Open Annihilation/open-annihilation";
#endif
    OA_CHECK(file_types::open_command(executable) == "\"" + spelled + "\" --open \"%1\"");
    OA_CHECK(file_types::default_icon(executable) == spelled + ",0");
}

void test_running_executable() {
    const std::optional<fs::path> executable = file_types::running_executable();
#if defined(_WIN32) || defined(__linux__)
    OA_CHECK(executable.has_value());
    if (executable) {
        std::error_code error;
        OA_CHECK(executable->is_absolute());
        OA_CHECK(fs::is_regular_file(*executable, error));
        OA_CHECK(executable->filename().string().find("oa-platform-file-types-test") == 0);
    }
#else
    OA_CHECK(!executable.has_value());
#endif
}

#ifndef _WIN32
/// Reports whether `text` begins with `start`.
bool starts_with(const std::string& text, const std::string& start) {
    return text.compare(0, start.size(), start) == 0;
}

/// Reports whether any line holds `part`.
bool any_line_holds(const std::vector<std::string>& lines, const std::string& part) {
    for (const std::string& line : lines)
        if (line.find(part) != std::string::npos)
            return true;
    return false;
}

/// Reads a whole file.
std::string read_file(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

/// Writes a whole file.
void write_file(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream << text;
}

/// Writes a shell script and makes it runnable.
void write_script(const fs::path& file, const std::string& body) {
    write_file(file, "#!/bin/sh\n" + body);
    fs::permissions(
        file,
        fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
            fs::perms::others_read | fs::perms::others_exec
    );
}

/// Sets an environment variable for the rest of a case and puts it back afterwards.
class ScopedVariable {
  public:

    ScopedVariable(const char* name, const char* value) : name_(name) {
        if (const char* old = std::getenv(name))
            old_ = std::string(old);
        if (value != nullptr)
            setenv(name, value, 1);
        else
            unsetenv(name);
    }

    ~ScopedVariable() {
        if (old_)
            setenv(name_.c_str(), old_->c_str(), 1);
        else
            unsetenv(name_.c_str());
    }

    ScopedVariable(const ScopedVariable&) = delete;
    ScopedVariable& operator=(const ScopedVariable&) = delete;

  private:

    std::string name_{};
    std::optional<std::string> old_{};
};

/// A scratch folder with a data home, a folder of recording tools and the file they write.
struct XdgScratch {
    fs::path root{};
    fs::path data{};
    fs::path tools{};
    fs::path calls{};
    fs::path empty{};
    fs::path hanging{};
};

/// Makes the scratch folder: three tools that each add a line naming themselves and their
/// arguments to the calls file, a folder with no tools, and one whose desktop tool hangs.
XdgScratch make_xdg_scratch() {
    XdgScratch scratch{};
    scratch.root = oa::test::make_scratch_directory("oa-platform-file-types");
    scratch.data = scratch.root / "data";
    scratch.tools = scratch.root / "tools";
    scratch.calls = scratch.root / "calls.txt";
    scratch.empty = scratch.root / "no-tools";
    scratch.hanging = scratch.root / "hanging-tools";
    const std::string record = "printf '%s' \"${0##*/}\" >> '" + scratch.calls.string() +
                               "'\n"
                               "for argument in \"$@\"; do printf ' %s' \"$argument\" >> '" +
                               scratch.calls.string() +
                               "'; done\n"
                               "printf '\\n' >> '" +
                               scratch.calls.string() + "'\n";
    for (const char* tool :
         {"update-mime-database", "update-desktop-database", "gtk-update-icon-cache"})
        write_script(scratch.tools / tool, record);
    fs::create_directories(scratch.empty);
    write_script(
        scratch.hanging / "update-desktop-database", "PATH=/bin:/usr/bin\nexec sleep 60\n"
    );
    return scratch;
}

/// Returns the calls the recording tools wrote, one a line.
std::vector<std::string> calls_of(const XdgScratch& scratch) {
    std::vector<std::string> calls;
    std::istringstream text(read_file(scratch.calls));
    for (std::string line; std::getline(text, line);)
        calls.push_back(line);
    return calls;
}

/// The most milliseconds a recording tool may run: generous, so that a machine under load never
/// stops one.
constexpr uint32_t recording_tool_limit_ms = 120000;

/// Returns bytes standing for an icon; the registration copies them as they are.
std::vector<uint8_t> icon_bytes(uint8_t first) {
    std::vector<uint8_t> bytes(300);
    for (std::size_t index = 0; index < bytes.size(); ++index)
        bytes[index] = static_cast<uint8_t>(first + index);
    return bytes;
}

void test_xdg_registration() {
    const XdgScratch scratch = make_xdg_scratch();
    const ScopedVariable path("PATH", scratch.tools.c_str());
    const ScopedVariable flatpak("FLATPAK_ID", nullptr);
    const ScopedVariable snap("SNAP", nullptr);
    file_types::Places places{};
    places.data_home = scratch.data;
    places.tool_time_limit_ms = recording_tool_limit_ms;
    const fs::path executable("/opt/Open Annihilation/open-annihilation");
    const std::vector<uint8_t> icon = icon_bytes(1);
    const std::string data = scratch.data.string();
    const fs::path program_icon =
        scratch.data / "icons/hicolor/256x256/apps/net.coreprime.open-annihilation.png";
    const fs::path type_icons[] = {
        scratch.data / "icons/hicolor/256x256/mimetypes/application-x-oamod.png",
        scratch.data / "icons/hicolor/256x256/mimetypes/application-x-oalang.png",
        scratch.data / "icons/hicolor/256x256/mimetypes/application-x-oamap.png",
        scratch.data / "icons/hicolor/256x256/mimetypes/application-x-oareg.png",
    };
    const fs::path package = scratch.data / "mime/packages/net.coreprime.open-annihilation.xml";
    const fs::path entry = scratch.data / "applications/net.coreprime.open-annihilation.desktop";
    const auto icons_match = [&](const std::vector<uint8_t>& bytes) {
        const std::string text(bytes.begin(), bytes.end());
        if (read_file(program_icon) != text)
            return false;
        for (const fs::path& type_icon : type_icons)
            if (read_file(type_icon) != text)
                return false;
        return true;
    };

    // The first start writes the five icon files, the MIME package and the desktop entry, and
    // runs the two database tools once each.
    file_types::Registration done = file_types::register_xdg(executable, icon, places);
    OA_CHECK(done.supported);
    OA_CHECK(done.changed);
    OA_CHECK(done.error.empty());
    OA_CHECK(icons_match(icon));
    OA_CHECK(read_file(package) == file_types::mime_package());
    OA_CHECK(read_file(entry) == file_types::desktop_entry(executable));
    std::vector<std::string> calls = calls_of(scratch);
#if OA_PROCESS_SPAWNING
    OA_CHECK(calls.size() == 2);
    if (calls.size() == 2) {
        OA_CHECK(calls[0] == "update-mime-database " + data + "/mime");
        OA_CHECK(calls[1] == "update-desktop-database " + data + "/applications");
    }
    OA_CHECK(any_line_holds(done.lines, "ran update-mime-database"));
#else
    OA_CHECK(calls.empty());
#endif
    // No temporary file is left beside the files.
    for (const auto& item : fs::recursive_directory_iterator(scratch.data))
        OA_CHECK(item.path().extension() != ".tmp");

    // The next start finds everything in place, writes nothing and runs nothing.
    done = file_types::register_xdg(executable, icon, places);
    OA_CHECK(done.supported);
    OA_CHECK(!done.changed);
    OA_CHECK(done.error.empty());
    OA_CHECK(done.lines.empty());
    OA_CHECK(calls_of(scratch).size() == calls.size());

    // A copy started from elsewhere rewrites the desktop entry alone, and runs its tool alone.
    const fs::path moved("/srv/games/open-annihilation");
    done = file_types::register_xdg(moved, icon, places);
    OA_CHECK(done.changed);
    OA_CHECK(done.error.empty());
    OA_CHECK(read_file(entry) == file_types::desktop_entry(moved));
    OA_CHECK(any_line_holds(done.lines, "wrote " + entry.string()));
    OA_CHECK(!any_line_holds(done.lines, "wrote " + package.string()));
    OA_CHECK(!any_line_holds(done.lines, ".png"));
#if OA_PROCESS_SPAWNING
    calls = calls_of(scratch);
    OA_CHECK(calls.size() == 3);
    if (calls.size() == 3)
        OA_CHECK(calls[2] == "update-desktop-database " + data + "/applications");
#endif

    // A new icon where the user's icon cache exists makes the cache again.
    write_file(scratch.data / "icons/hicolor/icon-theme.cache", "cache");
    const std::vector<uint8_t> new_icon = icon_bytes(7);
    done = file_types::register_xdg(moved, new_icon, places);
    OA_CHECK(done.changed);
    OA_CHECK(icons_match(new_icon));
#if OA_PROCESS_SPAWNING
    calls = calls_of(scratch);
    OA_CHECK(calls.size() == 4);
    if (calls.size() == 4)
        OA_CHECK(
            calls[3] ==
            "gtk-update-icon-cache --force --ignore-theme-index --quiet " + data + "/icons/hicolor"
        );
#endif

    // Tools that are not installed are no error, and the next start that has them runs them,
    // and only them.
    {
        const ScopedVariable no_tools("PATH", scratch.empty.c_str());
        done = file_types::register_xdg(executable, icon, places);
        OA_CHECK(done.changed);
        OA_CHECK(done.error.empty());
#if OA_PROCESS_SPAWNING
        OA_CHECK(any_line_holds(done.lines, "update-desktop-database is not installed"));
        OA_CHECK(any_line_holds(done.lines, "gtk-update-icon-cache is not installed"));
#endif
    }
    done = file_types::register_xdg(executable, icon, places);
    OA_CHECK(!done.changed && done.error.empty());
#if OA_PROCESS_SPAWNING
    calls = calls_of(scratch);
    OA_CHECK(calls.size() == 6);
    if (calls.size() == 6) {
        OA_CHECK(calls[4] == "update-desktop-database " + data + "/applications");
        OA_CHECK(starts_with(calls[5], "gtk-update-icon-cache "));
    }
    done = file_types::register_xdg(executable, icon, places);
    OA_CHECK(done.lines.empty() && calls_of(scratch).size() == 6);
#endif

    // Without the tools asked for, none runs.
    places.run_tools = false;
    const std::size_t before = calls_of(scratch).size();
    done = file_types::register_xdg(moved, icon, places);
    OA_CHECK(done.changed);
    OA_CHECK(calls_of(scratch).size() == before);
    places.run_tools = true;

#if OA_PROCESS_SPAWNING
    // A tool that hangs is stopped at the limit, and the next start runs it again.
    {
        const ScopedVariable hanging("PATH", scratch.hanging.c_str());
        places.tool_time_limit_ms = 1000;
        const auto started = std::chrono::steady_clock::now();
        done = file_types::register_xdg(executable, icon, places);
        const auto took = std::chrono::steady_clock::now() - started;
        OA_CHECK(done.changed);
        OA_CHECK(done.error.empty());
        OA_CHECK(any_line_holds(done.lines, "update-desktop-database did not finish in 1000 ms"));
        OA_CHECK(took >= std::chrono::milliseconds(1000));
        // Stopped, not waited for: the tool would sleep a minute.
        OA_CHECK(took < std::chrono::seconds(50));
        places.tool_time_limit_ms = recording_tool_limit_ms;
    }
    const std::size_t stopped = calls_of(scratch).size();
    done = file_types::register_xdg(executable, icon, places);
    OA_CHECK(!done.changed && done.error.empty());
    calls = calls_of(scratch);
    OA_CHECK(calls.size() == stopped + 1);
    if (calls.size() == stopped + 1)
        OA_CHECK(calls.back() == "update-desktop-database " + data + "/applications");
#endif
    std::error_code error;
    fs::remove_all(scratch.root, error);
}

void test_xdg_skips() {
    const XdgScratch scratch = make_xdg_scratch();
    const ScopedVariable path("PATH", scratch.tools.c_str());
    const ScopedVariable snap("SNAP", nullptr);
    file_types::Places places{};
    places.data_home = scratch.data;
    const std::vector<uint8_t> icon = icon_bytes(1);
    const auto nothing_written = [&] {
        return !fs::exists(scratch.data) && !fs::exists(scratch.calls);
    };

    {
        const ScopedVariable flatpak("FLATPAK_ID", "net.example.Game");
        const file_types::Registration done =
            file_types::register_xdg("/opt/Open Annihilation/open-annihilation", icon, places);
        OA_CHECK(done.supported);
        OA_CHECK(!done.changed);
        OA_CHECK(done.error.empty());
        OA_CHECK(done.lines.size() == 1 && starts_with(done.lines[0], "skipped: a Flatpak"));
    }
    const ScopedVariable flatpak("FLATPAK_ID", nullptr);
    {
        const ScopedVariable in_snap("SNAP", "/snap/example/1");
        const file_types::Registration done =
            file_types::register_xdg("/opt/Open Annihilation/open-annihilation", icon, places);
        OA_CHECK(!done.changed && done.lines.size() == 1);
    }
    // A copy run from the temporary folder, as one run from inside an archive is.
    file_types::Registration done = file_types::register_xdg(
        fs::temp_directory_path() / "unpacked" / "open-annihilation", icon, places
    );
    OA_CHECK(!done.changed && done.error.empty());
    OA_CHECK(done.lines.size() == 1 && done.lines[0].find("temporary folder") != std::string::npos);
    done = file_types::register_xdg("/tmp/unpacked/open-annihilation", icon, places);
    OA_CHECK(!done.changed && done.lines.size() == 1);
    // A path a desktop entry cannot hold.
    done = file_types::register_xdg("/opt/a\nb/open-annihilation", icon, places);
    OA_CHECK(!done.changed && done.error.empty() && done.lines.size() == 1);
    done = file_types::register_xdg("/opt/caf\xE9/open-annihilation", icon, places);
    OA_CHECK(!done.changed && done.error.empty() && done.lines.size() == 1);
    // A relative program path, or data folder, is an error.
    done = file_types::register_xdg("open-annihilation", icon, places);
    OA_CHECK(!done.changed && !done.error.empty());
    file_types::Places relative{};
    relative.data_home = "data";
    done = file_types::register_xdg("/opt/Open Annihilation/open-annihilation", icon, relative);
    OA_CHECK(!done.changed && !done.error.empty());
    OA_CHECK(nothing_written());
    std::error_code error;
    fs::remove_all(scratch.root, error);
}

void test_xdg_data_folder() {
    const XdgScratch scratch = make_xdg_scratch();
    const ScopedVariable path("PATH", scratch.tools.c_str());
    const ScopedVariable flatpak("FLATPAK_ID", nullptr);
    const ScopedVariable snap("SNAP", nullptr);
    file_types::Places places{};
    places.run_tools = false;
    const fs::path executable("/opt/Open Annihilation/open-annihilation");
    const std::vector<uint8_t> icon = icon_bytes(1);
    const fs::path entry_name = "applications/net.coreprime.open-annihilation.desktop";
    {
        const ScopedVariable data_home("XDG_DATA_HOME", (scratch.root / "xdg").c_str());
        const file_types::Registration done = file_types::register_xdg(executable, icon, places);
        OA_CHECK(done.changed && done.error.empty());
        OA_CHECK(fs::exists(scratch.root / "xdg" / entry_name));
    }
    {
        // A relative XDG_DATA_HOME is passed over for the home folder's.
        const ScopedVariable data_home("XDG_DATA_HOME", "relative");
        const ScopedVariable home("HOME", (scratch.root / "home").c_str());
        const file_types::Registration done = file_types::register_xdg(executable, icon, places);
        OA_CHECK(done.changed && done.error.empty());
        OA_CHECK(fs::exists(scratch.root / "home/.local/share" / entry_name));
    }
    {
        const ScopedVariable data_home("XDG_DATA_HOME", nullptr);
        const ScopedVariable home("HOME", nullptr);
        const file_types::Registration done = file_types::register_xdg(executable, icon, places);
        OA_CHECK(!done.changed && !done.error.empty());
    }
    std::error_code error;
    fs::remove_all(scratch.root, error);
}

void test_registration_by_system() {
    const XdgScratch scratch = make_xdg_scratch();
    const ScopedVariable path("PATH", scratch.tools.c_str());
    const ScopedVariable flatpak("FLATPAK_ID", nullptr);
    const ScopedVariable snap("SNAP", nullptr);
    file_types::Places places{};
    places.data_home = scratch.data;
    const std::vector<uint8_t> icon = icon_bytes(1);
    const file_types::Registration done =
        file_types::register_file_types("/opt/Open Annihilation/open-annihilation", icon, places);
#if defined(__linux__)
    OA_CHECK(done.supported && done.changed && done.error.empty());
    OA_CHECK(fs::exists(scratch.data / "applications/net.coreprime.open-annihilation.desktop"));
#else
    // The bundle registers the type: nothing is done at run time.
    OA_CHECK(!done.supported && !done.changed && done.lines.empty() && done.error.empty());
    OA_CHECK(!fs::exists(scratch.data));
#endif
    std::error_code error;
    fs::remove_all(scratch.root, error);
}

/// A subset registers only its types. Withdrawing one leaves the others, and a package and an
/// icon another program wrote. With every type off, the desktop entry and the MIME package go.
void test_xdg_subset_and_withdrawal() {
    const XdgScratch scratch = make_xdg_scratch();
    const ScopedVariable path("PATH", scratch.tools.c_str());
    const ScopedVariable flatpak("FLATPAK_ID", nullptr);
    const ScopedVariable snap("SNAP", nullptr);
    file_types::Places places{};
    places.data_home = scratch.data;
    places.tool_time_limit_ms = recording_tool_limit_ms;
    const fs::path executable("/opt/Open Annihilation/open-annihilation");
    const std::vector<uint8_t> icon = icon_bytes(1);
    file_types::TypeSet packages;
    packages.language = false;
    packages.registry = false;
    const fs::path package = scratch.data / "mime/packages/net.coreprime.open-annihilation.xml";
    const fs::path entry = scratch.data / "applications/net.coreprime.open-annihilation.desktop";
    const fs::path program_icon =
        scratch.data / "icons/hicolor/256x256/apps/net.coreprime.open-annihilation.png";
    const fs::path mod_icon =
        scratch.data / "icons/hicolor/256x256/mimetypes/application-x-oamod.png";
    const fs::path map_icon =
        scratch.data / "icons/hicolor/256x256/mimetypes/application-x-oamap.png";
    const fs::path language_icon =
        scratch.data / "icons/hicolor/256x256/mimetypes/application-x-oalang.png";
    const fs::path registry_icon =
        scratch.data / "icons/hicolor/256x256/mimetypes/application-x-oareg.png";

    file_types::Registration done = file_types::register_xdg(executable, icon, places, packages);
    OA_CHECK(done.supported && done.changed && done.error.empty());
    OA_CHECK(read_file(package) == file_types::mime_package(packages));
    OA_CHECK(read_file(entry) == file_types::desktop_entry(executable, packages));
    OA_CHECK(fs::exists(program_icon));
    OA_CHECK(fs::exists(mod_icon));
    OA_CHECK(fs::exists(map_icon));
    OA_CHECK(!fs::exists(language_icon));
    OA_CHECK(!fs::exists(registry_icon));

    // Another program's package and icon, beside this game's, stay when a type is withdrawn.
    const fs::path other_package = scratch.data / "mime/packages/other.xml";
    const fs::path other_icon =
        scratch.data / "icons/hicolor/256x256/mimetypes/application-x-other.png";
    write_file(other_package, "<mime-info>other</mime-info>\n");
    write_file(other_icon, "other-icon");
    file_types::TypeSet maps;
    maps.mod = false;
    maps.language = false;
    maps.registry = false;
    done = file_types::withdraw_xdg(maps, places);
    OA_CHECK(done.supported && done.changed && done.error.empty());
    file_types::TypeSet mods;
    mods.language = false;
    mods.map = false;
    mods.registry = false;
    OA_CHECK(read_file(package) == file_types::mime_package(mods));
    OA_CHECK(read_file(entry) == file_types::desktop_entry(executable, mods));
    OA_CHECK(!fs::exists(map_icon));
    OA_CHECK(fs::exists(mod_icon));
    OA_CHECK(fs::exists(program_icon));
    OA_CHECK(read_file(other_package) == "<mime-info>other</mime-info>\n");
    OA_CHECK(read_file(other_icon) == "other-icon");
    // A second withdrawal of a type already gone changes nothing of what remains.
    done = file_types::withdraw_xdg(maps, places);
    OA_CHECK(done.supported && !done.changed && done.error.empty());
    OA_CHECK(read_file(package) == file_types::mime_package(mods));

    // With every type off, the desktop entry and the MIME package are removed, and so are the
    // stamps a later registration's tools read. The program's icon stays.
    places.run_tools = false;
    file_types::TypeSet none;
    none.mod = false;
    none.language = false;
    none.map = false;
    none.registry = false;
    done = file_types::register_xdg(executable, icon, places, none);
    OA_CHECK(done.supported && done.changed && done.error.empty());
    OA_CHECK(!fs::exists(package));
    OA_CHECK(!fs::exists(entry));
    OA_CHECK(!fs::exists(mod_icon));
    OA_CHECK(!fs::exists(map_icon));
    OA_CHECK(!fs::exists(language_icon));
    OA_CHECK(!fs::exists(registry_icon));
    OA_CHECK(fs::exists(program_icon));
    OA_CHECK(read_file(other_package) == "<mime-info>other</mime-info>\n");
    OA_CHECK(read_file(other_icon) == "other-icon");
    OA_CHECK(!fs::exists(scratch.data / "mime/.net.coreprime.open-annihilation.updated"));
    OA_CHECK(!fs::exists(scratch.data / "applications/.net.coreprime.open-annihilation.updated"));

    // Withdrawing every type from a full registration removes the entry and the package too.
    done = file_types::register_xdg(executable, icon, places);
    OA_CHECK(done.changed && fs::exists(package) && fs::exists(entry));
    done = file_types::withdraw_xdg(file_types::TypeSet{}, places);
    OA_CHECK(done.changed && done.error.empty());
    OA_CHECK(!fs::exists(package));
    OA_CHECK(!fs::exists(entry));
    OA_CHECK(fs::exists(program_icon));
    OA_CHECK(read_file(other_package) == "<mime-info>other</mime-info>\n");

    std::error_code error;
    fs::remove_all(scratch.root, error);
}
#else
/// Reads a string value under HKEY_CURRENT_USER.
std::optional<std::wstring> read_value(const std::wstring& key_name, const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key_name.c_str(), 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS)
        return std::nullopt;
    DWORD type = 0;
    DWORD size = 0;
    std::optional<std::wstring> value;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
        std::vector<wchar_t> text(size / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(
                key, name, nullptr, &type, reinterpret_cast<BYTE*>(text.data()), &size
            ) == ERROR_SUCCESS)
            value = std::wstring(text.data());
    }
    RegCloseKey(key);
    return value;
}

void test_windows_registration() {
    const std::wstring parent = L"Software\\OpenAnnihilationFileTypesTest";
    const std::wstring scratch = parent + L"\\" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                 std::to_wstring(GetTickCount());
    const std::wstring classes = scratch + L"\\Classes";
    file_types::Places places{};
    places.classes_key = fs::path(classes).string();
    places.notify_shell = false;
    const fs::path executable(L"C:\\Games\\Open Annihilation\\open-annihilation.exe");
    const std::wstring program = executable.wstring();

    file_types::Registration done = file_types::register_windows(executable, places);
    OA_CHECK(done.supported);
    OA_CHECK(done.changed);
    OA_CHECK(done.error.empty());
    OA_CHECK(done.lines.size() == 24);
    const std::wstring command = L"\"" + program + L"\" --open \"%1\"";
    for (const file_types::FileType& type : file_types::file_types) {
        const std::wstring extension(type.extension.begin(), type.extension.end());
        const std::wstring program_id(type.program_id.begin(), type.program_id.end());
        const std::wstring mime(type.mime_type.begin(), type.mime_type.end());
        const std::wstring type_name(type.type_name.begin(), type.type_name.end());
        OA_CHECK(read_value(classes + L"\\." + extension, nullptr) == program_id);
        OA_CHECK(read_value(classes + L"\\." + extension, L"Content Type") == mime);
        OA_CHECK(
            read_value(classes + L"\\." + extension + L"\\OpenWithProgids", program_id.c_str()) ==
            L""
        );
        OA_CHECK(read_value(classes + L"\\" + program_id, nullptr) == type_name);
        OA_CHECK(
            read_value(classes + L"\\" + program_id + L"\\DefaultIcon", nullptr) == program + L",0"
        );
        OA_CHECK(
            read_value(classes + L"\\" + program_id + L"\\shell\\open\\command", nullptr) == command
        );
    }

    // The next start finds everything in place and writes nothing.
    done = file_types::register_windows(executable, places);
    OA_CHECK(done.supported && !done.changed && done.error.empty() && done.lines.empty());

    // A copy started from elsewhere rewrites two values per type: its icon and its command.
    const fs::path moved(L"D:\\Spiele\\open-annihilation.exe");
    done = file_types::register_windows(moved, places);
    OA_CHECK(done.changed && done.error.empty());
    OA_CHECK(done.lines.size() == 8);
    const std::wstring moved_command = L"\"" + moved.wstring() + L"\" --open \"%1\"";
    for (const file_types::FileType& type : file_types::file_types) {
        const std::wstring program_id(type.program_id.begin(), type.program_id.end());
        OA_CHECK(
            read_value(classes + L"\\" + program_id + L"\\DefaultIcon", nullptr) ==
            moved.wstring() + L",0"
        );
        OA_CHECK(
            read_value(classes + L"\\" + program_id + L"\\shell\\open\\command", nullptr) ==
            moved_command
        );
    }

    // A copy in the temporary folder, as one started from inside a zip is, writes nothing.
    done = file_types::register_windows(
        fs::temp_directory_path() / L"Temp1_open-annihilation.zip" / L"open-annihilation.exe",
        places
    );
    OA_CHECK(!done.changed && done.error.empty());
    OA_CHECK(done.lines.size() == 1 && done.lines[0].find("temporary folder") != std::string::npos);
    OA_CHECK(
        read_value(classes + L"\\OpenAnnihilation.Mod\\DefaultIcon", nullptr) ==
        moved.wstring() + L",0"
    );

    // A relative path is an error.
    done = file_types::register_windows(fs::path(L"open-annihilation.exe"), places);
    OA_CHECK(!done.changed && !done.error.empty());

    OA_CHECK(SHDeleteKeyW(HKEY_CURRENT_USER, scratch.c_str()) == ERROR_SUCCESS);
    // The parent goes too once no other run's key is under it.
    static_cast<void>(RegDeleteKeyW(HKEY_CURRENT_USER, parent.c_str()));

    // The dispatch registers in the registry here.
    places.classes_key = fs::path(classes).string();
    done = file_types::register_file_types(executable, {}, places);
    OA_CHECK(done.supported && done.changed && done.error.empty());
    OA_CHECK(SHDeleteKeyW(HKEY_CURRENT_USER, scratch.c_str()) == ERROR_SUCCESS);
    static_cast<void>(RegDeleteKeyW(HKEY_CURRENT_USER, parent.c_str()));
}

/// Writes a string value, making its key.
void write_sz(const std::wstring& key_name, const wchar_t* name, const std::wstring& data) {
    HKEY opened = nullptr;
    OA_CHECK(
        RegCreateKeyExW(
            HKEY_CURRENT_USER,
            key_name.c_str(),
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &opened,
            nullptr
        ) == ERROR_SUCCESS
    );
    const auto size = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
    OA_CHECK(
        RegSetValueExW(
            opened, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()), size
        ) == ERROR_SUCCESS
    );
    RegCloseKey(opened);
}

/// Reports whether a key is there.
bool key_exists(const std::wstring& key_name) {
    HKEY opened = nullptr;
    const LONG result =
        RegOpenKeyExW(HKEY_CURRENT_USER, key_name.c_str(), 0, KEY_QUERY_VALUE, &opened);
    if (result != ERROR_SUCCESS)
        return false;
    RegCloseKey(opened);
    return true;
}

/// A subset registers only its types. Withdrawing one leaves the others, and a value another
/// program wrote, including the player's UserChoice. A value is removed only while it still
/// names this game.
void test_windows_withdrawal() {
    const std::wstring parent = L"Software\\OpenAnnihilationFileTypesTest";
    const std::wstring scratch = parent + L"\\" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                 std::to_wstring(GetTickCount()) + L"-withdraw";
    const std::wstring classes = scratch + L"\\Classes";
    file_types::Places places{};
    places.classes_key = fs::path(classes).string();
    places.notify_shell = false;
    const fs::path executable(L"C:\\Games\\Open Annihilation\\open-annihilation.exe");

    file_types::TypeSet packages;
    packages.language = false;
    packages.registry = false;
    file_types::Registration done = file_types::register_windows(executable, places, packages);
    OA_CHECK(done.supported && done.changed && done.error.empty());
    OA_CHECK(done.lines.size() == 12);
    OA_CHECK(read_value(classes + L"\\.oamod", nullptr) == L"OpenAnnihilation.Mod");
    OA_CHECK(read_value(classes + L"\\.oamap", nullptr) == L"OpenAnnihilation.MapPack");
    OA_CHECK(!key_exists(classes + L"\\.oalang"));
    OA_CHECK(!key_exists(classes + L"\\OpenAnnihilation.Language"));
    OA_CHECK(!key_exists(classes + L"\\.oareg"));
    OA_CHECK(!key_exists(classes + L"\\OpenAnnihilation.Registry"));

    done = file_types::register_windows(executable, places);
    OA_CHECK(done.changed && done.lines.size() == 12);
    OA_CHECK(key_exists(classes + L"\\OpenAnnihilation.Language"));
    OA_CHECK(key_exists(classes + L"\\OpenAnnihilation.Registry"));

    // Another program's opener, and the player's own choice, planted on the game's extensions.
    write_sz(classes + L"\\.oamod\\OpenWithProgids", L"Other.Mod", L"");
    write_sz(classes + L"\\.oamod\\UserChoice", L"ProgId", L"Other.Mod");
    write_sz(classes + L"\\.oamap", nullptr, L"Other.Map");
    write_sz(classes + L"\\.oamap", L"Content Type", L"application/x-other-map");

    file_types::TypeSet maps;
    maps.mod = false;
    maps.language = false;
    maps.registry = false;
    done = file_types::withdraw_windows(maps, places);
    OA_CHECK(done.supported && done.changed && done.error.empty());
    // .oamap's (Default) and Content Type name the other program, so they stay. The game's
    // program-identifier key, and its OpenWithProgids value, go.
    OA_CHECK(read_value(classes + L"\\.oamap", nullptr) == L"Other.Map");
    OA_CHECK(read_value(classes + L"\\.oamap", L"Content Type") == L"application/x-other-map");
    OA_CHECK(!key_exists(classes + L"\\OpenAnnihilation.MapPack"));
    OA_CHECK(!read_value(classes + L"\\.oamap\\OpenWithProgids", L"OpenAnnihilation.MapPack"));
    OA_CHECK(read_value(classes + L"\\.oamod\\OpenWithProgids", L"Other.Mod") == L"");
    OA_CHECK(read_value(classes + L"\\.oamod\\UserChoice", L"ProgId") == L"Other.Mod");
    OA_CHECK(read_value(classes + L"\\.oamod", nullptr) == L"OpenAnnihilation.Mod");
    OA_CHECK(read_value(classes + L"\\.oamod", L"Content Type") == L"application/x-oamod");
    OA_CHECK(read_value(classes + L"\\.oalang", nullptr) == L"OpenAnnihilation.Language");
    OA_CHECK(key_exists(classes + L"\\OpenAnnihilation.Language"));
    OA_CHECK(read_value(classes + L"\\.oareg", nullptr) == L"OpenAnnihilation.Registry");
    OA_CHECK(key_exists(classes + L"\\OpenAnnihilation.Registry"));

    // .oamod's (Default) and Content Type still name the game, so those two values go. The
    // extension key stays, and so does the other program's value and the player's UserChoice.
    file_types::TypeSet mods;
    mods.language = false;
    mods.map = false;
    mods.registry = false;
    done = file_types::withdraw_windows(mods, places);
    OA_CHECK(done.changed && done.error.empty());
    OA_CHECK(!read_value(classes + L"\\.oamod", nullptr));
    OA_CHECK(!read_value(classes + L"\\.oamod", L"Content Type"));
    OA_CHECK(!key_exists(classes + L"\\OpenAnnihilation.Mod"));
    OA_CHECK(!read_value(classes + L"\\.oamod\\OpenWithProgids", L"OpenAnnihilation.Mod"));
    OA_CHECK(read_value(classes + L"\\.oamod\\OpenWithProgids", L"Other.Mod") == L"");
    OA_CHECK(read_value(classes + L"\\.oamod\\UserChoice", L"ProgId") == L"Other.Mod");
    OA_CHECK(key_exists(classes + L"\\.oamod"));
    OA_CHECK(read_value(classes + L"\\.oamap", nullptr) == L"Other.Map");
    OA_CHECK(read_value(classes + L"\\.oalang", nullptr) == L"OpenAnnihilation.Language");
    OA_CHECK(key_exists(classes + L"\\OpenAnnihilation.Registry"));

    done = file_types::withdraw_windows(mods, places);
    OA_CHECK(done.supported && !done.changed && done.error.empty());
    OA_CHECK(read_value(classes + L"\\.oamod\\UserChoice", L"ProgId") == L"Other.Mod");
    OA_CHECK(read_value(classes + L"\\.oamod\\OpenWithProgids", L"Other.Mod") == L"");

    OA_CHECK(SHDeleteKeyW(HKEY_CURRENT_USER, scratch.c_str()) == ERROR_SUCCESS);
    static_cast<void>(RegDeleteKeyW(HKEY_CURRENT_USER, parent.c_str()));
}
#endif

/// A Launch Services stand-in: each content type has one opener, and every question is recorded.
struct FakeLaunchServices {
    std::map<std::string, file_types::Handler> handlers{};
    std::string fail_read{};  ///< a content type whose question fails
    std::string fail_write{}; ///< a content type whose change fails
    bool store_writes = true; ///< a change that answers and leaves the opener as it was
    std::vector<std::string> reads{};
    std::vector<std::pair<std::string, std::string>> writes{};
};

file_types::Handler fake_read(void* context, std::string_view content_type, std::string& failure) {
    auto* fake = static_cast<FakeLaunchServices*>(context);
    const std::string type(content_type);
    fake->reads.push_back(type);
    if (type == fake->fail_read) {
        failure = "refused";
        return {};
    }
    const auto found = fake->handlers.find(type);
    if (found == fake->handlers.end())
        return {};
    return found->second;
}

bool fake_write(
    void* context,
    std::string_view content_type,
    const file_types::Handler& handler,
    std::string& failure
) {
    auto* fake = static_cast<FakeLaunchServices*>(context);
    const std::string type(content_type);
    if (type == fake->fail_write) {
        failure = "sandbox";
        return false;
    }
    fake->writes.emplace_back(type, handler.bundle_id);
    if (fake->store_writes)
        fake->handlers[type] = handler;
    return true;
}

file_types::HandlerCalls fake_calls(FakeLaunchServices& fake) {
    file_types::HandlerCalls calls;
    calls.context = &fake;
    calls.default_handler = &fake_read;
    calls.set_handler = &fake_write;
    return calls;
}

file_types::Handler handler_named(std::string_view bundle_id) {
    file_types::Handler handler;
    handler.bundle_id = std::string(bundle_id);
    return handler;
}

/// The four switches ask for this copy when on, and for the opener of a zip or of plain text
/// when off. A question that fails is logged and does not stop the others.
void test_opener_changes() {
    const file_types::Handler application = handler_named("net.coreprime.open-annihilation");
    const file_types::Handler archive = handler_named("com.example.archive");
    const file_types::Handler editor = handler_named("com.example.editor");
    FakeLaunchServices fake;
    fake.handlers["public.zip-archive"] = archive;
    fake.handlers["public.plain-text"] = editor;
    for (const file_types::ExportedContentType& type : file_types::exported_content_types)
        fake.handlers[std::string(type.content_type)] = application;

    file_types::TypeSet fallback;
    fallback.mod = false;
    fallback.language = false;
    fallback.map = false;
    fallback.registry = false;
    const file_types::OpenerReport open =
        file_types::reported_openers(fake_calls(fake), application.bundle_id, fallback);
    OA_CHECK(open.error.empty());
    OA_CHECK(open.open.mod && open.open.language && open.open.map && open.open.registry);

    fake.handlers[std::string(file_types::exported_content_types[0].content_type)] = archive;
    const file_types::OpenerReport mixed =
        file_types::reported_openers(fake_calls(fake), application.bundle_id, fallback);
    OA_CHECK(!mixed.open.mod && mixed.open.language && mixed.open.map && mixed.open.registry);

    fake.fail_read = std::string(file_types::exported_content_types[2].content_type);
    file_types::TypeSet kept = fallback;
    kept.map = true;
    const file_types::OpenerReport failed =
        file_types::reported_openers(fake_calls(fake), application.bundle_id, kept);
    OA_CHECK(failed.open.map);
    OA_CHECK(failed.error.find("oamap: refused") != std::string::npos);
    fake.fail_read.clear();
    fake.reads.clear();

    const file_types::OpenerChange off[] = {
        {file_types::mod_extension, false},
        {file_types::registry_extension, false},
    };
    const file_types::Registration handed =
        file_types::apply_opener_changes(fake_calls(fake), application, off);
    OA_CHECK(handed.supported && handed.changed && handed.error.empty());
    OA_CHECK(handed.lines.size() == 2);
    OA_CHECK(handed.lines[0] == "hand back oamod to com.example.archive");
    OA_CHECK(handed.lines[1] == "hand back oareg to com.example.editor");
    // Each type asks for the opener it hands back to, then reads its own opener afterwards.
    OA_CHECK(fake.reads.size() == 4);
    OA_CHECK(fake.reads[0] == "public.zip-archive");
    OA_CHECK(fake.reads[1] == "net.coreprime.open-annihilation.oamod");
    OA_CHECK(fake.reads[2] == "public.plain-text");
    OA_CHECK(fake.reads[3] == "net.coreprime.open-annihilation.oareg");
    OA_CHECK(
        fake.handlers["net.coreprime.open-annihilation.oamod"].bundle_id == "com.example.archive"
    );
    OA_CHECK(
        fake.handlers["net.coreprime.open-annihilation.oareg"].bundle_id == "com.example.editor"
    );
    // The two types that did not move were not asked.
    OA_CHECK(
        fake.handlers["net.coreprime.open-annihilation.oalang"].bundle_id == application.bundle_id
    );
    OA_CHECK(
        fake.handlers["net.coreprime.open-annihilation.oamap"].bundle_id == application.bundle_id
    );

    fake.reads.clear();
    fake.writes.clear();
    const file_types::OpenerChange on[] = {{file_types::mod_extension, true}};
    const file_types::Registration taken =
        file_types::apply_opener_changes(fake_calls(fake), application, on);
    OA_CHECK(taken.changed && taken.error.empty());
    OA_CHECK(taken.lines.size() == 1 && taken.lines[0] == "open oamod");
    // On does not look up a zip or text opener. It reads this type back after the set.
    OA_CHECK(fake.reads.size() == 1);
    OA_CHECK(fake.reads[0] == "net.coreprime.open-annihilation.oamod");
    OA_CHECK(
        fake.handlers["net.coreprime.open-annihilation.oamod"].bundle_id == application.bundle_id
    );

    fake.fail_read = "public.zip-archive";
    fake.writes.clear();
    const file_types::OpenerChange again[] = {
        {file_types::language_extension, false},
        {file_types::map_extension, true},
    };
    const file_types::Registration partial =
        file_types::apply_opener_changes(fake_calls(fake), application, again);
    OA_CHECK(partial.changed);
    OA_CHECK(partial.error.find("oalang: refused") != std::string::npos);
    OA_CHECK(partial.lines.size() == 1 && partial.lines[0] == "open oamap");
    OA_CHECK(
        fake.handlers["net.coreprime.open-annihilation.oamap"].bundle_id == application.bundle_id
    );
    fake.fail_read.clear();

    fake.fail_write = "net.coreprime.open-annihilation.oamod";
    const file_types::Registration refused =
        file_types::apply_opener_changes(fake_calls(fake), application, on);
    OA_CHECK(!refused.changed);
    OA_CHECK(refused.error.find("oamod: sandbox") != std::string::npos);
    fake.fail_write.clear();

    // A set that answers and leaves another opener is logged and is not counted.
    fake.handlers["net.coreprime.open-annihilation.oamod"] = archive;
    fake.store_writes = false;
    const file_types::Registration ignored =
        file_types::apply_opener_changes(fake_calls(fake), application, on);
    OA_CHECK(!ignored.changed && ignored.lines.empty());
    OA_CHECK(ignored.error.find("oamod: the change did not take") != std::string::npos);
    OA_CHECK(fake.handlers["net.coreprime.open-annihilation.oamod"].bundle_id == archive.bundle_id);
    fake.store_writes = true;

    const file_types::OpenerChange unknown[] = {{"zip", true}};
    const file_types::Registration other =
        file_types::apply_opener_changes(fake_calls(fake), application, unknown);
    OA_CHECK(other.error.find("unknown file type zip") != std::string::npos);

    file_types::HandlerCalls none;
    const file_types::Registration absent = file_types::apply_opener_changes(none, application, on);
    OA_CHECK(!absent.supported && !absent.changed);
    OA_CHECK(!absent.lines.empty() && absent.lines[0].find("skipped:") == 0);

    // This test is not an application bundle, so it neither reads nor changes a real opener.
    OA_CHECK(file_types::running_application().bundle_id.empty());
    if (file_types::default_opener_is_a_setting())
        OA_CHECK(file_types::system_handler_calls().default_handler != nullptr);
    else
        OA_CHECK(file_types::system_handler_calls().default_handler == nullptr);
}

} // namespace

int main() {
    test_desktop_exec_argument();
    test_texts();
    test_running_executable();
    test_opener_changes();
#ifndef _WIN32
    test_xdg_registration();
    test_xdg_skips();
    test_xdg_data_folder();
    test_registration_by_system();
    test_xdg_subset_and_withdrawal();
#else
    test_windows_registration();
    test_windows_withdrawal();
#endif
    return oa::test::check_exit_status();
}
