/**
 * @file test_arm_limits.c
 * @brief Host (PC) tests for main/arm_limits.c (S param_init with the joint limits).
 *        No ESP-IDF needed.
 *
 * Build and run from firmware/esp32_arm:
 *
 *   gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
 *       test_host/test_arm_limits.c main/arm_limits.c main/m_payload.c \
 *       -o /tmp/test_arm_limits && /tmp/test_arm_limits
 */

#include <stdio.h>
#include <string.h>

#include "arm_limits.h"
#include "frame_parser.h"

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/** The URDF limits as the plugin sends them (min block | max block), '|' = FS. */
static const char URDF_PARAM_INIT[] =
    "-1920|-1745|-1640|-1658|-2744|-0175|+1920|+1710|+1580|+1658|+2720|+1745";

/**
 * @brief Builds a payload from a string where '|' stands for FS.
 *
 * @param out Output buffer.
 * @param s   Text.
 * @return Payload size.
 */
static size_t payload_from(uint8_t *out, const char *s)
{
    size_t n = strlen(s);
    for (size_t i = 0; i < n; i++) {
        out[i] = s[i] == '|' ? FRAME_FS : (uint8_t)s[i];
    }
    return n;
}

/**
 * @brief Parses a '|'-separated text.
 *
 * @param s      Text.
 * @param limits In/out limits.
 * @return The parse result.
 */
static arm_limits_parse_t parse(const char *s, arm_limits_t *limits)
{
    uint8_t buf[256];
    size_t n = payload_from(buf, s);
    return arm_limits_parse(buf, n, limits);
}

/**
 * @brief Checks whether two limit sets are equal.
 *
 * @param a First set.
 * @param b Second set.
 * @return 1 if equal, 0 otherwise.
 */
static int same(const arm_limits_t *a, const arm_limits_t *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

/** @brief Defaults: the corrected URDF limits in mrad (D5), all valid. */
static void test_defaults(void)
{
    puts("defaults: corrected URDF limits (D5), min < max, within +-pi");
    arm_limits_t l;
    arm_limits_set_default(&l);
    CHECK(l.min_mrad[0] == -1920 && l.max_mrad[0] == 1920);  // shoulder_pan +-110 deg
    CHECK(l.min_mrad[1] == -1745 && l.max_mrad[1] == 1710);
    CHECK(l.min_mrad[2] == -1640 && l.max_mrad[2] == 1580);
    CHECK(l.min_mrad[3] == -1658 && l.max_mrad[3] == 1658);
    CHECK(l.min_mrad[4] == -2744 && l.max_mrad[4] == 2720);
    CHECK(l.min_mrad[5] == -175 && l.max_mrad[5] == 1745);   // gripper -10..100 deg
    for (size_t j = 0; j < ARM_LIMITS_JOINTS; j++) {
        CHECK(l.min_mrad[j] < l.max_mrad[j]);
        CHECK(l.min_mrad[j] >= -ARM_LIMITS_MAX_MRAD && l.max_mrad[j] <= ARM_LIMITS_MAX_MRAD);
    }
}

/** @brief The plugin's param_init decodes to the defaults; size is 71 bytes. */
static void test_parse_valid(void)
{
    puts("parse: two M blocks (mins, maxs), 71 bytes");
    CHECK(ARM_LIMITS_PAYLOAD_SIZE == 71);
    CHECK(strlen(URDF_PARAM_INIT) == ARM_LIMITS_PAYLOAD_SIZE);

    arm_limits_t l = {{0}, {0}};
    arm_limits_t def;
    arm_limits_set_default(&def);
    CHECK(parse(URDF_PARAM_INIT, &l) == ARM_LIMITS_OK);
    CHECK(same(&l, &def));

    // Other values, the extremes included.
    CHECK(parse("-3142|-0001|+0000|-1000|-0500|-0010|+3142|+0001|+0001|+1000|+0500|+0010",
                &l) == ARM_LIMITS_OK);
    CHECK(l.min_mrad[0] == -3142 && l.max_mrad[0] == 3142);
    CHECK(l.min_mrad[2] == 0 && l.max_mrad[2] == 1);
}

/** @brief No param_init: keep the current limits (DS11). */
static void test_parse_empty(void)
{
    puts("parse: empty param_init -> EMPTY, limits untouched");
    arm_limits_t l;
    arm_limits_set_default(&l);
    arm_limits_t before = l;
    CHECK(arm_limits_parse(NULL, 0, &l) == ARM_LIMITS_EMPTY);
    CHECK(same(&l, &before));
}

/** @brief Malformed or out-of-range limits: INVALID, limits untouched (DS11, E 21). */
static void test_parse_invalid(void)
{
    puts("parse: malformed, min >= max, out of +-3142 -> INVALID, limits untouched");
    arm_limits_t l;
    arm_limits_set_default(&l);
    arm_limits_t before = l;

    static const char *const BAD[] = {
        // Only one block (an M payload).
        "-1920|-1745|-1640|-1658|-2744|-0175",
        // Wrong separator between the blocks.
        "-1920|-1745|-1640|-1658|-2744|-0175,+1920|+1710|+1580|+1658|+2720|+1745",
        // Malformed field.
        "-1920|-1745|-1640|-1658|-2744|-0175|+1920|+1710|+15x0|+1658|+2720|+1745",
        // One byte too many.
        "-1920|-1745|-1640|-1658|-2744|-0175|+1920|+1710|+1580|+1658|+2720|+17450",
        // min == max.
        "-1920|-1745|+0100|-1658|-2744|-0175|+1920|+1710|+0100|+1658|+2720|+1745",
        // min > max (blocks swapped).
        "+1920|+1710|+1580|+1658|+2720|+1745|-1920|-1745|-1640|-1658|-2744|-0175",
        // Beyond +-pi.
        "-3143|-1745|-1640|-1658|-2744|-0175|+1920|+1710|+1580|+1658|+2720|+1745",
        "-1920|-1745|-1640|-1658|-2744|-0175|+1920|+1710|+1580|+1658|+2720|+3143",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
        CHECK(parse(BAD[i], &l) == ARM_LIMITS_INVALID);
        CHECK(same(&l, &before));
    }
}

/** @brief Clamp: inside, below, above, bad joint. */
static void test_clamp(void)
{
    puts("clamp: targets limited to [min, max] of the joint");
    arm_limits_t l;
    arm_limits_set_default(&l);
    CHECK(arm_limits_clamp(&l, 0, 0) == 0);
    CHECK(arm_limits_clamp(&l, 0, -3000) == -1920);
    CHECK(arm_limits_clamp(&l, 0, 3000) == 1920);
    CHECK(arm_limits_clamp(&l, 5, -500) == -175);
    CHECK(arm_limits_clamp(&l, 5, 1745) == 1745);
    CHECK(arm_limits_clamp(&l, ARM_LIMITS_JOINTS, 100) == 0);
}

/**
 * @brief Runs every arm_limits test.
 *
 * @return 0 if all checks pass, 1 otherwise.
 */
int main(void)
{
    test_defaults();
    test_parse_valid();
    test_parse_empty();
    test_parse_invalid();
    test_clamp();

    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
