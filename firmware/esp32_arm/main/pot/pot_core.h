#ifndef POT_CORE_H
#define POT_CORE_H

/**
 * @file pot_core.h
 * @brief Pure logic of the potentiometer reader: moving average, counts -> percent and change
 *        detection with hysteresis. No ADC, no FreeRTOS, no logging.
 *
 * Everything here can be tested on the PC (test_host/test_pot_core.c):
 *
 *   every sample (10 ms) -> pot_avg_push()      raw counts -> moving average over N samples
 *   filtered value       -> pot_change_update() log or stay quiet (hysteresis)
 *   log line             -> pot_percent_mv()    calibrated mV -> 0..100 % (pot_percent()
 *                                               on counts if calibration is unavailable)
 *
 * ADC notes (ESP32, 12-bit, ADC_ATTEN_DB_12): counts go 0..4095, but the ADC is nonlinear
 * near both ends and saturates (4095) at about 3.1 V, below the 3.3 V supply. So 0 % is
 * read over the first bit of travel and 100 % is reached slightly before the end of travel.
 * Counts are also nonlinear in between (1.65 V reads ~1830, not 2048), so the logged percent
 * comes from the calibrated millivolts, which follow the real voltage.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POT_RAW_MAX 4095    // full scale of the 12-bit ADC (ADC_BITWIDTH_DEFAULT on ESP32)
#define POT_AVG_LEN 16      // moving average window: 16 samples x 10 ms = 160 ms
#define POT_HYST_COUNTS 20  // filtered change (counts, ~0.5 %) needed to log a new value

/**
 * @brief Moving average over the last POT_AVG_LEN samples (ring buffer plus running sum).
 */
typedef struct {
    uint16_t samples[POT_AVG_LEN]; // ring buffer, valid entries: count
    uint32_t sum;                  // sum of the valid entries
    size_t count;                  // valid entries, 0..POT_AVG_LEN
    size_t next;                   // slot written by the next push
} pot_avg_t;

/**
 * @brief Change detector with hysteresis: reports a value only when it moved far enough
 *        from the last reported one.
 */
typedef struct {
    int last;      // last reported value (valid if has_last)
    bool has_last; // false until the first report
    int threshold; // minimum |value - last| that is reported
    int max;       // full scale: 0 and max are always reported when reached
} pot_change_t;

/**
 * @brief Empties the moving average.
 *
 * @param avg Filter state (not NULL).
 */
void pot_avg_init(pot_avg_t *avg);

/**
 * @brief Adds one sample and returns the average of the samples held (up to POT_AVG_LEN).
 *
 * While the window is filling up, the average is over the samples received so far, so the
 * first output equals the first sample. The sample is clamped to 0..POT_RAW_MAX.
 *
 * @param avg Filter state (not NULL).
 * @param raw ADC counts.
 * @return Rounded average, 0..POT_RAW_MAX.
 */
int pot_avg_push(pot_avg_t *avg, int raw);

/**
 * @brief Converts ADC counts to percent of the ADC range.
 *
 * @param counts ADC counts; values outside 0..POT_RAW_MAX are clamped.
 * @return Rounded percent, 0..100 (0 -> 0, 4095 -> 100, 2048 -> 50).
 */
int pot_percent(int counts);

/**
 * @brief Converts calibrated millivolts to percent of the voltage range the ADC can read.
 *
 * mv_min and mv_max are the calibrated voltages of counts 0 and POT_RAW_MAX, so 0 % and
 * 100 % are reached exactly where the counts saturate, and in between the percent follows
 * the real voltage (linear in the wiper position).
 *
 * @param mv     Calibrated millivolts; values outside mv_min..mv_max are clamped.
 * @param mv_min Millivolts at 0 %.
 * @param mv_max Millivolts at 100 % (must be > mv_min, otherwise 0 is returned).
 * @return Rounded percent, 0..100.
 */
int pot_percent_mv(int mv, int mv_min, int mv_max);

/**
 * @brief Prepares a change detector. The first pot_change_update() always reports.
 *
 * @param chg       Detector state (not NULL).
 * @param threshold Minimum change that is reported (values < 1 are treated as 1).
 * @param max       Full scale of the values (e.g. POT_RAW_MAX).
 */
void pot_change_init(pot_change_t *chg, int threshold, int max);

/**
 * @brief Decides whether a new value must be reported, and if so remembers it.
 *
 * Reports when: it is the first value; or |value - last| >= threshold (in either
 * direction); or the value reached an end (0 or max) and differs from the last report, so
 * the extremes are always shown even after a move smaller than the threshold. Small jitter
 * around the last reported value is ignored, and the reference does not drift with it.
 *
 * @param chg   Detector state (not NULL, initialized).
 * @param value New (filtered) value.
 * @return true if the value must be reported (it becomes the new reference).
 */
bool pot_change_update(pot_change_t *chg, int value);

#endif // POT_CORE_H
