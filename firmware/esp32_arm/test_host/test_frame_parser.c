/**
 * @file test_frame_parser.c
 * @brief Host (PC) tests for main/frame_parser.c. No ESP-IDF needed.
 *
 * Build and run from firmware/esp32_arm:
 *
 *   gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
 *       test_host/test_frame_parser.c main/frame_parser.c -o /tmp/test_frame_parser \
 *       && /tmp/test_frame_parser
 */

#include <stdio.h>
#include <string.h>

#include "frame_parser.h"

#define MAX_FRAMES 64
#define MAX_PAYLOAD FRAME_MAX_LEN

typedef struct {
    char cid;
    uint8_t payload[MAX_PAYLOAD];
    size_t payload_len;
} received_frame_t;

/** Frames collected by the handler; passed as the parser's ctx. */
typedef struct {
    received_frame_t frames[MAX_FRAMES];
    size_t count;
} received_t;

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void collect(char cid, const uint8_t *payload, size_t payload_len, void *ctx)
{
    received_t *rx = ctx;
    if (rx->count < MAX_FRAMES) {
        received_frame_t *f = &rx->frames[rx->count];
        f->cid = cid;
        memcpy(f->payload, payload, payload_len);
        f->payload_len = payload_len;
    }
    rx->count++;
}

/**
 * @brief Builds a valid frame (same encoding as the ros2_control plugin's write()).
 *
 * @param out     Output buffer (at least FRAME_MAX_SIZE bytes).
 * @param cid     Command id.
 * @param payload Payload bytes.
 * @param plen    Payload size.
 * @return Total frame size.
 */
static size_t make_frame(uint8_t *out, char cid, const uint8_t *payload, size_t plen)
{
    size_t len = plen + FRAME_MIN_LEN;
    size_t n = 0;
    out[n++] = FRAME_STX;
    n += (size_t)sprintf((char *)out + n, "%04zu", len);
    out[n++] = (uint8_t)cid;
    out[n++] = FRAME_FS;
    memcpy(out + n, payload, plen);
    n += plen;
    out[n++] = FRAME_ETX;
    uint8_t lrc = 0;
    for (size_t i = 1; i < n; i++) {
        lrc ^= out[i];
    }
    out[n++] = lrc;
    return n;
}

/** M payload for 6 joints: +1571 FS -0785 FS +0000 FS -3142 FS +3142 FS +0001. */
static size_t m_payload(uint8_t *out)
{
    const char *joints[] = {"+1571", "-0785", "+0000", "-3142", "+3142", "+0001"};
    size_t n = 0;
    for (size_t i = 0; i < 6; i++) {
        if (i > 0) {
            out[n++] = FRAME_FS;
        }
        memcpy(out + n, joints[i], 5);
        n += 5;
    }
    return n;
}

static void setup(frame_parser_t *p, received_t *rx)
{
    memset(rx, 0, sizeof(*rx));
    frame_parser_init(p, collect, rx);
}

static void test_single_m_frame(void)
{
    puts("single M frame");
    frame_parser_t p;
    received_t rx;
    setup(&p, &rx);
    uint8_t payload[64], frame[FRAME_MAX_SIZE];
    size_t plen = m_payload(payload);
    size_t n = make_frame(frame, 'M', payload, plen);

    CHECK(n == 44);                              // R09: an M frame is 44 bytes
    CHECK(memcmp(frame + 1, "0039", 4) == 0);    // R09: LEN = 0039
    frame_parser_feed(&p, frame, n);
    CHECK(rx.count == 1);
    CHECK(rx.frames[0].cid == 'M');
    CHECK(rx.frames[0].payload_len == plen);
    CHECK(memcmp(rx.frames[0].payload, payload, plen) == 0);
    CHECK(p.len == 0);
}

static void test_byte_by_byte(void)
{
    puts("frame fed one byte at a time");
    frame_parser_t p;
    received_t rx;
    setup(&p, &rx);
    uint8_t payload[64], frame[FRAME_MAX_SIZE];
    size_t n = make_frame(frame, 'M', payload, m_payload(payload));

    for (size_t i = 0; i < n; i++) {
        CHECK(rx.count == 0);
        frame_parser_feed(&p, &frame[i], 1);
    }
    CHECK(rx.count == 1);
}

static void test_incomplete_kept(void)
{
    puts("incomplete frame is kept until the rest arrives");
    frame_parser_t p;
    received_t rx;
    setup(&p, &rx);
    uint8_t frame[FRAME_MAX_SIZE];
    size_t n = make_frame(frame, 'D', (const uint8_t *)"07", 2);

    frame_parser_feed(&p, frame, 3);   // STX + 2 LEN digits
    CHECK(rx.count == 0 && p.len == 3);
    frame_parser_feed(&p, frame + 3, n - 4);   // all but the LRC
    CHECK(rx.count == 0 && p.len == n - 1);
    frame_parser_feed(&p, frame + n - 1, 1);
    CHECK(rx.count == 1 && rx.frames[0].cid == 'D');
}

static void test_many_frames_one_chunk(void)
{
    puts("50 frames in a single chunk (larger than the buffer)");
    frame_parser_t p;
    received_t rx;
    setup(&p, &rx);
    uint8_t payload[64], stream[50 * FRAME_MAX_SIZE];
    size_t plen = m_payload(payload), n = 0;
    for (int i = 0; i < 50; i++) {
        n += make_frame(stream + n, 'M', payload, plen);
    }
    frame_parser_feed(&p, stream, n);
    CHECK(rx.count == 50);
    CHECK(p.len == 0);
}

static void test_garbage_before(void)
{
    puts("garbage and log text before a frame");
    frame_parser_t p;
    received_t rx;
    setup(&p, &rx);
    uint8_t stream[256];
    const char *junk = "I (29) boot: ESP-IDF v5.5\r\n\x03\x1c\xff";
    size_t n = strlen(junk);
    memcpy(stream, junk, n);
    n += make_frame(stream + n, 'D', (const uint8_t *)"07", 2);

    frame_parser_feed(&p, stream, n);
    CHECK(rx.count == 1);
    CHECK(rx.frames[0].payload_len == 2 && memcmp(rx.frames[0].payload, "07", 2) == 0);
}

static void test_no_stx_clears_buffer(void)
{
    puts("1000 bytes without STX leave the buffer empty");
    frame_parser_t p;
    received_t rx;
    setup(&p, &rx);
    uint8_t junk[1000];
    memset(junk, 'x', sizeof(junk));
    frame_parser_feed(&p, junk, sizeof(junk));
    CHECK(rx.count == 0 && p.len == 0);
}

static void test_bad_lrc_then_good(void)
{
    puts("bad LRC is dropped, next frame still parsed");
    frame_parser_t p;
    received_t rx;
    setup(&p, &rx);
    uint8_t stream[2 * FRAME_MAX_SIZE];
    size_t n1 = make_frame(stream, 'D', (const uint8_t *)"07", 2);
    stream[n1 - 1] ^= 0x55;   // corrupt LRC
    size_t n = n1 + make_frame(stream + n1, 'D', (const uint8_t *)"08", 2);

    frame_parser_feed(&p, stream, n);
    CHECK(rx.count == 1);
    CHECK(memcmp(rx.frames[0].payload, "08", 2) == 0);
}

static void test_bad_etx_and_fs(void)
{
    puts("wrong ETX or FS is rejected");
    frame_parser_t p;
    received_t rx;
    setup(&p, &rx);
    uint8_t frame[FRAME_MAX_SIZE];
    size_t n = make_frame(frame, 'D', (const uint8_t *)"07", 2);
    frame[n - 2] = 'X';   // ETX
    frame_parser_feed(&p, frame, n);
    CHECK(rx.count == 0);

    setup(&p, &rx);
    n = make_frame(frame, 'D', (const uint8_t *)"07", 2);
    frame[6] = ',';       // FS
    frame_parser_feed(&p, frame, n);
    CHECK(rx.count == 0);
}

static void test_valid_frame_inside_corrupted_one(void)
{
    puts("valid frame starting inside a corrupted one is recovered (drop 1 byte, not the frame)");
    frame_parser_t p;
    received_t rx;
    setup(&p, &rx);
    uint8_t stream[3 * FRAME_MAX_SIZE];
    // A fake header claiming LEN 8, immediately followed by a real 11-byte frame:
    // the fake candidate (13 bytes) swallows the real frame's start and fails ETX/LRC.
    size_t n = 0;
    stream[n++] = FRAME_STX;
    memcpy(stream + n, "0008", 4);
    n += 4;
    n += make_frame(stream + n, 'D', (const uint8_t *)"07", 2);

    frame_parser_feed(&p, stream, n);
    CHECK(rx.count == 1);
    CHECK(rx.frames[0].cid == 'D');
}

static void test_corrupted_len_waits_for_bytes(void)
{
    puts("a corrupted LEN delays (does not lose) the frames after it");
    frame_parser_t p;
    received_t rx;
    setup(&p, &rx);
    uint8_t stream[3 * FRAME_MAX_SIZE];
    // Fake header claiming LEN 20 (25 bytes) + one real 11-byte frame: the
    // candidate is incomplete, so the parser must wait (it cannot know LEN is wrong).
    size_t n = 0;
    stream[n++] = FRAME_STX;
    memcpy(stream + n, "0020", 4);
    n += 4;
    n += make_frame(stream + n, 'D', (const uint8_t *)"07", 2);
    frame_parser_feed(&p, stream, n);
    CHECK(rx.count == 0);

    // The next frame completes the fake candidate, which then fails and is
    // dropped byte by byte: both real frames come out. FRAME_MAX_LEN bounds the wait.
    size_t n2 = make_frame(stream + n, 'D', (const uint8_t *)"08", 2);
    frame_parser_feed(&p, stream + n, n2);
    CHECK(rx.count == 2);
    CHECK(memcmp(rx.frames[0].payload, "07", 2) == 0);
    CHECK(memcmp(rx.frames[1].payload, "08", 2) == 0);
}

static void test_len_validation(void)
{
    puts("LEN: non-digits, below min and above max are rejected; limits accepted");
    frame_parser_t p;
    received_t rx;
    uint8_t stream[2 * FRAME_MAX_SIZE], payload[FRAME_MAX_LEN];
    memset(payload, 'a', sizeof(payload));

    // Corrupted LEN must not stall the parser: the next frame is still found.
    const char *bad_lens[] = {"9999", "0129", "0003", "00a9", " 039"};
    for (size_t i = 0; i < sizeof(bad_lens) / sizeof(bad_lens[0]); i++) {
        setup(&p, &rx);
        size_t n = 0;
        stream[n++] = FRAME_STX;
        memcpy(stream + n, bad_lens[i], 4);
        n += 4;
        n += make_frame(stream + n, 'D', (const uint8_t *)"07", 2);
        frame_parser_feed(&p, stream, n);
        CHECK(rx.count == 1);
    }

    setup(&p, &rx);   // LEN = 4: empty payload
    size_t n = make_frame(stream, 'S', payload, 0);
    frame_parser_feed(&p, stream, n);
    CHECK(rx.count == 1 && rx.frames[0].cid == 'S' && rx.frames[0].payload_len == 0);

    setup(&p, &rx);   // LEN = 128: largest frame, exactly fills the buffer
    n = make_frame(stream, 'D', payload, FRAME_MAX_LEN - FRAME_MIN_LEN);
    CHECK(n == FRAME_MAX_SIZE);
    frame_parser_feed(&p, stream, n);
    CHECK(rx.count == 1 && rx.frames[0].payload_len == FRAME_MAX_LEN - FRAME_MIN_LEN);
}

static void test_split_at_every_point(void)
{
    puts("two frames split at every possible point");
    uint8_t payload[64], stream[2 * FRAME_MAX_SIZE];
    size_t plen = m_payload(payload);
    size_t n = make_frame(stream, 'M', payload, plen);
    n += make_frame(stream + n, 'D', (const uint8_t *)"07", 2);

    for (size_t cut = 0; cut <= n; cut++) {
        frame_parser_t p;
        received_t rx;
        setup(&p, &rx);
        frame_parser_feed(&p, stream, cut);
        frame_parser_feed(&p, stream + cut, n - cut);
        CHECK(rx.count == 2);
        CHECK(rx.frames[0].cid == 'M' && rx.frames[1].cid == 'D');
    }
}

int main(void)
{
    test_single_m_frame();
    test_byte_by_byte();
    test_incomplete_kept();
    test_many_frames_one_chunk();
    test_garbage_before();
    test_no_stx_clears_buffer();
    test_bad_lrc_then_good();
    test_bad_etx_and_fs();
    test_valid_frame_inside_corrupted_one();
    test_corrupted_len_waits_for_bytes();
    test_len_validation();
    test_split_at_every_point();

    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
