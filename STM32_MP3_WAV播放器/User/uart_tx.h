/**
 * @file    uart_tx.h
 * @brief   Blocking USART1 transmit helpers for binary readback.
 */

#ifndef __UART_TX_H
#define __UART_TX_H

#include <stdint.h>

void UART_TX_SendByte(uint8_t byte);
void UART_TX_SendBuffer(const uint8_t *data, uint32_t len);
void UART_TX_SendString(const char *str);
void UART_TX_WaitIdle(void);

#endif
