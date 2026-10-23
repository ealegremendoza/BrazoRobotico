# 1602 I2C LCD driver (HD44780 + PCF8574), copied unchanged from ../lcd_i2c_lib_idf/main.
# Public API: lcd.h (lcd/) and the pure lcd_core.h (core/). The HAL is private.
idf_component_register(SRCS "lcd/lcd.c"
                            "core/lcd_core.c"
                            "hal/lcd_hal_esp32.c"
                    INCLUDE_DIRS "lcd" "core"
                    PRIV_INCLUDE_DIRS "hal"
                    PRIV_REQUIRES esp_driver_i2c log esp_rom)
