/**
 * @file test_button_core.c
 * @brief Host (PC) tests for main/button/button_core.c (and the pulsador_core it uses). No
 *        ESP-IDF needed.
 *
 * The rig plays the adapter of button_esp32.c: a level IRQ that is masked during a debounce
 * window, a one-shot debounce timer that reads the pin when it expires and re-arms the IRQ,
 * and a one-shot long timer started / stopped as the core asks.
 *
 * Build and run from pot_nav_lcd_idf:
 *
 *   gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main/button \
 *       test_host/test_button_core.c main/button/button_core.c main/button/pulsador_core.c \
 *       -o /tmp/test_button_core && /tmp/test_button_core
 */

#include <stdbool.h>
#include <stdio.h>

#include "button_core.h"

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                   \
        }                                                                 \
    } while (0)

#define STEP_US 1000u       // simulation resolution: 1 ms
#define DEBOUNCE_US 30000u  // 30 ms
#define LONG_US 3000000u    // 3 s

/** Simulated adapter: pin, IRQ, timers and event counters. */
typedef struct {
    btn_core_t b;
    uint64_t now;
    int pin;               // logical level now (1 = pressed)
    bool irq_armed;        // level IRQ enabled
    bool deb_running;      // debounce timer armed
    uint64_t deb_at;
    bool long_running;     // long timer armed
    uint64_t long_at;
    int irqs;              // ISR entries
    int shorts;
    int longs;
    uint64_t long_fired_at;
} rig_t;

/**
 * @brief Counts an event.
 *
 * @param r  Rig.
 * @param ev Event.
 */
static void deliver(rig_t *r, btn_event_t ev)
{
    if (ev == BTN_EV_SHORT) {
        r->shorts++;
    } else if (ev == BTN_EV_LONG) {
        r->longs++;
        r->long_fired_at = r->now;
    }
}

/**
 * @brief Start-up as button_init(): IRQ disabled, first debounce window opened.
 *
 * @param r       Rig.
 * @param pin     Level at start-up.
 */
static void rig_init(rig_t *r, int pin)
{
    btn_core_init(&r->b, DEBOUNCE_US, LONG_US);
    r->now = 1000000;
    r->pin = pin;
    r->irq_armed = false;
    r->deb_running = true;
    r->deb_at = r->now + DEBOUNCE_US;
    r->long_running = false;
    r->long_at = 0;
    r->irqs = 0;
    r->shorts = 0;
    r->longs = 0;
    r->long_fired_at = 0;
}

/**
 * @brief Advances 1 ms: level IRQ (armed for the opposite of the stable level), then the
 *        timers, as the adapter does.
 *
 * @param r Rig.
 */
static void tick(rig_t *r)
{
    r->now += STEP_US;

    // Level IRQ: fires while the pin differs from the stable state; ISR masks + starts timer.
    if (r->irq_armed && r->pin != (int)btn_core_is_down(&r->b)) {
        r->irqs++;
        r->irq_armed = false;
        r->deb_running = true;
        r->deb_at = r->now + DEBOUNCE_US;
    }
    if (r->deb_running && r->now >= r->deb_at) {
        r->deb_running = false;
        btn_out_t out = btn_core_window_end(&r->b, r->pin, r->now);
        if (out.stop_long) {
            r->long_running = false;
        }
        if (out.start_long) {
            r->long_running = true;
            r->long_at = r->now + btn_core_long_delay_us(&r->b);
        }
        r->irq_armed = true;  // re-armed for btn_core_is_down()
        deliver(r, out.event);
    }
    if (r->long_running && r->now >= r->long_at) {
        r->long_running = false;
        deliver(r, btn_core_long_expired(&r->b));
    }
}

/**
 * @brief Holds the pin at a level for a while.
 *
 * @param r   Rig.
 * @param pin Level.
 * @param us  Duration (multiple of STEP_US).
 */
static void hold(rig_t *r, int pin, uint64_t us)
{
    r->pin = pin;
    for (uint64_t t = 0; t < us; t += STEP_US) {
        tick(r);
    }
}

/**
 * @brief Bounces: alternates the level every millisecond.
 *
 * @param r     Rig.
 * @param ms    Duration.
 * @param first First level.
 */
static void bounce(rig_t *r, int ms, int first)
{
    for (int i = 0; i < ms; i++) {
        hold(r, (first + i) % 2, STEP_US);
    }
}

/** @brief A short press: one event on the release; one IRQ per change. */
static void test_short_press(void)
{
    puts("short: one event on release, one IRQ per change, long timer stopped");
    rig_t r;
    rig_init(&r, 0);
    hold(&r, 0, 100000);  // first window confirms released, arms the IRQ
    CHECK(r.irq_armed && !btn_core_is_down(&r.b) && r.irqs == 0);

    hold(&r, 1, 200000);
    CHECK(btn_core_is_down(&r.b) && r.long_running && r.shorts == 0 && r.irqs == 1);
    hold(&r, 0, 20000);  // inside the release window
    CHECK(r.shorts == 0);
    hold(&r, 0, 20000);
    CHECK(r.shorts == 1 && !r.long_running && r.irqs == 2);
    hold(&r, 0, 4000000);  // the stopped long timer never fires
    CHECK(r.shorts == 1 && r.longs == 0);

    hold(&r, 1, 100000);
    hold(&r, 0, 100000);
    CHECK(r.shorts == 2 && r.longs == 0);
}

/** @brief Bounces inside the window are never seen: the IRQ is masked. */
static void test_bounce(void)
{
    puts("bounce: bounces in the window are ignored (IRQ masked), one press per press");
    rig_t r;
    rig_init(&r, 0);
    hold(&r, 0, 100000);

    bounce(&r, 9, 1);     // contact bounce, ends pressed (1,0,...,1)
    hold(&r, 1, 300000);
    CHECK(btn_core_is_down(&r.b) && r.irqs == 1);  // a single IRQ for the whole bounce
    bounce(&r, 15, 0);    // slow RC release crossing the threshold back and forth
    hold(&r, 0, 100000);
    CHECK(r.shorts == 1 && r.longs == 0 && r.irqs == 2);

    // A glitch that settles back before the window ends: no event, IRQ re-armed.
    hold(&r, 1, 5000);
    hold(&r, 0, 100000);
    CHECK(r.shorts == 1 && r.longs == 0 && r.irq_armed && !btn_core_is_down(&r.b));

    // Still pressed when a window ends after a release bounce: the IRQ fires again at once
    // (pin at the armed level), so the release is not lost.
    hold(&r, 1, 100000);
    hold(&r, 0, 2000);
    hold(&r, 1, 40000);   // window ends with the pin pressed again: no change
    CHECK(r.shorts == 1 && btn_core_is_down(&r.b));
    hold(&r, 0, 100000);
    CHECK(r.shorts == 2);
}

/** @brief Long press: once, 3 s after the contact, while held; no short afterwards. */
static void test_long_press(void)
{
    puts("long: fires once at 3 s while held, release gives no short press");
    rig_t r;
    rig_init(&r, 0);
    hold(&r, 0, 100000);

    uint64_t contact = r.now + STEP_US;  // the IRQ sees the press on the next tick
    hold(&r, 1, LONG_US - 10000);
    CHECK(r.longs == 0 && r.shorts == 0);
    hold(&r, 1, 20000);
    CHECK(r.longs == 1);
    CHECK(r.long_fired_at - contact == LONG_US);
    hold(&r, 1, 5000000);
    CHECK(r.longs == 1 && r.shorts == 0);
    bounce(&r, 6, 0);
    hold(&r, 0, 200000);
    CHECK(r.longs == 1 && r.shorts == 0);

    hold(&r, 1, 100000);  // the next press is a normal short press
    hold(&r, 0, 100000);
    CHECK(r.shorts == 1 && r.longs == 1);

    // A late long timer (released meanwhile) reports nothing.
    hold(&r, 1, 100000);
    hold(&r, 0, 100000);
    CHECK(btn_core_long_expired(&r.b) == BTN_EV_NONE);
    CHECK(btn_core_long_delay_us(&r.b) == LONG_US - DEBOUNCE_US);
}

/** @brief Held at start-up: ignored (no long, no short) until released. */
static void test_held_at_start(void)
{
    puts("start-up: a press held at start is ignored until released");
    rig_t r;
    rig_init(&r, 1);
    hold(&r, 1, LONG_US + 500000);
    CHECK(btn_core_is_down(&r.b) && !r.long_running && r.longs == 0 && r.shorts == 0);
    hold(&r, 0, 100000);
    CHECK(r.longs == 0 && r.shorts == 0 && !btn_core_is_down(&r.b));
    hold(&r, 1, 100000);
    hold(&r, 0, 100000);
    CHECK(r.shorts == 1);
}

/**
 * @brief Runs every button_core test.
 *
 * @return 0 if all checks pass, 1 otherwise.
 */
int main(void)
{
    test_short_press();
    test_bounce();
    test_long_press();
    test_held_at_start();

    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
