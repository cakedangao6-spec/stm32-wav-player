/**
 * @file    ring_buffer.c
 * @brief   环形缓冲区实现，用于 USART 中断和主循环之间传递字节
 */

#include "ring_buffer.h"

/**
  * 函    数：环形缓冲区初始化
  * 参    数：rb 要初始化的环形缓冲区指针
  * 返 回 值：无
  * 说    明：head 表示写入位置，tail 表示读取位置，二者相等表示缓冲区为空
  */
void RingBuffer_Init(RingBuffer_t *rb)
{
    rb->head = 0;       // 写指针清零
    rb->tail = 0;       // 读指针清零
}

/**
  * 函    数：向环形缓冲区写入 1 个字节
  * 参    数：rb 环形缓冲区指针
  * 参    数：b  要写入的字节
  * 返 回 值：1 表示写入成功，0 表示缓冲区已满
  */
uint8_t RingBuffer_Put(RingBuffer_t *rb, uint8_t b)
{
    uint32_t head = rb->head;                           // 保存当前写入位置
    uint32_t next_head = (head + 1) & RING_BUF_MASK;     // 计算下一个写入位置，利用掩码实现回绕
    if (next_head == rb->tail) {
        return 0;                                       // 写指针追上读指针，说明缓冲区已满
    }
    rb->buffer[head] = b;                               // 将字节放入当前写入位置
    rb->head = next_head;                               // 更新写指针
    return 1;
}

/**
  * 函    数：从环形缓冲区读取 1 个字节
  * 参    数：rb 环形缓冲区指针
  * 参    数：pb 用于接收读出字节的指针
  * 返 回 值：1 表示读取成功，0 表示缓冲区为空
  */
uint8_t RingBuffer_Get(RingBuffer_t *rb, uint8_t *pb)
{
    uint32_t tail = rb->tail;                           // 保存当前读取位置
    if (tail == rb->head) {
        return 0;                                       // 读写指针相等，说明没有数据可读
    }
    *pb = rb->buffer[tail];                             // 取出当前读取位置的数据
    rb->tail = (tail + 1) & RING_BUF_MASK;               // 更新读指针，并在末尾自动回绕
    return 1;
}

/**
  * 函    数：获取环形缓冲区中已有数据数量
  * 参    数：rb 环形缓冲区指针
  * 返 回 值：当前可读取的字节数
  */
uint32_t RingBuffer_Available(const RingBuffer_t *rb)
{
    return (rb->head - rb->tail) & RING_BUF_MASK;        // 利用无符号差值和掩码处理回绕情况
}

/**
  * 函    数：清空环形缓冲区
  * 参    数：rb 环形缓冲区指针
  * 返 回 值：无
  */
void RingBuffer_Reset(RingBuffer_t *rb)
{
    rb->head = 0;       // 写指针回到起始位置
    rb->tail = 0;       // 读指针回到起始位置
}
