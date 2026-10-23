/**
 * @file button_core.c
 * @brief Pure pushbutton logic for an interrupt + timer driver (see button_core.h).
 */

#include "button_core.h"

/**
 * @brief Prepares a button.
 *
 * @param b           Button (not NULL).
 * @param debounce_us Debounce window.
 * @param long_us     Long-press time.
 */
void btn_core_init(btn_core_t *b, uint32_t debounce_us, uint32_t long_us)
{
    pul_init(&b->deb, 0);  // not trusted: the first window confirms the real level
    b->armed = false;
    b->long_done = false;
    b->debounce_us = debounce_us;
    b->long_us = long_us;
}

/**
 * @brief End of a debounce window: confirms the level read from the pin.
 *
 * The adapter's one-shot timer already measured the quiet window, so the change is
 * recorded and confirmed at once (pul_edge() + pul_update() with no extra wait).
 *
 * @param b       Button (not NULL, initialized).
 * @param pressed Logical level: nonzero = pressed.
 * @param now_us  Time.
 * @return The event and the long-timer actions.
 */
btn_out_t btn_core_window_end(btn_core_t *b, int pressed, uint64_t now_us)
{
    btn_out_t out = {.event = BTN_EV_NONE, .start_long = false, .stop_long = false};
    int was = b->deb.state;

    pul_edge(&b->deb, now_us);
    int state = pul_update(&b->deb, pressed ? 1 : 0, now_us, 0);

    if (state && !was) {
        // Press confirmed. One held since start-up (no release seen yet) is ignored.
        b->long_done = !b->armed;
        out.start_long = b->armed;
    } else if (!state && was) {
        // Release confirmed: short press unless the long press already fired.
        out.stop_long = true;
        out.event = b->long_done ? BTN_EV_NONE : BTN_EV_SHORT;
        b->armed = true;
    } else if (!state) {
        b->armed = true;  // released and unchanged (start-up, or a glitch that settled back)
    }
    return out;
}

/**
 * @brief The long timer expired.
 *
 * @param b Button (not NULL).
 * @return BTN_EV_LONG once per counted press while held; else BTN_EV_NONE.
 */
btn_event_t btn_core_long_expired(btn_core_t *b)
{
    if (!b->deb.state || b->long_done) {
        return BTN_EV_NONE;  // released (late timer) or already reported
    }
    b->long_done = true;  // the release will not give a short press
    return BTN_EV_LONG;
}

/**
 * @brief Delay of the long timer after the press is confirmed.
 *
 * @param b Button (not NULL).
 * @return long_us - debounce_us, at least 1.
 */
uint32_t btn_core_long_delay_us(const btn_core_t *b)
{
    return b->long_us > b->debounce_us ? b->long_us - b->debounce_us : 1u;
}

/**
 * @brief Debounced level.
 *
 * @param b Button (not NULL).
 * @return true while the confirmed state is pressed.
 */
bool btn_core_is_down(const btn_core_t *b)
{
    return b->deb.state != 0;
}
