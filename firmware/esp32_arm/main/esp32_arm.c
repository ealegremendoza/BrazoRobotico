#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"

#include "board_pins.h"

// RX ring buffers must be larger than the 128-byte hardware FIFO.
// The PC buffer holds several frames: an M frame is 44 bytes, a D frame up to ~130.
#define PC_UART_RX_BUF_SIZE 1024
#define SERVOS_UART_RX_BUF_SIZE 256

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

void app_main(void)
{
    init_uarts();
}
