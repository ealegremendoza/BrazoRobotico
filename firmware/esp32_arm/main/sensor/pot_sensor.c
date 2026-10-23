/**
 * @file pot_sensor.c
 * @brief ADC adapter of the potentiometer reader (see pot_sensor.h).
 */

#include "pot_sensor.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "pot_core.h"  // POT_RAW_MAX

#define POT_ADC_UNIT ADC_UNIT_1                // ADC1: ADC2 conflicts with WiFi
#define POT_ADC_ATTEN ADC_ATTEN_DB_12          // full range ~0..3.1 V (saturates above)
#define POT_ADC_BITWIDTH ADC_BITWIDTH_DEFAULT  // 12 bits on the ESP32: 0..4095

/**
 * @brief State of one pot's channel.
 */
typedef struct {
    adc_channel_t channel;  // ADC1 channel
    adc_cali_handle_t cali; // NULL if calibration is unavailable
    int mv_min;             // calibrated mV of counts 0
    int mv_max;             // calibrated mV of counts POT_RAW_MAX
} pot_chan_t;

static adc_oneshot_unit_handle_t s_adc;  // ADC1 oneshot unit, shared by every channel
static pot_chan_t s_chan[POT_SENSOR_MAX];
static size_t s_count;                   // configured channels

/**
 * @brief Deletes a calibration scheme created by cali_create().
 *
 * @param cali Handle (not NULL).
 */
static void cali_delete(adc_cali_handle_t cali)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_delete_scheme_curve_fitting(cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_delete_scheme_line_fitting(cali);
#else
    (void)cali;
#endif
}

/**
 * @brief Creates the calibration of one channel (line fitting on the ESP32) and stores the
 *        mV of both ends of the count range.
 *
 * On failure ch->cali stays NULL and the channel reports counts only.
 *
 * @param ch Channel state.
 * @return ESP_OK, ESP_ERR_NOT_SUPPORTED (no scheme or eFuse values), or the driver error.
 */
static esp_err_t cali_create(pot_chan_t *ch)
{
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;

    ch->cali = NULL;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    const adc_cali_curve_fitting_config_t cfg = {
        .unit_id = POT_ADC_UNIT,
        .chan = ch->channel,
        .atten = POT_ADC_ATTEN,
        .bitwidth = POT_ADC_BITWIDTH,
    };
    err = adc_cali_create_scheme_curve_fitting(&cfg, &ch->cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    const adc_cali_line_fitting_config_t cfg = {
        .unit_id = POT_ADC_UNIT,
        .atten = POT_ADC_ATTEN,
        .bitwidth = POT_ADC_BITWIDTH,
#if CONFIG_IDF_TARGET_ESP32
        .default_vref = 1100, // mV, used only if the eFuse holds no Vref or Two Point values
#endif
    };
    err = adc_cali_create_scheme_line_fitting(&cfg, &ch->cali);
#endif
    if (err != ESP_OK) {
        ch->cali = NULL;
        return err;
    }

    err = adc_cali_raw_to_voltage(ch->cali, 0, &ch->mv_min);
    if (err == ESP_OK) {
        err = adc_cali_raw_to_voltage(ch->cali, POT_RAW_MAX, &ch->mv_max);
    }
    if (err == ESP_OK && ch->mv_max <= ch->mv_min) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err != ESP_OK) {
        cali_delete(ch->cali);
        ch->cali = NULL;
    }
    return err;
}

/**
 * @brief Creates the ADC1 oneshot unit, configures every channel and its calibration.
 *
 * @param channels ADC1 channels, one per pot.
 * @param n        Number of channels, 1..POT_SENSOR_MAX.
 * @return ESP_OK, ESP_ERR_INVALID_ARG/STATE, or the ADC driver error.
 */
esp_err_t pot_sensor_init(const adc_channel_t *channels, size_t n)
{
    if (channels == NULL || n == 0 || n > POT_SENSOR_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_adc != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = POT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        s_adc = NULL;
        return err;
    }

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = POT_ADC_ATTEN,
        .bitwidth = POT_ADC_BITWIDTH,
    };
    for (size_t i = 0; i < n; i++) {
        s_chan[i].channel = channels[i];
        err = adc_oneshot_config_channel(s_adc, channels[i], &chan_cfg);
        if (err != ESP_OK) {
            for (size_t k = 0; k < i; k++) {
                if (s_chan[k].cali != NULL) {
                    cali_delete(s_chan[k].cali);
                    s_chan[k].cali = NULL;
                }
            }
            adc_oneshot_del_unit(s_adc);
            s_adc = NULL;
            return err;
        }
        cali_create(&s_chan[i]);  // not fatal: see pot_sensor_has_cali()
    }
    s_count = n;
    return ESP_OK;
}

/**
 * @brief Reads the raw counts of one pot.
 *
 * @param id  Pot index, 0..n-1.
 * @param raw Output: counts.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or the ADC driver error.
 */
esp_err_t pot_sensor_read(size_t id, int *raw)
{
    if (id >= s_count || raw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return adc_oneshot_read(s_adc, s_chan[id].channel, raw);
}

/**
 * @brief Converts counts to calibrated millivolts.
 *
 * @param id     Pot index.
 * @param counts ADC counts.
 * @param mv     Output: millivolts.
 * @return true if the channel is calibrated and the conversion worked.
 */
bool pot_sensor_to_mv(size_t id, int counts, int *mv)
{
    if (id >= s_count || mv == NULL || s_chan[id].cali == NULL) {
        return false;
    }
    return adc_cali_raw_to_voltage(s_chan[id].cali, counts, mv) == ESP_OK;
}

/**
 * @brief Tells whether a channel has calibration and gives its readable mV range.
 *
 * @param id     Pot index.
 * @param mv_min Output (may be NULL): mV of counts 0.
 * @param mv_max Output (may be NULL): mV of counts 4095.
 * @return true if the channel is calibrated.
 */
bool pot_sensor_has_cali(size_t id, int *mv_min, int *mv_max)
{
    if (id >= s_count || s_chan[id].cali == NULL) {
        return false;
    }
    if (mv_min != NULL) {
        *mv_min = s_chan[id].mv_min;
    }
    if (mv_max != NULL) {
        *mv_max = s_chan[id].mv_max;
    }
    return true;
}

/**
 * @brief GPIO of a pot's channel.
 *
 * @param id Pot index.
 * @return GPIO number, or -1.
 */
int pot_sensor_gpio(size_t id)
{
    int gpio = -1;

    if (id < s_count) {
        adc_oneshot_channel_to_io(POT_ADC_UNIT, s_chan[id].channel, &gpio);
    }
    return gpio;
}
