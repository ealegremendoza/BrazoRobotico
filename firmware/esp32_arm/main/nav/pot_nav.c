/**
 * @file pot_nav.c
 * @brief Pure relative pot navigation (see pot_nav.h).
 */

#include "pot_nav.h"

/**
 * @brief Clamps a percent to 0..POT_NAV_FULL.
 *
 * @param percent Value.
 * @return percent limited to 0..POT_NAV_FULL.
 */
static int clamp_pct(int percent)
{
    if (percent < 0) {
        return 0;
    }
    if (percent > POT_NAV_FULL) {
        return POT_NAV_FULL;
    }
    return percent;
}

/**
 * @brief Prepares a navigator, not anchored.
 *
 * @param nav      Navigator (not NULL).
 * @param step_pct Percent per item (values < 1 are treated as 1).
 * @param hyst_pct Extra percent to reverse direction (values < 0 are treated as 0).
 */
void pot_nav_init(pot_nav_t *nav, int step_pct, int hyst_pct)
{
    nav->step = step_pct < 1 ? 1 : step_pct;
    nav->hyst = hyst_pct < 0 ? 0 : hyst_pct;
    pot_nav_reset(nav);
}

/**
 * @brief Forgets the anchor.
 *
 * @param nav Navigator (not NULL).
 */
void pot_nav_reset(pot_nav_t *nav)
{
    nav->anchor = 0;
    nav->anchored = false;
    nav->last_dir = 0;
}

/**
 * @brief Takes percent as the new anchor, with no direction yet.
 *
 * @param nav     Navigator (not NULL).
 * @param percent Current pot percent.
 */
void pot_nav_anchor(pot_nav_t *nav, int percent)
{
    nav->anchor = clamp_pct(percent);
    nav->anchored = true;
    nav->last_dir = 0;
}

/**
 * @brief Applies a new pot reading to a cursor (see pot_nav.h).
 *
 * @param nav     Navigator (not NULL).
 * @param percent New pot percent.
 * @param index   Current cursor.
 * @param count   Number of items.
 * @return The new cursor, 0..count-1.
 */
size_t pot_nav_update(pot_nav_t *nav, int percent, size_t index, size_t count)
{
    if (count == 0) {
        return index;
    }
    if (index >= count) {
        index = count - 1;
    }
    int p = clamp_pct(percent);
    if (!nav->anchored) {
        pot_nav_anchor(nav, p);
        return index;
    }

    // End stop: a real move (>= hyst, at least 1 %) that reaches a physical end of the pot.
    int end_move = nav->hyst > 0 ? nav->hyst : 1;
    if (p >= POT_NAV_FULL && p - nav->anchor >= end_move) {
        nav->anchor = p;
        nav->last_dir = 1;
        return count - 1;
    }
    if (p <= 0 && nav->anchor - p >= end_move) {
        nav->anchor = p;
        nav->last_dir = -1;
        return 0;
    }

    // Detents: one item per step crossed; reversing needs step + hyst.
    for (;;) {
        int d = p - nav->anchor;
        int fwd = nav->step + (nav->last_dir < 0 ? nav->hyst : 0);
        int back = nav->step + (nav->last_dir > 0 ? nav->hyst : 0);
        if (d >= fwd && index + 1 < count) {
            index++;
            nav->anchor += nav->step;
            nav->last_dir = 1;
        } else if (d <= -back && index > 0) {
            index--;
            nav->anchor -= nav->step;
            nav->last_dir = -1;
        } else {
            break;
        }
    }

    // Clamped: the anchor follows the pot past the end, so turning back responds at once.
    if (index + 1 >= count && p > nav->anchor) {
        nav->anchor = p;
        nav->last_dir = 1;
    }
    if (index == 0 && p < nav->anchor) {
        nav->anchor = p;
        nav->last_dir = -1;
    }
    return index;
}
