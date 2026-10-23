/**
 * @file pot_core.c
 * @brief Pure logic of the potentiometer reader (see pot_core.h).
 */

#include "pot_core.h"

/**
 * @brief Clamps a value to 0..max.
 *
 * @param value Value to clamp.
 * @param max   Upper limit (>= 0).
 * @return value limited to 0..max.
 */
static int clamp_range(int value, int max)
{
    if (value < 0) {
        return 0;
    }
    if (value > max) {
        return max;
    }
    return value;
}

/**
 * @brief Empties the moving average.
 *
 * @param avg Filter state (not NULL).
 */
void pot_avg_init(pot_avg_t *avg)
{
    for (size_t i = 0; i < POT_AVG_LEN; i++) {
        avg->samples[i] = 0;
    }
    avg->sum = 0;
    avg->count = 0;
    avg->next = 0;
}

/**
 * @brief Adds one sample and returns the average of the samples held (up to POT_AVG_LEN).
 *
 * @param avg Filter state (not NULL).
 * @param raw ADC counts (clamped to 0..POT_RAW_MAX).
 * @return Rounded average, 0..POT_RAW_MAX.
 */
int pot_avg_push(pot_avg_t *avg, int raw)
{
    uint16_t sample = (uint16_t)clamp_range(raw, POT_RAW_MAX);

    if (avg->count == POT_AVG_LEN) {
        avg->sum -= avg->samples[avg->next]; // window full: drop the oldest sample
    } else {
        avg->count++;
    }
    avg->samples[avg->next] = sample;
    avg->sum += sample;
    avg->next = (avg->next + 1) % POT_AVG_LEN;

    return (int)((avg->sum + avg->count / 2) / avg->count);
}

/**
 * @brief Converts ADC counts to percent of the ADC range.
 *
 * @param counts ADC counts (clamped to 0..POT_RAW_MAX).
 * @return Rounded percent, 0..100.
 */
int pot_percent(int counts)
{
    int c = clamp_range(counts, POT_RAW_MAX);

    return (c * 100 + POT_RAW_MAX / 2) / POT_RAW_MAX;
}

/**
 * @brief Converts calibrated millivolts to percent of the voltage range the ADC can read.
 *
 * @param mv     Calibrated millivolts (clamped to mv_min..mv_max).
 * @param mv_min Millivolts at 0 %.
 * @param mv_max Millivolts at 100 % (> mv_min, otherwise 0 is returned).
 * @return Rounded percent, 0..100.
 */
int pot_percent_mv(int mv, int mv_min, int mv_max)
{
    int span = mv_max - mv_min;

    if (span <= 0) {
        return 0;
    }
    int v = clamp_range(mv - mv_min, span);
    return (v * 100 + span / 2) / span;
}

/**
 * @brief Prepares a change detector. The first pot_change_update() always reports.
 *
 * @param chg       Detector state (not NULL).
 * @param threshold Minimum change that is reported (values < 1 are treated as 1).
 * @param max       Full scale of the values.
 */
void pot_change_init(pot_change_t *chg, int threshold, int max)
{
    chg->last = 0;
    chg->has_last = false;
    chg->threshold = threshold < 1 ? 1 : threshold;
    chg->max = max;
}

/**
 * @brief Decides whether a new value must be reported, and if so remembers it.
 *
 * @param chg   Detector state (not NULL, initialized).
 * @param value New (filtered) value.
 * @return true if the value must be reported (it becomes the new reference).
 */
bool pot_change_update(pot_change_t *chg, int value)
{
    bool report;

    if (!chg->has_last) {
        report = true;
    } else {
        int delta = value - chg->last;
        if (delta < 0) {
            delta = -delta;
        }
        bool at_end = value <= 0 || value >= chg->max;
        report = delta >= chg->threshold || (at_end && delta > 0);
    }

    if (report) {
        chg->last = value;
        chg->has_last = true;
    }
    return report;
}
