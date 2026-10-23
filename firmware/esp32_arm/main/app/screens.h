#ifndef SCREENS_H
#define SCREENS_H

/**
 * @file screens.h
 * @brief Every screen of the menu app (16x2 characters), declared with lcd_ui (ui.h).
 *
 * Only the UI task calls these (it owns the display and lcd_ui). screens_draw() builds
 * the current screen (ui_screen_begin() ... ui_screen_end()) and keeps the handles of the
 * widgets that change; screens_update() and screens_draw_joint_values() pass the new
 * values to the ui_*_set() functions, which send only the cells that changed.
 *
 * JOINTS shows ONE joint (app_state_t::joint_index): its angle, its position "k/6" and the
 * mode tag (NAV / CTL / LEC) on row 0, and a bar of the angle within the joint limits on
 * row 1, or a text instead of the bar (app_joint_row1_text(): a notice or the CTL takeover
 * hint). A joint commanded in CTL shows its command; otherwise the last angle read from the
 * servos (the bus task's mailbox).
 */

#include <stdint.h>
#include "esp_err.h"

#include "app_state.h"

#define SCREENS_NUM_JOINTS APP_JOINT_COUNT

/**
 * @brief One sample of the arm's joint angles (the message on the joints mailbox).
 */
typedef struct {
    float deg[SCREENS_NUM_JOINTS]; /**< Degrees, in esp32_arm joint order (servo IDs 1..6). */
} joint_angles_t;

/**
 * @brief Draws the whole current screen (s->screen) from scratch.
 *
 * Every handle of the previous screen becomes invalid. On the joints screen the fields
 * show the last sample received (blank before the first one).
 *
 * @param s UI state.
 * @return ESP_OK, the lcd_ui layout error, or the first drawing error.
 */
esp_err_t screens_draw(const app_state_t *s);

/**
 * @brief Redraws only the parts named by APP_ACT_DRAW_MENU / _INPUT / _TEXT / _CALIB /
 *        _POWER / _SCROLL / _JOINT.
 *
 * Flags that do not belong to the current screen are ignored. A change that needs other
 * widgets (the MENSAJES view switching between message and typing) rebuilds the screen;
 * INFO scrolling only moves its list window; a JOINTS change rewrites its row and bar.
 *
 * @param s       UI state.
 * @param actions APP_ACT_* flags from app_handle_event().
 * @return ESP_OK, or the first drawing error.
 */
esp_err_t screens_update(const app_state_t *s, uint32_t actions);

/**
 * @brief Joints screen: shows the latest sample of the joint on screen (unless it is held
 *        at a command) and repaints only the cells that changed.
 *
 * @param s      UI state.
 * @param angles Latest sample (also kept for the next JOINTS screen drawn).
 * @return ESP_OK; ESP_ERR_INVALID_STATE if the joints screen is not the one shown;
 *         or the first drawing error.
 */
esp_err_t screens_draw_joint_values(const app_state_t *s, const joint_angles_t *angles);

/**
 * @brief Window positions of the INFO list (lines - rows + 1), for app_set_info_positions().
 *
 * Builds the INFO lines from ui_get_display_info() (works before ui_init()).
 *
 * @return Positions, >= 1.
 */
unsigned screens_info_positions(void);

#endif // SCREENS_H
