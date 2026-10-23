#ifndef LCD_CORE_H
#define LCD_CORE_H

/**
 * @file lcd_core.h
 * @brief Pure logic of the 1602 LCD (HD44780, 4-bit mode, behind a PCF8574): no I2C, waits
 *        or locks.
 *
 * Everything here can be tested on the PC (test_host/test_lcd_core.c):
 *
 *   lcd_init()          -> lcd_init_steps[]    power-up sequence (nibbles and bytes)
 *                       -> lcd_pick_addr()     choose the I2C address from a bus scan
 *   every command/data  -> lcd_pcf_byte()      LCD byte -> 4 PCF8574 bytes
 *                       -> lcd_pcf_nibble()    nibble -> 2 PCF8574 bytes (init only)
 *                       -> lcd_pcf_idle()      backlight-only byte (no E pulse)
 *   positioning         -> lcd_ddram_addr()    (col, row) -> DDRAM address
 *                       -> lcd_cgram_addr()    custom char slot -> CGRAM address
 *   lcd_display/cursor  -> lcd_display_cmd()   flags -> Display Control command
 *   lcd_write_text/line -> lcd_row_put()       text -> row of cells (clipped)
 *                       -> lcd_diff_next()     shadow vs wanted row -> cells to send
 *
 * Panel protocol (verified on hardware, see CLAUDE.md): PCF8574 P0 = RS, P1 = RW (always
 * 0), P2 = E, P3 = backlight, P4..P7 = D4..D7. One LCD byte is 4 PCF8574 bytes in one I2C
 * transaction, high nibble first, each nibble sent with E = 1 and then E = 0.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------- geometry (fixed: 1602 module) ----------

#define LCD_COLS 16        // visible columns
#define LCD_ROWS 2         // rows, DDRAM 0x00 and 0x40 (2-line mode)
#define LCD_ROW1_ADDR 0x40 // DDRAM address of row 1, column 0
#define LCD_CHAR_W 5       // dots per character, horizontally
#define LCD_CHAR_H 8       // dots per character, vertically (5x8 font)
#define LCD_CGRAM_SLOTS 8  // custom characters: codes 0..7 (also 8..15)
#define LCD_GLYPH_ROWS 8   // bytes per custom character, top to bottom, 5 low bits used

// ---------- PCF8574 bits ----------

#define LCD_PCF_RS 0x01      // P0: 0 = command, 1 = data
#define LCD_PCF_RW 0x02      // P1: always 0 (write only)
#define LCD_PCF_E 0x04       // P2: enable, the LCD latches on the falling edge
#define LCD_PCF_BL 0x08      // P3: backlight
#define LCD_PCF_BYTE_LEN 4   // PCF8574 bytes per LCD byte
#define LCD_PCF_NIBBLE_LEN 2 // PCF8574 bytes per nibble

// ---------- HD44780 commands ----------

#define LCD_CMD_CLEAR 0x01   // 1.52 ms
#define LCD_CMD_HOME 0x02    // 1.52 ms
#define LCD_CMD_ENTRY 0x04   // | LCD_ENTRY_INC
#define LCD_ENTRY_INC 0x02   // address increments after each data byte, no shift
#define LCD_CMD_DISPLAY 0x08 // | D | C | B, see lcd_display_cmd()
#define LCD_DISPLAY_ON 0x04     // D: display on
#define LCD_DISPLAY_CURSOR 0x02 // C: underline cursor
#define LCD_DISPLAY_BLINK 0x01  // B: blinking block cursor
#define LCD_CMD_FUNC 0x20    // | LCD_FUNC_2LINES
#define LCD_FUNC_2LINES 0x08 // N = 1; DL = 0 (4 bits), F = 0 (5x8)
#define LCD_CMD_CGRAM 0x40   // | CGRAM address (6 bits)
#define LCD_CMD_DDRAM 0x80   // | DDRAM address (7 bits)

#define LCD_POWER_ON_MS 50       // > 40 ms after VCC rises above 2.7 V
#define LCD_CLEAR_DELAY_US 2000  // Clear and Home: 1.52 ms in the datasheet

/** DDRAM address that never matches a real one: the panel address is unknown. */
#define LCD_AC_UNKNOWN 0xFF

/**
 * Unchanged cells between two changed ones that lcd_diff_next() still sends in the same run.
 * Repositioning costs one command (4 PCF8574 bytes, like one data byte) plus a separate I2C
 * transaction, so re-sending one unchanged cell is never more expensive than a jump.
 */
#define LCD_DIFF_MAX_GAP 1

/**
 * @brief One power-up step: a lone nibble (8-bit phase) or a byte sent as two nibbles.
 */
typedef struct {
    bool nibble;       /**< true = only the nibble val (0..15); false = a full command byte. */
    uint8_t val;       /**< Nibble or command. */
    uint16_t delay_us; /**< Wait after it; 0 = none. */
} lcd_init_step_t;

/** Power-up sequence (HD44780U figure 24, 4-bit mode); ends with display on and cleared. */
extern const lcd_init_step_t lcd_init_steps[];
extern const size_t lcd_init_nsteps;

/**
 * @brief Builds the 4 PCF8574 bytes that send one byte to the LCD.
 *
 * High nibble first, each one twice: with E = 1 and with E = 0 (the LCD latches it on the
 * falling edge). All four carry RS and the backlight bit, otherwise the backlight flickers.
 * RW stays 0.
 *
 * @param val Command (rs false) or data (rs true).
 * @param rs  false = command, true = data.
 * @param bl  Backlight on.
 * @param out Output: {hi|E, hi, lo|E, lo}.
 */
void lcd_pcf_byte(uint8_t val, bool rs, bool bl, uint8_t out[LCD_PCF_BYTE_LEN]);

/**
 * @brief Builds the 2 PCF8574 bytes that send a lone nibble (power-up only).
 *
 * During the first init steps the LCD is in 8-bit mode and reads D7..D4 only
 * (D3..D0 are not wired).
 *
 * @param nib Nibble (low 4 bits used).
 * @param rs  false = command, true = data.
 * @param bl  Backlight on.
 * @param out Output: {nib|E, nib}.
 */
void lcd_pcf_nibble(uint8_t nib, bool rs, bool bl, uint8_t out[LCD_PCF_NIBBLE_LEN]);

/**
 * @brief PCF8574 byte without an E pulse: only sets the backlight (RS = RW = E = 0).
 *
 * @param bl Backlight on.
 * @return LCD_PCF_BL or 0.
 */
uint8_t lcd_pcf_idle(bool bl);

/**
 * @brief DDRAM address of a cell.
 *
 * In 2-line mode row 0 starts at 0x00 and row 1 at 0x40; each row has 40 addresses, the
 * first LCD_COLS are visible.
 *
 * @param col Column (0..39; LCD_COLS is the hidden cell right after the last visible one).
 * @param row Row; any value other than 0 means row 1.
 * @return The address; the command is LCD_CMD_DDRAM | address.
 */
uint8_t lcd_ddram_addr(unsigned col, unsigned row);

/**
 * @brief CGRAM address of custom character slot (8 bytes per character).
 *
 * @param slot Slot; the low 3 bits are used (0..7).
 * @return The address; the command is LCD_CMD_CGRAM | address.
 */
uint8_t lcd_cgram_addr(unsigned slot);

/**
 * @brief Display Control command.
 *
 * @param on     Display on (D).
 * @param cursor Underline cursor shown (C).
 * @param blink  Blinking block cursor (B).
 * @return LCD_CMD_DISPLAY | D | C | B.
 */
uint8_t lcd_display_cmd(bool on, bool cursor, bool blink);

/**
 * @brief Chooses the I2C address from a bus scan.
 *
 * The configured address wins if it answered. Otherwise, if exactly one device answered,
 * that one is used (the caller should warn). With zero or several devices it does not guess.
 *
 * @param want  Configured 7-bit address.
 * @param found Addresses that answered (may be NULL if n is 0).
 * @param n     Number of addresses in found.
 * @return The chosen address, or -1 if there is no single candidate.
 */
int lcd_pick_addr(uint8_t want, const uint8_t *found, size_t n);

/**
 * @brief Copies text into a row of cells from a column, clipped at the right edge.
 *
 * Bytes are copied as they are (character codes of the panel's ROM; 8..15 are the custom
 * characters 0..7). Nothing wraps to the next row; cells outside the text are untouched.
 *
 * @param row  Row of LCD_COLS cells, modified in place.
 * @param col  First column; LCD_COLS or more copies nothing.
 * @param text NUL-terminated text.
 * @return strlen(text): the unclipped number of characters.
 */
size_t lcd_row_put(uint8_t row[LCD_COLS], unsigned col, const char *text);

/**
 * @brief Finds the next run of cells to send so that the panel goes from shown to want.
 *
 * Starting at index from, it skips equal cells, and returns the run from the first
 * different cell to the last different one, merging gaps of at most LCD_DIFF_MAX_GAP equal
 * cells (re-sending them is cheaper than a reposition). Call it again with
 * from = *start + run length until it returns 0. Every different cell is covered by exactly
 * one run, and every run starts and ends on a different cell.
 *
 * @param shown What the panel shows (the shadow buffer), len cells.
 * @param want  What it should show, len cells.
 * @param len   Number of cells.
 * @param from  First index to look at.
 * @param start Output: first cell of the run. Untouched when 0 is returned.
 * @return Length of the run, or 0 when no cell from index from on differs.
 */
size_t lcd_diff_next(const uint8_t *shown, const uint8_t *want, size_t len, size_t from,
                     size_t *start);

#endif // LCD_CORE_H
