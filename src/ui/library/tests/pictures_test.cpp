// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The pictures of the rows in view. A list of forty rows, laid out with a
// synthetic font, asks a fake source for each row the list shows and a few
// past each edge, once each, the selected row first. Scrolling asks for the
// new rows and does not ask again for the ones already asked. A row with no
// picture shows its letters, and a picture that arrives is on that row.

#include "oa/formats/fnt.hpp"
#include "oa/test/check.hpp"
#include "oa/ui/frontend_renderer.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/looks.hpp"
#include "oa/ui/library/library.hpp"
#include "oa/ui/library/screen.hpp"
#include "screen_fixture.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace kit = oa::ui::kit;
namespace lib = oa::ui::library;
namespace renderer = oa::ui::frontend_renderer;
namespace fixture = screen_fixture;

/// How many mods sit under the base game, so the Mods list holds forty rows.
constexpr int kPackCount = 39;
/// The selected row, past the first window: pack 30, place 30 once the base
/// game leads the list.
constexpr std::string_view kSelectedKey = "pack-30";
/// How many row heights the second look scrolls by.
constexpr int32_t kScrollRows = 18;
/// The most bytes one word of an automation name keeps, as the layout does.
constexpr std::size_t kNameWordBytes = 40;

/// The synthetic font of the kit's tests, for both roles.
///
/// @return the fonts
kit::Fonts block_fonts() {
    renderer::TextFont font;
    font.font.nominal_height = 8;
    constexpr uint8_t ink_index = 1;
    for (int byte = 0; byte < 256; ++byte) {
        const auto width = static_cast<uint16_t>(2 + byte % 4);
        constexpr uint16_t height = 8;
        const auto count = static_cast<std::size_t>(width) * height;
        std::vector<uint8_t> pixels(count, ink_index);
        std::vector<uint8_t> coverage(count, 1);
        font.font.glyphs[static_cast<std::size_t>(byte)] =
            oa::formats::fnt::Glyph{width, height, 0, 0, std::move(pixels), std::move(coverage)};
    }
    font.ink[ink_index] = static_cast<uint16_t>(renderer::blend_opaque);
    return {font, font, {}, {}};
}

/// The fonts every layout here is measured in.
///
/// @return the fonts
const kit::Fonts& fonts() {
    static const kit::Fonts loaded = block_fonts();
    return loaded;
}

/// Returns a text as one word of an automation name, as the layout does.
///
/// @param text the text
/// @return the word
std::string name_word(std::string_view text) {
    std::string word;
    bool hyphen = false;
    for (const char character : text) {
        const bool small = character >= 'a' && character <= 'z';
        const bool capital = character >= 'A' && character <= 'Z';
        const bool digit = character >= '0' && character <= '9';
        if (small || digit || capital) {
            if (hyphen && !word.empty())
                word += '-';
            hyphen = false;
            word += capital ? static_cast<char>(character - 'A' + 'a') : character;
        } else {
            hyphen = true;
        }
        if (word.size() >= kNameWordBytes)
            break;
    }
    if (word.empty())
        word = "x";
    return word;
}

/// Returns the automation name a row is given.
///
/// @param entry the entry
/// @return the name
std::string row_automation_name(const lib::Entry& entry) {
    std::string name = "library.row.";
    if (!entry.id.registry.empty())
        name += name_word(entry.id.registry) + ".";
    name += name_word(entry.id.key);
    return name;
}

/// One row the list shows, in the order down the list.
struct ShownRow {
    std::string name{}; ///< its automation name
    int32_t top{};      ///< its top, in points
    int32_t height{};   ///< its height, in points
};

/// Returns the list rows the screen shows, top to bottom.
///
/// A row is a list item whose name starts with library.row. A row's download
/// arrow is a button, so it is not one of these.
///
/// @param list the display list
/// @return the rows
std::vector<ShownRow> shown_rows(const kit::DisplayList& list) {
    std::vector<ShownRow> rows;
    for (const kit::Control& control : list.controls) {
        if (control.kind != kit::ControlKind::list_item)
            continue;
        if (control.name.compare(0, 12, "library.row.") != 0)
            continue;
        rows.push_back(ShownRow{control.name, control.rect.y, control.rect.height});
    }
    std::sort(rows.begin(), rows.end(), [](const ShownRow& left, const ShownRow& right) {
        if (left.top != right.top)
            return left.top < right.top;
        return left.name < right.name;
    });
    return rows;
}

/// Returns the place, in the visible list, of the first shown row.
///
/// The shown names are a contiguous run of the visible entries' names.
///
/// @param library the model
/// @param shown the rows the list shows
/// @return the place; nothing when the names do not match a run
std::optional<std::size_t>
shown_origin(const lib::Library& library, const std::vector<ShownRow>& shown) {
    if (shown.empty() || shown.size() > library.visible.size())
        return std::nullopt;
    for (std::size_t origin = 0; origin + shown.size() <= library.visible.size(); ++origin) {
        bool matches = true;
        for (std::size_t step = 0; step < shown.size(); ++step) {
            const lib::Entry& entry = library.entries[library.visible[origin + step]];
            if (row_automation_name(entry) != shown[step].name)
                matches = false;
        }
        if (matches)
            return origin;
    }
    return std::nullopt;
}

/// Returns the keys whose pictures this layout asks for.
///
/// The selected entry is first. Then every shown row, and picture_margin
/// rows before the first and after the last. The window comes from the rows
/// the list shows, not from the function that asks.
///
/// @param library the model
/// @param list the display list
/// @return the keys, in the order they are asked for
std::vector<std::string> expected_keys(const lib::Library& library, const kit::DisplayList& list) {
    const std::vector<ShownRow> shown = shown_rows(list);
    const std::optional<std::size_t> origin = shown_origin(library, shown);
    std::vector<std::string> keys;
    const auto add = [&](std::size_t place) {
        if (place >= library.visible.size())
            return;
        const std::string& key = library.entries[library.visible[place]].id.key;
        if (std::find(keys.begin(), keys.end(), key) != keys.end())
            return;
        keys.push_back(key);
    };
    if (library.selected) {
        for (std::size_t place = 0; place < library.visible.size(); ++place) {
            if (library.entries[library.visible[place]].id == *library.selected) {
                add(place);
                break;
            }
        }
    }
    if (!origin)
        return keys;
    const std::size_t begin = *origin > lib::picture_margin ? *origin - lib::picture_margin : 0;
    std::size_t end = *origin + shown.size();
    if (end < library.visible.size()) {
        const std::size_t room = library.visible.size() - end;
        end += room < lib::picture_margin ? room : lib::picture_margin;
    }
    for (std::size_t place = begin; place < end; ++place)
        add(place);
    return keys;
}

/// Prints two key lists when they differ.
///
/// @param got what was asked for
/// @param want what the rows in view call for
void report_keys(const std::vector<std::string>& got, const std::vector<std::string>& want) {
    if (got == want)
        return;
    std::fputs("asked:", stderr);
    for (const std::string& key : got)
        std::fprintf(stderr, " %s", key.c_str());
    std::fputs("\nwanted:", stderr);
    for (const std::string& key : want)
        std::fprintf(stderr, " %s", key.c_str());
    std::fputc('\n', stderr);
}

/// A fake picture source: each key once, the selected row moved first.
struct FakePictures {
    std::vector<std::string> order{}; ///< the keys asked for, first to last
};

/// Tells whether this entry's picture was already asked for, or is held.
///
/// @param context the fake
/// @param entry the entry
/// @return true when it was
bool picture_known(void* context, const lib::Entry& entry) {
    const auto* fake = static_cast<const FakePictures*>(context);
    if (entry.badge != nullptr)
        return true;
    if (entry.listing != nullptr && entry.listing->badge)
        return true;
    return std::find(fake->order.begin(), fake->order.end(), entry.id.key) != fake->order.end();
}

/// Records one ask. A key already recorded stays one entry; the lead moves
/// to the front.
///
/// @param context the fake
/// @param entry the entry
/// @param lead the selected row
void picture_request(void* context, const lib::Entry& entry, bool lead) {
    auto* fake = static_cast<FakePictures*>(context);
    const auto found = std::find(fake->order.begin(), fake->order.end(), entry.id.key);
    if (found != fake->order.end()) {
        if (lead && found != fake->order.begin()) {
            const std::string key = *found;
            fake->order.erase(found);
            fake->order.insert(fake->order.begin(), key);
        }
        return;
    }
    if (lead)
        fake->order.insert(fake->order.begin(), entry.id.key);
    else
        fake->order.push_back(entry.id.key);
}

/// Asks the fake for the pictures of this layout.
///
/// @param library the model
/// @param list the display list
/// @param fake the source
void ask(const lib::Library& library, const kit::DisplayList& list, FakePictures& fake) {
    lib::PictureSource source;
    source.context = &fake;
    source.known = picture_known;
    source.request = picture_request;
    lib::request_visible_pictures(library, list, source);
}

/// Returns the look of a named list row.
///
/// @param list the display list
/// @param name the row's name
/// @return the look; null when the row is not drawn
const kit::ListRowLook* row_look_named(const kit::DisplayList& list, std::string_view name) {
    const kit::Control* control = fixture::named(list, name);
    if (control == nullptr)
        return nullptr;
    for (const kit::Item& item : list.items) {
        if (item.role != kit::Role::list_row || item.control != control->id)
            continue;
        return std::get_if<kit::ListRowLook>(&item.look);
    }
    return nullptr;
}

/// Returns the Library: the base game and thirty-nine built-in mods.
///
/// Each mod names Open Annihilation as its author, so its row's text
/// carries the maker's mark and its byline names that author.
///
/// @return the Library, on Mods, with pack 30 selected
lib::Library forty_rows() {
    lib::Inputs inputs;
    lib::Registry registry;
    registry.id = std::string(fixture::core);
    registry.name = "Fixture Core";
    registry.built_in = true;
    inputs.registries = {std::move(registry)};
    inputs.engine = {0, 8, 0};
    inputs.version_text = "v0.8.0";
    for (int number = 1; number <= kPackCount; ++number) {
        const std::string digits = (number < 10 ? "0" : "") + std::to_string(number);
        const std::string key = "pack-" + digits;
        const std::string name = "Pack " + digits;
        lib::Listing pack = fixture::listing(
            fixture::core, lib::Kind::mod, key, name, "1.0", fixture::mib, "A pack.", {}
        );
        pack.author = "Open Annihilation";
        inputs.listings.push_back(std::move(pack));
    }
    lib::Library library;
    lib::refresh(library, std::move(inputs));
    lib::set_tab(library, lib::Tab::mods);
    lib::EntryId selected;
    selected.registry = std::string(fixture::core);
    selected.kind = lib::Kind::mod;
    selected.key = std::string(kSelectedKey);
    lib::select(library, selected);
    return library;
}

/// Lays the Library out.
///
/// @param library the model
/// @param state the screen's state
/// @param frame the frame
/// @return the display list
kit::DisplayList
laid_out(const lib::Library& library, const lib::ScreenState& state, const kit::Frame& frame) {
    return lib::library_layout(library, state, frame, fonts());
}

void rows_in_view_are_asked_once_and_a_picture_redraws() {
    lib::Library library = forty_rows();
    OA_CHECK(library.visible.size() == static_cast<std::size_t>(kPackCount) + 1);
    OA_CHECK(library.selected && library.selected->key == kSelectedKey);

    const kit::Frame frame = fixture::frames()[1].frame;
    lib::ScreenState state;
    const kit::DisplayList first = laid_out(library, state, frame);
    const std::vector<ShownRow> at_top = shown_rows(first);
    OA_CHECK(!at_top.empty());
    OA_CHECK(at_top.size() < library.visible.size());
    OA_CHECK(shown_origin(library, at_top).has_value());

    // A shown mod's text carries the maker's mark, and its byline the author,
    // which is what a match on the drawn text used to miss.
    const ShownRow* marked = nullptr;
    for (const ShownRow& row : at_top) {
        if (row.name != "library.row.base-game")
            marked = &row;
    }
    OA_CHECK(marked != nullptr);
    if (marked != nullptr) {
        const kit::Control* control = fixture::named(first, marked->name);
        const kit::ListRowLook* look = row_look_named(first, marked->name);
        OA_CHECK(control != nullptr && look != nullptr);
        if (control != nullptr && look != nullptr) {
            OA_CHECK(control->text.find("Open Annihilation") != std::string::npos);
            OA_CHECK(control->text.find(" · ") != std::string::npos);
            OA_CHECK(look->by.find("by Open Annihilation") != std::string::npos);
            OA_CHECK(look->by.find("download") == std::string::npos);
            OA_CHECK(look->maker == kit::MakerMark::open_annihilation);
            OA_CHECK(!renderer::picture_drawable(look->badge));
            OA_CHECK(!look->badge_text.empty());
            OA_CHECK(look->badge_text == lib::badge_letters(look->title));
        }
    }

    FakePictures fake;
    ask(library, first, fake);
    const std::vector<std::string> wanted = expected_keys(library, first);
    report_keys(fake.order, wanted);
    OA_CHECK(fake.order == wanted);
    OA_CHECK(!fake.order.empty());
    OA_CHECK(fake.order.front() == kSelectedKey);
    OA_CHECK(fake.order.size() < library.visible.size());
    OA_CHECK(std::find(fake.order.begin(), fake.order.end(), "pack-39") == fake.order.end());

    const std::size_t asked_at_top = fake.order.size();
    ask(library, first, fake);
    OA_CHECK(fake.order.size() == asked_at_top);

    OA_CHECK(!at_top.empty() && at_top.front().height > 0);
    if (!at_top.empty() && at_top.front().height > 0)
        state.list_scroll = kScrollRows * at_top.front().height;
    const int32_t scrolled = state.list_scroll;
    const kit::DisplayList lower = laid_out(library, state, frame);
    OA_CHECK(state.list_scroll == scrolled);
    const std::vector<ShownRow> below = shown_rows(lower);
    OA_CHECK(shown_origin(library, below).has_value());
    bool scrolled_rows = below.size() != at_top.size();
    if (!scrolled_rows) {
        for (std::size_t index = 0; index < below.size(); ++index)
            scrolled_rows = scrolled_rows || below[index].name != at_top[index].name;
    }
    OA_CHECK(scrolled_rows);

    ask(library, lower, fake);
    const std::vector<std::string> wanted_below = expected_keys(library, lower);
    std::vector<std::string> combined = wanted;
    for (const std::string& key : wanted_below) {
        if (std::find(combined.begin(), combined.end(), key) == combined.end())
            combined.push_back(key);
    }
    // The selected row was already first, so offering it again leaves it there.
    report_keys(fake.order, combined);
    OA_CHECK(fake.order == combined);
    OA_CHECK(fake.order.size() > asked_at_top);
    OA_CHECK(fake.order.size() < library.visible.size());
    OA_CHECK(fake.order.front() == kSelectedKey);
    for (std::size_t index = 0; index < fake.order.size(); ++index) {
        const auto again = std::find(
            fake.order.begin() + static_cast<std::ptrdiff_t>(index) + 1,
            fake.order.end(),
            fake.order[index]
        );
        OA_CHECK(again == fake.order.end());
    }

    const std::size_t asked_below = fake.order.size();
    ask(library, lower, fake);
    OA_CHECK(fake.order.size() == asked_below);

    // A picture that arrives replaces the letters on its row, and is not
    // asked for again. The scroll stays where the player left it.
    std::string arrived_key;
    std::string arrived_name;
    if (shown_origin(library, below)) {
        const std::size_t origin = *shown_origin(library, below);
        for (std::size_t step = 0; step < below.size(); ++step) {
            const lib::Entry& entry = library.entries[library.visible[origin + step]];
            if (entry.id.key == kSelectedKey || entry.listing == nullptr)
                continue;
            arrived_key = entry.id.key;
            arrived_name = below[step].name;
            break;
        }
    }
    OA_CHECK(!arrived_key.empty());
    const kit::ListRowLook* waiting = row_look_named(lower, arrived_name);
    OA_CHECK(waiting != nullptr && !renderer::picture_drawable(waiting->badge));
    if (!arrived_key.empty()) {
        for (lib::Listing& listing : library.inputs.listings) {
            if (listing.key == arrived_key)
                listing.badge = fixture::gradient_badge();
        }
        const lib::EntryId selected = *library.selected;
        lib::refresh(library, std::move(library.inputs));
        lib::set_tab(library, lib::Tab::mods);
        lib::select(library, selected);
        state.list_scroll = scrolled;
        const kit::DisplayList arrived = laid_out(library, state, frame);
        OA_CHECK(state.list_scroll == scrolled);
        const kit::ListRowLook* look = row_look_named(arrived, arrived_name);
        OA_CHECK(look != nullptr && renderer::picture_drawable(look->badge));
        ask(library, arrived, fake);
        OA_CHECK(fake.order.size() == asked_below);
        OA_CHECK(std::find(fake.order.begin(), fake.order.end(), arrived_key) != fake.order.end());
    }
}

} // namespace

int main() {
    rows_in_view_are_asked_once_and_a_picture_redraws();
    return oa::test::check_exit_status();
}
