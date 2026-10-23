/**
 * @file test_app_state.c
 * @brief Host (PC) tests for main/app/app_state.c. No ESP-IDF needed.
 *
 * Started as pot_nav_lcd_idf's test (the menu, INFO, CALIBRAR, MENSAJES and APAGAR tests are
 * unchanged); JOINTS was adapted to the real arm (doc/md/bitacora-E10-panel-lcd.md): CTL
 * granted by the bus task, soft takeover, LEC and notices, HOME during a ROS session.
 *
 * Build and run from firmware/esp32_arm:
 *
 *   gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main/app -I main/nav \
 *       test_host/test_app_state.c main/app/app_state.c main/nav/pot_nav.c -lm \
 *       -o /tmp/test_app_state && /tmp/test_app_state
 */

#include <stdio.h>
#include <string.h>

#include "app_state.h"

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/**
 * @brief Sends a printable key.
 *
 * @param s  State.
 * @param ch Character.
 * @return Actions.
 */
static uint32_t key(app_state_t *s, char ch)
{
    const app_event_t ev = {.type = APP_EV_CHAR, .ch = ch};
    return app_handle_event(s, &ev);
}

/**
 * @brief Sends a non-character event.
 *
 * @param s    State.
 * @param type Event type.
 * @return Actions.
 */
static uint32_t ev(app_state_t *s, app_event_type_t type)
{
    const app_event_t e = {.type = type, .ch = 0};
    return app_handle_event(s, &e);
}

/**
 * @brief Sends a pot reading.
 *
 * @param s       State.
 * @param percent Pot percent.
 * @return Actions.
 */
static uint32_t pot(app_state_t *s, int percent)
{
    const app_event_t e = {.type = APP_EV_POT, .value = percent};
    return app_handle_event(s, &e);
}

/**
 * @brief Turns the pot 1 % at a time from its last reading to percent, OR-ing the actions.
 *
 * @param s       State (s->has_pot must be true).
 * @param percent Target percent.
 * @return Actions of every reading.
 */
static uint32_t pot_turn(app_state_t *s, int percent)
{
    uint32_t act = 0;
    int dir = percent > s->pot_percent ? 1 : -1;
    while (s->pot_percent != percent) {
        act |= pot(s, s->pot_percent + dir);
    }
    return act;
}

/**
 * @brief Sends an event with a value (bus task events).
 *
 * @param s     State.
 * @param type  Event type.
 * @param value Value.
 * @return Actions.
 */
static uint32_t ev_value(app_state_t *s, app_event_type_t type, int value)
{
    const app_event_t e = {.type = type, .value = value};
    return app_handle_event(s, &e);
}

/**
 * @brief On JOINTS NAV: asks for CTL with Enter and gets it granted at deg.
 *
 * @param s   State.
 * @param deg Current angle of the joint reported by the bus task.
 * @return Actions of the grant.
 */
static uint32_t enter_ctl(app_state_t *s, int deg)
{
    ev(s, APP_EV_ENTER);
    return ev_value(s, APP_EV_CTL_GRANTED, deg);
}

/**
 * @brief Sends n ticks.
 *
 * @param s State.
 * @param n Ticks.
 * @return OR of the actions.
 */
static uint32_t ticks(app_state_t *s, unsigned n)
{
    uint32_t act = 0;
    for (unsigned i = 0; i < n; i++) {
        act |= ev(s, APP_EV_TICK);
    }
    return act;
}

/**
 * @brief Decodes a byte string, collecting the events.
 *
 * @param bytes Input bytes.
 * @param n     Number of bytes.
 * @param out   Output events.
 * @param max   Capacity of out.
 * @return Number of events.
 */
static size_t decode(const char *bytes, size_t n, app_event_t *out, size_t max)
{
    app_decoder_t d;
    app_decoder_init(&d);
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        app_event_t e;
        if (app_decode_byte(&d, (uint8_t)bytes[i], &e) && count < max) {
            out[count++] = e;
        }
    }
    return count;
}

/**
 * @brief Goes HOME -> MENU and selects a menu entry (state starts at HOME).
 *
 * @param s    State.
 * @param item Entry to open.
 * @return Actions of the final Enter.
 */
static uint32_t open_item(app_state_t *s, app_menu_item_t item)
{
    app_state_init(s);
    ev(s, APP_EV_ENTER);  // HOME -> MENU
    for (unsigned i = 0; i < (unsigned)item; i++) {
        key(s, 's');
    }
    return ev(s, APP_EV_ENTER);
}

/** @brief CR, LF and CRLF are one Enter each; CR CR and LF LF are two. */
static void test_decode_enter(void)
{
    puts("decode: CR, LF, CRLF = one Enter; CR CR, LF LF = two");
    app_event_t e[8];
    CHECK(decode("\r", 1, e, 8) == 1 && e[0].type == APP_EV_ENTER);
    CHECK(decode("\n", 1, e, 8) == 1 && e[0].type == APP_EV_ENTER);
    CHECK(decode("\r\n", 2, e, 8) == 1 && e[0].type == APP_EV_ENTER);
    CHECK(decode("\r\r", 2, e, 8) == 2);
    CHECK(decode("\n\n", 2, e, 8) == 2);
    CHECK(decode("\r\n\r\n", 4, e, 8) == 2);
    // A LF after CR + another key is a new Enter.
    CHECK(decode("\rx\n", 3, e, 8) == 3 && e[1].type == APP_EV_CHAR && e[2].type == APP_EV_ENTER);
}

/** @brief Printable, backspace, arrows, other bytes. */
static void test_decode_keys(void)
{
    puts("decode: chars, 0x08/0x7F backspace, arrows, lone ESC, other bytes");
    app_event_t e[8];
    CHECK(decode("aZ ~", 4, e, 8) == 4);
    CHECK(e[0].type == APP_EV_CHAR && e[0].ch == 'a');
    CHECK(e[1].ch == 'Z' && e[2].ch == ' ' && e[3].ch == '~');

    CHECK(decode("\x08\x7f", 2, e, 8) == 2);
    CHECK(e[0].type == APP_EV_BACKSPACE && e[1].type == APP_EV_BACKSPACE);

    CHECK(decode("\x1b[A\x1b[B\x1bOA", 9, e, 8) == 3);
    CHECK(e[0].type == APP_EV_UP && e[1].type == APP_EV_DOWN && e[2].type == APP_EV_UP);

    // Right arrow and a modified arrow (ESC [ 1 ; 5 C): one OTHER, no typed text.
    CHECK(decode("\x1b[C\x1b[1;5C", 9, e, 8) == 2);
    CHECK(e[0].type == APP_EV_OTHER && e[1].type == APP_EV_OTHER);

    // Lone ESC followed by a normal key: the key is decoded normally.
    CHECK(decode("\x1bw", 2, e, 8) == 1 && e[0].type == APP_EV_CHAR && e[0].ch == 'w');

    // Tab, control chars and UTF-8 bytes are OTHER.
    CHECK(decode("\t\x01\xc3\xa9", 4, e, 8) == 4);
    for (int i = 0; i < 4; i++) {
        CHECK(e[i].type == APP_EV_OTHER);
    }
}

/** @brief HOME: only Enter does something; 'q' does nothing. */
static void test_home(void)
{
    puts("home: q and other keys ignored, Enter -> MENU");
    app_state_t s;
    app_state_init(&s);
    CHECK(s.screen == APP_SCREEN_HOME && s.menu_index == 0);
    CHECK(key(&s, 'q') == 0 && s.screen == APP_SCREEN_HOME);
    CHECK(key(&s, 'w') == 0 && key(&s, 's') == 0 && key(&s, 'x') == 0);
    CHECK(ev(&s, APP_EV_OTHER) == 0 && ev(&s, APP_EV_TICK) == 0);
    CHECK(s.screen == APP_SCREEN_HOME);
    CHECK(ev(&s, APP_EV_ENTER) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);
}

/** @brief MENU: w/s wrap around, uppercase and arrows work, q -> HOME. */
static void test_menu_navigation(void)
{
    puts("menu: w/s/W/S/arrows move with wrap-around, other keys ignored, q -> HOME");
    app_state_t s;
    app_state_init(&s);
    ev(&s, APP_EV_ENTER);

    CHECK(APP_MENU_COUNT == 5 && APP_MENU_POWER == APP_MENU_COUNT - 1);  // APAGAR stays last
    CHECK(key(&s, 'w') == APP_ACT_DRAW_MENU && s.menu_index == APP_MENU_POWER);  // wrap up
    CHECK(key(&s, 'w') == APP_ACT_DRAW_MENU && s.menu_index == APP_MENU_INFO);
    CHECK(key(&s, 's') == APP_ACT_DRAW_MENU && s.menu_index == APP_MENU_POWER);
    CHECK(key(&s, 's') == APP_ACT_DRAW_MENU && s.menu_index == 0);                  // wrap down
    CHECK(key(&s, 'S') == APP_ACT_DRAW_MENU && s.menu_index == 1);
    CHECK(ev(&s, APP_EV_DOWN) == APP_ACT_DRAW_MENU && s.menu_index == 2);
    CHECK(key(&s, 'W') == APP_ACT_DRAW_MENU && s.menu_index == 1);
    CHECK(ev(&s, APP_EV_UP) == APP_ACT_DRAW_MENU && s.menu_index == 0);
    CHECK(key(&s, 'x') == 0 && ev(&s, APP_EV_BACKSPACE) == 0 && ev(&s, APP_EV_OTHER) == 0);
    CHECK(s.screen == APP_SCREEN_MENU && s.menu_index == 0);

    CHECK(strcmp(app_menu_labels[APP_MENU_JOINTS], "JOINTS") == 0);
    CHECK(strcmp(app_menu_labels[APP_MENU_CALIB], "CALIBRAR") == 0);
    CHECK(strcmp(app_menu_labels[APP_MENU_TEXT], "MENSAJES") == 0);
    CHECK(strcmp(app_menu_labels[APP_MENU_INFO], "INFO") == 0);
    CHECK(strcmp(app_menu_labels[APP_MENU_POWER], "APAGAR") == 0);

    // Five steps down from the first entry wrap back to it.
    for (unsigned i = 0; i < APP_MENU_COUNT; i++) {
        key(&s, 's');
    }
    CHECK(s.menu_index == 0);

    CHECK(key(&s, 'Q') == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_HOME);
}

/** @brief Each menu entry opens its screen; q (or empty Enter) comes back to MENU. */
static void test_open_and_back(void)
{
    puts("menu: each entry opens its screen, back returns to MENU with the selection kept");
    app_state_t s;

    CHECK(open_item(&s, APP_MENU_JOINTS) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_JOINTS);
    CHECK(key(&s, 'x') == 0 && s.screen == APP_SCREEN_JOINTS);
    CHECK((enter_ctl(&s, 0) & APP_ACT_DRAW_JOINT) && s.joint_mode == APP_JOINT_CTL);
    CHECK(ev(&s, APP_EV_ENTER) == (APP_ACT_DRAW_JOINT | APP_ACT_CTL_EXIT));
    CHECK(s.joint_mode == APP_JOINT_NAV);
    CHECK(key(&s, 'q') == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);
    CHECK(s.menu_index == APP_MENU_JOINTS);

    CHECK(open_item(&s, APP_MENU_CALIB) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_CALIB);
    CHECK(key(&s, 'Q') == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);
    CHECK(s.menu_index == APP_MENU_CALIB);

    CHECK(open_item(&s, APP_MENU_TEXT) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_TEXT);
    CHECK(ev(&s, APP_EV_ENTER) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);
    CHECK(s.menu_index == APP_MENU_TEXT);

    CHECK(open_item(&s, APP_MENU_INFO) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_INFO);
    CHECK(key(&s, 'x') == 0 && ev(&s, APP_EV_ENTER) == 0);
    CHECK(ev(&s, APP_EV_TICK) == 0 && !app_needs_tick(&s) && s.screen == APP_SCREEN_INFO);
    CHECK(key(&s, 'q') == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);
    CHECK(s.menu_index == APP_MENU_INFO);

    CHECK(open_item(&s, APP_MENU_POWER) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_POWER);

    // Full round trip: MENU -> HOME.
    open_item(&s, APP_MENU_JOINTS);
    key(&s, 'q');
    CHECK(key(&s, 'q') == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_HOME);
}

/** @brief INFO: up/down scroll with wrap, reset on entry, other screens untouched. */
static void test_scroll(void)
{
    puts("scroll: INFO up/down move scroll (wraps, consistent modulo), reset on entry");
    app_state_t s;
    const app_menu_item_t items[] = {APP_MENU_INFO};
    for (size_t k = 0; k < sizeof(items) / sizeof(items[0]); k++) {
        open_item(&s, items[k]);
        CHECK(s.scroll == 0);
        CHECK(key(&s, 's') == APP_ACT_DRAW_SCROLL && s.scroll == 1);
        CHECK(ev(&s, APP_EV_DOWN) == APP_ACT_DRAW_SCROLL && s.scroll == 2);
        CHECK(key(&s, 'W') == APP_ACT_DRAW_SCROLL && s.scroll == 1);
        CHECK(ev(&s, APP_EV_UP) == APP_ACT_DRAW_SCROLL && s.scroll == 0);
        CHECK(key(&s, 'w') == APP_ACT_DRAW_SCROLL && s.scroll == APP_SCROLL_WRAP - 1);
        // Up from 0 lands on the last page/line for every count up to 16.
        for (unsigned n = 1; n <= 16; n++) {
            CHECK(s.scroll % n == n - 1);
        }
        CHECK(key(&s, 'S') == APP_ACT_DRAW_SCROLL && s.scroll == 0);  // and back
        key(&s, 's');
        CHECK(key(&s, 'q') == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);
        CHECK(ev(&s, APP_EV_ENTER) == APP_ACT_DRAW_SCREEN && s.scroll == 0);  // reset on entry
    }

    // The menu keeps using up/down for its selection, not for scroll.
    open_item(&s, APP_MENU_INFO);
    key(&s, 's');
    key(&s, 'q');
    CHECK(key(&s, 's') == APP_ACT_DRAW_MENU && s.scroll == 1);
}

/** @brief JOINTS console: NAV picks the joint (wrap), Enter asks for CTL, jog, q. */
static void test_joints_console(void)
{
    puts("joints: NAV w/s pick the joint (wrap), Enter -> CTL request, CTL w/s jog, q -> NAV");
    app_state_t s;
    open_item(&s, APP_MENU_JOINTS);
    CHECK(s.joint_index == 0 && s.joint_mode == APP_JOINT_NAV);
    CHECK(key(&s, 's') == APP_ACT_DRAW_JOINT && s.joint_index == 1);
    CHECK(ev(&s, APP_EV_DOWN) == APP_ACT_DRAW_JOINT && s.joint_index == 2);
    CHECK(key(&s, 'w') == APP_ACT_DRAW_JOINT && s.joint_index == 1);
    key(&s, 'w');
    CHECK(key(&s, 'w') == APP_ACT_DRAW_JOINT && s.joint_index == APP_JOINT_COUNT - 1);  // wrap
    CHECK(key(&s, 's') == APP_ACT_DRAW_JOINT && s.joint_index == 0);

    // Enter only asks: nothing moves until the bus task grants CTL (DS4).
    key(&s, 's');  // J2: -100..98
    CHECK(ev(&s, APP_EV_ENTER) == (APP_ACT_DRAW_JOINT | APP_ACT_CTL_REQUEST));
    CHECK(s.joint_mode == APP_JOINT_WAIT && app_needs_tick(&s));
    CHECK(key(&s, 's') == 0 && ev(&s, APP_EV_ENTER) == 0 && s.joint_index == 1);  // waiting
    CHECK(ev_value(&s, APP_EV_CTL_GRANTED, 20) == APP_ACT_DRAW_JOINT);
    CHECK(s.joint_mode == APP_JOINT_CTL && s.ctl_ref_deg == 20 && !s.joint_held[1]);
    CHECK(!app_needs_tick(&s));

    // Jog from the joint's current angle (DS13), not from a stale command.
    CHECK(key(&s, 'w') == (APP_ACT_DRAW_JOINT | APP_ACT_SERVO) && s.joint_cmd[1] == 25);
    CHECK(ev(&s, APP_EV_DOWN) == (APP_ACT_DRAW_JOINT | APP_ACT_SERVO) && s.joint_cmd[1] == 20);
    CHECK(s.joint_index == 1);  // CTL: up/down never change the joint
    for (int i = 0; i < 30; i++) {
        key(&s, 'w');
    }
    CHECK(s.joint_cmd[1] == 98);           // clamped to the limit
    CHECK(key(&s, 'w') == 0);              // no change: no servo command
    CHECK(key(&s, 'x') == 0);
    CHECK(key(&s, 'q') == (APP_ACT_DRAW_JOINT | APP_ACT_CTL_EXIT));  // q: CTL -> NAV
    CHECK(s.joint_mode == APP_JOINT_NAV && s.screen == APP_SCREEN_JOINTS);
    CHECK(!s.joint_held[1]);  // NAV shows the reading again

    // q while waiting: back to NAV, and the bus task is told (a late grant must not stick).
    ev(&s, APP_EV_ENTER);
    CHECK(key(&s, 'q') == (APP_ACT_DRAW_JOINT | APP_ACT_CTL_EXIT));
    CHECK(s.joint_mode == APP_JOINT_NAV);
    CHECK(ev_value(&s, APP_EV_CTL_GRANTED, 0) == APP_ACT_CTL_EXIT && s.joint_mode == APP_JOINT_NAV);

    CHECK(key(&s, 'q') == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);
    // Entering JOINTS again: J1, NAV.
    CHECK(ev(&s, APP_EV_ENTER) == APP_ACT_DRAW_SCREEN && s.joint_index == 0);
    CHECK(s.joint_mode == APP_JOINT_NAV);
}

/** @brief Joint limits, percent -> angle and angle -> bar percent. */
static void test_joint_mapping(void)
{
    puts("joints: limits, pot percent -> angle, angle -> bar percent");
    app_state_t s;
    app_state_init(&s);
    for (unsigned j = 0; j < APP_JOINT_COUNT; j++) {
        CHECK(s.joint_min_deg[j] < s.joint_max_deg[j]);
        CHECK(app_joint_deg_from_percent(&s, j, 0) == s.joint_min_deg[j]);
        CHECK(app_joint_deg_from_percent(&s, j, 100) == s.joint_max_deg[j]);
        CHECK(app_joint_deg_from_percent(&s, j, -10) == s.joint_min_deg[j]);
        CHECK(app_joint_deg_from_percent(&s, j, 150) == s.joint_max_deg[j]);
        CHECK(app_joint_percent(&s, j, s.joint_min_deg[j]) == 0);
        CHECK(app_joint_percent(&s, j, s.joint_max_deg[j]) == 100);
        CHECK(app_joint_percent(&s, j, s.joint_max_deg[j] + 50.0) == 100);
    }
    // Defaults: the corrected URDF limits (D5), rounded inward.
    CHECK(s.joint_min_deg[0] == -110 && s.joint_max_deg[0] == 110);
    CHECK(s.joint_min_deg[2] == -94 && s.joint_max_deg[2] == 90);   // URDF -94..+90.5
    CHECK(s.joint_min_deg[4] == -157 && s.joint_max_deg[4] == 155); // URDF -157..+155.8
    CHECK(s.joint_min_deg[5] == -10 && s.joint_max_deg[5] == 100);  // gripper
    CHECK(app_joint_deg_from_percent(&s, 0, 50) == 0 && app_joint_deg_from_percent(&s, 0, 75) == 55);
    CHECK(app_joint_percent(&s, 0, 0.0) == 50 && app_joint_percent(&s, 0, -55.0) == 25);
    CHECK(app_joint_deg_from_percent(&s, 5, 50) == 45);  // J6 -10..100
    CHECK(app_joint_deg_from_percent(&s, APP_JOINT_COUNT, 50) == 0);
    CHECK(app_joint_percent(&s, APP_JOINT_COUNT, 10.0) == 0);
}

/** @brief Limits from the bus task: mrad -> whole degrees rounded inward (DS6). */
static void test_limits_from_mrad(void)
{
    puts("limits: mrad -> degrees rounded inward, set only valid pairs");
    // The firmware defaults (arm_limits.c): the URDF limits as the plugin sends them.
    const int32_t min_mrad[APP_JOINT_COUNT] = {-1920, -1745, -1640, -1658, -2744, -175};
    const int32_t max_mrad[APP_JOINT_COUNT] = {1920, 1710, 1580, 1658, 2720, 1745};
    int min_deg[APP_JOINT_COUNT];
    int max_deg[APP_JOINT_COUNT];
    app_limits_from_mrad(min_mrad, max_mrad, min_deg, max_deg);
    CHECK(min_deg[0] == -110 && max_deg[0] == 110);  // -110.008 -> -110 (inward)
    CHECK(min_deg[1] == -99 && max_deg[1] == 97);    // -99.98 -> -99, 97.98 -> 97
    CHECK(min_deg[2] == -93 && max_deg[2] == 90);    // -93.97 -> -93 (the DS6 example)
    CHECK(min_deg[3] == -94 && max_deg[3] == 94);
    CHECK(min_deg[4] == -157 && max_deg[4] == 155);
    CHECK(min_deg[5] == -10 && max_deg[5] == 99);
    for (unsigned j = 0; j < APP_JOINT_COUNT; j++) {
        // Never outside the mrad range.
        CHECK(app_deg_to_mrad(min_deg[j]) >= min_mrad[j]);
        CHECK(app_deg_to_mrad(max_deg[j]) <= max_mrad[j]);
    }

    app_state_t s;
    app_state_init(&s);
    CHECK(app_set_joint_limits(&s, min_deg, max_deg));
    CHECK(s.joint_min_deg[1] == -99 && s.joint_max_deg[5] == 99);
    CHECK(!app_set_joint_limits(&s, min_deg, max_deg));  // unchanged
    min_deg[0] = 50;  // min >= max: that joint keeps its limits
    max_deg[0] = 50;
    CHECK(!app_set_joint_limits(&s, min_deg, max_deg));
    CHECK(s.joint_min_deg[0] == -110 && s.joint_max_deg[0] == 110);

    CHECK(app_deg_to_mrad(0) == 0 && app_deg_to_mrad(90) == 1571 && app_deg_to_mrad(-90) == -1571);
    CHECK(app_mrad_to_deg(1571) > 90.0 && app_mrad_to_deg(1571) < 90.02);
}

/** @brief Pot on MENU: relative steps, clamped, anchored on entry (no jump). */
static void test_pot_menu(void)
{
    puts("pot menu: relative, clamped, anchored on entry; keys keep wrapping");
    app_state_t s;
    app_state_init(&s);
    CHECK(pot(&s, 40) == 0 && s.has_pot && s.pot_percent == 40);  // HOME: only stored
    CHECK(pot_turn(&s, 70) == 0 && s.screen == APP_SCREEN_HOME);
    ev(&s, APP_EV_ENTER);  // MENU, anchored at 70
    CHECK(s.menu_index == 0);
    CHECK(pot(&s, 70) == 0 && pot(&s, 77) == 0 && s.menu_index == 0);  // no jump on entry
    CHECK(pot(&s, 78) == APP_ACT_DRAW_MENU && s.menu_index == 1);
    CHECK(pot_turn(&s, 94) == APP_ACT_DRAW_MENU && s.menu_index == 3);
    CHECK(pot_turn(&s, 99) == 0 && s.menu_index == 3);
    // 100 %: end stop on the last entry; turning back steps down.
    CHECK(pot(&s, 100) == APP_ACT_DRAW_MENU && s.menu_index == APP_MENU_POWER);
    CHECK(pot_turn(&s, 91) == 0);
    CHECK(pot(&s, 90) == APP_ACT_DRAW_MENU && s.menu_index == APP_MENU_INFO);
    // Down to the first entry and past it: clamped, no wrap.
    pot_turn(&s, 20);
    CHECK(s.menu_index == 0);
    CHECK(pot_turn(&s, 5) == 0 && s.menu_index == 0);
    // Keys still wrap from there.
    CHECK(key(&s, 'w') == APP_ACT_DRAW_MENU && s.menu_index == APP_MENU_POWER);

    // Opening a screen and coming back re-anchors: the selection does not move.
    s.menu_index = APP_MENU_INFO;
    ev(&s, APP_EV_ENTER);                 // INFO at 5 %
    CHECK(pot_turn(&s, 60) == 0 && s.screen == APP_SCREEN_INFO);  // INFO not pot-scrollable
    key(&s, 'q');                         // MENU, anchored at 60
    CHECK(s.menu_index == APP_MENU_INFO);
    CHECK(pot(&s, 61) == 0 && pot(&s, 59) == 0 && s.menu_index == APP_MENU_INFO);
    CHECK(pot(&s, 52) == APP_ACT_DRAW_MENU && s.menu_index == APP_MENU_TEXT);

    // Step and hysteresis from Kconfig.
    app_set_pot_nav(&s, 20, 0);
    CHECK(pot(&s, 71) == 0 && pot(&s, 72) == APP_ACT_DRAW_MENU && s.menu_index == APP_MENU_INFO);
}

/** @brief Pot on INFO: clamps to the window positions; consistent with the key scroll. */
static void test_pot_info(void)
{
    puts("pot info: scrolls the window positions, clamped, consistent with w/s wrap");
    app_state_t s;
    app_state_init(&s);
    app_set_info_positions(&s, 12);
    pot(&s, 50);
    open_item(&s, APP_MENU_INFO);  // open_item re-initializes: set again
    app_set_info_positions(&s, 12);
    pot(&s, 50);  // first reading after init: anchors
    CHECK(s.scroll == 0);
    CHECK(pot_turn(&s, 66) == APP_ACT_DRAW_SCROLL && s.scroll == 2);
    CHECK(pot(&s, 100) == APP_ACT_DRAW_SCROLL && s.scroll == 11);  // end stop
    CHECK(pot_turn(&s, 50) == APP_ACT_DRAW_SCROLL && s.scroll < 11);

    // Up from 0 with w wraps to APP_SCROLL_WRAP - 1 = the last position (mod 12): the pot
    // continues from what the list shows.
    s.scroll = 0;
    key(&s, 'w');
    CHECK(s.scroll % 12 == 11);
    unsigned before = s.scroll;
    pot_turn(&s, 40);  // step back from the last position
    CHECK(s.scroll < 11 && s.scroll != before);

    app_set_info_positions(&s, 0);
    CHECK(s.info_positions == 1);
    CHECK(pot_turn(&s, 90) == 0);  // nothing to scroll
}

/** @brief Pot on JOINTS: NAV picks the joint, CTL takes over softly, back to NAV re-anchors. */
static void test_pot_joints(void)
{
    puts("pot joints: NAV picks the joint, CTL engages when the pot passes the angle, NAV");
    app_state_t s;
    app_state_init(&s);
    pot(&s, 30);
    ev(&s, APP_EV_ENTER);
    pot_turn(&s, 22);  // MENU 0, clamped
    CHECK(ev(&s, APP_EV_ENTER) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_JOINTS);
    CHECK(s.joint_index == 0 && s.joint_mode == APP_JOINT_NAV);
    CHECK(pot_turn(&s, 46) == APP_ACT_DRAW_JOINT && s.joint_index == 3);  // 22 -> 46: 3 steps

    // Short press -> CTL request; the pot is ignored while waiting.
    const app_event_t press = {.type = APP_EV_BTN_SHORT};
    CHECK(app_handle_event(&s, &press) == (APP_ACT_DRAW_JOINT | APP_ACT_CTL_REQUEST));
    CHECK(pot(&s, 47) == 0 && s.joint_index == 3);

    // Granted with J4 (-95..95) at +30 deg; the pot (47 % = -6 deg) is below: no jump.
    CHECK(ev_value(&s, APP_EV_CTL_GRANTED, 30) == APP_ACT_DRAW_JOINT);
    CHECK(s.joint_mode == APP_JOINT_CTL && !s.ctl_engaged);
    CHECK(app_joint_row1_text(&s) != NULL && strcmp(app_joint_row1_text(&s), "GIRAR >>") == 0);
    int deg = 0;
    CHECK(!app_joint_shows_command(&s, &deg));  // the reading is shown until a command
    CHECK(pot_turn(&s, 60) == APP_ACT_DRAW_JOINT);  // 60 % = +19 deg: still below, no servo
    CHECK(!s.ctl_engaged && !s.joint_held[3]);

    // Passing through +30 (66 % = +30.4 -> 30) engages; from there the pot is absolute.
    CHECK(pot_turn(&s, 66) == (APP_ACT_DRAW_JOINT | APP_ACT_SERVO));
    CHECK(s.ctl_engaged && s.joint_cmd[3] == app_joint_deg_from_percent(&s, 3, 66));
    CHECK(app_joint_row1_text(&s) == NULL);  // the bar again
    CHECK(app_joint_shows_command(&s, &deg) && deg == s.joint_cmd[3]);
    CHECK(pot(&s, 100) == (APP_ACT_DRAW_JOINT | APP_ACT_SERVO) && s.joint_cmd[3] == 95);
    CHECK(pot(&s, 0) == (APP_ACT_DRAW_JOINT | APP_ACT_SERVO) && s.joint_cmd[3] == -95);
    CHECK(s.joint_index == 3);  // no navigation in CTL
    pot(&s, 70);

    // Short press -> NAV, anchored at 70: the joint does not jump.
    CHECK(app_handle_event(&s, &press) == (APP_ACT_DRAW_JOINT | APP_ACT_CTL_EXIT));
    CHECK(s.joint_mode == APP_JOINT_NAV);
    CHECK(pot(&s, 71) == 0 && pot(&s, 64) == 0 && s.joint_index == 3);
    CHECK(pot(&s, 62) == APP_ACT_DRAW_JOINT && s.joint_index == 2);
}

/** @brief Soft takeover from above, a jump past the angle, jog disengages (DS13). */
static void test_ctl_takeover(void)
{
    puts("ctl takeover: from above (<< hint), jump across the angle engages, jog disengages");
    app_state_t s;
    open_item(&s, APP_MENU_JOINTS);  // J1 -110..110
    pot(&s, 90);                     // NAV: first reading anchors
    CHECK(enter_ctl(&s, -20) == APP_ACT_DRAW_JOINT);
    CHECK(strcmp(app_joint_row1_text(&s), "<< GIRAR") == 0);  // 90 % = +88 > -20

    // A fast turn jumps from above to below the angle in one reading: still engages.
    CHECK(pot(&s, 30) == (APP_ACT_DRAW_JOINT | APP_ACT_SERVO));  // 30 % = -44
    CHECK(s.ctl_engaged && s.joint_cmd[0] == -44);

    // Jog: from the current angle, and the pot must pass the new angle again.
    CHECK(key(&s, 'w') == (APP_ACT_DRAW_JOINT | APP_ACT_SERVO) && s.joint_cmd[0] == -39);
    CHECK(!s.ctl_engaged && s.ctl_ref_deg == -39);
    CHECK(strcmp(app_joint_row1_text(&s), "GIRAR >>") == 0);  // pot at -44
    CHECK(pot(&s, 31) == APP_ACT_DRAW_JOINT && s.joint_cmd[0] == -39);  // -42: not yet
    CHECK(pot(&s, 32) == (APP_ACT_DRAW_JOINT | APP_ACT_SERVO));  // -40: within 2 deg
    CHECK(s.ctl_engaged && s.joint_cmd[0] == -40);

    // Without a pot reading: only the jog moves, no hint.
    open_item(&s, APP_MENU_JOINTS);
    enter_ctl(&s, 10);
    CHECK(app_joint_row1_text(&s) == NULL && !s.ctl_engaged);
    CHECK(key(&s, 's') == (APP_ACT_DRAW_JOINT | APP_ACT_SERVO) && s.joint_cmd[0] == 5);

    // A grant outside the limits takes the nearest limit as the angle.
    open_item(&s, APP_MENU_JOINTS);
    enter_ctl(&s, 150);
    CHECK(s.ctl_ref_deg == 110);
}

/** @brief ROS session (D1): HOME text, LEC tag, Enter shows the reason ~1.5 s (DS12, DS16). */
static void test_session_read_only(void)
{
    puts("session: HOME TAREA EN CURSO, JOINTS LEC, Enter shows the reason for 1.5 s");
    app_state_t s;
    app_state_init(&s);
    CHECK(strcmp(app_home_text(&s), "Enter: menu") == 0);
    CHECK(ev_value(&s, APP_EV_ARM_STATE, 1) == APP_ACT_DRAW_SCREEN);  // HOME redraws row 1
    CHECK(strcmp(app_home_text(&s), "TAREA EN CURSO") == 0);
    CHECK(ev_value(&s, APP_EV_ARM_STATE, 1) == 0);  // no change

    // The menu keeps working during the session (D1).
    CHECK(ev(&s, APP_EV_ENTER) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);
    CHECK(ev(&s, APP_EV_ENTER) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_JOINTS);
    CHECK(strcmp(app_joint_mode_tag(&s), "LEC") == 0 && app_joint_row1_text(&s) == NULL);
    CHECK(key(&s, 's') == APP_ACT_DRAW_JOINT && s.joint_index == 1);  // still navigable

    // Enter / short press: no CTL request, the reason instead of the bar.
    CHECK(ev(&s, APP_EV_ENTER) == APP_ACT_DRAW_JOINT && s.joint_mode == APP_JOINT_NAV);
    CHECK(strcmp(app_joint_row1_text(&s), "TAREA EN CURSO") == 0 && app_needs_tick(&s));
    CHECK(ticks(&s, APP_NOTICE_TICKS - 1) == 0);
    CHECK(app_joint_row1_text(&s) != NULL);
    CHECK(ev(&s, APP_EV_TICK) == APP_ACT_DRAW_JOINT && app_joint_row1_text(&s) == NULL);
    CHECK(!app_needs_tick(&s) && strcmp(app_joint_mode_tag(&s), "LEC") == 0);
    const app_event_t press = {.type = APP_EV_BTN_SHORT};
    CHECK(app_handle_event(&s, &press) == APP_ACT_DRAW_JOINT);
    CHECK(strcmp(app_joint_row1_text(&s), "TAREA EN CURSO") == 0);

    // Session over (watchdog): NAV again, HOME back to "Enter: menu".
    CHECK(ev_value(&s, APP_EV_ARM_STATE, 0) == APP_ACT_DRAW_JOINT);
    ticks(&s, APP_NOTICE_TICKS);
    CHECK(strcmp(app_joint_mode_tag(&s), "NAV") == 0);
    CHECK((ev(&s, APP_EV_ENTER) & APP_ACT_CTL_REQUEST) && s.joint_mode == APP_JOINT_WAIT);
    const app_event_t hold = {.type = APP_EV_BTN_LONG};
    app_handle_event(&s, &hold);
    CHECK(s.screen == APP_SCREEN_HOME && strcmp(app_home_text(&s), "Enter: menu") == 0);

    // State changes on other screens only store the state.
    open_item(&s, APP_MENU_INFO);
    CHECK(ev_value(&s, APP_EV_ARM_STATE, 1) == 0 && s.arm_operating);
}

/** @brief CTL denied or unanswered: NAV with the reason, LEC while it is shown. */
static void test_ctl_denied(void)
{
    puts("ctl denied / no answer: NAV, reason on row 1, LEC while shown, retry possible");
    app_state_t s;
    open_item(&s, APP_MENU_JOINTS);
    ev(&s, APP_EV_ENTER);
    CHECK(ev_value(&s, APP_EV_CTL_DENIED, APP_CTL_DENIED_NO_SERVO) == APP_ACT_DRAW_JOINT);
    CHECK(s.joint_mode == APP_JOINT_NAV && strcmp(app_joint_row1_text(&s), "SERVO SIN RESP.") == 0);
    CHECK(strcmp(app_joint_mode_tag(&s), "LEC") == 0);
    ticks(&s, APP_NOTICE_TICKS);
    CHECK(strcmp(app_joint_mode_tag(&s), "NAV") == 0 && app_joint_row1_text(&s) == NULL);

    // Race with an S (DS4): the bus task says a session started.
    ev(&s, APP_EV_ENTER);
    ev_value(&s, APP_EV_CTL_DENIED, APP_CTL_DENIED_SESSION);
    CHECK(strcmp(app_joint_row1_text(&s), "TAREA EN CURSO") == 0);
    // A denial that arrives when nobody waits is ignored.
    CHECK(ev_value(&s, APP_EV_CTL_DENIED, APP_CTL_DENIED_NO_SERVO) == 0);
    ticks(&s, APP_NOTICE_TICKS);

    // No answer within APP_CTL_WAIT_TICKS: NAV, exit sent (cancels a late grant), reason.
    CHECK(ev(&s, APP_EV_ENTER) & APP_ACT_CTL_REQUEST);
    CHECK(ticks(&s, APP_CTL_WAIT_TICKS - 1) == 0 && s.joint_mode == APP_JOINT_WAIT);
    CHECK(ev(&s, APP_EV_TICK) == (APP_ACT_DRAW_JOINT | APP_ACT_CTL_EXIT));
    CHECK(s.joint_mode == APP_JOINT_NAV && strcmp(app_joint_row1_text(&s), "SIN RESPUESTA") == 0);
    CHECK(ev_value(&s, APP_EV_CTL_GRANTED, 0) == APP_ACT_CTL_EXIT && s.joint_mode == APP_JOINT_NAV);

    // Retry while the reason is shown: a new request.
    CHECK(ev(&s, APP_EV_ENTER) == (APP_ACT_DRAW_JOINT | APP_ACT_CTL_REQUEST));
    CHECK(app_joint_row1_text(&s) == NULL);
}

/** @brief Leaving JOINTS in CTL or WAIT always tells the bus task (no stuck grant, D2). */
static void test_ctl_exit_on_leave(void)
{
    puts("ctl: leaving the screen (long press) in CTL or WAIT sends the CTL exit");
    const app_event_t hold = {.type = APP_EV_BTN_LONG};
    app_state_t s;
    open_item(&s, APP_MENU_JOINTS);
    enter_ctl(&s, 0);
    CHECK(app_handle_event(&s, &hold) == (APP_ACT_DRAW_SCREEN | APP_ACT_CTL_EXIT));
    CHECK(s.screen == APP_SCREEN_HOME && s.joint_mode == APP_JOINT_NAV);

    open_item(&s, APP_MENU_JOINTS);
    ev(&s, APP_EV_ENTER);
    CHECK(app_handle_event(&s, &hold) == (APP_ACT_DRAW_SCREEN | APP_ACT_CTL_EXIT));
    CHECK(ev_value(&s, APP_EV_CTL_GRANTED, 0) == APP_ACT_CTL_EXIT);  // late grant
    CHECK(s.screen == APP_SCREEN_HOME);

    // NAV: leaving needs no exit.
    open_item(&s, APP_MENU_JOINTS);
    CHECK(key(&s, 'q') == APP_ACT_DRAW_SCREEN);
}

/** @brief Short press = Enter on every screen (INFO -> MENU, APAGAR ignores it). */
static void test_button_short(void)
{
    puts("button short: Enter everywhere; INFO -> MENU; APAGAR ignores it");
    const app_event_t press = {.type = APP_EV_BTN_SHORT};
    app_state_t s;
    app_state_init(&s);
    CHECK(app_handle_event(&s, &press) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);
    for (unsigned i = 0; i < APP_MENU_COUNT; i++) {
        app_state_init(&s);
        app_handle_event(&s, &press);
        s.menu_index = i;
        CHECK(app_handle_event(&s, &press) == APP_ACT_DRAW_SCREEN);
    }
    CHECK(s.screen == APP_SCREEN_POWER && app_handle_event(&s, &press) == 0);

    open_item(&s, APP_MENU_INFO);
    CHECK(app_handle_event(&s, &press) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);
    CHECK(s.menu_index == APP_MENU_INFO);

    // CALIBRAR: start, ignored while running, done -> MENU.
    open_item(&s, APP_MENU_CALIB);
    CHECK(app_handle_event(&s, &press) == APP_ACT_DRAW_SCREEN && s.calib_phase == APP_CALIB_RUNNING);
    CHECK(app_handle_event(&s, &press) == 0);
    for (unsigned i = 0; i < APP_CALIB_TOTAL_TICKS; i++) {
        ev(&s, APP_EV_TICK);
    }
    CHECK(s.calib_phase == APP_CALIB_DONE);
    CHECK(app_handle_event(&s, &press) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);

    // MENSAJES: submits the typed line; empty -> MENU.
    open_item(&s, APP_MENU_TEXT);
    key(&s, 'h');
    key(&s, 'i');
    CHECK(app_handle_event(&s, &press) == (APP_ACT_DRAW_TEXT | APP_ACT_DRAW_INPUT));
    CHECK(strcmp(s.last_text, "hi") == 0);
    CHECK(app_handle_event(&s, &press) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);
}

/** @brief Long press: HOME from every screen and mode (countdown and CTL included). */
static void test_button_long(void)
{
    puts("button long: HOME from every screen and mode, cancels countdown/calibration");
    const app_event_t hold = {.type = APP_EV_BTN_LONG};
    app_state_t s;
    app_state_init(&s);
    CHECK(app_handle_event(&s, &hold) == 0 && s.screen == APP_SCREEN_HOME);  // already HOME

    for (unsigned i = 0; i < APP_MENU_COUNT; i++) {
        open_item(&s, (app_menu_item_t)i);
        CHECK(app_handle_event(&s, &hold) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_HOME);
    }
    app_state_init(&s);
    ev(&s, APP_EV_ENTER);
    CHECK(app_handle_event(&s, &hold) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_HOME);

    // APAGAR countdown half way: cancelled, no more ticks.
    open_item(&s, APP_MENU_POWER);
    ev(&s, APP_EV_TICK);
    ev(&s, APP_EV_TICK);
    CHECK(app_handle_event(&s, &hold) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_HOME);
    CHECK(!app_needs_tick(&s));

    // Running calibration: cancelled.
    open_item(&s, APP_MENU_CALIB);
    ev(&s, APP_EV_ENTER);
    CHECK(app_needs_tick(&s));
    app_handle_event(&s, &hold);
    CHECK(s.screen == APP_SCREEN_HOME && !app_needs_tick(&s));

    // CTL mode: HOME (and the CTL exit), and JOINTS starts again in NAV.
    open_item(&s, APP_MENU_JOINTS);
    enter_ctl(&s, 0);
    CHECK(s.joint_mode == APP_JOINT_CTL);
    CHECK(app_handle_event(&s, &hold) == (APP_ACT_DRAW_SCREEN | APP_ACT_CTL_EXIT));
    CHECK(s.screen == APP_SCREEN_HOME);
    ev(&s, APP_EV_ENTER);
    ev(&s, APP_EV_ENTER);
    CHECK(s.screen == APP_SCREEN_JOINTS && s.joint_mode == APP_JOINT_NAV);

    // MENSAJES while typing.
    open_item(&s, APP_MENU_TEXT);
    key(&s, 'a');
    CHECK(app_handle_event(&s, &hold) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_HOME);
}

/** @brief Screens without pot navigation only store the reading; ticks are unaffected. */
static void test_pot_ignored(void)
{
    puts("pot: CALIBRAR, MENSAJES, APAGAR and HOME only store the reading");
    app_state_t s;
    open_item(&s, APP_MENU_TEXT);
    CHECK(pot(&s, 10) == 0 && pot(&s, 90) == 0 && s.line_len == 0);
    open_item(&s, APP_MENU_CALIB);
    CHECK(pot(&s, 10) == 0 && pot(&s, 90) == 0 && s.calib_phase == APP_CALIB_CONFIRM);
    open_item(&s, APP_MENU_POWER);
    CHECK(pot(&s, 10) == 0 && s.power_ticks == 0 && s.pot_percent == 10);
    CHECK(pot(&s, -3) == 0 && s.pot_percent == 0);
    CHECK(pot(&s, 300) == 0 && s.pot_percent == 100);
}

/** @brief Calibration: confirm, ticks advance steps, done, q cancels, late ticks ignored. */
static void test_calibration(void)
{
    puts("calib: confirm prompt, Enter starts, ticks advance, done, q cancels");
    app_state_t s;
    open_item(&s, APP_MENU_CALIB);
    CHECK(s.calib_phase == APP_CALIB_CONFIRM && !app_needs_tick(&s));
    CHECK(ev(&s, APP_EV_TICK) == 0 && s.calib_ticks == 0);  // not running: tick ignored
    CHECK(key(&s, 'w') == 0);

    CHECK(ev(&s, APP_EV_ENTER) == APP_ACT_DRAW_SCREEN);
    CHECK(s.calib_phase == APP_CALIB_RUNNING && app_needs_tick(&s));
    CHECK(app_calib_step(&s) == 0 && app_calib_percent(&s) == 0);
    CHECK(ev(&s, APP_EV_ENTER) == 0);  // Enter while running: ignored

    for (unsigned i = 0; i < APP_CALIB_TICKS_PER_STEP; i++) {
        CHECK(ev(&s, APP_EV_TICK) == APP_ACT_DRAW_CALIB);
    }
    CHECK(app_calib_step(&s) == 1);
    CHECK(app_calib_percent(&s) == 100 / APP_CALIB_STEPS);

    uint32_t last = 0;
    for (unsigned i = APP_CALIB_TICKS_PER_STEP; i < APP_CALIB_TOTAL_TICKS; i++) {
        last = ev(&s, APP_EV_TICK);
    }
    CHECK(last == APP_ACT_DRAW_SCREEN);
    CHECK(s.calib_phase == APP_CALIB_DONE && !app_needs_tick(&s));
    CHECK(app_calib_step(&s) == APP_CALIB_STEPS && app_calib_percent(&s) == 100);
    CHECK(ev(&s, APP_EV_TICK) == 0);  // late tick
    CHECK(ev(&s, APP_EV_ENTER) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);

    // Cancel while running, and re-entering starts again from the prompt.
    open_item(&s, APP_MENU_CALIB);
    ev(&s, APP_EV_ENTER);
    ev(&s, APP_EV_TICK);
    CHECK(key(&s, 'q') == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);
    CHECK(!app_needs_tick(&s) && ev(&s, APP_EV_TICK) == 0);
    CHECK(ev(&s, APP_EV_ENTER) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_CALIB);
    CHECK(s.calib_phase == APP_CALIB_CONFIRM && s.calib_ticks == 0);
}

/** @brief Text: accumulate, backspace, max length, submit, empty Enter -> MENU. */
static void test_text_entry(void)
{
    puts("text: typing, backspace, max length, submit, w/s/q are text, empty Enter -> MENU");
    app_state_t s;
    open_item(&s, APP_MENU_TEXT);
    CHECK(s.line_len == 0 && s.last_text[0] == '\0');

    CHECK(ev(&s, APP_EV_BACKSPACE) == 0);  // nothing to delete
    CHECK(key(&s, 'q') == APP_ACT_DRAW_INPUT && s.screen == APP_SCREEN_TEXT);  // q is text here
    CHECK(key(&s, 'w') == APP_ACT_DRAW_INPUT && key(&s, 's') == APP_ACT_DRAW_INPUT);
    CHECK(strcmp(s.line, "qws") == 0 && s.line_len == 3);
    CHECK(ev(&s, APP_EV_BACKSPACE) == APP_ACT_DRAW_INPUT && strcmp(s.line, "qw") == 0);
    CHECK(ev(&s, APP_EV_UP) == 0 && ev(&s, APP_EV_OTHER) == 0 && strcmp(s.line, "qw") == 0);

    CHECK(ev(&s, APP_EV_ENTER) == (APP_ACT_DRAW_TEXT | APP_ACT_DRAW_INPUT));
    CHECK(strcmp(s.last_text, "qw") == 0 && s.line_len == 0 && s.line[0] == '\0');
    CHECK(s.screen == APP_SCREEN_TEXT);

    // Max length: extra characters are ignored.
    for (int i = 0; i < APP_TEXT_MAX; i++) {
        CHECK(key(&s, (char)('a' + i % 26)) == APP_ACT_DRAW_INPUT);
    }
    CHECK(key(&s, 'X') == 0 && s.line_len == APP_TEXT_MAX && strlen(s.line) == APP_TEXT_MAX);
    CHECK(ev(&s, APP_EV_BACKSPACE) == APP_ACT_DRAW_INPUT && s.line_len == APP_TEXT_MAX - 1);
    CHECK(key(&s, 'Y') == APP_ACT_DRAW_INPUT && s.line[APP_TEXT_MAX - 1] == 'Y');
    ev(&s, APP_EV_ENTER);
    CHECK(strlen(s.last_text) == APP_TEXT_MAX);

    // Empty Enter -> MENU; the last text is kept for the next visit.
    CHECK(ev(&s, APP_EV_ENTER) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_MENU);
    CHECK(ev(&s, APP_EV_ENTER) == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_TEXT);
    CHECK(strlen(s.last_text) == APP_TEXT_MAX && s.line_len == 0);
}

/** @brief Power: countdown ticks to HOME, every key ignored. */
static void test_power_countdown(void)
{
    puts("power: 5 s countdown on ticks, keys ignored, then HOME");
    app_state_t s;
    open_item(&s, APP_MENU_POWER);
    CHECK(s.screen == APP_SCREEN_POWER && app_needs_tick(&s));
    CHECK(app_power_seconds_left(&s) == APP_POWER_SECONDS && app_power_percent(&s) == 0);

    // Keys are ignored during the countdown.
    CHECK(key(&s, 'q') == 0 && ev(&s, APP_EV_ENTER) == 0 && key(&s, 'w') == 0);
    CHECK(ev(&s, APP_EV_BACKSPACE) == 0 && ev(&s, APP_EV_OTHER) == 0 && ev(&s, APP_EV_UP) == 0);
    CHECK(s.screen == APP_SCREEN_POWER && s.power_ticks == 0);

    CHECK(ev(&s, APP_EV_TICK) == APP_ACT_DRAW_POWER);
    CHECK(app_power_seconds_left(&s) == APP_POWER_SECONDS);  // 4.9 s rounds up to 5
    for (unsigned i = 1; i < APP_TICKS_PER_SECOND; i++) {
        ev(&s, APP_EV_TICK);
    }
    CHECK(app_power_seconds_left(&s) == APP_POWER_SECONDS - 1);
    CHECK(app_power_percent(&s) == 100 / APP_POWER_SECONDS);

    uint32_t last = 0;
    unsigned ticks = APP_TICKS_PER_SECOND;
    while (s.screen == APP_SCREEN_POWER && ticks < 2 * APP_POWER_TOTAL_TICKS) {
        if (ticks == APP_POWER_TOTAL_TICKS - 1) {
            CHECK(app_power_seconds_left(&s) == 1);
        }
        last = ev(&s, APP_EV_TICK);
        ticks++;
    }
    CHECK(ticks == APP_POWER_TOTAL_TICKS);  // exactly 5 s of ticks
    CHECK(last == APP_ACT_DRAW_SCREEN && s.screen == APP_SCREEN_HOME && !app_needs_tick(&s));
    CHECK(ev(&s, APP_EV_TICK) == 0);  // late tick on HOME

    // Re-entering restarts the countdown.
    open_item(&s, APP_MENU_POWER);
    CHECK(s.power_ticks == 0 && app_power_seconds_left(&s) == APP_POWER_SECONDS);
}

/**
 * @brief Runs every app_state test.
 *
 * @return 0 if all checks pass, 1 otherwise.
 */
int main(void)
{
    test_decode_enter();
    test_decode_keys();
    test_home();
    test_menu_navigation();
    test_open_and_back();
    test_scroll();
    test_joints_console();
    test_joint_mapping();
    test_limits_from_mrad();
    test_pot_menu();
    test_pot_info();
    test_pot_joints();
    test_ctl_takeover();
    test_session_read_only();
    test_ctl_denied();
    test_ctl_exit_on_leave();
    test_button_short();
    test_button_long();
    test_pot_ignored();
    test_calibration();
    test_text_entry();
    test_power_countdown();

    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
