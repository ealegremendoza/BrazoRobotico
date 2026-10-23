/**
 * @file arm_limits.c
 * @brief Joint limits: defaults and the S param_init decoding (see arm_limits.h).
 */

#include "arm_limits.h"

#include "frame_parser.h"

/**
 * Corrected URDF limits (robotic_arm.urdf.xacro, D5 table of bitacora-E10-panel-lcd.md)
 * in mrad, rounded like the plugin's buildMPayload() (std::lround of rad * 1000).
 */
static const arm_limits_t DEFAULT_LIMITS = {
    //           J1     J2     J3     J4     J5    J6 (gripper)
    .min_mrad = {-1920, -1745, -1640, -1658, -2744, -175},
    .max_mrad = {1920, 1710, 1580, 1658, 2720, 1745},
};

/**
 * @brief Writes the default limits (corrected URDF, D5).
 *
 * @param limits Output.
 */
void arm_limits_set_default(arm_limits_t *limits)
{
    *limits = DEFAULT_LIMITS;
}

/**
 * @brief Decodes the param_init of an S command (two M-format blocks: mins, then maxs).
 *
 * @param payload     param_init bytes.
 * @param payload_len Number of bytes in payload.
 * @param limits      Output, written only on ARM_LIMITS_OK.
 * @return ARM_LIMITS_EMPTY, ARM_LIMITS_OK or ARM_LIMITS_INVALID.
 */
arm_limits_parse_t arm_limits_parse(const uint8_t *payload, size_t payload_len,
                                    arm_limits_t *limits)
{
    if (payload_len == 0) {
        return ARM_LIMITS_EMPTY;
    }
    if (payload_len != ARM_LIMITS_PAYLOAD_SIZE || payload[M_PAYLOAD_SIZE] != FRAME_FS) {
        return ARM_LIMITS_INVALID;
    }

    // Decode into a local struct first: limits is only written if everything is valid.
    arm_limits_t parsed;
    if (!m_payload_parse(payload, M_PAYLOAD_SIZE, parsed.min_mrad) ||
        !m_payload_parse(payload + M_PAYLOAD_SIZE + 1, M_PAYLOAD_SIZE, parsed.max_mrad)) {
        return ARM_LIMITS_INVALID;
    }
    for (size_t j = 0; j < ARM_LIMITS_JOINTS; j++) {
        int32_t lo = parsed.min_mrad[j];
        int32_t hi = parsed.max_mrad[j];
        if (lo >= hi || lo < -ARM_LIMITS_MAX_MRAD || hi > ARM_LIMITS_MAX_MRAD) {
            return ARM_LIMITS_INVALID;
        }
    }

    *limits = parsed;
    return ARM_LIMITS_OK;
}

/**
 * @brief Clamps a joint target to its limits.
 *
 * @param limits Limits.
 * @param joint  Joint index.
 * @param mrad   Target in mrad.
 * @return The clamped target, 0 for a bad joint index.
 */
int32_t arm_limits_clamp(const arm_limits_t *limits, size_t joint, int32_t mrad)
{
    if (joint >= ARM_LIMITS_JOINTS) {
        return 0;
    }
    if (mrad < limits->min_mrad[joint]) {
        return limits->min_mrad[joint];
    }
    if (mrad > limits->max_mrad[joint]) {
        return limits->max_mrad[joint];
    }
    return mrad;
}
