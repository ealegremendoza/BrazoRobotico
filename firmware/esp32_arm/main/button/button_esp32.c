/**
 * @file button_esp32.c
 * @brief Interrupt-driven pushbutton adapter: level IRQ + one-shot esp_timers (see button.h).
 *
 * Concurrency: the ISR only masks the IRQ and starts the debounce timer; it never touches
 * s_core. Both timer callbacks run in the esp_timer task, one after the other, so s_core
 * needs no lock. While a debounce window is open the IRQ stays masked, so the ISR cannot
 * start the debounce timer twice; it is re-armed only at the end of the window.
 *
 * IRAM: the ISR service is installed with ESP_INTR_FLAG_IRAM, the handler is IRAM_ATTR and
 * calls only gpio_intr_disable() (IRAM with CONFIG_GPIO_CTRL_FUNC_IN_IRAM, set in
 * sdkconfig.defaults) and esp_timer_start_once() (IRAM with CONFIG_ESP_TIMER_IN_IRAM, the
 * default; it locks with portENTER_CRITICAL_SAFE, so it can run in an ISR).
 */

#include "button.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_timer.h"

static gpio_num_t s_gpio = GPIO_NUM_NC;
static uint64_t s_debounce_us;             // read by the ISR
static esp_timer_handle_t s_debounce_timer;
static esp_timer_handle_t s_long_timer;
static btn_core_t s_core;                  // esp_timer task only
static button_cb_t s_cb;
static void *s_ctx;

/**
 * @brief Button ISR: masks the level IRQ (else it re-enters while the level lasts) and opens
 *        the debounce window. No pin read, no core call, no log.
 *
 * @param arg Unused.
 */
static void IRAM_ATTR button_isr(void *arg)
{
    (void)arg;
    gpio_intr_disable(s_gpio);
    esp_timer_start_once(s_debounce_timer, s_debounce_us);
}

/**
 * @brief Arms the level IRQ for the level that means "changed" from the stable state.
 *
 * Active low: stable released (pin 1) -> low level; stable pressed (pin 0) -> high level.
 * If the pin is already at that level, the IRQ fires as soon as it is enabled.
 *
 * @param pressed Stable state in logical level.
 */
static void irq_arm(bool pressed)
{
    gpio_set_intr_type(s_gpio, pressed ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
    gpio_intr_enable(s_gpio);
}

/**
 * @brief End of the debounce window (esp_timer task): confirm the level, long timer,
 *        re-arm, deliver.
 *
 * @param arg Unused.
 */
static void debounce_expired(void *arg)
{
    (void)arg;
    int pressed = gpio_get_level(s_gpio) == 0 ? 1 : 0;  // active low
    btn_out_t out = btn_core_window_end(&s_core, pressed, (uint64_t)esp_timer_get_time());

    if (out.stop_long) {
        esp_timer_stop(s_long_timer);  // ESP_ERR_INVALID_STATE if it already fired: fine
    }
    if (out.start_long) {
        esp_timer_start_once(s_long_timer, btn_core_long_delay_us(&s_core));
    }
    irq_arm(btn_core_is_down(&s_core));
    if (out.event != BTN_EV_NONE) {
        s_cb(out.event, s_ctx);
    }
}

/**
 * @brief Long timer expired (esp_timer task): BTN_EV_LONG once if still held.
 *
 * @param arg Unused.
 */
static void long_expired(void *arg)
{
    (void)arg;
    btn_event_t ev = btn_core_long_expired(&s_core);
    if (ev != BTN_EV_NONE) {
        s_cb(ev, s_ctx);
    }
}

/**
 * @brief Configures the pin, the IRQ and the timers, and opens the first window.
 *
 * @param gpio        GPIO of the button.
 * @param debounce_ms Debounce window.
 * @param long_ms     Long-press time.
 * @param cb          Event callback.
 * @param ctx         Passed to cb.
 * @return ESP_OK, or the first error.
 */
esp_err_t button_init(int gpio, uint32_t debounce_ms, uint32_t long_ms, button_cb_t cb,
                      void *ctx)
{
    if (!GPIO_IS_VALID_GPIO(gpio) || cb == NULL || debounce_ms == 0 || long_ms <= debounce_ms) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_gpio != GPIO_NUM_NC) {
        return ESP_ERR_INVALID_STATE;
    }

    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,  // external 10 kOhm pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,     // armed by the first window
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    s_cb = cb;
    s_ctx = ctx;
    s_debounce_us = (uint64_t)debounce_ms * 1000u;
    btn_core_init(&s_core, debounce_ms * 1000u, long_ms * 1000u);

    const esp_timer_create_args_t deb_args = {
        .callback = debounce_expired,
        .name = "btn_debounce",
    };
    const esp_timer_create_args_t long_args = {
        .callback = long_expired,
        .name = "btn_long",
    };
    err = esp_timer_create(&deb_args, &s_debounce_timer);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_timer_create(&long_args, &s_long_timer);
    if (err != ESP_OK) {
        esp_timer_delete(s_debounce_timer);
        return err;
    }

    s_gpio = (gpio_num_t)gpio;
    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_OK;  // already installed by someone else
    }
    if (err == ESP_OK) {
        err = gpio_isr_handler_add(s_gpio, button_isr, NULL);  // IRQ still disabled
    }
    if (err == ESP_OK) {
        // Start-up: the RC may still be charging, so the level is not read now. The first
        // window confirms it and arms the IRQ (as pulsador_idf's probe).
        err = esp_timer_start_once(s_debounce_timer, s_debounce_us);
    }
    if (err != ESP_OK) {
        gpio_isr_handler_remove(s_gpio);
        esp_timer_delete(s_long_timer);
        esp_timer_delete(s_debounce_timer);
        s_gpio = GPIO_NUM_NC;
    }
    return err;
}
