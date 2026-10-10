#ifndef ARM_STATE_H
#define ARM_STATE_H

/**
 * @file arm_state.h
 * @brief Arm state: STOPPED / OPERATING plus the PC link watchdog.
 *
 * Minimal version of the R03 state machine (doc/md/bitacora-R03-arquitectura.md);
 * the rest of the states, buttons and E codes come with E09.
 *
 * - STOPPED (after boot or a link timeout): M frames only read the servos and
 *   answer the feedback, they never move them, so the PC can learn the real pose
 *   before commanding anything.
 * - OPERATING (after S, start): M frames move the servos.
 * - Link watchdog: if no frame arrives for longer than the timeout while
 *   OPERATING, go back to STOPPED and wait for a new S. Never resume moving on
 *   stale targets after the PC went away (plugin stopped, cable unplugged).
 *
 * Pure logic: the caller passes the time, so it can be tested on the PC.
 */

#include <stdbool.h>
#include <stdint.h>

/** Arm states. */
typedef enum {
    ARM_STOPPED,   /**< M = read only. */
    ARM_OPERATING, /**< M = move + read. */
} arm_state_id_t;

/**
 * @brief State and watchdog bookkeeping. One per PC link.
 */
typedef struct {
    arm_state_id_t state;      /**< Current state. */
    int64_t last_frame_us;     /**< Time of the last valid frame from the PC. */
    int64_t link_timeout_us;   /**< Max silence while OPERATING before STOPPED. */
} arm_state_t;

/**
 * @brief Starts STOPPED (boot state).
 *
 * @param arm             State to initialize.
 * @param link_timeout_us Max silence from the PC while OPERATING, in microseconds.
 */
void arm_state_init(arm_state_t *arm, int64_t link_timeout_us);

/**
 * @brief Registers a valid frame from the PC; applies the link watchdog first.
 *
 * Call it for every valid frame, before handling it.
 *
 * @param arm    State.
 * @param now_us Current time in microseconds (monotonic).
 * @return true if the watchdog fired (was OPERATING and the silence exceeded the
 *         timeout): the state is now STOPPED.
 */
bool arm_state_on_frame(arm_state_t *arm, int64_t now_us);

/**
 * @brief Handles S (start): STOPPED -> OPERATING.
 *
 * @param arm State.
 * @return true if the state changed (it was STOPPED), false if already OPERATING.
 */
bool arm_state_start(arm_state_t *arm);

/**
 * @brief Whether M frames may move the servos.
 *
 * @param arm State.
 * @return true only when OPERATING.
 */
bool arm_state_can_move(const arm_state_t *arm);

#endif // ARM_STATE_H
