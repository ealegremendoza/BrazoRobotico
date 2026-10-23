#ifndef LCD_PINS_H
#define LCD_PINS_H

/**
 * @file lcd_pins.h
 * @brief I2C port, pins, clock and address of the 1602 LCD's PCF8574 backpack.
 *
 * Same scheme as esp32_arm's board_pins.h: configurable values come from Kconfig
 * (menu "LCD I2C Configuration"); fixed ones are defined here. Defaults are the wiring
 * verified on hardware (see README.md): SDA 25 and SCL 26 through an HW-221 (TXS0108E)
 * level shifter. They avoid GPIO1/3 (PC link, UART0), 14/27 (console, UART1) and 16/17
 * (servo bus, UART2) of esp32_arm, and the TFT pins (5, 18, 21, 22, 23); neither is a
 * strapping pin.
 */

#include "driver/i2c_types.h"
#include "sdkconfig.h"

// Do not modify: 100 kHz is the PCF8574 maximum (fSCL), verified on hardware.
#define LCD_I2C_PORT I2C_NUM_0
#define LCD_I2C_CLOCK_HZ 100000
#define LCD_I2C_TIMEOUT_MS 50

#define LCD_SDA_PIN CONFIG_LCD_I2C_SDA_GPIO
#define LCD_SCL_PIN CONFIG_LCD_I2C_SCL_GPIO
#define LCD_I2C_ADDR CONFIG_LCD_I2C_ADDR  // 7 bits; scan fallback if it does not answer

#endif // LCD_PINS_H
