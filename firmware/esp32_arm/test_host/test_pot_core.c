/**
 * @file test_pot_core.c
 * @brief Host (PC) tests for main/pot/pot_core.c. No ESP-IDF needed.
 *
 * Build and run from pot_idf:
 *
 *   gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main/pot \
 *       test_host/test_pot_core.c main/pot/pot_core.c \
 *       -o /tmp/test_pot_core && /tmp/test_pot_core
 */

#include <stdio.h>
#include <stdlib.h>

#include "pot_core.h"

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/** @brief Ends, midpoint, rounding, monotonicity and clamping of pot_percent(). */
static void test_percent(void)
{
    CHECK(pot_percent(0) == 0);
    CHECK(pot_percent(POT_RAW_MAX) == 100);
    CHECK(pot_percent(2048) == 50);
    CHECK(pot_percent(2047) == 50);   // 49.99 % rounds to 50
    CHECK(pot_percent(1024) == 25);
    CHECK(pot_percent(20) == 0);      // 0.49 %
    CHECK(pot_percent(21) == 1);      // 0.51 %
    CHECK(pot_percent(4074) == 99);   // 99.49 %
    CHECK(pot_percent(4075) == 100);  // 99.51 %
    CHECK(pot_percent(-1) == 0);
    CHECK(pot_percent(-100000) == 0);
    CHECK(pot_percent(POT_RAW_MAX + 1) == 100);
    CHECK(pot_percent(100000) == 100);

    int prev = 0;
    for (int c = 0; c <= POT_RAW_MAX; c++) {
        int p = pot_percent(c);
        CHECK(p >= prev && p <= 100);
        prev = p;
    }
}

/** @brief pot_percent_mv() with the range measured on the board (142..3155 mV). */
static void test_percent_mv(void)
{
    const int lo = 142;
    const int hi = 3155;

    CHECK(pot_percent_mv(lo, lo, hi) == 0);
    CHECK(pot_percent_mv(hi, lo, hi) == 100);
    CHECK(pot_percent_mv(1650, lo, hi) == 50);  // half of 3.3 V
    CHECK(pot_percent_mv(1848, lo, hi) == 57);  // logged as "50%" from counts before
    CHECK(pot_percent_mv(0, lo, hi) == 0);      // below the range: clamped
    CHECK(pot_percent_mv(3300, lo, hi) == 100); // above the range: clamped
    CHECK(pot_percent_mv(-5, lo, hi) == 0);
    CHECK(pot_percent_mv(1000, 500, 500) == 0); // empty range
    CHECK(pot_percent_mv(1000, 600, 500) == 0); // inverted range

    int prev = 0;
    for (int mv = 0; mv <= 3300; mv++) {
        int p = pot_percent_mv(mv, lo, hi);
        CHECK(p >= prev && p <= 100);
        prev = p;
    }
}

/** @brief Fill-up (average of what was received), steady state, window slide, clamping. */
static void test_avg(void)
{
    pot_avg_t avg;

    pot_avg_init(&avg);
    CHECK(pot_avg_push(&avg, 100) == 100); // first output = first sample
    CHECK(pot_avg_push(&avg, 200) == 150);
    CHECK(pot_avg_push(&avg, 300) == 200);
    CHECK(pot_avg_push(&avg, 0) == 150);   // (100 + 200 + 300 + 0) / 4

    // Rounding: (1 + 2) / 2 = 1.5 -> 2.
    pot_avg_init(&avg);
    pot_avg_push(&avg, 1);
    CHECK(pot_avg_push(&avg, 2) == 2);

    // Window full of 1000, then a step to 3000: the average reaches 3000 after N samples.
    pot_avg_init(&avg);
    for (int i = 0; i < POT_AVG_LEN; i++) {
        CHECK(pot_avg_push(&avg, 1000) == 1000);
    }
    CHECK(avg.count == POT_AVG_LEN);
    int out = 0;
    int prev = 1000;
    for (int i = 1; i <= POT_AVG_LEN; i++) {
        out = pot_avg_push(&avg, 3000);
        int expected = (1000 * (POT_AVG_LEN - i) + 3000 * i + POT_AVG_LEN / 2) / POT_AVG_LEN;
        CHECK(out == expected);
        CHECK(out > prev); // monotonic toward the new value
        prev = out;
    }
    CHECK(out == 3000);
    CHECK(pot_avg_push(&avg, 3000) == 3000); // stays there

    // Samples out of range are clamped.
    pot_avg_init(&avg);
    CHECK(pot_avg_push(&avg, -50) == 0);
    pot_avg_init(&avg);
    CHECK(pot_avg_push(&avg, 9999) == POT_RAW_MAX);

    // Long run with a pseudo-random signal: the running sum never drifts.
    pot_avg_init(&avg);
    srand(1234);
    int hist[POT_AVG_LEN] = {0};
    for (int i = 0; i < 20000; i++) {
        int raw = rand() % (POT_RAW_MAX + 1);
        hist[i % POT_AVG_LEN] = raw;
        int n = i + 1 < POT_AVG_LEN ? i + 1 : POT_AVG_LEN;
        long sum = 0;
        for (int k = 0; k < n; k++) {
            sum += hist[k];
        }
        CHECK(pot_avg_push(&avg, raw) == (int)((sum + n / 2) / n));
    }
}

/** @brief First value, jitter ignored, real moves both ways, no drift, ends always shown. */
static void test_change(void)
{
    pot_change_t chg;

    pot_change_init(&chg, POT_HYST_COUNTS, POT_RAW_MAX);
    CHECK(pot_change_update(&chg, 2000));       // startup line
    CHECK(!pot_change_update(&chg, 2000));      // still
    CHECK(!pot_change_update(&chg, 2005));      // jitter
    CHECK(!pot_change_update(&chg, 1985));
    CHECK(!pot_change_update(&chg, 2000 + POT_HYST_COUNTS - 1));
    CHECK(!pot_change_update(&chg, 2000 - POT_HYST_COUNTS + 1));
    CHECK(pot_change_update(&chg, 2000 + POT_HYST_COUNTS)); // up, exactly the threshold
    CHECK(chg.last == 2000 + POT_HYST_COUNTS);
    CHECK(!pot_change_update(&chg, 2000 + 1));  // 19 below the new reference
    CHECK(pot_change_update(&chg, 2000));       // down, exactly the threshold
    CHECK(pot_change_update(&chg, 1500));       // big move down
    CHECK(pot_change_update(&chg, 3500));       // big move up

    // Slow creep: the reference does not follow ignored values, so a slow move is still
    // reported once it adds up to the threshold.
    pot_change_init(&chg, POT_HYST_COUNTS, POT_RAW_MAX);
    CHECK(pot_change_update(&chg, 1000));
    int reports = 0;
    for (int v = 1001; v <= 1100; v++) {
        if (pot_change_update(&chg, v)) {
            reports++;
            CHECK(chg.last == v);
        }
    }
    CHECK(reports == 100 / POT_HYST_COUNTS);
    CHECK(chg.last == 1100);

    // Ends: reached by a move smaller than the threshold, still reported once.
    pot_change_init(&chg, POT_HYST_COUNTS, POT_RAW_MAX);
    CHECK(pot_change_update(&chg, POT_RAW_MAX - 5));
    CHECK(pot_change_update(&chg, POT_RAW_MAX));
    CHECK(!pot_change_update(&chg, POT_RAW_MAX));
    CHECK(!pot_change_update(&chg, POT_RAW_MAX - 3));
    CHECK(!pot_change_update(&chg, POT_RAW_MAX)); // back: same as the last report
    pot_change_init(&chg, POT_HYST_COUNTS, POT_RAW_MAX);
    CHECK(pot_change_update(&chg, 7));
    CHECK(pot_change_update(&chg, 0));
    CHECK(!pot_change_update(&chg, 0));
    CHECK(!pot_change_update(&chg, 4));

    // Threshold below 1 behaves as 1 (every change reported, no repeats).
    pot_change_init(&chg, 0, POT_RAW_MAX);
    CHECK(pot_change_update(&chg, 10));
    CHECK(!pot_change_update(&chg, 10));
    CHECK(pot_change_update(&chg, 11));
}

/**
 * @brief Runs every test and reports the result.
 *
 * @return 0 if every check passed, 1 otherwise.
 */
int main(void)
{
    test_percent();
    test_percent_mv();
    test_avg();
    test_change();

    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all pot_core tests passed\n");
    return 0;
}
