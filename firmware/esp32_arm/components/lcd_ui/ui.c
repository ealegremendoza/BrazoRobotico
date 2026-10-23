/**
 * @file ui.c
 * @brief lcd_ui rendering: widget pool, builder, layout, CGRAM plan and drawing (see ui.h).
 *
 * The math (layout, wrap, fields, progress cells, menu window, glyph plan, handles) is in
 * core/ui_core.c and host-tested. This file stores the widgets, renders them into a frame
 * of 16x2 character cells (s_frame) and sends it with lcd.h. Single owner task, no locks.
 *
 * Repaint: a setter re-renders its widget into the frame, compares its cells with what
 * the frame held, and writes only the rows that changed with lcd_write_text() over the
 * widget's span. The lcd driver then sends only the cells that differ from its shadow.
 * A row whose write failed is marked dirty and rewritten whole on its next repaint.
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "sdkconfig.h"

#include "lcd.h"
#include "ui.h"

#ifdef CONFIG_LCD_UI_MAX_WIDGETS
#define UI_MAX_WIDGETS CONFIG_LCD_UI_MAX_WIDGETS
#else
#define UI_MAX_WIDGETS 12
#endif

#ifdef CONFIG_LCD_UI_TEXT_MAX
#define UI_TEXT_MAX CONFIG_LCD_UI_TEXT_MAX
#else
#define UI_TEXT_MAX 64
#endif

#define COLS UI_SCREEN_COLS
#define ROWS UI_SCREEN_ROWS
#define LINE_CAP (COLS + 1)  // one row of cells plus the NUL

/** Glyph references one screen can collect: every cell an image, plus two bars. */
#define GLYPH_REFS (COLS * ROWS + 2 * UI_PROGRESS_GLYPHS)

#define STEPS_MAX 9  // "<k> " with a single digit

_Static_assert(UI_GLYPH_ROWS == LCD_GLYPH_ROWS, "ui_core and lcd disagree on glyph rows");
_Static_assert(UI_CGRAM_SLOTS == LCD_CGRAM_SLOTS, "ui_core and lcd disagree on CGRAM slots");
_Static_assert(UI_MAX_WIDGETS <= UI_HANDLE_MAX_INDEX + 1, "pool too big for the handles");

/** Returns the error of a drawing call to the caller right away. */
#define TRY(expr)                    \
    do {                             \
        esp_err_t err_ = (expr);     \
        if (err_ != ESP_OK) {        \
            return err_;             \
        }                            \
    } while (0)

static const char *TAG = "lcd_ui";

/**
 * @brief Widget kinds.
 */
typedef enum {
    K_TEXT,
    K_MESSAGE,
    K_PROGRESS,
    K_MENU,
    K_ROW,
    K_VALUE,
    K_COUNTER,
    K_STEPS,
    K_IMAGE,
    K_INPUT,
    K_LIST,
} kind_t;

/** Kind names for the logs. */
static const char *const KIND_NAMES[] = {
    [K_TEXT] = "text",       [K_MESSAGE] = "message", [K_PROGRESS] = "progress",
    [K_MENU] = "menu",       [K_ROW] = "row",         [K_VALUE] = "value",
    [K_COUNTER] = "counter", [K_STEPS] = "steps",     [K_IMAGE] = "image",
    [K_INPUT] = "input",     [K_LIST] = "list",
};

/**
 * @brief Life cycle of the current screen.
 */
typedef enum {
    PHASE_IDLE,     /**< No ui_screen_begin() yet. */
    PHASE_BUILDING, /**< Between ui_screen_begin() and ui_screen_end(). */
    PHASE_DRAWN,    /**< Laid out and drawn: setters repaint. */
    PHASE_FAILED,   /**< ui_screen_end() failed: nothing on screen belongs to it. */
} phase_t;

/**
 * @brief One widget of the pool. Fields not used by a kind stay zero.
 */
typedef struct {
    kind_t kind;                /**< What it is. */
    bool footer;                /**< Pinned to the bottom rows. */
    bool beside;                /**< On the row of the previous body widget. */
    bool has_value;             /**< Counter: set at least once. */
    unsigned gap;               /**< Cells before a beside widget. */
    ui_align_t align;           /**< Text alignment. */
    int req_w;                  /**< Requested width in cells (0 = rest of the row). */
    int req_h;                  /**< Requested height in rows (0 = flex). */
    int row;                    /**< Final first row. */
    int col;                    /**< Final first column. */
    int w;                      /**< Final width in cells. */
    int h;                      /**< Final height in rows. */
    char text[UI_TEXT_MAX + 1]; /**< Text, label, message, typed line. */
    char aux[LINE_CAP];         /**< Value field, row right side. */
    const char *fmt;            /**< Value format (caller storage). */
    const char *const *items;   /**< Menu items / step names / list lines (caller storage). */
    const char *placeholder;    /**< Message placeholder (caller storage). */
    const ui_glyph_t *glyphs;   /**< Image glyphs (caller storage). */
    size_t n;                   /**< Number of items / steps. */
    size_t sel;                 /**< Selected item / current step / list scroll position. */
    size_t top;                 /**< Menu, list: first item shown. */
    unsigned width;             /**< Value field width in characters. */
    unsigned percent;           /**< Progress percent. */
    int value;                  /**< Counter value. */
    size_t glyph_first;         /**< First entry in s_want / s_slot_of. */
    size_t glyph_count;         /**< Entries in s_want / s_slot_of. */
} widget_t;

// Everything below is owned by the UI task (see ui.h).
static widget_t s_pool[UI_MAX_WIDGETS];
static size_t s_count;
static uint32_t s_gen;
static phase_t s_phase = PHASE_IDLE;
static esp_err_t s_err;  // first builder error of the screen
static char s_title[LINE_CAP + 1];
static bool s_title_long;  // title cut at copy: does not fit the row
static char s_frame[ROWS][LINE_CAP];  // the cells of the current screen, NUL-terminated rows
static bool s_row_dirty[ROWS];        // the last write of that row failed

// CGRAM: what the panel holds (s_cgram, valid bits) and the plan of the current screen.
static ui_glyph_t s_cgram[UI_CGRAM_SLOTS];
static unsigned s_cgram_valid;
static ui_glyph_t s_want[GLYPH_REFS];
static uint8_t s_slot_of[GLYPH_REFS];
static size_t s_want_n;

// ---------- small helpers ----------

/**
 * @brief Copies a string into a fixed buffer, cutting it if needed.
 *
 * @param dst Destination.
 * @param cap Size of dst.
 * @param src Source (NULL = "").
 * @return true if it was cut.
 */
static bool copy_text(char *dst, size_t cap, const char *src)
{
    size_t len = src != NULL ? strlen(src) : 0;
    bool cut = len >= cap;
    if (cut) {
        len = cap - 1;
    }
    if (len > 0) {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
    return cut;
}

/**
 * @brief Records the first builder error of the screen and logs it.
 *
 * @param err  Error.
 * @param what Which call failed.
 */
static void build_fail(esp_err_t err, const char *what)
{
    ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err));
    if (s_err == ESP_OK) {
        s_err = err;
    }
}

/**
 * @brief Records a bad builder argument (only while building).
 *
 * @param what Which call failed.
 */
static void build_bad_arg(const char *what)
{
    if (s_phase == PHASE_BUILDING) {
        build_fail(ESP_ERR_INVALID_ARG, what);
    }
}

/**
 * @brief Takes the next pool slot for a builder call.
 *
 * @param kind  Widget kind.
 * @param req_w Requested width (0 = rest of the row).
 * @param req_h Requested height (0 = flex).
 * @param what  Builder name, for the log.
 * @param out   Output: handle of the new widget.
 * @return The zeroed widget, or NULL (error recorded).
 */
static widget_t *add(kind_t kind, int req_w, int req_h, const char *what, ui_widget_t *out)
{
    *out = UI_NONE;
    if (s_phase != PHASE_BUILDING) {
        ESP_LOGE(TAG, "%s outside ui_screen_begin()/ui_screen_end()", what);
        return NULL;  // no screen to fail: just refuse
    }
    if (s_count >= UI_MAX_WIDGETS) {
        build_fail(ESP_ERR_NO_MEM, what);
        return NULL;
    }
    widget_t *w = &s_pool[s_count];
    memset(w, 0, sizeof(*w));
    w->kind = kind;
    w->align = UI_ALIGN_LEFT;
    w->req_w = req_w;
    w->req_h = req_h;
    *out = ui_handle_make(s_gen, s_count);
    s_count++;
    return w;
}

/**
 * @brief Resolves a handle of the current screen.
 *
 * @param h   Handle.
 * @param out Output: the widget.
 * @return ESP_OK; ESP_ERR_INVALID_ARG (UI_NONE, malformed); ESP_ERR_INVALID_STATE
 *         (handle of a previous screen, or no screen).
 */
static esp_err_t lookup(ui_widget_t h, widget_t **out)
{
    uint32_t gen;
    size_t idx;
    if (!ui_handle_split(h, &gen, &idx)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_phase == PHASE_IDLE || gen != s_gen) {
        return ESP_ERR_INVALID_STATE;  // stale: its screen is gone
    }
    if (idx >= s_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = &s_pool[idx];
    return ESP_OK;
}

/**
 * @brief Resolves a handle and checks its kind.
 *
 * @param h    Handle.
 * @param kind Expected kind.
 * @param out  Output: the widget.
 * @return ESP_OK, ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_STATE.
 */
static esp_err_t lookup_kind(ui_widget_t h, kind_t kind, widget_t **out)
{
    widget_t *w;
    esp_err_t err = lookup(h, &w);
    if (err != ESP_OK) {
        return err;
    }
    if (w->kind != kind) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = w;
    return ESP_OK;
}

// ---------- rendering into the frame ----------

/**
 * @brief Writes a text into the cells of one widget row.
 *
 * @param w     Widget.
 * @param i     Row of the widget (0 = its first row).
 * @param s     Text.
 * @param len   Characters of s.
 * @param align Alignment.
 */
static void put_row(const widget_t *w, int i, const char *s, size_t len, ui_align_t align)
{
    ui_cells_put(&s_frame[w->row + i][w->col], (size_t)w->w, s, len, align);
}

/**
 * @brief Character of a progress cell of fill level 0..5.
 *
 * @param w     Progress widget (its glyph slots).
 * @param level Fill level.
 * @return ' ', the ROM full block, or the custom partial glyph.
 */
static char progress_char(const widget_t *w, uint8_t level)
{
    if (level == 0) {
        return ' ';
    }
    if (level >= UI_CELL_DOTS) {
        return UI_CHAR_FULL_BLOCK;
    }
    return LCD_CHAR_CUSTOM(s_slot_of[w->glyph_first + level - 1]);
}

/**
 * @brief Renders a message (or its placeholder) into its cells.
 *
 * @param w Message widget.
 */
static void render_message(const widget_t *w)
{
    const char *text = w->text;
    if (text[0] == '\0' && w->placeholder != NULL) {
        text = w->placeholder;
    }
    ui_line_t lines[ROWS];
    size_t n = 0;
    ui_msg_fit(text, (size_t)w->w, (size_t)w->h, lines, ROWS, &n);
    for (int i = 0; i < w->h; i++) {
        if ((size_t)i < n) {
            put_row(w, i, text + lines[i].start, lines[i].len, UI_ALIGN_LEFT);
        } else {
            put_row(w, i, "", 0, UI_ALIGN_LEFT);
        }
    }
}

/**
 * @brief Draws the scroll indicator of one row of a scrollable widget (menu, list).
 *
 * The arrows are the widget's two custom glyphs (up = its first entry in s_slot_of, down =
 * the second); a widget whose items all fit has none and never shows an indicator.
 *
 * @param w     Menu or list widget.
 * @param i     Row of the widget.
 * @param cells First cell of that row.
 */
static void put_indicator(const widget_t *w, int i, char *cells)
{
    ui_scroll_ind_t ind = ui_scroll_indicator((size_t)i, (size_t)w->h, w->top, w->n);
    if (ind != UI_SCROLL_IND_NONE && w->glyph_count == UI_SCROLL_GLYPHS) {
        size_t k = w->glyph_first + (ind == UI_SCROLL_IND_UP ? 0 : 1);
        cells[w->w - 1] = LCD_CHAR_CUSTOM(s_slot_of[k]);
    }
}

/**
 * @brief Renders the visible window of a menu: marker cell + label + gap + indicator.
 *
 * The last two cells of every row are reserved (blank gap, scroll indicator), so the
 * layout does not shift when the indicators appear.
 *
 * @param w Menu widget.
 */
static void render_menu(const widget_t *w)
{
    size_t label_w = (size_t)w->w - UI_MENU_MARKER_COLS - UI_SCROLL_SIDE_COLS;
    for (int i = 0; i < w->h; i++) {
        char *cells = &s_frame[w->row + i][w->col];
        ui_cells_put(cells, (size_t)w->w, "", 0, UI_ALIGN_LEFT);
        size_t idx = w->top + (size_t)i;
        if (idx < w->n) {
            cells[0] = idx == w->sel ? UI_CHAR_RIGHT_ARROW : ' ';
            ui_cells_put(cells + UI_MENU_MARKER_COLS, label_w, w->items[idx],
                         strlen(w->items[idx]), UI_ALIGN_LEFT);
        }
        put_indicator(w, i, cells);
    }
}

/**
 * @brief Renders the visible window of a list: line + gap + indicator per row.
 *
 * @param w List widget.
 */
static void render_list(const widget_t *w)
{
    size_t line_w = (size_t)w->w - UI_SCROLL_SIDE_COLS;
    for (int i = 0; i < w->h; i++) {
        char *cells = &s_frame[w->row + i][w->col];
        ui_cells_put(cells, (size_t)w->w, "", 0, UI_ALIGN_LEFT);
        size_t idx = w->top + (size_t)i;
        if (idx < w->n) {
            ui_cells_put(cells, line_w, w->items[idx], strlen(w->items[idx]), UI_ALIGN_LEFT);
        }
        put_indicator(w, i, cells);
    }
}

/**
 * @brief Renders a widget into the frame (only inside its cells).
 *
 * @param w Widget.
 */
static void render(const widget_t *w)
{
    char buf[LINE_CAP];
    switch (w->kind) {
    case K_TEXT:
        put_row(w, 0, w->text, strlen(w->text), w->align);
        break;
    case K_MESSAGE:
        render_message(w);
        break;
    case K_PROGRESS: {
        uint8_t levels[COLS];
        ui_progress_levels(w->percent, (size_t)w->w, levels);
        for (int c = 0; c < w->w; c++) {
            s_frame[w->row][w->col + c] = progress_char(w, levels[c]);
        }
        break;
    }
    case K_MENU:
        render_menu(w);
        break;
    case K_LIST:
        render_list(w);
        break;
    case K_ROW:
        put_row(w, 0, w->text, strlen(w->text), UI_ALIGN_LEFT);
        if (w->aux[0] != '\0') {
            size_t len = strlen(w->aux);
            ui_cells_put(&s_frame[w->row][w->col + w->w - (int)len], len, w->aux, len,
                         UI_ALIGN_LEFT);
        }
        break;
    case K_VALUE: {
        size_t label = strlen(w->text);
        put_row(w, 0, w->text, label, UI_ALIGN_LEFT);
        ui_cells_put(&s_frame[w->row][w->col + (int)label], w->width, w->aux, w->width,
                     UI_ALIGN_LEFT);
        break;
    }
    case K_COUNTER:
        if (w->has_value) {
            ui_fmt_counter(buf, sizeof(buf), w->value, (size_t)w->w);
        } else {
            buf[0] = '\0';
        }
        put_row(w, 0, buf, strlen(buf), UI_ALIGN_LEFT);
        break;
    case K_STEPS: {
        size_t len = ui_step_line(w->sel, w->n, w->items[ui_step_shown(w->sel, w->n)], buf,
                                  sizeof(buf));
        put_row(w, 0, buf, len < sizeof(buf) ? len : sizeof(buf) - 1, UI_ALIGN_LEFT);
        break;
    }
    case K_IMAGE:
        for (int r = 0; r < w->h; r++) {
            for (int c = 0; c < w->w; c++) {
                size_t k = w->glyph_first + (size_t)(r * w->w + c);
                s_frame[w->row + r][w->col + c] = LCD_CHAR_CUSTOM(s_slot_of[k]);
            }
        }
        break;
    case K_INPUT:
        ui_input_format(w->text, (size_t)w->w, buf, sizeof(buf));
        put_row(w, 0, buf, strlen(buf), UI_ALIGN_LEFT);
        break;
    default:
        break;
    }
}

/**
 * @brief Writes a whole row of the frame (lcd_write_line(): only changed cells go out).
 *
 * @param row Row.
 * @return ESP_OK, or the lcd error (then the row is dirty).
 */
static esp_err_t send_row(int row)
{
    esp_err_t err = lcd_write_line(row, s_frame[row]);
    s_row_dirty[row] = err != ESP_OK;
    return err;
}

/**
 * @brief Re-renders a widget and sends the rows of its cells that changed.
 *
 * @param w Widget.
 * @return ESP_OK, or the first lcd error.
 */
static esp_err_t repaint(const widget_t *w)
{
    char before[ROWS][LINE_CAP];
    memcpy(before, s_frame, sizeof(before));
    render(w);

    esp_err_t first = ESP_OK;
    for (int r = w->row; r < w->row + w->h; r++) {
        esp_err_t err = ESP_OK;
        if (s_row_dirty[r]) {
            err = send_row(r);  // its last write failed: resend it whole
        } else if (memcmp(&before[r][w->col], &s_frame[r][w->col], (size_t)w->w) != 0) {
            char span[LINE_CAP];
            memcpy(span, &s_frame[r][w->col], (size_t)w->w);
            span[w->w] = '\0';
            err = lcd_write_text(w->col, r, span, NULL);
            s_row_dirty[r] = err != ESP_OK;
        }
        if (first == ESP_OK) {
            first = err;
        }
    }
    return first;
}

/**
 * @brief What a setter does after storing its value: it repaints only on a drawn screen.
 *
 * @param w Widget whose value was stored.
 * @return ESP_OK (stored), ESP_ERR_INVALID_STATE after a failed layout, or the repaint
 *         result.
 */
static esp_err_t setter_done(const widget_t *w)
{
    if (s_phase == PHASE_FAILED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_phase != PHASE_DRAWN) {
        return ESP_OK;  // stored: the first draw of the screen uses it
    }
    return repaint(w);
}

/**
 * @brief Writes the planned glyphs into the CGRAM slots whose content changes.
 *
 * @param define_mask Slots to write.
 * @return ESP_OK, or the first lcd error (that slot is then unknown).
 */
static esp_err_t define_glyphs(unsigned define_mask)
{
    esp_err_t first = ESP_OK;
    for (size_t i = 0; i < s_want_n; i++) {
        unsigned slot = s_slot_of[i];
        if (!(define_mask & (1u << slot))) {
            continue;
        }
        define_mask &= ~(1u << slot);  // equal patterns share the slot: write it once
        esp_err_t err = lcd_define_char((int)slot, s_want[i]);
        if (err == ESP_OK) {
            memcpy(s_cgram[slot], s_want[i], sizeof(ui_glyph_t));
            s_cgram_valid |= 1u << slot;
        } else {
            s_cgram_valid &= ~(1u << slot);
            if (first == ESP_OK) {
                first = err;
            }
        }
    }
    return first;
}

/**
 * @brief Renders the whole frame and draws it: glyphs first, then both rows.
 *
 * @param define_mask CGRAM slots to write.
 * @return ESP_OK, or the first lcd error (every row is still attempted).
 */
static esp_err_t draw_all(unsigned define_mask)
{
    for (int r = 0; r < ROWS; r++) {
        memset(s_frame[r], ' ', COLS);
        s_frame[r][COLS] = '\0';
    }
    if (s_title[0] != '\0') {
        ui_cells_put(s_frame[0], COLS, s_title, strlen(s_title), UI_ALIGN_CENTER);
    }
    for (size_t i = 0; i < s_count; i++) {
        render(&s_pool[i]);
    }

    esp_err_t first = define_glyphs(define_mask);
    for (int r = 0; r < ROWS; r++) {
        esp_err_t err = send_row(r);
        if (first == ESP_OK) {
            first = err;
        }
    }
    return first;
}

// ---------- layout checks ----------

/**
 * @brief Checks that the fixed texts of a widget fit its final cells.
 *
 * @param w Widget (placed).
 * @return true if they fit.
 */
static bool widget_fits(const widget_t *w)
{
    char buf[LINE_CAP];
    size_t cols = (size_t)w->w;
    switch (w->kind) {
    case K_TEXT:
        return strlen(w->text) <= cols;
    case K_ROW:
        return strlen(w->text) + strlen(w->aux) <= cols;
    case K_VALUE:
        return strlen(w->text) + w->width <= cols;
    case K_MENU:
        for (size_t i = 0; i < w->n; i++) {
            if (!ui_menu_label_fits(strlen(w->items[i]), cols)) {
                ESP_LOGE(TAG, "menu item \"%s\" does not fit: %u cells minus marker and %d "
                         "reserved (gap, scroll indicator)", w->items[i], (unsigned)cols,
                         UI_SCROLL_SIDE_COLS);
                return false;
            }
        }
        return true;
    case K_LIST:
        for (size_t i = 0; i < w->n; i++) {
            if (!ui_list_line_fits(strlen(w->items[i]), cols)) {
                ESP_LOGE(TAG, "list line \"%s\" does not fit: %u cells minus %d reserved "
                         "(gap, scroll indicator)", w->items[i], (unsigned)cols,
                         UI_SCROLL_SIDE_COLS);
                return false;
            }
        }
        return true;
    case K_STEPS:
        for (size_t i = 0; i < w->n; i++) {
            if (ui_step_line(i, w->n, w->items[i], buf, sizeof(buf)) > cols) {
                return false;
            }
        }
        return true;
    case K_INPUT:
        return cols >= 2;
    default:
        return true;
    }
}

/**
 * @brief Collects the glyphs of every widget into s_want, in widget order.
 *
 * @return -1 if they all fit s_want, else the index of the first widget that overflows it.
 */
static int collect_glyphs(void)
{
    s_want_n = 0;
    for (size_t i = 0; i < s_count; i++) {
        widget_t *w = &s_pool[i];
        w->glyph_first = s_want_n;
        w->glyph_count = 0;
        size_t need = 0;
        if (w->kind == K_IMAGE) {
            need = (size_t)(w->req_w * w->req_h);
        } else if (w->kind == K_PROGRESS) {
            need = UI_PROGRESS_GLYPHS;
        } else if (w->kind == K_MENU || w->kind == K_LIST) {
            need = ui_scroll_glyph_count(w->n, (size_t)w->h);  // placed: h is final
        }
        if (s_want_n + need > GLYPH_REFS) {
            return (int)i;
        }
        for (size_t k = 0; k < need; k++) {
            if (w->kind == K_IMAGE) {
                memcpy(s_want[s_want_n + k], w->glyphs[k], sizeof(ui_glyph_t));
            } else if (w->kind == K_MENU || w->kind == K_LIST) {
                ui_scroll_arrow_glyph(k == 0 ? UI_SCROLL_IND_UP : UI_SCROLL_IND_DOWN,
                                    s_want[s_want_n + k]);
            } else {
                ui_progress_glyph((unsigned)k + 1, s_want[s_want_n + k]);
            }
        }
        w->glyph_count = need;
        s_want_n += need;
    }
    return -1;
}

/**
 * @brief Index of the widget that owns entry k of s_want.
 *
 * @param k Entry.
 * @return Widget index (s_count if none).
 */
static size_t glyph_owner(size_t k)
{
    for (size_t i = 0; i < s_count; i++) {
        if (k >= s_pool[i].glyph_first && k < s_pool[i].glyph_first + s_pool[i].glyph_count) {
            return i;
        }
    }
    return s_count;
}

/**
 * @brief Marks the screen as failed (the caller has logged why).
 *
 * @param err Error to return.
 * @return err.
 */
static esp_err_t end_fail(esp_err_t err)
{
    s_phase = PHASE_FAILED;
    return err;
}

// ---------- public: init, info ----------

/**
 * @brief Initializes the display.
 *
 * @return ESP_OK, or the lcd_init() error.
 */
esp_err_t ui_init(void)
{
    s_cgram_valid = 0;  // the CGRAM content after a power-up is unknown
    for (int r = 0; r < ROWS; r++) {
        s_row_dirty[r] = true;
    }
    return lcd_init();
}

/**
 * @brief Describes the panel and lcd_ui's limits.
 *
 * @param out Output.
 * @return ESP_OK, or ESP_ERR_INVALID_ARG.
 */
esp_err_t ui_get_display_info(ui_display_info_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    TRY(lcd_get_info(&out->panel));
    out->cols = out->panel.cols;
    out->rows = out->panel.rows;
    out->glyph_slots = out->panel.cgram_slots;
    out->glyph_refs = GLYPH_REFS;
    out->max_widgets = UI_MAX_WIDGETS;
    out->text_max = UI_TEXT_MAX;
    return ESP_OK;
}

// ---------- public: builder ----------

/**
 * @brief Starts a new screen.
 *
 * @param title Title, or NULL.
 * @return ESP_OK.
 */
esp_err_t ui_screen_begin(const char *title)
{
    s_gen = ui_gen_next(s_gen);  // every old handle becomes stale
    s_count = 0;
    s_err = ESP_OK;
    s_want_n = 0;
    s_title_long = copy_text(s_title, sizeof(s_title), title);
    s_phase = PHASE_BUILDING;
    return ESP_OK;
}

/**
 * @brief Adds a single line of text.
 *
 * @param text  Text.
 * @param width Width in cells, 0 = rest of the row.
 * @param align Alignment.
 * @return Handle, or UI_NONE.
 */
ui_widget_t ui_text(const char *text, unsigned width, ui_align_t align)
{
    ui_widget_t h;
    if (width > COLS) {
        build_bad_arg("ui_text (width)");
        return UI_NONE;
    }
    widget_t *w = add(K_TEXT, (int)width, 1, "ui_text", &h);
    if (w == NULL) {
        return UI_NONE;
    }
    copy_text(w->text, sizeof(w->text), text);
    w->align = align;
    return h;
}

/**
 * @brief Adds a word-wrapped text block (flex).
 *
 * @param text Text.
 * @return Handle, or UI_NONE.
 */
ui_widget_t ui_message(const char *text)
{
    ui_widget_t h;
    widget_t *w = add(K_MESSAGE, 0, 0, "ui_message", &h);
    if (w == NULL) {
        return UI_NONE;
    }
    copy_text(w->text, sizeof(w->text), text);
    return h;
}

/**
 * @brief Adds a progress bar.
 *
 * @param width Width in cells, 0 = rest of the row.
 * @return Handle, or UI_NONE.
 */
ui_widget_t ui_progress(unsigned width)
{
    ui_widget_t h;
    if (width > COLS) {
        build_bad_arg("ui_progress (width)");
        return UI_NONE;
    }
    widget_t *w = add(K_PROGRESS, (int)width, 1, "ui_progress", &h);
    return w != NULL ? h : UI_NONE;
}

/**
 * @brief Checks a caller-owned list of strings.
 *
 * @param list Strings.
 * @param n    Number of strings.
 * @return true if the list and every entry are non-NULL and n >= 1.
 */
static bool list_valid(const char *const *list, size_t n)
{
    if (list == NULL || n == 0) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (list[i] == NULL) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Adds a menu.
 *
 * @param items    Labels (caller storage).
 * @param n        Number of items.
 * @param selected Selected index.
 * @return Handle, or UI_NONE.
 */
ui_widget_t ui_menu(const char *const *items, size_t n, size_t selected)
{
    ui_widget_t h;
    if (!list_valid(items, n) || selected >= n) {
        build_bad_arg("ui_menu");
        return UI_NONE;
    }
    widget_t *w = add(K_MENU, 0, 0, "ui_menu", &h);
    if (w == NULL) {
        return UI_NONE;
    }
    w->items = items;
    w->n = n;
    w->sel = selected;
    return h;
}

/**
 * @brief Adds a scrolled list of read-only lines (flex).
 *
 * @param lines Lines (caller storage).
 * @param n     Number of lines.
 * @return Handle, or UI_NONE.
 */
ui_widget_t ui_list(const char *const *lines, size_t n)
{
    ui_widget_t h;
    if (!list_valid(lines, n)) {
        build_bad_arg("ui_list");
        return UI_NONE;
    }
    widget_t *w = add(K_LIST, 0, 0, "ui_list", &h);
    if (w == NULL) {
        return UI_NONE;
    }
    w->items = lines;
    w->n = n;
    return h;
}

/**
 * @brief Adds a static two-sided row.
 *
 * @param left  Left text.
 * @param right Right text, or NULL.
 * @return Handle, or UI_NONE.
 */
ui_widget_t ui_row(const char *left, const char *right)
{
    ui_widget_t h;
    widget_t *w = add(K_ROW, 0, 1, "ui_row", &h);
    if (w == NULL) {
        return UI_NONE;
    }
    copy_text(w->text, sizeof(w->text), left);
    copy_text(w->aux, sizeof(w->aux), right);
    return h;
}

/**
 * @brief Adds a value: label + fixed-width field.
 *
 * @param label Label.
 * @param fmt   printf format with one double conversion (caller storage).
 * @param width Field width.
 * @return Handle, or UI_NONE.
 */
ui_widget_t ui_value(const char *label, const char *fmt, unsigned width)
{
    ui_widget_t h;
    size_t label_len = label != NULL ? strlen(label) : 0;
    if (fmt == NULL || width < 1 || width > COLS || label_len + width > COLS) {
        build_bad_arg("ui_value");
        return UI_NONE;
    }
    widget_t *w = add(K_VALUE, (int)(label_len + width), 1, "ui_value", &h);
    if (w == NULL) {
        return UI_NONE;
    }
    copy_text(w->text, sizeof(w->text), label);
    memset(w->aux, ' ', width);  // blank field until ui_value_set()
    w->aux[width] = '\0';
    w->fmt = fmt;
    w->width = width;
    return h;
}

/**
 * @brief Adds a centered integer.
 *
 * @param width Width in cells, 0 = rest of the row.
 * @return Handle, or UI_NONE.
 */
ui_widget_t ui_counter(unsigned width)
{
    ui_widget_t h;
    if (width > COLS) {
        build_bad_arg("ui_counter (width)");
        return UI_NONE;
    }
    widget_t *w = add(K_COUNTER, (int)width, 1, "ui_counter", &h);
    return w != NULL ? h : UI_NONE;
}

/**
 * @brief Adds a steps row.
 *
 * @param names Step names (caller storage).
 * @param n     Number of steps.
 * @return Handle, or UI_NONE.
 */
ui_widget_t ui_steps(const char *const *names, size_t n)
{
    ui_widget_t h;
    if (!list_valid(names, n) || n > STEPS_MAX) {
        build_bad_arg("ui_steps");
        return UI_NONE;
    }
    widget_t *w = add(K_STEPS, 0, 1, "ui_steps", &h);
    if (w == NULL) {
        return UI_NONE;
    }
    w->items = names;
    w->n = n;
    return h;
}

/**
 * @brief Adds a picture of custom characters.
 *
 * @param glyphs w * h glyphs, row by row (caller storage).
 * @param w      Width in cells.
 * @param h      Height in rows.
 * @return Handle, or UI_NONE.
 */
ui_widget_t ui_image(const ui_glyph_t *glyphs, unsigned w, unsigned h)
{
    ui_widget_t hd;
    if (glyphs == NULL || w < 1 || w > COLS || h < 1 || h > ROWS) {
        build_bad_arg("ui_image");
        return UI_NONE;
    }
    widget_t *wd = add(K_IMAGE, (int)w, (int)h, "ui_image", &hd);
    if (wd == NULL) {
        return UI_NONE;
    }
    wd->glyphs = glyphs;
    return hd;
}

/**
 * @brief Adds the live input line.
 *
 * @return Handle, or UI_NONE.
 */
ui_widget_t ui_input(void)
{
    ui_widget_t h;
    widget_t *w = add(K_INPUT, 0, 1, "ui_input", &h);
    return w != NULL ? h : UI_NONE;
}

/**
 * @brief Adds a hint on its own bottom row.
 *
 * @param hint Hint.
 * @return ESP_OK, or the builder error.
 */
esp_err_t ui_footer(const char *hint)
{
    if (s_phase != PHASE_BUILDING) {
        ESP_LOGE(TAG, "ui_footer outside ui_screen_begin()/ui_screen_end()");
        return ESP_ERR_INVALID_STATE;
    }
    if (ui_pin_footer(ui_text(hint, 0, UI_ALIGN_LEFT)) == UI_NONE) {
        return s_err != ESP_OK ? s_err : ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Moves a one-row widget to the footer.
 *
 * @param h Widget.
 * @return h, or UI_NONE.
 */
ui_widget_t ui_pin_footer(ui_widget_t h)
{
    widget_t *w;
    if (h == UI_NONE) {
        return UI_NONE;  // the builder already recorded its error
    }
    if (s_phase != PHASE_BUILDING || lookup(h, &w) != ESP_OK) {
        build_bad_arg("ui_pin_footer (bad handle)");
        return UI_NONE;
    }
    if (w->req_h != 1 || w->beside) {
        build_fail(ESP_ERR_INVALID_ARG, "ui_pin_footer (flex, tall or beside widget)");
        return UI_NONE;
    }
    w->footer = true;
    return h;
}

/**
 * @brief Puts a widget on the row of the previous body widget.
 *
 * @param h   Widget.
 * @param gap Blank cells before it.
 * @return h, or UI_NONE.
 */
ui_widget_t ui_beside(ui_widget_t h, unsigned gap)
{
    widget_t *w;
    if (h == UI_NONE) {
        return UI_NONE;
    }
    if (s_phase != PHASE_BUILDING || lookup(h, &w) != ESP_OK) {
        build_bad_arg("ui_beside (bad handle)");
        return UI_NONE;
    }
    if (w->footer || gap > COLS) {
        build_fail(ESP_ERR_INVALID_ARG, "ui_beside (footer widget or gap)");
        return UI_NONE;
    }
    w->beside = true;
    w->gap = gap;
    return h;
}

/**
 * @brief Placeholder of an empty message.
 *
 * @param h    Message widget.
 * @param text Placeholder (caller storage).
 * @return h, or UI_NONE.
 */
ui_widget_t ui_message_placeholder(ui_widget_t h, const char *text)
{
    widget_t *w;
    if (h == UI_NONE) {
        return UI_NONE;
    }
    if (lookup_kind(h, K_MESSAGE, &w) != ESP_OK) {
        build_bad_arg("ui_message_placeholder (bad handle)");
        return UI_NONE;
    }
    w->placeholder = text;
    return h;
}

/**
 * @brief Lays out the widgets and checks that their texts fit (logs the culprit).
 *
 * @return ESP_OK, or ESP_ERR_INVALID_SIZE.
 */
static esp_err_t place_widgets(void)
{
    if (s_title_long || strlen(s_title) > COLS) {
        ESP_LOGE(TAG, "title \"%s\" is wider than %d cells", s_title, COLS);
        return ESP_ERR_INVALID_SIZE;
    }

    ui_layout_item_t items[UI_MAX_WIDGETS];
    for (size_t i = 0; i < s_count; i++) {
        const widget_t *w = &s_pool[i];
        items[i] = (ui_layout_item_t){
            .w = w->req_w,
            .h = w->req_h,
            .beside = w->beside,
            .gap = (int)w->gap,
            .footer = w->footer,
        };
    }
    const ui_layout_params_t p = {.cols = COLS, .rows = ROWS, .title = s_title[0] != '\0'};
    ui_layout_t lay;
    if (!ui_layout_compute(&p, items, s_count, &lay)) {
        int i = lay.overflow;
        ESP_LOGE(TAG, "screen \"%s\": widget #%d (%s) does not fit: row %d col %d, %dx%d cells",
                 s_title, i, KIND_NAMES[s_pool[i].kind], items[i].row, items[i].col,
                 items[i].width, items[i].height);
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < s_count; i++) {
        widget_t *w = &s_pool[i];
        w->row = items[i].row;
        w->col = items[i].col;
        w->w = items[i].width;
        w->h = items[i].height;
        if (!widget_fits(w)) {
            ESP_LOGE(TAG, "screen \"%s\": widget #%u (%s) is wider than its %d cells", s_title,
                     (unsigned)i, KIND_NAMES[w->kind], w->w);
            return ESP_ERR_INVALID_SIZE;
        }
        if (w->kind == K_MENU) {
            w->top = ui_menu_window(w->sel, w->n, (size_t)w->h, 0);
        } else if (w->kind == K_LIST) {
            w->top = ui_list_top(w->sel, w->n, (size_t)w->h);
        }
    }
    return ESP_OK;
}

/**
 * @brief Lays out, plans the CGRAM and draws the screen.
 *
 * @return ESP_OK, a builder/layout error, or the first lcd error.
 */
esp_err_t ui_screen_end(void)
{
    if (s_phase != PHASE_BUILDING) {
        ESP_LOGE(TAG, "ui_screen_end without ui_screen_begin");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_err != ESP_OK) {
        ESP_LOGE(TAG, "screen \"%s\" not drawn: %s", s_title, esp_err_to_name(s_err));
        return end_fail(s_err);
    }
    esp_err_t err = place_widgets();
    if (err != ESP_OK) {
        return end_fail(err);
    }

    int bad = collect_glyphs();
    if (bad >= 0) {
        ESP_LOGE(TAG, "screen \"%s\": widget #%d (%s) exceeds %d glyph references", s_title,
                 bad, KIND_NAMES[s_pool[bad].kind], GLYPH_REFS);
        return end_fail(ESP_ERR_INVALID_SIZE);
    }
    unsigned define_mask = 0;
    size_t overflow = 0;
    if (ui_glyph_plan(s_want, s_want_n, s_cgram, s_cgram_valid, s_slot_of, &define_mask,
                      &overflow) < 0) {
        size_t i = glyph_owner(overflow);
        ESP_LOGE(TAG, "screen \"%s\": widget #%u (%s) needs more than %d custom characters",
                 s_title, (unsigned)i, i < s_count ? KIND_NAMES[s_pool[i].kind] : "?",
                 UI_CGRAM_SLOTS);
        return end_fail(ESP_ERR_INVALID_SIZE);
    }

    s_phase = PHASE_DRAWN;
    return draw_all(define_mask);
}

// ---------- public: updates ----------

/**
 * @brief Sets a progress bar.
 *
 * @param h       Widget.
 * @param percent 0..100.
 * @return ESP_OK, an argument/state error or the lcd error.
 */
esp_err_t ui_progress_set(ui_widget_t h, unsigned percent)
{
    widget_t *w;
    TRY(lookup_kind(h, K_PROGRESS, &w));
    w->percent = percent > 100 ? 100 : percent;
    return setter_done(w);
}

/**
 * @brief Moves the menu selection.
 *
 * @param h     Widget.
 * @param index New selection.
 * @return ESP_OK, an argument/state error or the lcd error.
 */
esp_err_t ui_menu_select(ui_widget_t h, size_t index)
{
    widget_t *w;
    TRY(lookup_kind(h, K_MENU, &w));
    if (index >= w->n) {
        return ESP_ERR_INVALID_ARG;
    }
    w->sel = index;
    if (s_phase == PHASE_DRAWN) {
        w->top = ui_menu_window(index, w->n, (size_t)w->h, w->top);
    }
    return setter_done(w);
}

/**
 * @brief Scrolls a list.
 *
 * @param h   Widget.
 * @param pos Scroll position (modulo the window positions).
 * @return ESP_OK, an argument/state error or the lcd error.
 */
esp_err_t ui_list_scroll(ui_widget_t h, size_t pos)
{
    widget_t *w;
    TRY(lookup_kind(h, K_LIST, &w));
    w->sel = pos;
    if (s_phase == PHASE_DRAWN) {
        w->top = ui_list_top(pos, w->n, (size_t)w->h);
    }
    return setter_done(w);
}

/**
 * @brief Redraws a list after its caller-owned lines changed.
 *
 * @param h Widget.
 * @return ESP_OK, an argument/state error or the lcd error.
 */
esp_err_t ui_list_refresh(ui_widget_t h)
{
    widget_t *w;
    TRY(lookup_kind(h, K_LIST, &w));
    return setter_done(w);
}

/**
 * @brief Sets the number of a value.
 *
 * @param h     Widget.
 * @param value Value.
 * @return ESP_OK, an argument/state error or the lcd error.
 */
esp_err_t ui_value_set(ui_widget_t h, float value)
{
    widget_t *w;
    TRY(lookup_kind(h, K_VALUE, &w));
    ui_fmt_fixed(w->aux, sizeof(w->aux), w->fmt, value, w->width);
    return setter_done(w);
}

/**
 * @brief Sets a counter.
 *
 * @param h     Widget.
 * @param value Number.
 * @return ESP_OK, an argument/state error or the lcd error.
 */
esp_err_t ui_counter_set(ui_widget_t h, int value)
{
    widget_t *w;
    TRY(lookup_kind(h, K_COUNTER, &w));
    w->value = value;
    w->has_value = true;
    return setter_done(w);
}

/**
 * @brief Replaces the text of a ui_text().
 *
 * @param h    Widget.
 * @param text New text.
 * @return ESP_OK, an argument/state error or the lcd error.
 */
esp_err_t ui_text_set(ui_widget_t h, const char *text)
{
    widget_t *w;
    TRY(lookup_kind(h, K_TEXT, &w));
    copy_text(w->text, sizeof(w->text), text);
    return setter_done(w);
}

/**
 * @brief Replaces the text of a message.
 *
 * @param h    Widget.
 * @param text New text.
 * @return ESP_OK, an argument/state error or the lcd error.
 */
esp_err_t ui_message_set(ui_widget_t h, const char *text)
{
    widget_t *w;
    TRY(lookup_kind(h, K_MESSAGE, &w));
    copy_text(w->text, sizeof(w->text), text);
    return setter_done(w);
}

/**
 * @brief Shows the text being typed.
 *
 * @param h    Widget.
 * @param line Text being typed.
 * @return ESP_OK, an argument/state error or the lcd error.
 */
esp_err_t ui_input_set(ui_widget_t h, const char *line)
{
    widget_t *w;
    TRY(lookup_kind(h, K_INPUT, &w));
    copy_text(w->text, sizeof(w->text), line);
    return setter_done(w);
}

/**
 * @brief Moves the current step.
 *
 * @param h       Widget.
 * @param current Current step (n = all done).
 * @return ESP_OK, an argument/state error or the lcd error.
 */
esp_err_t ui_steps_set(ui_widget_t h, size_t current)
{
    widget_t *w;
    TRY(lookup_kind(h, K_STEPS, &w));
    w->sel = current > w->n ? w->n : current;
    return setter_done(w);
}
