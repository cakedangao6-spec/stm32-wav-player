/**
 * @file    ring_buffer.h
 * @brief   环形缓冲区
 */

#ifndef __RING_BUFFER_H
#define __RING_BUFFER_H

#include <stdint.h>

#define RING_BUF_SIZE   512U
#define RING_BUF_MASK   (RING_BUF_SIZE - 1)

typedef struct {
    uint8_t  buffer[RING_BUF_SIZE];     // 实际存放数据的数组
    volatile uint32_t head;             // 写指针，在中断或写入函数中更新
    volatile uint32_t tail;             // 读指针，在主循环读取数据时更新
} RingBuffer_t;

void     RingBuffer_Init(RingBuffer_t *rb);
uint8_t  RingBuffer_Put(RingBuffer_t *rb, uint8_t b);
uint8_t  RingBuffer_Get(RingBuffer_t *rb, uint8_t *pb);
uint32_t RingBuffer_Available(const RingBuffer_t *rb);
void     RingBuffer_Reset(RingBuffer_t *rb);

#endif
