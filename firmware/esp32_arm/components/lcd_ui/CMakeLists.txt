# lcd_ui: declarative screen builder on top of the lcd component.
# core/ is pure C (host-tested); ui.c draws through lcd.h only.
idf_component_register(SRCS "ui.c"
                            "core/ui_core.c"
                    INCLUDE_DIRS "." "core"
                    REQUIRES lcd
                    PRIV_REQUIRES log)
