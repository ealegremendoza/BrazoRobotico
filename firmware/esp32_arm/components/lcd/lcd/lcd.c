/**
 * @file lcd.c
 * @brief 1602 LCD driver: text API on top of lcd_core and lcd_hal (see lcd.h).
 *
 * Every call runs under the driver mutex. The driver tracks three things:
 *
 * - s_shadow: the cells the panel shows. Row writes compose the wanted row from it and
 *   send only the runs lcd_diff_next() reports. A row whose transfer failed is marked
 *   stale and sent whole on its next write, so an I2C error never leaves the shadow
 *   believing something the panel does not show.
 * - s_ac: the panel's address counter (it auto-increments after each data byte), or
 *   LCD_AC_UNKNOWN. A Set DDRAM Address command is only sent when it differs.
 * - s_cur_col/s_cur_row: the logical cursor, where lcd_write_char() writes and where the
 *   visible cursor is put back after a row write. s_cur_col == LCD_COLS means "past the
 *   end of the row".
 */

#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include "lcd_core.h"
#include "lcd_hal.h"
#include "lcd.h"

// Reported by lcd_get_info(): controller + interface, and the character ROM of the module.
#define DRIVER_NAME "HD44780 + PCF8574"
#define ROM_NAME "A00"

#define GLYPH_DOTS_MASK 0x1F  // 5 dots per CGRAM row

_Static_assert(LCD_GLYPH_ROWS <= LCD_COLS, "a glyph must fit in one data transfer");

static bool s_initialized;
static bool s_backlight;
static bool s_display_on;
static bool s_cursor_show;
static bool s_cursor_blink;

/** What the panel shows, row by row. Valid only for rows that are not stale. */
static uint8_t s_shadow[LCD_ROWS][LCD_COLS];
/** true = a transfer to that row failed: its content on the panel is unknown. */
static bool s_row_stale[LCD_ROWS];
static uint8_t s_cur_col;
static uint8_t s_cur_row;
static uint8_t s_ac = LCD_AC_UNKNOWN;

// ---------- panel access (callers hold the lock) ----------

/**
 * @brief Waits as the datasheet asks: busy-wait if short, sleeping from 1 ms on.
 *
 * @param us Microseconds; 0 = none.
 */
static void wait_us(uint32_t us)
{
    if (us >= 1000) {
        lcd_hal_delay_ms((us + 999) / 1000);
    } else if (us > 0) {
        lcd_hal_delay_us(us);
    }
}

/**
 * @brief Sends one command (RS = 0) in one 4-byte transaction.
 *
 * Clear and Home take 1.52 ms and are waited for; the other commands (37 us) are already
 * covered by the I2C transfer itself.
 *
 * @param cmd HD44780 command.
 * @return ESP_OK, or the I2C error.
 */
static esp_err_t send_cmd(uint8_t cmd)
{
    uint8_t b[LCD_PCF_BYTE_LEN];
    lcd_pcf_byte(cmd, false, s_backlight, b);

    esp_err_t err = lcd_hal_write(b, sizeof(b));
    if (err == ESP_OK && (cmd == LCD_CMD_CLEAR || cmd == LCD_CMD_HOME)) {
        wait_us(LCD_CLEAR_DELAY_US);
    }
    return err;
}

/**
 * @brief Sends data bytes (RS = 1) from the panel's current address, in one transaction.
 *
 * @param buf Characters or glyph rows.
 * @param len Number of bytes, at most LCD_COLS.
 * @return ESP_OK, or the I2C error.
 */
static esp_err_t send_data(const uint8_t *buf, size_t len)
{
    uint8_t tx[LCD_COLS * LCD_PCF_BYTE_LEN];
    for (size_t i = 0; i < len; i++) {
        lcd_pcf_byte(buf[i], true, s_backlight, &tx[i * LCD_PCF_BYTE_LEN]);
    }
    return lcd_hal_write(tx, len * LCD_PCF_BYTE_LEN);
}

/**
 * @brief Moves the panel's address counter to a DDRAM address, unless it is already there.
 *
 * @param addr DDRAM address.
 * @return ESP_OK, or the I2C error (then the address is unknown).
 */
static esp_err_t goto_addr(uint8_t addr)
{
    if (s_ac == addr) {
        return ESP_OK;
    }
    esp_err_t err = send_cmd(LCD_CMD_DDRAM | addr);
    s_ac = err == ESP_OK ? addr : LCD_AC_UNKNOWN;
    return err;
}

/**
 * @brief Writes consecutive cells of one row and records them in the shadow.
 *
 * @param row   Row.
 * @param col   First column.
 * @param cells Character codes.
 * @param n     Number of cells; col + n <= LCD_COLS.
 * @return ESP_OK, or the I2C error (then the row is marked stale).
 */
static esp_err_t send_cells(unsigned row, unsigned col, const uint8_t *cells, size_t n)
{
    esp_err_t err = goto_addr(lcd_ddram_addr(col, row));
    if (err == ESP_OK) {
        err = send_data(cells, n);
    }
    if (err != ESP_OK) {
        // Some of the bytes may have reached the panel: content and address are unknown.
        s_ac = LCD_AC_UNKNOWN;
        s_row_stale[row] = true;
        return err;
    }
    memcpy(&s_shadow[row][col], cells, n);
    s_ac = (uint8_t)(s_ac + n);  // the HD44780 increments after each data byte
    return ESP_OK;
}

/**
 * @brief Makes a row of the panel show want, sending only the cells that differ.
 *
 * A stale row is sent whole.
 *
 * @param row  Row.
 * @param want LCD_COLS character codes.
 * @return ESP_OK, or the first I2C error.
 */
static esp_err_t flush_row(unsigned row, const uint8_t want[LCD_COLS])
{
    if (s_row_stale[row]) {
        esp_err_t err = send_cells(row, 0, want, LCD_COLS);
        if (err == ESP_OK) {
            s_row_stale[row] = false;
        }
        return err;
    }

    size_t start = 0;
    size_t n;
    for (size_t from = 0; (n = lcd_diff_next(s_shadow[row], want, LCD_COLS, from, &start)) > 0;
         from = start + n) {
        esp_err_t err = send_cells(row, (unsigned)start, &want[start], n);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

/**
 * @brief Puts the panel's address back on the logical cursor if the cursor is visible.
 *
 * @return ESP_OK, or the I2C error.
 */
static esp_err_t sync_visible_cursor(void)
{
    if (!s_cursor_show && !s_cursor_blink) {
        return ESP_OK;  // nobody sees it; the next write repositions if needed
    }
    return goto_addr(lcd_ddram_addr(s_cur_col, s_cur_row));
}

/**
 * @brief Power-up sequence: idle byte, then lcd_init_steps[] with their waits.
 *
 * Leaves the display on, cleared, cursor hidden at (0,0). The LCD_POWER_ON_MS wait is
 * done by the caller.
 *
 * @return ESP_OK, or the I2C error.
 */
static esp_err_t panel_init(void)
{
    uint8_t b[LCD_PCF_BYTE_LEN];
    b[0] = lcd_pcf_idle(s_backlight);  // RS = RW = E = 0
    esp_err_t err = lcd_hal_write(b, 1);

    for (size_t i = 0; err == ESP_OK && i < lcd_init_nsteps; i++) {
        const lcd_init_step_t *s = &lcd_init_steps[i];
        if (s->nibble) {
            lcd_pcf_nibble(s->val, false, s_backlight, b);
            err = lcd_hal_write(b, LCD_PCF_NIBBLE_LEN);
        } else {
            lcd_pcf_byte(s->val, false, s_backlight, b);
            err = lcd_hal_write(b, LCD_PCF_BYTE_LEN);
        }
        if (err == ESP_OK) {
            wait_us(s->delay_us);
        }
    }
    return err;
}

/**
 * @brief Records the panel as cleared: blank shadow, address and cursor at (0,0).
 */
static void state_cleared(void)
{
    memset(s_shadow, ' ', sizeof(s_shadow));
    memset(s_row_stale, 0, sizeof(s_row_stale));
    s_cur_col = 0;
    s_cur_row = 0;
    s_ac = 0;
}

/**
 * @brief Records the panel content and address as unknown (after a failed clear).
 */
static void state_unknown(void)
{
    for (int r = 0; r < LCD_ROWS; r++) {
        s_row_stale[r] = true;
    }
    s_ac = LCD_AC_UNKNOWN;
}

/**
 * @brief Checks a (col, row) pair against the screen.
 *
 * @param col Column.
 * @param row Row.
 * @return true if both are on screen.
 */
static bool cell_valid(int col, int row)
{
    return col >= 0 && col < LCD_COLS && row >= 0 && row < LCD_ROWS;
}

/**
 * @brief Composes and writes a row (lock held, driver initialized, row valid).
 *
 * @param col  First column of text.
 * @param row  Row.
 * @param text NUL-terminated text.
 * @param pad  true = the rest of the row becomes spaces; false = it keeps its content.
 * @param len  Output: strlen(text).
 * @return ESP_OK, or the I2C error.
 */
static esp_err_t write_row_locked(unsigned col, unsigned row, const char *text, bool pad,
                                  size_t *len)
{
    uint8_t want[LCD_COLS];
    if (pad) {
        memset(want, ' ', sizeof(want));
    } else {
        memcpy(want, s_shadow[row], sizeof(want));
    }
    *len = lcd_row_put(want, col, text);

    esp_err_t err = flush_row(row, want);
    if (err != ESP_OK) {
        return err;
    }
    size_t end = col + *len;
    s_cur_col = (uint8_t)(end < LCD_COLS ? end : LCD_COLS);
    s_cur_row = (uint8_t)row;
    return sync_visible_cursor();
}

/**
 * @brief Sends Display Control with the given state (lock held, initialized).
 *
 * @param on    Display on.
 * @param show  Underline cursor.
 * @param blink Blinking cursor.
 * @return ESP_OK, or the I2C error (state unchanged).
 */
static esp_err_t display_ctrl_locked(bool on, bool show, bool blink)
{
    esp_err_t err = send_cmd(lcd_display_cmd(on, show, blink));
    if (err == ESP_OK) {
        s_display_on = on;
        s_cursor_show = show;
        s_cursor_blink = blink;
    }
    return err;
}

// ---------- public API ----------

/**
 * @brief Brings up the HAL and initializes the panel.
 *
 * @return ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_NOT_FOUND or the I2C error.
 */
esp_err_t lcd_init(void)
{
    lcd_hal_lock();
    if (s_initialized) {
        lcd_hal_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = lcd_hal_init();
    if (err == ESP_OK) {
        lcd_hal_delay_ms(LCD_POWER_ON_MS);
        s_backlight = true;
        err = panel_init();
        if (err != ESP_OK) {
            s_backlight = false;
            lcd_hal_deinit();
        }
    }
    if (err == ESP_OK) {
        s_display_on = true;
        s_cursor_show = false;
        s_cursor_blink = false;
        state_cleared();
        s_initialized = true;  // only now: other calls see a fully initialized driver
    }
    lcd_hal_unlock();
    return err;
}

/**
 * @brief Clears the display, turns the backlight off and releases the I2C bus.
 *
 * @return ESP_OK, or ESP_ERR_INVALID_STATE if not initialized.
 */
esp_err_t lcd_deinit(void)
{
    lcd_hal_lock();
    if (!s_initialized) {
        lcd_hal_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    // Best effort: release the bus anyway.
    send_cmd(LCD_CMD_CLEAR);
    s_backlight = false;
    uint8_t b = lcd_pcf_idle(false);
    lcd_hal_write(&b, 1);

    lcd_hal_deinit();
    s_initialized = false;
    s_ac = LCD_AC_UNKNOWN;
    lcd_hal_unlock();
    return ESP_OK;
}

/**
 * @brief Clears the display and moves the cursor to (0,0).
 *
 * @return ESP_OK, ESP_ERR_INVALID_STATE or the I2C error.
 */
esp_err_t lcd_clear(void)
{
    lcd_hal_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_initialized) {
        err = send_cmd(LCD_CMD_CLEAR);
        if (err == ESP_OK) {
            state_cleared();
        } else {
            state_unknown();
        }
    }
    lcd_hal_unlock();
    return err;
}

/**
 * @brief Moves the cursor.
 *
 * @param col Column.
 * @param row Row.
 * @return ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG or the I2C error.
 */
esp_err_t lcd_set_cursor(int col, int row)
{
    if (!cell_valid(col, row)) {
        return ESP_ERR_INVALID_ARG;
    }

    lcd_hal_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_initialized) {
        s_cur_col = (uint8_t)col;
        s_cur_row = (uint8_t)row;
        err = goto_addr(lcd_ddram_addr(s_cur_col, s_cur_row));
    }
    lcd_hal_unlock();
    return err;
}

/**
 * @brief Writes one character at the cursor and advances it.
 *
 * @param c Character code.
 * @return ESP_OK, ESP_ERR_INVALID_STATE or the I2C error.
 */
esp_err_t lcd_write_char(char c)
{
    lcd_hal_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_initialized) {
        err = ESP_OK;
        if (s_cur_col < LCD_COLS) {  // past the right edge: dropped
            uint8_t cell = (uint8_t)c;
            err = send_cells(s_cur_row, s_cur_col, &cell, 1);
            if (err == ESP_OK) {
                s_cur_col++;
            }
        }
    }
    lcd_hal_unlock();
    return err;
}

/**
 * @brief Writes a string on one row, clipped at the right edge; only changed cells are sent.
 *
 * @param col     First column.
 * @param row     Row.
 * @param text    NUL-terminated string.
 * @param col_end Output (may be NULL): col + strlen(text).
 * @return ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG or the I2C error.
 */
esp_err_t lcd_write_text(int col, int row, const char *text, int *col_end)
{
    if (text == NULL || !cell_valid(col, row)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = 0;
    lcd_hal_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_initialized) {
        err = write_row_locked((unsigned)col, (unsigned)row, text, false, &len);
    }
    lcd_hal_unlock();

    if (err == ESP_OK && col_end != NULL) {
        size_t end = (size_t)col + len;
        *col_end = end > INT_MAX ? INT_MAX : (int)end;
    }
    return err;
}

/**
 * @brief Writes a whole row padded with spaces; only changed cells are sent.
 *
 * @param row  Row.
 * @param text NUL-terminated string.
 * @return ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG or the I2C error.
 */
esp_err_t lcd_write_line(int row, const char *text)
{
    if (text == NULL || !cell_valid(0, row)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = 0;
    lcd_hal_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_initialized) {
        err = write_row_locked(0, (unsigned)row, text, true, &len);
    }
    lcd_hal_unlock();
    return err;
}

/**
 * @brief Defines a custom character and restores the DDRAM address.
 *
 * @param slot Slot, 0..LCD_CGRAM_SLOTS-1.
 * @param rows LCD_GLYPH_ROWS row bytes, top to bottom.
 * @return ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG or the I2C error.
 */
esp_err_t lcd_define_char(int slot, const uint8_t rows[LCD_GLYPH_ROWS])
{
    if (rows == NULL || slot < 0 || slot >= LCD_CGRAM_SLOTS) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t glyph[LCD_GLYPH_ROWS];
    for (int i = 0; i < LCD_GLYPH_ROWS; i++) {
        glyph[i] = rows[i] & GLYPH_DOTS_MASK;
    }

    lcd_hal_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_initialized) {
        // The address counter now points into the CGRAM: unknown for DDRAM purposes.
        s_ac = LCD_AC_UNKNOWN;
        err = send_cmd(LCD_CMD_CGRAM | lcd_cgram_addr((unsigned)slot));
        if (err == ESP_OK) {
            err = send_data(glyph, sizeof(glyph));
        }
        // Back to the DDRAM, even after an error, so the next data byte lands on screen.
        esp_err_t restore = goto_addr(lcd_ddram_addr(s_cur_col, s_cur_row));
        if (err == ESP_OK) {
            err = restore;
        }
    }
    lcd_hal_unlock();
    return err;
}

/**
 * @brief Turns the backlight on or off.
 *
 * Every PCF8574 byte carries the backlight bit; one byte without an E pulse is enough to
 * change it.
 *
 * @param on true = on.
 * @return ESP_OK, ESP_ERR_INVALID_STATE or the I2C error.
 */
esp_err_t lcd_backlight(bool on)
{
    lcd_hal_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_initialized) {
        uint8_t b = lcd_pcf_idle(on);
        err = lcd_hal_write(&b, 1);
        if (err == ESP_OK) {
            s_backlight = on;
        }
    }
    lcd_hal_unlock();
    return err;
}

/**
 * @brief Shows or hides the cursor.
 *
 * Puts the panel's address on the logical cursor first, so the cursor appears where
 * lcd_write_char() would write.
 *
 * @param show  Underline cursor.
 * @param blink Blinking block cursor.
 * @return ESP_OK, ESP_ERR_INVALID_STATE or the I2C error.
 */
esp_err_t lcd_cursor(bool show, bool blink)
{
    lcd_hal_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_initialized) {
        err = goto_addr(lcd_ddram_addr(s_cur_col, s_cur_row));
        if (err == ESP_OK) {
            err = display_ctrl_locked(s_display_on, show, blink);
        }
    }
    lcd_hal_unlock();
    return err;
}

/**
 * @brief Turns the display on or off (content kept).
 *
 * @param on true = on.
 * @return ESP_OK, ESP_ERR_INVALID_STATE or the I2C error.
 */
esp_err_t lcd_display(bool on)
{
    lcd_hal_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_initialized) {
        err = display_ctrl_locked(on, s_cursor_show, s_cursor_blink);
    }
    lcd_hal_unlock();
    return err;
}

/**
 * @brief Describes the panel, the I2C settings and the pins (no I2C traffic).
 *
 * @param out Output.
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if out is NULL.
 */
esp_err_t lcd_get_info(lcd_info_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lcd_hal_config_t cfg;
    lcd_hal_lock();
    lcd_hal_get_config(&cfg);  // under the lock: the address in use is set by lcd_init()
    bool initialized = s_initialized;
    bool backlight = s_initialized && s_backlight;
    lcd_hal_unlock();

    *out = (lcd_info_t){
        .driver = DRIVER_NAME,
        .cols = LCD_COLS,
        .rows = LCD_ROWS,
        .char_w = LCD_CHAR_W,
        .char_h = LCD_CHAR_H,
        .cgram_slots = LCD_CGRAM_SLOTS,
        .rom = ROM_NAME,
        .i2c_port = cfg.i2c_port,
        .i2c_addr = cfg.i2c_addr,
        .i2c_clock_hz = cfg.i2c_clock_hz,
        .pin_sda = cfg.pin_sda,
        .pin_scl = cfg.pin_scl,
        .backlight = backlight,
        .initialized = initialized,
    };
    return ESP_OK;
}
