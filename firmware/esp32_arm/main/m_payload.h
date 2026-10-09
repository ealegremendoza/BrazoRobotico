#ifndef M_PAYLOAD_H
#define M_PAYLOAD_H

/**
 * @file m_payload.h
 * @brief Payload of the M (motors) command: one angle per joint in mrad.
 *
 * Format (see doc/md/bitacora-R09-ros2-control.md):
 *
 *   j1 FS j2 FS j3 FS j4 FS j5 FS j6
 *
 * Each joint is a fixed-width signed field of 5 chars: a '+' or '-' sign and
 * 4 decimal digits (e.g. "+1571", "-0785"). FS is 0x1C.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Joints in an M payload, in arm order: shoulder_pan .. gripper. */
#define M_PAYLOAD_JOINTS 6
/** Chars per joint field: sign + 4 digits. */
#define M_PAYLOAD_FIELD_SIZE 5
/** Largest magnitude a 4-digit field can hold. */
#define M_PAYLOAD_FIELD_MAX 9999
/** Exact payload size: 6 fields + 5 separators = 35 bytes. */
#define M_PAYLOAD_SIZE (M_PAYLOAD_JOINTS * M_PAYLOAD_FIELD_SIZE + (M_PAYLOAD_JOINTS - 1))

/**
 * @brief Decodes an M payload into one angle per joint.
 *
 * All or nothing: if the size, a separator or any field is malformed, it
 * returns false and leaves mrad untouched, so the servos are never commanded
 * with a partially decoded frame. Only the format is checked here; the range
 * (+-3142 mrad) is enforced by mrad_to_counts().
 *
 * @param payload     Payload bytes (between FS and ETX), not NUL-terminated.
 * @param payload_len Number of bytes in payload.
 * @param mrad        Output: M_PAYLOAD_JOINTS angles in milliradians.
 * @return true if the payload is valid and mrad was written, false otherwise.
 */
bool m_payload_parse(const uint8_t *payload, size_t payload_len, int32_t mrad[M_PAYLOAD_JOINTS]);

/**
 * @brief Encodes one angle per joint as an M payload (inverse of m_payload_parse()).
 *
 * Same format as the ros2_control plugin's write() (%+05ld per field, FS between
 * fields). Values outside +-9999 are clamped so every field stays 5 chars wide.
 *
 * @param out      Output buffer, at least M_PAYLOAD_SIZE bytes. Not NUL-terminated.
 * @param out_size Size of out.
 * @param mrad     M_PAYLOAD_JOINTS angles in milliradians.
 * @return M_PAYLOAD_SIZE, or 0 if out is too small.
 */
size_t m_payload_format(uint8_t *out, size_t out_size, const int32_t mrad[M_PAYLOAD_JOINTS]);

#endif // M_PAYLOAD_H
