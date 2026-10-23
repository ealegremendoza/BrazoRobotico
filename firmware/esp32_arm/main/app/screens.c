/**
 * @file screens.c
 * @brief The menu app screens on the 16x2 LCD, declared with lcd_ui (see screens.h).
 *
 * Each screen is a builder sequence: lcd_ui stacks the widgets in rows, places the
 * ui_beside() ones on the same row, pins the footer to the bottom row and draws
 * everything in ui_screen_end(). Updates go through the stored handles. Every layout
 * below is also checked by test_host/test_ui_core.c (AB/CD = the arm, 4 custom
 * characters; > = the ROM arrow; ^/v = scroll arrows, 2 custom characters; # = progress
 * cells; o = degree sign):
 *
 *   HOME    |ABBRAZO ROBOTICO|   MENU    |>JOINTS         |   (window of 2 items)
 *           |CD Enter: menu  |           | CALIBRAR      v|
 *   JOINTS  |J1 +045o 1/6 NAV|   CTL     |J3 -012o 3/6 CTL|   (one joint, bar = angle
 *           |########        |           |    GIRAR >>    |    within the joint limits,
 *                                                              or a text: notice / hint)
 */

#include <stdarg.h>
#include <stdio.h>

#include "screens.h"
#include "ui.h"

/** Returns the error of a drawing call to the caller right away. */
#define TRY(expr)                    \
    do {                             \
        esp_err_t err_ = (expr);     \
        if (err_ != ESP_OK) {        \
            return err_;             \
        }                            \
    } while (0)

#define JOINT_FMT "%+04.0f" UI_STR_DEGREE  // "+045°": whole degrees, sign, 3 digits
#define JOINT_FIELD 5
#define JOINT_ROW_CAP (UI_SCREEN_COLS + 1)  // "J1 +045o 1/6 NAV" = 16 cells

#define CALIB_BAR_CELLS 11  // + 1 gap + "100%" = 16
#define TEXT_TITLE "MENSAJES"
#define TEXT_PLACEHOLDER "Escriba y Enter. Enter vacio:menu"  // 2 rows of 16 when wrapped

#define INFO_MAX_LINES 16  // <= 15 window positions: APP_SCROLL_WRAP divides by any of them
#define INFO_TMP_CAP 48
#define INFO_COLS (UI_SCREEN_COLS - UI_SCROLL_SIDE_COLS)  // 14: gap + scroll indicator
#define INFO_WRAP_MAX 4  // lines one INFO entry may wrap into

#define HOME_ART_W 2  // arm: 2 x 2 custom characters

/** Joint labels in esp32_arm order (servo IDs 1..6). */
static const char *const JOINT_LABELS[SCREENS_NUM_JOINTS] = {"J1", "J2", "J3", "J4", "J5", "J6"};


// The arm, 10 x 16 dots: forearm up to the gripper on the top row, upper arm and base below.
static const ui_glyph_t ARM_TOP[HOME_ART_W] = {
    {0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x06, 0x0C},
    {0x02, 0x05, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00},
};
static const ui_glyph_t ARM_BOTTOM[HOME_ART_W] = {
    {0x08, 0x08, 0x08, 0x08, 0x0E, 0x0E, 0x1F, 0x1F},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x1C},
};

// Widgets updated after the first draw. Valid until the next screens_draw(); a handle of
// another screen is rejected by lcd_ui (ESP_ERR_INVALID_STATE). UI task only.
static ui_widget_t s_menu;
static ui_widget_t s_joint_row;  // JOINTS row 0: "J1 +045o 1/6 NAV"
static ui_widget_t s_joint_bar;  // JOINTS row 1: the angle within the joint limits, or
static ui_widget_t s_joint_msg;  //   the row-1 text (notice / takeover hint)
static ui_widget_t s_steps;
static ui_widget_t s_bar;
static ui_widget_t s_percent;
static ui_widget_t s_counter;
static ui_widget_t s_input;
static ui_widget_t s_text_count;
static ui_widget_t s_info;

static joint_angles_t s_last_angles;  // shown on a new JOINTS screen right away
static bool s_have_angles;
static bool s_joint_text_layout;  // JOINTS on screen with the row-1 text instead of the bar
static bool s_text_typing;  // MENSAJES view on screen: typing (true) or message (false)
static char s_info_lines[INFO_MAX_LINES][INFO_COLS + 1];
static const char *s_info_ptrs[INFO_MAX_LINES];  // ui_list() lines: s_info_lines[i]
static size_t s_info_n;

// ---------- HOME ----------

/**
 * @brief HOME: the arm (2 x 2 custom characters), the title and "Enter: menu", or
 *        "TAREA EN CURSO" during a ROS session (DS16).
 *
 * @param s UI state.
 * @return ESP_OK, or the first error.
 */
static esp_err_t draw_home(const app_state_t *s)
{
    ui_screen_begin(NULL);
    ui_image(ARM_TOP, HOME_ART_W, 1);
    ui_beside(ui_text("BRAZO ROBOTICO", 0, UI_ALIGN_CENTER), 0);  // 14 cells: exactly fits
    ui_image(ARM_BOTTOM, HOME_ART_W, 1);
    ui_beside(ui_text(app_home_text(s), 0, UI_ALIGN_CENTER), 0);  // 14 cells at most
    return ui_screen_end();
}

// ---------- MENU ----------

/**
 * @brief MENU: a 2-row window of the entries, the selected one marked.
 *
 * @param s UI state.
 * @return ESP_OK, or the first error.
 */
static esp_err_t draw_menu(const app_state_t *s)
{
    ui_screen_begin(NULL);
    s_menu = ui_menu(app_menu_labels, APP_MENU_COUNT, s->menu_index);
    return ui_screen_end();
}

// ---------- JOINTS ----------

/**
 * @brief Angle of the joint on screen: its CTL command if commanded, else the last reading.
 *
 * @param s   UI state.
 * @param deg Output: degrees (written only on true).
 * @return false before the first reading of a joint that is not commanded.
 */
static bool joint_angle(const app_state_t *s, double *deg)
{
    unsigned j = s->joint_index;
    int cmd;
    if (app_joint_shows_command(s, &cmd)) {
        *deg = cmd;
        return true;
    }
    if (s_have_angles) {
        *deg = s_last_angles.deg[j];
        return true;
    }
    return false;
}

/**
 * @brief JOINTS row and bar or row-1 text (stored before ui_screen_end(), repainted after).
 *
 * @param s UI state.
 * @return ESP_OK, or the first error.
 */
static esp_err_t joint_fill(const app_state_t *s)
{
    unsigned j = s->joint_index;
    double deg = 0.0;
    bool known = joint_angle(s, &deg);
    char field[JOINT_FIELD + 1] = "     ";  // blank before the first sample
    if (known) {
        ui_fmt_fixed(field, sizeof(field), JOINT_FMT, deg, JOINT_FIELD);  // '#' if too long
    }
    char row[JOINT_ROW_CAP];
    snprintf(row, sizeof(row), "%s %s %u/%u %s", JOINT_LABELS[j], field, j + 1,
             (unsigned)SCREENS_NUM_JOINTS, app_joint_mode_tag(s));
    TRY(ui_text_set(s_joint_row, row));
    if (s_joint_text_layout) {
        const char *text = app_joint_row1_text(s);
        return ui_text_set(s_joint_msg, text != NULL ? text : "");
    }
    return ui_progress_set(s_joint_bar, known ? app_joint_percent(s, j, deg) : 0);
}

/**
 * @brief JOINTS: one joint per screen, its angle, position, mode, and a full-row bar.
 *
 *   NAV |J1 +045o 1/6 NAV|   pot / w,s pick the joint; Enter / short press -> CTL
 *       |##########      |   (bar: -90..+90 of J1, 16 cells x 5 steps)
 *
 * @param s UI state.
 * @return ESP_OK, or the first error.
 */
static esp_err_t draw_joints(const app_state_t *s)
{
    s_joint_text_layout = app_joint_row1_text(s) != NULL;
    ui_screen_begin(NULL);
    s_joint_row = ui_text("", 0, UI_ALIGN_LEFT);
    if (s_joint_text_layout) {
        s_joint_msg = ui_text("", 0, UI_ALIGN_CENTER);
    } else {
        s_joint_bar = ui_progress(0);
    }
    TRY(joint_fill(s));  // stored, drawn by ui_screen_end()
    return ui_screen_end();
}

/**
 * @brief Repaints JOINTS: only the changed cells, or the whole screen when row 1 switches
 *        between the bar and a text.
 *
 * @param s UI state.
 * @return ESP_OK, or the first error.
 */
static esp_err_t joint_update(const app_state_t *s)
{
    if ((app_joint_row1_text(s) != NULL) != s_joint_text_layout) {
        return draw_joints(s);
    }
    return joint_fill(s);
}

/**
 * @brief Keeps the latest sample and repaints the joint on screen (cells that changed).
 *
 * @param s      UI state.
 * @param angles Latest sample.
 * @return ESP_OK, or the first error.
 */
esp_err_t screens_draw_joint_values(const app_state_t *s, const joint_angles_t *angles)
{
    s_last_angles = *angles;
    s_have_angles = true;
    return joint_update(s);
}

// ---------- CALIBRAR (SIMULATED) ----------

/**
 * @brief Calibration progress: step row, bar and percent.
 *
 * Before ui_screen_end() this only stores the values; after it, it repaints.
 *
 * @param s UI state.
 * @return ESP_OK, or the first error.
 */
static esp_err_t calib_progress(const app_state_t *s)
{
    unsigned pct = app_calib_percent(s);
    char text[8];
    snprintf(text, sizeof(text), "%3u%%", pct);  // fixed width
    TRY(ui_steps_set(s_steps, app_calib_step(s)));
    TRY(ui_progress_set(s_bar, pct));
    return ui_text_set(s_percent, text);
}

/**
 * @brief CALIBRAR: confirm prompt, running steps + bar, or the done message.
 *
 *   confirm |CALIBRAR  q:menu|   running |2 Buscar limites|   done | CALIBRACION OK |
 *           |(SIMULADA) Enter|           |###          25%|        |(SIMULADA) Enter|
 *
 * @param s UI state.
 * @return ESP_OK, or the first error.
 */
static esp_err_t draw_calib(const app_state_t *s)
{
    if (s->calib_phase == APP_CALIB_RUNNING) {
        ui_screen_begin(NULL);
        s_steps = ui_steps(app_calib_step_names, APP_CALIB_STEPS);
        s_bar = ui_progress(CALIB_BAR_CELLS);
        s_percent = ui_beside(ui_text("", 0, UI_ALIGN_RIGHT), 1);
        TRY(calib_progress(s));  // stored, drawn by ui_screen_end()
        return ui_screen_end();
    }

    if (s->calib_phase == APP_CALIB_DONE) {
        ui_screen_begin("CALIBRACION OK");
    } else {
        ui_screen_begin(NULL);
        ui_text("CALIBRAR", 8, UI_ALIGN_LEFT);
        ui_beside(ui_text("q:menu", 0, UI_ALIGN_RIGHT), 0);  // fills the rest of the row
    }
    ui_text("(SIMULADA)", 10, UI_ALIGN_LEFT);
    ui_beside(ui_text("Enter", 0, UI_ALIGN_RIGHT), 0);
    return ui_screen_end();
}

// ---------- MENSAJES (text) ----------

/**
 * @brief Typing view: the "n/32" counter and the input line.
 *
 * @param s UI state.
 * @return ESP_OK, or the first error.
 */
static esp_err_t text_typing_update(const app_state_t *s)
{
    char count[UI_SCREEN_COLS + 1];
    snprintf(count, sizeof(count), "%u/%u", (unsigned)s->line_len, (unsigned)APP_TEXT_MAX);
    TRY(ui_text_set(s_text_count, count));
    return ui_input_set(s_input, s->line);
}

/**
 * @brief MENSAJES: the last text on both rows, or (while typing) the counter and the line.
 *
 *   message |hola mundo desde|   typing |MENSAJES    5/32|
 *           |el monitor      |          |>hola_          |
 *
 * @param s UI state.
 * @return ESP_OK, or the first error.
 */
static esp_err_t draw_text_screen(const app_state_t *s)
{
    s_text_typing = s->line_len > 0;
    ui_screen_begin(NULL);
    if (!s_text_typing) {
        ui_message_placeholder(ui_message(s->last_text), TEXT_PLACEHOLDER);
        return ui_screen_end();
    }
    ui_text(TEXT_TITLE, sizeof(TEXT_TITLE) - 1, UI_ALIGN_LEFT);
    s_text_count = ui_beside(ui_text("", 0, UI_ALIGN_RIGHT), 0);
    s_input = ui_input();
    TRY(text_typing_update(s));  // stored, drawn by ui_screen_end()
    return ui_screen_end();
}

// ---------- INFO ----------

/**
 * @brief Appends one INFO entry, word-wrapped to the list width (INFO_COLS).
 *
 * The formats below fit 14 cells for every Kconfig value; the wrap only guards longer
 * strings (the driver name is wrapped on purpose).
 *
 * @param fmt printf format.
 * @param ... Arguments.
 */
static void info_add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void info_add(const char *fmt, ...)
{
    char tmp[INFO_TMP_CAP];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    ui_line_t lines[INFO_WRAP_MAX];
    size_t n = ui_wrap(tmp, INFO_COLS, lines, INFO_WRAP_MAX);
    for (size_t i = 0; i < n && i < INFO_WRAP_MAX && s_info_n < INFO_MAX_LINES; i++) {
        snprintf(s_info_lines[s_info_n], sizeof(s_info_lines[0]), "%.*s", (int)lines[i].len,
                 tmp + lines[i].start);
        s_info_ptrs[s_info_n] = s_info_lines[s_info_n];
        s_info_n++;
    }
}

/**
 * @brief Builds the INFO lines from ui_get_display_info(); nothing is hardcoded.
 *
 * @return ESP_OK, or the ui_get_display_info() error.
 */
static esp_err_t info_build(void)
{
    ui_display_info_t info;
    TRY(ui_get_display_info(&info));
    const lcd_info_t *p = &info.panel;

    s_info_n = 0;
    info_add("INFO q:menu");  // w/s: the scroll arrows already say there is more
    info_add("%s", p->driver);  // "HD44780 +" / "PCF8574"
    info_add("LCD %dx%d %dx%dpt", p->cols, p->rows, p->char_w, p->char_h);
    info_add("ROM %s", p->rom);
    info_add("Glifos %d %dref", info.glyph_slots, info.glyph_refs);
    info_add("I2C%d 0x%02X", p->i2c_port, p->i2c_addr);
    info_add("Reloj %dkHz", p->i2c_clock_hz / 1000);
    info_add("SDA %d SCL %d", p->pin_sda, p->pin_scl);
    info_add("Luz %s init %s", p->backlight ? "si" : "no", p->initialized ? "si" : "no");
    info_add("Celdas %dx%d", info.cols, info.rows);
    info_add("Widgets max %d", info.max_widgets);
    info_add("Texto max %d", info.text_max);
    return ESP_OK;
}

/**
 * @brief INFO: the lines as a ui_list(), scrolled one line per w/s (wraps around).
 *
 *   |INFO q:menu     |   ...   |Luz si init si ^|   (^/v = scroll arrows)
 *   |HD44780 +      v|         |Celdas 16x2    v|
 *
 * @param s UI state.
 * @return ESP_OK, or the first error.
 */
static esp_err_t draw_info(const app_state_t *s)
{
    TRY(info_build());
    ui_screen_begin(NULL);
    s_info = ui_list(s_info_ptrs, s_info_n);
    TRY(ui_list_scroll(s_info, s->scroll));  // stored, drawn by ui_screen_end()
    return ui_screen_end();
}

/**
 * @brief Window positions of the INFO list.
 *
 * @return lines - rows + 1, or 1 if every line fits (or the info is unavailable).
 */
unsigned screens_info_positions(void)
{
    if (info_build() != ESP_OK || s_info_n <= UI_SCREEN_ROWS) {
        return 1;
    }
    return (unsigned)(s_info_n - UI_SCREEN_ROWS + 1);
}

// ---------- APAGAR (MOCK countdown) ----------

/**
 * @brief Countdown digit and bar (stored before ui_screen_end(), repainted after).
 *
 * @param s UI state.
 * @return ESP_OK, or the first error.
 */
static esp_err_t power_progress(const app_state_t *s)
{
    TRY(ui_counter_set(s_counter, (int)app_power_seconds_left(s)));
    return ui_progress_set(s_bar, app_power_percent(s));
}

/**
 * @brief APAGAR: "APAGANDO SISTEMA", the seconds left and a bar. MOCK: nothing is turned off.
 *
 *   |APAGANDO SISTEMA|
 *   |4 ###           |   (# = bar cells, 5 steps each)
 *
 * @param s UI state.
 * @return ESP_OK, or the first error.
 */
static esp_err_t draw_power(const app_state_t *s)
{
    ui_screen_begin("APAGANDO SISTEMA");  // 16 cells: exactly the row
    s_counter = ui_counter(1);
    s_bar = ui_beside(ui_progress(0), 1);
    TRY(power_progress(s));
    return ui_screen_end();
}

// ---------- public ----------

/**
 * @brief Builds and draws the whole current screen.
 *
 * @param s UI state.
 * @return ESP_OK, or the first error.
 */
esp_err_t screens_draw(const app_state_t *s)
{
    switch (s->screen) {
    case APP_SCREEN_HOME:
        return draw_home(s);
    case APP_SCREEN_MENU:
        return draw_menu(s);
    case APP_SCREEN_JOINTS:
        return draw_joints(s);
    case APP_SCREEN_CALIB:
        return draw_calib(s);
    case APP_SCREEN_TEXT:
        return draw_text_screen(s);
    case APP_SCREEN_INFO:
        return draw_info(s);
    case APP_SCREEN_POWER:
        return draw_power(s);
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

/**
 * @brief Partial redraws for the current screen.
 *
 * @param s       UI state.
 * @param actions APP_ACT_* flags.
 * @return ESP_OK, or the first error.
 */
esp_err_t screens_update(const app_state_t *s, uint32_t actions)
{
    switch (s->screen) {
    case APP_SCREEN_MENU:
        if (actions & APP_ACT_DRAW_MENU) {
            TRY(ui_menu_select(s_menu, s->menu_index));
        }
        break;
    case APP_SCREEN_JOINTS:
        if (actions & APP_ACT_DRAW_JOINT) {
            TRY(joint_update(s));  // same widgets: only the changed cells are sent
        }
        break;
    case APP_SCREEN_INFO:
        if (actions & APP_ACT_DRAW_SCROLL) {
            TRY(ui_list_scroll(s_info, s->scroll));
        }
        break;
    case APP_SCREEN_TEXT:
        if (actions & (APP_ACT_DRAW_TEXT | APP_ACT_DRAW_INPUT)) {
            bool typing = s->line_len > 0;
            if ((actions & APP_ACT_DRAW_TEXT) || typing != s_text_typing) {
                return draw_text_screen(s);  // the other view (or a new message)
            }
            if (typing) {
                TRY(text_typing_update(s));
            }
        }
        break;
    case APP_SCREEN_CALIB:
        if ((actions & APP_ACT_DRAW_CALIB) && s->calib_phase == APP_CALIB_RUNNING) {
            TRY(calib_progress(s));
        }
        break;
    case APP_SCREEN_POWER:
        if (actions & APP_ACT_DRAW_POWER) {
            TRY(power_progress(s));
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}
