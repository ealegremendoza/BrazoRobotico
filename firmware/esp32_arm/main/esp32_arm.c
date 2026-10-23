/**
 * @file esp32_arm.c
 * @brief Firmware entry point: UART setup and the PC receive task (parser + forward).
 *
 * Tasks (doc/md/bitacora-E10-panel-lcd.md, DS18 #1):
 *
 * - bus (priority 11, arm_bus.c): only owner of the servo bus (UART2) and of arm_state;
 *   answers the PC on UART0.
 * - pc_rx (priority 10): reads UART0, reassembles frames and forwards M and S to the bus.
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

#include "arm_bus.h"
#include "arm_limits.h"
#include "board_pins.h"
#include "frame_parser.h"
#include "m_payload.h"

// RX ring buffers must be larger than the 128-byte hardware FIFO.
// The PC buffer holds several frames: an M frame is 44 bytes, a D frame up to ~130.
#define PC_UART_RX_BUF_SIZE 1024
#define SERVOS_UART_RX_BUF_SIZE 256

// Max bytes read from UART0 per call; any size works, frame_parser_feed() handles chunking.
#define PC_RX_CHUNK_SIZE 64
#define PC_RX_TASK_STACK_SIZE 4096
#define PC_RX_TASK_PRIORITY 10

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
 * @brief frame_parser callback: decodes M and S and forwards them to the bus task.
 *
 * pc_rx only parses (DS3): the bus task owns the servo bus and arm_state, applies the link
 * watchdog with the arrival time and answers the PC. Requests are queued without waiting:
 * with the queue full the frame is dropped and logged (the PC sends again in 20 ms).
 *
 * @param cid         Command id.
 * @param payload     Payload bytes, only valid during the call.
 * @param payload_len Number of bytes in payload.
 * @param ctx         Unused.
 */
static void on_pc_frame(char cid, const uint8_t *payload, size_t payload_len, void *ctx)
{
    (void)ctx;

    arm_bus_request_t req = {.rx_us = esp_timer_get_time()};
    switch (cid) {
    case 'M':
        if (!m_payload_parse(payload, payload_len, req.m_mrad)) {
            ESP_LOGW(TAG, "M: malformed payload (%u bytes), frame ignored", (unsigned)payload_len);
            return;
        }
        req.type = ARM_BUS_REQ_M;
        break;
    case 'S':
        // Start (R03): param_init = joint limits (E10, DS10); the bus task decides (DS9).
        req.type = ARM_BUS_REQ_S;
        req.s.params = arm_limits_parse(payload, payload_len, &req.s.limits);
        if (req.s.params == ARM_LIMITS_INVALID) {
            ESP_LOGW(TAG, "S: invalid limits in param_init (%u bytes)", (unsigned)payload_len);
        }
        break;
    default:
        // D and the rest arrive with E09.
        ESP_LOGW(TAG, "Unhandled CID '%c' (%u payload bytes)", cid, (unsigned)payload_len);
        return;
    }
    arm_bus_send(&req, 0);
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
 * @brief ESP-IDF entry point: initializes the UARTs, the servo bus task and the PC receive
 *        task.
 */
void app_main(void)
{
    init_uarts();
    ESP_ERROR_CHECK(arm_bus_start(NULL, NULL));
    xTaskCreate(pc_rx_task, "pc_rx", PC_RX_TASK_STACK_SIZE, NULL, PC_RX_TASK_PRIORITY, NULL);
}
