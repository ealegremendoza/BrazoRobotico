/**
 * @file servo_bus.c
 * @brief STS3215 servo bus packet builders (see servo_bus.h).
 */

#include "servo_bus.h"

#define HEADER_BYTE 0xFF
#define READ_POSITION_LEN 2   // bytes read per servo: present position
#define STATUS_LENGTH 4       // Length field of a 2-byte read answer: ERROR + 2 data + checksum
#define MAX_READ_SERVOS 32    // > 31, the most a 1-byte Length allows in a SYNC_WRITE

/**
 * @brief Checksum of a packet: ~(sum of ID .. last param) & 0xFF.
 *
 * @param packet Packet starting at the FF FF header.
 * @param end    Index of the checksum byte (exclusive end of the summed range).
 * @return Checksum byte.
 */
static uint8_t checksum(const uint8_t *packet, size_t end)
{
    uint8_t sum = 0;  // uint8_t wraps: only the low byte of the sum is kept
    for (size_t i = 2; i < end; i++) {
        sum += packet[i];
    }
    return (uint8_t)~sum;
}

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
    out[n] = checksum(out, n);
    n++;

    return n;
}

/**
 * @brief Builds a SYNC_READ that asks several servos for their present position.
 *
 * @param out      Output buffer.
 * @param out_size Size of out; at least SERVO_BUS_SYNC_READ_SIZE(count).
 * @param ids      Servo IDs to query, in answer order.
 * @param count    Number of servos.
 * @return Packet size in bytes, or 0 if out is too small.
 */
size_t servo_bus_build_sync_read_pos(uint8_t *out, size_t out_size, const uint8_t *ids, size_t count)
{
    size_t packet_size = SERVO_BUS_SYNC_READ_SIZE(count);
    size_t length = packet_size - 4;  // manual: N + 4
    if (packet_size > out_size || length > 0xFF) {
        return 0;
    }

    size_t n = 0;
    out[n++] = HEADER_BYTE;
    out[n++] = HEADER_BYTE;
    out[n++] = SERVO_BUS_BROADCAST_ID;
    out[n++] = (uint8_t)length;
    out[n++] = SERVO_BUS_INST_SYNC_READ;
    out[n++] = SERVO_REG_PRESENT_POSITION;  // start address
    out[n++] = READ_POSITION_LEN;           // bytes to read per servo
    for (size_t i = 0; i < count; i++) {
        out[n++] = ids[i];
    }
    out[n] = checksum(out, n);
    n++;

    return n;
}

/**
 * @brief Extracts every servo's present position from the bytes received after a SYNC_READ.
 *
 * @param rx          Received bytes.
 * @param rx_len      Number of bytes in rx.
 * @param ids         Expected servo IDs, in answer order.
 * @param count       Number of servos.
 * @param positions   Output: present position of each servo, untouched on failure.
 * @param error_flags Output: OR of all ERROR bytes, untouched on failure.
 * @return true if every servo answered, false otherwise.
 */
bool servo_bus_parse_positions(const uint8_t *rx, size_t rx_len, const uint8_t *ids, size_t count,
                               int32_t *positions, uint8_t *error_flags)
{
    // Decode into locals first: outputs are only written if every servo answered.
    int32_t found[MAX_READ_SERVOS];
    uint8_t errors = 0;
    if (count > MAX_READ_SERVOS) {
        return false;
    }

    size_t pos = 0;
    for (size_t i = 0; i < count; i++) {
        // Scan for this servo's status packet; anything else is skipped one byte
        // at a time (same resync idea as frame_parser), e.g. an echo of our request.
        bool matched = false;
        while (pos + SERVO_BUS_POS_STATUS_SIZE <= rx_len) {
            const uint8_t *p = rx + pos;
            if (p[0] == HEADER_BYTE && p[1] == HEADER_BYTE && p[2] == ids[i] &&
                p[3] == STATUS_LENGTH && p[7] == checksum(p, 7)) {
                uint16_t raw = (uint16_t)(p[5] | (p[6] << 8));  // little-endian
                int32_t magnitude = raw & ~SERVO_POSITION_SIGN_BIT;
                found[i] = (raw & SERVO_POSITION_SIGN_BIT) ? -magnitude : magnitude;
                errors |= p[4];
                pos += SERVO_BUS_POS_STATUS_SIZE;
                matched = true;
                break;
            }
            pos++;
        }
        if (!matched) {
            return false;
        }
    }

    for (size_t i = 0; i < count; i++) {
        positions[i] = found[i];
    }
    *error_flags = errors;
    return true;
}
