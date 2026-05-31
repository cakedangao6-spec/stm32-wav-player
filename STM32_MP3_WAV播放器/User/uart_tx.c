/**
 * @file    uart_tx.c
 * @brief   Blocking USART1 transmit helpers for binary readback.
 */

#include "uart_tx.h"
#include "stm32f10x.h"

void UART_TX_SendByte(uint8_t byte)
{
    uint32_t timeout = 100000UL;
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET && timeout > 0U) {
        timeout--;
    }
    if (timeout == 0U) {
        return;
    }
    USART_SendData(USART1, byte);
}

void UART_TX_SendBuffer(const uint8_t *data, uint32_t len)
{
    if (data == 0) {
        return;
    }

    for (uint32_t i = 0; i < len; i++) {
        UART_TX_SendByte(data[i]);
    }
    UART_TX_WaitIdle();
}

void UART_TX_SendString(const char *str)
{
    if (str == 0) {
        return;
    }

    while (*str != '\0') {
        UART_TX_SendByte((uint8_t)*str);
        str++;
    }
    UART_TX_WaitIdle();
}

void UART_TX_WaitIdle(void)
{
    uint32_t timeout = 100000UL;
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET && timeout > 0U) {
        timeout--;
    }
}
