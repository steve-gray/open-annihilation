// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Provisional: this header changes whenever OA's screens need it to, until the kit is declared stable (src/ui/kit/README.md).

// The kit's list rows, chips, text and search fields, tabs, cards and their
// grid, hover cards and links, drawn for the crisp backend. Their colours are
// kit tokens and their sizes compact_metrics. Texts arrive already looked
// up; a field holds the player's own text. paint draws their items.
#pragma once

#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/looks.hpp"
#include "oa/ui/kit/text.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::kit {

/// Returns a one-bit mark's size.
///
/// @param mark which mark
/// @return its columns and rows
[[nodiscard]] Point mark_size(Mark mark) noexcept;

/// Returns a text cut to fit a width: the text itself when it fits, and
/// otherwise as many whole characters as fit before "...".
///
/// @param fonts the fonts
/// @param role the font it is drawn in
/// @param text the text, in UTF-8
/// @param width the room, in points
/// @return the text to draw; "..." alone when not even one character fits
[[nodiscard]] std::string
cut_to_width(const Fonts& fonts, FontRole role, std::string_view text, int32_t width);

// ---- List rows ----

/// Returns where a list row draws its chip: at the row's right inset,
/// centred down the row, chip_height high and as wide as chip_width.
///
/// A screen whose row chip takes a press adds the chip with this rectangle
/// and its own control before the row, so that a press there reaches it.
///
/// @param fonts the fonts
/// @param row the row
/// @param chip the chip
/// @return the chip's rectangle
[[nodiscard]] Rect list_row_chip_rect(const Fonts& fonts, const Rect& row, const ChipLook& chip);

/// Draws a list row.
///
/// The face is the hover colour while hovered; a selected row is tinted
/// with the accent and outlined in it. The badge is a square of the row's
/// height less two insets, at most list_row_badge, at the left inset and
/// centred down the row: the picture when it is drawable, else the badge
/// text on its colour, else a blank dashed square. Right of it, the title in
/// the regular font with the byline after it in the small font, then each
/// line in the small font one under another, each cut with "..." to its
/// room, and the tags as chips on the line after the last. The text is
/// centred down the row; in a row short_list_row_height high it lies as the
/// Mods row's does. The aside is right-aligned in the hint colour, one entry
/// a line from the title's, left of the chip, which sits at the right inset.
///
/// @param canvas where it is drawn
/// @param rect the row; its height chooses the short form, the full form or a taller one
/// @param look what the row shows
void draw_list_row(const Canvas& canvas, const Rect& rect, const ListRowLook& look);

/// Appends a list row: one item and one control of kind list_item, which
/// does not take steps, checked while selected, its text the title.
///
/// @param[in,out] list the display list
/// @param rect the row
/// @param look its look
/// @param id the control's number
/// @param name the control's automation name
void add_list_row(
    DisplayList& list, const Rect& rect, const ListRowLook& look, ControlId id, std::string name
);

// ---- Chips ----

/// Returns a chip's width: its text in the small font and chip_padding
/// either side.
///
/// @param fonts the fonts
/// @param look the chip
/// @return the width, in points
[[nodiscard]] int32_t chip_width(const Fonts& fonts, const ChipLook& look);

/// Draws a chip: an outline and its text centred in the small font, in its
/// state's colours. Get draws the hover border and the hint, installed the
/// accent, update the lock colour, online the online colour and problem the
/// danger colour; playing is filled with the accent under on_accent text. A
/// filter draws the border and the hint, the hover border while hovered, and
/// while selected the list's selected face, an accent outline and the text
/// colour. A hovered chip that is not filled takes the hover face. A
/// disabled chip draws its outline and text in the idle colour, whatever its
/// state.
///
/// @param canvas where it is drawn
/// @param rect the chip
/// @param look its text and state
void draw_chip(const Canvas& canvas, const Rect& rect, const ChipLook& look);

/// Appends a chip: one item and, with a control number, one control of
/// kind button that does not take steps, checked while selected, enabled
/// unless disabled, its text the chip's. With no_control only the item is
/// added, as for a chip inside a row or on a card.
///
/// @param[in,out] list the display list
/// @param rect the chip
/// @param look its look
/// @param id the control's number, or no_control for the item alone
/// @param name the control's automation name
void add_chip(
    DisplayList& list, const Rect& rect, const ChipLook& look, ControlId id, std::string name
);

// ---- Text and search fields ----

/// Returns the rectangle a field's text is drawn in: inside field_inset,
/// and after the magnifier when it has one.
///
/// @param rect the field
/// @param look the field's look
/// @return the text's rectangle
[[nodiscard]] Rect field_text_rect(const Rect& rect, const SearchLook& look) noexcept;

/// Returns how far a field's text is moved left so that its caret shows.
///
/// 0 while the text before the caret and the caret fit the text's room;
/// otherwise just enough that the caret stands at the room's right.
///
/// @param fonts the fonts
/// @param rect the field
/// @param look the field's text, caret and magnifier
/// @return the columns the text is moved left
[[nodiscard]] int32_t field_scroll(const Fonts& fonts, const Rect& rect, const SearchLook& look);

/// Draws a text field: the well and its border (the hover border while
/// hovered, the accent while focused), the magnifier when the look has one,
/// and the placeholder in the hint colour while the text is empty, else the
/// text in the regular font, moved left so the caret shows. While focused,
/// a caret_width caret in the text colour stands at the caret.
///
/// @param canvas where it is drawn
/// @param rect the field
/// @param look its text, caret and state
void draw_search(const Canvas& canvas, const Rect& rect, const SearchLook& look);

/// Appends a text field: one item and one control of kind text_field that
/// takes steps, so Left and Right go to it to move its caret, its text the
/// field's.
///
/// @param[in,out] list the display list
/// @param rect the field
/// @param look its look
/// @param id the control's number
/// @param name the control's automation name
void add_search(
    DisplayList& list, const Rect& rect, const SearchLook& look, ControlId id, std::string name
);

// ---- Tabs ----

/// Returns each tab's rectangle, left to right from the strip's left, each
/// as tall as the strip: its caption in the small font with tab_tracking,
/// tab_padding either side, and its count after the caption when it is
/// above 0.
///
/// @param fonts the fonts
/// @param strip the strip, tab_height high
/// @param look the captions and counts
/// @return one rectangle per caption
[[nodiscard]] std::vector<Rect>
tab_rects(const Fonts& fonts, const Rect& strip, const TabsLook& look);

/// Draws a strip of tabs: a hairline along the strip's foot, each caption
/// in the small font, the selected one in the text colour over a tab_rule
/// accent rule along the foot, the hovered one in the text colour and the
/// others in the hint colour, and each count above 0 in a pill of the lock
/// colour with on_accent digits. Without fonts only the hairline is drawn.
///
/// @param canvas where it is drawn
/// @param strip the strip
/// @param look the captions, the counts and the state
void draw_tabs(const Canvas& canvas, const Rect& strip, const TabsLook& look);

/// Appends a strip of tabs: one item for the strip and one control per tab,
/// of kind tab, which does not take steps (Left and Right move the focus),
/// checked on the selected tab, its text the caption and its count
/// ("Updates 2"). The screen opens a tab when Space or Enter goes to it.
///
/// @param[in,out] list the display list
/// @param fonts the fonts the strip is drawn in, which place the tabs
/// @param strip the strip
/// @param look the captions, the counts and the state
/// @param ids one control number per tab
/// @param names one automation name per tab
void add_tabs(
    DisplayList& list,
    const Fonts& fonts,
    const Rect& strip,
    const TabsLook& look,
    std::span<const ControlId> ids,
    std::span<const std::string> names
);

// ---- Cards ----

/// Returns a card's height for its width: the square preview's side, the
/// title's and the subtitle's lines and three insets.
///
/// @param width the card's width, in points
/// @return the height, in points
[[nodiscard]] int32_t card_height(int32_t width) noexcept;

/// Places cards in a grid, through grid.
///
/// With columns 0, as many columns as fit at card_least_width, and at least
/// one; otherwise exactly that many. Each column is as wide as the area
/// allows, card_gap apart, the spare points going to the columns on the
/// left; fewer cards than columns keep those widths. Each cell is
/// card_height of its width high, and the rows lie card_gap apart.
///
/// @param area the area, in points
/// @param count the cards
/// @param columns the columns, or 0 for as many as fit
/// @return the cells, row by row
[[nodiscard]] std::vector<Rect>
card_cells(const Rect& area, std::size_t count, std::size_t columns = 0);

/// Draws a card: its face and outline (the accent while selected, the hover
/// border while hovered), the square preview inset from its edges holding
/// the picture, or the placeholder centred in the hint colour on the well,
/// the chips at the preview's bottom left, then the title in the regular
/// font and the subtitle in the small font, each cut with "...". A greyed
/// card is drawn under the panel blended at locked_fade.
///
/// @param canvas where it is drawn
/// @param rect the card
/// @param look what it shows
void draw_card(const Canvas& canvas, const Rect& rect, const CardLook& look);

/// Appends a card: one item and one control of kind list_item, which does
/// not take steps, so the arrows move through a grid by where cards sit;
/// checked while selected, its text the title. A greyed card still takes a
/// press.
///
/// @param[in,out] list the display list
/// @param rect the card
/// @param look its look
/// @param id the control's number
/// @param name the control's automation name
void add_card(
    DisplayList& list, const Rect& rect, const CardLook& look, ControlId id, std::string name
);

// ---- Hover cards ----

/// Returns a hover card's size, its arrow not counted: its width, or with
/// width 0 its widest line, and hover_card_padding either side; its lines,
/// small_line each, and hover_card_padding over and under them.
///
/// @param fonts the fonts
/// @param look the card
/// @return the width and height, in points
[[nodiscard]] Point hover_card_size(const Fonts& fonts, const HoverCardLook& look);

/// Returns where a hover card of a size lies by what it describes: its left
/// at the anchor's left, hover_card_arrow under the anchor, or over it when
/// the frame has no room below, kept inside the frame. A card larger than
/// the frame starts at the frame's left or top.
///
/// @param anchor what the card describes
/// @param frame the room it must stay in
/// @param size the card's size, from hover_card_size
/// @return the card's rectangle
[[nodiscard]] Rect hover_card_rect(const Rect& anchor, const Rect& frame, Point size) noexcept;

/// Draws a hover card on the band inside a border of the lock colour: the
/// header (the small OA mark, the title in the text colour and the aside
/// right-aligned in the hint colour), each row (its label in the hint colour
/// in a column as wide as the widest label and hover_card_padding, its value
/// in its colour, from the left when the label is empty), the block's lines
/// in the quiet colour, every text in the small font and cut with "...", and
/// the arrow: a triangle in the border's colour reaching hover_card_arrow
/// out of its edge, its point arrow_at along that edge.
///
/// @param canvas where it is drawn
/// @param rect the card, its arrow outside it
/// @param look what it shows
void draw_hover_card(const Canvas& canvas, const Rect& rect, const HoverCardLook& look);

/// Appends a hover card. It adds no control.
///
/// @param[in,out] list the display list
/// @param rect the card, its arrow outside it
/// @param look its look
void add_hover_card(DisplayList& list, const Rect& rect, const HoverCardLook& look);

/// How long the pointer rests on one control before its hover card shows, in milliseconds.
inline constexpr uint64_t hover_card_delay_ms = 400;

/// When the pointer settled on the control it rests on. A screen keeps one
/// in its own state.
struct HoverTimer {
    ControlId over{no_control}; ///< the control the pointer rests on, or none
    uint64_t since_ms{};        ///< when it settled there, in milliseconds
};

/// Tells whether a hover card is due, and follows the pointer.
///
/// The wait starts when the pointer settles on a control. Another control
/// starts it again, and no control ends it. The card is due once
/// hover_card_delay_ms have passed since the pointer settled.
///
/// @param[in,out] timer the control the pointer rests on and since when
/// @param hovered the control under the pointer now, or no_control
/// @param now_ms the time now, in milliseconds, from the clock that set since_ms
/// @return true when the card is due
[[nodiscard]] bool hover_card_due(HoverTimer& timer, ControlId hovered, uint64_t now_ms) noexcept;

// ---- Links ----

/// Returns a link's width: its caption in the small font.
///
/// @param fonts the fonts
/// @param look the link
/// @return the width, in points
[[nodiscard]] int32_t link_width(const Fonts& fonts, const LinkLook& look);

/// Draws a link: its caption in the small font at the rectangle's left, in
/// the accent, the light accent while hovered and the idle colour while
/// disabled.
///
/// @param canvas where it is drawn
/// @param rect the link, small_line high
/// @param look its caption and state
void draw_link(const Canvas& canvas, const Rect& rect, const LinkLook& look);

/// Appends a link: one item and one control of kind link that does not take
/// steps, enabled as the look is, its text the caption.
///
/// @param[in,out] list the display list
/// @param rect the link
/// @param look its look
/// @param id the control's number
/// @param name the control's automation name
void add_link(
    DisplayList& list, const Rect& rect, const LinkLook& look, ControlId id, std::string name
);

} // namespace oa::ui::kit
