/**
 * @file arm_state.c
 * @brief Arm state (STOPPED / OPERATING), PC link watchdog, CTL grant and S decision
 *        (see arm_state.h).
 */

#include "arm_state.h"

/**
 * @brief Starts STOPPED (boot state), without CTL.
 *
 * @param arm             State to initialize.
 * @param link_timeout_us Max silence from the PC while OPERATING, in microseconds.
 * @param joint_count     Number of joints the panel may pick from.
 */
void arm_state_init(arm_state_t *arm, int64_t link_timeout_us, unsigned joint_count)
{
    arm->state = ARM_STOPPED;
    arm->last_frame_us = 0;
    arm->link_timeout_us = link_timeout_us;
    arm->ctl_active = false;
    arm->ctl_joint = 0;
    arm->joint_count = joint_count;
}

/**
 * @brief Applies the link watchdog at a given time.
 *
 * @param arm    State.
 * @param now_us Current time in microseconds.
 * @return true if the watchdog fired and the state went back to STOPPED.
 */
bool arm_state_poll(arm_state_t *arm, int64_t now_us)
{
    // Only OPERATING can time out: STOPPED is already the safe state.
    bool timed_out = arm->state == ARM_OPERATING &&
                     now_us - arm->last_frame_us > arm->link_timeout_us;
    if (timed_out) {
        arm->state = ARM_STOPPED;
    }
    return timed_out;
}

/**
 * @brief Registers a valid frame from the PC; applies the link watchdog first.
 *
 * @param arm    State.
 * @param now_us Time the frame arrived, in microseconds.
 * @return true if the watchdog fired and the state went back to STOPPED.
 */
bool arm_state_on_frame(arm_state_t *arm, int64_t now_us)
{
    bool timed_out = arm_state_poll(arm, now_us);
    arm->last_frame_us = now_us;
    return timed_out;
}

/**
 * @brief Decides on an S (start) command (see arm_state.h for the order of the checks).
 *
 * @param arm             State.
 * @param params          Content of the param_init.
 * @param servos_answered The SYNC_READ done for this S got every servo.
 * @return The decision.
 */
arm_s_decision_t arm_state_on_start(arm_state_t *arm, arm_s_params_t params,
                                    bool servos_answered)
{
    arm_s_decision_t d = {.reply = ARM_S_REPLY_E, .e_code = 0, .state_changed = false,
                          .apply_limits = false};
    if (arm->ctl_active) {
        d.e_code = ARM_E_S_REJECTED_CTL;  // the panel wins (D2): stay STOPPED
        return d;
    }
    if (params == ARM_S_PARAMS_INVALID) {
        d.e_code = ARM_E_INVALID_LIMITS;
        return d;
    }
    if (!servos_answered) {
        d.e_code = ARM_E_SERVOS_NOT_ANSWERING;  // no position to answer with (D3)
        return d;
    }

    d.reply = ARM_S_REPLY_M;
    d.apply_limits = params == ARM_S_PARAMS_LIMITS;
    d.state_changed = arm->state != ARM_OPERATING;
    arm->state = ARM_OPERATING;
    return d;
}

/**
 * @brief The panel asks for CTL on one joint.
 *
 * @param arm            State.
 * @param joint          Joint index.
 * @param servo_answered The SYNC_READ done for this request got the servos.
 * @return ARM_CTL_GRANTED or the reason for the denial.
 */
arm_ctl_result_t arm_state_ctl_request(arm_state_t *arm, unsigned joint, bool servo_answered)
{
    arm_ctl_result_t result = ARM_CTL_GRANTED;
    if (arm->state != ARM_STOPPED) {
        result = ARM_CTL_DENIED_SESSION;
    } else if (joint >= arm->joint_count) {
        result = ARM_CTL_DENIED_BAD_JOINT;
    } else if (!servo_answered) {
        result = ARM_CTL_DENIED_NO_SERVO;
    }

    // A denied request also drops a previous grant: the panel is no longer in CTL.
    arm->ctl_active = result == ARM_CTL_GRANTED;
    arm->ctl_joint = arm->ctl_active ? joint : 0;
    return result;
}

/**
 * @brief The panel left CTL.
 *
 * @param arm State.
 */
void arm_state_ctl_exit(arm_state_t *arm)
{
    arm->ctl_active = false;
    arm->ctl_joint = 0;
}

/**
 * @brief Whether CTL is granted.
 *
 * @param arm State.
 * @return true while the panel holds CTL.
 */
bool arm_state_ctl_active(const arm_state_t *arm)
{
    return arm->ctl_active;
}

/**
 * @brief Whether a CTL command for a joint may move its servo.
 *
 * @param arm   State.
 * @param joint Joint of the command.
 * @return true only with CTL granted on that joint (and STOPPED).
 */
bool arm_state_ctl_can_move(const arm_state_t *arm, unsigned joint)
{
    return arm->ctl_active && arm->state == ARM_STOPPED && joint == arm->ctl_joint;
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
