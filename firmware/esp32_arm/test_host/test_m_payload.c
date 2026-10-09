/**
 * @file test_m_payload.c
 * @brief Host (PC) tests for main/m_payload.c. No ESP-IDF needed.
 *
 * Build and run from firmware/esp32_arm:
 *
 *   gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
 *       test_host/test_m_payload.c main/m_payload.c -o /tmp/test_m_payload \
 *       && /tmp/test_m_payload
 */

#include <stdio.h>
#include <string.h>

#include "frame_parser.h"
#include "m_payload.h"

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/**
 * @brief Builds a payload from a string where '|' stands for FS.
 *
 * @param out Output buffer.
 * @param s   Text, e.g. "+1571|-0785|...".
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
 * @brief Fills the output with sentinel values that must survive any rejected payload.
 *
 * @param mrad Output array to fill.
 */
static void fill_sentinel(int32_t mrad[M_PAYLOAD_JOINTS])
{
    for (size_t j = 0; j < M_PAYLOAD_JOINTS; j++) {
        mrad[j] = 7777;
    }
}

/**
 * @brief Checks that a rejected payload left the output untouched.
 *
 * @param mrad Output array previously filled with fill_sentinel().
 * @return 1 if every value is still the sentinel, 0 otherwise.
 */
static int is_sentinel(const int32_t mrad[M_PAYLOAD_JOINTS])
{
    for (size_t j = 0; j < M_PAYLOAD_JOINTS; j++) {
        if (mrad[j] != 7777) {
            return 0;
        }
    }
    return 1;
}

/** @brief A valid payload decodes signs, zero padding and the +-3142 extremes. */
static void test_valid_payload(void)
{
    puts("valid payload: signs, zero padding, extremes");
    uint8_t p[64];
    int32_t mrad[M_PAYLOAD_JOINTS];
    size_t n = payload_from(p, "+1571|-0785|+0000|-3142|+3142|-0001");

    CHECK(n == M_PAYLOAD_SIZE);   // R09: M frame LEN 39 = 35 payload + 4
    CHECK(m_payload_parse(p, n, mrad));
    CHECK(mrad[0] == 1571);
    CHECK(mrad[1] == -785);
    CHECK(mrad[2] == 0);
    CHECK(mrad[3] == -3142);
    CHECK(mrad[4] == 3142);
    CHECK(mrad[5] == -1);
}

/** @brief Well-formed out-of-range values are accepted: the range is mrad_to_counts()'s job. */
static void test_out_of_range_is_format_valid(void)
{
    puts("out-of-range but well-formed values are accepted (range is mrad_to_counts' job)");
    uint8_t p[64];
    int32_t mrad[M_PAYLOAD_JOINTS];
    size_t n = payload_from(p, "+9999|-9999|-0000|+0000|+0000|+0000");
    CHECK(m_payload_parse(p, n, mrad));
    CHECK(mrad[0] == 9999 && mrad[1] == -9999 && mrad[2] == 0);
}

/**
 * @brief Checks that a malformed payload returns false and leaves the output untouched.
 *
 * @param name Case name, printed on failure.
 * @param text Payload text, '|' stands for FS.
 */
static void test_rejected(const char *name, const char *text)
{
    uint8_t p[64];
    int32_t mrad[M_PAYLOAD_JOINTS];
    fill_sentinel(mrad);
    size_t n = payload_from(p, text);
    int ok = m_payload_parse(p, n, mrad);
    if (ok || !is_sentinel(mrad)) {
        printf("  FAIL rejected case '%s': \"%s\"\n", name, text);
        failures++;
    }
}

/** @brief Every malformed payload variant is rejected. */
static void test_malformed_payloads(void)
{
    puts("malformed payloads are rejected and leave the output untouched");
    test_rejected("empty", "");
    test_rejected("5 joints", "+1571|-0785|+0000|-3142|+3142");
    test_rejected("7 joints", "+1571|-0785|+0000|-3142|+3142|+0001|+0002");
    test_rejected("missing sign", "+1571|-0785|00000|-3142|+3142|+0001");
    test_rejected("space as sign", "+1571|-0785| 0000|-3142|+3142|+0001");
    test_rejected("letter in digits", "+1571|-0785|+00a0|-3142|+3142|+0001");
    test_rejected("bad last field", "+1571|-0785|+0000|-3142|+3142|+000x");
    test_rejected("comma instead of FS", "+1571,-0785|+0000|-3142|+3142|+0001");
    test_rejected("short field + long field", "+157|-07850|+0000|-3142|+3142|+0001");
    test_rejected("trailing FS", "+1571|-0785|+0000|-3142|+3142|+001|");
}

/** @brief m_payload_format() writes the same text the ros2_control plugin sends (%+05ld, FS). */
static void test_format_known(void)
{
    puts("format: same text as the plugin, out-of-range values clamped to 4 digits");
    uint8_t out[64], expected[64];
    const int32_t mrad[M_PAYLOAD_JOINTS] = {1571, -785, 0, -3142, 3142, -1};
    size_t n = m_payload_format(out, sizeof(out), mrad);
    CHECK(n == M_PAYLOAD_SIZE);
    CHECK(memcmp(out, expected, payload_from(expected, "+1571|-0785|+0000|-3142|+3142|-0001")) == 0);

    const int32_t big[M_PAYLOAD_JOINTS] = {12000, -12000, 9999, -9999, 10, -10};
    n = m_payload_format(out, sizeof(out), big);
    CHECK(memcmp(out, expected, payload_from(expected, "+9999|-9999|+9999|-9999|+0010|-0010")) == 0);

    CHECK(m_payload_format(out, M_PAYLOAD_SIZE - 1, mrad) == 0);
}

/** @brief parse(format(x)) == x for every angle in [-3142, 3142]. */
static void test_format_parse_round_trip(void)
{
    puts("round trip: parse(format(x)) == x for every mrad in [-3142, 3142]");
    uint8_t out[64];
    int mismatches = 0;
    for (int32_t v = -3142; v <= 3142; v++) {
        const int32_t mrad[M_PAYLOAD_JOINTS] = {v, -v, v / 2, 0, v, -1};
        int32_t back[M_PAYLOAD_JOINTS];
        size_t n = m_payload_format(out, sizeof(out), mrad);
        if (!m_payload_parse(out, n, back) || memcmp(mrad, back, sizeof(back)) != 0) {
            mismatches++;
        }
    }
    CHECK(mismatches == 0);
}

/**
 * @brief Runs every m_payload test.
 *
 * @return 0 if all checks pass, 1 otherwise.
 */
int main(void)
{
    test_valid_payload();
    test_out_of_range_is_format_valid();
    test_malformed_payloads();
    test_format_known();
    test_format_parse_round_trip();

    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
