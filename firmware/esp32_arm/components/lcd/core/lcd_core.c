/**
 * @file lcd_core.c
 * @brief Pure logic of the 1602 LCD behind a PCF8574 (see lcd_core.h).
 */

#include "lcd_core.h"

/**
 * HD44780U datasheet figure 24 (4-bit mode), verified on hardware with lcd_i2c_test_idf.
 * Before it the driver waits LCD_POWER_ON_MS with RS = RW = E = 0. The three 0x3 nibbles
 * force 8-bit mode from any state; 0x2 switches to 4 bits. From then on every command is
 * two nibbles; the 37 us each one needs are already covered by the 4 I2C bytes (~0.36 ms
 * at 100 kHz).
 */
const lcd_init_step_t lcd_init_steps[] = {
    {true, 0x3, 5000},                            // > 4.1 ms
    {true, 0x3, 150},                             // > 100 us
    {true, 0x3, 150},
    {true, 0x2, 150},                             // 4-bit mode from here on
    {false, LCD_CMD_FUNC | LCD_FUNC_2LINES, 0},   // 0x28: 4 bits, 2 lines, 5x8
    {false, LCD_CMD_DISPLAY | LCD_DISPLAY_ON, 0}, // 0x0C: display on, no cursor
    {false, LCD_CMD_CLEAR, LCD_CLEAR_DELAY_US},   // 0x01
    {false, LCD_CMD_ENTRY | LCD_ENTRY_INC, 0},    // 0x06: increment, no shift
};

const size_t lcd_init_nsteps = sizeof(lcd_init_steps) / sizeof(lcd_init_steps[0]);

/**
 * @brief Control bits common to every PCF8574 byte of a transfer.
 *
 * @param rs false = command, true = data.
 * @param bl Backlight on.
 * @return LCD_PCF_RS and/or LCD_PCF_BL.
 */
static uint8_t ctl_bits(bool rs, bool bl)
{
    return (uint8_t)((rs ? LCD_PCF_RS : 0) | (bl ? LCD_PCF_BL : 0));
}

/**
 * @brief Builds the 4 PCF8574 bytes that send one byte to the LCD.
 *
 * @param val Command or data.
 * @param rs  false = command, true = data.
 * @param bl  Backlight on.
 * @param out Output: {hi|E, hi, lo|E, lo}.
 */
void lcd_pcf_byte(uint8_t val, bool rs, bool bl, uint8_t out[LCD_PCF_BYTE_LEN])
{
    uint8_t ctl = ctl_bits(rs, bl);
    uint8_t hi = (uint8_t)((val & 0xF0) | ctl);
    uint8_t lo = (uint8_t)((uint8_t)(val << 4) | ctl);

    out[0] = hi | LCD_PCF_E;
    out[1] = hi;
    out[2] = lo | LCD_PCF_E;
    out[3] = lo;
}

/**
 * @brief Builds the 2 PCF8574 bytes that send a lone nibble.
 *
 * @param nib Nibble (low 4 bits used).
 * @param rs  false = command, true = data.
 * @param bl  Backlight on.
 * @param out Output: {nib|E, nib}.
 */
void lcd_pcf_nibble(uint8_t nib, bool rs, bool bl, uint8_t out[LCD_PCF_NIBBLE_LEN])
{
    uint8_t b = (uint8_t)(((nib & 0x0F) << 4) | ctl_bits(rs, bl));

    out[0] = b | LCD_PCF_E;
    out[1] = b;
}

/**
 * @brief PCF8574 byte without an E pulse: only the backlight bit.
 *
 * @param bl Backlight on.
 * @return LCD_PCF_BL or 0.
 */
uint8_t lcd_pcf_idle(bool bl)
{
    return bl ? LCD_PCF_BL : 0;
}

/**
 * @brief DDRAM address of a cell (row 0 at 0x00, row 1 at 0x40).
 *
 * @param col Column.
 * @param row Row; non-zero = row 1.
 * @return The address.
 */
uint8_t lcd_ddram_addr(unsigned col, unsigned row)
{
    return (uint8_t)((row ? LCD_ROW1_ADDR : 0) + col);
}

/**
 * @brief CGRAM address of a custom character slot.
 *
 * @param slot Slot (low 3 bits used).
 * @return slot * 8.
 */
uint8_t lcd_cgram_addr(unsigned slot)
{
    return (uint8_t)((slot & 0x07) << 3);
}

/**
 * @brief Display Control command.
 *
 * @param on     Display on.
 * @param cursor Underline cursor.
 * @param blink  Blinking cursor.
 * @return LCD_CMD_DISPLAY | D | C | B.
 */
uint8_t lcd_display_cmd(bool on, bool cursor, bool blink)
{
    return (uint8_t)(LCD_CMD_DISPLAY | (on ? LCD_DISPLAY_ON : 0) |
                     (cursor ? LCD_DISPLAY_CURSOR : 0) | (blink ? LCD_DISPLAY_BLINK : 0));
}

/**
 * @brief Chooses the I2C address from a bus scan.
 *
 * @param want  Configured address.
 * @param found Addresses that answered.
 * @param n     Number of addresses in found.
 * @return The chosen address, or -1.
 */
int lcd_pick_addr(uint8_t want, const uint8_t *found, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (found[i] == want) {
            return want;
        }
    }
    if (n == 1) {
        return found[0];
    }
    return -1;
}

/**
 * @brief Copies text into a row from a column, clipped at the right edge.
 *
 * @param row  Row of LCD_COLS cells.
 * @param col  First column.
 * @param text NUL-terminated text.
 * @return strlen(text).
 */
size_t lcd_row_put(uint8_t row[LCD_COLS], unsigned col, const char *text)
{
    size_t len = 0;
    for (const char *p = text; *p != '\0'; p++, len++) {
        // Past the right edge nothing is visible; keep counting for the length.
        if (col < LCD_COLS && len < LCD_COLS - col) {
            row[col + len] = (uint8_t)*p;
        }
    }
    return len;
}

/**
 * @brief Finds the next run of cells that differ between shown and want.
 *
 * @param shown Current cells.
 * @param want  Wanted cells.
 * @param len   Number of cells.
 * @param from  First index to look at.
 * @param start Output: first cell of the run.
 * @return Length of the run, or 0 if nothing else differs.
 */
size_t lcd_diff_next(const uint8_t *shown, const uint8_t *want, size_t len, size_t from,
                     size_t *start)
{
    size_t i = from;
    while (i < len && shown[i] == want[i]) {
        i++;
    }
    if (i >= len) {
        return 0;
    }

    size_t first = i;
    size_t last = i;  // last different cell of the run
    for (i = first + 1; i < len; i++) {
        if (shown[i] != want[i]) {
            last = i;
        } else if (i - last > LCD_DIFF_MAX_GAP) {
            break;  // gap too long: cheaper to reposition
        }
    }
    *start = first;
    return last - first + 1;
}
