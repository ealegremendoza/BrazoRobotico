/**
 * @file m_payload.c
 * @brief M command payload decoding (see m_payload.h).
 */

#include "m_payload.h"

#include "frame_parser.h"

/**
 * @brief Decodes one fixed-width signed field ("+1571", "-0785").
 *
 * @param field Pointer to the sign char; M_PAYLOAD_FIELD_SIZE bytes are read.
 * @param value Output: decoded value.
 * @return true if the field is a sign followed by 4 decimal digits.
 */
static bool parse_field(const uint8_t *field, int32_t *value)
{
    if (field[0] != '+' && field[0] != '-') {
        return false;
    }

    int32_t magnitude = 0;
    for (size_t i = 1; i < M_PAYLOAD_FIELD_SIZE; i++) {
        if (field[i] < '0' || field[i] > '9') {
            return false;
        }
        magnitude = magnitude * 10 + (field[i] - '0');
    }

    *value = field[0] == '-' ? -magnitude : magnitude;
    return true;
}

/**
 * @brief Decodes an M payload into one angle per joint (all or nothing).
 *
 * @param payload     Payload bytes (between FS and ETX), not NUL-terminated.
 * @param payload_len Number of bytes in payload.
 * @param mrad        Output: M_PAYLOAD_JOINTS angles in milliradians, untouched on failure.
 * @return true if the payload is valid and mrad was written, false otherwise.
 */
bool m_payload_parse(const uint8_t *payload, size_t payload_len, int32_t mrad[M_PAYLOAD_JOINTS])
{
    if (payload_len != M_PAYLOAD_SIZE) {
        return false;
    }

    // Decode into a local array first: mrad is only written if every field is valid.
    int32_t values[M_PAYLOAD_JOINTS];
    for (size_t j = 0; j < M_PAYLOAD_JOINTS; j++) {
        // Each field is followed by FS, except the last one.
        const uint8_t *field = payload + j * (M_PAYLOAD_FIELD_SIZE + 1);

        if (j > 0 && field[-1] != FRAME_FS) {
            return false;
        }
        if (!parse_field(field, &values[j])) {
            return false;
        }
    }

    for (size_t j = 0; j < M_PAYLOAD_JOINTS; j++) {
        mrad[j] = values[j];
    }
    return true;
}
