/**
 * @file lcd_hal_esp32.c
 * @brief ESP32 implementation of the LCD hardware layer (see lcd_hal.h).
 *
 * Wiring verified with lcd_i2c_test_idf (see README.md): ESP32 -> HW-221 (TXS0108E) side A
 * (3.3 V) -> side B (5 V) -> PCF8574 backpack at 5 V.
 */

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lcd_core.h"  // lcd_pick_addr()
#include "lcd_hal.h"
#include "lcd_pins.h"

#define SCAN_FIRST 0x08  // non-reserved 7-bit addresses
#define SCAN_LAST 0x77
#define SCAN_MAX 16      // devices remembered by the scan

// The only log of the library: which address the scan fell back to (diagnostics).
static const char *TAG = "lcd_hal";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static int s_addr = -1;  // address in use; -1 = no device
static SemaphoreHandle_t s_mutex;
static StaticSemaphore_t s_mutex_buf;

/**
 * @brief Finds the LCD's address on the bus.
 *
 * The configured address is used if it answers. Otherwise the whole bus is scanned
 * (logging every address that answers) and lcd_pick_addr() decides.
 *
 * @return The 7-bit address, or -1 if there is no single candidate.
 */
static int find_addr(void)
{
    if (i2c_master_probe(s_bus, LCD_I2C_ADDR, LCD_I2C_TIMEOUT_MS) == ESP_OK) {
        return LCD_I2C_ADDR;
    }

    ESP_LOGW(TAG, "0x%02X does not answer; scanning SDA=%d SCL=%d", LCD_I2C_ADDR, LCD_SDA_PIN,
             LCD_SCL_PIN);
    uint8_t found[SCAN_MAX];
    size_t n = 0;
    for (int a = SCAN_FIRST; a <= SCAN_LAST; a++) {
        if (i2c_master_probe(s_bus, (uint16_t)a, LCD_I2C_TIMEOUT_MS) != ESP_OK) {
            continue;
        }
        ESP_LOGI(TAG, "0x%02X answers", a);
        if (n < SCAN_MAX) {
            found[n++] = (uint8_t)a;
        }
    }

    int addr = lcd_pick_addr(LCD_I2C_ADDR, found, n);
    if (addr < 0) {
        ESP_LOGE(TAG, "%u device(s) found: cannot choose", (unsigned)n);
    } else {
        ESP_LOGW(TAG, "using 0x%02X instead of 0x%02X", addr, LCD_I2C_ADDR);
    }
    return addr;
}

/**
 * @brief Reports the I2C port, address, clock and pins from lcd_pins.h.
 *
 * @param out Output.
 */
void lcd_hal_get_config(lcd_hal_config_t *out)
{
    *out = (lcd_hal_config_t){
        .i2c_port = (int)LCD_I2C_PORT,
        .i2c_addr = s_addr >= 0 ? s_addr : LCD_I2C_ADDR,
        .i2c_clock_hz = LCD_I2C_CLOCK_HZ,
        .pin_sda = LCD_SDA_PIN,
        .pin_scl = LCD_SCL_PIN,
    };
}

/**
 * @brief Creates the I2C bus, finds the LCD and adds the device.
 *
 * Internal pull-ups stay disabled: the HW-221 (TXS0108E) has its own, and TI advises
 * against adding more on side A.
 *
 * @return ESP_OK, ESP_ERR_NOT_FOUND or the I2C error.
 */
esp_err_t lcd_hal_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = LCD_I2C_PORT,
        .sda_io_num = LCD_SDA_PIN,
        .scl_io_num = LCD_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        return err;
    }

    int addr = find_addr();
    if (addr < 0) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = (uint16_t)addr,
        .scl_speed_hz = LCD_I2C_CLOCK_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return err;
    }
    s_addr = addr;
    return ESP_OK;
}

/**
 * @brief Removes the device and frees the I2C bus.
 */
void lcd_hal_deinit(void)
{
    i2c_master_bus_rm_device(s_dev);
    s_dev = NULL;
    i2c_del_master_bus(s_bus);
    s_bus = NULL;
    s_addr = -1;
}

/**
 * @brief Sends raw bytes to the PCF8574 in one I2C transaction.
 *
 * @param buf Bytes.
 * @param len Number of bytes.
 * @return ESP_OK, or the I2C error.
 */
esp_err_t lcd_hal_write(const uint8_t *buf, size_t len)
{
    if (len == 0) {
        return ESP_OK;
    }
    return i2c_master_transmit(s_dev, buf, len, LCD_I2C_TIMEOUT_MS);
}

/**
 * @brief Blocks the calling task for at least ms milliseconds.
 *
 * Rounds up to whole ticks and adds one: pdMS_TO_TICKS() truncates (at the default
 * CONFIG_FREERTOS_HZ = 100, pdMS_TO_TICKS(2) is 0, so the 2 ms Clear wait would not wait
 * at all), and vTaskDelay(n) only guarantees n - 1 full tick periods, because the first
 * tick may arrive right after the call.
 *
 * @param ms Milliseconds.
 */
void lcd_hal_delay_ms(uint32_t ms)
{
    TickType_t ticks = (TickType_t)((ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS);
    vTaskDelay(ticks + 1);
}

/**
 * @brief Busy-waits.
 *
 * @param us Microseconds.
 */
void lcd_hal_delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

/**
 * @brief Takes the driver mutex, creating it on first use.
 *
 * Creating it here too lets the API check "not initialized" under the lock even before
 * the first lcd_init(). Race-free only if the first call is not concurrent; lcd_init()
 * is normally called once from app_main.
 */
void lcd_hal_lock(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);  // static storage: cannot fail
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

/**
 * @brief Releases the mutex taken with lcd_hal_lock().
 */
void lcd_hal_unlock(void)
{
    xSemaphoreGive(s_mutex);
}
