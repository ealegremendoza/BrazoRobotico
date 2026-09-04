#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"

#define BRIDGE_TXD CONFIG_EXAMPLE_UART_TXD // G26
#define BRIDGE_RXD CONFIG_EXAMPLE_UART_RXD // G25
#define BRIDGE_RTS (UART_PIN_NO_CHANGE)
#define BRIDGE_CTS (UART_PIN_NO_CHANGE)

#define BRIDGE_UART_PORT CONFIG_EXAMPLE_UART_PORT_NUM
#define BRIDGE_BAUD_RATE CONFIG_EXAMPLE_UART_BAUD_RATE // set to 9600 via menuconfig to match the Nano's SoftwareSerial
#define BRIDGE_TASK_STACK_SIZE CONFIG_EXAMPLE_TASK_STACK_SIZE

#define STX 0x02
#define ETX 0x03

// Mirrors the Nano's rcvBuffer capacity (arduino-uart-test.ino BUFFER_LEN),
// which holds STX + data + ETX (the LRC is read separately, not buffered).
#define NANO_BUFFER_LEN 32
#define MAX_DATA_LEN (NANO_BUFFER_LEN - 2)
#define LINE_BUF_LEN (MAX_DATA_LEN + 1) // +1 for fgets' terminating '\0'
#define FRAME_BUF_LEN (MAX_DATA_LEN + 3) // STX + data + ETX + LRC

static const char *TAG = "ESP32-BRIDGE";

// Reads a line typed on the USB monitor (stdin), frames it as
// STX + data + ETX + LRC (same protocol the Nano parses), and forwards it
// over UART2. Doesn't interpret the text at all -- that's the Nano's job.
static void stdin_to_nano_task(void *arg)
{
    char line[LINE_BUF_LEN];
    uint8_t frame[FRAME_BUF_LEN];

    while (1) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) {
            continue;
        }
        if (len > MAX_DATA_LEN) {
            len = MAX_DATA_LEN; // truncate to fit the Nano's rcvBuffer
        }

        ESP_LOGI(TAG, "Echo: \"%.*s\"", (int)len, line);

        uint8_t lrc = 0;
        size_t frame_len = 0;

        frame[frame_len++] = STX; // STX itself is not part of the LRC

        for (size_t i = 0; i < len; i++) {
            uint8_t b = (uint8_t)line[i];
            frame[frame_len++] = b;
            lrc ^= b;
        }

        frame[frame_len++] = ETX;
        lrc ^= ETX;

        frame[frame_len++] = lrc;

        uart_write_bytes(BRIDGE_UART_PORT, (const char *)frame, frame_len);
        ESP_LOGI(TAG, "Sent %d bytes to Nano: \"%.*s\"", (int)frame_len, (int)len, line);
    }
}

// Reads whatever the Nano sends back over UART2 (e.g. an ACK) and logs it.
// Accumulates bytes until '\n' instead of logging whatever happens to be in
// the ring buffer at read time -- uart_read_bytes returns as soon as
// anything is available, so a slow SoftwareSerial transmission on the
// Nano's side could otherwise get split across reads and logged as garbage.
static void nano_to_stdout_task(void *arg)
{
    char line[64];
    size_t line_len = 0;
    uint8_t byte;

    while (1) {
        int got = uart_read_bytes(BRIDGE_UART_PORT, &byte, 1, 200 / portTICK_PERIOD_MS);
        if (got <= 0) {
            if (line_len > 0) {
                line[line_len] = '\0';
                ESP_LOGW(TAG, "Nano (no newline within 200ms, showing what arrived): \"%s\"", line);
                line_len = 0;
            }
            continue;
        }

        if (byte == '\n') {
            if (line_len > 0 && line[line_len - 1] == '\r') {
                line_len--;
            }
            line[line_len] = '\0';
            ESP_LOGI(TAG, "Nano: %s", line);
            line_len = 0;
            continue;
        }

        if (line_len < sizeof(line) - 1) {
            line[line_len++] = (char)byte;
        }
    }
}

// Console stdin defaults to non-blocking, per-character reads, which makes
// fgets() return as soon as the RX FIFO momentarily empties instead of
// waiting for '\n'. Switching stdin to the interrupt-driven UART driver
// makes reads properly blocking, so a whole typed line arrives together.
static void configure_stdin_blocking(void)
{
    if (uart_is_driver_installed((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM)) {
        return;
    }
    setvbuf(stdin, NULL, _IONBF, 0);
    ESP_ERROR_CHECK(uart_driver_install((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);
}

void app_main(void)
{
    configure_stdin_blocking();

    uart_config_t uart_config = {
        .baud_rate = BRIDGE_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(BRIDGE_UART_PORT, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(BRIDGE_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(BRIDGE_UART_PORT, BRIDGE_TXD, BRIDGE_RXD, BRIDGE_RTS, BRIDGE_CTS));

    xTaskCreate(stdin_to_nano_task, "stdin_to_nano", BRIDGE_TASK_STACK_SIZE, NULL, 10, NULL);
    xTaskCreate(nano_to_stdout_task, "nano_to_stdout", BRIDGE_TASK_STACK_SIZE, NULL, 10, NULL);
}
