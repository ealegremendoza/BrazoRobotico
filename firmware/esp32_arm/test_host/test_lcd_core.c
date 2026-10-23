/**
 * @file test_lcd_core.c
 * @brief Host (PC) tests for main/core/lcd_core.c. No ESP-IDF needed.
 *
 * Build and run from lcd_i2c_lib_idf:
 *
 *   gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main/core \
 *       test_host/test_lcd_core.c main/core/lcd_core.c \
 *       -o /tmp/test_lcd_core && /tmp/test_lcd_core
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lcd_core.h"

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/**
 * @brief Decodes 4 PCF8574 bytes back into the LCD byte, checking the E pulses.
 *
 * @param b  Output of lcd_pcf_byte().
 * @param rs Output: RS bit seen on every byte (-1 if inconsistent).
 * @param bl Output: backlight bit seen on every byte (-1 if inconsistent).
 * @return The LCD byte, or -1 if the E pattern or RW is wrong.
 */
static int pcf_decode(const uint8_t b[LCD_PCF_BYTE_LEN], int *rs, int *bl)
{
    *rs = b[0] & LCD_PCF_RS ? 1 : 0;
    *bl = b[0] & LCD_PCF_BL ? 1 : 0;
    for (int i = 0; i < LCD_PCF_BYTE_LEN; i++) {
        if (((b[i] & LCD_PCF_RS) != 0) != *rs) {
            *rs = -1;
        }
        if (((b[i] & LCD_PCF_BL) != 0) != *bl) {
            *bl = -1;
        }
        int e_expected = (i % 2) == 0;  // E = 1, then E = 0 (falling edge latches)
        if (((b[i] & LCD_PCF_E) != 0) != e_expected || (b[i] & LCD_PCF_RW)) {
            return -1;
        }
    }
    if (b[0] >> 4 != b[1] >> 4 || b[2] >> 4 != b[3] >> 4) {
        return -1;
    }
    return (b[0] & 0xF0) | (b[2] >> 4);
}

/** @brief Known encodings, and every byte round-trips with every RS/backlight combination. */
static void test_pcf_byte(void)
{
    puts("pcf: byte -> {hi|E, hi, lo|E, lo} with RS and backlight on all four");
    uint8_t b[LCD_PCF_BYTE_LEN];

    lcd_pcf_byte(0x28, false, true, b);  // Function Set, backlight on
    CHECK(b[0] == 0x2C && b[1] == 0x28 && b[2] == 0x8C && b[3] == 0x88);

    lcd_pcf_byte('A', true, false, b);  // 0x41 data, backlight off
    CHECK(b[0] == 0x45 && b[1] == 0x41 && b[2] == 0x15 && b[3] == 0x11);

    for (int v = 0; v < 256; v++) {
        for (int rs = 0; rs < 2; rs++) {
            for (int bl = 0; bl < 2; bl++) {
                int got_rs;
                int got_bl;
                lcd_pcf_byte((uint8_t)v, rs, bl, b);
                CHECK(pcf_decode(b, &got_rs, &got_bl) == v);
                CHECK(got_rs == rs && got_bl == bl);
            }
        }
    }
}

/** @brief Nibble: low 4 bits only, on D4..D7; idle byte carries only the backlight. */
static void test_pcf_nibble_idle(void)
{
    puts("pcf: nibble -> {nib|E, nib}; idle byte = backlight bit only");
    uint8_t b[LCD_PCF_NIBBLE_LEN];
    lcd_pcf_nibble(0x3, false, true, b);
    CHECK(b[0] == 0x3C && b[1] == 0x38);
    lcd_pcf_nibble(0xF2, false, false, b);  // high bits ignored
    CHECK(b[0] == 0x24 && b[1] == 0x20);

    CHECK(lcd_pcf_idle(true) == LCD_PCF_BL);
    CHECK(lcd_pcf_idle(false) == 0);
}

/** @brief The init table keeps the hardware-verified sequence. */
static void test_init_steps(void)
{
    puts("init: 0x3 0x3 0x3 0x2 nibbles, then 0x28 0x0C 0x01 0x06 with their waits");
    static const lcd_init_step_t expected[] = {
        {true, 0x3, 5000}, {true, 0x3, 150},   {true, 0x3, 150},     {true, 0x2, 150},
        {false, 0x28, 0},  {false, 0x0C, 0},   {false, 0x01, 2000},  {false, 0x06, 0},
    };
    const size_t n = sizeof(expected) / sizeof(expected[0]);
    CHECK(lcd_init_nsteps == n);
    for (size_t i = 0; i < n && i < lcd_init_nsteps; i++) {
        CHECK(lcd_init_steps[i].nibble == expected[i].nibble);
        CHECK(lcd_init_steps[i].val == expected[i].val);
        CHECK(lcd_init_steps[i].delay_us == expected[i].delay_us);
    }
}

/** @brief DDRAM rows at 0x00/0x40, CGRAM slot n at n * 8, Display Control bits. */
static void test_addresses_and_ctrl(void)
{
    puts("addresses: DDRAM (col,row), CGRAM slots, Display Control flags");
    CHECK(lcd_ddram_addr(0, 0) == 0x00);
    CHECK(lcd_ddram_addr(15, 0) == 0x0F);
    CHECK(lcd_ddram_addr(0, 1) == 0x40);
    CHECK(lcd_ddram_addr(15, 1) == 0x4F);
    CHECK(lcd_ddram_addr(LCD_COLS, 1) == 0x50);  // hidden cell after the last column

    CHECK(lcd_cgram_addr(0) == 0x00);
    CHECK(lcd_cgram_addr(1) == 0x08);
    CHECK(lcd_cgram_addr(7) == 0x38);
    CHECK(lcd_cgram_addr(9) == 0x08);  // low 3 bits only
    CHECK((LCD_CMD_CGRAM | lcd_cgram_addr(7)) == 0x78);

    CHECK(lcd_display_cmd(true, false, false) == 0x0C);
    CHECK(lcd_display_cmd(false, false, false) == 0x08);
    CHECK(lcd_display_cmd(true, true, false) == 0x0E);
    CHECK(lcd_display_cmd(true, true, true) == 0x0F);
    CHECK(lcd_display_cmd(false, false, true) == 0x09);
}

/** @brief Configured address wins; one other device is used; zero or several: -1. */
static void test_pick_addr(void)
{
    puts("pick_addr: configured, single fallback, none, ambiguous");
    const uint8_t with_want[] = {0x20, 0x27, 0x3C};
    const uint8_t single[] = {0x3F};
    const uint8_t two[] = {0x20, 0x3F};
    CHECK(lcd_pick_addr(0x27, with_want, 3) == 0x27);
    CHECK(lcd_pick_addr(0x27, single, 1) == 0x3F);
    CHECK(lcd_pick_addr(0x27, NULL, 0) == -1);
    CHECK(lcd_pick_addr(0x27, two, 2) == -1);
}

/** @brief Text is copied from col, clipped at the right edge; the rest is untouched. */
static void test_row_put(void)
{
    puts("row_put: placement, right-edge clipping, unclipped length, other cells kept");
    uint8_t row[LCD_COLS];

    memset(row, '.', sizeof(row));
    CHECK(lcd_row_put(row, 3, "abc") == 3);
    CHECK(memcmp(row, "...abc..........", LCD_COLS) == 0);

    memset(row, '.', sizeof(row));
    CHECK(lcd_row_put(row, 12, "clipped") == 7);
    CHECK(memcmp(row, "............clip", LCD_COLS) == 0);

    memset(row, '.', sizeof(row));
    CHECK(lcd_row_put(row, 0, "0123456789ABCDEFGHIJ") == 20);
    CHECK(memcmp(row, "0123456789ABCDEF", LCD_COLS) == 0);

    memset(row, '.', sizeof(row));
    CHECK(lcd_row_put(row, LCD_COLS, "x") == 1);  // fully off-screen
    CHECK(lcd_row_put(row, 1000, "x") == 1);
    CHECK(lcd_row_put(row, 5, "") == 0);
    CHECK(memcmp(row, "................", LCD_COLS) == 0);

    // Non-ASCII codes go through untouched (degree sign, custom char 0 alias).
    CHECK(lcd_row_put(row, 0, "\xDF\x08") == 2);
    CHECK(row[0] == 0xDF && row[1] == 0x08);
}

/**
 * @brief Collects every run lcd_diff_next() reports for shown -> want.
 *
 * @param shown  Current cells.
 * @param want   Wanted cells.
 * @param len    Number of cells.
 * @param starts Output: run starts.
 * @param lens   Output: run lengths.
 * @param max    Capacity of starts/lens.
 * @return Number of runs.
 */
static size_t collect_runs(const uint8_t *shown, const uint8_t *want, size_t len, size_t *starts,
                           size_t *lens, size_t max)
{
    size_t nruns = 0;
    size_t start = 0;
    size_t n;
    for (size_t from = 0; nruns < max && (n = lcd_diff_next(shown, want, len, from, &start)) > 0;
         from = start + n) {
        starts[nruns] = start;
        lens[nruns] = n;
        nruns++;
    }
    return nruns;
}

/** @brief Hand-picked cases: nothing, one cell, merged gap of 1, split gap of 2, edges. */
static void test_diff_cases(void)
{
    puts("diff: equal rows, single cell, gap merge/split, first and last cells");
    size_t st[LCD_COLS];
    size_t ln[LCD_COLS];
    const uint8_t *base = (const uint8_t *)"J1+045. J2-012.>";

    CHECK(collect_runs(base, base, LCD_COLS, st, ln, LCD_COLS) == 0);

    size_t start = 99;
    CHECK(lcd_diff_next(base, base, LCD_COLS, 0, &start) == 0);
    CHECK(start == 99);  // untouched when nothing differs

    // One digit changed.
    const uint8_t *one = (const uint8_t *)"J1+046. J2-012.>";
    CHECK(collect_runs(base, one, LCD_COLS, st, ln, LCD_COLS) == 1);
    CHECK(st[0] == 5 && ln[0] == 1);

    // Two digits with one equal cell between: one run of 3.
    const uint8_t *gap1 = (const uint8_t *)"J1+146. J2-012.>";
    CHECK(collect_runs(base, gap1, LCD_COLS, st, ln, LCD_COLS) == 1);
    CHECK(st[0] == 3 && ln[0] == 3);

    // Far apart changes: two runs.
    const uint8_t *split = (const uint8_t *)"J1-045. J2-013.>";
    CHECK(collect_runs(base, split, LCD_COLS, st, ln, LCD_COLS) == 2);
    CHECK(st[0] == 2 && ln[0] == 1);
    CHECK(st[1] == 13 && ln[1] == 1);

    // First and last cells.
    const uint8_t *edges = (const uint8_t *)"X1+045. J2-012.<";
    CHECK(collect_runs(base, edges, LCD_COLS, st, ln, LCD_COLS) == 2);
    CHECK(st[0] == 0 && ln[0] == 1);
    CHECK(st[1] == 15 && ln[1] == 1);

    // Everything different: one run of the whole row.
    const uint8_t *all = (const uint8_t *)"################";
    CHECK(collect_runs(base, all, LCD_COLS, st, ln, LCD_COLS) == 1);
    CHECK(st[0] == 0 && ln[0] == LCD_COLS);

    // from skips earlier differences.
    CHECK(lcd_diff_next(base, edges, LCD_COLS, 1, &start) == 1 && start == 15);
    CHECK(lcd_diff_next(base, edges, LCD_COLS, LCD_COLS, &start) == 0);
}

/**
 * @brief Random rows: applying the runs to the shadow always yields want, and runs are
 *        minimal (start and end on changed cells, separated by more than the max gap).
 */
static void test_diff_fuzz(void)
{
    puts("diff: 20000 random rows, applying the runs reproduces want, runs are tight");
    srand(1602);
    for (int iter = 0; iter < 20000; iter++) {
        uint8_t shown[LCD_COLS];
        uint8_t want[LCD_COLS];
        int density = rand() % 101;  // percent of cells that change
        for (int i = 0; i < LCD_COLS; i++) {
            shown[i] = (uint8_t)(rand() % 4);  // small alphabet: many accidental matches
            want[i] = (rand() % 100) < density ? (uint8_t)(rand() % 4) : shown[i];
        }

        size_t st[LCD_COLS];
        size_t ln[LCD_COLS];
        size_t nruns = collect_runs(shown, want, LCD_COLS, st, ln, LCD_COLS);

        uint8_t panel[LCD_COLS];
        memcpy(panel, shown, sizeof(panel));
        size_t sent = 0;
        for (size_t r = 0; r < nruns; r++) {
            CHECK(ln[r] >= 1 && st[r] + ln[r] <= LCD_COLS);
            CHECK(shown[st[r]] != want[st[r]]);                         // starts on a change
            CHECK(shown[st[r] + ln[r] - 1] != want[st[r] + ln[r] - 1]); // ends on a change
            if (r > 0) {
                size_t prev_end = st[r - 1] + ln[r - 1];
                CHECK(st[r] - prev_end > LCD_DIFF_MAX_GAP);  // gap > max: worth a jump
            }
            memcpy(&panel[st[r]], &want[st[r]], ln[r]);
            sent += ln[r];
        }
        CHECK(memcmp(panel, want, LCD_COLS) == 0);

        size_t changed = 0;
        for (int i = 0; i < LCD_COLS; i++) {
            changed += shown[i] != want[i];
        }
        CHECK(sent >= changed);
        CHECK((changed == 0) == (nruns == 0));
    }
}

/**
 * @brief Runs every lcd_core test.
 *
 * @return 0 if all checks pass, 1 otherwise.
 */
int main(void)
{
    test_pcf_byte();
    test_pcf_nibble_idle();
    test_init_steps();
    test_addresses_and_ctrl();
    test_pick_addr();
    test_row_put();
    test_diff_cases();
    test_diff_fuzz();

    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
