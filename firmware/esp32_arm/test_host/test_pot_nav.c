/**
 * @file test_pot_nav.c
 * @brief Host (PC) tests for main/nav/pot_nav.c. No ESP-IDF needed.
 *
 * Build and run from pot_nav_lcd_idf:
 *
 *   gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main/nav \
 *       test_host/test_pot_nav.c main/nav/pot_nav.c \
 *       -o /tmp/test_pot_nav && /tmp/test_pot_nav
 */

#include <stdio.h>
#include <stdlib.h>

#include "pot_nav.h"

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                   \
        }                                                                 \
    } while (0)

#define STEP 8
#define HYST 2

/**
 * @brief Turns the pot from one percent to another in 1 % steps, updating the cursor.
 *
 * @param nav   Navigator.
 * @param from  Start percent (excluded).
 * @param to    End percent (included).
 * @param index Cursor.
 * @param count Items.
 * @return The cursor at the end.
 */
static size_t sweep(pot_nav_t *nav, int from, int to, size_t index, size_t count)
{
    int dir = to > from ? 1 : -1;
    for (int p = from + dir; dir > 0 ? p <= to : p >= to; p += dir) {
        index = pot_nav_update(nav, p, index, count);
    }
    return index;
}

/** @brief The first update only anchors; nothing moves until a whole step. */
static void test_anchor(void)
{
    puts("anchor: first update anchors, explicit anchor, no move below one step");
    pot_nav_t nav;
    pot_nav_init(&nav, STEP, HYST);
    CHECK(!nav.anchored);
    CHECK(pot_nav_update(&nav, 50, 2, 5) == 2 && nav.anchored && nav.anchor == 50);
    CHECK(pot_nav_update(&nav, 57, 2, 5) == 2);   // 7 %: below the step
    CHECK(pot_nav_update(&nav, 43, 2, 5) == 2);   // -7 %
    CHECK(pot_nav_update(&nav, 58, 2, 5) == 3);   // 8 %: one item
    CHECK(nav.anchor == 58);

    // A screen change anchors at the current percent: the cursor never jumps.
    pot_nav_anchor(&nav, 90);
    CHECK(pot_nav_update(&nav, 90, 0, 5) == 0);
    CHECK(pot_nav_update(&nav, 91, 0, 5) == 0);
    pot_nav_reset(&nav);
    CHECK(pot_nav_update(&nav, 10, 4, 5) == 4 && nav.anchor == 10);  // only re-anchors
    CHECK(pot_nav_update(&nav, 10, 9, 5) == 4);  // index past the end is clamped
    CHECK(pot_nav_update(&nav, 10, 3, 0) == 3);  // no items: unchanged
}

/** @brief One item per step crossed, evenly spaced, several at once on a jump. */
static void test_steps(void)
{
    puts("steps: one item per step, evenly spaced; a jump moves several items");
    pot_nav_t nav;
    pot_nav_init(&nav, STEP, HYST);
    pot_nav_anchor(&nav, 20);
    size_t idx = 0;
    // 1 % at a time from 20 to 60: moves at 28, 36, 44, 52, 60.
    for (int p = 21; p <= 60; p++) {
        size_t before = idx;
        idx = pot_nav_update(&nav, p, idx, 10);
        bool detent = (p - 20) % STEP == 0;
        CHECK(idx == before + (detent ? 1u : 0u));
    }
    CHECK(idx == 5);

    pot_nav_anchor(&nav, 10);
    CHECK(pot_nav_update(&nav, 35, 0, 10) == 3);  // 25 %: three steps, 1 % left over
    CHECK(nav.anchor == 34);
    CHECK(pot_nav_update(&nav, 42, 3, 10) == 4);  // the remainder counts
}

/** @brief Reversing needs step + hyst; noise around a boundary never toggles. */
static void test_hysteresis(void)
{
    puts("hysteresis: reversing needs step + hyst, boundary noise does not toggle");
    pot_nav_t nav;
    pot_nav_init(&nav, STEP, HYST);
    pot_nav_anchor(&nav, 50);
    size_t idx = 5;
    idx = pot_nav_update(&nav, 58, idx, 10);  // forward at 58
    CHECK(idx == 6);

    // Noise around the boundary that was just crossed: 1000 random readings 55..61.
    srand(1);
    for (int i = 0; i < 1000; i++) {
        idx = pot_nav_update(&nav, 55 + rand() % 7, idx, 10);
        CHECK(idx == 6);
    }
    idx = pot_nav_update(&nav, 58, idx, 10);

    // Back: 50 (one step) is not enough after a forward move, 48 (step + hyst) is.
    CHECK(pot_nav_update(&nav, 50, idx, 10) == 6);
    CHECK(pot_nav_update(&nav, 49, idx, 10) == 6);
    idx = pot_nav_update(&nav, 48, idx, 10);
    CHECK(idx == 5 && nav.anchor == 50);
    // Forward again after a reverse needs step + hyst too: 60, not 58.
    CHECK(pot_nav_update(&nav, 58, idx, 10) == 5);
    CHECK(pot_nav_update(&nav, 59, idx, 10) == 5);
    CHECK(pot_nav_update(&nav, 60, idx, 10) == 6);
    // Continuing in the same direction keeps the plain step: 66 after 58's anchor.
    CHECK(pot_nav_update(&nav, 66, 6, 10) == 7);

    // Zero hysteresis: plain steps both ways (still a whole step of margin).
    pot_nav_init(&nav, STEP, 0);
    pot_nav_anchor(&nav, 50);
    CHECK(pot_nav_update(&nav, 58, 5, 10) == 6);
    CHECK(pot_nav_update(&nav, 51, 6, 10) == 6);
    CHECK(pot_nav_update(&nav, 50, 6, 10) == 5);
}

/** @brief Clamp at both ends, the anchor follows past the end, turning back responds. */
static void test_clamp(void)
{
    puts("clamp: no wrap, re-anchored past the end, turning back moves at once");
    pot_nav_t nav;
    pot_nav_init(&nav, STEP, HYST);
    pot_nav_anchor(&nav, 30);
    size_t idx = sweep(&nav, 30, 95, 3, 5);  // 3 -> 4 at 38, then clamped
    CHECK(idx == 4);
    CHECK(nav.anchor == 95);  // followed the pot
    idx = sweep(&nav, 95, 86, idx, 5);
    CHECK(idx == 4);           // 9 %: below step + hyst
    idx = sweep(&nav, 86, 85, idx, 5);
    CHECK(idx == 3);           // 10 % back from where the pot stopped

    // Bottom end.
    pot_nav_anchor(&nav, 60);
    idx = sweep(&nav, 60, 5, 1, 5);
    CHECK(idx == 0 && nav.anchor == 5);
    idx = sweep(&nav, 5, 15, idx, 5);
    CHECK(idx == 1);  // 10 % up from 5

    // One item: always 0, the anchor just follows.
    pot_nav_anchor(&nav, 50);
    CHECK(pot_nav_update(&nav, 90, 0, 1) == 0 && pot_nav_update(&nav, 10, 0, 1) == 0);
}

/** @brief End stop: reaching 0 % / 100 % by a real move goes to the first / last item. */
static void test_end_stop(void)
{
    puts("end stop: a move that reaches 0 / 100 % goes to the first / last item");
    pot_nav_t nav;
    pot_nav_init(&nav, STEP, HYST);

    // INFO-like list of 12 positions, entered at 60 %: plain steps cannot reach the end.
    pot_nav_anchor(&nav, 60);
    size_t idx = sweep(&nav, 60, 99, 0, 12);
    CHECK(idx == 4);
    CHECK(pot_nav_update(&nav, 100, idx, 12) == 11);  // end stop
    idx = 11;
    CHECK(pot_nav_update(&nav, 100, idx, 12) == 11);  // stays
    idx = sweep(&nav, 100, 90, idx, 12);
    CHECK(idx == 10);  // and steps back from the end

    // Entered at 100 % (or 99): noise at the end never triggers it.
    pot_nav_anchor(&nav, 99);
    CHECK(pot_nav_update(&nav, 100, 2, 12) == 2);
    CHECK(pot_nav_update(&nav, 99, 2, 12) == 2);
    CHECK(pot_nav_update(&nav, 100, 2, 12) == 2);

    pot_nav_anchor(&nav, 1);
    CHECK(pot_nav_update(&nav, 0, 7, 12) == 7);  // 1 %: noise
    pot_nav_anchor(&nav, 30);
    CHECK(pot_nav_update(&nav, 0, 7, 12) == 0);  // a real move to the bottom
    CHECK(pot_nav_update(&nav, -5, 0, 12) == 0);  // out of range is clamped
}

/**
 * @brief Runs every pot_nav test.
 *
 * @return 0 if all checks pass, 1 otherwise.
 */
int main(void)
{
    test_anchor();
    test_steps();
    test_hysteresis();
    test_clamp();
    test_end_stop();

    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
