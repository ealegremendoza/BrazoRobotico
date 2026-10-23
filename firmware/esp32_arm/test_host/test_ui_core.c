/**
 * @file test_ui_core.c
 * @brief Host (PC) tests for components/lcd_ui/core/ui_core.c. No ESP-IDF needed.
 *
 * Build and run from lcd_i2c_ui_idf:
 *
 *   gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I components/lcd_ui/core \
 *       test_host/test_ui_core.c components/lcd_ui/core/ui_core.c \
 *       -o /tmp/test_ui_core && /tmp/test_ui_core
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_core.h"

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                   \
        }                                                                 \
    } while (0)

#define COLS 16
#define ROWS 2
#define TEXT_MAX 32  // APP_TEXT_MAX of the menu app (2 x 16)
#define DEG "\xDF"   // ROM A00 degree sign, as in UI_STR_DEGREE

/**
 * @brief Layout parameters of the 16x2 panel.
 *
 * @param title Title row present.
 * @return Parameters.
 */
static ui_layout_params_t params(bool title)
{
    return (ui_layout_params_t){.cols = COLS, .rows = ROWS, .title = title};
}

/**
 * @brief Shorthand for a body item.
 *
 * @param w      Width (0 = rest of the row).
 * @param h      Height (0 = flex).
 * @param beside On the previous item's row.
 * @param gap    Cells before it.
 * @return The item.
 */
static ui_layout_item_t item(int w, int h, bool beside, int gap)
{
    return (ui_layout_item_t){.w = w, .h = h, .beside = beside, .gap = gap};
}

/**
 * @brief Shorthand for a footer item (one row, whole width).
 *
 * @return The item.
 */
static ui_layout_item_t footer(void)
{
    return (ui_layout_item_t){.w = 0, .h = 1, .footer = true};
}

/**
 * @brief Checks the placement of one item.
 *
 * @param it  Item.
 * @param row Expected row.
 * @param col Expected column.
 * @param w   Expected width.
 * @param h   Expected height.
 * @return 1 if equal.
 */
static int at(const ui_layout_item_t *it, int row, int col, int w, int h)
{
    return it->row == row && it->col == col && it->width == w && it->height == h;
}

/**
 * @brief Checks one wrapped line against the expected text.
 *
 * @param text     Wrapped text.
 * @param line     Line.
 * @param expected Expected content.
 * @return 1 if equal.
 */
static int line_is(const char *text, ui_line_t line, const char *expected)
{
    return line.len == strlen(expected) && memcmp(text + line.start, expected, line.len) == 0;
}

/** @brief Stacking, beside, fill width, title and footer rows. */
static void test_layout_basic(void)
{
    puts("layout: stacked lines, beside with gap, fill width, title row, footer row");
    ui_layout_item_t it[3] = {item(5, 1, false, 0), item(0, 1, true, 1), item(0, 1, false, 0)};
    ui_layout_params_t p = params(false);
    ui_layout_t out;
    CHECK(ui_layout_compute(&p, it, 3, &out));
    CHECK(out.title_row == -1 && out.body_top == 0 && out.body_bottom == 2);
    CHECK(at(&it[0], 0, 0, 5, 1) && at(&it[1], 0, 6, 10, 1) && at(&it[2], 1, 0, 16, 1));

    // Title on row 0, one body line, nothing left.
    ui_layout_item_t one[1] = {item(0, 1, false, 0)};
    p = params(true);
    CHECK(ui_layout_compute(&p, one, 1, &out));
    CHECK(out.title_row == 0 && out.body_top == 1 && at(&one[0], 1, 0, 16, 1));

    // Footer goes to the bottom even when declared first; body above it.
    ui_layout_item_t f[2] = {footer(), item(0, 1, false, 0)};
    p = params(false);
    CHECK(ui_layout_compute(&p, f, 2, &out));
    CHECK(out.body_bottom == 1 && at(&f[0], 1, 0, 16, 1) && at(&f[1], 0, 0, 16, 1));
}

/** @brief Flex items share the free rows; tall images set the line height. */
static void test_layout_flex(void)
{
    puts("layout: flex takes the free rows, shares them, tall fixed items");
    ui_layout_item_t m[1] = {item(0, 0, false, 0)};
    ui_layout_params_t p = params(false);
    ui_layout_t out;
    CHECK(ui_layout_compute(&p, m, 1, &out) && at(&m[0], 0, 0, 16, 2));

    // Flex message + input pinned below it: 1 row each.
    ui_layout_item_t mi[2] = {item(0, 0, false, 0), footer()};
    CHECK(ui_layout_compute(&p, mi, 2, &out) && at(&mi[0], 0, 0, 16, 1));
    CHECK(at(&mi[1], 1, 0, 16, 1));

    // Two flex lines share 2 rows.
    ui_layout_item_t two[2] = {item(0, 0, false, 0), item(0, 0, false, 0)};
    CHECK(ui_layout_compute(&p, two, 2, &out) && at(&two[0], 0, 0, 16, 1));
    CHECK(at(&two[1], 1, 0, 16, 1));

    // A 2x2 image with a text beside it: the text sits on the top row.
    ui_layout_item_t img[2] = {item(2, 2, false, 0), item(0, 1, true, 1)};
    CHECK(ui_layout_compute(&p, img, 2, &out) && at(&img[0], 0, 0, 2, 2));
    CHECK(at(&img[1], 0, 3, 13, 1));
}

/** @brief Overflow: right edge, bottom, no room for flex, footer over title, bad beside. */
static void test_layout_overflow(void)
{
    puts("layout: overflow reports the first widget that does not fit");
    ui_layout_params_t p = params(false);
    ui_layout_t out;

    ui_layout_item_t three[3] = {item(0, 1, false, 0), item(0, 1, false, 0), item(0, 1, false, 0)};
    CHECK(!ui_layout_compute(&p, three, 3, &out) && out.overflow == 2);

    ui_layout_item_t wide[2] = {item(10, 1, false, 0), item(6, 1, true, 1)};  // 10 + 1 + 6 = 17
    CHECK(!ui_layout_compute(&p, wide, 2, &out) && out.overflow == 1);
    wide[1].gap = 0;  // exactly 16
    CHECK(ui_layout_compute(&p, wide, 2, &out));

    ui_layout_item_t full[2] = {item(0, 1, false, 0), item(0, 1, true, 0)};  // fill leaves 0
    CHECK(!ui_layout_compute(&p, full, 2, &out) && out.overflow == 1);

    ui_layout_item_t flex[3] = {item(0, 1, false, 0), item(0, 1, false, 0), item(0, 0, false, 0)};
    CHECK(!ui_layout_compute(&p, flex, 3, &out) && out.overflow == 2);  // no row left

    ui_layout_item_t lone[1] = {item(4, 1, true, 0)};  // beside nothing
    CHECK(!ui_layout_compute(&p, lone, 1, &out) && out.overflow == 0);

    ui_layout_params_t pt = params(true);
    ui_layout_item_t ff[2] = {footer(), footer()};  // 2 footers + title = 3 rows
    CHECK(!ui_layout_compute(&pt, ff, 2, &out) && out.overflow == 0);

    ui_layout_item_t tall[1] = {item(2, 2, false, 0)};  // image under a title
    CHECK(!ui_layout_compute(&pt, tall, 1, &out) && out.overflow == 0);

    ui_layout_item_t tf[1] = {{.w = 0, .h = 2, .footer = true}};  // tall footer
    CHECK(!ui_layout_compute(&p, tf, 1, &out) && out.overflow == 0);

    CHECK(ui_layout_compute(&p, NULL, 0, &out) && out.body_bottom == 2);
}

/**
 * @brief Runs the layout of one app screen.
 *
 * @param title Title row.
 * @param it    Items.
 * @param n     Number of items.
 * @return true if it fits.
 */
static bool screen_fits(bool title, ui_layout_item_t *it, size_t n)
{
    ui_layout_params_t p = params(title);
    ui_layout_t out;
    return ui_layout_compute(&p, it, n, &out);
}

/** @brief Every screen of the menu app (as built in screens.c) fits 16x2. */
static void test_layout_app_screens(void)
{
    puts("layout: HOME, MENU, JOINTS, CALIBRAR x3, MENSAJES x2, INFO, APAGAR fit 16x2");

    // HOME: 2x1 image + 14-char title, 2x1 image + hint.
    ui_layout_item_t home[4] = {item(2, 1, false, 0), item(0, 1, true, 0), item(2, 1, false, 0),
                                item(0, 1, true, 0)};
    CHECK(screen_fits(false, home, 4));
    CHECK(at(&home[1], 0, 2, 14, 1) && (int)strlen("BRAZO ROBOTICO") <= home[1].width);
    CHECK(at(&home[3], 1, 2, 14, 1));

    // MENU: one flex menu, 2 rows; the longest label + marker fits.
    ui_layout_item_t menu[1] = {item(0, 0, false, 0)};
    CHECK(screen_fits(false, menu, 1) && at(&menu[0], 0, 0, 16, 2));
    CHECK(1 + strlen("CALIBRAR") <= 16 && 1 + strlen("MENSAJES") <= 16);

    // JOINTS: one flex list on both rows (lines checked in test_list_joints).
    ui_layout_item_t jl[1] = {item(0, 0, false, 0)};
    CHECK(screen_fits(false, jl, 1) && at(&jl[0], 0, 0, 16, 2));

    // CALIBRAR confirm: "CALIBRAR" (8) + "q:menu" / "(SIMULADA)" (10) + "Enter".
    ui_layout_item_t cc[4] = {item(8, 1, false, 0), item(0, 1, true, 0), item(10, 1, false, 0),
                              item(0, 1, true, 0)};
    CHECK(screen_fits(false, cc, 4) && cc[1].width >= (int)strlen("q:menu"));
    CHECK(cc[3].width >= (int)strlen("Enter"));
    // Running: steps row / 11-cell bar + gap + "100%".
    ui_layout_item_t cr[3] = {item(0, 1, false, 0), item(11, 1, false, 0), item(0, 1, true, 1)};
    CHECK(screen_fits(false, cr, 3) && cr[2].width == 4);
    // Done: title + "(SIMULADA)" + "Enter".
    ui_layout_item_t cd[2] = {item(10, 1, false, 0), item(0, 1, true, 0)};
    CHECK(screen_fits(true, cd, 2) && strlen("CALIBRACION OK") <= 16);

    // MENSAJES message view: flex message on both rows.
    ui_layout_item_t tm[1] = {item(0, 0, false, 0)};
    CHECK(screen_fits(false, tm, 1) && tm[0].height == 2);
    // Typing view: "MENSAJES" + "32/32" / input line.
    ui_layout_item_t tt[3] = {item(8, 1, false, 0), item(0, 1, true, 0), item(0, 1, false, 0)};
    CHECK(screen_fits(false, tt, 3) && tt[1].width >= (int)strlen("32/32"));

    // INFO: one flex list on both rows.
    ui_layout_item_t info[1] = {item(0, 0, false, 0)};
    CHECK(screen_fits(false, info, 1) && at(&info[0], 0, 0, 16, 2));

    // APAGAR: title "APAGANDO SISTEMA" (16) / counter (1) + gap + bar (rest).
    ui_layout_item_t pw[2] = {item(1, 1, false, 0), item(0, 1, true, 1)};
    CHECK(screen_fits(true, pw, 2) && at(&pw[1], 1, 2, 14, 1));
    CHECK(strlen("APAGANDO SISTEMA") == 16);
}

/** @brief Cells: alignment, cutting, padding. */
static void test_cells_put(void)
{
    puts("cells: left/center/right, cut to the span, padded with spaces");
    char c[9];
    c[8] = '\0';
    ui_cells_put(c, 8, "ab", 2, UI_ALIGN_LEFT);
    CHECK(strcmp(c, "ab      ") == 0);
    ui_cells_put(c, 8, "ab", 2, UI_ALIGN_RIGHT);
    CHECK(strcmp(c, "      ab") == 0);
    ui_cells_put(c, 8, "abc", 3, UI_ALIGN_CENTER);
    CHECK(strcmp(c, "  abc   ") == 0);  // odd leftover to the right
    ui_cells_put(c, 8, "0123456789", 10, UI_ALIGN_RIGHT);
    CHECK(strcmp(c, "01234567") == 0);
    ui_cells_put(c, 8, NULL, 5, UI_ALIGN_LEFT);
    CHECK(strcmp(c, "        ") == 0);
}

/** @brief Word wrap: fitting, breaking at spaces, long words, spaces, limits. */
static void test_wrap(void)
{
    puts("wrap: empty, exact fit, break at spaces, hard-break long words, capacity");
    ui_line_t l[8];
    CHECK(ui_wrap("", 16, l, 8) == 0 && ui_wrap("   ", 16, l, 8) == 0);
    CHECK(ui_wrap(NULL, 16, l, 8) == 0 && ui_wrap("abc", 0, l, 8) == 0);

    const char *t1 = "hola mundo desde";  // exactly 16
    CHECK(ui_wrap(t1, 16, l, 8) == 1 && line_is(t1, l[0], "hola mundo desde"));
    const char *t2 = "hola mundo desde el monitor";
    CHECK(ui_wrap(t2, 16, l, 8) == 2 && line_is(t2, l[0], "hola mundo desde"));
    CHECK(line_is(t2, l[1], "el monitor"));
    const char *t3 = "abcdefghijklmnopqrst";  // one 20-char word
    CHECK(ui_wrap(t3, 16, l, 8) == 2 && line_is(t3, l[0], "abcdefghijklmnop"));
    CHECK(line_is(t3, l[1], "qrst"));
    const char *t4 = "  a   b  ";
    CHECK(ui_wrap(t4, 16, l, 8) == 1 && line_is(t4, l[0], "a   b"));
    CHECK(ui_wrap(t2, 16, NULL, 0) == 2);

    // The driver name of lcd_get_info() on INFO: two lines.
    const char *drv = "HD44780 + PCF8574";
    CHECK(ui_wrap(drv, 16, l, 8) == 2 && line_is(drv, l[0], "HD44780 +"));
    CHECK(line_is(drv, l[1], "PCF8574"));
}

/** @brief Message fit: word wrap, character-wrap fallback, every 32-char text fits 2x16. */
static void test_msg_fit(void)
{
    puts("message: word wrap, else character wrap; any 32-char text fits 16x2");
    ui_line_t l[ROWS];
    size_t n = 99;
    CHECK(ui_msg_fit("", 16, 2, l, ROWS, &n) && n == 0);
    CHECK(ui_msg_fit(NULL, 16, 2, l, ROWS, &n) && n == 0);

    const char *w = "hola mundo desde el monitor";
    CHECK(ui_msg_fit(w, 16, 2, l, ROWS, &n) && n == 2 && line_is(w, l[1], "el monitor"));

    // Word wrap would need 3 rows: plain slices instead.
    const char *c = "abc defghijklmnopq rstuvwxyz0123";  // 32
    CHECK(ui_wrap(c, 16, NULL, 0) == 3);
    CHECK(ui_msg_fit(c, 16, 2, l, ROWS, &n) && n == 2);
    CHECK(line_is(c, l[0], "abc defghijklmno") && line_is(c, l[1], "pq rstuvwxyz0123"));

    // Too long even sliced: the rest is dropped.
    char big[40];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    CHECK(!ui_msg_fit(big, 16, 2, l, ROWS, &n) && n == 2 && l[1].start == 16 && l[1].len == 16);

    // Placeholder of MENSAJES: word-wrapped on 2 rows, nothing dropped.
    const char *ph = "Escriba y Enter. Enter vacio:menu";
    CHECK(ui_msg_fit(ph, 16, 2, l, ROWS, &n) && n == 2 && ui_wrap(ph, 16, NULL, 0) == 2);

    // Random printable texts up to TEXT_MAX always show completely.
    srand(1234);
    for (int k = 0; k < 20000; k++) {
        char t[TEXT_MAX + 1];
        size_t len = (size_t)(rand() % (TEXT_MAX + 1));
        for (size_t i = 0; i < len; i++) {
            t[i] = (rand() % 4 == 0) ? ' ' : (char)('a' + rand() % 26);
        }
        t[len] = '\0';
        size_t lines = 0;
        bool all = ui_msg_fit(t, 16, 2, l, ROWS, &lines);
        CHECK(all && lines <= 2);
        size_t shown = 0;
        for (size_t i = 0; i < lines; i++) {
            CHECK(l[i].len <= 16 && l[i].start + l[i].len <= len);
            shown += l[i].len;
        }
        size_t non_space = 0;
        for (size_t i = 0; i < len; i++) {
            non_space += t[i] != ' ';
        }
        CHECK(shown >= non_space);  // no visible character lost
        if (failures > 20) {
            break;
        }
    }
}

/** @brief Fixed value fields (JOINTS) and counters (APAGAR). */
static void test_fields(void)
{
    puts("fields: \"%+04.0f\" + degree joint field, '#' when too long, centered counter");
    char out[16];
    CHECK(ui_fmt_fixed(out, sizeof(out), "%+04.0f" DEG, 45.2, 5) == 5);
    CHECK(strcmp(out, "+045" DEG) == 0);
    CHECK(ui_fmt_fixed(out, sizeof(out), "%+04.0f" DEG, -12.0, 5) == 5);
    CHECK(strcmp(out, "-012" DEG) == 0);
    CHECK(ui_fmt_fixed(out, sizeof(out), "%+04.0f" DEG, 120.0, 5) == 5);
    CHECK(strcmp(out, "+120" DEG) == 0);
    CHECK(ui_fmt_fixed(out, sizeof(out), "%+04.0f" DEG, -999.4, 5) == 5);
    CHECK(strcmp(out, "-999" DEG) == 0);
    CHECK(ui_fmt_fixed(out, sizeof(out), "%+04.0f" DEG, 1000.0, 5) == 5);
    CHECK(strcmp(out, "#####") == 0);
    CHECK(ui_fmt_fixed(out, sizeof(out), "%.1f", 1.5, 6) == 6 && strcmp(out, "   1.5") == 0);
    CHECK(ui_fmt_fixed(out, sizeof(out), NULL, 1.0, 5) == 0 && out[0] == '\0');
    CHECK(ui_fmt_fixed(out, 5, "%.0f", 1.0, 5) == 0 && out[0] == '\0');

    CHECK(ui_fmt_counter(out, sizeof(out), 5, 1) == 1 && strcmp(out, "5") == 0);
    CHECK(ui_fmt_counter(out, sizeof(out), 5, 4) == 4 && strcmp(out, " 5  ") == 0);
    CHECK(ui_fmt_counter(out, sizeof(out), 10, 1) == 1 && strcmp(out, "#") == 0);
    CHECK(ui_fmt_counter(out, sizeof(out), -3, 2) == 2 && strcmp(out, "-3") == 0);
    CHECK(ui_fmt_counter(out, sizeof(out), 1, 0) == 0 && out[0] == '\0');
}

/** @brief Input line: prompt, tail, cursor, padding. */
static void test_input_format(void)
{
    puts("input: \">\" + last 14 chars + \"_\", padded to 16");
    char out[32];
    CHECK(ui_input_format("", 16, out, sizeof(out)) == 16 && strcmp(out, ">_              ") == 0);
    CHECK(ui_input_format(NULL, 16, out, sizeof(out)) == 16 && strncmp(out, ">_ ", 3) == 0);
    CHECK(ui_input_format("hola", 16, out, sizeof(out)) == 16);
    CHECK(strcmp(out, ">hola_          ") == 0);
    CHECK(ui_input_format("abcdefghijklmn", 16, out, sizeof(out)) == 16);  // 14: all shown
    CHECK(strcmp(out, ">abcdefghijklmn_") == 0);
    CHECK(ui_input_format("0123456789abcdefghijklmnopqrstuv", 16, out, sizeof(out)) == 16);
    CHECK(strcmp(out, ">ijklmnopqrstuv_") == 0);  // a full 32-char line: its tail
    CHECK(ui_input_format("x", 2, out, sizeof(out)) == 2 && strcmp(out, ">_") == 0);
    CHECK(ui_input_format("x", 1, out, sizeof(out)) == 0 && out[0] == '\0');
    CHECK(ui_input_format("x", 16, out, 16) == 0 && out[0] == '\0');
}

/** @brief Progress: 5 steps per cell, one partial cell, partial glyphs. */
static void test_progress(void)
{
    puts("progress: 5 dots per cell, levels, partial-block glyphs");
    uint8_t lv[16];
    CHECK(ui_progress_levels(0, 4, lv) == 0 && lv[0] == 0 && lv[3] == 0);
    CHECK(ui_progress_levels(100, 4, lv) == 20 && lv[0] == 5 && lv[3] == 5);
    CHECK(ui_progress_levels(150, 4, lv) == 20);
    // 4 cells = 20 steps; 35 % = 7 steps: one full cell and a 2-dot cell.
    CHECK(ui_progress_levels(35, 4, lv) == 7 && lv[0] == 5 && lv[1] == 2 && lv[2] == 0);
    // CALIBRAR bar (11 cells = 55 steps) at 25 %: 13 steps = 2 full + 3 dots.
    CHECK(ui_progress_levels(25, 11, lv) == 13 && lv[1] == 5 && lv[2] == 3 && lv[3] == 0);
    // Every percent: levels are monotonic, at most one partial cell, sum == steps.
    for (unsigned pct = 0; pct <= 100; pct++) {
        size_t steps = ui_progress_levels(pct, 14, lv);
        size_t sum = 0;
        int partial = 0;
        for (int i = 0; i < 14; i++) {
            sum += lv[i];
            partial += lv[i] > 0 && lv[i] < 5;
            CHECK(i == 0 || lv[i] <= lv[i - 1]);
        }
        CHECK(sum == steps && partial <= 1 && steps == 14 * 5 * pct / 100);
    }

    ui_glyph_t g;
    ui_progress_glyph(1, g);
    CHECK(g[0] == 0x10 && g[7] == 0x10);
    ui_progress_glyph(2, g);
    CHECK(g[3] == 0x18);
    ui_progress_glyph(4, g);
    CHECK(g[0] == 0x1E);
    ui_progress_glyph(5, g);
    CHECK(g[0] == 0x1F);
    ui_progress_glyph(0, g);
    CHECK(g[0] == 0x00);
}

/** @brief Menu window: minimal scroll, wrap jumps, never empty rows at the end. */
static void test_menu_window(void)
{
    puts("menu window: 5 items in 2 rows, minimal scroll, wrap-around jumps");
    CHECK(ui_menu_window(0, 5, 2, 0) == 0);
    CHECK(ui_menu_window(1, 5, 2, 0) == 0);  // still visible: no scroll
    CHECK(ui_menu_window(2, 5, 2, 0) == 1);  // below: scroll by one
    CHECK(ui_menu_window(3, 5, 2, 1) == 2);
    CHECK(ui_menu_window(4, 5, 2, 2) == 3);
    CHECK(ui_menu_window(0, 5, 2, 3) == 0);  // wrap from the last to the first
    CHECK(ui_menu_window(4, 5, 2, 0) == 3);  // wrap from the first to the last
    CHECK(ui_menu_window(2, 5, 2, 3) == 2);  // above: scroll up by one
    CHECK(ui_menu_window(3, 5, 2, 4) == 3);  // top past the end is clamped
    CHECK(ui_menu_window(1, 2, 2, 1) == 0);  // fewer items than rows
    CHECK(ui_menu_window(0, 0, 2, 0) == 0 && ui_menu_window(0, 3, 0, 1) == 0);

    // Walking the app menu down and up keeps the selection inside the window.
    size_t top = 0;
    for (size_t step = 0; step < 12; step++) {
        size_t sel = step % 5;
        top = ui_menu_window(sel, 5, 2, top);
        CHECK(sel >= top && sel < top + 2 && top <= 3);
    }
}

/**
 * @brief Expected indicators of a 2-row window over the 5-item app menu.
 *
 * @param top First item shown.
 * @param r0  Output: indicator of row 0.
 * @param r1  Output: indicator of row 1.
 */
static void app_menu_indicators(size_t top, ui_scroll_ind_t *r0, ui_scroll_ind_t *r1)
{
    *r0 = ui_scroll_indicator(0, 2, top, 5);
    *r1 = ui_scroll_indicator(1, 2, top, 5);
}

/** @brief Scroll indicators: every selection of the app menu, n <= rows, one row. */
static void test_menu_indicators(void)
{
    puts("menu indicators: up on the first row, down on the last, list position only");
    // Every selection walking down (with the wrap 4 -> 0) and walking up (0 -> 4).
    // Rows of the window: top, top + 1. Expected (row 0, row 1) per selection:
    //   sel 0 top 0: (none, down)   sel 1 top 0: (none, down)   sel 2 top 1: (up, down)
    //   sel 3 top 2: (up, down)     sel 4 top 3: (up, none)
    static const size_t DOWN_SEL[] = {0, 1, 2, 3, 4, 0};
    static const size_t DOWN_TOP[] = {0, 0, 1, 2, 3, 0};
    static const size_t UP_SEL[] = {0, 4, 3, 2, 1, 0};
    static const size_t UP_TOP[] = {0, 3, 3, 2, 1, 0};
    const ui_scroll_ind_t N = UI_SCROLL_IND_NONE;
    const ui_scroll_ind_t U = UI_SCROLL_IND_UP;
    const ui_scroll_ind_t D = UI_SCROLL_IND_DOWN;
    // Indexed by top (0..3).
    const ui_scroll_ind_t R0[4] = {N, U, U, U};
    const ui_scroll_ind_t R1[4] = {D, D, D, N};

    size_t top = 0;
    for (size_t i = 0; i < 6; i++) {
        top = ui_menu_window(DOWN_SEL[i], 5, 2, top);
        CHECK(top == DOWN_TOP[i]);
        ui_scroll_ind_t r0, r1;
        app_menu_indicators(top, &r0, &r1);
        CHECK(r0 == R0[top] && r1 == R1[top]);
    }
    top = 0;
    for (size_t i = 0; i < 6; i++) {
        top = ui_menu_window(UP_SEL[i], 5, 2, top);
        CHECK(top == UP_TOP[i]);
        ui_scroll_ind_t r0, r1;
        app_menu_indicators(top, &r0, &r1);
        CHECK(r0 == R0[top] && r1 == R1[top]);
    }
    // The wrap does not light both arrows: sel 0 after sel 4 is the top window again.
    CHECK(ui_scroll_indicator(0, 2, ui_menu_window(0, 5, 2, 3), 5) == N);

    // Everything fits: blank, and no glyph spent.
    for (size_t n = 1; n <= 2; n++) {
        for (size_t r = 0; r < 2; r++) {
            CHECK(ui_scroll_indicator(r, 2, 0, n) == N);
        }
        CHECK(ui_scroll_glyph_count(n, 2) == 0);
    }
    CHECK(ui_scroll_glyph_count(3, 2) == UI_SCROLL_GLYPHS && ui_scroll_glyph_count(5, 2) == 2);
    CHECK(ui_scroll_glyph_count(1, 1) == 0 && ui_scroll_glyph_count(2, 1) == 2);

    // One row: first and last at once. DOWN wins while items are below; UP only at the end.
    CHECK(ui_scroll_indicator(0, 1, 0, 5) == D);
    CHECK(ui_scroll_indicator(0, 1, 2, 5) == D);  // both apply: down (static rule)
    CHECK(ui_scroll_indicator(0, 1, 4, 5) == U);
    CHECK(ui_scroll_indicator(0, 1, 0, 1) == N);

    // Three rows over 5: the middle row never has an indicator.
    CHECK(ui_scroll_indicator(1, 3, 1, 5) == N);
    CHECK(ui_scroll_indicator(0, 3, 1, 5) == U && ui_scroll_indicator(2, 3, 1, 5) == D);
    CHECK(ui_scroll_indicator(2, 3, 2, 5) == N && ui_scroll_indicator(0, 3, 2, 5) == U);
    // Out of range rows.
    CHECK(ui_scroll_indicator(2, 2, 0, 5) == N && ui_scroll_indicator(0, 0, 0, 5) == N);
}

/** @brief Menu labels: marker + label + 2 reserved cells; the app labels fit. */
static void test_menu_label_width(void)
{
    puts("menu labels: 13 cells on a 16-cell row, app labels fit, wider ones refused");
    CHECK(ui_menu_label_fits(13, COLS));
    CHECK(!ui_menu_label_fits(14, COLS));  // would reach the gap cell
    CHECK(!ui_menu_label_fits(15, COLS));  // the old limit (marker only) is now refused
    CHECK(ui_menu_label_fits(0, 4) && ui_menu_label_fits(1, 4) && !ui_menu_label_fits(2, 4));
    CHECK(!ui_menu_label_fits(0, 3) && !ui_menu_label_fits(0, 0));

    // Same labels as app_menu_labels[].
    const char *const labels[] = {"JOINTS", "CALIBRAR", "MENSAJES", "INFO", "APAGAR"};
    for (size_t i = 0; i < 5; i++) {
        CHECK(ui_menu_label_fits(strlen(labels[i]), COLS));
    }
    CHECK(!ui_menu_label_fits(strlen("CONFIGURACIONES"), COLS));  // 15 characters
}

/** @brief Arrow glyphs: distinct, up mirrors down, cursor row blank. */
static void test_menu_arrow_glyphs(void)
{
    puts("menu arrows: up and down are 5x8 mirrors with a blank cursor row");
    ui_glyph_t up, down, none;
    ui_scroll_arrow_glyph(UI_SCROLL_IND_UP, up);
    ui_scroll_arrow_glyph(UI_SCROLL_IND_DOWN, down);
    ui_scroll_arrow_glyph(UI_SCROLL_IND_NONE, none);
    CHECK(memcmp(up, down, sizeof(up)) != 0);
    CHECK(up[0] == 0x04 && up[1] == 0x0E && down[6] == 0x04 && down[5] == 0x0E);
    for (size_t r = 0; r < UI_GLYPH_ROWS - 1; r++) {
        CHECK(up[r] == down[UI_GLYPH_ROWS - 2 - r]);  // rows 0..6 mirrored
        CHECK((up[r] & ~0x1F) == 0 && none[r] == 0);
    }
    CHECK(up[7] == 0 && down[7] == 0);
}

#define INFO_COLS 14   // UI_SCREEN_COLS - UI_SCROLL_SIDE_COLS
#define INFO_LINES 16  // INFO_MAX_LINES of screens.c

/**
 * @brief Builds the INFO lines like screens.c's info_build(): every entry word-wrapped to
 *        INFO_COLS.
 *
 * @param out   Output lines.
 * @param drv   Driver name.
 * @param port  I2C port.
 * @param addr  I2C address.
 * @param khz   I2C clock in kHz.
 * @param sda   SDA pin.
 * @param scl   SCL pin.
 * @param wid   Widget pool.
 * @param tmax  Text limit.
 * @return Number of lines.
 */
static size_t info_lines(char out[INFO_LINES][32], const char *drv, int port, int addr, int khz,
                         int sda, int scl, int wid, int tmax)
{
    char e[12][48];
    snprintf(e[0], sizeof(e[0]), "INFO q:menu");
    snprintf(e[1], sizeof(e[1]), "%s", drv);
    snprintf(e[2], sizeof(e[2]), "LCD %dx%d %dx%dpt", 16, 2, 5, 8);
    snprintf(e[3], sizeof(e[3]), "ROM %s", "A00");
    snprintf(e[4], sizeof(e[4]), "Glifos %d %dref", 8, 40);
    snprintf(e[5], sizeof(e[5]), "I2C%d 0x%02X", port, addr);
    snprintf(e[6], sizeof(e[6]), "Reloj %dkHz", khz);
    snprintf(e[7], sizeof(e[7]), "SDA %d SCL %d", sda, scl);
    snprintf(e[8], sizeof(e[8]), "Luz %s init %s", "no", "no");
    snprintf(e[9], sizeof(e[9]), "Celdas %dx%d", 16, 2);
    snprintf(e[10], sizeof(e[10]), "Widgets max %d", wid);
    snprintf(e[11], sizeof(e[11]), "Texto max %d", tmax);
    size_t n = 0;
    for (size_t k = 0; k < 12; k++) {
        ui_line_t l[4];
        size_t m = ui_wrap(e[k], INFO_COLS, l, 4);
        for (size_t i = 0; i < m && i < 4 && n < INFO_LINES; i++) {
            snprintf(out[n++], 32, "%.*s", (int)l[i].len, e[k] + l[i].start);
        }
    }
    return n;
}

/** @brief INFO list: 14-cell lines, every scroll position, wrap of the scroll counter. */
static void test_list_info(void)
{
    puts("list (INFO): 13 lines fit 14 cells, 12 positions, up/down per position");
    CHECK(ui_list_line_fits(14, COLS) && !ui_list_line_fits(15, COLS));
    CHECK(ui_list_line_fits(0, 3) && !ui_list_line_fits(0, 2) && !ui_list_line_fits(0, 0));

    // Defaults: 13 lines, the driver name on two of them; nothing needed a forced break.
    char l[INFO_LINES][32];
    size_t n = info_lines(l, "HD44780 + PCF8574", 0, 0x27, 100, 25, 26, 12, 64);
    CHECK(n == 13);
    CHECK(strcmp(l[1], "HD44780 +") == 0 && strcmp(l[2], "PCF8574") == 0);
    CHECK(strcmp(l[3], "LCD 16x2 5x8pt") == 0 && strcmp(l[12], "Texto max 64") == 0);
    for (size_t i = 0; i < n; i++) {
        CHECK(ui_list_line_fits(strlen(l[i]), COLS));
    }
    // Worst Kconfig values: still one line per entry, all within 14 cells.
    size_t nw = info_lines(l, "HD44780 + PCF8574", 1, 0x77, 1000, 39, 39, 64, 512);
    CHECK(nw == 13);
    for (size_t i = 0; i < nw; i++) {
        CHECK(strlen(l[i]) <= INFO_COLS);
    }

    // 13 lines in 2 rows: 12 window positions; the scroll counter wraps around them.
    CHECK(ui_list_top(0, 13, 2) == 0 && ui_list_top(11, 13, 2) == 11);
    CHECK(ui_list_top(12, 13, 2) == 0);           // s past the last window: back to the top
    CHECK(ui_list_top(720719u, 13, 2) == 11);     // w from 0 (APP_SCROLL_WRAP - 1): last window
    CHECK(ui_list_top(5, 2, 2) == 0 && ui_list_top(5, 1, 2) == 0 && ui_list_top(3, 4, 0) == 0);
    CHECK(ui_scroll_glyph_count(13, 2) == UI_SCROLL_GLYPHS);

    // Every position: row 0 up iff top > 0, row 1 down iff top < 11.
    for (size_t pos = 0; pos < 12; pos++) {
        size_t top = ui_list_top(pos, 13, 2);
        CHECK(top == pos);
        CHECK(ui_scroll_indicator(0, 2, top, 13) == (top > 0 ? UI_SCROLL_IND_UP
                                                              : UI_SCROLL_IND_NONE));
        CHECK(ui_scroll_indicator(1, 2, top, 13) == (top < 11 ? UI_SCROLL_IND_DOWN
                                                               : UI_SCROLL_IND_NONE));
    }
}

/** @brief JOINTS list: 2 joints per 14-cell line, live updates touch only changed fields. */
static void test_list_joints(void)
{
    puts("list (JOINTS): \"J1+045oJ2-012o\" = 14, 4 lines, 3 positions, live value cells");
    const char *const labels[] = {"J1", "J2", "J3", "J4", "J5", "J6"};
    const char *fmt = "%+04.0f" DEG;
    char l[4][32];
    double deg[6] = {45, -12, 10, 0, 120, 22};

    // Blank fields before the first sample: same length, so the layout never moves.
    CHECK(ui_fmt_value_row(l[0], sizeof(l[0]), labels, fmt, NULL, 2, 5) == 14);
    CHECK(strcmp(l[0], "J1     J2     ") == 0);
    for (size_t r = 0; r < 3; r++) {
        CHECK(ui_fmt_value_row(l[r], sizeof(l[r]), &labels[2 * r], fmt, &deg[2 * r], 2, 5) == 14);
        CHECK(ui_list_line_fits(strlen(l[r]), COLS));
    }
    CHECK(strcmp(l[0], "J1+045" DEG "J2-012" DEG) == 0);
    CHECK(strcmp(l[1], "J3+010" DEG "J4+000" DEG) == 0);
    CHECK(strcmp(l[2], "J5+120" DEG "J6+022" DEG) == 0);
    snprintf(l[3], sizeof(l[3]), "SIMUL. q:menu");
    CHECK(ui_list_line_fits(strlen(l[3]), COLS));

    // Range and overflow as ui_value(): -180..180 fit, 4 digits fill the field with '#'.
    double ext[2] = {-180, 1000};
    char e[32];
    CHECK(ui_fmt_value_row(e, sizeof(e), labels, fmt, ext, 2, 5) == 14);
    CHECK(strcmp(e, "J1-180" DEG "J2#####") == 0);
    // Too small a buffer or bad arguments: "".
    CHECK(ui_fmt_value_row(e, 14, labels, fmt, ext, 2, 5) == 0 && e[0] == '\0');
    CHECK(ui_fmt_value_row(e, sizeof(e), NULL, fmt, ext, 2, 5) == 0);
    CHECK(ui_fmt_value_row(e, sizeof(e), labels, fmt, ext, 2, 0) == 0);
    CHECK(ui_fmt_value_row(e, sizeof(e), labels, fmt, ext, 1, 5) == 7);  // odd joint count

    // Update path: a new sample changes only the cells of the fields that changed.
    char before[32];
    memcpy(before, l[0], sizeof(before));
    double next[2] = {46, -12};
    CHECK(ui_fmt_value_row(l[0], sizeof(l[0]), labels, fmt, next, 2, 5) == 14);
    size_t changed = 0;
    for (size_t c = 0; c < 14; c++) {
        if (before[c] != l[0][c]) {
            changed++;
            CHECK(c >= 2 && c < 7);  // inside J1's field
        }
    }
    CHECK(changed == 1);  // "+045" -> "+046": one digit, one I2C cell

    // 4 lines in 2 rows: 3 positions; w from 0 lands on the last window.
    CHECK(ui_list_top(3, 4, 2) == 0 && ui_list_top(720719u, 4, 2) == 2);
    CHECK(ui_scroll_glyph_count(4, 2) == UI_SCROLL_GLYPHS);
    const ui_scroll_ind_t N = UI_SCROLL_IND_NONE;
    const ui_scroll_ind_t U = UI_SCROLL_IND_UP;
    const ui_scroll_ind_t D = UI_SCROLL_IND_DOWN;
    const ui_scroll_ind_t R0[3] = {N, U, U};
    const ui_scroll_ind_t R1[3] = {D, D, N};
    for (size_t pos = 0; pos < 3; pos++) {
        size_t top = ui_list_top(pos, 4, 2);
        CHECK(top == pos);
        CHECK(ui_scroll_indicator(0, 2, top, 4) == R0[pos]);
        CHECK(ui_scroll_indicator(1, 2, top, 4) == R1[pos]);
    }
}

/** @brief Steps row text, and the calibration steps fit 16 cells. */
static void test_steps(void)
{
    puts("steps: \"<k> <name>\", done shows the last, calibration names fit 16");
    char out[32];
    CHECK(ui_step_line(0, 4, "Centrar joints", out, sizeof(out)) == 16);
    CHECK(strcmp(out, "1 Centrar joints") == 0);
    CHECK(ui_step_line(3, 4, "Guardar datos", out, sizeof(out)) == 15);
    CHECK(strcmp(out, "4 Guardar datos") == 0);
    CHECK(ui_step_shown(4, 4) == 3 && ui_step_shown(9, 4) == 3 && ui_step_shown(1, 4) == 1);
    CHECK(ui_step_line(4, 4, "Guardar datos", out, sizeof(out)) == 15 && out[0] == '4');

    // Same names as app_calib_step_names[].
    const char *const names[] = {"Centrar joints", "Buscar limites", "Medir offsets",
                                 "Guardar datos"};
    for (size_t i = 0; i < 4; i++) {
        CHECK(ui_step_line(i, 4, names[i], out, sizeof(out)) <= 16);
    }
    // Cut to the buffer, full length reported.
    CHECK(ui_step_line(0, 4, "Centrar joints", out, 8) == 16 && strlen(out) == 7);
}

/**
 * @brief Fills a glyph with one repeated row byte.
 *
 * @param g   Glyph.
 * @param row Byte.
 */
static void glyph_fill(ui_glyph_t g, uint8_t row)
{
    memset(g, row, UI_GLYPH_ROWS);
}

/** @brief CGRAM plan: dedup, keep held slots, define only changes, refuse a 9th glyph. */
static void test_glyph_plan(void)
{
    puts("glyphs: dedup, held slots kept, minimal defines, 9 distinct glyphs refused");
    ui_glyph_t cur[UI_CGRAM_SLOTS];
    memset(cur, 0, sizeof(cur));
    ui_glyph_t want[12];
    uint8_t slot[12];
    unsigned def = 0;
    size_t over = 99;

    CHECK(ui_glyph_plan(NULL, 0, cur, 0, NULL, &def, &over) == 0 && def == 0);

    // 3 patterns, one repeated; empty CGRAM: 3 slots, all defined, repeats share.
    glyph_fill(want[0], 0x01);
    glyph_fill(want[1], 0x02);
    glyph_fill(want[2], 0x01);
    glyph_fill(want[3], 0x03);
    CHECK(ui_glyph_plan(want, 4, cur, 0, slot, &def, &over) == 3);
    CHECK(slot[0] == slot[2] && slot[0] != slot[1] && slot[1] != slot[3] && slot[0] != slot[3]);
    CHECK(def == ((1u << slot[0]) | (1u << slot[1]) | (1u << slot[3])));

    // Only the 5 low bits count.
    glyph_fill(want[2], 0xE1);
    CHECK(ui_glyph_plan(want, 4, cur, 0, slot, &def, &over) == 3 && slot[2] == slot[0]);

    // The CGRAM already holds 0x02 in slot 6 and 0x01 in slot 5: they keep them, no define.
    glyph_fill(cur[6], 0x02);
    glyph_fill(cur[5], 0x01);
    unsigned valid = (1u << 6) | (1u << 5);
    CHECK(ui_glyph_plan(want, 4, cur, valid, slot, &def, &over) == 3);
    CHECK(slot[0] == 5 && slot[1] == 6 && slot[2] == 5);
    CHECK(def == (1u << slot[3]) && slot[3] != 5 && slot[3] != 6);
    CHECK(!(valid & def));  // the new pattern takes an unknown slot, not a held one

    // Same screen again with its own result as CGRAM: nothing to define.
    for (size_t i = 0; i < 4; i++) {
        memcpy(cur[slot[i]], want[i], sizeof(ui_glyph_t));
        valid |= 1u << slot[i];
    }
    CHECK(ui_glyph_plan(want, 4, cur, valid, slot, &def, &over) == 3 && def == 0);

    // Exactly 8 distinct fit; a 9th is refused and reported.
    for (size_t i = 0; i < 9; i++) {
        glyph_fill(want[i], (uint8_t)(0x10 + i));
    }
    CHECK(ui_glyph_plan(want, 8, cur, 0, slot, &def, &over) == 8 && def == 0xFFu);
    unsigned seen = 0;
    for (size_t i = 0; i < 8; i++) {
        seen |= 1u << slot[i];
    }
    CHECK(seen == 0xFFu);
    over = 99;
    CHECK(ui_glyph_plan(want, 9, cur, 0, slot, &def, &over) == -1 && over == 8);
    // Repeats after 8 distinct are fine.
    memcpy(want[8], want[0], sizeof(ui_glyph_t));
    CHECK(ui_glyph_plan(want, 9, cur, 0, slot, &def, &over) == 8 && slot[8] == slot[0]);

    // Full CGRAM of other glyphs: new ones overwrite held-but-unneeded slots.
    for (size_t k = 0; k < UI_CGRAM_SLOTS; k++) {
        glyph_fill(cur[k], (uint8_t)k);
    }
    glyph_fill(want[0], 0x1F);
    glyph_fill(want[1], 0x03);  // held in slot 3
    CHECK(ui_glyph_plan(want, 2, cur, 0xFFu, slot, &def, &over) == 2);
    CHECK(slot[1] == 3 && slot[0] != 3 && def == (1u << slot[0]));
}

/** @brief Glyph budget of every app screen: HOME 4 (arm), CALIBRAR/APAGAR 4 (one bar). */
static void test_glyph_budget_app(void)
{
    puts("glyphs: HOME arm (4) + bars (4) share the 8 slots; two bars share 4");
    ui_glyph_t cur[UI_CGRAM_SLOTS];
    memset(cur, 0, sizeof(cur));
    uint8_t slot[16];
    unsigned def = 0;
    size_t over = 0;

    // Arm glyphs of screens.c.
    ui_glyph_t home[4] = {
        {0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x06, 0x0C},
        {0x02, 0x05, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00},
        {0x08, 0x08, 0x08, 0x08, 0x0E, 0x0E, 0x1F, 0x1F},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x1C},
    };
    CHECK(ui_glyph_plan(home, 4, cur, 0, slot, &def, &over) == 4);
    for (size_t i = 0; i < 4; i++) {
        memcpy(cur[slot[i]], home[i], sizeof(ui_glyph_t));
    }
    unsigned valid = def;

    // CALIBRAR after HOME: the bar takes the 4 other slots; the arm stays defined.
    ui_glyph_t bar[8];
    for (unsigned l = 1; l <= UI_PROGRESS_GLYPHS; l++) {
        ui_progress_glyph(l, bar[l - 1]);
        ui_progress_glyph(l, bar[l + 3]);  // a second bar: same patterns
    }
    CHECK(ui_glyph_plan(bar, 8, cur, valid, slot, &def, &over) == 4);
    CHECK((def & valid) == 0 && slot[0] == slot[4]);
    for (size_t i = 0; i < 4; i++) {
        memcpy(cur[slot[i]], bar[i], sizeof(ui_glyph_t));
    }
    valid |= def;
    CHECK(valid == 0xFFu);

    // Back to HOME: everything is already there.
    CHECK(ui_glyph_plan(home, 4, cur, valid, slot, &def, &over) == 4 && def == 0);
    // Arm + bar on one screen: exactly 8.
    ui_glyph_t both[8];
    memcpy(both, home, sizeof(home));
    memcpy(both[4], bar, 4 * sizeof(ui_glyph_t));
    CHECK(ui_glyph_plan(both, 8, cur, valid, slot, &def, &over) == 8 && def == 0);
}

/**
 * @brief Plans one screen and applies the plan to a simulated CGRAM.
 *
 * @param want  Glyphs of the screen.
 * @param n     Number of glyphs.
 * @param cur   Simulated CGRAM (updated).
 * @param valid Valid slots (updated).
 * @return Slots written, or -1 if the screen does not fit.
 */
static int visit(const ui_glyph_t *want, size_t n, ui_glyph_t cur[UI_CGRAM_SLOTS],
                 unsigned *valid)
{
    uint8_t slot[16];
    unsigned def = 0;
    size_t over = 0;
    if (ui_glyph_plan(want, n, cur, *valid, slot, &def, &over) < 0) {
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        memcpy(cur[slot[i]], want[i], sizeof(ui_glyph_t));
    }
    *valid |= def;
    return __builtin_popcount(def);
}

/** @brief MENU glyph budget: 2 arrows, and the app's screens still fit the 8 slots. */
static void test_glyph_budget_menu(void)
{
    puts("glyphs: MENU arrows (2) fit; HOME/MENU/CALIBRAR cycle rewrites at most 4 slots");
    ui_glyph_t cur[UI_CGRAM_SLOTS];
    memset(cur, 0, sizeof(cur));
    unsigned valid = 0;

    ui_glyph_t home[4] = {
        {0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x06, 0x0C},
        {0x02, 0x05, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00},
        {0x08, 0x08, 0x08, 0x08, 0x0E, 0x0E, 0x1F, 0x1F},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x1C},
    };
    ui_glyph_t menu[UI_SCROLL_GLYPHS];  // the 5-item app menu in 2 rows can scroll
    CHECK(ui_scroll_glyph_count(5, 2) == UI_SCROLL_GLYPHS);
    ui_scroll_arrow_glyph(UI_SCROLL_IND_UP, menu[0]);
    ui_scroll_arrow_glyph(UI_SCROLL_IND_DOWN, menu[1]);
    ui_glyph_t bar[UI_PROGRESS_GLYPHS];
    for (unsigned l = 1; l <= UI_PROGRESS_GLYPHS; l++) {
        ui_progress_glyph(l, bar[l - 1]);
    }

    // The arrows are new patterns: none collides with the arm or a bar glyph.
    ui_glyph_t all[10];
    memcpy(all, home, sizeof(home));
    memcpy(all[4], bar, sizeof(bar));
    memcpy(all[8], menu, sizeof(menu));
    uint8_t slot[10];
    unsigned def = 0;
    size_t over = 0;
    CHECK(ui_glyph_plan(all, 8, cur, 0, slot, &def, &over) == 8);
    CHECK(ui_glyph_plan(all, 10, cur, 0, slot, &def, &over) == -1 && over == 8);

    // MENU alone: 2 slots on an empty CGRAM; a menu plus a bar on one screen: 6.
    CHECK(visit(menu, 2, cur, &valid) == 2);
    CHECK(ui_glyph_plan(&all[4], 6, cur, valid, slot, &def, &over) == 6);
    CHECK(visit(menu, 2, cur, &valid) == 0);  // redrawn: nothing to write
    // INFO's list uses the same two arrows: MENU -> INFO -> MENU writes nothing.
    ui_glyph_t info[UI_SCROLL_GLYPHS];
    ui_scroll_arrow_glyph(UI_SCROLL_IND_UP, info[0]);
    ui_scroll_arrow_glyph(UI_SCROLL_IND_DOWN, info[1]);
    CHECK(visit(info, 2, cur, &valid) == 0);
    // JOINTS too (it used no custom character before): MENU -> JOINTS writes nothing, and
    // JOINTS on a fresh CGRAM needs just 2 slots.
    CHECK(visit(info, 2, cur, &valid) == 0);
    ui_glyph_t fresh[UI_CGRAM_SLOTS];
    memset(fresh, 0, sizeof(fresh));
    unsigned fresh_valid = 0;
    CHECK(visit(info, 2, fresh, &fresh_valid) == 2);

    // The app's path HOME -> MENU -> CALIBRAR -> MENU -> HOME, three times: every screen
    // fits (arm 4, arrows 2, bar 4 = 10 patterns for 8 slots, so some are rewritten), and a
    // screen change never writes more slots than the screen has glyphs.
    memset(cur, 0, sizeof(cur));
    valid = 0;
    int total_last_round = 0;
    for (int round = 0; round < 3; round++) {
        int writes[5];
        writes[0] = visit(home, 4, cur, &valid);
        writes[1] = visit(menu, 2, cur, &valid);
        writes[2] = visit(bar, 4, cur, &valid);
        writes[3] = visit(menu, 2, cur, &valid);
        writes[4] = visit(home, 4, cur, &valid);
        CHECK(writes[0] >= 0 && writes[0] <= 4 && writes[1] >= 0 && writes[1] <= 2);
        CHECK(writes[2] >= 0 && writes[2] <= 4 && writes[3] >= 0 && writes[3] <= 2);
        CHECK(writes[4] >= 0 && writes[4] <= 4);
        CHECK(writes[3] == 0);  // back to MENU from CALIBRAR: the arrows are still there
        total_last_round = writes[0] + writes[1] + writes[2] + writes[3] + writes[4];
    }
    CHECK(total_last_round <= 4);  // steady state: at most 4 slot writes per round trip
    // MENU right after MENU (selection moves rebuild nothing): no write.
    CHECK(visit(menu, 2, cur, &valid) == 0);
}

/** @brief Handles: round trip, generation changes the handle, bad values rejected. */
static void test_handles(void)
{
    puts("handles: generation + index, 0 is none, stale generations differ");
    uint32_t gen = 0;
    size_t idx = 0;
    uint32_t h = ui_handle_make(5, 3);
    CHECK(h != 0 && ui_handle_split(h, &gen, &idx) && gen == 5 && idx == 3);
    CHECK(ui_handle_make(6, 3) != h);  // same slot, next screen: a different handle
    CHECK(ui_handle_make(0, 3) == 0 && ui_handle_make(0x1000000u, 0) == 0);
    CHECK(ui_handle_make(1, UI_HANDLE_MAX_INDEX + 1) == 0);
    CHECK(ui_handle_make(1, UI_HANDLE_MAX_INDEX) != 0);
    CHECK(!ui_handle_split(0, &gen, &idx));
    CHECK(!ui_handle_split(0x100u, NULL, NULL));  // generation 1, index byte 0
    CHECK(!ui_handle_split(0x7u, NULL, NULL));    // generation 0
    CHECK(ui_gen_next(0) == 1 && ui_gen_next(1) == 2 && ui_gen_next(0xFFFFFFu) == 1);
}

/**
 * @brief Runs every ui_core test.
 *
 * @return 0 if all checks pass, 1 otherwise.
 */
int main(void)
{
    test_layout_basic();
    test_layout_flex();
    test_layout_overflow();
    test_layout_app_screens();
    test_cells_put();
    test_wrap();
    test_msg_fit();
    test_fields();
    test_input_format();
    test_progress();
    test_menu_window();
    test_menu_indicators();
    test_menu_label_width();
    test_menu_arrow_glyphs();
    test_list_info();
    test_list_joints();
    test_steps();
    test_glyph_plan();
    test_glyph_budget_app();
    test_glyph_budget_menu();
    test_handles();

    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
