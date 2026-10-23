#ifndef BUTTON_CORE_H
#define BUTTON_CORE_H

/**
 * @file button_core.h
 * @brief Pure pushbutton logic for an interrupt + timer driver: debounced state
 *        (pulsador_core, copied from pulsador_idf) and short / long press. No GPIO, no
 *        timers, no FreeRTOS: the adapter (button_esp32.c) owns those and calls in here.
 *
 * Adapter flow (see button.h):
 *
 *   level IRQ (armed for the opposite of the stable level)
 *     -> ISR: mask the IRQ, start the one-shot DEBOUNCE timer (no core call)
 *   debounce timer expired (the pin is quiet: bounces inside the window were never seen)
 *     -> btn_core_window_end(pin)  -> event / start or stop the LONG timer
 *     -> re-arm the IRQ for btn_core_is_down()
 *   long timer expired (started when a press was confirmed, btn_core_long_delay_us())
 *     -> btn_core_long_expired()   -> BTN_EV_LONG once, while still held
 *
 * A release before the long timer expires stops it and gives BTN_EV_SHORT; a release after
 * BTN_EV_LONG gives nothing. A press already held when the driver starts (the first window
 * is opened at start-up) is ignored until it is released.
 *
 * Both timer callbacks run in the same task (esp_timer), so the core needs no lock.
 * Tested on the PC in test_host/test_button_core.c.
 */

#include <stdbool.h>
#include <stdint.h>

#include "pulsador_core.h"

/**
 * @brief Events reported to the app.
 */
typedef enum {
    BTN_EV_NONE,   /**< Nothing to report. */
    BTN_EV_SHORT,  /**< Released before the long-press time (reported on the release). */
    BTN_EV_LONG,   /**< Held for the long-press time (reported once, while still held). */
} btn_event_t;

/**
 * @brief What the adapter must do after a debounce window.
 */
typedef struct {
    btn_event_t event;  /**< Event to deliver (BTN_EV_NONE = none). */
    bool start_long;    /**< A press was confirmed: start the long timer. */
    bool stop_long;     /**< A release was confirmed: stop the long timer. */
} btn_out_t;

/**
 * @brief State of one button.
 */
typedef struct {
    struct pul_deb deb;    // debounced state (pulsador_core): deb.state 1 = pressed
    bool armed;            // a confirmed release was seen: presses count from now on
    bool long_done;        // the current press already gave BTN_EV_LONG (or is ignored)
    uint32_t debounce_us;  // debounce window (the adapter's one-shot timer)
    uint32_t long_us;      // long-press time, from the contact
} btn_core_t;

/**
 * @brief Prepares a button: released, not armed (the first window confirms the real level).
 *
 * @param b           Button (not NULL).
 * @param debounce_us Debounce window (e.g. 30000).
 * @param long_us     Long-press time (e.g. 3000000); should be > debounce_us.
 */
void btn_core_init(btn_core_t *b, uint32_t debounce_us, uint32_t long_us);

/**
 * @brief End of a debounce window: confirms the level read from the (now quiet) pin.
 *
 * @param b       Button (not NULL, initialized).
 * @param pressed Logical level read at the end of the window: nonzero = pressed.
 * @param now_us  Time (monotonic).
 * @return The event and the long-timer actions.
 */
btn_out_t btn_core_window_end(btn_core_t *b, int pressed, uint64_t now_us);

/**
 * @brief The long timer expired.
 *
 * @param b Button (not NULL).
 * @return BTN_EV_LONG the first time for a held, counted press; else BTN_EV_NONE.
 */
btn_event_t btn_core_long_expired(btn_core_t *b);

/**
 * @brief Delay of the long timer, started when the press is confirmed (one window after
 *        the contact): long_us - debounce_us, so BTN_EV_LONG comes long_us after the contact.
 *
 * @param b Button (not NULL).
 * @return Microseconds, >= 1.
 */
uint32_t btn_core_long_delay_us(const btn_core_t *b);

/**
 * @brief Debounced level (the adapter re-arms the IRQ for its opposite).
 *
 * @param b Button (not NULL).
 * @return true while the confirmed state is pressed.
 */
bool btn_core_is_down(const btn_core_t *b);

#endif // BUTTON_CORE_H
