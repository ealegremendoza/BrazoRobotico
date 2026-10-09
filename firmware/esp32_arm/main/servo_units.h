#ifndef SERVO_UNITS_H
#define SERVO_UNITS_H

/**
 * @file servo_units.h
 * @brief Conversion between joint angles (mrad, PC side) and STS3215 counts (servo side).
 *
 * 4096 counts = 6283 mrad (one turn); the calibrated zero pose reads 2048 counts.
 */

#include <stdint.h>

/**
 * @brief Converts a joint angle to servo counts.
 *
 * @param mrad Angle in milliradians; clamped to +-3142 (+-pi).
 * @return Servo position in counts, in [0, 4095] (+pi would be 4096 and is clamped).
 */
int32_t mrad_to_counts(int32_t mrad);

/**
 * @brief Converts servo counts to a joint angle (inverse of mrad_to_counts()).
 *
 * @param counts Servo position; clamped to [0, 4095].
 * @return Angle in milliradians, in [-3142, 3140].
 */
int32_t counts_to_mrad(int32_t counts);

#endif // SERVO_UNITS_H
