#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/**
 * @file board_pins.h
 * @brief UART ports, pins and baud rates of the board.
 *
 * Configurable values come from Kconfig (menu "Robotic Arm Configuration"); fixed
 * ones are defined here. The console (UART1, GPIO14/27) is configured in
 * sdkconfig.defaults, not here: ESP-IDF sets it up before app_main.
 */

#include "driver/uart.h"
#include "sdkconfig.h"

#define PC_UART_PORT UART_NUM_0
#define PC_UART_BAUD_RATE CONFIG_ARM_PC_UART_BAUD_RATE
// Do not modify these pins. They are wired to the USB chip on the board.
// You must reconnect them using uart_set_pin(), as the bootloader disconnects them when the console is moved.
#define PC_UART_TX_PIN 1
#define PC_UART_RX_PIN 3

#define SERVOS_UART_PORT UART_NUM_2
#define SERVOS_UART_BAUD_RATE CONFIG_ARM_SERVO_UART_BAUD_RATE
#define SERVOS_UART_TX_PIN CONFIG_ARM_SERVO_UART_TX_GPIO
#define SERVOS_UART_RX_PIN CONFIG_ARM_SERVO_UART_RX_GPIO


#endif // BOARD_PINS_H
