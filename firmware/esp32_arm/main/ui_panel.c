/**
 * @file ui_panel.c
 * @brief Local panel tasks: console keys, pot, button and the display (see ui_panel.h).
 *
 * Adapted from pot_nav_lcd_idf/main/main.c: the keyboard reads the console UART1 instead of
 * UART0 (the PC link, D7), the simulated joints (joint_sim_task) and the MOCK servo are gone:
 * JOINTS shows the positions the bus task reads and CTL sends requests to it (DS1, DS5).
 */

#include "ui_panel.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "app_state.h"
#include "button.h"
#include "pot_core.h"
#include "pot_sensor.h"
#include "screens.h"
#include "ui.h"

// Keyboard: above the display so keystrokes are queued promptly. The console UART (UART1,
// GPIO14/27, sdkconfig.defaults): its logs and these keys share the same terminal.
#define INPUT_TASK_STACK_SIZE 3072
#define INPUT_TASK_PRIORITY 5
#define INPUT_UART CONFIG_ESP_CONSOLE_UART_NUM
#define INPUT_UART_RX_BUF 256  // driver minimum is UART_HW_FIFO_LEN (128)

// Pot: same priority as the keyboard, sampled.
#define PANEL_TASK_STACK_SIZE 3072
#define PANEL_TASK_PRIORITY 5
#define PANEL_PERIOD_MS 10  // pot sample period
#define POT_CHANNEL ADC_CHANNEL_6  // GPIO34 (ADC1 channel 6)
#define POT_ID 0

// Display: low priority (above idle). Larger stack: snprintf() with floats.
#define UI_TASK_STACK_SIZE 4096
#define UI_TASK_PRIORITY 2
#define UI_EVENT_QUEUE_LEN 16
#define JOINT_POLL_MS 50  // joints screen: how often the UI task looks at the mailbox

// CTL enter / exit must reach the bus task (a lost exit would keep rejecting S): wait a
// little for room. CTL commands do not wait: the pot sends again on its next change.
#define CTL_REQUEST_WAIT_MS 100

/** Events (keys, pot, button, bus task) from every producer to ui_task. */
static QueueHandle_t s_event_queue;

static bool s_have_pot;  // ADC ready

static const char *TAG = "PANEL";

/** Screen names for the log. */
static const char *const SCREEN_NAMES[] = {
    [APP_SCREEN_HOME] = "HOME",       [APP_SCREEN_MENU] = "MENU",
    [APP_SCREEN_JOINTS] = "JOINTS",   [APP_SCREEN_CALIB] = "CALIBRAR",
    [APP_SCREEN_TEXT] = "MENSAJES",   [APP_SCREEN_INFO] = "INFO",
    [APP_SCREEN_POWER] = "APAGAR",
};

/** JOINTS mode names for the log. */
static const char *const MODE_NAMES[] = {
    [APP_JOINT_NAV] = "NAV (pot = joint)",
    [APP_JOINT_WAIT] = "CTL requested",
    [APP_JOINT_CTL] = "CTL (pot = angle)",
};

/**
 * @brief Bus task event callback: converts it to an app_event_t and queues it.
 *
 * @param ev    Event.
 * @param joint Joint of a CTL answer.
 * @param value Event value.
 * @param ctx   Unused.
 */
void ui_panel_on_bus_event(arm_bus_event_t ev, unsigned joint, int32_t value, void *ctx)
{
    (void)joint;
    (void)ctx;
    if (s_event_queue == NULL) {
        return;
    }

    app_event_t out = {.type = APP_EV_NONE};
    switch (ev) {
    case ARM_BUS_EV_STATE:
        out = (app_event_t){.type = APP_EV_ARM_STATE, .value = value == ARM_OPERATING};
        break;
    case ARM_BUS_EV_CTL_GRANTED:
        // mrad -> whole degrees at the edge (DS5).
        out = (app_event_t){.type = APP_EV_CTL_GRANTED, .value = (int)lround(app_mrad_to_deg(value))};
        break;
    case ARM_BUS_EV_CTL_DENIED:
        out.type = APP_EV_CTL_DENIED;
        out.value = value == ARM_CTL_DENIED_SESSION    ? APP_CTL_DENIED_SESSION
                    : value == ARM_CTL_DENIED_NO_SERVO ? APP_CTL_DENIED_NO_SERVO
                                                       : APP_CTL_DENIED_OTHER;
        break;
    default:
        return;
    }
    if (xQueueSend(s_event_queue, &out, 0) != pdTRUE) {
        ESP_LOGW(TAG, "UI event queue full: bus event %d dropped", (int)ev);
    }
}

/**
 * @brief Reads keystrokes from the console UART and sends the decoded events to the UI task.
 *
 * @param arg Unused.
 */
static void input_task(void *arg)
{
    (void)arg;

    app_decoder_t dec;
    app_decoder_init(&dec);
    while (1) {
        uint8_t b;
        if (uart_read_bytes(INPUT_UART, &b, 1, portMAX_DELAY) != 1) {
            continue;
        }
        app_event_t ev;
        if (app_decode_byte(&dec, b, &ev)) {
            xQueueSend(s_event_queue, &ev, portMAX_DELAY);  // only waits if the UI is far behind
        }
    }
}

/**
 * @brief Percent of the pot from its filtered counts (calibrated mV when available).
 *
 * @param counts Filtered ADC counts.
 * @return 0..100.
 */
static int pot_percent_of(int counts)
{
    int mv = 0;
    int mv_min = 0;
    int mv_max = 0;
    if (pot_sensor_to_mv(POT_ID, counts, &mv) && pot_sensor_has_cali(POT_ID, &mv_min, &mv_max)) {
        return pot_percent_mv(mv, mv_min, mv_max);
    }
    return pot_percent(counts);
}

/**
 * @brief Samples the pot every PANEL_PERIOD_MS and sends APP_EV_POT when the percent changes.
 *
 * A percent that cannot be queued (queue full) stays pending and is retried with the newest
 * value on the next period.
 *
 * @param arg Unused.
 */
static void panel_task(void *arg)
{
    (void)arg;

    pot_avg_t avg;
    pot_change_t chg;
    pot_avg_init(&avg);
    pot_change_init(&chg, POT_HYST_COUNTS, POT_RAW_MAX);

    int sent_pct = -1;
    int new_pct = -1;
    bool read_failed = false;
    TickType_t wake = xTaskGetTickCount();
    TickType_t period = pdMS_TO_TICKS(PANEL_PERIOD_MS) > 0 ? pdMS_TO_TICKS(PANEL_PERIOD_MS) : 1;

    for (;;) {
        int raw = 0;
        esp_err_t err = pot_sensor_read(POT_ID, &raw);
        if (err != ESP_OK) {
            if (!read_failed) {  // once per failure streak
                ESP_LOGW(TAG, "pot read: %s", esp_err_to_name(err));
                read_failed = true;
            }
        } else {
            read_failed = false;
            int filtered = pot_avg_push(&avg, raw);
            if (pot_change_update(&chg, filtered)) {
                new_pct = pot_percent_of(filtered);
                ESP_LOGD(TAG, "pot %d%% raw=%d", new_pct, filtered);
            }
        }
        if (new_pct >= 0 && new_pct != sent_pct) {
            const app_event_t ev = {.type = APP_EV_POT, .value = new_pct};
            if (xQueueSend(s_event_queue, &ev, 0) == pdTRUE) {
                sent_pct = new_pct;
            }
        }
        vTaskDelayUntil(&wake, period);
    }
}

/**
 * @brief Button callback (esp_timer task): queues the event without blocking.
 *
 * Never waits and never draws: the UI task logs and handles it. With the queue full
 * (16 events behind) the press is dropped rather than stalling the esp_timer task.
 *
 * @param be  BTN_EV_SHORT or BTN_EV_LONG.
 * @param ctx Unused.
 */
static void button_event(btn_event_t be, void *ctx)
{
    (void)ctx;
    const app_event_t ev = {.type = be == BTN_EV_LONG ? APP_EV_BTN_LONG : APP_EV_BTN_SHORT};
    xQueueSend(s_event_queue, &ev, 0);
}

/**
 * @brief Logs a drawing error only when it changes (avoids flooding at 20 Hz).
 *
 * @param err      Result of the last drawing.
 * @param last_err In/out: the previous result.
 */
static void report_draw(esp_err_t err, esp_err_t *last_err)
{
    if (err == *last_err) {
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Drawing failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Drawing recovered");
    }
    *last_err = err;
}

/**
 * @brief Logs what happened in the state machine (screen and JOINTS mode changes, results).
 *
 * @param prev      Screen before the event.
 * @param prev_mode JOINTS mode before the event.
 * @param s         State after the event.
 * @param actions   Flags returned by app_handle_event().
 */
static void log_transition(app_screen_t prev, app_joint_mode_t prev_mode, const app_state_t *s,
                           uint32_t actions)
{
    if (s->screen != prev) {
        if (prev == APP_SCREEN_POWER && s->screen == APP_SCREEN_HOME &&
            s->power_ticks >= APP_POWER_TOTAL_TICKS) {
            ESP_LOGI(TAG, "Power-off countdown finished (MOCK: nothing was turned off)");
        }
        ESP_LOGI(TAG, "Screen: %s", SCREEN_NAMES[s->screen]);
    } else if (s->screen == APP_SCREEN_CALIB && (actions & APP_ACT_DRAW_SCREEN)) {
        ESP_LOGI(TAG, "Calibration (SIMULATED): %s",
                 s->calib_phase == APP_CALIB_DONE ? "done" : "started");
    } else if (s->screen == APP_SCREEN_JOINTS && s->joint_mode != prev_mode) {
        ESP_LOGI(TAG, "JOINTS J%u: %s", s->joint_index + 1, MODE_NAMES[s->joint_mode]);
    }
    if (actions & APP_ACT_DRAW_TEXT) {
        ESP_LOGI(TAG, "Message: \"%s\"", s->last_text);
    }
}

/**
 * @brief Sends the CTL requests the state machine asked for to the bus task.
 *
 * @param s       State.
 * @param actions APP_ACT_* flags.
 */
static void send_ctl_requests(const app_state_t *s, uint32_t actions)
{
    arm_bus_request_t req = {.ctl = {.joint = (uint8_t)s->joint_index}};
    if (actions & APP_ACT_CTL_EXIT) {
        req.type = ARM_BUS_REQ_CTL_EXIT;
        arm_bus_send(&req, pdMS_TO_TICKS(CTL_REQUEST_WAIT_MS));
    }
    if (actions & APP_ACT_CTL_REQUEST) {
        req.type = ARM_BUS_REQ_CTL_ENTER;
        arm_bus_send(&req, pdMS_TO_TICKS(CTL_REQUEST_WAIT_MS));
    }
    if (actions & APP_ACT_SERVO) {
        req.type = ARM_BUS_REQ_CTL_MOVE;
        req.ctl.mrad = app_deg_to_mrad(s->joint_cmd[s->joint_index]);
        arm_bus_send(&req, 0);
    }
}

/**
 * @brief Takes the newest positions and limits from the bus task's mailbox.
 *
 * Limits go to the state machine (whole degrees, rounded inward, DS6); positions to the
 * screen (degrees).
 *
 * @param s      State.
 * @param angles Output: the positions, written only on true.
 * @return true if the mailbox holds a reading.
 */
static bool sync_feedback(app_state_t *s, joint_angles_t *angles)
{
    arm_bus_feedback_t fb;
    if (!arm_bus_peek_feedback(&fb)) {
        return false;
    }
    int min_deg[APP_JOINT_COUNT];
    int max_deg[APP_JOINT_COUNT];
    app_limits_from_mrad(fb.limits.min_mrad, fb.limits.max_mrad, min_deg, max_deg);
    if (app_set_joint_limits(s, min_deg, max_deg)) {
        ESP_LOGI(TAG, "Joint limits (deg): J1 %d..%d J2 %d..%d J3 %d..%d J4 %d..%d J5 %d..%d "
                      "J6 %d..%d", min_deg[0], max_deg[0], min_deg[1], max_deg[1], min_deg[2],
                 max_deg[2], min_deg[3], max_deg[3], min_deg[4], max_deg[4], min_deg[5],
                 max_deg[5]);
    }
    if (!fb.valid) {
        return false;
    }
    for (unsigned j = 0; j < APP_JOINT_COUNT; j++) {
        angles->deg[j] = (float)app_mrad_to_deg(fb.mrad[j]);
    }
    return true;
}

/**
 * @brief Owns the display: events -> state machine -> drawing and bus requests; ticks via
 *        the queue receive timeout.
 *
 * @param arg Unused.
 */
static void ui_task(void *arg)
{
    (void)arg;

    const TickType_t tick_period = pdMS_TO_TICKS(APP_TICK_MS);
    app_state_t s;
    app_state_init(&s);
    app_set_pot_nav(&s, CONFIG_NAV_POT_STEP_PCT, CONFIG_NAV_POT_HYST_PCT);
    app_set_info_positions(&s, screens_info_positions());
    joint_angles_t angles;
    sync_feedback(&s, &angles);  // limits before the first screen
    esp_err_t last_err = ESP_OK;
    report_draw(screens_draw(&s), &last_err);

    bool ticking = false;
    TickType_t next_tick = 0;
    while (1) {
        // Sleep until an event, the next tick, or the next joints poll, whichever is first.
        TickType_t wait = portMAX_DELAY;
        if (s.screen == APP_SCREEN_JOINTS) {
            wait = pdMS_TO_TICKS(JOINT_POLL_MS);
        }
        if (ticking) {
            int32_t left = (int32_t)(next_tick - xTaskGetTickCount());
            TickType_t to_tick = left > 0 ? (TickType_t)left : 0;
            wait = to_tick < wait ? to_tick : wait;
        }

        uint32_t actions = 0;
        app_screen_t prev = s.screen;
        app_joint_mode_t prev_mode = s.joint_mode;
        app_event_t ev;
        if (xQueueReceive(s_event_queue, &ev, wait) == pdTRUE) {
            if (ev.type == APP_EV_BTN_SHORT || ev.type == APP_EV_BTN_LONG) {
                ESP_LOGI(TAG, "Button: %s press", ev.type == APP_EV_BTN_LONG ? "long" : "short");
            } else if (ev.type == APP_EV_ARM_STATE) {
                ESP_LOGI(TAG, "Arm: %s", ev.value ? "OPERATING (ROS session)" : "STOPPED");
            }
            actions |= app_handle_event(&s, &ev);
        }

        TickType_t now = xTaskGetTickCount();
        if (ticking && app_needs_tick(&s) && (int32_t)(now - next_tick) >= 0) {
            const app_event_t tick = {.type = APP_EV_TICK};
            actions |= app_handle_event(&s, &tick);
            next_tick += tick_period;
            if ((int32_t)(now - next_tick) >= 0) {
                next_tick = now + tick_period;  // fell behind: do not burst
            }
        }

        // Start the tick clock when ticking begins (calibration, countdown, notice, CTL wait).
        bool need_tick = app_needs_tick(&s);
        if (need_tick && !ticking) {
            next_tick = xTaskGetTickCount() + tick_period;
        }
        ticking = need_tick;

        send_ctl_requests(&s, actions);
        if (actions != 0) {
            log_transition(prev, prev_mode, &s, actions);
            esp_err_t err = (actions & APP_ACT_DRAW_SCREEN) ? screens_draw(&s)
                                                            : screens_update(&s, actions);
            report_draw(err, &last_err);
        }

        bool have_angles = sync_feedback(&s, &angles);
        if (s.screen == APP_SCREEN_JOINTS && have_angles) {
            report_draw(screens_draw_joint_values(&s, &angles), &last_err);
        }
    }
}

/**
 * @brief Starts the pot sensor and the button (IRQ + timers); neither is fatal (the console
 *        still works).
 */
static void panel_init(void)
{
    const adc_channel_t channel = POT_CHANNEL;
    esp_err_t err = pot_sensor_init(&channel, 1);
    if (err == ESP_OK) {
        s_have_pot = true;
        int mv_min = 0;
        int mv_max = 0;
        ESP_LOGI(TAG, "Pot: B1K on GPIO%d, 12 dB, %d-sample average, step %d%%, hysteresis "
                      "%d%%", pot_sensor_gpio(POT_ID), POT_AVG_LEN, CONFIG_NAV_POT_STEP_PCT,
                 CONFIG_NAV_POT_HYST_PCT);
        if (pot_sensor_has_cali(POT_ID, &mv_min, &mv_max)) {
            ESP_LOGI(TAG, "Pot: ADC calibration ready: 0..100 %% = %d..%d mV", mv_min, mv_max);
        } else {
            ESP_LOGW(TAG, "Pot: ADC calibration unavailable: percent from counts");
        }
    } else {
        ESP_LOGE(TAG, "Pot: ADC init failed: %s; no pot input", esp_err_to_name(err));
    }

    err = button_init(CONFIG_NAV_BUTTON_GPIO, CONFIG_NAV_BUTTON_DEBOUNCE_MS,
                      CONFIG_NAV_LONG_PRESS_MS, button_event, NULL);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Button: GPIO%d, active low, external pull-up, level IRQ, debounce %d ms, "
                      "long press %d ms", CONFIG_NAV_BUTTON_GPIO, CONFIG_NAV_BUTTON_DEBOUNCE_MS,
                 CONFIG_NAV_LONG_PRESS_MS);
    } else {
        ESP_LOGE(TAG, "Button: GPIO%d init failed: %s; no button input", CONFIG_NAV_BUTTON_GPIO,
                 esp_err_to_name(err));
    }
}

/**
 * @brief Creates the event queue, starts the display, the inputs and the panel tasks.
 *
 * @return ESP_OK, or ESP_ERR_NO_MEM.
 */
esp_err_t ui_panel_start(void)
{
    s_event_queue = xQueueCreate(UI_EVENT_QUEUE_LEN, sizeof(app_event_t));
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "Out of memory creating the event queue");
        return ESP_ERR_NO_MEM;
    }

    // Without a display everything else must keep working: log, do not abort. The
    // UI task still runs the state machine (its drawing calls fail and are logged).
    esp_err_t err = ui_init();
    ui_display_info_t info;
    if (ui_get_display_info(&info) == ESP_OK) {
        const lcd_info_t *p = &info.panel;
        ESP_LOGI(TAG, "%s %dx%d chars %dx%d dots, ROM %s, I2C%d 0x%02X %d Hz, SDA %d SCL %d; "
                      "%d CGRAM slots, %d glyph refs; %d widgets, %d chars",
                 p->driver, p->cols, p->rows, p->char_w, p->char_h, p->rom, p->i2c_port,
                 p->i2c_addr, p->i2c_clock_hz, p->pin_sda, p->pin_scl, info.glyph_slots,
                 info.glyph_refs, info.max_widgets, info.text_max);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "LCD %dx%d ready", UI_SCREEN_COLS, UI_SCREEN_ROWS);
    } else {
        ESP_LOGE(TAG, "LCD init failed: %s; running without display", esp_err_to_name(err));
    }

    // The console UART is already configured by ESP-IDF (pins, baud rate): the driver is
    // installed only to read the keystrokes; the logs keep going out as usual.
    err = uart_driver_install(INPUT_UART, INPUT_UART_RX_BUF, 0, 0, NULL, 0);
    if (err == ESP_OK) {
        xTaskCreate(input_task, "input", INPUT_TASK_STACK_SIZE, NULL, INPUT_TASK_PRIORITY, NULL);
    } else {
        ESP_LOGE(TAG, "UART%d driver install failed: %s; no keyboard input", INPUT_UART,
                 esp_err_to_name(err));
    }

    panel_init();
    if (s_have_pot) {
        xTaskCreate(panel_task, "panel", PANEL_TASK_STACK_SIZE, NULL, PANEL_TASK_PRIORITY, NULL);
    }
    xTaskCreate(ui_task, "ui", UI_TASK_STACK_SIZE, NULL, UI_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "Keys (console UART%d): w=up s=down Enter=select q=back (arrows work too). "
                  "JOINTS: Enter asks for CTL / back to NAV, w/s pick the joint (NAV) or jog it "
                  "%d deg (CTL).", INPUT_UART, APP_JOINT_JOG_DEG);
    ESP_LOGI(TAG, "Pot: moves the cursor (relative), sets the angle in CTL (after passing the "
                  "joint's angle). Button: short = Enter, long (%d ms) = HOME.",
             CONFIG_NAV_LONG_PRESS_MS);
    return ESP_OK;
}
