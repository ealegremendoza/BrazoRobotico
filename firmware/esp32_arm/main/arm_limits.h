#ifndef ARM_LIMITS_H
#define ARM_LIMITS_H

/**
 * @file arm_limits.h
 * @brief Joint limits used by the local panel (CTL mode) and sent by the PC in the S command.
 *
 * The limits are the corrected URDF ones (robotic_arm.urdf.xacro, see
 * doc/md/bitacora-E10-panel-lcd.md, D5): any pose reached from the panel is valid for MoveIt.
 * The firmware boots with a copy of them (arm_limits_set_default()); every accepted S with a
 * param_init replaces them, in RAM only.
 *
 * param_init of S (E10, DS10): two blocks with the format of the M payload (m_payload.h),
 * the minimums first and then the maximums, both in mrad:
 *
 *   min1 FS ... FS min6 FS max1 FS ... FS max6        (35 + 1 + 35 = 71 bytes)
 *
 * Pure C: no ESP-IDF, tested on the PC (test_host/test_arm_limits.c).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "m_payload.h"

/** Joints with limits, same order as the M payload (shoulder_pan .. gripper). */
#define ARM_LIMITS_JOINTS M_PAYLOAD_JOINTS

/** Largest magnitude of a limit: +-pi, the range of mrad_to_counts(). */
#define ARM_LIMITS_MAX_MRAD 3142

/** Size of a param_init with limits: two M payloads joined by FS. */
#define ARM_LIMITS_PAYLOAD_SIZE (2 * M_PAYLOAD_SIZE + 1)

/**
 * @brief Limits of every joint, in mrad. A plain struct: no logic hidden in it.
 */
typedef struct {
    int32_t min_mrad[ARM_LIMITS_JOINTS]; /**< Lower limit of each joint. */
    int32_t max_mrad[ARM_LIMITS_JOINTS]; /**< Upper limit of each joint (> min). */
} arm_limits_t;

/**
 * @brief Result of arm_limits_parse().
 */
typedef enum {
    ARM_LIMITS_EMPTY,   /**< No param_init: keep the current limits (DS11). */
    ARM_LIMITS_OK,      /**< Valid limits written to the output. */
    ARM_LIMITS_INVALID, /**< Malformed block, min >= max or out of +-ARM_LIMITS_MAX_MRAD. */
} arm_limits_parse_t;

/**
 * @brief Writes the default limits: the corrected URDF ones in mrad (D5), rounded to the
 *        nearest mrad as the ros2_control plugin sends them.
 *
 * Update them together with robotic_arm.urdf.xacro ([R14], [R15]).
 *
 * @param limits Output.
 */
void arm_limits_set_default(arm_limits_t *limits);

/**
 * @brief Decodes the param_init of an S command.
 *
 * Each block is decoded with m_payload_parse(). All or nothing: on ARM_LIMITS_EMPTY or
 * ARM_LIMITS_INVALID the output is left untouched.
 *
 * @param payload     param_init bytes (between FS and ETX), not NUL-terminated.
 * @param payload_len Number of bytes in payload (0 = no param_init).
 * @param limits      Output: the decoded limits, written only on ARM_LIMITS_OK.
 * @return ARM_LIMITS_EMPTY, ARM_LIMITS_OK or ARM_LIMITS_INVALID.
 */
arm_limits_parse_t arm_limits_parse(const uint8_t *payload, size_t payload_len,
                                    arm_limits_t *limits);

/**
 * @brief Clamps a joint target to its limits (last safety net before the servo bus).
 *
 * @param limits Limits.
 * @param joint  Joint index, 0..ARM_LIMITS_JOINTS-1.
 * @param mrad   Target in mrad.
 * @return mrad limited to [min, max] of the joint; 0 for a bad joint index.
 */
int32_t arm_limits_clamp(const arm_limits_t *limits, size_t joint, int32_t mrad);

#endif // ARM_LIMITS_H
