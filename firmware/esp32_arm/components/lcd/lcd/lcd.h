#ifndef LCD_H
#define LCD_H

/**
 * @file lcd.h
 * @brief Driver for the 1602 character LCD (HD44780, 16x2) behind a PCF8574 I2C backpack:
 *        plain C text API.
 *
 * Coordinates: column 0..LCD_COLS-1 from the left, row 0..LCD_ROWS-1 from the top.
 * Text never wraps: whatever runs past the right edge is clipped.
 *
 * Characters are the panel's own codes (ROM A00, see README.md): printable ASCII except
 * 0x5C ('\\' is a yen sign) and 0x7E/0x7F (arrows); no accented letters. Use the LCD_CHAR_*
 * and LCD_STR_* macros below for the useful non-ASCII glyphs, and LCD_CHAR_CUSTOM(slot) for
 * the custom characters defined with lcd_define_char().
 *
 * The driver keeps a shadow copy of what the panel shows: lcd_write_text() and
 * lcd_write_line() only send the cells that changed, so rewriting a whole row every
 * refresh costs nothing when nothing changed and never flickers.
 *
 * Thread-safe: every call takes the driver mutex, so several tasks may write. All calls
 * block on I2C (~0.36 ms per character at 100 kHz): never call them from an ISR, and keep
 * them out of time-critical tasks (see README.md). lcd_clear() also sleeps for the
 * panel's 1.52 ms, rounded up to whole ticks (10-20 ms at the default 100 Hz tick): to
 * refresh, overwrite rows with lcd_write_line() instead of clearing.
 *
 * Every function returns ESP_OK, ESP_ERR_INVALID_STATE before lcd_init(),
 * ESP_ERR_INVALID_ARG on a bad argument, or the I2C error.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#include "lcd_core.h"  // LCD_COLS, LCD_ROWS, LCD_CGRAM_SLOTS, LCD_GLYPH_ROWS

/**
 * Character code of custom character slot (0..LCD_CGRAM_SLOTS-1), usable inside strings.
 * The HD44780 maps codes 8..15 to the same CGRAM slots as 0..7, and 8 is not a string
 * terminator, so slot 0 also works in text: "\x08" is slot 0.
 */
#define LCD_CHAR_CUSTOM(slot) ((char)(0x08 + ((slot) & 0x07)))

// Useful glyphs of ROM A00 (verified on the module, see ../doc/README.md).
#define LCD_CHAR_RIGHT_ARROW ((char)0x7E)
#define LCD_CHAR_LEFT_ARROW ((char)0x7F)
#define LCD_CHAR_DEGREE ((char)0xDF)
#define LCD_CHAR_ENYE ((char)0xEE)  // lowercase n with tilde
#define LCD_CHAR_YEN ((char)0x5C)   // what '\\' shows on A00

// Same glyphs as string literals, for concatenation: "45" LCD_STR_DEGREE "C". A literal
// "\xDFC" would not work: C reads every hex digit after \x.
#define LCD_STR_RIGHT_ARROW "\x7E"
#define LCD_STR_LEFT_ARROW "\x7F"
#define LCD_STR_DEGREE "\xDF"
#define LCD_STR_ENYE "\xEE"

/**
 * @brief Static description of the panel and its wiring (lcd_get_info()).
 *
 * Geometry comes from lcd_core.h; I2C port, clock, address and pins from lcd_pins.h
 * (pins and address via Kconfig), so it always matches what lcd_init() uses.
 */
typedef struct {
    const char *driver; /**< Controller and interface, "HD44780 + PCF8574". */
    int cols;           /**< Visible columns, LCD_COLS = 16. */
    int rows;           /**< Rows, LCD_ROWS = 2. */
    int char_w;         /**< Dots per character, horizontally: 5. */
    int char_h;         /**< Dots per character, vertically: 8. */
    int cgram_slots;    /**< Custom characters, 8. */
    const char *rom;    /**< Character ROM of the module, "A00". */
    int i2c_port;       /**< Numeric I2C port (I2C_NUM_0 = 0). */
    int i2c_addr;       /**< 7-bit address in use (after the scan fallback); before
                             lcd_init(), the configured one. */
    int i2c_clock_hz;   /**< SCL clock in Hz, 100000. */
    int pin_sda;        /**< GPIO of SDA (HW-221 A1). */
    int pin_scl;        /**< GPIO of SCL (HW-221 A2). */
    bool backlight;     /**< Backlight state; false when not initialized. */
    bool initialized;   /**< true between a successful lcd_init() and lcd_deinit(). */
} lcd_info_t;

/**
 * @brief Describes the panel, the I2C settings and the pins.
 *
 * Works at any time, also before lcd_init() (then initialized is false). It does no I2C
 * traffic; it only takes the driver mutex to read the state.
 *
 * @param out Output.
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if out is NULL.
 */
esp_err_t lcd_get_info(lcd_info_t *out);

/**
 * @brief Brings up I2C, finds the PCF8574 and initializes the panel.
 *
 * Leaves the display on and cleared, cursor hidden at (0,0), backlight on. If the
 * configured address does not answer, scans the bus and uses the only device found.
 * Undoes everything on failure.
 *
 * @return ESP_OK; ESP_ERR_INVALID_STATE if already initialized; ESP_ERR_NOT_FOUND if no
 *         single device answers; or the I2C error.
 */
esp_err_t lcd_init(void);

/**
 * @brief Clears the display, turns the backlight off and releases the I2C bus.
 *
 * The panel commands are best effort: the bus is released even if they fail. Do not call
 * it while another task may still be writing.
 *
 * @return ESP_OK, or ESP_ERR_INVALID_STATE if not initialized.
 */
esp_err_t lcd_deinit(void);

/**
 * @brief Clears the display and moves the cursor to (0,0).
 *
 * @return ESP_OK, ESP_ERR_INVALID_STATE or the I2C error.
 */
esp_err_t lcd_clear(void);

/**
 * @brief Moves the cursor (where lcd_write_char() writes, and the visible cursor).
 *
 * @param col Column, 0..LCD_COLS-1.
 * @param row Row, 0..LCD_ROWS-1.
 * @return ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG or the I2C error.
 */
esp_err_t lcd_set_cursor(int col, int row);

/**
 * @brief Writes one character at the cursor and advances it one column.
 *
 * Past the last column the character is dropped (no wrap) and ESP_OK is returned. '\0'
 * is custom character 0; control characters are not interpreted.
 *
 * @param c Character code.
 * @return ESP_OK, ESP_ERR_INVALID_STATE or the I2C error.
 */
esp_err_t lcd_write_char(char c);

/**
 * @brief Writes a string on one row from a column, clipped at the right edge (no wrap).
 *
 * Only the cells whose content changes are sent. Bytes are written as they are ('\n' and
 * other controls are not interpreted). The whole string is written under one lock, so
 * text from different tasks never interleaves. Afterwards the cursor is right after the
 * last visible character.
 *
 * @param col     First column, 0..LCD_COLS-1.
 * @param row     Row, 0..LCD_ROWS-1.
 * @param text    NUL-terminated string.
 * @param col_end Output (may be NULL): column right after the last character,
 *                col + strlen(text), even if clipped. Use it to continue on the same
 *                row. Only written on ESP_OK.
 * @return ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG (text NULL, col or row out of
 *         range) or the I2C error.
 */
esp_err_t lcd_write_text(int col, int row, const char *text, int *col_end);

/**
 * @brief Writes a whole row: text from column 0, padded with spaces, clipped at the edge.
 *
 * The flicker-free way to overwrite a row (no clear first); only changed cells are sent.
 * Afterwards the cursor is right after the last visible character of text.
 *
 * @param row  Row, 0..LCD_ROWS-1.
 * @param text NUL-terminated string ("" blanks the row).
 * @return ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG or the I2C error.
 */
esp_err_t lcd_write_line(int row, const char *text);

/**
 * @brief Defines a custom 5x8 character.
 *
 * Cells already showing that slot change at once. The cursor position is restored.
 *
 * @param slot Slot, 0..LCD_CGRAM_SLOTS-1; shown with LCD_CHAR_CUSTOM(slot).
 * @param rows LCD_GLYPH_ROWS bytes, top to bottom; bit 4 is the leftmost dot (5 low bits
 *             used).
 * @return ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG or the I2C error.
 */
esp_err_t lcd_define_char(int slot, const uint8_t rows[LCD_GLYPH_ROWS]);

/**
 * @brief Turns the backlight on or off (the text stays).
 *
 * @param on true = on.
 * @return ESP_OK, ESP_ERR_INVALID_STATE or the I2C error.
 */
esp_err_t lcd_backlight(bool on);

/**
 * @brief Shows or hides the cursor at the current position.
 *
 * @param show  Underline cursor.
 * @param blink Blinking block cursor (independent of show).
 * @return ESP_OK, ESP_ERR_INVALID_STATE or the I2C error.
 */
esp_err_t lcd_cursor(bool show, bool blink);

/**
 * @brief Turns the display on or off; the content is kept and writes still work.
 *
 * @param on true = on.
 * @return ESP_OK, ESP_ERR_INVALID_STATE or the I2C error.
 */
esp_err_t lcd_display(bool on);

#endif // LCD_H
