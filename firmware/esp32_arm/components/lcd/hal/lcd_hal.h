#ifndef LCD_HAL_H
#define LCD_HAL_H

/**
 * @file lcd_hal.h
 * @brief Hardware layer of the LCD: I2C bus and address, raw PCF8574 writes, waits, mutex.
 *
 * The LCD sits behind a PCF8574: every I2C byte sets its 8 outputs (RS, RW, E, backlight
 * and D4..D7). It is write-only. Port, pins, clock and address are in lcd_pins.h.
 *
 * None of these functions may be called from an ISR: they can all block.
 */

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief I2C settings and pins the HAL uses (from lcd_pins.h), for lcd_get_info().
 */
typedef struct {
    int i2c_port;     /**< Numeric i2c_port_num_t value. */
    int i2c_addr;     /**< 7-bit address in use (after the scan fallback), else configured. */
    int i2c_clock_hz; /**< SCL clock in Hz. */
    int pin_sda;      /**< SDA GPIO. */
    int pin_scl;      /**< SCL GPIO. */
} lcd_hal_config_t;

/**
 * @brief Reports the I2C port, address, clock and pins. No hardware access.
 *
 * Call it with the lock held: the address in use changes in lcd_hal_init().
 *
 * @param out Output (not NULL).
 */
void lcd_hal_get_config(lcd_hal_config_t *out);

/**
 * @brief Creates the I2C bus (internal pull-ups off), finds the PCF8574 and adds it.
 *
 * Uses the configured address if it answers; otherwise scans the bus and, if exactly one
 * device answers, uses that one (with a warning in the log).
 *
 * @return ESP_OK; ESP_ERR_NOT_FOUND if there is no single candidate; or the I2C error
 *         (everything undone).
 */
esp_err_t lcd_hal_init(void);

/**
 * @brief Removes the device and frees the I2C bus.
 */
void lcd_hal_deinit(void);

/**
 * @brief Sends raw bytes to the PCF8574 in one I2C transaction.
 *
 * @param buf Bytes; each one sets the 8 outputs (E pulses already encoded).
 * @param len Number of bytes (0 sends nothing).
 * @return ESP_OK, or the I2C error (NACK, timeout).
 */
esp_err_t lcd_hal_write(const uint8_t *buf, size_t len);

/**
 * @brief Blocks the calling task for at least ms milliseconds.
 *
 * @param ms Milliseconds.
 */
void lcd_hal_delay_ms(uint32_t ms);

/**
 * @brief Busy-waits (does not release the CPU). For waits under 1 ms.
 *
 * @param us Microseconds.
 */
void lcd_hal_delay_us(uint32_t us);

/**
 * @brief Takes the driver mutex (a mutex, not a critical section: I2C transfers block).
 *
 * The mutex is created on first use (static storage) and never deleted, so this is safe
 * even before lcd_hal_init() or after lcd_hal_deinit().
 */
void lcd_hal_lock(void);

/**
 * @brief Releases the mutex taken with lcd_hal_lock().
 */
void lcd_hal_unlock(void);

#endif // LCD_HAL_H
