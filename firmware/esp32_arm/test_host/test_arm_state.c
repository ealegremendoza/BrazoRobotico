/**
 * @file test_arm_state.c
 * @brief Host (PC) tests for main/arm_state.c. No ESP-IDF needed.
 *
 * Build and run from firmware/esp32_arm:
 *
 *   gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
 *       test_host/test_arm_state.c main/arm_state.c -o /tmp/test_arm_state \
 *       && /tmp/test_arm_state
 */

#include <stdio.h>

#include "arm_state.h"

#define TIMEOUT_US 500000  // 500 ms, PC_LINK_TIMEOUT_MS in esp32_arm.c

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/** @brief After boot the arm is STOPPED: M frames must not move the servos. */
static void test_boots_stopped(void)
{
    puts("boots STOPPED: M frames do not move");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US);
    CHECK(!arm_state_can_move(&arm));

    // Frames alone (the plugin's startup probes) never start the arm.
    for (int64_t t = 1000000; t < 3000000; t += 20000) {
        CHECK(!arm_state_on_frame(&arm, t));
        CHECK(!arm_state_can_move(&arm));
    }
}

/** @brief S moves to OPERATING; a second S changes nothing. */
static void test_start(void)
{
    puts("S: STOPPED -> OPERATING, a repeated S is a no-op");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US);
    arm_state_on_frame(&arm, 1000000);
    CHECK(arm_state_start(&arm));
    CHECK(arm_state_can_move(&arm));
    CHECK(!arm_state_start(&arm));
    CHECK(arm_state_can_move(&arm));
}

/** @brief Frames every 20 ms (50 Hz) keep the arm OPERATING. */
static void test_steady_link_keeps_operating(void)
{
    puts("frames at 50 Hz keep OPERATING");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US);
    arm_state_on_frame(&arm, 1000000);
    arm_state_start(&arm);
    for (int64_t t = 1020000; t < 6000000; t += 20000) {
        CHECK(!arm_state_on_frame(&arm, t));
        CHECK(arm_state_can_move(&arm));
    }
}

/** @brief A silence longer than the timeout stops the arm; it needs a new S. */
static void test_watchdog_stops(void)
{
    puts("silence > timeout while OPERATING: STOPPED until a new S");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US);
    arm_state_on_frame(&arm, 1000000);
    arm_state_start(&arm);

    CHECK(arm_state_on_frame(&arm, 1000000 + TIMEOUT_US + 1));
    CHECK(!arm_state_can_move(&arm));

    // Later frames keep it STOPPED (the old targets must not move the arm)...
    CHECK(!arm_state_on_frame(&arm, 1600000));
    CHECK(!arm_state_can_move(&arm));
    // ...until S.
    CHECK(arm_state_start(&arm));
    CHECK(arm_state_can_move(&arm));
}

/** @brief Exactly the timeout is still fine; one microsecond more is not. */
static void test_watchdog_boundary(void)
{
    puts("watchdog boundary: == timeout keeps OPERATING, > timeout stops");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US);
    arm_state_on_frame(&arm, 1000000);
    arm_state_start(&arm);
    CHECK(!arm_state_on_frame(&arm, 1000000 + TIMEOUT_US));
    CHECK(arm_state_can_move(&arm));
    CHECK(arm_state_on_frame(&arm, 1000000 + 2 * TIMEOUT_US + 1));
    CHECK(!arm_state_can_move(&arm));
}

/** @brief The plugin's startup sequence: probes (STOPPED), S, then moving M frames. */
static void test_plugin_startup_sequence(void)
{
    puts("plugin startup: probes after a 700 ms wait, S, then moves");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US);
    arm_state_on_frame(&arm, 1000000);      // previous session, OPERATING
    arm_state_start(&arm);

    // New session without an ESP32 reset: the plugin waits 700 ms (> timeout).
    CHECK(arm_state_on_frame(&arm, 1700000));   // first probe: watchdog fires
    CHECK(!arm_state_can_move(&arm));           // probes only read
    CHECK(!arm_state_on_frame(&arm, 1720000));
    CHECK(!arm_state_can_move(&arm));

    CHECK(!arm_state_on_frame(&arm, 1740000));  // S frame
    CHECK(arm_state_start(&arm));
    CHECK(!arm_state_on_frame(&arm, 1760000));  // next M moves
    CHECK(arm_state_can_move(&arm));
}

/**
 * @brief Runs every arm_state test.
 *
 * @return 0 if all checks pass, 1 otherwise.
 */
int main(void)
{
    test_boots_stopped();
    test_start();
    test_steady_link_keeps_operating();
    test_watchdog_stops();
    test_watchdog_boundary();
    test_plugin_startup_sequence();

    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
