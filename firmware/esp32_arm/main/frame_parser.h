#ifndef FRAME_PARSER_H
#define FRAME_PARSER_H

/**
 * @file frame_parser.h
 * @brief Reassembles PC <-> ESP32 protocol frames from a raw byte stream.
 *
 * Same format and resync algorithm as the ros2_control plugin's read()
 * (see doc/md/bitacora-R09-ros2-control.md):
 *
 *   STX | LEN | CID | FS | payload | ETX | LRC
 *
 *   - STX: 0x02
 *   - LEN: 4 ASCII decimal digits, size of CID + FS + payload + ETX + LRC
 *   - CID: 1 ASCII char (command id)
 *   - FS:  0x1C
 *   - ETX: 0x03
 *   - LRC: XOR of LEN..ETX (everything except STX and the LRC itself)
 *
 * The parser knows nothing about the UART: feed it bytes from any source.
 */

#include <stddef.h>
#include <stdint.h>

#define FRAME_STX 0x02
#define FRAME_ETX 0x03
#define FRAME_FS 0x1C

#define FRAME_STX_SIZE 1
#define FRAME_LEN_DIGITS 4
/** Smallest LEN: CID + FS + ETX + LRC (empty payload). */
#define FRAME_MIN_LEN 4
/** Sanity cap for LEN: a corrupted LEN must not stall the parser (see R09/R03). */
#define FRAME_MAX_LEN 128
/** Largest complete frame, from STX to LRC. */
#define FRAME_MAX_SIZE (FRAME_STX_SIZE + FRAME_LEN_DIGITS + FRAME_MAX_LEN)

/**
 * @brief Called once per valid frame.
 *
 * @param cid         Command id of the frame.
 * @param payload     Bytes between FS and ETX. Points into the parser's buffer:
 *                    only valid during the call (copy it to keep it). Not NUL-terminated.
 * @param payload_len Number of bytes in payload (0 if empty).
 * @param ctx         The pointer given to frame_parser_init().
 */
typedef void (*frame_handler_t)(char cid, const uint8_t *payload, size_t payload_len, void *ctx);

/**
 * @brief Parser state. Allocate one per byte stream and pass it by pointer.
 */
typedef struct {
    uint8_t buf[FRAME_MAX_SIZE]; /**< Pending bytes; a candidate frame always starts at buf[0]. */
    size_t len;                  /**< Number of valid bytes in buf. */
    frame_handler_t on_frame;    /**< Handler for valid frames. */
    void *ctx;                   /**< Passed back to on_frame (e.g. a test's result list). */
} frame_parser_t;

/**
 * @brief Clears the buffer and sets the handler. Call before the first feed.
 *
 * @param parser   Parser to initialize.
 * @param on_frame Handler called for every valid frame.
 * @param ctx      User pointer passed back to on_frame.
 */
void frame_parser_init(frame_parser_t *parser, frame_handler_t on_frame, void *ctx);

/**
 * @brief Consumes bytes and calls on_frame for every valid frame found.
 *
 * Accepts chunks of any size: a frame may be split across calls, and one call
 * may contain several frames. Invalid bytes are dropped one at a time to resync;
 * an incomplete frame is kept until more bytes arrive.
 *
 * @param parser Parser state.
 * @param data   Received bytes.
 * @param size   Number of bytes in data.
 */
void frame_parser_feed(frame_parser_t *parser, const uint8_t *data, size_t size);

#endif // FRAME_PARSER_H
