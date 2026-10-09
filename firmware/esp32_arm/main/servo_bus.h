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

#include <stddef.h>
#include <stdint.h>

#define SERVO_BUS_BROADCAST_ID 0xFE
#define SERVO_BUS_INST_SYNC_WRITE 0x83

/** STS3215 register map (scservo_sdk sms_sts.py). */
#define SERVO_REG_ACC 41

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

#endif // SERVO_BUS_H
