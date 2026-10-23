/**
 * @file esp32_arm.c
 * @brief Firmware entry point: UART setup, PC receive task and M command handling.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "arm_state.h"
#include "board_pins.h"
#include "frame_parser.h"
#include "m_payload.h"
#include "servo_bus.h"
#include "servo_units.h"

// RX ring buffers must be larger than the 128-byte hardware FIFO.
// The PC buffer holds several frames: an M frame is 44 bytes, a D frame up to ~130.
#define PC_UART_RX_BUF_SIZE 1024
#define SERVOS_UART_RX_BUF_SIZE 256

// Max bytes read from UART0 per call; any size works, frame_parser_feed() handles chunking.
#define PC_RX_CHUNK_SIZE 64
#define PC_RX_TASK_STACK_SIZE 4096
#define PC_RX_TASK_PRIORITY 10

// Max wait for all servos to answer a SYNC_READ. They answer in < 1 ms at 1 Mbaud;
// on timeout no M reply is sent and the PC sees a missing answer for that cycle.
#define SERVO_READ_TIMEOUT_MS 10
// Room for an echo of our own SYNC_WRITE + SYNC_READ (if the bus adapter echoes TX)
// plus every servo's status packet.
#define SERVO_RX_BUF_SIZE                                        \
    (SERVO_BUS_SYNC_WRITE_POS_SIZE(M_PAYLOAD_JOINTS) +           \
     SERVO_BUS_SYNC_READ_SIZE(M_PAYLOAD_JOINTS) + M_PAYLOAD_JOINTS * SERVO_BUS_POS_STATUS_SIZE)

// Link watchdog: with no valid frame from the PC for this long, go back to STOPPED.
// The ros2_control plugin waits longer than this after opening the port, so it
// always starts against a STOPPED ESP32 even when opening the port does not reset it.
#define PC_LINK_TIMEOUT_MS 500

/** STOPPED / OPERATING + link watchdog (see arm_state.h). Owned by pc_rx_task. */
static arm_state_t s_arm;

/** Servo bus IDs in M payload order: joint j drives servo j + 1 (shoulder_pan = 1 .. gripper = 6). */
static const uint8_t SERVO_IDS[M_PAYLOAD_JOINTS] = {1, 2, 3, 4, 5, 6};

static const char *TAG = "ESP32-ARM";

/**
 * @brief Installs the UART driver and configures one port as 8N1 without flow control.
 *
 * Aborts (ESP_ERROR_CHECK) if any step fails: without its UARTs the firmware is useless.
 *
 * @param port        UART port (UART_NUM_x).
 * @param baud_rate   Baud rate.
 * @param tx_pin      TX GPIO.
 * @param rx_pin      RX GPIO.
 * @param rx_buf_size RX ring buffer size; must be larger than the 128-byte hardware FIFO.
 */
static void init_uart(uart_port_t port, int baud_rate, int tx_pin, int rx_pin, int rx_buf_size)
{
    const uart_config_t config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // No TX buffer: uart_write_bytes() blocks until the data is in the hardware FIFO.
    ESP_ERROR_CHECK(uart_driver_install(port, rx_buf_size, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(port, &config));
    ESP_ERROR_CHECK(uart_set_pin(port, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

/**
 * @brief Brings up the PC link (UART0) and the servo bus (UART2) from board_pins.h.
 */
static void init_uarts(void)
{
    // Also reconnects GPIO1/GPIO3 to UART0: the bootloader detaches them
    // when the console is moved to UART1 (see board_pins.h).
    init_uart(PC_UART_PORT, PC_UART_BAUD_RATE, PC_UART_TX_PIN, PC_UART_RX_PIN,
              PC_UART_RX_BUF_SIZE);
    init_uart(SERVOS_UART_PORT, SERVOS_UART_BAUD_RATE, SERVOS_UART_TX_PIN, SERVOS_UART_RX_PIN,
              SERVOS_UART_RX_BUF_SIZE);

    ESP_LOGI(TAG, "PC link on UART%d @ %d baud, servo bus on UART%d @ %d baud (TX=%d, RX=%d)",
             PC_UART_PORT, PC_UART_BAUD_RATE, SERVOS_UART_PORT, SERVOS_UART_BAUD_RATE,
             SERVOS_UART_TX_PIN, SERVOS_UART_RX_PIN);
}

/**
 * @brief Reads every servo's present position with one SYNC_READ.
 *
 * Collects the answers until all servos replied or SERVO_READ_TIMEOUT_MS passes.
 * Reads SERVO_BUS_POS_STATUS_SIZE bytes per call after the first batch, so each
 * call returns as soon as the next answer arrives instead of waiting for a full buffer.
 *
 * Timeouts are in FreeRTOS ticks (10 ms at CONFIG_FREERTOS_HZ=100). A 1-tick timeout
 * ends at the next tick interrupt, anywhere from 0 to 10 ms later: with only
 * pdMS_TO_TICKS(10) the read sometimes gave up before the servos (~1.5 ms) answered
 * ("0 bytes received", ~14% of cycles). Each wait adds one tick, so it lasts at
 * least the remaining time; the deadline itself is measured with esp_timer.
 *
 * @param positions Output: present position of each servo in counts, untouched on failure.
 * @return true if every servo answered in time, false otherwise.
 */
static bool read_servo_positions(int32_t positions[M_PAYLOAD_JOINTS])
{
    uint8_t request[SERVO_BUS_SYNC_READ_SIZE(M_PAYLOAD_JOINTS)];
    size_t request_size = servo_bus_build_sync_read_pos(request, sizeof(request), SERVO_IDS,
                                                        M_PAYLOAD_JOINTS);
    uart_write_bytes(SERVOS_UART_PORT, request, request_size);

    static uint8_t last_errors = 0;
    uint8_t rx[SERVO_RX_BUF_SIZE];
    size_t rx_len = 0;
    size_t want = M_PAYLOAD_JOINTS * SERVO_BUS_POS_STATUS_SIZE;  // no echo: exactly the answers
    int64_t deadline_us = esp_timer_get_time() + SERVO_READ_TIMEOUT_MS * 1000;

    while (rx_len < sizeof(rx)) {
        int64_t remaining_us = deadline_us - esp_timer_get_time();
        if (remaining_us <= 0) {
            break;
        }
        // Round up to whole ms, then add one tick: see the tick granularity note above.
        TickType_t wait = pdMS_TO_TICKS((uint32_t)((remaining_us + 999) / 1000)) + 1;

        size_t space = sizeof(rx) - rx_len;
        int n = uart_read_bytes(SERVOS_UART_PORT, rx + rx_len, want < space ? want : space, wait);
        if (n <= 0) {
            continue;  // nothing arrived in this wait: the deadline check ends the loop
        }
        rx_len += (size_t)n;

        uint8_t errors;
        if (servo_bus_parse_positions(rx, rx_len, SERVO_IDS, M_PAYLOAD_JOINTS, positions, &errors)) {
            if (errors != last_errors) {
                // Only log changes: at 50 Hz a persistent error would flood the console.
                ESP_LOGW(TAG, "Servo status error flags: 0x%02x", errors);
                last_errors = errors;
            }
            return true;
        }
        want = SERVO_BUS_POS_STATUS_SIZE;  // still missing answers (or an echo arrived first)
    }

    ESP_LOGW(TAG, "SYNC_READ: not every servo answered (%u bytes received)", (unsigned)rx_len);
    return false;
}

/**
 * @brief Sends the M feedback frame with the present joint angles to the PC.
 *
 * @param counts Present position of each servo, in counts.
 */
static void send_m_feedback(const int32_t counts[M_PAYLOAD_JOINTS])
{
    int32_t mrad[M_PAYLOAD_JOINTS];
    for (size_t j = 0; j < M_PAYLOAD_JOINTS; j++) {
        mrad[j] = counts_to_mrad(counts[j]);
    }

    uint8_t payload[M_PAYLOAD_SIZE];
    size_t payload_len = m_payload_format(payload, sizeof(payload), mrad);

    uint8_t frame[FRAME_MAX_SIZE];
    size_t frame_size = frame_build(frame, sizeof(frame), 'M', payload, payload_len);
    uart_write_bytes(PC_UART_PORT, frame, frame_size);

    ESP_LOGI(TAG, "Feedback: counts [%ld %ld %ld %ld %ld %ld] -> mrad [%ld %ld %ld %ld %ld %ld]",
             (long)counts[0], (long)counts[1], (long)counts[2], (long)counts[3], (long)counts[4],
             (long)counts[5], (long)mrad[0], (long)mrad[1], (long)mrad[2], (long)mrad[3],
             (long)mrad[4], (long)mrad[5]);
}

/**
 * @brief Handles an M frame: moves the servos (SYNC_WRITE, only when OPERATING),
 *        reads where they are (SYNC_READ) and answers the PC with an M feedback frame.
 *
 * @param payload     Payload bytes (between FS and ETX).
 * @param payload_len Number of bytes in payload.
 */
static void handle_m_frame(const uint8_t *payload, size_t payload_len)
{
    int32_t mrad[M_PAYLOAD_JOINTS];
    if (!m_payload_parse(payload, payload_len, mrad)) {
        ESP_LOGW(TAG, "M: malformed payload (%u bytes), frame ignored", (unsigned)payload_len);
        return;
    }

    // Same joint order as robotic_arm_ros2_control.xacro (see SERVO_IDS).
    servo_target_t targets[M_PAYLOAD_JOINTS];
    for (size_t j = 0; j < M_PAYLOAD_JOINTS; j++) {
        targets[j] = (servo_target_t){
            .id = SERVO_IDS[j],
            .position = (uint16_t)mrad_to_counts(mrad[j]),  // always 0..4095
            .speed = CONFIG_ARM_SERVO_MOVE_SPEED,
            .acc = CONFIG_ARM_SERVO_MOVE_ACC,
        };
    }

    uart_flush_input(SERVOS_UART_PORT);  // drop stale bytes: only this cycle's answers count
    if (arm_state_can_move(&s_arm)) {
        uint8_t packet[SERVO_BUS_SYNC_WRITE_POS_SIZE(M_PAYLOAD_JOINTS)];
        size_t packet_size = servo_bus_build_sync_write_pos(packet, sizeof(packet), targets,
                                                            M_PAYLOAD_JOINTS);
        uart_write_bytes(SERVOS_UART_PORT, packet, packet_size);
    }

    ESP_LOGI(TAG, "M (%s): mrad [%ld %ld %ld %ld %ld %ld] -> counts [%u %u %u %u %u %u]",
             arm_state_can_move(&s_arm) ? "move" : "stopped, read only", (long)mrad[0],
             (long)mrad[1], (long)mrad[2], (long)mrad[3], (long)mrad[4], (long)mrad[5],
             targets[0].position, targets[1].position, targets[2].position, targets[3].position,
             targets[4].position, targets[5].position);

    // Request/response (R09): one M feedback per M command. No answer if the
    // servos did not reply; the PC detects the missing frame.
    int32_t present[M_PAYLOAD_JOINTS];
    if (read_servo_positions(present)) {
        send_m_feedback(present);
    }
}

/**
 * @brief frame_parser callback: dispatches each valid frame by its command id.
 *
 * @param cid         Command id.
 * @param payload     Payload bytes, only valid during the call.
 * @param payload_len Number of bytes in payload.
 * @param ctx         Unused.
 */
static void on_pc_frame(char cid, const uint8_t *payload, size_t payload_len, void *ctx)
{
    (void)ctx;

    // Link watchdog: a long silence means the PC went away (plugin stopped, cable
    // unplugged). Never resume moving on stale targets: require a new S.
    if (arm_state_on_frame(&s_arm, esp_timer_get_time())) {
        ESP_LOGW(TAG, "No frame from the PC for > %d ms: STOPPED (waiting for S)", PC_LINK_TIMEOUT_MS);
    }

    switch (cid) {
    case 'M':
        handle_m_frame(payload, payload_len);
        break;
    case 'S':
        // Start (R03). The reply (M or E) and the limits in param_init come with the bus task.
        if (arm_state_on_start(&s_arm, ARM_S_PARAMS_EMPTY, true).state_changed) {
            ESP_LOGI(TAG, "S: OPERATING, M frames now move the servos");
        }
        break;
    default:
        // D and the rest arrive with E09.
        ESP_LOGW(TAG, "Unhandled CID '%c' (%u payload bytes)", cid, (unsigned)payload_len);
        break;
    }
}

/**
 * @brief Reads bytes from the PC link (UART0) and feeds them to the frame parser.
 *
 * Blocks until one byte arrives, then takes whatever is already buffered without
 * waiting. Asking uart_read_bytes() for a full chunk with a timeout would wait for
 * the timeout on every frame (an M frame, 44 bytes, never fills 64), adding 20-30 ms
 * of latency: more than one 20 ms ros2_control cycle.
 *
 * @param arg Unused.
 */
static void pc_rx_task(void *arg)
{
    (void)arg;

    // static: the parser holds a 133-byte buffer, keep it off the task stack.
    static frame_parser_t parser;
    frame_parser_init(&parser, on_pc_frame, NULL);
    arm_state_init(&s_arm, PC_LINK_TIMEOUT_MS * 1000LL, M_PAYLOAD_JOINTS);

    uint8_t chunk[PC_RX_CHUNK_SIZE];
    while (1) {
        // Nothing else to do until the PC sends something: wait for the first byte.
        int n = uart_read_bytes(PC_UART_PORT, chunk, 1, portMAX_DELAY);
        if (n <= 0) {
            continue;
        }

        // Then grab what is already buffered, without waiting for more.
        size_t buffered = 0;
        uart_get_buffered_data_len(PC_UART_PORT, &buffered);
        size_t want = buffered < sizeof(chunk) - 1 ? buffered : sizeof(chunk) - 1;
        if (want > 0) {
            int more = uart_read_bytes(PC_UART_PORT, chunk + 1, want, 0);
            if (more > 0) {
                n += more;
            }
        }

        frame_parser_feed(&parser, chunk, (size_t)n);
    }
}

/**
 * @brief ESP-IDF entry point: initializes the UARTs and starts the PC receive task.
 */
void app_main(void)
{
    init_uarts();
    xTaskCreate(pc_rx_task, "pc_rx", PC_RX_TASK_STACK_SIZE, NULL, PC_RX_TASK_PRIORITY, NULL);
}
