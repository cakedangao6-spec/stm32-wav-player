/**
 * @file    ring_buffer.c
 * @brief   环形缓冲区实现
 */

#include "ring_buffer.h"

void RingBuffer_Init(RingBuffer_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
}

uint8_t RingBuffer_Put(RingBuffer_t *rb, uint8_t b)
{
    uint32_t head = rb->head;
    uint32_t next_head = (head + 1) & RING_BUF_MASK;
    if (next_head == rb->tail) {
        return 0;
    }
    rb->buffer[head] = b;
    rb->head = next_head;
    return 1;
}

uint8_t RingBuffer_Get(RingBuffer_t *rb, uint8_t *pb)
{
    uint32_t tail = rb->tail;
    if (tail == rb->head) {
        return 0;
    }
    *pb = rb->buffer[tail];
    rb->tail = (tail + 1) & RING_BUF_MASK;
    return 1;
}

uint32_t RingBuffer_Available(const RingBuffer_t *rb)
{
    return (rb->head - rb->tail) & RING_BUF_MASK;
}

void RingBuffer_Reset(RingBuffer_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
}
