#ifndef POT_NAV_H
#define POT_NAV_H

/**
 * @file pot_nav.h
 * @brief Pure relative ("detent") navigation with a potentiometer: turns pot percent changes
 *        into cursor moves over a list of count items. No ADC, no FreeRTOS, no logging.
 *
 * The pot is RELATIVE, never absolute: the cursor never jumps to "where the pot is". When a
 * screen (or a mode) is entered, pot_nav_anchor() takes the current percent as the anchor.
 * Every time the pot moves one step (step percent) away from the anchor, the cursor moves
 * one item and the anchor advances by one step, so the detents stay evenly spaced:
 *
 *   anchor a, step 8:   ... a-8  a  a+8  a+16 ...   each crossing = one item
 *
 * Hysteresis: reversing direction needs step + hyst instead of step (forward moves at a+8;
 * after it, going back needs a+8-10 = a-2, then forward again needs a+10), so ADC noise
 * around a step boundary cannot toggle the cursor back and forth.
 *
 * Ends: the cursor CLAMPS at 0 and count-1 (no wrap-around). While the pot keeps turning
 * past an end, the anchor follows it (re-anchored on every clamped update), so turning back
 * moves the cursor again after step + hyst, however far the pot went past the end.
 *
 * End stop: when the pot reaches its physical end (0 % or 100 %) moving toward it by at least
 * hyst from the anchor, the cursor goes to the first or last item. Without this a list longer
 * than the pot travel left over from the anchor could never be scrolled to its end (a
 * relative pot has no clutch). Noise cannot trigger it: it needs a real move of hyst percent.
 *
 * Tested on the PC in test_host/test_pot_nav.c.
 */

#include <stdbool.h>
#include <stddef.h>

#define POT_NAV_FULL 100  // percent at the physical end of the pot travel

/**
 * @brief State of one relative navigator.
 */
typedef struct {
    int anchor;     // percent of the last detent (valid if anchored)
    bool anchored;  // false until pot_nav_anchor() or the first update
    int last_dir;   // +1 / -1: direction of the last move or clamp; 0: none yet
    int step;       // percent per item (>= 1)
    int hyst;       // extra percent to reverse direction (>= 0)
} pot_nav_t;

/**
 * @brief Prepares a navigator, not anchored.
 *
 * @param nav      Navigator (not NULL).
 * @param step_pct Percent of pot travel per item (values < 1 are treated as 1).
 * @param hyst_pct Extra percent to reverse direction (values < 0 are treated as 0).
 */
void pot_nav_init(pot_nav_t *nav, int step_pct, int hyst_pct);

/**
 * @brief Forgets the anchor: the next pot_nav_update() only anchors (moves nothing).
 *
 * @param nav Navigator (not NULL).
 */
void pot_nav_reset(pot_nav_t *nav);

/**
 * @brief Takes percent as the new anchor (screen or mode entered). No direction yet.
 *
 * @param nav     Navigator (not NULL).
 * @param percent Current pot percent (clamped to 0..POT_NAV_FULL).
 */
void pot_nav_anchor(pot_nav_t *nav, int percent);

/**
 * @brief Applies a new pot reading to a cursor.
 *
 * Not anchored: anchors at percent and returns index unchanged. Otherwise moves the cursor
 * one item per detent crossed (several if the pot jumped), clamps it to 0..count-1 and
 * re-anchors while clamped, and applies the end stop (see the file comment).
 *
 * @param nav     Navigator (not NULL).
 * @param percent New pot percent (clamped to 0..POT_NAV_FULL).
 * @param index   Current cursor (values >= count are treated as count - 1).
 * @param count   Number of items (0 returns index unchanged).
 * @return The new cursor, 0..count-1.
 */
size_t pot_nav_update(pot_nav_t *nav, int percent, size_t index, size_t count);

#endif // POT_NAV_H
