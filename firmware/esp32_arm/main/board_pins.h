#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "driver/uart.h"

#define PC_UART_PORT UART_NUM_0
#define PC_UART_BAUD_RATE 115200
// Do not modify these pins. They are wired to the USB chip on the board.
// You must reconnect them using uart_set_pin(), as the bootloader disconnects them when the console is moved.
#define PC_UART_TX_PIN 1
#define PC_UART_RX_PIN 3

#define SERVOS_UART_PORT UART_NUM_2
#define SERVOS_UART_BAUD_RATE 1000000
#define SERVOS_UART_TX_PIN 17
#define SERVOS_UART_RX_PIN 16


#endif // BOARD_PINS_H
