#ifndef UI_CORE_H
#define UI_CORE_H

/**
 * @file ui_core.h
 * @brief Pure logic of lcd_ui: cell layout, word wrap, fixed fields, progress cells, menu
 *        window, CGRAM glyph planning, handles.
 *
 * Only standard headers: everything here is tested on the PC
 * (test_host/test_ui_core.c). ui.c turns these results into lcd.h calls.
 *
 *   ui_screen_end()     -> ui_layout_compute()   title row, body lines, flex, footer, overflow
 *                       -> ui_glyph_plan()       custom characters -> the 8 CGRAM slots
 *   every text cell     -> ui_cells_put()        aligned, cut text in a fixed span of cells
 *   ui_message*()       -> ui_msg_fit()          word wrap, else character wrap, else cut
 *   ui_value_set()      -> ui_fmt_fixed()        fixed-width value field
 *   live list lines     -> ui_fmt_value_row()    label + field pairs on one line (JOINTS)
 *   ui_counter_set()    -> ui_fmt_counter()      centered integer field
 *   ui_input_set()      -> ui_input_format()     ">" + tail + "_" padded to the span
 *   ui_progress*()      -> ui_progress_levels()  5 dots per cell; ui_progress_glyph()
 *   ui_menu*()          -> ui_menu_window()      which items the rows show
 *                       -> ui_scroll_indicator()   up/down scroll arrow of each row
 *                       -> ui_menu_label_fits()  marker + label + gap + indicator
 *                       -> ui_scroll_glyph_count() / ui_scroll_arrow_glyph()  CGRAM arrows
 *   ui_list*()          -> ui_list_top()         window of a scrolled list of lines
 *                       -> ui_list_line_fits()   line + gap + indicator
 *                       -> ui_scroll_indicator() (shared with the menu)
 *   ui_steps*()         -> ui_step_line()        "<k> <name>" of the current step
 *   widget handles      -> ui_handle_make(), ui_handle_split(), ui_gen_next()
 *
 * Units are character cells: a widget occupies (row, col, width, height).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Bytes per custom character (5x8 dots, top to bottom, bit 4 = leftmost dot). */
#define UI_GLYPH_ROWS 8

/** Custom character slots of the HD44780 CGRAM. */
#define UI_CGRAM_SLOTS 8

/** Dots per cell horizontally: one progress cell holds 5 steps. */
#define UI_CELL_DOTS 5

/** Distinct partial levels of a progress cell that need a custom glyph (1..4 dots). */
#define UI_PROGRESS_GLYPHS (UI_CELL_DOTS - 1)

/** Character code of the solid 5x8 block in ROM A00 (HD44780U table 4). */
#define UI_CHAR_FULL_BLOCK ((char)0xFF)

/** Largest widget index a handle can carry (the pool is smaller). */
#define UI_HANDLE_MAX_INDEX 254u

/** One custom character: UI_GLYPH_ROWS rows, 5 low bits used. */
typedef uint8_t ui_glyph_t[UI_GLYPH_ROWS];

/**
 * @brief Horizontal alignment of a text inside its span of cells.
 */
typedef enum {
    UI_ALIGN_LEFT,   /**< Flush with the first cell. */
    UI_ALIGN_CENTER, /**< Centered (odd leftover cell goes to the right). */
    UI_ALIGN_RIGHT,  /**< Flush with the last cell. */
} ui_align_t;

/**
 * @brief One line produced by ui_wrap() / ui_msg_fit(): a slice of the input text.
 */
typedef struct {
    size_t start; /**< Offset of the first character in the text. */
    size_t len;   /**< Number of characters. */
} ui_line_t;

/**
 * @brief One widget as seen by the layout: its size request, its placement, and the result.
 */
typedef struct {
    int w;       /**< In: width in cells (> 0), or 0 = the rest of the row from its column. */
    int h;       /**< In: height in rows (> 0), or 0 = FLEX (shares the free body rows). */
    bool beside; /**< In: on the row of the previous body item, gap cells after its end. */
    int gap;     /**< In: blank cells before a beside item (>= 0). */
    bool footer; /**< In: pinned to the bottom rows (one row each, stacked in order). */
    int row;     /**< Out: first row. */
    int col;     /**< Out: first column. */
    int width;   /**< Out: final width in cells. */
    int height;  /**< Out: final height in rows. */
} ui_layout_item_t;

/**
 * @brief Screen geometry for ui_layout_compute().
 */
typedef struct {
    int cols;   /**< Columns of the screen (16). */
    int rows;   /**< Rows of the screen (2). */
    bool title; /**< Row 0 is the title row. */
} ui_layout_params_t;

/**
 * @brief Result of ui_layout_compute() (besides the per-item placement).
 */
typedef struct {
    int title_row;   /**< 0 with a title, -1 without. */
    int body_top;    /**< First row available to body items. */
    int body_bottom; /**< First row NOT available to body items (= first footer row). */
    int overflow;    /**< -1 if everything fits, else index of the first item that does not. */
} ui_layout_t;

/**
 * @brief Computes where every widget of a screen goes, in character cells.
 *
 * Rules:
 * - Title: with p->title, row 0 is the title; the body starts at row 1.
 * - Footer: footer items take one row each at the bottom, stacked in creation order, from
 *   column 0 (width w, or the whole row for w == 0). The body ends above them. A footer
 *   item with h > 1, a beside one, or one that would cover the title overflows.
 * - Body lines: every non-beside body item starts a new line at column 0; a beside item
 *   goes on the same line, gap cells after the end of the previous body item, top-aligned.
 *   A line is as tall as its tallest fixed item; a line with a FLEX item gets an equal
 *   share of the rows the fixed lines leave (the last flex line takes the remainder), and
 *   every flex item in it gets the whole line height. Lines stack from body_top.
 * - Width: w cells, or w == 0 = from its column to the right edge.
 * - Overflow: the first item that is cut by the right edge or the body bottom, gets no
 *   cell, is a beside item without a previous body item, or is a flex line with fewer
 *   than 1 row, is reported in out->overflow; the placements are still filled in.
 *
 * @param p     Geometry.
 * @param items In: requests; out: placement. May be NULL if n is 0.
 * @param n     Number of items.
 * @param out   Output: title/body rows and the overflow index.
 * @return true if everything fits (out->overflow == -1).
 */
bool ui_layout_compute(const ui_layout_params_t *p, ui_layout_item_t *items, size_t n,
                       ui_layout_t *out);

/**
 * @brief Writes a text into a span of cells: aligned, cut to the span, padded with spaces.
 *
 * Exactly width cells are written (no NUL), so it always covers the previous content.
 *
 * @param dst   First cell of the span.
 * @param width Cells in the span.
 * @param src   Text (NULL counts as empty); only its first len bytes are used.
 * @param len   Characters of src (cut to width).
 * @param align Alignment inside the span.
 */
void ui_cells_put(char *dst, size_t width, const char *src, size_t len, ui_align_t align);

/**
 * @brief Word-wraps a string into lines of at most cols characters.
 *
 * Lines break at spaces; spaces at a break are dropped (a line never starts or ends with
 * a space). A word longer than cols is HARD-BROKEN into cols-sized pieces. Only ' ' is a
 * separator.
 *
 * @param text      NUL-terminated string (NULL counts as empty).
 * @param cols      Characters per line (0 produces no lines).
 * @param lines     Output: the first max_lines lines (may be NULL if max_lines is 0).
 * @param max_lines Capacity of lines.
 * @return Number of lines the whole text needs (may be > max_lines). 0 for an empty or
 *         all-space text.
 */
size_t ui_wrap(const char *text, size_t cols, ui_line_t *lines, size_t max_lines);

/**
 * @brief Fits a message into cols x rows cells.
 *
 * First the word wrap of ui_wrap(). If it needs more than rows lines, the text is cut
 * into plain cols-character slices instead (character wrap: words may break anywhere,
 * spaces are kept), so any text of up to cols * rows characters always fits. Lines that
 * still do not fit are dropped (*n_lines is clamped to rows and max_lines).
 *
 * @param text      NUL-terminated message (NULL counts as empty).
 * @param cols      Characters per line.
 * @param rows      Lines available.
 * @param lines     Output lines.
 * @param max_lines Capacity of lines.
 * @param n_lines   Output: lines to draw. 0 for an empty text.
 * @return true if the whole text is shown (nothing dropped).
 */
bool ui_msg_fit(const char *text, size_t cols, size_t rows, ui_line_t *lines,
                size_t max_lines, size_t *n_lines);

/**
 * @brief Formats a number into a field of exactly width characters, right-aligned.
 *
 * The field always has width characters, so drawing it covers the previous one. If the
 * formatted number is longer than the field, the field is filled with '#' (like a
 * spreadsheet) instead of being cut.
 *
 * @param out      Output buffer, at least width + 1 bytes.
 * @param out_size Size of out.
 * @param fmt      printf format with one double conversion (e.g. "%+04.0f").
 * @param value    Value.
 * @param width    Field width in characters (>= 1).
 * @return width, or 0 if fmt is NULL, width is 0 or out is too small (then out is "" if
 *         out_size > 0).
 */
size_t ui_fmt_fixed(char *out, size_t out_size, const char *fmt, double value, size_t width);

/**
 * @brief Formats an integer centered in a field of exactly width characters.
 *
 * '#' characters fill the field if the number does not fit.
 *
 * @param out      Output buffer, at least width + 1 bytes.
 * @param out_size Size of out.
 * @param value    Value ("%d").
 * @param width    Field width in characters (>= 1).
 * @return width, or 0 on a bad argument (then out is "" if out_size > 0).
 */
size_t ui_fmt_counter(char *out, size_t out_size, int value, size_t width);

/**
 * @brief Formats count "label + field" pairs back to back on one line (no separator).
 *
 * Every field is ui_fmt_fixed() of width characters ('#' when the number does not fit), or
 * width spaces when values is NULL (no sample yet). So the line always has the same
 * length, and a new sample changes only the cells of the fields whose text changed.
 *
 * @param out      Output buffer.
 * @param out_size Size of out.
 * @param labels   count labels (NULL entries count as empty).
 * @param fmt      printf format with one double conversion.
 * @param values   count values, or NULL for blank fields.
 * @param count    Number of pairs.
 * @param width    Field width in characters (>= 1).
 * @return Length of the line, or 0 if it does not fit out or an argument is bad (then out
 *         is "" if out_size > 0).
 */
size_t ui_fmt_value_row(char *out, size_t out_size, const char *const *labels, const char *fmt,
                        const double *values, size_t count, size_t width);

/**
 * @brief Builds the live input line: ">" + the tail of line + "_", padded with spaces.
 *
 * The result is exactly cols characters. When line is longer than cols - 2 characters,
 * only its last cols - 2 are shown.
 *
 * @param line     Text being typed (NULL counts as empty).
 * @param cols     Width of the line in characters (>= 2).
 * @param out      Output buffer, at least cols + 1 bytes.
 * @param out_size Size of out.
 * @return cols, or 0 if cols < 2 or out is too small (then out is "" if out_size > 0).
 */
size_t ui_input_format(const char *line, size_t cols, char *out, size_t out_size);

/**
 * @brief Splits a progress bar of width cells into per-cell fill levels.
 *
 * The bar has width * 5 steps (one per dot column); percent is rounded down to whole
 * steps. Cells fill from the left: full cells are 5, at most one partial cell is 1..4,
 * the rest are 0.
 *
 * @param percent 0..100 (larger counts as 100).
 * @param width   Cells of the bar.
 * @param levels  Output: width levels, 0..5.
 * @return Filled steps, 0..width * 5.
 */
size_t ui_progress_levels(unsigned percent, size_t width, uint8_t *levels);

/**
 * @brief Glyph of a partially filled progress cell: the leftmost level dot columns lit.
 *
 * @param level 1..UI_PROGRESS_GLYPHS (0 gives an empty glyph, 5 or more a full one).
 * @param out   Output glyph.
 */
void ui_progress_glyph(unsigned level, ui_glyph_t out);

/**
 * @brief First item a menu window shows so that the selection is visible.
 *
 * Scrolls as little as possible from the window shown now: up when the selection is above
 * it, down when it is below it (so a wrap from the last item to the first jumps to the top).
 * Never leaves empty rows at the end when the menu has at least rows items.
 *
 * @param sel  Selected item (< n).
 * @param n    Number of items.
 * @param rows Rows of the window (>= 1).
 * @param top  First item shown now.
 * @return The first item to show.
 */
size_t ui_menu_window(size_t sel, size_t n, size_t rows, size_t top);

/** Cells of a menu row before the label: the selection marker. */
#define UI_MENU_MARKER_COLS 1

/**
 * Cells reserved at the right of every row of a scrollable widget (ui_menu(), ui_list()):
 * a blank gap, then the scroll indicator.
 */
#define UI_SCROLL_SIDE_COLS 2

/** Custom characters of a widget that can scroll: the up arrow (0), the down arrow (1). */
#define UI_SCROLL_GLYPHS 2

/**
 * @brief What the indicator cell (the last one) of a row of a scrollable widget shows.
 */
typedef enum {
    UI_SCROLL_IND_NONE, /**< Blank. */
    UI_SCROLL_IND_UP,   /**< Up arrow: there are items above the window. */
    UI_SCROLL_IND_DOWN, /**< Down arrow: there are items below the window. */
} ui_scroll_ind_t;

/**
 * @brief Scroll indicator of one row of a window over n items (menu items, list lines).
 *
 * It shows the list position, not the wrap-around navigation:
 * - the first row of the window shows UP if top > 0;
 * - the last row of the window shows DOWN if top + rows < n;
 * - any other row, and a widget whose items all fit (n <= rows), shows NONE.
 * With rows == 1 the single row is both first and last: DOWN wins when both apply (static
 * rule: it never alternates), UP shows only on the last window.
 *
 * @param row  Row of the window (0 = first).
 * @param rows Rows of the window.
 * @param top  First item shown (ui_menu_window(), ui_list_top()).
 * @param n    Number of items.
 * @return The indicator (NONE for row >= rows or rows == 0).
 */
ui_scroll_ind_t ui_scroll_indicator(size_t row, size_t rows, size_t top, size_t n);

/**
 * @brief Checks that a menu label fits its row next to the marker and the reserved cells.
 *
 * A row of cols cells is: UI_MENU_MARKER_COLS marker + label + UI_SCROLL_SIDE_COLS (gap and
 * indicator), so the label gets cols - 3 cells.
 *
 * @param len  Characters of the label.
 * @param cols Cells of the menu row.
 * @return true if it fits (false also when cols cannot hold the marker and the reserve).
 */
bool ui_menu_label_fits(size_t len, size_t cols);

/**
 * @brief Custom characters a scrollable widget needs: the two arrows only when it can scroll.
 *
 * @param n    Number of items.
 * @param rows Rows of the window.
 * @return UI_SCROLL_GLYPHS if n > rows, else 0 (a widget that fits spends no CGRAM slot).
 */
size_t ui_scroll_glyph_count(size_t n, size_t rows);

/**
 * @brief Checks that a list line fits its row next to the reserved cells.
 *
 * A row of cols cells is: line + UI_SCROLL_SIDE_COLS (gap and indicator), so the line gets
 * cols - 2 cells (14 on a full row).
 *
 * @param len  Characters of the line.
 * @param cols Cells of the list row.
 * @return true if it fits (false also when cols cannot hold one cell and the reserve).
 */
bool ui_list_line_fits(size_t len, size_t cols);

/**
 * @brief First line a list window shows for a scroll position.
 *
 * A list of n lines in rows rows has n - rows + 1 window positions (1 if it fits). pos is
 * taken modulo that count, so a wrapping counter (app_state_t::scroll) cycles from the
 * last window back to the first and, going back, from the first to the last.
 *
 * @param pos  Scroll position (any value).
 * @param n    Number of lines.
 * @param rows Rows of the window.
 * @return The first line to show, 0..max(n - rows, 0).
 */
size_t ui_list_top(size_t pos, size_t n, size_t rows);

/**
 * @brief Glyph of a scroll indicator (ROM A00 has no up/down arrows).
 *
 * @param ind UI_SCROLL_IND_UP or UI_SCROLL_IND_DOWN (NONE gives an empty glyph).
 * @param out Output glyph (row 7, the cursor row, stays blank).
 */
void ui_scroll_arrow_glyph(ui_scroll_ind_t ind, ui_glyph_t out);

/**
 * @brief Text of a steps row: "<k> <name>" for the current step, k counting from 1.
 *
 * When current >= n (all done) the last step is shown.
 *
 * @param current  Current step.
 * @param n        Number of steps (>= 1).
 * @param name     Name of the step shown (names[min(current, n - 1)]).
 * @param out      Output buffer.
 * @param out_size Size of out.
 * @return Length of the full text (it may have been cut to out_size - 1).
 */
size_t ui_step_line(size_t current, size_t n, const char *name, char *out, size_t out_size);

/**
 * @brief Index of the step a steps row shows.
 *
 * @param current Current step (n or more = all done).
 * @param n       Number of steps (>= 1).
 * @return min(current, n - 1).
 */
size_t ui_step_shown(size_t current, size_t n);

/**
 * @brief Assigns the custom characters of one screen to the 8 CGRAM slots.
 *
 * Equal patterns (compared on their 5 low bits) share one slot. A pattern that the CGRAM
 * already holds (bit k of cur_valid set and cur[k] equal) keeps that slot, so redrawing
 * a screen or reusing glyphs across screens costs no CGRAM write. The other patterns take
 * the free slots in order, invalid ones first; define_mask gets one bit per slot to write.
 *
 * @param want      Patterns the screen uses, in widget order (may repeat; NULL if n is 0).
 * @param n         Number of patterns.
 * @param cur       What each CGRAM slot holds now.
 * @param cur_valid Bit k set = cur[k] is known to be on the panel.
 * @param slot_of   Output: slot of every want[i] (n entries; may be NULL if n is 0).
 * @param define_mask Output: bit k set = slot k must be written with its new pattern.
 * @param overflow  Output: on failure, the index of the first want[i] that needs a 9th
 *                  distinct slot; untouched on success.
 * @return Number of distinct patterns (0..8), or -1 if they do not fit.
 */
int ui_glyph_plan(const ui_glyph_t *want, size_t n, const ui_glyph_t cur[UI_CGRAM_SLOTS],
                  unsigned cur_valid, uint8_t *slot_of, unsigned *define_mask, size_t *overflow);

/**
 * @brief Next screen generation (wraps around, never 0).
 *
 * @param gen Current generation.
 * @return gen + 1 within 1..0xFFFFFF.
 */
uint32_t ui_gen_next(uint32_t gen);

/**
 * @brief Builds a widget handle from a screen generation and a pool index.
 *
 * Handle 0 is "no widget". A handle is only valid while its generation is the current one,
 * so a handle kept from a previous screen is detected even when its pool slot was reused.
 *
 * @param gen Generation, 1..0xFFFFFF.
 * @param idx Pool index, 0..UI_HANDLE_MAX_INDEX.
 * @return The handle, or 0 if gen or idx is out of range.
 */
uint32_t ui_handle_make(uint32_t gen, size_t idx);

/**
 * @brief Splits a handle into its generation and pool index.
 *
 * @param h   Handle.
 * @param gen Output: generation (may be NULL).
 * @param idx Output: pool index (may be NULL).
 * @return false for handle 0 or a malformed handle (outputs untouched).
 */
bool ui_handle_split(uint32_t h, uint32_t *gen, size_t *idx);

#endif // UI_CORE_H
