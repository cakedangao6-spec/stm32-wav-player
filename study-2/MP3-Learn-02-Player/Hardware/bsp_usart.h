/**
 * @file    bsp_usart.h
 * @brief   USART1 底层驱动（标准库中断接收 + 环形缓冲）
 */

#ifndef __BSP_USART_H
#define __BSP_USART_H

#include "stm32f10x.h"
#include "ring_buffer.h"

#define USART1_DMA_BUF_SIZE     1024U
#define USART1_BAUDRATE         115200U

extern RingBuffer_t  usart1_rx_ring;

void USART1_Init(void);
void USART1_SendString(const char *str);

#endif
