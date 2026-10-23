# Pure cores (arm_state.c, arm_limits.c, frame_parser.c, m_payload.c, servo_bus.c,
# servo_units.c, app/app_state.c, nav/, pot/, button/*_core.c) are host-tested in test_host/;
# the adapters (arm_bus.c, ui_panel.c, sensor/, button/button_esp32.c, app/screens.c) talk to
# ESP-IDF or lcd_ui. esp32_arm.c only composes them.
idf_component_register(SRCS "esp32_arm.c"
                            "servo_units.c"
                            "frame_parser.c"
                            "m_payload.c"
                            "servo_bus.c"
                            "arm_state.c"
                            "arm_limits.c"
                            "arm_bus.c"
                            "ui_panel.c"
                            "app/app_state.c"
                            "app/screens.c"
                            "nav/pot_nav.c"
                            "pot/pot_core.c"
                            "sensor/pot_sensor.c"
                            "button/pulsador_core.c"
                            "button/button_core.c"
                            "button/button_esp32.c"
                    INCLUDE_DIRS "." "app" "nav" "pot" "sensor" "button"
                    REQUIRES lcd_ui esp_driver_uart esp_driver_gpio esp_adc esp_timer)
