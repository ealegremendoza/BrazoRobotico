#ifndef ARM_STATE_H
#define ARM_STATE_H

/**
 * @file arm_state.h
 * @brief Arm state: STOPPED / OPERATING, the PC link watchdog, the local panel's CTL grant
 *        and the decision on the S command.
 *
 * Minimal version of the R03 state machine (doc/md/bitacora-R03-arquitectura.md); the rest
 * of the states, the buttons and their E codes come with E09. Owned by the servo bus task
 * (doc/md/bitacora-E10-panel-lcd.md, DS3/DS9): the safety state never depends on the UI.
 *
 * - STOPPED (after boot or a link timeout): M frames only read the servos and answer the
 *   feedback, they never move them, so the PC can learn the real pose before commanding
 *   anything. The local panel may take ONE joint (CTL mode) only here (E10, D1/D4).
 * - OPERATING (after an accepted S, start): M frames move the servos. CTL is not allowed.
 * - Link watchdog: if no frame arrives for longer than the timeout while OPERATING, go back
 *   to STOPPED and wait for a new S. Never resume moving on stale targets after the PC went
 *   away (plugin stopped, cable unplugged). Checked on every frame and periodically
 *   (arm_state_poll()), so it also fires when the PC stops sending altogether.
 * - CTL grant (DS4): the UI asks, this module grants or denies. While CTL is granted an S
 *   is rejected (E 01, D2); without a grant the CTL commands are ignored.
 * - S (D3, DS8, DS11): accepted -> answer M with the current position (read on the same bus
 *   transaction), rejected -> answer E with a two-digit code.
 *
 * Pure logic: the caller passes the time and the bus results, so it can be tested on the PC.
 */

#include <stdbool.h>
#include <stdint.h>

/** E codes (two decimal digits, table in R03; 0x answers/states, 1x stops (E09), 2x errors). */
#define ARM_E_S_REJECTED_CTL 1        /**< S rejected: the panel is in CTL mode (D2). */
#define ARM_E_SERVOS_NOT_ANSWERING 20 /**< The servos did not answer the read for the S. */
#define ARM_E_INVALID_LIMITS 21       /**< Invalid limits in the S param_init (DS11). */

/** Arm states. */
typedef enum {
    ARM_STOPPED,   /**< M = read only; CTL allowed. */
    ARM_OPERATING, /**< M = move + read. */
} arm_state_id_t;

/** Why a CTL request was denied (ARM_CTL_GRANTED = it was not). */
typedef enum {
    ARM_CTL_GRANTED,         /**< Granted. */
    ARM_CTL_DENIED_SESSION,  /**< OPERATING: a ROS session is running (D1). */
    ARM_CTL_DENIED_NO_SERVO, /**< The servos did not answer the read before granting (DS14). */
    ARM_CTL_DENIED_BAD_JOINT, /**< Joint index out of range. */
} arm_ctl_result_t;

/** What to answer to an S. */
typedef enum {
    ARM_S_REPLY_M, /**< Accepted: answer M with the position just read (D3). */
    ARM_S_REPLY_E, /**< Rejected: answer E with arm_s_decision_t::e_code. */
} arm_s_reply_t;

/**
 * @brief Decision on an S command.
 */
typedef struct {
    arm_s_reply_t reply;    /**< M or E. */
    uint8_t e_code;         /**< ARM_E_* code when reply is ARM_S_REPLY_E, 0 otherwise. */
    bool state_changed;     /**< STOPPED -> OPERATING happened. */
    bool apply_limits;      /**< Accepted with valid limits: the caller replaces its limits. */
} arm_s_decision_t;

/**
 * @brief Content of the S param_init, as decoded by the caller (arm_limits_parse()).
 */
typedef enum {
    ARM_S_PARAMS_EMPTY,   /**< No param_init: keep the current limits. */
    ARM_S_PARAMS_LIMITS,  /**< Valid limits. */
    ARM_S_PARAMS_INVALID, /**< Invalid limits: reject with E 21. */
} arm_s_params_t;

/**
 * @brief State, watchdog and CTL bookkeeping. One per PC link.
 */
typedef struct {
    arm_state_id_t state;      /**< Current state. */
    int64_t last_frame_us;     /**< Time of the last valid frame from the PC. */
    int64_t link_timeout_us;   /**< Max silence while OPERATING before STOPPED. */
    bool ctl_active;           /**< The panel holds CTL on ctl_joint. */
    unsigned ctl_joint;        /**< Joint the panel may move (valid while ctl_active). */
    unsigned joint_count;      /**< Number of joints (valid ctl_joint < joint_count). */
} arm_state_t;

/**
 * @brief Starts STOPPED (boot state), without CTL.
 *
 * @param arm             State to initialize.
 * @param link_timeout_us Max silence from the PC while OPERATING, in microseconds.
 * @param joint_count     Number of joints the panel may pick from.
 */
void arm_state_init(arm_state_t *arm, int64_t link_timeout_us, unsigned joint_count);

/**
 * @brief Registers a valid frame from the PC; applies the link watchdog first.
 *
 * Call it for every valid frame, before handling it.
 *
 * @param arm    State.
 * @param now_us Time the frame arrived, in microseconds (monotonic).
 * @return true if the watchdog fired (was OPERATING and the silence exceeded the
 *         timeout): the state is now STOPPED.
 */
bool arm_state_on_frame(arm_state_t *arm, int64_t now_us);

/**
 * @brief Applies the link watchdog without a frame (periodic check, DS3).
 *
 * @param arm    State.
 * @param now_us Current time in microseconds (monotonic).
 * @return true if the watchdog fired: the state is now STOPPED.
 */
bool arm_state_poll(arm_state_t *arm, int64_t now_us);

/**
 * @brief Decides on an S (start) command.
 *
 * Checked in this order: CTL granted -> E 01 (D2, nothing changes); invalid limits -> E 21;
 * servos did not answer -> E 20; otherwise accepted: OPERATING (already OPERATING stays so)
 * and answer M with the position read. The caller reads the servos for every S, before
 * calling this, so the M answer and the transition are atomic (D3).
 *
 * @param arm             State.
 * @param params          Content of the param_init.
 * @param servos_answered The SYNC_READ done for this S got every servo.
 * @return The decision.
 */
arm_s_decision_t arm_state_on_start(arm_state_t *arm, arm_s_params_t params,
                                    bool servos_answered);

/**
 * @brief The panel asks for CTL on one joint (DS4, DS14).
 *
 * Granted only when STOPPED, the joint is valid and the read done before calling this got
 * the servos (that read is the joint's current angle for the soft takeover, DS13). A new
 * grant replaces a previous one (one joint at a time).
 *
 * @param arm            State.
 * @param joint          Joint index.
 * @param servo_answered The SYNC_READ done for this request got the servos.
 * @return ARM_CTL_GRANTED or the reason for the denial.
 */
arm_ctl_result_t arm_state_ctl_request(arm_state_t *arm, unsigned joint, bool servo_answered);

/**
 * @brief The panel left CTL. Harmless without a grant.
 *
 * @param arm State.
 */
void arm_state_ctl_exit(arm_state_t *arm);

/**
 * @brief Whether CTL is granted.
 *
 * @param arm State.
 * @return true while the panel holds CTL.
 */
bool arm_state_ctl_active(const arm_state_t *arm);

/**
 * @brief Whether a CTL command for a joint may move its servo.
 *
 * @param arm   State.
 * @param joint Joint of the command.
 * @return true only with CTL granted on that same joint.
 */
bool arm_state_ctl_can_move(const arm_state_t *arm, unsigned joint);

/**
 * @brief Whether M frames may move the servos.
 *
 * @param arm State.
 * @return true only when OPERATING.
 */
bool arm_state_can_move(const arm_state_t *arm);

#endif // ARM_STATE_H
