/**
 * @file ui_core.c
 * @brief Pure logic of lcd_ui (see ui_core.h).
 */

#include <stdio.h>
#include <string.h>

#include "ui_core.h"

#define GEN_MAX 0xFFFFFFu
#define INPUT_PROMPT '>'
#define INPUT_CURSOR '_'
#define GLYPH_DOTS_MASK 0x1F  // 5 dots per glyph row
#define FIELD_TMP_CAP 48

// ---------- layout ----------

/**
 * @brief Places the footer items, bottom rows in creation order.
 *
 * @param p        Geometry.
 * @param items    Items (only footer ones are placed).
 * @param n        Number of items.
 * @param body_top First body row (a footer row above it covers the title).
 * @param out      Output: body_bottom and overflow.
 */
static void layout_footer(const ui_layout_params_t *p, ui_layout_item_t *items, size_t n,
                          int body_top, ui_layout_t *out)
{
    int count = 0;
    for (size_t i = 0; i < n; i++) {
        if (items[i].footer) {
            count++;
        }
    }
    out->body_bottom = p->rows - count;

    int row = out->body_bottom;
    for (size_t i = 0; i < n; i++) {
        ui_layout_item_t *it = &items[i];
        if (!it->footer) {
            continue;
        }
        it->row = row++;
        it->col = 0;
        it->width = it->w > 0 ? it->w : p->cols;
        it->height = 1;
        bool bad = it->h != 1 || it->beside || it->row < body_top || it->width > p->cols;
        if (bad && out->overflow < 0) {
            out->overflow = (int)i;
        }
    }
}

/**
 * @brief Records an overflow if it comes before the one already recorded.
 *
 * @param out Layout result.
 * @param i   Index of the item that does not fit.
 */
static void mark_overflow(ui_layout_t *out, size_t i)
{
    if (out->overflow < 0 || (int)i < out->overflow) {
        out->overflow = (int)i;
    }
}

/**
 * @brief Describes the body line that starts at item i.
 *
 * @param items Items.
 * @param n     Number of items.
 * @param i     First item of the line (a body item).
 * @param fixed Output: tallest fixed item of the line (0 if none).
 * @param flex  Output: the line has a flex item.
 * @return Index right after the last item of the line.
 */
static size_t line_scan(const ui_layout_item_t *items, size_t n, size_t i, int *fixed,
                        bool *flex)
{
    *fixed = 0;
    *flex = false;
    size_t j = i;
    do {
        if (!items[j].footer) {
            if (items[j].h > 0) {
                *fixed = items[j].h > *fixed ? items[j].h : *fixed;
            } else {
                *flex = true;
            }
        }
        j++;
        while (j < n && items[j].footer) {
            j++;  // footer items do not break a line
        }
    } while (j < n && items[j].beside);
    return j;
}

/**
 * @brief Computes the cell layout of a screen (see ui_core.h for the rules).
 *
 * @param p     Geometry.
 * @param items Requests in, placement out.
 * @param n     Number of items.
 * @param out   Output: title/body rows and the overflow index.
 * @return true if everything fits.
 */
bool ui_layout_compute(const ui_layout_params_t *p, ui_layout_item_t *items, size_t n,
                       ui_layout_t *out)
{
    out->overflow = -1;
    out->title_row = p->title ? 0 : -1;
    out->body_top = p->title ? 1 : 0;
    layout_footer(p, items, n, out->body_top, out);

    // Rows left for the flex lines after the fixed lines.
    int fixed_rows = 0;
    int flex_lines = 0;
    size_t i = 0;
    while (i < n && items[i].footer) {
        i++;
    }
    while (i < n) {
        int fixed;
        bool flex;
        i = line_scan(items, n, i, &fixed, &flex);
        if (flex) {
            flex_lines++;
        } else {
            fixed_rows += fixed;
        }
    }
    int left = out->body_bottom - out->body_top - fixed_rows;
    int share = (flex_lines > 0 && left > 0) ? left / flex_lines : 0;
    int extra = (flex_lines > 0 && left > 0) ? left - share * flex_lines : 0;

    int row = out->body_top;
    int flex_seen = 0;
    bool any_body = false;
    i = 0;
    while (i < n) {
        if (items[i].footer) {
            i++;
            continue;
        }
        int fixed;
        bool flex;
        size_t end = line_scan(items, n, i, &fixed, &flex);
        int line_h = fixed;
        if (flex) {
            flex_seen++;
            line_h = share + (flex_seen == flex_lines ? extra : 0);
        }

        int col = 0;
        for (size_t j = i; j < end; j++) {
            ui_layout_item_t *it = &items[j];
            if (it->footer) {
                continue;
            }
            if (it->beside) {
                if (!any_body) {
                    mark_overflow(out, j);  // nothing to stand beside
                }
                col += it->gap > 0 ? it->gap : 0;
            }
            it->row = row;
            it->col = col;
            it->width = it->w > 0 ? it->w : p->cols - col;
            it->height = it->h > 0 ? it->h : line_h;
            if (it->width <= 0 || col + it->width > p->cols || it->height <= 0 ||
                (it->h > 0 && it->h > line_h) || row + it->height > out->body_bottom) {
                mark_overflow(out, j);
            }
            col += it->width > 0 ? it->width : 0;
            any_body = true;
        }
        row += line_h > 0 ? line_h : 1;
        i = end;
    }
    return out->overflow < 0;
}

// ---------- text ----------

/**
 * @brief Writes an aligned, cut, space-padded text into a span of cells.
 *
 * @param dst   First cell.
 * @param width Cells.
 * @param src   Text, or NULL.
 * @param len   Characters of src.
 * @param align Alignment.
 */
void ui_cells_put(char *dst, size_t width, const char *src, size_t len, ui_align_t align)
{
    if (src == NULL) {
        len = 0;
    }
    if (len > width) {
        len = width;
    }
    memset(dst, ' ', width);
    size_t slack = width - len;
    size_t at = 0;
    if (align == UI_ALIGN_CENTER) {
        at = slack / 2;
    } else if (align == UI_ALIGN_RIGHT) {
        at = slack;
    }
    if (len > 0) {
        memcpy(dst + at, src, len);
    }
}

/**
 * @brief Word-wraps a string (see ui_core.h).
 *
 * @param text      NUL-terminated string, or NULL.
 * @param cols      Characters per line.
 * @param lines     Output lines.
 * @param max_lines Capacity of lines.
 * @return Number of lines the whole text needs.
 */
size_t ui_wrap(const char *text, size_t cols, ui_line_t *lines, size_t max_lines)
{
    if (text == NULL || cols == 0) {
        return 0;
    }

    size_t n = 0;
    size_t i = 0;
    while (1) {
        while (text[i] == ' ') {
            i++;
        }
        if (text[i] == '\0') {
            break;
        }

        // Add whole words while they fit; end = exclusive end of the line content.
        size_t start = i;
        size_t end = i;
        size_t p = i;
        while (1) {
            while (text[p] == ' ') {
                p++;
            }
            if (text[p] == '\0') {
                break;
            }
            while (text[p] != '\0' && text[p] != ' ') {
                p++;
            }
            if (p - start <= cols) {
                end = p;
                continue;
            }
            if (end == start) {
                end = start + cols;  // first word longer than a line: hard break
            }
            break;
        }

        if (n < max_lines) {
            lines[n].start = start;
            lines[n].len = end - start;
        }
        n++;
        i = end;
    }
    return n;
}

/**
 * @brief Fits a message into cols x rows (see ui_core.h).
 *
 * @param text      Message.
 * @param cols      Characters per line.
 * @param rows      Lines available.
 * @param lines     Output lines.
 * @param max_lines Capacity of lines.
 * @param n_lines   Output: lines to draw.
 * @return true if nothing was dropped.
 */
bool ui_msg_fit(const char *text, size_t cols, size_t rows, ui_line_t *lines,
                size_t max_lines, size_t *n_lines)
{
    size_t cap = rows < max_lines ? rows : max_lines;
    *n_lines = 0;
    if (text == NULL || cols == 0) {
        return text == NULL || text[0] == '\0';
    }

    size_t n = ui_wrap(text, cols, lines, cap);
    if (n <= rows) {
        *n_lines = n <= cap ? n : cap;
        return n <= cap;
    }

    // Word wrap needs too many rows: plain slices of cols characters, spaces kept.
    size_t len = strlen(text);
    size_t start = 0;
    while (start < len && text[start] == ' ') {
        start++;  // leading spaces would only waste cells
    }
    size_t count = 0;
    while (start < len && count < cap) {
        size_t take = len - start < cols ? len - start : cols;
        lines[count].start = start;
        lines[count].len = take;
        count++;
        start += take;
    }
    *n_lines = count;
    return start >= len;
}

/**
 * @brief Fixed-width, right-aligned number field (see ui_core.h).
 *
 * @param out      Output buffer.
 * @param out_size Size of out.
 * @param fmt      printf format with one double conversion.
 * @param value    Value.
 * @param width    Field width.
 * @return width, or 0 on a bad argument.
 */
size_t ui_fmt_fixed(char *out, size_t out_size, const char *fmt, double value, size_t width)
{
    if (out != NULL && out_size > 0) {
        out[0] = '\0';
    }
    if (out == NULL || fmt == NULL || width == 0 || out_size < width + 1) {
        return 0;
    }

    char tmp[FIELD_TMP_CAP];
    int n = snprintf(tmp, sizeof(tmp), fmt, value);
    if (n < 0 || (size_t)n > width) {
        memset(out, '#', width);  // does not fit: visibly wrong instead of silently cut
    } else {
        ui_cells_put(out, width, tmp, (size_t)n, UI_ALIGN_RIGHT);
    }
    out[width] = '\0';
    return width;
}

/**
 * @brief Centered integer field (see ui_core.h).
 *
 * @param out      Output buffer.
 * @param out_size Size of out.
 * @param value    Value.
 * @param width    Field width.
 * @return width, or 0 on a bad argument.
 */
size_t ui_fmt_counter(char *out, size_t out_size, int value, size_t width)
{
    if (out != NULL && out_size > 0) {
        out[0] = '\0';
    }
    if (out == NULL || width == 0 || out_size < width + 1) {
        return 0;
    }

    char tmp[FIELD_TMP_CAP];
    int n = snprintf(tmp, sizeof(tmp), "%d", value);
    if (n < 0 || (size_t)n > width) {
        memset(out, '#', width);
    } else {
        ui_cells_put(out, width, tmp, (size_t)n, UI_ALIGN_CENTER);
    }
    out[width] = '\0';
    return width;
}

/**
 * @brief Label + field pairs on one line (see ui_core.h).
 *
 * @param out      Output buffer.
 * @param out_size Size of out.
 * @param labels   Labels.
 * @param fmt      printf format with one double conversion.
 * @param values   Values, or NULL.
 * @param count    Pairs.
 * @param width    Field width.
 * @return Line length, or 0.
 */
size_t ui_fmt_value_row(char *out, size_t out_size, const char *const *labels, const char *fmt,
                        const double *values, size_t count, size_t width)
{
    if (out != NULL && out_size > 0) {
        out[0] = '\0';
    }
    if (out == NULL || labels == NULL || fmt == NULL || width == 0 || width >= FIELD_TMP_CAP) {
        return 0;
    }
    size_t pos = 0;
    for (size_t i = 0; i < count; i++) {
        const char *label = labels[i] != NULL ? labels[i] : "";
        size_t len = strlen(label);
        if (pos + len + width + 1 > out_size) {
            out[0] = '\0';
            return 0;
        }
        memcpy(out + pos, label, len);
        pos += len;
        if (values != NULL) {
            char field[FIELD_TMP_CAP];
            ui_fmt_fixed(field, sizeof(field), fmt, values[i], width);
            memcpy(out + pos, field, width);
        } else {
            memset(out + pos, ' ', width);
        }
        pos += width;
    }
    out[pos] = '\0';
    return pos;
}

/**
 * @brief Live input line (see ui_core.h).
 *
 * @param line     Text being typed.
 * @param cols     Line width in characters.
 * @param out      Output buffer.
 * @param out_size Size of out.
 * @return cols, or 0 on a bad argument.
 */
size_t ui_input_format(const char *line, size_t cols, char *out, size_t out_size)
{
    if (out != NULL && out_size > 0) {
        out[0] = '\0';
    }
    if (out == NULL || cols < 2 || out_size < cols + 1) {
        return 0;
    }

    size_t visible = cols - 2;
    size_t len = line != NULL ? strlen(line) : 0;
    size_t skip = len > visible ? len - visible : 0;
    size_t shown = len - skip;

    out[0] = INPUT_PROMPT;
    if (shown > 0) {
        memcpy(out + 1, line + skip, shown);
    }
    size_t pos = 1 + shown;
    out[pos++] = INPUT_CURSOR;
    memset(out + pos, ' ', cols - pos);
    out[cols] = '\0';
    return cols;
}

// ---------- progress, menu, steps ----------

/**
 * @brief Per-cell fill levels of a progress bar (see ui_core.h).
 *
 * @param percent 0..100.
 * @param width   Cells.
 * @param levels  Output levels.
 * @return Filled steps.
 */
size_t ui_progress_levels(unsigned percent, size_t width, uint8_t *levels)
{
    if (percent > 100) {
        percent = 100;
    }
    size_t steps = width * UI_CELL_DOTS * percent / 100;
    for (size_t i = 0; i < width; i++) {
        size_t before = i * UI_CELL_DOTS;
        size_t lit = steps > before ? steps - before : 0;
        levels[i] = (uint8_t)(lit > UI_CELL_DOTS ? UI_CELL_DOTS : lit);
    }
    return steps;
}

/**
 * @brief Glyph of a partial progress cell.
 *
 * @param level Lit dot columns from the left.
 * @param out   Output glyph.
 */
void ui_progress_glyph(unsigned level, ui_glyph_t out)
{
    if (level > UI_CELL_DOTS) {
        level = UI_CELL_DOTS;
    }
    // Bit 4 is the leftmost dot: level columns = the top `level` bits of the 5.
    uint8_t row = (uint8_t)((GLYPH_DOTS_MASK << (UI_CELL_DOTS - level)) & GLYPH_DOTS_MASK);
    memset(out, row, UI_GLYPH_ROWS);
}

/**
 * @brief First item of a menu window (see ui_core.h).
 *
 * @param sel  Selection.
 * @param n    Items.
 * @param rows Window rows.
 * @param top  First item shown now.
 * @return New first item.
 */
size_t ui_menu_window(size_t sel, size_t n, size_t rows, size_t top)
{
    if (n == 0 || rows == 0) {
        return 0;
    }
    if (sel >= n) {
        sel = n - 1;
    }
    if (sel < top) {
        top = sel;
    } else if (sel >= top + rows) {
        top = sel - rows + 1;
    }
    size_t max_top = n > rows ? n - rows : 0;
    return top > max_top ? max_top : top;
}

/**
 * @brief Scroll indicator of one menu row (see ui_core.h).
 *
 * @param row  Row of the window.
 * @param rows Window rows.
 * @param top  First item shown.
 * @param n    Items.
 * @return The indicator.
 */
ui_scroll_ind_t ui_scroll_indicator(size_t row, size_t rows, size_t top, size_t n)
{
    if (rows == 0 || row >= rows) {
        return UI_SCROLL_IND_NONE;
    }
    bool above = top > 0;
    bool below = top + rows < n;
    if (row == rows - 1 && below) {
        return UI_SCROLL_IND_DOWN;  // checked first: with one row, DOWN wins
    }
    if (row == 0 && above) {
        return UI_SCROLL_IND_UP;
    }
    return UI_SCROLL_IND_NONE;
}

/**
 * @brief Menu label width check (see ui_core.h).
 *
 * @param len  Label length.
 * @param cols Row cells.
 * @return true if it fits.
 */
bool ui_menu_label_fits(size_t len, size_t cols)
{
    size_t fixed = UI_MENU_MARKER_COLS + UI_SCROLL_SIDE_COLS;
    return cols > fixed && len <= cols - fixed;
}

/**
 * @brief Custom characters of a menu (see ui_core.h).
 *
 * @param n    Items.
 * @param rows Window rows.
 * @return UI_SCROLL_GLYPHS or 0.
 */
size_t ui_scroll_glyph_count(size_t n, size_t rows)
{
    return n > rows ? UI_SCROLL_GLYPHS : 0;
}

/**
 * @brief List line width check (see ui_core.h).
 *
 * @param len  Line length.
 * @param cols Row cells.
 * @return true if it fits.
 */
bool ui_list_line_fits(size_t len, size_t cols)
{
    return cols > UI_SCROLL_SIDE_COLS && len <= cols - UI_SCROLL_SIDE_COLS;
}

/**
 * @brief First line of a list window (see ui_core.h).
 *
 * @param pos  Scroll position.
 * @param n    Lines.
 * @param rows Window rows.
 * @return First line shown.
 */
size_t ui_list_top(size_t pos, size_t n, size_t rows)
{
    if (rows == 0 || n <= rows) {
        return 0;
    }
    return pos % (n - rows + 1);
}

/**
 * @brief Arrow glyph of a scroll indicator (see ui_core.h).
 *
 * @param ind Which arrow.
 * @param out Output glyph.
 */
void ui_scroll_arrow_glyph(ui_scroll_ind_t ind, ui_glyph_t out)
{
    static const ui_glyph_t UP = {0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x04, 0x00};
    static const ui_glyph_t DOWN = {0x04, 0x04, 0x04, 0x04, 0x15, 0x0E, 0x04, 0x00};
    if (ind == UI_SCROLL_IND_UP) {
        memcpy(out, UP, sizeof(ui_glyph_t));
    } else if (ind == UI_SCROLL_IND_DOWN) {
        memcpy(out, DOWN, sizeof(ui_glyph_t));
    } else {
        memset(out, 0, sizeof(ui_glyph_t));
    }
}

/**
 * @brief Index of the step shown.
 *
 * @param current Current step.
 * @param n       Steps.
 * @return min(current, n - 1).
 */
size_t ui_step_shown(size_t current, size_t n)
{
    if (n == 0) {
        return 0;
    }
    return current < n ? current : n - 1;
}

/**
 * @brief Text of a steps row (see ui_core.h).
 *
 * @param current  Current step.
 * @param n        Steps.
 * @param name     Name of the shown step.
 * @param out      Output.
 * @param out_size Size of out.
 * @return Full length of the text.
 */
size_t ui_step_line(size_t current, size_t n, const char *name, char *out, size_t out_size)
{
    int len = snprintf(out, out_size, "%u %s", (unsigned)(ui_step_shown(current, n) + 1),
                       name != NULL ? name : "");
    return len > 0 ? (size_t)len : 0;
}

// ---------- CGRAM ----------

/**
 * @brief Compares two glyphs on their 5 visible dots per row.
 *
 * @param a Glyph.
 * @param b Glyph.
 * @return true if they look the same.
 */
static bool glyph_eq(const ui_glyph_t a, const ui_glyph_t b)
{
    for (size_t r = 0; r < UI_GLYPH_ROWS; r++) {
        if ((a[r] & GLYPH_DOTS_MASK) != (b[r] & GLYPH_DOTS_MASK)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Plans the CGRAM slots of one screen (see ui_core.h).
 *
 * @param want        Patterns.
 * @param n           Number of patterns.
 * @param cur         CGRAM contents now.
 * @param cur_valid   Valid slots of cur.
 * @param slot_of     Output: slot of every pattern.
 * @param define_mask Output: slots to write.
 * @param overflow    Output: first pattern that needs a 9th slot.
 * @return Distinct patterns, or -1.
 */
int ui_glyph_plan(const ui_glyph_t *want, size_t n, const ui_glyph_t cur[UI_CGRAM_SLOTS],
                  unsigned cur_valid, uint8_t *slot_of, unsigned *define_mask, size_t *overflow)
{
    size_t first[UI_CGRAM_SLOTS];  // index in want of each distinct pattern
    size_t distinct = 0;
    *define_mask = 0;

    // Pass 1: distinct patterns; slot_of temporarily holds the distinct id.
    for (size_t i = 0; i < n; i++) {
        size_t d = 0;
        while (d < distinct && !glyph_eq(want[first[d]], want[i])) {
            d++;
        }
        if (d == distinct) {
            if (distinct == UI_CGRAM_SLOTS) {
                *overflow = i;
                return -1;
            }
            first[distinct++] = i;
        }
        slot_of[i] = (uint8_t)d;
    }

    // Pass 2: patterns the CGRAM already holds keep their slot.
    int slot_for[UI_CGRAM_SLOTS];
    unsigned taken = 0;
    for (size_t d = 0; d < distinct; d++) {
        slot_for[d] = -1;
        for (int k = 0; k < UI_CGRAM_SLOTS; k++) {
            bool held = (cur_valid & (1u << k)) && glyph_eq(cur[k], want[first[d]]);
            if (held && !(taken & (1u << k))) {
                slot_for[d] = k;
                taken |= 1u << k;
                break;
            }
        }
    }

    // Pass 3: the rest take free slots, unknown ones first (they hold nothing reusable).
    for (size_t d = 0; d < distinct; d++) {
        if (slot_for[d] >= 0) {
            continue;
        }
        int pick = -1;
        for (int k = 0; k < UI_CGRAM_SLOTS && pick < 0; k++) {
            if (!(taken & (1u << k)) && !(cur_valid & (1u << k))) {
                pick = k;
            }
        }
        for (int k = 0; k < UI_CGRAM_SLOTS && pick < 0; k++) {
            if (!(taken & (1u << k))) {
                pick = k;
            }
        }
        slot_for[d] = pick;  // always found: distinct <= UI_CGRAM_SLOTS
        taken |= 1u << pick;
        *define_mask |= 1u << pick;
    }

    for (size_t i = 0; i < n; i++) {
        slot_of[i] = (uint8_t)slot_for[slot_of[i]];
    }
    return (int)distinct;
}

// ---------- handles ----------

/**
 * @brief Next generation, never 0.
 *
 * @param gen Current generation.
 * @return Next one.
 */
uint32_t ui_gen_next(uint32_t gen)
{
    return (gen >= GEN_MAX) ? 1u : gen + 1u;
}

/**
 * @brief Handle = generation << 8 | (index + 1); 0 is "no widget".
 *
 * @param gen Generation.
 * @param idx Pool index.
 * @return Handle, or 0 if out of range.
 */
uint32_t ui_handle_make(uint32_t gen, size_t idx)
{
    if (gen == 0 || gen > GEN_MAX || idx > UI_HANDLE_MAX_INDEX) {
        return 0;
    }
    return (gen << 8) | (uint32_t)(idx + 1);
}

/**
 * @brief Splits a handle.
 *
 * @param h   Handle.
 * @param gen Output generation (may be NULL).
 * @param idx Output index (may be NULL).
 * @return false for 0 or a malformed handle.
 */
bool ui_handle_split(uint32_t h, uint32_t *gen, size_t *idx)
{
    uint32_t g = h >> 8;
    uint32_t i = h & 0xFFu;
    if (g == 0 || i == 0) {
        return false;
    }
    if (gen != NULL) {
        *gen = g;
    }
    if (idx != NULL) {
        *idx = i - 1;
    }
    return true;
}
