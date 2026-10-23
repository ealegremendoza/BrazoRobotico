#ifndef BUTTON_H
#define BUTTON_H

/**
 * @file button.h
 * @brief Interrupt-driven pushbutton adapter (ESP32): level IRQ + one-shot esp_timers,
 *        feeding the pure button_core. Same approach as pulsador_idf.
 *
 * Wiring (as pulsador_idf): external 10 kOhm pull-up to 3V3, the button to GND and 100 nF
 * across it. Active low (released = 1, pressed = 0); the pin has NO internal pull.
 *
 * - The IRQ is LEVEL-triggered and armed for the level that means "changed" from the stable
 *   state: released -> low level, pressed -> high level. Never edge-triggered: the slow RC
 *   release crosses the threshold of an input without hysteresis and gave ~2800 edge IRQs
 *   per release in pulsador_idf.
 * - ISR (IRAM): masks the IRQ and starts the one-shot debounce timer. Nothing else.
 * - Debounce timer (debounce_ms, esp_timer task): reads the pin, btn_core_window_end(),
 *   starts / stops the long timer, re-arms the IRQ for the new stable level, delivers the
 *   event. A change during the window makes the level IRQ fire as soon as it is re-armed,
 *   so nothing is lost.
 * - Long timer (long_ms - debounce_ms after the press is confirmed): btn_core_long_expired().
 *
 * Events go to the callback, called from the esp_timer task: it must not block (e.g.
 * xQueueSend() with timeout 0) and must never touch the display.
 */

#include <stdint.h>
#include "esp_err.h"

#include "button_core.h"

/**
 * @brief Receives BTN_EV_SHORT / BTN_EV_LONG. Runs in the esp_timer task: do not block.
 *
 * @param ev  Event.
 * @param ctx The ctx given to button_init().
 */
typedef void (*button_cb_t)(btn_event_t ev, void *ctx);

/**
 * @brief Configures the pin, the IRQ and the timers, and opens the first debounce window
 *        (the level at start-up is confirmed by it; the IRQ is armed afterwards).
 *
 * @param gpio        GPIO of the button (input capable).
 * @param debounce_ms Debounce window (>= 1).
 * @param long_ms     Long-press time (> debounce_ms).
 * @param cb          Event callback (not NULL).
 * @param ctx         Passed to cb.
 * @return ESP_OK; ESP_ERR_INVALID_ARG/STATE; or the gpio / esp_timer error.
 */
esp_err_t button_init(int gpio, uint32_t debounce_ms, uint32_t long_ms, button_cb_t cb,
                      void *ctx);

#endif // BUTTON_H
