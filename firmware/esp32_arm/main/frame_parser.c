/**
 * @file frame_parser.c
 * @brief PC <-> ESP32 frame reassembly (see frame_parser.h).
 */

#include "frame_parser.h"

#include <string.h>

/** Offsets inside a frame, counted from STX (buf[0]). */
#define LEN_OFFSET FRAME_STX_SIZE                      // first LEN digit
#define CID_OFFSET (LEN_OFFSET + FRAME_LEN_DIGITS)     // command id
#define FS_OFFSET (CID_OFFSET + 1)                     // separator after the CID
#define PAYLOAD_OFFSET (FS_OFFSET + 1)                 // first payload byte
#define HEADER_SIZE CID_OFFSET                         // STX + LEN: bytes not counted by LEN

/**
 * @brief Removes the first n bytes of the buffer, keeping the rest at buf[0].
 *
 * @param parser Parser state.
 * @param n      Bytes to drop (at most parser->len).
 */
static void drop(frame_parser_t *parser, size_t n)
{
    memmove(parser->buf, parser->buf + n, parser->len - n);
    parser->len -= n;
}

/**
 * @brief Parses the 4 ASCII digits of LEN.
 *
 * @param digits Pointer to the first LEN digit.
 * @param len    Output: decoded value.
 * @return 1 if all 4 chars are decimal digits, 0 otherwise.
 */
static int parse_len(const uint8_t *digits, size_t *len)
{
    size_t value = 0;
    for (size_t i = 0; i < FRAME_LEN_DIGITS; i++) {
        if (digits[i] < '0' || digits[i] > '9') {
            return 0;
        }
        value = value * 10 + (digits[i] - '0');
    }
    *len = value;
    return 1;
}

/**
 * @brief Checks the fixed bytes and the LRC of a complete candidate frame at buf[0].
 *
 * @param buf        Frame bytes, starting at STX.
 * @param frame_size Total size from STX to LRC.
 * @return 1 if FS, ETX and LRC are correct, 0 otherwise.
 */
static int frame_is_valid(const uint8_t *buf, size_t frame_size)
{
    size_t etx_pos = frame_size - 2;
    size_t lrc_pos = frame_size - 1;

    if (buf[FS_OFFSET] != FRAME_FS || buf[etx_pos] != FRAME_ETX) {
        return 0;
    }

    // LRC covers LEN..ETX: everything except STX and the LRC itself.
    uint8_t lrc = 0;
    for (size_t i = LEN_OFFSET; i <= etx_pos; i++) {
        lrc ^= buf[i];
    }
    return lrc == buf[lrc_pos];
}

/**
 * @brief Extracts every complete frame from the buffer (R09 read() algorithm).
 *
 * Invalid data is dropped one byte at a time (not the whole candidate frame):
 * a corrupted frame may contain the start of a valid one, e.g. an LRC equal to STX.
 * Returns when the buffer is empty or holds an incomplete frame.
 *
 * @param parser Parser state.
 */
static void parse_buffer(frame_parser_t *parser)
{
    while (parser->len > 0) {
        // 1. Discard everything before the first STX.
        uint8_t *stx = memchr(parser->buf, FRAME_STX, parser->len);
        if (stx == NULL) {
            parser->len = 0;
            return;
        }
        drop(parser, (size_t)(stx - parser->buf));

        // 2. Wait for the 4 LEN digits.
        if (parser->len < HEADER_SIZE) {
            return;
        }

        // 3. LEN must be 4 digits within range.
        size_t len;
        if (!parse_len(parser->buf + LEN_OFFSET, &len) || len < FRAME_MIN_LEN ||
            len > FRAME_MAX_LEN) {
            drop(parser, 1);
            continue;
        }

        // 4. Wait for the whole frame.
        size_t frame_size = HEADER_SIZE + len;
        if (parser->len < frame_size) {
            return;
        }

        // 5. FS, ETX and LRC must match.
        if (!frame_is_valid(parser->buf, frame_size)) {
            drop(parser, 1);
            continue;
        }

        // 6. Valid frame: hand it over, then remove it.
        size_t payload_len = len - FRAME_MIN_LEN;
        parser->on_frame((char)parser->buf[CID_OFFSET], parser->buf + PAYLOAD_OFFSET,
                         payload_len, parser->ctx);
        drop(parser, frame_size);
    }
}

/**
 * @brief Clears the buffer and sets the handler. Call before the first feed.
 *
 * @param parser   Parser to initialize.
 * @param on_frame Handler called for every valid frame.
 * @param ctx      User pointer passed back to on_frame.
 */
void frame_parser_init(frame_parser_t *parser, frame_handler_t on_frame, void *ctx)
{
    parser->len = 0;
    parser->on_frame = on_frame;
    parser->ctx = ctx;
}

/**
 * @brief Consumes bytes and calls on_frame for every valid frame found.
 *
 * @param parser Parser state.
 * @param data   Received bytes (any chunk size).
 * @param size   Number of bytes in data.
 */
void frame_parser_feed(frame_parser_t *parser, const uint8_t *data, size_t size)
{
    // Copy only what fits, parse (which frees space), repeat: the buffer never
    // overflows whatever the chunk size.
    while (size > 0) {
        size_t space = sizeof(parser->buf) - parser->len;
        size_t n = size < space ? size : space;

        memcpy(parser->buf + parser->len, data, n);
        parser->len += n;
        data += n;
        size -= n;

        parse_buffer(parser);

        // A full buffer always starts with a complete candidate frame (LEN is
        // capped), so parse_buffer() frees space. Guard anyway against a stall.
        if (parser->len == sizeof(parser->buf)) {
            drop(parser, 1);
        }
    }
}
