/**
 * @file servo_bus.c
 * @brief STS3215 servo bus packet builders (see servo_bus.h).
 */

#include "servo_bus.h"

#define HEADER_BYTE 0xFF

/**
 * @brief Builds a broadcast position SYNC_WRITE for several servos.
 *
 * @param out      Output buffer.
 * @param out_size Size of out; at least SERVO_BUS_SYNC_WRITE_POS_SIZE(count).
 * @param targets  One goal per servo.
 * @param count    Number of servos.
 * @return Packet size in bytes, or 0 if out is too small or Length would overflow.
 */
size_t servo_bus_build_sync_write_pos(uint8_t *out, size_t out_size, const servo_target_t *targets,
                                      size_t count)
{
    size_t packet_size = SERVO_BUS_SYNC_WRITE_POS_SIZE(count);
    // Length = everything after the Length byte: instruction .. checksum.
    size_t length = packet_size - 4;
    if (packet_size > out_size || length > 0xFF) {
        return 0;
    }

    size_t n = 0;
    out[n++] = HEADER_BYTE;
    out[n++] = HEADER_BYTE;
    out[n++] = SERVO_BUS_BROADCAST_ID;
    out[n++] = (uint8_t)length;
    out[n++] = SERVO_BUS_INST_SYNC_WRITE;
    out[n++] = SERVO_REG_ACC;            // start address
    out[n++] = SERVO_BUS_POS_DATA_SIZE;  // data bytes per servo

    for (size_t i = 0; i < count; i++) {
        const servo_target_t *t = &targets[i];
        out[n++] = t->id;
        out[n++] = t->acc;
        out[n++] = (uint8_t)(t->position & 0xFF);  // little-endian
        out[n++] = (uint8_t)(t->position >> 8);
        out[n++] = 0;                              // time: unused, speed rules
        out[n++] = 0;
        out[n++] = (uint8_t)(t->speed & 0xFF);
        out[n++] = (uint8_t)(t->speed >> 8);
    }

    // Checksum covers ID .. last param (everything except the FF FF header).
    uint8_t sum = 0;
    for (size_t i = 2; i < n; i++) {
        sum += out[i];
    }
    out[n++] = (uint8_t)~sum;

    return n;
}
