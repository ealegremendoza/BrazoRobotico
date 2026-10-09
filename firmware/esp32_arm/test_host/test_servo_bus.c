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

static void test_buffer_too_small(void)
{
    puts("buffer one byte too small: returns 0 and writes nothing past it");
    const servo_target_t t = {.id = 1, .position = 2048, .speed = 300, .acc = 10};
    uint8_t out[SERVO_BUS_SYNC_WRITE_POS_SIZE(1)];
    CHECK(servo_bus_build_sync_write_pos(out, sizeof(out) - 1, &t, 1) == 0);
    CHECK(servo_bus_build_sync_write_pos(out, sizeof(out), &t, 1) == sizeof(out));
}

static void test_length_overflow(void)
{
    puts("too many servos for the 1-byte Length field: returns 0");
    static servo_target_t targets[40];
    static uint8_t out[SERVO_BUS_SYNC_WRITE_POS_SIZE(40)];
    // 31 servos: Length = 4 + 31 * 8 = 252 fits; 32 servos: 260 does not.
    CHECK(servo_bus_build_sync_write_pos(out, sizeof(out), targets, 31) != 0);
    CHECK(servo_bus_build_sync_write_pos(out, sizeof(out), targets, 32) == 0);
}

int main(void)
{
    test_e04_vector();
    test_six_servos();
    test_buffer_too_small();
    test_length_overflow();

    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
