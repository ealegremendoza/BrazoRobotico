#ifndef UI_H
#define UI_H

/**
 * @file ui.h
 * @brief lcd_ui: declarative, builder-style screens on top of the 1602 LCD driver.
 *
 * The app says WHAT is on a screen; lcd_ui lays it out in character cells and draws it.
 * No coordinates, no malloc:
 *
 *   ui_screen_begin(NULL);                          // no title row
 *   s_j1 = ui_value("J1", "%+04.0f" UI_STR_DEGREE, 5); // "J1+045°", 7 cells, row 0
 *   s_j2 = ui_beside(ui_value("J2", "%+04.0f" UI_STR_DEGREE, 5), 1);  // same row, 1 cell gap
 *   ui_footer("SIMULADO  q:menu");                   // pinned to the bottom row
 *   ui_screen_end();                                 // layout + CGRAM + draw everything
 *   ...
 *   ui_value_set(s_j1, 42.0f);                       // sends only the cells that changed
 *
 * Layout (see ui_layout_compute() in ui_core.h for the exact rules), in cells of the
 * 16x2 panel:
 * - Title: ui_screen_begin("TEXT") puts a centered title on row 0; NULL or "" = none.
 * - Footer: ui_footer() and ui_pin_footer() widgets take the bottom rows, one row each.
 * - Body: every other widget starts a new line at column 0, top to bottom in call order,
 *   unless ui_beside() puts it on the row of the previous one (gap cells after it).
 *   ui_message(), ui_menu() and ui_list() are FLEX: they share the rows left by the fixed
 *   lines.
 * - Width: ui_text(), ui_progress() and ui_counter() take a width in cells (0 = the rest
 *   of the row); ui_value() is its label plus its field; ui_image() is w x h glyphs;
 *   every other widget takes the rest of the row.
 * - Overflow: if a widget does not fit (past the right edge or the bottom, a fixed text
 *   wider than its cells, or the screen needs more than 8 distinct custom characters),
 *   ui_screen_end() logs which widget and returns ESP_ERR_INVALID_SIZE WITHOUT drawing
 *   anything. Nothing is ever drawn outside a widget's cells: texts set later are cut.
 *
 * Custom characters (CGRAM, 8 slots) are a per-screen resource: ui_image() glyphs and the
 * 4 partial-block glyphs of every progress bar (shared by all bars) are collected by
 * ui_screen_end(), equal patterns share a slot, and only slots whose content changes are
 * rewritten. Redefining a slot changes every cell showing it at once, so this happens in
 * ui_screen_end() right before the new rows are written.
 *
 * Builder errors (pool full, bad argument, call outside begin/end) are sticky: the
 * builder call returns UI_NONE, decorators pass UI_NONE through, and ui_screen_end()
 * returns the first error without drawing.
 *
 * Handles: a ui_widget_t is valid from its builder call until the next
 * ui_screen_begin(). Setters return ESP_ERR_INVALID_ARG for UI_NONE, a malformed handle
 * or the wrong widget kind, and ESP_ERR_INVALID_STATE for a handle of a previous screen
 * (the handle carries the screen generation) or when the screen failed its layout.
 * Before ui_screen_end() setters only store the value (the first draw uses it); after it
 * they re-render the widget and send only its cells that changed (lcd_write_text(); the
 * driver's shadow buffer then skips the cells the panel already shows).
 *
 * Threading: ONE task (the UI task) owns lcd_ui. None of these functions are locked (the
 * lcd driver below is, but a screen is many calls). Call them only from that task, never
 * from an ISR, and keep that task below time-critical ones: every draw blocks on I2C
 * (~0.36 ms per changed cell, a full screen ~12 ms).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#include "lcd.h"      // lcd_info_t, LCD_COLS / LCD_ROWS, ROM A00 glyph codes
#include "ui_core.h"  // ui_align_t, ui_glyph_t, ui_wrap()

/** Screen size in character cells. */
#define UI_SCREEN_COLS LCD_COLS
#define UI_SCREEN_ROWS LCD_ROWS

// ROM A00 glyphs (same values as lcd.h), so apps only need ui.h.
#define UI_CHAR_RIGHT_ARROW LCD_CHAR_RIGHT_ARROW
#define UI_CHAR_LEFT_ARROW LCD_CHAR_LEFT_ARROW
#define UI_CHAR_DEGREE LCD_CHAR_DEGREE
#define UI_CHAR_ENYE LCD_CHAR_ENYE
#define UI_STR_RIGHT_ARROW LCD_STR_RIGHT_ARROW
#define UI_STR_LEFT_ARROW LCD_STR_LEFT_ARROW
#define UI_STR_DEGREE LCD_STR_DEGREE
#define UI_STR_ENYE LCD_STR_ENYE

/** Handle of a widget of the current screen (0 = none). */
typedef uint32_t ui_widget_t;

/** "No widget": what builders return on error. */
#define UI_NONE ((ui_widget_t)0)

/**
 * @brief What the display and lcd_ui can hold (ui_get_display_info()).
 */
typedef struct {
    lcd_info_t panel; /**< From lcd_get_info(): driver, geometry, ROM, I2C, pins, state. */
    int cols;         /**< Text capacity: characters per row (16). */
    int rows;         /**< Text capacity: rows (2). */
    int glyph_slots;  /**< Custom characters one screen can use (CGRAM slots, 8). */
    int glyph_refs;   /**< Glyph references one screen can collect (image cells + 4 per bar). */
    int max_widgets;  /**< Widget pool per screen (CONFIG_LCD_UI_MAX_WIDGETS). */
    int text_max;     /**< Longest text per widget, in characters (CONFIG_LCD_UI_TEXT_MAX). */
} ui_display_info_t;

/**
 * @brief Describes the panel and lcd_ui's limits. Works before ui_init() (no drawing).
 *
 * @param out Output.
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if out is NULL.
 */
esp_err_t ui_get_display_info(ui_display_info_t *out);

/**
 * @brief Initializes the display (lcd_init()). The first ui_screen_end() fills it.
 *
 * Without a working display lcd_ui still runs: layouts are computed and every draw
 * returns the lcd error.
 *
 * @return ESP_OK, or the lcd_init() error.
 */
esp_err_t ui_init(void);

/**
 * @brief Starts a new screen: empties the widget pool and invalidates every handle.
 *
 * @param title Title (copied), centered on row 0; it must fit the row (else
 *              ui_screen_end() fails). NULL or "" = no title row.
 * @return ESP_OK.
 */
esp_err_t ui_screen_begin(const char *title);

/**
 * @brief Adds a single line of text (copied).
 *
 * @param text  Text; it must fit its cells (else ui_screen_end() fails).
 * @param width Width in cells, 0 = the rest of the row.
 * @param align Alignment inside its cells.
 * @return Handle, or UI_NONE on error.
 */
ui_widget_t ui_text(const char *text, unsigned width, ui_align_t align);

/**
 * @brief Adds a word-wrapped text block (copied; FLEX: takes the remaining rows).
 *
 * Word-wrapped; if that needs too many rows, cut into plain slices (ui_msg_fit()), so up
 * to width x rows characters always show. Takes the rest of the row.
 *
 * @param text Text (NULL = ""; see ui_message_placeholder()).
 * @return Handle, or UI_NONE on error.
 */
ui_widget_t ui_message(const char *text);

/**
 * @brief Adds a progress bar, 5 steps per cell (partial-block glyphs), initially 0 %.
 *
 * @param width Width in cells, 0 = the rest of the row.
 * @return Handle, or UI_NONE on error.
 */
ui_widget_t ui_progress(unsigned width);

/**
 * @brief Adds a menu (FLEX): a window of items, one per row, the selected one marked
 *        with a right arrow. The window scrolls to keep the selection visible.
 *
 * The last two cells of every row are reserved: a blank gap and a scroll indicator. When
 * the menu has more items than rows, the first row shows an up arrow while items are above
 * the window and the last row a down arrow while items are below it (with one row, down
 * wins); they follow the list position, not the wrap-around. The arrows are two custom
 * characters, planned only for a menu that can scroll. A menu that fits leaves both cells
 * blank and spends no CGRAM slot.
 *
 * @param items    Labels (NOT copied: the array and strings must stay valid until the
 *                 next ui_screen_begin(); use static storage). Each must fit the row
 *                 minus the marker cell and the 2 reserved cells (13 on a full row), or
 *                 ui_screen_end() fails with ESP_ERR_INVALID_SIZE.
 * @param n        Number of items (>= 1).
 * @param selected Selected index (< n).
 * @return Handle, or UI_NONE on error.
 */
ui_widget_t ui_menu(const char *const *items, size_t n, size_t selected);

/**
 * @brief Adds a scrolled list of read-only lines (FLEX): a window of lines, one per row.
 *
 * Same scroll indicators as ui_menu(): the last two cells of every row are reserved (blank
 * gap, indicator); the first row shows an up arrow while lines are above the window, the
 * last row a down arrow while lines are below it (with one row, down wins). The two arrow
 * glyphs (shared with any menu) are planned only when the list can scroll (n > rows).
 *
 * @param lines Lines (NOT copied, like ui_menu() items; their text may change between
 *              ui_list_scroll() calls). Each must fit the row minus the 2 reserved cells
 *              (14 on a full row), or ui_screen_end() fails with ESP_ERR_INVALID_SIZE.
 * @param n     Number of lines (>= 1).
 * @return Handle, or UI_NONE on error.
 */
ui_widget_t ui_list(const char *const *lines, size_t n);

/**
 * @brief Adds a static row: left text at the left edge, right text at the right one.
 *
 * @param left  Left text (copied).
 * @param right Right text (copied; may be NULL).
 * @return Handle, or UI_NONE on error.
 */
ui_widget_t ui_row(const char *left, const char *right);

/**
 * @brief Adds a value: the label, immediately followed by a fixed-width number field.
 *
 * Takes strlen(label) + width cells. The field is blank until ui_value_set(). Numbers too
 * long for the field show as '#' characters.
 *
 * @param label Label (copied).
 * @param fmt   printf format with one double conversion, e.g. "%+04.0f" UI_STR_DEGREE
 *              (NOT copied: keep it valid until the next ui_screen_begin()).
 * @param width Field width in characters (1..UI_SCREEN_COLS).
 * @return Handle, or UI_NONE on error.
 */
ui_widget_t ui_value(const char *label, const char *fmt, unsigned width);

/**
 * @brief Adds a centered integer. Blank until ui_counter_set().
 *
 * @param width Width in cells, 0 = the rest of the row.
 * @return Handle, or UI_NONE on error.
 */
ui_widget_t ui_counter(unsigned width);

/**
 * @brief Adds a steps row: "<k> <name>" of the current step (k from 1). Starts at step 0.
 *
 * Every "<k> <name>" must fit the row (checked by ui_screen_end()).
 *
 * @param names Step names (NOT copied, like ui_menu() items).
 * @param n     Number of steps (1..9).
 * @return Handle, or UI_NONE on error.
 */
ui_widget_t ui_steps(const char *const *names, size_t n);

/**
 * @brief Adds a picture made of custom characters: w x h cells, one glyph per cell.
 *
 * The glyphs take CGRAM slots for this screen only (equal glyphs share one).
 *
 * @param glyphs w * h glyphs, row by row (NOT copied: use static storage).
 * @param w      Width in cells (1..UI_SCREEN_COLS).
 * @param h      Height in rows (1..UI_SCREEN_ROWS).
 * @return Handle, or UI_NONE on error.
 */
ui_widget_t ui_image(const ui_glyph_t *glyphs, unsigned w, unsigned h);

/**
 * @brief Adds the live input line: ">" + the tail of the text + "_", the rest of the row.
 *
 * @return Handle, or UI_NONE on error.
 */
ui_widget_t ui_input(void);

/**
 * @brief Adds a hint text on its own bottom row (left-aligned, the whole row).
 *
 * More footer rows can be added with ui_pin_footer(); they stack in creation order.
 *
 * @param hint Hint (copied).
 * @return ESP_OK, or the builder error (also recorded for ui_screen_end()).
 */
esp_err_t ui_footer(const char *hint);

/**
 * @brief Moves a one-row widget to the footer (bottom rows, stacked in creation order).
 *
 * @param w Widget (not a flex one, not a ui_beside() one, one row high).
 * @return w, or UI_NONE on error.
 */
ui_widget_t ui_pin_footer(ui_widget_t w);

/**
 * @brief Puts a widget on the row of the previous body widget, gap cells after its end.
 *
 * @param w   Widget (not a footer one).
 * @param gap Blank cells before it.
 * @return w, or UI_NONE on error.
 */
ui_widget_t ui_beside(ui_widget_t w, unsigned gap);

/**
 * @brief Text shown (wrapped) while a message is empty.
 *
 * @param w    Message widget.
 * @param text Placeholder (NOT copied: use static storage).
 * @return w, or UI_NONE on error.
 */
ui_widget_t ui_message_placeholder(ui_widget_t w, const char *text);

/**
 * @brief Computes the layout, plans the CGRAM and draws the whole screen.
 *
 * Both rows are rewritten with lcd_write_line() (no clear: the driver sends only the
 * cells that differ from what the panel shows).
 *
 * @return ESP_OK; the first builder error; ESP_ERR_INVALID_SIZE if a widget does not fit
 *         or the screen needs more than 8 custom characters (logged, nothing drawn);
 *         ESP_ERR_INVALID_STATE without ui_screen_begin(); or the first lcd error.
 */
esp_err_t ui_screen_end(void);

/**
 * @brief Sets a progress bar; sends only the cells whose fill changed.
 *
 * @param w       Progress widget.
 * @param percent 0..100 (larger counts as 100).
 * @return ESP_OK, ESP_ERR_INVALID_ARG/STATE (see the file comment) or the lcd error.
 */
esp_err_t ui_progress_set(ui_widget_t w, unsigned percent);

/**
 * @brief Moves the menu selection (scrolling the window if needed).
 *
 * @param w     Menu widget.
 * @param index New selection (< number of items).
 * @return ESP_OK, ESP_ERR_INVALID_ARG/STATE or the lcd error.
 */
esp_err_t ui_menu_select(ui_widget_t w, size_t index);

/**
 * @brief Scrolls a list: the window starts at ui_list_top(pos, n, rows).
 *
 * pos is taken modulo the window positions (n - rows + 1), so a wrapping counter cycles
 * from the last window to the first. Re-renders the window and its indicators; the driver
 * sends only the cells that changed.
 *
 * @param w   List widget.
 * @param pos Scroll position (any value).
 * @return ESP_OK, ESP_ERR_INVALID_ARG/STATE or the lcd error.
 */
esp_err_t ui_list_scroll(ui_widget_t w, size_t pos);

/**
 * @brief Redraws a list whose lines (caller storage) changed: a live list (JOINTS).
 *
 * Re-renders the window from the current text of the lines; the driver sends only the
 * cells that differ (e.g. one changed digit), the indicators are untouched. Lines must keep
 * fitting the row (they are not re-checked): use fixed-width fields (ui_fmt_value_row()).
 *
 * @param w List widget.
 * @return ESP_OK, ESP_ERR_INVALID_ARG/STATE or the lcd error.
 */
esp_err_t ui_list_refresh(ui_widget_t w);

/**
 * @brief Sets the number of a value; sends the field only where its text changed.
 *
 * @param w     Value widget.
 * @param value Value (formatted with the widget's fmt into its fixed width).
 * @return ESP_OK, ESP_ERR_INVALID_ARG/STATE or the lcd error.
 */
esp_err_t ui_value_set(ui_widget_t w, float value);

/**
 * @brief Sets a counter.
 *
 * @param w     Counter widget.
 * @param value Number shown ("%d", centered; '#' if it does not fit).
 * @return ESP_OK, ESP_ERR_INVALID_ARG/STATE or the lcd error.
 */
esp_err_t ui_counter_set(ui_widget_t w, int value);

/**
 * @brief Replaces the text of a ui_text() (or footer hint); cut to its cells.
 *
 * @param w    Text widget.
 * @param text New text (copied; NULL = "").
 * @return ESP_OK, ESP_ERR_INVALID_ARG/STATE or the lcd error.
 */
esp_err_t ui_text_set(ui_widget_t w, const char *text);

/**
 * @brief Replaces the text of a message; re-wraps it in its cells.
 *
 * @param w    Message widget.
 * @param text New text (copied; NULL = "").
 * @return ESP_OK, ESP_ERR_INVALID_ARG/STATE or the lcd error.
 */
esp_err_t ui_message_set(ui_widget_t w, const char *text);

/**
 * @brief Shows the text being typed on an input line (only its tail fits).
 *
 * @param w    Input widget.
 * @param line Text being typed (NULL = "").
 * @return ESP_OK, ESP_ERR_INVALID_ARG/STATE or the lcd error.
 */
esp_err_t ui_input_set(ui_widget_t w, const char *line);

/**
 * @brief Moves the current step of a steps row.
 *
 * @param w       Steps widget.
 * @param current Current step; n (or more) = all done (the last step stays shown).
 * @return ESP_OK, ESP_ERR_INVALID_ARG/STATE or the lcd error.
 */
esp_err_t ui_steps_set(ui_widget_t w, size_t current);

#endif // UI_H
