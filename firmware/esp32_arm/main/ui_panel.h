#ifndef UI_PANEL_H
#define UI_PANEL_H

/**
 * @file ui_panel.h
 * @brief Local panel (doc/md/bitacora-E10-panel-lcd.md): the menu app of pot_nav_lcd_idf on
 *        the 1602 I2C LCD, driven by a pot, a pushbutton and the console keyboard (UART1),
 *        moving the real arm through the servo bus task (CTL mode of JOINTS).
 *
 *   inputs                        pure cores                     outputs
 *   UART1 console (input_task) -> app_decode_byte() --+
 *   pot_sensor (panel_task) ----> pot_core -> percent -+-> event queue -> app_state (+pot_nav)
 *   button (IRQ + esp_timer) ---> button_core --------+       (ui_task)     |
 *   bus task (arm_bus.h) -------> state / CTL answers +                     +-> screens (lcd_ui)
 *   bus mailbox (positions, limits) -----------------------------------> +-> bus requests (CTL)
 *
 * Tasks: input (priority 5, console keys), panel (5, pot every 10 ms), ui (2, owns the
 * display); the button callbacks run in the esp_timer task. The display task has the LOWEST
 * priority: every I2C transfer blocks (a full screen ~12 ms), below the control loop.
 */

#include <stdint.h>
#include "esp_err.h"

#include "arm_bus.h"

/**
 * @brief Creates the event queue, starts the display, the pot, the button and the panel tasks.
 *
 * Nothing here is fatal: without the LCD, the ADC or the button the rest keeps working (the
 * failure is logged).
 *
 * @return ESP_OK, or ESP_ERR_NO_MEM if the event queue could not be created.
 */
esp_err_t ui_panel_start(void);

/**
 * @brief Bus task event callback (arm_bus_event_cb_t): forwards the event to the UI queue.
 *
 * Runs in the bus task: never blocks (a full queue drops the event, logged).
 *
 * @param ev    Event.
 * @param joint Joint of a CTL answer.
 * @param value Event value (state, angle in mrad, or denial reason).
 * @param ctx   Unused.
 */
void ui_panel_on_bus_event(arm_bus_event_t ev, unsigned joint, int32_t value, void *ctx);

#endif // UI_PANEL_H
