#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"

#include "board_pins.h"
#include "frame_parser.h"
#include "m_payload.h"
#include "servo_bus.h"
#include "servo_units.h"

// RX ring buffers must be larger than the 128-byte hardware FIFO.
// The PC buffer holds several frames: an M frame is 44 bytes, a D frame up to ~130.
#define PC_UART_RX_BUF_SIZE 1024
#define SERVOS_UART_RX_BUF_SIZE 256

// Bytes read from UART0 per call; any size works, frame_parser_feed() handles chunking.
#define PC_RX_CHUNK_SIZE 64
// Max wait for the first byte: short, so the task stays responsive but doesn't spin.
#define PC_RX_TIMEOUT_MS 20
#define PC_RX_TASK_STACK_SIZE 4096
#define PC_RX_TASK_PRIORITY 10

static const char *TAG = "ESP32-ARM";

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
 * @brief Handles an M frame: decodes the joint angles and moves the servos with one SYNC_WRITE.
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

    // Joint j of the M payload drives servo ID j + 1 (shoulder_pan = 1 .. gripper = 6),
    // same order as the joints in robotic_arm_ros2_control.xacro.
    servo_target_t targets[M_PAYLOAD_JOINTS];
    for (size_t j = 0; j < M_PAYLOAD_JOINTS; j++) {
        targets[j] = (servo_target_t){
            .id = (uint8_t)(j + 1),
            .position = (uint16_t)mrad_to_counts(mrad[j]),  // always 0..4095
            .speed = CONFIG_ARM_SERVO_MOVE_SPEED,
            .acc = CONFIG_ARM_SERVO_MOVE_ACC,
        };
    }

    uint8_t packet[SERVO_BUS_SYNC_WRITE_POS_SIZE(M_PAYLOAD_JOINTS)];
    size_t packet_size = servo_bus_build_sync_write_pos(packet, sizeof(packet), targets,
                                                        M_PAYLOAD_JOINTS);
    uart_write_bytes(SERVOS_UART_PORT, packet, packet_size);

    // TODO(E06): read the present positions and reply with an M feedback frame.
    ESP_LOGI(TAG, "M: mrad [%ld %ld %ld %ld %ld %ld] -> counts [%u %u %u %u %u %u]",
             (long)mrad[0], (long)mrad[1], (long)mrad[2], (long)mrad[3], (long)mrad[4],
             (long)mrad[5], targets[0].position, targets[1].position, targets[2].position,
             targets[3].position, targets[4].position, targets[5].position);
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

    switch (cid) {
    case 'M':
        handle_m_frame(payload, payload_len);
        break;
    default:
        // D, S and the rest arrive with E09.
        ESP_LOGW(TAG, "Unhandled CID '%c' (%u payload bytes)", cid, (unsigned)payload_len);
        break;
    }
}

/**
 * @brief Reads bytes from the PC link (UART0) and feeds them to the frame parser.
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
        int n = uart_read_bytes(PC_UART_PORT, chunk, sizeof(chunk), pdMS_TO_TICKS(PC_RX_TIMEOUT_MS));
        if (n > 0) {
            frame_parser_feed(&parser, chunk, (size_t)n);
        }
    }
}

void app_main(void)
{
    init_uarts();
    xTaskCreate(pc_rx_task, "pc_rx", PC_RX_TASK_STACK_SIZE, NULL, PC_RX_TASK_PRIORITY, NULL);
}
