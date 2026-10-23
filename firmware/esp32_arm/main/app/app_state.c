/**
 * @file app_state.c
 * @brief Pure UI logic: key decoding and the screen state machine (see app_state.h).
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app_state.h"

#define CH_BS 0x08
#define CH_LF 0x0A
#define CH_CR 0x0D
#define CH_ESC 0x1B
#define CH_DEL 0x7F

// Decoder escape states.
#define ESC_NONE 0
#define ESC_START 1  // got ESC
#define ESC_SEQ 2    // got ESC [ or ESC O: parameter bytes until a final byte

const char *const app_menu_labels[APP_MENU_COUNT] = {
    [APP_MENU_JOINTS] = "JOINTS",
    [APP_MENU_CALIB] = "CALIBRAR",
    [APP_MENU_TEXT] = "MENSAJES",
    [APP_MENU_INFO] = "INFO",
    [APP_MENU_POWER] = "APAGAR",
};

// SIMULATED calibration: names only, nothing is measured or moved.
const char *const app_calib_step_names[APP_CALIB_STEPS] = {
    "Centrar joints",
    "Buscar limites",
    "Medir offsets",
    "Guardar datos",
};

// Corrected URDF limits (D5 table of bitacora-E10-panel-lcd.md), whole degrees rounded inward.
const int app_joint_default_min_deg[APP_JOINT_COUNT] = {-110, -100, -94, -95, -157, -10};
const int app_joint_default_max_deg[APP_JOINT_COUNT] = {110, 98, 90, 95, 155, 100};

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/** Row-1 texts of JOINTS (16 cells at most, ROM A00: no accents). */
static const char *const NOTICE_TEXTS[] = {
    [APP_NOTICE_NONE] = NULL,
    [APP_NOTICE_SESSION] = "TAREA EN CURSO",
    [APP_NOTICE_NO_SERVO] = "SERVO SIN RESP.",
    [APP_NOTICE_NO_ANSWER] = "SIN RESPUESTA",
};
#define HINT_TURN_UP "GIRAR >>"   // CTL, pot below the joint's angle
#define HINT_TURN_DOWN "<< GIRAR" // CTL, pot above the joint's angle

/**
 * @brief Navigation meaning of a key outside the text screen.
 */
typedef enum {
    NAV_NONE,
    NAV_UP,
    NAV_DOWN,
    NAV_SELECT,
    NAV_BACK,
} nav_t;

// ---------- decoder ----------

/**
 * @brief Resets a decoder.
 *
 * @param d Decoder.
 */
void app_decoder_init(app_decoder_t *d)
{
    d->last_cr = false;
    d->esc = ESC_NONE;
}

/**
 * @brief Writes an event.
 *
 * @param out  Output event.
 * @param type Event type.
 * @param ch   Character (APP_EV_CHAR) or 0.
 * @return true, so callers can `return emit(...)`.
 */
static bool emit(app_event_t *out, app_event_type_t type, char ch)
{
    out->type = type;
    out->ch = ch;
    return true;
}

/**
 * @brief Decodes one byte from the serial monitor (see app_state.h for the rules).
 *
 * @param d   Decoder.
 * @param b   Byte received.
 * @param out Output event (written only on true).
 * @return true if the byte completed an event.
 */
bool app_decode_byte(app_decoder_t *d, uint8_t b, app_event_t *out)
{
    bool after_cr = d->last_cr;
    d->last_cr = false;

    if (d->esc == ESC_START) {
        if (b == '[' || b == 'O') {
            d->esc = ESC_SEQ;
            return false;
        }
        d->esc = ESC_NONE;  // lone ESC: decode this byte normally
    } else if (d->esc == ESC_SEQ) {
        if (b >= 0x30 && b <= 0x3F) {
            return false;  // parameter byte, e.g. "1;5" in ESC [ 1 ; 5 A
        }
        d->esc = ESC_NONE;
        if (b >= 0x40 && b <= 0x7E) {
            if (b == 'A') {
                return emit(out, APP_EV_UP, 0);
            }
            if (b == 'B') {
                return emit(out, APP_EV_DOWN, 0);
            }
            return emit(out, APP_EV_OTHER, 0);  // right/left/home/...: only wakes
        }
        // Broken sequence: decode this byte normally.
    }

    if (b == CH_CR) {
        d->last_cr = true;
        return emit(out, APP_EV_ENTER, 0);
    }
    if (b == CH_LF) {
        if (after_cr) {
            return false;  // second half of CRLF
        }
        return emit(out, APP_EV_ENTER, 0);
    }
    if (b == CH_ESC) {
        d->esc = ESC_START;
        return false;
    }
    if (b == CH_BS || b == CH_DEL) {
        return emit(out, APP_EV_BACKSPACE, 0);
    }
    if (b >= 0x20 && b <= 0x7E) {
        return emit(out, APP_EV_CHAR, (char)b);
    }
    return emit(out, APP_EV_OTHER, 0);
}

// ---------- state machine ----------

/**
 * @brief Initial state.
 *
 * @param s State.
 */
void app_state_init(app_state_t *s)
{
    memset(s, 0, sizeof(*s));
    s->screen = APP_SCREEN_HOME;
    s->menu_index = APP_MENU_JOINTS;
    s->calib_phase = APP_CALIB_CONFIRM;
    s->joint_mode = APP_JOINT_NAV;
    s->info_positions = 1;
    s->notice = APP_NOTICE_NONE;
    memcpy(s->joint_min_deg, app_joint_default_min_deg, sizeof(s->joint_min_deg));
    memcpy(s->joint_max_deg, app_joint_default_max_deg, sizeof(s->joint_max_deg));
    pot_nav_init(&s->nav, APP_POT_STEP_PCT_DEFAULT, APP_POT_HYST_PCT_DEFAULT);
}

/**
 * @brief Sets the joint limits (whole degrees); invalid pairs are ignored.
 *
 * @param s       State.
 * @param min_deg Lower limits.
 * @param max_deg Upper limits.
 * @return true if any limit changed.
 */
bool app_set_joint_limits(app_state_t *s, const int min_deg[APP_JOINT_COUNT],
                          const int max_deg[APP_JOINT_COUNT])
{
    bool changed = false;
    for (unsigned j = 0; j < APP_JOINT_COUNT; j++) {
        if (min_deg[j] >= max_deg[j]) {
            continue;
        }
        if (s->joint_min_deg[j] != min_deg[j] || s->joint_max_deg[j] != max_deg[j]) {
            s->joint_min_deg[j] = min_deg[j];
            s->joint_max_deg[j] = max_deg[j];
            changed = true;
        }
    }
    return changed;
}

/**
 * @brief Converts an angle in mrad to degrees.
 *
 * @param mrad Milliradians.
 * @return Degrees.
 */
double app_mrad_to_deg(int32_t mrad)
{
    return (double)mrad * 180.0 / (1000.0 * M_PI);
}

/**
 * @brief Converts whole degrees to mrad (nearest).
 *
 * @param deg Degrees.
 * @return Milliradians.
 */
int32_t app_deg_to_mrad(int deg)
{
    return (int32_t)lround((double)deg * 1000.0 * M_PI / 180.0);
}

/**
 * @brief Converts limits in mrad to whole degrees rounded inward (DS6).
 *
 * @param min_mrad Lower limits, mrad.
 * @param max_mrad Upper limits, mrad.
 * @param min_deg  Output: lower limits, degrees (rounded up).
 * @param max_deg  Output: upper limits, degrees (rounded down).
 */
void app_limits_from_mrad(const int32_t min_mrad[APP_JOINT_COUNT],
                          const int32_t max_mrad[APP_JOINT_COUNT], int min_deg[APP_JOINT_COUNT],
                          int max_deg[APP_JOINT_COUNT])
{
    for (unsigned j = 0; j < APP_JOINT_COUNT; j++) {
        min_deg[j] = (int)ceil(app_mrad_to_deg(min_mrad[j]));
        max_deg[j] = (int)floor(app_mrad_to_deg(max_mrad[j]));
    }
}

/**
 * @brief Sets the pot navigation step and reverse hysteresis.
 *
 * @param s        State.
 * @param step_pct Percent per item.
 * @param hyst_pct Extra percent to reverse direction.
 */
void app_set_pot_nav(app_state_t *s, int step_pct, int hyst_pct)
{
    pot_nav_init(&s->nav, step_pct, hyst_pct);
    if (s->has_pot) {
        pot_nav_anchor(&s->nav, s->pot_percent);
    }
}

/**
 * @brief Sets the INFO window positions (for the pot clamp).
 *
 * @param s         State.
 * @param positions Window positions; 0 counts as 1.
 */
void app_set_info_positions(app_state_t *s, unsigned positions)
{
    s->info_positions = positions == 0 ? 1 : positions;
}

/**
 * @brief Anchors the pot navigation at the current pot percent (screen or mode entered).
 *
 * Without a reading yet, the first APP_EV_POT anchors it.
 *
 * @param s State.
 */
static void nav_reanchor(app_state_t *s)
{
    if (s->has_pot) {
        pot_nav_anchor(&s->nav, s->pot_percent);
    } else {
        pot_nav_reset(&s->nav);
    }
}

/**
 * @brief Maps a key to its navigation meaning (outside the text screen).
 *
 * @param ev Event.
 * @return The navigation action, NAV_NONE for keys without one.
 */
static nav_t nav_of(const app_event_t *ev)
{
    switch (ev->type) {
    case APP_EV_UP:
        return NAV_UP;
    case APP_EV_DOWN:
        return NAV_DOWN;
    case APP_EV_ENTER:
        return NAV_SELECT;
    case APP_EV_CHAR:
        switch (ev->ch) {
        case 'w':
        case 'W':
            return NAV_UP;
        case 's':
        case 'S':
            return NAV_DOWN;
        case 'q':
        case 'Q':
            return NAV_BACK;
        default:
            return NAV_NONE;
        }
    default:
        return NAV_NONE;
    }
}

/**
 * @brief Leaves CTL or WAIT for NAV (no-op in NAV): the joint shows its reading again.
 *
 * @param s State.
 * @return APP_ACT_CTL_EXIT if CTL was granted or requested, else 0.
 */
static uint32_t ctl_leave(app_state_t *s)
{
    if (s->joint_mode == APP_JOINT_NAV) {
        return 0;
    }
    s->joint_mode = APP_JOINT_NAV;
    s->ctl_engaged = false;
    memset(s->joint_held, 0, sizeof(s->joint_held));
    return APP_ACT_CTL_EXIT;
}

/**
 * @brief Shows a notice on JOINTS row 1 for APP_NOTICE_TICKS.
 *
 * @param s      State.
 * @param notice Notice.
 * @return APP_ACT_DRAW_JOINT.
 */
static uint32_t show_notice(app_state_t *s, app_notice_t notice)
{
    s->notice = notice;
    s->notice_ticks = APP_NOTICE_TICKS;
    return APP_ACT_DRAW_JOINT;
}

/**
 * @brief Switches to a screen and resets its per-visit state.
 *
 * @param s      State.
 * @param screen Screen to enter.
 * @return APP_ACT_DRAW_SCREEN.
 */
static uint32_t enter_screen(app_state_t *s, app_screen_t screen)
{
    uint32_t act = APP_ACT_DRAW_SCREEN;
    if (s->screen == APP_SCREEN_JOINTS) {
        act |= ctl_leave(s);  // the bus task must know CTL ended (no stuck grant, D2)
    }
    s->notice = APP_NOTICE_NONE;
    s->notice_ticks = 0;
    s->screen = screen;
    switch (screen) {
    case APP_SCREEN_CALIB:
        s->calib_phase = APP_CALIB_CONFIRM;
        s->calib_ticks = 0;
        break;
    case APP_SCREEN_TEXT:
        s->line[0] = '\0';
        s->line_len = 0;
        break;
    case APP_SCREEN_POWER:
        s->power_ticks = 0;
        break;
    case APP_SCREEN_JOINTS:
        s->joint_index = 0;
        s->joint_mode = APP_JOINT_NAV;
        break;
    case APP_SCREEN_INFO:
        s->scroll = 0;
        break;
    default:
        break;
    }
    nav_reanchor(s);
    return act;
}

/**
 * @brief Home: Enter opens the menu; everything else (including 'q') is ignored.
 *
 * @param s  State.
 * @param ev Event.
 * @return APP_ACT_* flags.
 */
static uint32_t handle_home(app_state_t *s, const app_event_t *ev)
{
    return nav_of(ev) == NAV_SELECT ? enter_screen(s, APP_SCREEN_MENU) : 0;
}

/**
 * @brief Menu: move (wrap-around), open the selected entry, or 'q' back to home.
 *
 * @param s  State.
 * @param ev Event.
 * @return APP_ACT_* flags.
 */
static uint32_t handle_menu(app_state_t *s, const app_event_t *ev)
{
    static const app_screen_t TARGET[APP_MENU_COUNT] = {
        [APP_MENU_JOINTS] = APP_SCREEN_JOINTS,
        [APP_MENU_CALIB] = APP_SCREEN_CALIB,
        [APP_MENU_TEXT] = APP_SCREEN_TEXT,
        [APP_MENU_INFO] = APP_SCREEN_INFO,
        [APP_MENU_POWER] = APP_SCREEN_POWER,
    };

    switch (nav_of(ev)) {
    case NAV_UP:
        s->menu_index = (s->menu_index + APP_MENU_COUNT - 1) % APP_MENU_COUNT;
        return APP_ACT_DRAW_MENU;
    case NAV_DOWN:
        s->menu_index = (s->menu_index + 1) % APP_MENU_COUNT;
        return APP_ACT_DRAW_MENU;
    case NAV_SELECT:
        return enter_screen(s, TARGET[s->menu_index]);
    case NAV_BACK:
        return enter_screen(s, APP_SCREEN_HOME);
    default:
        return 0;
    }
}

/**
 * @brief INFO: up/down scroll (wrap-around), 'q' back to the menu.
 *
 * @param s  State.
 * @param ev Event.
 * @return APP_ACT_* flags.
 */
static uint32_t handle_scroll(app_state_t *s, const app_event_t *ev)
{
    switch (nav_of(ev)) {
    case NAV_UP:
        s->scroll = (s->scroll + APP_SCROLL_WRAP - 1) % APP_SCROLL_WRAP;
        return APP_ACT_DRAW_SCROLL;
    case NAV_DOWN:
        s->scroll = (s->scroll + 1) % APP_SCROLL_WRAP;
        return APP_ACT_DRAW_SCROLL;
    case NAV_BACK:
        return enter_screen(s, APP_SCREEN_MENU);
    default:
        return 0;
    }
}

/**
 * @brief Clamps an angle to the limits of a joint.
 *
 * @param s     State (limits).
 * @param joint Joint index (< APP_JOINT_COUNT).
 * @param deg   Angle.
 * @return deg limited to min..max of the joint.
 */
static int joint_clamp(const app_state_t *s, unsigned joint, int deg)
{
    if (deg < s->joint_min_deg[joint]) {
        return s->joint_min_deg[joint];
    }
    if (deg > s->joint_max_deg[joint]) {
        return s->joint_max_deg[joint];
    }
    return deg;
}

/**
 * @brief Commands the shown joint (CTL): it becomes held at deg, which is now where the joint
 *        is (ctl_ref_deg).
 *
 * @param s   State.
 * @param deg Angle (clamped to the joint limits).
 * @return APP_ACT_DRAW_JOINT | APP_ACT_SERVO if the command changed, else 0.
 */
static uint32_t joint_command(app_state_t *s, int deg)
{
    unsigned j = s->joint_index;
    deg = joint_clamp(s, j, deg);
    if (s->joint_held[j] && s->joint_cmd[j] == deg) {
        return 0;
    }
    s->joint_cmd[j] = deg;
    s->joint_held[j] = true;
    s->ctl_ref_deg = deg;
    return APP_ACT_DRAW_JOINT | APP_ACT_SERVO;
}

/**
 * @brief JOINTS, NAV + Enter: ask the bus task for CTL, or say why it is not allowed (D1).
 *
 * @param s State.
 * @return APP_ACT_* flags.
 */
static uint32_t ctl_request(app_state_t *s)
{
    if (s->arm_operating) {
        return show_notice(s, APP_NOTICE_SESSION);  // read only (DS12)
    }
    s->joint_mode = APP_JOINT_WAIT;
    s->wait_ticks = 0;
    s->notice = APP_NOTICE_NONE;
    s->notice_ticks = 0;
    return APP_ACT_DRAW_JOINT | APP_ACT_CTL_REQUEST;
}

/**
 * @brief CTL (or WAIT) -> NAV, re-anchored so the joint selection does not jump.
 *
 * @param s State.
 * @return APP_ACT_DRAW_JOINT | APP_ACT_CTL_EXIT.
 */
static uint32_t ctl_back_to_nav(app_state_t *s)
{
    uint32_t act = APP_ACT_DRAW_JOINT | ctl_leave(s);
    nav_reanchor(s);
    return act;
}

/**
 * @brief The bus task granted CTL: enter CTL with the pot disengaged (soft takeover, DS13).
 *
 * @param s   State.
 * @param deg Current angle of the joint (whole degrees).
 * @return APP_ACT_* flags; APP_ACT_CTL_EXIT if CTL is no longer wanted (late answer).
 */
static uint32_t ctl_granted(app_state_t *s, int deg)
{
    if (s->screen != APP_SCREEN_JOINTS || s->joint_mode != APP_JOINT_WAIT) {
        return APP_ACT_CTL_EXIT;  // the user left before the answer: give CTL back
    }
    unsigned j = s->joint_index;
    s->joint_mode = APP_JOINT_CTL;
    s->joint_held[j] = false;
    s->ctl_ref_deg = joint_clamp(s, j, deg);
    s->ctl_engaged = false;
    return APP_ACT_DRAW_JOINT;
}

/**
 * @brief The bus task denied CTL: back to NAV with the reason on row 1.
 *
 * @param s      State.
 * @param reason app_ctl_denied_t.
 * @return APP_ACT_* flags.
 */
static uint32_t ctl_denied(app_state_t *s, int reason)
{
    if (s->screen != APP_SCREEN_JOINTS || s->joint_mode != APP_JOINT_WAIT) {
        return 0;
    }
    s->joint_mode = APP_JOINT_NAV;
    nav_reanchor(s);
    return show_notice(s, reason == APP_CTL_DENIED_SESSION ? APP_NOTICE_SESSION
                                                            : APP_NOTICE_NO_SERVO);
}

/**
 * @brief The arm state changed (bus task): redraw what shows it.
 *
 * @param s         State.
 * @param operating A ROS session runs.
 * @return APP_ACT_* flags.
 */
static uint32_t arm_state_changed(app_state_t *s, bool operating)
{
    if (s->arm_operating == operating) {
        return 0;
    }
    s->arm_operating = operating;
    if (s->screen == APP_SCREEN_HOME) {
        return APP_ACT_DRAW_SCREEN;  // DS16: row 1
    }
    if (s->screen == APP_SCREEN_JOINTS) {
        // A session never starts during CTL (the S is rejected, D2); leave it if it did.
        uint32_t act = operating ? ctl_leave(s) : 0;
        return act | APP_ACT_DRAW_JOINT;
    }
    return 0;
}

/**
 * @brief CTL pot (DS13): absolute once it passed through the joint's angle.
 *
 * @param s        State.
 * @param had_pot  There was a reading before this one.
 * @param prev_pct Previous reading.
 * @param percent  New reading.
 * @return APP_ACT_* flags.
 */
static uint32_t ctl_pot(app_state_t *s, bool had_pot, int prev_pct, int percent)
{
    unsigned j = s->joint_index;
    int deg = app_joint_deg_from_percent(s, j, percent);
    if (!s->ctl_engaged) {
        int ref = s->ctl_ref_deg;
        int prev = had_pot ? app_joint_deg_from_percent(s, j, prev_pct) : deg;
        bool crossed = (prev <= ref && deg >= ref) || (prev >= ref && deg <= ref);
        if (!crossed && abs(deg - ref) > APP_CTL_ENGAGE_DEG) {
            return APP_ACT_DRAW_JOINT;  // the hint may have changed side
        }
        s->ctl_engaged = true;
    }
    return APP_ACT_DRAW_JOINT | joint_command(s, deg);
}

/**
 * @brief CTL jog (w/s): from the joint's current angle; the pot must pass the new angle again.
 *
 * @param s     State.
 * @param delta Degrees to add.
 * @return APP_ACT_* flags.
 */
static uint32_t ctl_jog(app_state_t *s, int delta)
{
    bool was_engaged = s->ctl_engaged;
    s->ctl_engaged = false;
    uint32_t act = joint_command(s, s->ctl_ref_deg + delta);
    return act | (was_engaged ? APP_ACT_DRAW_JOINT : 0);
}

/**
 * @brief JOINTS: keys in NAV (pick the joint, Enter -> CTL, q -> MENU) and CTL (jog,
 *        Enter or q -> NAV).
 *
 * @param s  State.
 * @param ev Event.
 * @return APP_ACT_* flags.
 */
static uint32_t handle_joints(app_state_t *s, const app_event_t *ev)
{
    nav_t nav = nav_of(ev);
    if (s->joint_mode == APP_JOINT_CTL) {
        switch (nav) {
        case NAV_UP:
            return ctl_jog(s, APP_JOINT_JOG_DEG);
        case NAV_DOWN:
            return ctl_jog(s, -APP_JOINT_JOG_DEG);
        case NAV_SELECT:
        case NAV_BACK:
            return ctl_back_to_nav(s);
        default:
            return 0;
        }
    }
    if (s->joint_mode == APP_JOINT_WAIT) {
        return nav == NAV_BACK ? ctl_back_to_nav(s) : 0;  // waiting for the bus task
    }

    switch (nav) {
    case NAV_UP:
        s->joint_index = (s->joint_index + APP_JOINT_COUNT - 1) % APP_JOINT_COUNT;
        return APP_ACT_DRAW_JOINT;
    case NAV_DOWN:
        s->joint_index = (s->joint_index + 1) % APP_JOINT_COUNT;
        return APP_ACT_DRAW_JOINT;
    case NAV_SELECT:
        return ctl_request(s);
    case NAV_BACK:
        return enter_screen(s, APP_SCREEN_MENU);
    default:
        return 0;
    }
}

/**
 * @brief Calibration (SIMULATED): confirm prompt, running (q cancels), done.
 *
 * @param s  State.
 * @param ev Event.
 * @return APP_ACT_* flags.
 */
static uint32_t handle_calib(app_state_t *s, const app_event_t *ev)
{
    nav_t nav = nav_of(ev);
    if (nav == NAV_BACK) {
        return enter_screen(s, APP_SCREEN_MENU);  // also cancels a running calibration
    }

    switch (s->calib_phase) {
    case APP_CALIB_CONFIRM:
        if (nav == NAV_SELECT) {
            s->calib_phase = APP_CALIB_RUNNING;
            s->calib_ticks = 0;
            return APP_ACT_DRAW_SCREEN;  // the layout changes: steps and progress bar
        }
        return 0;
    case APP_CALIB_DONE:
        if (nav == NAV_SELECT) {
            return enter_screen(s, APP_SCREEN_MENU);
        }
        return 0;
    default:
        return 0;  // running: only q (above) and ticks matter
    }
}

/**
 * @brief Advances the running calibration, the power countdown, the JOINTS notice or the CTL
 *        request timeout by one tick.
 *
 * @param s State.
 * @return APP_ACT_* flags, 0 if nothing is ticking (a late tick).
 */
static uint32_t handle_tick(app_state_t *s)
{
    if (!app_needs_tick(s)) {
        return 0;
    }
    if (s->screen == APP_SCREEN_JOINTS) {
        uint32_t act = 0;
        if (s->notice_ticks > 0 && --s->notice_ticks == 0) {
            s->notice = APP_NOTICE_NONE;
            act |= APP_ACT_DRAW_JOINT;
        }
        if (s->joint_mode == APP_JOINT_WAIT && ++s->wait_ticks >= APP_CTL_WAIT_TICKS) {
            // No answer (request dropped?): give up; the exit cancels a late grant.
            act |= ctl_back_to_nav(s) | show_notice(s, APP_NOTICE_NO_ANSWER);
        }
        return act;
    }
    if (s->screen == APP_SCREEN_POWER) {
        s->power_ticks++;
        if (s->power_ticks >= APP_POWER_TOTAL_TICKS) {
            return enter_screen(s, APP_SCREEN_HOME);  // MOCK: nothing was turned off
        }
        return APP_ACT_DRAW_POWER;
    }
    s->calib_ticks++;
    if (s->calib_ticks >= APP_CALIB_TOTAL_TICKS) {
        s->calib_ticks = APP_CALIB_TOTAL_TICKS;
        s->calib_phase = APP_CALIB_DONE;
        return APP_ACT_DRAW_SCREEN;
    }
    return APP_ACT_DRAW_CALIB;
}

/**
 * @brief Text ("MENSAJES"): every printable key is text; empty Enter goes to the menu.
 *
 * @param s  State.
 * @param ev Event.
 * @return APP_ACT_* flags.
 */
static uint32_t handle_text(app_state_t *s, const app_event_t *ev)
{
    switch (ev->type) {
    case APP_EV_CHAR:
        if (s->line_len >= APP_TEXT_MAX) {
            return 0;  // full: extra characters are ignored
        }
        s->line[s->line_len++] = ev->ch;
        s->line[s->line_len] = '\0';
        return APP_ACT_DRAW_INPUT;
    case APP_EV_BACKSPACE:
        if (s->line_len == 0) {
            return 0;
        }
        s->line[--s->line_len] = '\0';
        return APP_ACT_DRAW_INPUT;
    case APP_EV_ENTER:
        if (s->line_len == 0) {
            return enter_screen(s, APP_SCREEN_MENU);
        }
        memcpy(s->last_text, s->line, s->line_len + 1);
        s->line[0] = '\0';
        s->line_len = 0;
        return APP_ACT_DRAW_TEXT | APP_ACT_DRAW_INPUT;
    default:
        return 0;  // arrows and other keys
    }
}

/**
 * @brief Pot: relative navigation on MENU / INFO / JOINTS NAV, the angle on JOINTS CTL.
 *
 * @param s       State.
 * @param percent New pot percent.
 * @return APP_ACT_* flags.
 */
static uint32_t handle_pot(app_state_t *s, int percent)
{
    percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    bool had_pot = s->has_pot;
    int prev_pct = s->pot_percent;
    s->pot_percent = percent;
    s->has_pot = true;

    switch (s->screen) {
    case APP_SCREEN_MENU: {
        size_t sel = pot_nav_update(&s->nav, percent, s->menu_index, APP_MENU_COUNT);
        if (sel == s->menu_index) {
            return 0;
        }
        s->menu_index = (unsigned)sel;
        return APP_ACT_DRAW_MENU;
    }
    case APP_SCREEN_INFO: {
        size_t cur = s->scroll % s->info_positions;  // what the list shows
        size_t pos = pot_nav_update(&s->nav, percent, cur, s->info_positions);
        if (pos == cur) {
            return 0;
        }
        s->scroll = (unsigned)pos;
        return APP_ACT_DRAW_SCROLL;
    }
    case APP_SCREEN_JOINTS:
        if (s->joint_mode == APP_JOINT_CTL) {
            return ctl_pot(s, had_pot, prev_pct, percent);
        } else if (s->joint_mode == APP_JOINT_WAIT) {
            return 0;  // stored: the takeover starts from it
        } else {
            size_t j = pot_nav_update(&s->nav, percent, s->joint_index, APP_JOINT_COUNT);
            if (j == s->joint_index) {
                return 0;
            }
            s->joint_index = (unsigned)j;
            return APP_ACT_DRAW_JOINT;
        }
    default:
        return 0;  // stored: the next screen or mode anchors at it
    }
}

/**
 * @brief Button short press: Enter on every screen, except INFO (-> MENU, like 'q') and
 *        APAGAR (ignored, like every key).
 *
 * @param s State.
 * @return APP_ACT_* flags.
 */
static uint32_t handle_short_press(app_state_t *s)
{
    if (s->screen == APP_SCREEN_INFO) {
        return enter_screen(s, APP_SCREEN_MENU);
    }
    const app_event_t enter = {.type = APP_EV_ENTER};
    return app_handle_event(s, &enter);
}

/**
 * @brief Applies one event to the state.
 *
 * @param s  State.
 * @param ev Event.
 * @return APP_ACT_* flags.
 */
uint32_t app_handle_event(app_state_t *s, const app_event_t *ev)
{
    if (ev->type == APP_EV_NONE) {
        return 0;
    }
    if (ev->type == APP_EV_TICK) {
        return handle_tick(s);
    }
    if (ev->type == APP_EV_POT) {
        return handle_pot(s, ev->value);
    }
    if (ev->type == APP_EV_BTN_LONG) {
        return s->screen == APP_SCREEN_HOME ? 0 : enter_screen(s, APP_SCREEN_HOME);
    }
    if (ev->type == APP_EV_BTN_SHORT) {
        return handle_short_press(s);
    }
    if (ev->type == APP_EV_ARM_STATE) {
        return arm_state_changed(s, ev->value != 0);
    }
    if (ev->type == APP_EV_CTL_GRANTED) {
        return ctl_granted(s, ev->value);
    }
    if (ev->type == APP_EV_CTL_DENIED) {
        return ctl_denied(s, ev->value);
    }

    switch (s->screen) {
    case APP_SCREEN_HOME:
        return handle_home(s, ev);
    case APP_SCREEN_MENU:
        return handle_menu(s, ev);
    case APP_SCREEN_JOINTS:
        return handle_joints(s, ev);
    case APP_SCREEN_INFO:
        return handle_scroll(s, ev);
    case APP_SCREEN_CALIB:
        return handle_calib(s, ev);
    case APP_SCREEN_TEXT:
        return handle_text(s, ev);
    case APP_SCREEN_POWER:
        return 0;  // countdown: keys are ignored, only ticks advance it
    default:
        return 0;
    }
}

/**
 * @brief True while the simulated calibration runs, the power countdown is shown, a JOINTS
 *        notice is on screen or a CTL request waits.
 *
 * @param s State.
 * @return Whether the UI task must send ticks.
 */
bool app_needs_tick(const app_state_t *s)
{
    return s->screen == APP_SCREEN_POWER ||
           (s->screen == APP_SCREEN_CALIB && s->calib_phase == APP_CALIB_RUNNING) ||
           (s->screen == APP_SCREEN_JOINTS &&
            (s->notice_ticks > 0 || s->joint_mode == APP_JOINT_WAIT));
}

/**
 * @brief Current calibration step.
 *
 * @param s State.
 * @return 0..APP_CALIB_STEPS-1, or APP_CALIB_STEPS when done.
 */
unsigned app_calib_step(const app_state_t *s)
{
    if (s->calib_phase == APP_CALIB_DONE) {
        return APP_CALIB_STEPS;
    }
    return s->calib_ticks / APP_CALIB_TICKS_PER_STEP;
}

/**
 * @brief Calibration progress.
 *
 * @param s State.
 * @return 0..100 percent.
 */
unsigned app_calib_percent(const app_state_t *s)
{
    if (s->calib_phase == APP_CALIB_DONE) {
        return 100;
    }
    return s->calib_ticks * 100u / APP_CALIB_TOTAL_TICKS;
}

/**
 * @brief Seconds left on the power countdown, rounded up.
 *
 * @param s State.
 * @return APP_POWER_SECONDS..1, or 0 once finished.
 */
unsigned app_power_seconds_left(const app_state_t *s)
{
    unsigned left = 0;
    if (s->power_ticks < APP_POWER_TOTAL_TICKS) {
        left = APP_POWER_TOTAL_TICKS - s->power_ticks;
    }
    return (left + APP_TICKS_PER_SECOND - 1) / APP_TICKS_PER_SECOND;
}

/**
 * @brief Power countdown progress.
 *
 * @param s State.
 * @return 0..100 percent.
 */
unsigned app_power_percent(const app_state_t *s)
{
    unsigned t = s->power_ticks > APP_POWER_TOTAL_TICKS ? APP_POWER_TOTAL_TICKS : s->power_ticks;
    return t * 100u / APP_POWER_TOTAL_TICKS;
}

/**
 * @brief Maps a pot percent to a joint angle.
 *
 * @param s       State (limits).
 * @param joint   Joint index.
 * @param percent Pot percent (clamped to 0..100).
 * @return Whole degrees, min..max of the joint (0 for a bad joint).
 */
int app_joint_deg_from_percent(const app_state_t *s, unsigned joint, int percent)
{
    if (joint >= APP_JOINT_COUNT) {
        return 0;
    }
    int p = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    int span = s->joint_max_deg[joint] - s->joint_min_deg[joint];
    return s->joint_min_deg[joint] + (p * span + 50) / 100;
}

/**
 * @brief Where an angle sits in the joint range, for the bar.
 *
 * @param s     State (limits).
 * @param joint Joint index.
 * @param deg   Angle in degrees (clamped to the range).
 * @return 0..100 percent (0 for a bad joint).
 */
unsigned app_joint_percent(const app_state_t *s, unsigned joint, double deg)
{
    if (joint >= APP_JOINT_COUNT) {
        return 0;
    }
    double lo = s->joint_min_deg[joint];
    double span = s->joint_max_deg[joint] - lo;
    double f = (deg - lo) / span;
    f = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
    return (unsigned)(f * 100.0 + 0.5);
}

/**
 * @brief JOINTS mode tag: NAV, CTL, or LEC while CTL is not allowed.
 *
 * @param s State.
 * @return "NAV", "CTL" or "LEC".
 */
const char *app_joint_mode_tag(const app_state_t *s)
{
    if (s->joint_mode == APP_JOINT_CTL) {
        return "CTL";
    }
    if (s->arm_operating || s->notice != APP_NOTICE_NONE) {
        return "LEC";
    }
    return "NAV";
}

/**
 * @brief JOINTS row 1 text: the notice, the takeover hint, or NULL for the bar.
 *
 * @param s State.
 * @return Text or NULL.
 */
const char *app_joint_row1_text(const app_state_t *s)
{
    if (s->notice != APP_NOTICE_NONE) {
        return NOTICE_TEXTS[s->notice];
    }
    if (s->joint_mode == APP_JOINT_CTL && !s->ctl_engaged && s->has_pot) {
        int pot_deg = app_joint_deg_from_percent(s, s->joint_index, s->pot_percent);
        return pot_deg < s->ctl_ref_deg ? HINT_TURN_UP : HINT_TURN_DOWN;
    }
    return NULL;
}

/**
 * @brief Whether the joint on screen shows its command (CTL, commanded).
 *
 * @param s   State.
 * @param deg Output: the command, written only on true.
 * @return true if the command is shown.
 */
bool app_joint_shows_command(const app_state_t *s, int *deg)
{
    unsigned j = s->joint_index;
    if (s->joint_mode != APP_JOINT_CTL || !s->joint_held[j]) {
        return false;
    }
    *deg = s->joint_cmd[j];
    return true;
}

/**
 * @brief HOME row 1: "Enter: menu", or "TAREA EN CURSO" during a ROS session (DS16).
 *
 * @param s State.
 * @return The text.
 */
const char *app_home_text(const app_state_t *s)
{
    return s->arm_operating ? "TAREA EN CURSO" : "Enter: menu";
}
