#ifndef SERVO_BUS_H
#define SERVO_BUS_H

/**
 * @file servo_bus.h
 * @brief Packets for the Feetech STS3215 servo bus (see doc/md/bitacora-E04-protocolo-driver.md).
 *
 * Packet format:
 *
 *   0xFF 0xFF | ID | Length | Instruction | Params... | Checksum
 *
 *   - Length:   Instruction + Params + Checksum
 *   - Checksum: ~(ID + Length + Instruction + Params) & 0xFF
 *
 * Builders only fill a byte buffer: they know nothing about the UART.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SERVO_BUS_BROADCAST_ID 0xFE
#define SERVO_BUS_INST_SYNC_READ 0x82
#define SERVO_BUS_INST_SYNC_WRITE 0x83

/** STS3215 register map (doc/datasheets/ST3215 memory register map-EN.xls). */
#define SERVO_REG_ACC 41
#define SERVO_REG_PRESENT_POSITION 56

/** Bit 15 of a 2-byte position: direction (sign) bit, magnitude in bits 0..14. */
#define SERVO_POSITION_SIGN_BIT 0x8000

/**
 * Bytes written per servo by a position SYNC_WRITE, from SERVO_REG_ACC:
 * acc, posL, posH, timeL, timeH, speedL, speedH (same block as SyncWritePosEx).
 */
#define SERVO_BUS_POS_DATA_SIZE 7

/** Header (FF FF), ID, Length, Instruction, start address, data length and checksum. */
#define SERVO_BUS_SYNC_WRITE_OVERHEAD 8

/** Size of a position SYNC_WRITE packet for n servos. */
#define SERVO_BUS_SYNC_WRITE_POS_SIZE(n) \
    (SERVO_BUS_SYNC_WRITE_OVERHEAD + (n) * (1 + SERVO_BUS_POS_DATA_SIZE))

/** Size of a SYNC_READ request for n servos: header, ID, Length, Instruction, address, data length, n IDs, checksum. */
#define SERVO_BUS_SYNC_READ_SIZE(n) (8 + (n))

/** Size of one servo's status packet answering a 2-byte read: FF FF ID Length ERROR lo hi checksum. */
#define SERVO_BUS_POS_STATUS_SIZE 8

/**
 * @brief Goal for one servo in a position SYNC_WRITE.
 */
typedef struct {
    uint8_t id;        /**< Servo bus ID. */
    uint16_t position; /**< Goal position in counts (0..4095). */
    uint16_t speed;    /**< Max speed in steps/s. */
    uint8_t acc;       /**< Acceleration in units of 100 steps/s^2. */
} servo_target_t;

/**
 * @brief Builds a broadcast SYNC_WRITE that moves several servos with one packet.
 *
 * The servos do not answer a broadcast packet (fire and forget).
 *
 * @param out      Output buffer.
 * @param out_size Size of out; must be at least SERVO_BUS_SYNC_WRITE_POS_SIZE(count).
 * @param targets  One goal per servo.
 * @param count    Number of servos.
 * @return Packet size in bytes, or 0 if out is too small or the packet would not fit
 *         the 1-byte Length field.
 */
size_t servo_bus_build_sync_write_pos(uint8_t *out, size_t out_size, const servo_target_t *targets,
                                      size_t count);

/**
 * @brief Builds a SYNC_READ that asks several servos for their present position.
 *
 * Each servo answers with its own status packet (SERVO_BUS_POS_STATUS_SIZE bytes),
 * in the order of ids. Parse the answers with servo_bus_parse_positions().
 *
 * @param out      Output buffer.
 * @param out_size Size of out; must be at least SERVO_BUS_SYNC_READ_SIZE(count).
 * @param ids      Servo IDs to query, in the order they must answer.
 * @param count    Number of servos.
 * @return Packet size in bytes, or 0 if out is too small.
 */
size_t servo_bus_build_sync_read_pos(uint8_t *out, size_t out_size, const uint8_t *ids, size_t count);

/**
 * @brief Extracts every servo's present position from the bytes received after a SYNC_READ.
 *
 * Looks for each status packet (FF FF ID 04 ERROR lo hi checksum) in ids order and
 * skips anything else one byte at a time: an echo of our own request (ID FE),
 * noise, or packets with a bad checksum. All or nothing: positions is only written
 * when every servo answered.
 *
 * @param rx          Received bytes.
 * @param rx_len      Number of bytes in rx.
 * @param ids         Expected servo IDs, in answer order.
 * @param count       Number of servos.
 * @param positions   Output: present position of each servo, in counts (sign bit applied).
 * @param error_flags Output: OR of the ERROR bytes of all answers (0 = no servo reported an error).
 * @return true if every servo answered, false otherwise (incomplete: wait for more bytes).
 */
bool servo_bus_parse_positions(const uint8_t *rx, size_t rx_len, const uint8_t *ids, size_t count,
                               int32_t *positions, uint8_t *error_flags);

#endif // SERVO_BUS_H
