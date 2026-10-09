#include "servo_units.h"

#include <stdlib.h>

#define COUNTS_PER_REV 4096     // 12-bit encoder: counts in one full turn
#define COUNTS_MAX (COUNTS_PER_REV - 1)
#define COUNTS_ZERO 2048        // calibrated zero pose (OFS in each servo's EEPROM)
#define MRAD_PER_REV 6283       // 2*pi rad, rounded to whole mrad
#define MRAD_MAX 3142           // +-pi: one full turn centered on the zero pose

/** 
 * @brief Converts milliradians to servo counts.
 *  A full rotation is 2π radians, which is 6283 milliradians. The servo has a range of 4096 counts.
 * Therefore, the conversion factor is 4096 counts / 6283 mrad.
 * 
 * 6283 mrad -> 4096 counts
 * x mrad -> x * 4096/6283 counts
 * 
 * The calibration offset is 2048 counts, which corresponds to 0 mrad.
 * 
 * Note: Use 6283 instead of 6283.185 introduce an error of 0.003% .For our servo 1 count corresponds 
 * to 1.533 mrad, which is a resolution of 0.088 degrees. This is sufficient for our application.
 *  
 * @param mrad The angle in milliradians.
 * @return The corresponding servo counts.
 */
int32_t mrad_to_counts(int32_t mrad)
{
    // Clamp the input first: keeps mrad * COUNTS_PER_REV far from int32_t overflow.
    if (mrad > MRAD_MAX) {
        mrad = MRAD_MAX;
    } else if (mrad < -MRAD_MAX) {
        mrad = -MRAD_MAX;
    }

    // Integer division truncates, so adding half the divisor before dividing
    // rounds to the nearest integer (like adding 0.5 to the quotient).
    // C division truncates toward zero, so adding half the divisor only rounds
    // correctly for positive values: round the magnitude, then restore the sign.
    int32_t delta = (abs(mrad) * COUNTS_PER_REV + MRAD_PER_REV / 2) / MRAD_PER_REV;
    if (mrad < 0) {
        delta = -delta;
    }

    // +pi maps to COUNTS_ZERO + 2048 = 4096, one past the last valid count.
    int32_t counts = COUNTS_ZERO + delta;
    return counts > COUNTS_MAX ? COUNTS_MAX : counts;
}

/**
 * @brief Converts servo counts to milliradians (inverse of mrad_to_counts()).
 *
 * 4096 counts -> 6283 mrad
 * x counts    -> (x - 2048) * 6283/4096 mrad
 *
 * The result spans [-3142, 3140] mrad, inside the +-3142 range the PC accepts.
 *
 * @param counts The servo position in counts (out-of-range values are clamped to [0, 4095]).
 * @return The corresponding angle in milliradians.
 */
int32_t counts_to_mrad(int32_t counts)
{
    // Clamp the input first: a corrupted reading must not produce an angle
    // outside +-pi (and keeps delta * MRAD_PER_REV far from int32_t overflow).
    if (counts > COUNTS_MAX) {
        counts = COUNTS_MAX;
    } else if (counts < 0) {
        counts = 0;
    }

    int32_t delta = counts - COUNTS_ZERO;

    // Same symmetric rounding as mrad_to_counts(): round the magnitude by adding
    // half the divisor before dividing, then restore the sign.
    int32_t mrad = (abs(delta) * MRAD_PER_REV + COUNTS_PER_REV / 2) / COUNTS_PER_REV;
    if (delta < 0) {
        mrad = -mrad;
    }
    return mrad;
}
