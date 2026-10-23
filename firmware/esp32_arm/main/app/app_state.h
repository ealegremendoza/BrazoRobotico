#ifndef APP_STATE_H
#define APP_STATE_H

/**
 * @file app_state.h
 * @brief Pure UI logic of the menu app: key decoding and the screen state machine, driven by
 *        the serial console, a potentiometer and a pushbutton.
 *
 * No ESP-IDF or FreeRTOS headers: everything here is tested on the PC
 * (test_host/test_app_state.c). The flow on the target is:
 *
 *   input_task: UART byte -> app_decode_byte() -> app_event_t ----------+
 *   panel_task: pot percent change -> APP_EV_POT                         +-> event queue
 *               button_core short / long press -> APP_EV_BTN_SHORT/_LONG +
 *   bus task:   arm state changes, CTL granted / denied -> APP_EV_ARM_STATE / _CTL_* +
 *   ui_task:    app_event_t -> app_handle_event() -> APP_ACT_* flags -> screens_*(), bus requests
 *
 * The decoder does not know the current screen: it only classifies bytes (char,
 * Enter, Backspace, arrow, other). Mapping 'w'/'s'/'q' to up/down/back happens in
 * app_handle_event(), because on the text screen the same bytes are typed text.
 *
 * Key map outside the text screen: 'w'/'W' or arrow up = up, 's'/'S' or arrow
 * down = down, Enter = select, 'q'/'Q' = back. Every other key is ignored. On the
 * 16x2 LCD, up/down also scroll INFO (lines, app_state_t::scroll) and pick the joint on
 * JOINTS (app_state_t::joint_index).
 *
 * Pot (APP_EV_POT, percent): RELATIVE navigation (pot_nav.h) on MENU (selection), INFO
 * (scroll) and JOINTS in NAV mode (joint); clamped at the ends, anchored on entering a
 * screen or a mode so the cursor never jumps. JOINTS in CTL mode maps the percent to the
 * joint angle instead (absolute, with soft takeover). Other screens ignore it.
 *
 * Real arm (esp32_arm, doc/md/bitacora-E10-panel-lcd.md): the servo bus task owns the safety
 * state, this module only the screens (DS4). JOINTS asks for CTL (APP_ACT_CTL_REQUEST) and
 * enters it only when the bus task grants it (APP_EV_CTL_GRANTED, with the joint's current
 * angle). While a ROS session runs (APP_EV_ARM_STATE = operating, D1) JOINTS is read only:
 * the mode tag reads LEC and Enter / a short press shows the reason on row 1 for
 * APP_NOTICE_TICKS (DS12); HOME shows "TAREA EN CURSO" (DS16). In CTL the pot is absolute
 * (0..100 % = the joint limits, DS6) with a soft takeover (DS13): nothing is commanded until
 * the pot passes through the joint's current angle (row 1 tells which way to turn); a jog
 * starts from the current angle and disengages the pot again.
 *
 * Button: a short press is Enter (HOME -> MENU, MENU -> the selected entry, JOINTS toggles
 * NAV/CTL, CALIBRAR start/done, MENSAJES submit or empty -> MENU), except on INFO (-> MENU,
 * like 'q', because Enter does nothing there) and APAGAR (ignored, like every key). A long
 * press goes to HOME from every screen and mode (it also cancels a running calibration and
 * the APAGAR countdown).
 *
 * Screen flow:
 *
 *   HOME --Enter--> MENU --Enter--> JOINTS | CALIB | TEXT ("MENSAJES") | INFO | POWER
 *   HOME <---q----- MENU <---q----- JOINTS | CALIB | TEXT (empty Enter) | INFO
 *   POWER (mock "APAGANDO SISTEMA" countdown, keys ignored) --timeout--> HOME
 *   any screen --long press--> HOME
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pot_nav.h"

/**
 * Longest text line the user can type (extra characters are ignored): the 16x2 panel,
 * so a submitted text always fits the MENSAJES message (2 rows of 16).
 */
#define APP_TEXT_MAX 32

/**
 * app_state_t::scroll stays in 0..APP_SCROLL_WRAP-1. 720720 is the least common multiple
 * of 1..16, so for any page or line count up to 16, scroll % count keeps stepping by one
 * across the wrap (up from 0 lands on the last page).
 */
#define APP_SCROLL_WRAP 720720u

/** Number of entries of the home menu. */
#define APP_MENU_COUNT 5

/** Joints of the arm (J1..J6, esp32_arm servo IDs 1..6). */
#define APP_JOINT_COUNT 6

/** CTL soft takeover: the pot engages within this many degrees of the joint's angle. */
#define APP_CTL_ENGAGE_DEG 2

/** Ticks a row-1 notice of JOINTS stays on screen (DS12: ~1.5 s). */
#define APP_NOTICE_TICKS 15

/** Ticks to wait for the bus task's answer to a CTL request before giving up (1 s). */
#define APP_CTL_WAIT_TICKS 10

/** JOINTS in CTL mode: degrees one console up/down jogs the commanded angle. */
#define APP_JOINT_JOG_DEG 5

/** Default pot navigation: percent of travel per item, and the extra percent to reverse. */
#define APP_POT_STEP_PCT_DEFAULT 8
#define APP_POT_HYST_PCT_DEFAULT 2

/** Period of APP_EV_TICK, sent by the UI task while app_needs_tick() is true. */
#define APP_TICK_MS 100

/** Calibration (SIMULATED): 4 steps x 10 ticks x 100 ms = 4 s in total. */
#define APP_CALIB_STEPS 4
#define APP_CALIB_TICKS_PER_STEP 10
#define APP_CALIB_TOTAL_TICKS (APP_CALIB_STEPS * APP_CALIB_TICKS_PER_STEP)

/** Power-off countdown (MOCK): 5 s, one displayed number per second. */
#define APP_POWER_SECONDS 5
#define APP_TICKS_PER_SECOND (1000 / APP_TICK_MS)
#define APP_POWER_TOTAL_TICKS (APP_POWER_SECONDS * APP_TICKS_PER_SECOND)

/**
 * @brief Screens of the app.
 */
typedef enum {
    APP_SCREEN_HOME,   /**< Title, arm drawing, "Enter: menu". */
    APP_SCREEN_MENU,   /**< The 5 options. */
    APP_SCREEN_JOINTS, /**< One joint: angle, NAV/CTL/LEC tag and a bar (real feedback). */
    APP_SCREEN_CALIB,  /**< Calibration (SIMULATED). */
    APP_SCREEN_TEXT,   /**< "MENSAJES": text typed on the serial monitor. */
    APP_SCREEN_INFO,   /**< Driver, I2C, pins and lcd_ui limits, scrolled with up/down. */
    APP_SCREEN_POWER,  /**< "APAGANDO SISTEMA" countdown (MOCK: nothing is turned off). */
} app_screen_t;

/**
 * @brief Menu entries, in display order (index = app_state_t::menu_index).
 */
typedef enum {
    APP_MENU_JOINTS,
    APP_MENU_CALIB,
    APP_MENU_TEXT,
    APP_MENU_INFO,
    APP_MENU_POWER, /**< Always last. */
} app_menu_item_t;

/**
 * @brief Phases of the (SIMULATED) calibration screen.
 */
typedef enum {
    APP_CALIB_CONFIRM, /**< "Enter: iniciar, q: volver". */
    APP_CALIB_RUNNING, /**< Steps advance on every APP_EV_TICK. */
    APP_CALIB_DONE,    /**< "Calibracion OK (simulada)". */
} app_calib_phase_t;

/**
 * @brief Modes of the JOINTS screen.
 */
typedef enum {
    APP_JOINT_NAV,  /**< The pot (and up/down) pick the joint. Default on entry. */
    APP_JOINT_WAIT, /**< CTL requested, waiting for the bus task (grant or denial). */
    APP_JOINT_CTL,  /**< CTL granted: the pot sets the joint angle; up/down jog it. */
} app_joint_mode_t;

/**
 * @brief Reason shown on JOINTS row 1 for APP_NOTICE_TICKS (DS12).
 */
typedef enum {
    APP_NOTICE_NONE,      /**< Nothing: the bar (or the takeover hint). */
    APP_NOTICE_SESSION,   /**< "TAREA EN CURSO": a ROS session runs (D1). */
    APP_NOTICE_NO_SERVO,  /**< The servos did not answer the read before CTL. */
    APP_NOTICE_NO_ANSWER, /**< The bus task did not answer the CTL request. */
} app_notice_t;

/**
 * @brief Why the bus task denied CTL (the value of APP_EV_CTL_DENIED).
 */
typedef enum {
    APP_CTL_DENIED_SESSION,  /**< A ROS session runs (OPERATING). */
    APP_CTL_DENIED_NO_SERVO, /**< The servos did not answer. */
    APP_CTL_DENIED_OTHER,    /**< Anything else (bad joint). */
} app_ctl_denied_t;

/**
 * @brief Event types: decoded keys, the calibration timer tick, the pot and the button.
 */
typedef enum {
    APP_EV_NONE,      /**< Nothing (never sent). */
    APP_EV_CHAR,      /**< Printable ASCII 0x20..0x7E, in app_event_t::ch. */
    APP_EV_ENTER,     /**< CR, LF or CRLF (one event per line end). */
    APP_EV_BACKSPACE, /**< 0x08 or 0x7F. */
    APP_EV_UP,        /**< Arrow up (ESC [ A or ESC O A). */
    APP_EV_DOWN,      /**< Arrow down (ESC [ B or ESC O B). */
    APP_EV_OTHER,     /**< Any other key (ignored by every screen). */
    APP_EV_TICK,      /**< Timer, every APP_TICK_MS while app_needs_tick() (from ui_task). */
    APP_EV_POT,       /**< Pot percent changed: 0..100 in app_event_t::value. */
    APP_EV_BTN_SHORT, /**< Button released before the long-press time. */
    APP_EV_BTN_LONG,  /**< Button held for the long-press time (once, while held). */
    APP_EV_ARM_STATE, /**< Bus task: value 1 = OPERATING (ROS session), 0 = STOPPED. */
    APP_EV_CTL_GRANTED, /**< Bus task granted CTL: value = the joint's angle, whole degrees. */
    APP_EV_CTL_DENIED,  /**< Bus task denied CTL: value = app_ctl_denied_t. */
} app_event_type_t;

/**
 * @brief One event for the UI task (the message on the event queue).
 */
typedef struct {
    app_event_type_t type; /**< What happened. */
    char ch;              /**< The character for APP_EV_CHAR; 0 otherwise. */
    int value;            /**< APP_EV_POT percent, APP_EV_ARM_STATE / _CTL_* value; else 0. */
} app_event_t;

/**
 * @brief Byte decoder state (one per input stream).
 */
typedef struct {
    bool last_cr; /**< Previous byte was CR: a following LF is the same Enter. */
    uint8_t esc;  /**< 0 = normal, 1 = after ESC, 2 = inside ESC [ / ESC O. */
} app_decoder_t;

/**
 * @brief Whole UI state, owned by the UI task.
 */
typedef struct {
    app_screen_t screen;                /**< Current screen. */
    unsigned menu_index;               /**< Selected menu entry, 0..APP_MENU_COUNT-1. */
    char line[APP_TEXT_MAX + 1];        /**< Text being typed (NUL-terminated). */
    size_t line_len;                   /**< strlen(line). */
    char last_text[APP_TEXT_MAX + 1];   /**< Last submitted line ("" = none yet). */
    app_calib_phase_t calib_phase;      /**< Calibration phase. */
    unsigned calib_ticks;              /**< Ticks elapsed, 0..APP_CALIB_TOTAL_TICKS. */
    unsigned power_ticks;              /**< Countdown ticks elapsed, 0..APP_POWER_TOTAL_TICKS. */
    unsigned scroll;                   /**< INFO line, 0..APP_SCROLL_WRAP-1; the screen
                                            takes it modulo its window positions. */
    unsigned info_positions;           /**< INFO window positions (pot clamps to them);
                                            1 = the pot does not scroll INFO. */
    unsigned joint_index;              /**< JOINTS: joint shown, 0..APP_JOINT_COUNT-1. */
    app_joint_mode_t joint_mode;        /**< JOINTS: NAV, WAIT or CTL. */
    int joint_cmd[APP_JOINT_COUNT];     /**< Last commanded angle (deg), valid if held. */
    bool joint_held[APP_JOINT_COUNT];   /**< Commanded in the current CTL: the joint shows
                                            joint_cmd instead of the live reading. Cleared
                                            when CTL ends. */
    int joint_min_deg[APP_JOINT_COUNT]; /**< Joint limits in whole degrees (inside the URDF
                                            ones, DS6); CTL maps pot 0..100 % onto them. */
    int joint_max_deg[APP_JOINT_COUNT]; /**< Upper limits, > joint_min_deg. */
    bool arm_operating;                /**< A ROS session runs: JOINTS is read only (D1). */
    int ctl_ref_deg;                   /**< CTL: angle the joint is at (grant reading or
                                            last command): where the pot must pass. */
    bool ctl_engaged;                  /**< CTL: the pot passed ctl_ref_deg and drives the
                                            joint (soft takeover, DS13). */
    unsigned wait_ticks;               /**< WAIT: ticks since the CTL request. */
    app_notice_t notice;               /**< JOINTS row-1 notice (APP_NOTICE_NONE = none). */
    unsigned notice_ticks;             /**< Ticks left for the notice. */
    int pot_percent;                   /**< Last pot percent (valid if has_pot). */
    bool has_pot;                      /**< At least one APP_EV_POT received. */
    pot_nav_t nav;                     /**< Relative pot navigation of the current screen. */
} app_state_t;

// Actions returned by app_handle_event() (bit flags); the UI task applies them.
#define APP_ACT_DRAW_SCREEN (1u << 0)  /**< Full redraw of app_state_t::screen. */
#define APP_ACT_DRAW_MENU (1u << 1)    /**< Menu: selection changed. */
#define APP_ACT_DRAW_INPUT (1u << 2)   /**< Text: the input line changed. */
#define APP_ACT_DRAW_TEXT (1u << 3)    /**< Text: a new line was submitted. */
#define APP_ACT_DRAW_CALIB (1u << 4)   /**< Calibration: progress changed. */
#define APP_ACT_DRAW_POWER (1u << 5)   /**< Power countdown: progress changed. */
#define APP_ACT_DRAW_SCROLL (1u << 6)  /**< Info: app_state_t::scroll changed. */
#define APP_ACT_DRAW_JOINT (1u << 7)   /**< Joints: joint, mode or commanded angle changed. */
#define APP_ACT_SERVO (1u << 8)        /**< Send joint_cmd[joint_index] to the bus (CTL). */
#define APP_ACT_CTL_REQUEST (1u << 9)  /**< Ask the bus task for CTL on joint_index. */
#define APP_ACT_CTL_EXIT (1u << 10)    /**< Tell the bus task that CTL ended. */

/** Menu labels, indexed by app_menu_item_t (ASCII only: ROM A00 has no accents). */
extern const char *const app_menu_labels[APP_MENU_COUNT];

/** Names of the simulated calibration steps. */
extern const char *const app_calib_step_names[APP_CALIB_STEPS];

/**
 * Default joint limits in whole degrees: the corrected URDF ones (D5) rounded inward, used
 * until the bus task publishes its limits (app_set_joint_limits()).
 */
extern const int app_joint_default_min_deg[APP_JOINT_COUNT];
extern const int app_joint_default_max_deg[APP_JOINT_COUNT];

/**
 * @brief Resets a decoder.
 *
 * @param d Decoder.
 */
void app_decoder_init(app_decoder_t *d);

/**
 * @brief Decodes one byte from the serial monitor.
 *
 * - CR -> ENTER; LF -> ENTER unless it follows a CR (CRLF is one Enter).
 *   CR CR and LF LF are two Enters.
 * - 0x08 and 0x7F -> BACKSPACE. 0x20..0x7E -> CHAR.
 * - ESC [ A / ESC O A -> UP, ESC [ B / ESC O B -> DOWN; other escape sequences
 *   -> OTHER. A lone ESC produces nothing (if the next byte does not continue a
 *   sequence, that byte is decoded normally).
 * - Anything else (control characters, bytes >= 0x80) -> OTHER.
 *
 * @param d   Decoder.
 * @param b   Byte received.
 * @param out Output: the event, written only when the function returns true.
 * @return true if the byte completed an event.
 */
bool app_decode_byte(app_decoder_t *d, uint8_t b, app_event_t *out);

/**
 * @brief Initial state: home screen, first menu entry selected, no text, no pot reading,
 *        default pot navigation (APP_POT_*_PCT_DEFAULT), INFO not pot-scrollable.
 *
 * @param s State.
 */
void app_state_init(app_state_t *s);

/**
 * @brief Sets the pot navigation step and reverse hysteresis (Kconfig on the target).
 *
 * @param s        State.
 * @param step_pct Percent per item (>= 1).
 * @param hyst_pct Extra percent to reverse direction (>= 0).
 */
void app_set_pot_nav(app_state_t *s, int step_pct, int hyst_pct);

/**
 * @brief Tells the state machine how many window positions INFO has (for the pot clamp).
 *
 * @param s         State.
 * @param positions Window positions (lines - rows + 1); 0 counts as 1.
 */
void app_set_info_positions(app_state_t *s, unsigned positions);

/**
 * @brief Sets the joint limits (from the bus task's mailbox, already in whole degrees).
 *
 * A joint whose min is not below its max keeps its previous limits. In CTL the commanded
 * angle is not moved: the next command is clamped to the new limits.
 *
 * @param s       State.
 * @param min_deg Lower limit of each joint.
 * @param max_deg Upper limit of each joint.
 * @return true if any limit changed.
 */
bool app_set_joint_limits(app_state_t *s, const int min_deg[APP_JOINT_COUNT],
                          const int max_deg[APP_JOINT_COUNT]);

/**
 * @brief Converts limits in mrad to whole degrees rounded INWARD (DS6): the min up, the max
 *        down, so the panel never leaves the URDF range (-1640 mrad = -93.97 deg -> -93).
 *
 * @param min_mrad Lower limits, mrad.
 * @param max_mrad Upper limits, mrad.
 * @param min_deg  Output: lower limits, degrees.
 * @param max_deg  Output: upper limits, degrees.
 */
void app_limits_from_mrad(const int32_t min_mrad[APP_JOINT_COUNT],
                          const int32_t max_mrad[APP_JOINT_COUNT], int min_deg[APP_JOINT_COUNT],
                          int max_deg[APP_JOINT_COUNT]);

/**
 * @brief Converts an angle in mrad to degrees.
 *
 * @param mrad Angle in milliradians.
 * @return Degrees.
 */
double app_mrad_to_deg(int32_t mrad);

/**
 * @brief Converts whole degrees to mrad, rounded to the nearest one.
 *
 * @param deg Degrees.
 * @return Milliradians.
 */
int32_t app_deg_to_mrad(int deg);

/**
 * @brief Applies one event to the state.
 *
 * Menu navigation with keys WRAPS AROUND (down on the last entry selects the first one);
 * with the pot it clamps. The selection is kept when coming back to the menu. Entering a
 * screen resets it: calibration starts on its confirm prompt, the power countdown at 5 s,
 * the text input empty (the last submitted text is kept), INFO at scroll 0, JOINTS at J1 in
 * NAV mode; and the pot navigation is re-anchored at the current pot percent. On INFO,
 * up/down move scroll by one (wrapping at APP_SCROLL_WRAP) and return APP_ACT_DRAW_SCROLL.
 * During the power countdown every key is ignored; only ticks advance it, and at the end it
 * goes to HOME. A long press goes to HOME from anywhere.
 *
 * JOINTS: NAV mode: up/down pick the joint (wrap), the pot picks it (clamped), 'q' -> MENU;
 * Enter or a short press ask for CTL (APP_ACT_CTL_REQUEST, mode WAIT), or show the reason
 * when a ROS session runs. WAIT: APP_EV_CTL_GRANTED -> CTL, APP_EV_CTL_DENIED or
 * APP_CTL_WAIT_TICKS without an answer -> NAV with the reason; 'q' -> NAV. CTL mode: the pot
 * sets the angle (min..max of the joint) once engaged, up/down jog it by APP_JOINT_JOG_DEG
 * from the current angle (and disengage the pot), Enter, a short press or 'q' go back to NAV
 * (re-anchored). Every new command returns APP_ACT_SERVO; leaving CTL or WAIT (also by
 * leaving the screen) returns APP_ACT_CTL_EXIT.
 *
 * @param s  State.
 * @param ev Event.
 * @return APP_ACT_* flags (0 = nothing to do).
 */
uint32_t app_handle_event(app_state_t *s, const app_event_t *ev);

/**
 * @brief Tells the UI task whether it must send APP_EV_TICK every APP_TICK_MS.
 *
 * @param s State.
 * @return true while the simulated calibration runs, the power countdown is shown, a JOINTS
 *         notice is on screen or a CTL request waits for its answer.
 */
bool app_needs_tick(const app_state_t *s);

/**
 * @brief Current calibration step.
 *
 * @param s State.
 * @return 0..APP_CALIB_STEPS-1 while running; APP_CALIB_STEPS when done.
 */
unsigned app_calib_step(const app_state_t *s);

/**
 * @brief Calibration progress.
 *
 * @param s State.
 * @return 0..100 percent.
 */
unsigned app_calib_percent(const app_state_t *s);

/**
 * @brief Seconds left on the power countdown, as displayed.
 *
 * @param s State.
 * @return APP_POWER_SECONDS..1 (rounded up), 0 once finished.
 */
unsigned app_power_seconds_left(const app_state_t *s);

/**
 * @brief Power countdown progress.
 *
 * @param s State.
 * @return 0..100 percent.
 */
unsigned app_power_percent(const app_state_t *s);

/**
 * @brief Maps a pot percent to a joint angle: 0 % = min, 100 % = max, rounded.
 *
 * @param s       State (joint limits).
 * @param joint   Joint index (>= APP_JOINT_COUNT returns 0).
 * @param percent Pot percent (clamped to 0..100).
 * @return Whole degrees, min..max of the joint.
 */
int app_joint_deg_from_percent(const app_state_t *s, unsigned joint, int percent);

/**
 * @brief Where an angle sits in the joint range, for the bar: min = 0 %, max = 100 %.
 *
 * @param s     State (joint limits).
 * @param joint Joint index (>= APP_JOINT_COUNT returns 0).
 * @param deg   Angle in degrees (clamped to the range).
 * @return 0..100 percent, rounded.
 */
unsigned app_joint_percent(const app_state_t *s, unsigned joint, double deg);

/**
 * @brief JOINTS mode tag (3 cells): "NAV", "CTL", or "LEC" while CTL is not allowed (a ROS
 *        session runs, or a notice explains a denial, DS12).
 *
 * @param s State.
 * @return The tag.
 */
const char *app_joint_mode_tag(const app_state_t *s);

/**
 * @brief JOINTS row 1 text instead of the bar: the notice (DS12) or, in CTL before the pot
 *        engages, which way to turn it (DS13).
 *
 * @param s State.
 * @return The text (at most 16 chars), or NULL to show the bar.
 */
const char *app_joint_row1_text(const app_state_t *s);

/**
 * @brief Whether JOINTS shows the commanded angle (CTL, commanded) instead of the reading.
 *
 * @param s   State.
 * @param deg Output: the commanded angle, written only on true.
 * @return true if the joint on screen shows its command.
 */
bool app_joint_shows_command(const app_state_t *s, int *deg);

/**
 * @brief HOME row 1 (DS16): "Enter: menu", or "TAREA EN CURSO" during a ROS session.
 *
 * @param s State.
 * @return The text (14 cells at most, beside the arm drawing).
 */
const char *app_home_text(const app_state_t *s);

#endif // APP_STATE_H
