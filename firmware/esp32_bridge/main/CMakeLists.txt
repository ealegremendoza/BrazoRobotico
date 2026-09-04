idf_component_register(SRCS "esp32_bridge.c"
                    REQUIRES esp_driver_uart esp_driver_gpio
                    INCLUDE_DIRS ".")
