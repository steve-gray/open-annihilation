// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The chooser's layouts in the Game files screen's look, built from the kit's
// parts (folder_chooser.hpp). Each view is the header bar, then a column of
// three blocks: a fixed top (the title, the lines under it, the notice and,
// in the browser, the places and the verdict), a body that scrolls when the
// column does not fit (the rows), and a fixed bottom (the buttons). When even
// the fixed blocks leave the body too little room, the whole column scrolls
// (the kit's scroll_column). The kit measures, fits and wraps the texts, a
// path breaking after its separators (path_break), and lays out the text
// parts, the buttons and their rows, the icons and the plain parts; the
// chooser's own compositions (its header, banners, title and rows) place
// them. Every control is at least min_button_points high, and each one's part
// carries its target, in the order the focus moves.
#include "folder_chooser_internal.hpp"

#include "oa/data/languages/interface_text.hpp"
#include "oa/ui/folder_chooser.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/text.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::folder_chooser {

namespace {

namespace gf = oa::ui::game_files;
namespace kit = oa::ui::kit;
using gf::Colour;
using gf::Glyph;
using kit::Lettering;
using kit::Role;
using kit::TextFit;
using kit::TextStyle;
using detail::rows_area_group;
using detail::target_part;

/// The sizes of one form, in points.
struct Metrics {
    float header_height{};     ///< the header bar
    float header_pad{};        ///< the header's side padding
    float badge{};             ///< the OA badge's side
    float header_text{};       ///< "OPEN ANNIHILATION  GAME FOLDER"
    float header_gap{};        ///< between the header's pieces
    float version_text{};      ///< the version
    float max_content_width{}; ///< the widest the column grows (0: no limit)
    float side_pad{};          ///< the column's least side padding
    float content_top{};       ///< above the column
    float content_bottom{};    ///< below the column
    float title{};             ///< the title
    float subtitle{};          ///< the lines under it
    float title_gap{};         ///< between the title and the next line
    float block_gap{};         ///< between the blocks of a view
    float banner_text{};       ///< a banner's text
    float banner_pad_x{};      ///< a banner's side padding
    float banner_pad_y{};      ///< a banner's top and bottom padding
    float banner_icon{};       ///< a banner's mark
    float heading{};           ///< a heading over a panel of rows
    float row_pad_x{};         ///< a row's side padding
    float row_pad_y{};         ///< a row's top and bottom padding
    float row_icon{};          ///< a row's mark
    float row_title{};         ///< a row's title
    float row_detail{};        ///< a row's other lines
    float row_gap{};           ///< between a row's columns
    float button_text{};       ///< a button's label
    float button_pad{};        ///< a button's side padding
    float button_min_width{};  ///< the main buttons' least width
    float button_gap{};        ///< between buttons
};

/// The tablet form (a 1280x800 window, the Steam Deck's screen).
constexpr Metrics tablet_metrics{
    .header_height = 56.0f,
    .header_pad = 24.0f,
    .badge = 26.0f,
    .header_text = 15.0f,
    .header_gap = 12.0f,
    .version_text = 12.0f,
    .max_content_width = 820.0f,
    .side_pad = 24.0f,
    .content_top = 24.0f,
    .content_bottom = 18.0f,
    .title = 24.0f,
    .subtitle = 14.0f,
    .title_gap = 8.0f,
    .block_gap = 16.0f,
    .banner_text = 13.5f,
    .banner_pad_x = 14.0f,
    .banner_pad_y = 8.0f,
    .banner_icon = 18.0f,
    .heading = 12.5f,
    .row_pad_x = 14.0f,
    .row_pad_y = 8.0f,
    .row_icon = 26.0f,
    .row_title = 15.0f,
    .row_detail = 12.5f,
    .row_gap = 12.0f,
    .button_text = 12.5f,
    .button_pad = 16.0f,
    .button_min_width = 150.0f,
    .button_gap = 12.0f,
};

/// The compact form (an 852x393-point phone window, or a small window), controls kept at
/// 44 points.
constexpr Metrics compact_metrics{
    .header_height = 48.0f,
    .header_pad = 14.0f,
    .badge = 22.0f,
    .header_text = 12.5f,
    .header_gap = 10.0f,
    .version_text = 11.0f,
    .max_content_width = 0.0f,
    .side_pad = 20.0f,
    .content_top = 10.0f,
    .content_bottom = 6.0f,
    .title = 19.0f,
    .subtitle = 12.0f,
    .title_gap = 3.0f,
    .block_gap = 8.0f,
    .banner_text = 11.0f,
    .banner_pad_x = 8.0f,
    .banner_pad_y = 4.0f,
    .banner_icon = 14.0f,
    .heading = 11.0f,
    .row_pad_x = 10.0f,
    .row_pad_y = 4.0f,
    .row_icon = 22.0f,
    .row_title = 13.0f,
    .row_detail = 10.5f,
    .row_gap = 8.0f,
    .button_text = 11.0f,
    .button_pad = 12.0f,
    .button_min_width = 124.0f,
    .button_gap = 8.0f,
};

/// A window whose shorter side is under this many points takes the compact sizes.
constexpr float compact_short_side_points = 600.0f;
/// The least height, in points, a scrolling body keeps before the whole column scrolls.
constexpr float least_body_points = 96.0f;
/// The space, in points, the header's version needs before "GAME FOLDER" shows beside the title.
constexpr float header_word_room_points = 40.0f;
/// The most lines the title takes.
constexpr std::size_t most_title_lines = 2;
/// The most lines the text under the title takes.
constexpr std::size_t most_subtitle_lines = 4;
/// The most lines a banner takes.
constexpr std::size_t most_banner_lines = 8;
/// The most lines a row's verdict takes.
constexpr std::size_t most_verdict_lines = 2;

/// Returns one of the chooser's words in the installed language.
///
/// @param english the words as the source writes them
/// @return their translation, or the words themselves
std::string tr(std::string_view english) {
    return std::string(oa::data::languages::interface_text(english));
}

/// The form being laid out: the model, the form's sizes and the kit's typesetter.
struct Form {
    const Model& model;     ///< what to show
    const Metrics& metrics; ///< the form's sizes
    kit::Typesetter type{}; ///< the scale and the text measure
};

/// Parts laid out in a block, their rectangles relative to the block's top.
struct Block {
    std::vector<kit::Item> items{}; ///< in painting order
    int height{};                   ///< pixels
};

/// Returns one of the chooser's buttons as the kit lays a button out.
///
/// @param label its upper-case label
/// @param target what a press reaches
/// @param enabled a press reaches it
/// @param glyph the mark beside the label
/// @param role main or plain
/// @return its label, look, mark and control
kit::ButtonSpec button_of(
    std::string label,
    Target target,
    bool enabled = true,
    Glyph glyph = Glyph::none,
    Role role = Role::button
) {
    kit::ButtonSpec button{};
    button.label = std::move(label);
    button.role = role;
    button.glyph = glyph;
    button.control = control_id(target);
    button.enabled = enabled;
    return button;
}

/// Adds a row of the chooser's buttons with the kit's button row at the form's sizes: a
/// button's label and padding, the main button's least width, the gap, and the least height
/// a control keeps.
///
/// @param form the form laid out
/// @param[in,out] block the block
/// @param x the column's left, pixels
/// @param y the row's top, pixels
/// @param room the column's width, pixels
/// @param left the buttons at the left, left to right
/// @param right the buttons at the right, left to right
/// @return the height taken, pixels
int button_row_of(
    const Form& form,
    Block& block,
    int x,
    int y,
    int room,
    const std::vector<kit::ButtonSpec>& left,
    const std::vector<kit::ButtonSpec>& right
) {
    const Metrics& metrics = form.metrics;
    const kit::ButtonRowSizes sizes{
        metrics.button_text,
        metrics.button_pad,
        metrics.button_min_width,
        metrics.button_gap,
        gf::min_button_points,
    };
    return kit::button_row(block.items, form.type, {x, y}, room, left, right, sizes);
}

/// Adds a banner: its mark and its text.
///
/// @param form the form laid out
/// @param[in,out] block the block
/// @param x the column's left, pixels
/// @param y the banner's top, pixels
/// @param room the column's width, pixels
/// @param text what it says
/// @param glyph the mark at its left
/// @param colour its rule and mark
/// @return the height taken, pixels
int add_banner(
    const Form& form,
    Block& block,
    int x,
    int y,
    int room,
    std::string_view text,
    Glyph glyph,
    Colour colour
) {
    const Metrics& metrics = form.metrics;
    const int pad_x = kit::canvas_pixels(form.type, metrics.banner_pad_x);
    const int pad_y = kit::canvas_pixels(form.type, metrics.banner_pad_y);
    const int icon = kit::canvas_pixels(form.type, metrics.banner_icon);
    const int gap = kit::canvas_pixels(form.type, metrics.row_gap);
    const int text_x = x + pad_x + icon + gap;
    std::vector<kit::Item> inner;
    const int text_height = kit::text_part(
        inner,
        form.type,
        Role::text,
        {text_x, 0},
        x + room - pad_x - text_x,
        text,
        Lettering{metrics.banner_text, false, gf::text_colour, TextStyle::lead},
        TextFit::column,
        most_banner_lines,
        kit::path_break
    );
    const int height = pad_y + std::max(text_height, icon) + pad_y;
    kit::plain_part(block.items, Role::banner, {x, y, room, height}, colour);
    kit::icon_part(
        block.items, {x + pad_x, y + (height - icon) / 2, icon, icon}, glyph, colour, false
    );
    kit::move_parts(block.items, inner, y + (height - text_height) / 2);
    return height;
}

/// One line of a row under its title.
struct RowLine {
    std::string text{};            ///< the words
    Colour colour{gf::dim_colour}; ///< their colour
    std::size_t most_lines{1};     ///< the most lines they wrap to
};

/// One row: a mark, a title, the lines under it and words at its right.
struct Row {
    Glyph glyph{Glyph::none};             ///< the mark at its left
    Colour glyph_colour{gf::dim_colour};  ///< the mark's colour
    std::string title{};                  ///< its title
    Colour title_colour{gf::text_colour}; ///< the title's colour
    bool title_bold{true};                ///< the title is bold
    std::vector<RowLine> lines{};         ///< the lines under the title
    std::string right{};                  ///< words at its right; empty for none
    Colour right_colour{gf::dim_colour};  ///< their colour
    Target target{};                      ///< what a press reaches
    bool enabled{true};                   ///< a press reaches it
};

/// Adds rows in a panel, with dividers between them; each row is a control at least
/// min_button_points high, its part carrying its target.
///
/// @param form the form laid out
/// @param[in,out] block the block
/// @param x the panel's left, pixels
/// @param y its top, pixels
/// @param room its width, pixels
/// @param rows the rows
/// @return the height taken, pixels
int add_rows(const Form& form, Block& block, int x, int y, int room, const std::vector<Row>& rows) {
    if (rows.empty())
        return 0;
    const Metrics& metrics = form.metrics;
    const int border = std::max(1, kit::canvas_pixels(form.type, 1.0f));
    const int pad_x = kit::canvas_pixels(form.type, metrics.row_pad_x);
    const int pad_y = kit::canvas_pixels(form.type, metrics.row_pad_y);
    const int icon = kit::canvas_pixels(form.type, metrics.row_icon);
    const int gap = kit::canvas_pixels(form.type, metrics.row_gap);
    const int least = kit::canvas_pixels(form.type, gf::min_button_points);
    const std::size_t panel_index = block.items.size();
    kit::plain_part(block.items, Role::panel, {x, y, room, 0}, gf::line_colour);
    int at = y + border;
    const int inner_x = x + border;
    const int inner_room = room - 2 * border;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const Row& row = rows[index];
        if (index > 0) {
            kit::plain_part(
                block.items, Role::divider, {inner_x, at, inner_room, border}, gf::line_colour
            );
            at += border;
        }
        // The texts are laid out first, to size the row.
        std::vector<kit::Item> texts;
        int right_edge = inner_x + inner_room - pad_x;
        const Lettering right_look =
            Lettering{metrics.row_detail, false, row.right_colour, TextStyle::row_detail};
        const int text_x = inner_x + pad_x + icon + gap;
        if (!row.right.empty()) {
            const int size = kit::canvas_font(form.type, right_look.points);
            const int wanted = kit::measured_width(form.type.measure, row.right, size, false);
            const int width = std::min(wanted, (right_edge - text_x) / 3);
            if (width > 0)
                right_edge -= width + gap;
        }
        const int text_room = std::max(0, right_edge - text_x);
        const Lettering title_look =
            Lettering{metrics.row_title, row.title_bold, row.title_colour, TextStyle::row_title};
        const int title_size = kit::canvas_font(form.type, title_look.points);
        const int title_height = kit::measured_line(form.type.measure, title_size, row.title_bold);
        int text_height = 0;
        if (kit::line_part(
                texts, form.type, text_x, title_height / 2, text_room, row.title, title_look
            ) > 0)
            text_height = title_height;
        for (const RowLine& line : row.lines)
            text_height += kit::text_part(
                texts,
                form.type,
                Role::text,
                {text_x, text_height},
                text_room,
                line.text,
                Lettering{metrics.row_detail, false, line.colour, TextStyle::row_detail},
                TextFit::column,
                line.most_lines,
                kit::path_break
            );
        const int height = std::max(least, text_height + 2 * pad_y);
        kit::plain_part(
            block.items, Role::row, {inner_x, at, inner_room, height}, gf::panel_colour
        );
        block.items.back().control = control_id(row.target);
        block.items.back().state.disabled = !row.enabled;
        const int middle = at + height / 2;
        kit::icon_part(
            block.items,
            {inner_x + pad_x, middle - icon / 2, icon, icon},
            row.glyph,
            row.glyph_colour,
            false
        );
        kit::move_parts(block.items, texts, at + (height - text_height) / 2);
        if (!row.right.empty())
            kit::line_part(
                block.items,
                form.type,
                inner_x + inner_room - pad_x,
                middle,
                (inner_x + inner_room - pad_x - text_x) / 3,
                row.right,
                right_look,
                TextFit::tight_right
            );
        at += height;
    }
    at += border;
    block.items[panel_index].rect.height = at - y;
    return at - y;
}

/// Adds the header bar: the OA badge, "OPEN ANNIHILATION  GAME FOLDER" and the version.
///
/// @param form the form laid out
/// @param[in,out] block the block (canvas coordinates)
/// @param safe the safe area, pixels
/// @return the header's bottom, pixels
int add_header(const Form& form, Block& block, Rect safe) {
    const Metrics& metrics = form.metrics;
    const Rect bar{
        safe.x, safe.y, safe.width, kit::canvas_pixels(form.type, metrics.header_height)
    };
    kit::plain_part(block.items, Role::header_bar, bar, gf::line_colour);
    const int pad = kit::canvas_pixels(form.type, metrics.header_pad);
    const int gap = kit::canvas_pixels(form.type, metrics.header_gap);
    const int middle = bar.y + bar.height / 2;
    const int badge = kit::canvas_pixels(form.type, metrics.badge);
    kit::Item badge_part{};
    badge_part.role = Role::badge;
    badge_part.rect = {bar.x + pad, middle - badge / 2, badge, badge};
    badge_part.colour = gf::green_colour;
    badge_part.glyph = Glyph::oa;
    block.items.push_back(std::move(badge_part));
    int right = bar.x + bar.width - pad;
    const int text_x = bar.x + pad + badge + gap;
    const int version = kit::line_part(
        block.items,
        form.type,
        right,
        middle,
        std::max(0, right - text_x),
        form.model.version,
        Lettering{metrics.version_text, false, gf::dim_colour, TextStyle::small},
        TextFit::tight_right,
        Role::version
    );
    if (version > 0)
        right -= version + gap;
    const int title = kit::line_part(
        block.items,
        form.type,
        text_x,
        middle,
        right - text_x,
        "OPEN ANNIHILATION",
        Lettering{metrics.header_text, true, gf::text_colour, TextStyle::header},
        TextFit::tight_left,
        Role::header_text
    );
    const int after = text_x + title + gap;
    if (title > 0 && right - after > kit::canvas_pixels(form.type, header_word_room_points))
        kit::line_part(
            block.items,
            form.type,
            after,
            middle,
            right - after,
            tr("GAME FOLDER"),
            Lettering{metrics.header_text, false, gf::dim_colour, TextStyle::header},
            TextFit::tight_left,
            Role::header_text
        );
    return bar.y + bar.height;
}

/// A view's blocks.
struct Column {
    Block top{};    ///< fixed above the rows
    Block body{};   ///< the rows, which scroll
    Block bottom{}; ///< fixed below: the buttons
};

/// Adds the title and the lines under it to a block.
///
/// @param form the form laid out
/// @param[in,out] block the block
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @param title the title
/// @param lines the lines under it, each a paragraph; empty ones are left out
/// @return the height taken, with the gap after it
int add_title(
    const Form& form,
    Block& block,
    int x,
    int room,
    std::string_view title,
    const std::vector<std::string>& lines
) {
    const Metrics& metrics = form.metrics;
    const int title_gap = kit::canvas_pixels(form.type, metrics.title_gap);
    int y = kit::text_part(
        block.items,
        form.type,
        Role::title,
        {x, 0},
        room,
        title,
        Lettering{metrics.title, true, gf::text_colour, TextStyle::title},
        TextFit::column,
        most_title_lines,
        kit::path_break
    );
    y += title_gap;
    for (const std::string& line : lines) {
        const int height = kit::text_part(
            block.items,
            form.type,
            Role::text,
            {x, y},
            room,
            line,
            Lettering{metrics.subtitle, false, gf::dim_colour, TextStyle::subtitle},
            TextFit::column,
            most_subtitle_lines,
            kit::path_break
        );
        if (height > 0)
            y += height + title_gap;
    }
    return y - title_gap + kit::canvas_pixels(form.type, metrics.block_gap);
}

/// Returns the words that start a row's title, its first letter raised: "Your Steam library".
///
/// @param words the words
/// @return the title
std::string raised_first(std::string words) {
    if (!words.empty() && words[0] >= 'a' && words[0] <= 'z')
        words[0] = static_cast<char>(words[0] - 'a' + 'A');
    return words;
}

/// Lays out the list: the found folders, the other ways to a folder, and Quit.
///
/// @param form the form laid out
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column list_column(const Form& form, int x, int room) {
    const Model& model = form.model;
    const Metrics& metrics = form.metrics;
    const int block_gap = kit::canvas_pixels(form.type, metrics.block_gap);
    Column column;
    std::string lead;
    if (model.installs.size() > 1)
        lead =
            tr("Total Annihilation is on this computer in more than one place. Choose the one "
               "to play.");
    else if (model.installs.size() == 1)
        lead =
            tr("Total Annihilation was found on this computer. Play it, or choose another "
               "folder.");
    else
        lead =
            tr("Choose the folder that holds totala1.hpi, or the folder that holds the "
               "installer of the Total Annihilation demo (1997).");
    int y =
        add_title(form, column.top, x, room, tr("Choose your Total Annihilation folder"), {lead});
    if (!model.notice.empty())
        y += add_banner(
                 form, column.top, x, y, room, model.notice, Glyph::warning, gf::amber_colour
             ) +
             block_gap;
    column.top.height = y;

    int at = 0;
    if (!model.installs.empty()) {
        std::vector<Row> rows;
        for (std::size_t index = 0; index < model.installs.size(); ++index) {
            const InstallRow& install = model.installs[index];
            Row row;
            row.glyph = install.usable ? Glyph::check : Glyph::cross;
            row.glyph_colour = install.usable ? gf::green_colour : gf::red_colour;
            row.title = raised_first(install.source);
            row.title_colour = install.usable ? gf::text_colour : gf::dim_colour;
            row.lines.push_back({install.folder, gf::dim_colour, 1});
            row.lines.push_back(
                {install.verdict,
                 install.usable ? gf::green_colour : gf::red_colour,
                 most_verdict_lines}
            );
            row.target = {TargetKind::install, static_cast<uint16_t>(index)};
            row.enabled = install.usable;
            rows.push_back(std::move(row));
        }
        at += add_rows(form, column.body, x, at, room, rows) + block_gap;
        at += kit::text_part(
                  column.body.items,
                  form.type,
                  Role::text,
                  {x, at},
                  room,
                  tr("Or find the folder yourself:"),
                  Lettering{metrics.heading, true, gf::dim_colour, TextStyle::small},
                  TextFit::column,
                  0,
                  kit::path_break
              ) +
              kit::canvas_pixels(form.type, metrics.title_gap);
    }
    std::vector<Row> ways;
    Row browse;
    browse.glyph = Glyph::folder;
    browse.glyph_colour = gf::green_colour;
    browse.title = tr("Browse\xE2\x80\xA6");
    browse.lines.push_back(
        {tr("Look through this computer's folders, drives and SD cards."), gf::dim_colour, 2}
    );
    browse.target = {TargetKind::browse, 0};
    ways.push_back(std::move(browse));
    Row demo;
    demo.glyph = Glyph::file;
    demo.glyph_colour = gf::green_colour;
    demo.title = tr("The Total Annihilation demo (1997)");
    demo.lines.push_back(
        {tr("Choose the folder its installer is in, usually Downloads."), gf::dim_colour, 2}
    );
    demo.target = {TargetKind::demo, 0};
    ways.push_back(std::move(demo));
    if (model.offers_dialog) {
        Row dialog;
        dialog.glyph = Glyph::folder;
        dialog.glyph_colour = gf::dim_colour;
        dialog.title = tr("Use the desktop's folder dialog");
        dialog.lines.push_back(
            {tr("Choose the folder in the system's own window."), gf::dim_colour, 2}
        );
        dialog.target = {TargetKind::dialog, 0};
        ways.push_back(std::move(dialog));
    }
    at += add_rows(form, column.body, x, at, room, ways);
    column.body.height = at;

    column.bottom.height = button_row_of(
        form, column.bottom, x, 0, room, {}, {button_of(tr("QUIT"), {TargetKind::quit, 0})}
    );
    return column;
}

/// Returns the name a browser's title shows for a folder: its last component, or the whole
/// path for a drive's root.
///
/// @param folder the folder, UTF-8
/// @return the name
std::string folder_title(std::string_view folder) {
    std::string_view trimmed = folder;
    while (trimmed.size() > 1 && (trimmed.back() == '/' || trimmed.back() == '\\'))
        trimmed.remove_suffix(1);
    const std::size_t separator = trimmed.find_last_of("/\\");
    if (separator == std::string_view::npos || separator + 1 >= trimmed.size())
        return std::string(folder);
    const std::string_view name = trimmed.substr(separator + 1);
    // A drive's root ("C:\") keeps its separator.
    if (name.size() == 2 && name[1] == ':')
        return std::string(folder);
    return std::string(name);
}

/// Lays out the browser: the folder, its places, its verdict, its folders, Back and "Play this
/// folder".
///
/// @param form the form laid out
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column browse_column(const Form& form, int x, int room) {
    const Model& model = form.model;
    const Metrics& metrics = form.metrics;
    const int block_gap = kit::canvas_pixels(form.type, metrics.block_gap);
    Column column;
    int y = add_title(
        form, column.top, x, room, folder_title(model.folder), {model.folder, model.hint}
    );
    if (!model.notice.empty())
        y += add_banner(
                 form, column.top, x, y, room, model.notice, Glyph::warning, gf::amber_colour
             ) +
             block_gap;
    std::vector<kit::ButtonSpec> places;
    for (std::size_t index = 0; index < model.place_labels.size(); ++index)
        places.push_back(
            button_of(model.place_labels[index], {TargetKind::place, static_cast<uint16_t>(index)})
        );
    places.push_back(
        button_of(tr("PARENT FOLDER"), {TargetKind::parent, 0}, model.has_parent, Glyph::folder)
    );
    y += button_row_of(form, column.top, x, y, room, places, {}) + block_gap;
    if (!model.folder_verdict.empty()) {
        // The sentence is translated whole, with the verdict in its place.
        std::string text = model.folder_verdict;
        if (model.folder_usable) {
            text = tr("This folder can be played: {folder}");
            if (const auto at = text.find("{folder}"); at != std::string::npos)
                text.replace(at, std::string_view("{folder}").size(), model.folder_verdict);
        }
        y += add_banner(
                 form,
                 column.top,
                 x,
                 y,
                 room,
                 text,
                 model.folder_usable ? Glyph::check : Glyph::info,
                 model.folder_usable ? gf::green_colour : gf::dim_colour
             ) +
             block_gap;
    }
    column.top.height = y;

    if (model.entries.empty()) {
        column.body.height = kit::text_part(
            column.body.items,
            form.type,
            Role::text,
            {x, 0},
            room,
            tr("No folders here."),
            Lettering{metrics.subtitle, false, gf::dim_colour, TextStyle::body},
            TextFit::column,
            0,
            kit::path_break
        );
    } else {
        std::vector<Row> rows;
        rows.reserve(model.entries.size());
        for (std::size_t index = 0; index < model.entries.size(); ++index) {
            const FolderEntry& entry = model.entries[index];
            Row row;
            row.glyph = entry.game_folder ? Glyph::check : Glyph::folder;
            row.glyph_colour = entry.game_folder ? gf::green_colour : gf::dim_colour;
            row.title = entry.name;
            row.title_bold = entry.game_folder;
            if (entry.game_folder) {
                row.right = tr("Total Annihilation");
                row.right_colour = gf::green_colour;
            }
            row.target = {TargetKind::entry, static_cast<uint16_t>(index)};
            rows.push_back(std::move(row));
        }
        column.body.height = add_rows(form, column.body, x, 0, room, rows);
    }

    column.bottom.height = button_row_of(
        form,
        column.bottom,
        x,
        0,
        room,
        {button_of(tr("BACK"), {TargetKind::back, 0})},
        {button_of(
            tr("PLAY THIS FOLDER"),
            {TargetKind::choose, 0},
            model.folder_usable,
            Glyph::play,
            Role::button_main
        )}
    );
    return column;
}

} // namespace

Layout lay_out(
    const Model& model,
    const UiState& state,
    const Viewport& viewport,
    const TextMeasureHooks& measure
) {
    Layout layout;
    layout.paint.device = gf::device_class(viewport);
    const float scale = viewport.px_per_point > 0.0f ? viewport.px_per_point : 1.0f;
    layout.paint.px_per_point = scale;
    const Rect safe{
        viewport.safe.left,
        viewport.safe.top,
        viewport.width - viewport.safe.left - viewport.safe.right,
        viewport.height - viewport.safe.top - viewport.safe.bottom,
    };
    if (safe.width <= 0 || safe.height <= 0)
        return layout;
    const bool compact = layout.paint.device == gf::DeviceClass::phone ||
                         static_cast<float>(std::min(viewport.width, viewport.height)) / scale <
                             compact_short_side_points;
    const Form form{
        model, compact ? compact_metrics : tablet_metrics, kit::Typesetter{measure, scale}
    };
    const Metrics& metrics = form.metrics;
    std::vector<kit::Item>& parts = layout.paint.list.items;

    Block header;
    const int header_bottom = add_header(form, header, safe);
    kit::move_parts(parts, header.items, 0);

    int side = kit::canvas_pixels(form.type, metrics.side_pad);
    if (metrics.max_content_width > 0.0f)
        side = std::max(
            side, (safe.width - kit::canvas_pixels(form.type, metrics.max_content_width)) / 2
        );
    side = std::min(side, safe.width / 4);
    const int x = safe.x + side;
    const int room = safe.width - 2 * side;
    Column column =
        model.view == View::browse ? browse_column(form, x, room) : list_column(form, x, room);

    const int area_top = header_bottom + kit::canvas_pixels(form.type, metrics.content_top);
    const int area_bottom =
        safe.y + safe.height - kit::canvas_pixels(form.type, metrics.content_bottom);
    const Rect area{safe.x, area_top, safe.width, std::max(1, area_bottom - area_top)};
    const kit::ScrollColumn placed = kit::scroll_column(
        area,
        column.top.height,
        column.body.height,
        column.bottom.height,
        kit::canvas_pixels(form.type, metrics.block_gap),
        kit::canvas_pixels(form.type, least_body_points),
        kit::canvas_pixels(form.type, static_cast<float>(state.scroll_points))
    );
    kit::move_parts(parts, column.top.items, placed.top, placed.top_clip);
    kit::move_parts(parts, column.body.items, placed.body, placed.body_clip);
    kit::move_parts(parts, column.bottom.items, placed.bottom, placed.bottom_clip);
    layout.paint.rows = placed.scroll.view;
    const int32_t limit = placed.scroll.limit;
    layout.scroll_max_points =
        limit > 0 ? static_cast<int32_t>(std::ceil(static_cast<float>(limit) / scale)) : 0;
    layout.paint.scroll_max_points = layout.scroll_max_points;

    // The targets' parts become the list's controls, the top one first; those in the
    // scrolling region are its scroll area, and the enabled ones in order are the focus order.
    kit::list_part_controls(layout.paint.list);
    for (kit::Control& control : layout.paint.list.controls)
        if (control.clip.width > 0 && control.clip.height > 0)
            control.group = rows_area_group;
    for (const kit::ControlId id : layout.paint.list.tab_order)
        layout.focus_order.push_back(target_of(id));
    // The focus and the press the chooser keeps show on the parts.
    kit::Interaction shown{};
    shown.focus_shown = state.focus_shown;
    if (state.focus >= 0 && static_cast<std::size_t>(state.focus) < layout.focus_order.size())
        shown.focused = control_id(layout.focus_order[static_cast<std::size_t>(state.focus)]);
    if (const kit::Item* held = target_part(layout.paint.list, state.pressed);
        held != nullptr && !held->state.disabled)
        shown.pressed = held->control;
    kit::mark_states(layout.paint.list, shown);
    return layout;
}

} // namespace oa::ui::folder_chooser
