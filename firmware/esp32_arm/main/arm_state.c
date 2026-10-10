/**
 * @file arm_state.c
 * @brief Arm state (STOPPED / OPERATING) and PC link watchdog (see arm_state.h).
 */

#include "arm_state.h"

/**
 * @brief Starts STOPPED (boot state).
 *
 * @param arm             State to initialize.
 * @param link_timeout_us Max silence from the PC while OPERATING, in microseconds.
 */
void arm_state_init(arm_state_t *arm, int64_t link_timeout_us)
{
    arm->state = ARM_STOPPED;
    arm->last_frame_us = 0;
    arm->link_timeout_us = link_timeout_us;
}

/**
 * @brief Registers a valid frame from the PC; applies the link watchdog first.
 *
 * @param arm    State.
 * @param now_us Current time in microseconds (monotonic).
 * @return true if the watchdog fired and the state went back to STOPPED.
 */
bool arm_state_on_frame(arm_state_t *arm, int64_t now_us)
{
    // Only OPERATING can time out: STOPPED is already the safe state.
    bool timed_out = arm->state == ARM_OPERATING &&
                     now_us - arm->last_frame_us > arm->link_timeout_us;
    if (timed_out) {
        arm->state = ARM_STOPPED;
    }
    arm->last_frame_us = now_us;
    return timed_out;
}

/**
 * @brief Handles S (start): STOPPED -> OPERATING.
 *
 * @param arm State.
 * @return true if the state changed.
 */
bool arm_state_start(arm_state_t *arm)
{
    if (arm->state == ARM_OPERATING) {
        return false;
    }
    arm->state = ARM_OPERATING;
    return true;
}

/**
 * @brief Whether M frames may move the servos.
 *
 * @param arm State.
 * @return true only when OPERATING.
 */
bool arm_state_can_move(const arm_state_t *arm)
{
    return arm->state == ARM_OPERATING;
}
