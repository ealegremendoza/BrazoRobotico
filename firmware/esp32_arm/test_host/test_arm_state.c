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

#define TIMEOUT_US 500000  // 500 ms, PC_LINK_TIMEOUT_MS in arm_bus.c
#define JOINTS 6

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/**
 * @brief Sends an S without param_init that the servos answered (the pre-E10 S).
 *
 * @param arm State.
 * @return true if the state changed (STOPPED -> OPERATING).
 */
static bool start(arm_state_t *arm)
{
    arm_s_decision_t d = arm_state_on_start(arm, ARM_S_PARAMS_EMPTY, true);
    return d.reply == ARM_S_REPLY_M && d.state_changed;
}

/** @brief After boot the arm is STOPPED: M frames must not move the servos. */
static void test_boots_stopped(void)
{
    puts("boots STOPPED: M frames do not move");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US, JOINTS);
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
    puts("S: STOPPED -> OPERATING, a repeated S answers M without a state change");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US, JOINTS);
    arm_state_on_frame(&arm, 1000000);
    CHECK(start(&arm));
    CHECK(arm_state_can_move(&arm));
    CHECK(!start(&arm));
    CHECK(arm_state_can_move(&arm));
}

/** @brief Frames every 20 ms (50 Hz) keep the arm OPERATING. */
static void test_steady_link_keeps_operating(void)
{
    puts("frames at 50 Hz keep OPERATING");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US, JOINTS);
    arm_state_on_frame(&arm, 1000000);
    start(&arm);
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
    arm_state_init(&arm, TIMEOUT_US, JOINTS);
    arm_state_on_frame(&arm, 1000000);
    start(&arm);

    CHECK(arm_state_on_frame(&arm, 1000000 + TIMEOUT_US + 1));
    CHECK(!arm_state_can_move(&arm));

    // Later frames keep it STOPPED (the old targets must not move the arm)...
    CHECK(!arm_state_on_frame(&arm, 1600000));
    CHECK(!arm_state_can_move(&arm));
    // ...until S.
    CHECK(start(&arm));
    CHECK(arm_state_can_move(&arm));
}

/** @brief Exactly the timeout is still fine; one microsecond more is not. */
static void test_watchdog_boundary(void)
{
    puts("watchdog boundary: == timeout keeps OPERATING, > timeout stops");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US, JOINTS);
    arm_state_on_frame(&arm, 1000000);
    start(&arm);
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
    arm_state_init(&arm, TIMEOUT_US, JOINTS);
    arm_state_on_frame(&arm, 1000000);      // previous session, OPERATING
    start(&arm);

    // New session without an ESP32 reset: the plugin waits 700 ms (> timeout).
    CHECK(arm_state_on_frame(&arm, 1700000));   // first probe: watchdog fires
    CHECK(!arm_state_can_move(&arm));           // probes only read
    CHECK(!arm_state_on_frame(&arm, 1720000));
    CHECK(!arm_state_can_move(&arm));

    CHECK(!arm_state_on_frame(&arm, 1740000));  // S frame
    CHECK(start(&arm));
    CHECK(!arm_state_on_frame(&arm, 1760000));  // next M moves
    CHECK(arm_state_can_move(&arm));
}

/** @brief Accepted S: answer M; limits applied only when the param_init had them. */
static void test_start_reply(void)
{
    puts("S accepted: reply M, limits applied only with a valid param_init");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US, JOINTS);
    arm_s_decision_t d = arm_state_on_start(&arm, ARM_S_PARAMS_LIMITS, true);
    CHECK(d.reply == ARM_S_REPLY_M && d.e_code == 0);
    CHECK(d.state_changed && d.apply_limits);
    CHECK(arm_state_can_move(&arm));

    // Already OPERATING: still answers M (no state change); empty param_init keeps limits.
    d = arm_state_on_start(&arm, ARM_S_PARAMS_EMPTY, true);
    CHECK(d.reply == ARM_S_REPLY_M && !d.state_changed && !d.apply_limits);
    CHECK(arm_state_can_move(&arm));
}

/** @brief S rejected with E: 21 invalid limits, 20 servos not answering; state unchanged. */
static void test_start_rejected(void)
{
    puts("S rejected: E 21 (invalid limits), E 20 (servos not answering), stays STOPPED");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US, JOINTS);

    arm_s_decision_t d = arm_state_on_start(&arm, ARM_S_PARAMS_INVALID, true);
    CHECK(d.reply == ARM_S_REPLY_E && d.e_code == ARM_E_INVALID_LIMITS);
    CHECK(!d.state_changed && !d.apply_limits && !arm_state_can_move(&arm));

    d = arm_state_on_start(&arm, ARM_S_PARAMS_LIMITS, false);
    CHECK(d.reply == ARM_S_REPLY_E && d.e_code == ARM_E_SERVOS_NOT_ANSWERING);
    CHECK(!d.state_changed && !d.apply_limits && !arm_state_can_move(&arm));

    // Invalid limits are reported before a bus failure.
    d = arm_state_on_start(&arm, ARM_S_PARAMS_INVALID, false);
    CHECK(d.e_code == ARM_E_INVALID_LIMITS);

    // The codes are the two-digit ones of the R03 table.
    CHECK(ARM_E_S_REJECTED_CTL == 1 && ARM_E_SERVOS_NOT_ANSWERING == 20);
    CHECK(ARM_E_INVALID_LIMITS == 21);
}

/** @brief CTL is granted only in STOPPED, on a valid joint, with the servos answering. */
static void test_ctl_grant(void)
{
    puts("CTL: granted in STOPPED; denied in OPERATING, bad joint or servos not answering");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US, JOINTS);
    CHECK(!arm_state_ctl_active(&arm));

    CHECK(arm_state_ctl_request(&arm, 2, false) == ARM_CTL_DENIED_NO_SERVO);
    CHECK(!arm_state_ctl_active(&arm));
    CHECK(arm_state_ctl_request(&arm, JOINTS, true) == ARM_CTL_DENIED_BAD_JOINT);
    CHECK(!arm_state_ctl_active(&arm));

    CHECK(arm_state_ctl_request(&arm, 2, true) == ARM_CTL_GRANTED);
    CHECK(arm_state_ctl_active(&arm));
    CHECK(!arm_state_can_move(&arm));  // M frames still only read (D1)

    // Only the granted joint may move (DS14).
    CHECK(arm_state_ctl_can_move(&arm, 2));
    for (unsigned j = 0; j < JOINTS; j++) {
        if (j != 2) {
            CHECK(!arm_state_ctl_can_move(&arm, j));
        }
    }

    // A new grant moves CTL to the new joint; a denied one drops it.
    CHECK(arm_state_ctl_request(&arm, 4, true) == ARM_CTL_GRANTED);
    CHECK(arm_state_ctl_can_move(&arm, 4) && !arm_state_ctl_can_move(&arm, 2));
    CHECK(arm_state_ctl_request(&arm, 4, false) == ARM_CTL_DENIED_NO_SERVO);
    CHECK(!arm_state_ctl_active(&arm) && !arm_state_ctl_can_move(&arm, 4));

    // Exit: no more CTL commands; exit without a grant is harmless.
    arm_state_ctl_request(&arm, 0, true);
    arm_state_ctl_exit(&arm);
    CHECK(!arm_state_ctl_active(&arm) && !arm_state_ctl_can_move(&arm, 0));
    arm_state_ctl_exit(&arm);
    CHECK(!arm_state_ctl_active(&arm));

    // OPERATING: a ROS session is running, the panel is read only (D1).
    start(&arm);
    CHECK(arm_state_ctl_request(&arm, 0, true) == ARM_CTL_DENIED_SESSION);
    CHECK(!arm_state_ctl_active(&arm) && !arm_state_ctl_can_move(&arm, 0));
}

/** @brief S while the panel holds CTL: rejected with E 01, STOPPED, CTL kept (D2). */
static void test_start_rejected_in_ctl(void)
{
    puts("S during CTL: E 01, stays STOPPED and in CTL; accepted after the CTL exit");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US, JOINTS);
    arm_state_ctl_request(&arm, 1, true);

    arm_state_on_frame(&arm, 1000000);
    arm_s_decision_t d = arm_state_on_start(&arm, ARM_S_PARAMS_LIMITS, true);
    CHECK(d.reply == ARM_S_REPLY_E && d.e_code == ARM_E_S_REJECTED_CTL);
    CHECK(!d.state_changed && !d.apply_limits);
    CHECK(!arm_state_can_move(&arm));
    CHECK(arm_state_ctl_active(&arm) && arm_state_ctl_can_move(&arm, 1));

    // CTL wins over every other error: no limits check, no read needed.
    d = arm_state_on_start(&arm, ARM_S_PARAMS_INVALID, false);
    CHECK(d.e_code == ARM_E_S_REJECTED_CTL);

    arm_state_ctl_exit(&arm);
    d = arm_state_on_start(&arm, ARM_S_PARAMS_LIMITS, true);
    CHECK(d.reply == ARM_S_REPLY_M && d.state_changed && arm_state_can_move(&arm));
}

/** @brief The periodic check stops the arm without any frame (the PC went silent). */
static void test_watchdog_poll(void)
{
    puts("periodic watchdog: fires without frames, only once, never while STOPPED");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US, JOINTS);

    // STOPPED (boot): nothing to time out.
    CHECK(!arm_state_poll(&arm, 10 * TIMEOUT_US));
    CHECK(!arm_state_can_move(&arm));

    arm_state_on_frame(&arm, 1000000);
    start(&arm);
    // 100 ms polls, like the bus task: fine up to the timeout, then STOPPED once.
    int fired = 0;
    for (int64_t t = 1000000; t <= 1000000 + 2 * TIMEOUT_US; t += 100000) {
        if (arm_state_poll(&arm, t)) {
            fired++;
            CHECK(t > 1000000 + TIMEOUT_US);
        }
    }
    CHECK(fired == 1);
    CHECK(!arm_state_can_move(&arm));

    // The poll does not count as a frame: the next frame does not fire it again.
    CHECK(!arm_state_on_frame(&arm, 1000000 + 3 * TIMEOUT_US));
    CHECK(!arm_state_can_move(&arm));
}

/** @brief After the watchdog stops a session, the panel may take CTL again (D4). */
static void test_ctl_after_watchdog(void)
{
    puts("CTL allowed again after the watchdog stopped the session");
    arm_state_t arm;
    arm_state_init(&arm, TIMEOUT_US, JOINTS);
    arm_state_on_frame(&arm, 1000000);
    start(&arm);
    CHECK(arm_state_ctl_request(&arm, 3, true) == ARM_CTL_DENIED_SESSION);
    CHECK(arm_state_poll(&arm, 1000000 + TIMEOUT_US + 1));
    CHECK(arm_state_ctl_request(&arm, 3, true) == ARM_CTL_GRANTED);
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
    test_start_reply();
    test_start_rejected();
    test_ctl_grant();
    test_start_rejected_in_ctl();
    test_watchdog_poll();
    test_ctl_after_watchdog();

    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
