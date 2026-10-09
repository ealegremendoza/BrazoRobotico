/**
 * @file test_servo_bus.c
 * @brief Host (PC) tests for main/servo_bus.c. No ESP-IDF needed.
 *
 * Build and run from firmware/esp32_arm:
 *
 *   gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
 *       test_host/test_servo_bus.c main/servo_bus.c -o /tmp/test_servo_bus \
 *       && /tmp/test_servo_bus
 */

#include <stdio.h>
#include <string.h>

#include "servo_bus.h"

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/** @brief A 2-servo SYNC_WRITE matches the worked example in bitacora-E04 byte for byte. */
static void test_e04_vector(void)
{
    puts("2-servo SYNC_WRITE matches the worked example in bitacora-E04 (Ejemplo 2)");
    const servo_target_t targets[] = {
        {.id = 1, .position = 2048, .speed = 500, .acc = 50},
        {.id = 2, .position = 1024, .speed = 300, .acc = 30},
    };
    const uint8_t expected[] = {0xFF, 0xFF, 0xFE, 0x14, 0x83, 0x29, 0x07, 0x01, 0x32, 0x00, 0x08, 0x00,
                                0x00, 0xF4, 0x01, 0x02, 0x1E, 0x00, 0x04, 0x00, 0x00, 0x2C, 0x01, 0xB9};
    uint8_t out[64];
    size_t n = servo_bus_build_sync_write_pos(out, sizeof(out), targets, 2);
    CHECK(n == sizeof(expected));
    CHECK(n == SERVO_BUS_SYNC_WRITE_POS_SIZE(2));
    CHECK(memcmp(out, expected, sizeof(expected)) == 0);
}

/** @brief A 6-servo packet has the right size, Length field, byte order and checksum. */
static void test_six_servos(void)
{
    puts("6-servo packet: size, Length field and checksum");
    servo_target_t targets[6];
    for (int i = 0; i < 6; i++) {
        targets[i] = (servo_target_t){.id = (uint8_t)(i + 1), .position = 4095, .speed = 3400, .acc = 254};
    }
    uint8_t out[SERVO_BUS_SYNC_WRITE_POS_SIZE(6)];
    size_t n = servo_bus_build_sync_write_pos(out, sizeof(out), targets, 6);
    CHECK(n == 56);                // 8 + 6 * 8
    CHECK(out[3] == n - 4);        // Length = instruction .. checksum
    CHECK(out[7 + 5 * 8] == 6);    // last block starts with ID 6
    CHECK(out[9] == 0xFF && out[10] == 0x0F);   // 4095 little-endian
    CHECK(out[13] == 0x48 && out[14] == 0x0D);  // 3400 little-endian

    // Receiver-side check: ID..checksum must add up to 0xFF.
    uint8_t sum = 0;
    for (size_t i = 2; i < n; i++) {
        sum += out[i];
    }
    CHECK(sum == 0xFF);
}

/** @brief A buffer one byte too small makes the builder return 0. */
static void test_buffer_too_small(void)
{
    puts("buffer one byte too small: returns 0 and writes nothing past it");
    const servo_target_t t = {.id = 1, .position = 2048, .speed = 300, .acc = 10};
    uint8_t out[SERVO_BUS_SYNC_WRITE_POS_SIZE(1)];
    CHECK(servo_bus_build_sync_write_pos(out, sizeof(out) - 1, &t, 1) == 0);
    CHECK(servo_bus_build_sync_write_pos(out, sizeof(out), &t, 1) == sizeof(out));
}

/** @brief Too many servos for the 1-byte Length field make the builder return 0. */
static void test_length_overflow(void)
{
    puts("too many servos for the 1-byte Length field: returns 0");
    static servo_target_t targets[40];
    static uint8_t out[SERVO_BUS_SYNC_WRITE_POS_SIZE(40)];
    // 31 servos: Length = 4 + 31 * 8 = 252 fits; 32 servos: 260 does not.
    CHECK(servo_bus_build_sync_write_pos(out, sizeof(out), targets, 31) != 0);
    CHECK(servo_bus_build_sync_write_pos(out, sizeof(out), targets, 32) == 0);
}

/**
 * @brief Builds the status packet a servo sends after a 2-byte position read.
 *
 * @param out   Output buffer (SERVO_BUS_POS_STATUS_SIZE bytes).
 * @param id    Servo ID.
 * @param raw   Raw 16-bit position (bit 15 = sign).
 * @param error ERROR byte.
 * @return SERVO_BUS_POS_STATUS_SIZE.
 */
static size_t make_status(uint8_t *out, uint8_t id, uint16_t raw, uint8_t error)
{
    out[0] = 0xFF;
    out[1] = 0xFF;
    out[2] = id;
    out[3] = 4;
    out[4] = error;
    out[5] = (uint8_t)(raw & 0xFF);
    out[6] = (uint8_t)(raw >> 8);
    uint8_t sum = 0;
    for (int i = 2; i < 7; i++) {
        sum += out[i];
    }
    out[7] = (uint8_t)~sum;
    return SERVO_BUS_POS_STATUS_SIZE;
}

static const uint8_t IDS[6] = {1, 2, 3, 4, 5, 6};

/** @brief A 6-servo SYNC_READ has the manual's layout: Length N+4, 0x82, address 0x38, 2 bytes, IDs. */
static void test_sync_read_request(void)
{
    puts("SYNC_READ request for 6 servos: layout and checksum");
    uint8_t out[SERVO_BUS_SYNC_READ_SIZE(6)];
    size_t n = servo_bus_build_sync_read_pos(out, sizeof(out), IDS, 6);
    const uint8_t head[] = {0xFF, 0xFF, 0xFE, 0x0A, 0x82, 0x38, 0x02, 1, 2, 3, 4, 5, 6};
    CHECK(n == 14);
    CHECK(memcmp(out, head, sizeof(head)) == 0);   // Length = N + 4 = 10
    uint8_t sum = 0;
    for (size_t i = 2; i < n; i++) {
        sum += out[i];
    }
    CHECK(sum == 0xFF);
    CHECK(servo_bus_build_sync_read_pos(out, sizeof(out) - 1, IDS, 6) == 0);
}

/** @brief The READ answer from the manual (example 2: FF FF 01 04 00 18 05 DD) decodes to 1304. */
static void test_parse_manual_answer(void)
{
    puts("status packet from the manual decodes to 1304");
    const uint8_t rx[] = {0xFF, 0xFF, 0x01, 0x04, 0x00, 0x18, 0x05, 0xDD};
    int32_t pos[1] = {0};
    uint8_t err = 0xAA;
    CHECK(servo_bus_parse_positions(rx, sizeof(rx), IDS, 1, pos, &err));
    CHECK(pos[0] == 1304);
    CHECK(err == 0);
}

/** @brief Six answers, with an echo of our own packets and noise before them, are all decoded. */
static void test_parse_six_with_echo(void)
{
    puts("6 answers after an echo of SYNC_WRITE + SYNC_READ and noise");
    uint8_t rx[256];
    size_t n = 0;
    servo_target_t targets[6];
    for (int i = 0; i < 6; i++) {
        targets[i] = (servo_target_t){.id = IDS[i], .position = 2048, .speed = 300, .acc = 10};
    }
    n += servo_bus_build_sync_write_pos(rx + n, sizeof(rx) - n, targets, 6);   // echo
    n += servo_bus_build_sync_read_pos(rx + n, sizeof(rx) - n, IDS, 6);        // echo
    rx[n++] = 0x00;                                                            // noise
    rx[n++] = 0xFF;
    const uint16_t raw[6] = {0, 1024, 2048, 3072, 4095, 0x8005};   // last one: sign bit -> -5
    for (int i = 0; i < 6; i++) {
        n += make_status(rx + n, IDS[i], raw[i], i == 2 ? 0x20 : 0);   // servo 3 reports an error
    }

    int32_t pos[6];
    uint8_t err = 0;
    CHECK(servo_bus_parse_positions(rx, n, IDS, 6, pos, &err));
    CHECK(pos[0] == 0 && pos[1] == 1024 && pos[2] == 2048 && pos[3] == 3072 && pos[4] == 4095);
    CHECK(pos[5] == -5);
    CHECK(err == 0x20);
}

/** @brief A missing answer, a bad checksum or a wrong order leave the outputs untouched. */
static void test_parse_incomplete(void)
{
    puts("missing answer, bad checksum, wrong order: false and outputs untouched");
    uint8_t rx[64];
    int32_t pos[6] = {7, 7, 7, 7, 7, 7};
    uint8_t err = 0x55;

    size_t n = 0;
    for (int i = 0; i < 5; i++) {   // servo 6 never answers
        n += make_status(rx + n, IDS[i], 2048, 0);
    }
    CHECK(!servo_bus_parse_positions(rx, n, IDS, 6, pos, &err));

    n += make_status(rx + n, IDS[5], 2048, 0);
    rx[n - 1] ^= 0x01;   // corrupt servo 6's checksum
    CHECK(!servo_bus_parse_positions(rx, n, IDS, 6, pos, &err));

    n = 0;
    n += make_status(rx + n, 2, 2048, 0);   // servo 2 before servo 1
    n += make_status(rx + n, 1, 2048, 0);
    CHECK(!servo_bus_parse_positions(rx, n, IDS, 2, pos, &err));

    CHECK(pos[0] == 7 && pos[5] == 7 && err == 0x55);
}

/**
 * @brief Runs every servo_bus test.
 *
 * @return 0 if all checks pass, 1 otherwise.
 */
int main(void)
{
    test_e04_vector();
    test_six_servos();
    test_buffer_too_small();
    test_length_overflow();
    test_sync_read_request();
    test_parse_manual_answer();
    test_parse_six_with_echo();
    test_parse_incomplete();

    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
