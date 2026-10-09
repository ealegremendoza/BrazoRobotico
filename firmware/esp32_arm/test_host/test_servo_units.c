/**
 * @file test_servo_units.c
 * @brief Host (PC) tests for main/servo_units.c. No ESP-IDF needed.
 *
 * Build and run from firmware/esp32_arm:
 *
 *   gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
 *       test_host/test_servo_units.c main/servo_units.c -lm -o /tmp/test_servo_units \
 *       && /tmp/test_servo_units
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "servo_units.h"

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/** Floating-point reference: 2048 + round(mrad * 4096 / 6283), input clamped to +-3142, output to 0..4095. */
static int32_t ref_mrad_to_counts(int32_t mrad)
{
    if (mrad > 3142) {
        mrad = 3142;
    } else if (mrad < -3142) {
        mrad = -3142;
    }
    double counts = 2048 + round(mrad * 4096.0 / 6283.0);
    return counts > 4095 ? 4095 : (int32_t)counts;
}

/** Floating-point reference: round((counts - 2048) * 6283 / 4096), input clamped to 0..4095. */
static int32_t ref_counts_to_mrad(int32_t counts)
{
    if (counts > 4095) {
        counts = 4095;
    } else if (counts < 0) {
        counts = 0;
    }
    return (int32_t)round((counts - 2048) * 6283.0 / 4096.0);
}

static void test_mrad_to_counts_known_values(void)
{
    puts("mrad_to_counts: known values");
    CHECK(mrad_to_counts(0) == 2048);
    CHECK(mrad_to_counts(1) == 2049);
    CHECK(mrad_to_counts(-1) == 2047);
    CHECK(mrad_to_counts(1571) == 3072);
    CHECK(mrad_to_counts(-1571) == 1024);   // naive (x + 3141) / 6283 gives 1025
    CHECK(mrad_to_counts(-3142) == 0);
    CHECK(mrad_to_counts(3142) == 4095);    // +pi would be 4096: clamped
}

static void test_mrad_to_counts_vs_reference(void)
{
    puts("mrad_to_counts: matches float reference for every mrad in [-3500, 3500]");
    int mismatches = 0;
    for (int32_t mrad = -3500; mrad <= 3500; mrad++) {
        if (mrad_to_counts(mrad) != ref_mrad_to_counts(mrad)) {
            mismatches++;
        }
    }
    CHECK(mismatches == 0);
}

static void test_mrad_to_counts_no_overflow(void)
{
    puts("mrad_to_counts: extreme inputs are clamped before multiplying");
    CHECK(mrad_to_counts(524288) == 4095);    // first value where mrad * 4096 overflows int32_t
    CHECK(mrad_to_counts(-524288) == 0);
    CHECK(mrad_to_counts(INT32_MAX) == 4095);
    CHECK(mrad_to_counts(INT32_MIN) == 0);
}

static void test_counts_to_mrad_known_values(void)
{
    puts("counts_to_mrad: known values");
    CHECK(counts_to_mrad(2048) == 0);
    CHECK(counts_to_mrad(3072) == 1571);
    CHECK(counts_to_mrad(1024) == -1571);
    CHECK(counts_to_mrad(0) == -3142);
    CHECK(counts_to_mrad(4095) == 3140);
}

static void test_counts_to_mrad_vs_reference(void)
{
    puts("counts_to_mrad: matches float reference for every count in [-100, 4200]");
    int mismatches = 0;
    for (int32_t counts = -100; counts <= 4200; counts++) {
        if (counts_to_mrad(counts) != ref_counts_to_mrad(counts)) {
            mismatches++;
        }
    }
    CHECK(mismatches == 0);
    CHECK(counts_to_mrad(INT32_MAX) == 3140);
    CHECK(counts_to_mrad(INT32_MIN) == -3142);
}

static void test_round_trip(void)
{
    puts("round trip: counts -> mrad -> counts is exact for all 4096 counts");
    int mismatches = 0;
    for (int32_t counts = 0; counts <= 4095; counts++) {
        if (mrad_to_counts(counts_to_mrad(counts)) != counts) {
            mismatches++;
        }
    }
    CHECK(mismatches == 0);

    puts("round trip: mrad -> counts -> mrad error <= 1 mrad (2 at +pi, clamped to 4095)");
    int max_error = 0;
    for (int32_t mrad = -3142; mrad < 3142; mrad++) {
        int error = abs(counts_to_mrad(mrad_to_counts(mrad)) - mrad);
        if (error > max_error) {
            max_error = error;
        }
    }
    CHECK(max_error <= 1);
    CHECK(counts_to_mrad(mrad_to_counts(3142)) == 3140);
}

int main(void)
{
    test_mrad_to_counts_known_values();
    test_mrad_to_counts_vs_reference();
    test_mrad_to_counts_no_overflow();
    test_counts_to_mrad_known_values();
    test_counts_to_mrad_vs_reference();
    test_round_trip();

    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
