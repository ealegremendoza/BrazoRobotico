#ifndef POT_SENSOR_H
#define POT_SENSOR_H

/**
 * @file pot_sensor.h
 * @brief ADC adapter of the potentiometer reader: one ADC1 oneshot unit shared by up to
 *        POT_SENSOR_MAX channels, each with its own calibration (counts -> mV).
 *
 * Every pot is an index (id) into the channel list given to pot_sensor_init(). All
 * channels use 12 dB attenuation and 12 bits (0..4095). Calibration failure is not fatal:
 * that channel reports counts only (pot_sensor_to_mv() returns false).
 *
 * Only ADC1 pins (GPIO32..39) can be used: ADC2 cannot be read while WiFi is on.
 */

#include <stdbool.h>
#include <stddef.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

#define POT_SENSOR_MAX 4  // channels one sensor can hold

/**
 * @brief Creates the ADC1 oneshot unit, configures every channel and its calibration.
 *
 * @param channels ADC1 channels, one per pot (e.g. ADC_CHANNEL_6 = GPIO34).
 * @param n        Number of channels, 1..POT_SENSOR_MAX.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or the ADC driver error (calibration errors are
 *         not returned: see pot_sensor_has_cali()).
 */
esp_err_t pot_sensor_init(const adc_channel_t *channels, size_t n);

/**
 * @brief Reads the raw counts of one pot.
 *
 * @param id  Pot index, 0..n-1.
 * @param raw Output: counts, 0..4095.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or the ADC driver error.
 */
esp_err_t pot_sensor_read(size_t id, int *raw);

/**
 * @brief Converts counts to calibrated millivolts.
 *
 * @param id     Pot index.
 * @param counts ADC counts (usually filtered).
 * @param mv     Output: millivolts.
 * @return true if the channel is calibrated and the conversion worked.
 */
bool pot_sensor_to_mv(size_t id, int counts, int *mv);

/**
 * @brief Tells whether a channel has calibration and gives its readable mV range.
 *
 * @param id     Pot index.
 * @param mv_min Output (may be NULL): mV of counts 0, used as 0 %.
 * @param mv_max Output (may be NULL): mV of counts 4095, used as 100 %.
 * @return true if the channel is calibrated.
 */
bool pot_sensor_has_cali(size_t id, int *mv_min, int *mv_max);

/**
 * @brief GPIO of a pot's channel, for the log.
 *
 * @param id Pot index.
 * @return GPIO number, or -1.
 */
int pot_sensor_gpio(size_t id);

#endif // POT_SENSOR_H
