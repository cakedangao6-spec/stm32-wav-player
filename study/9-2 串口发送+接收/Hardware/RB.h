// RB.h
#ifndef __RB_H
#define __RB_H

#include "stm32f10x.h"

#define RING_BUFFER_SIZE 256

extern volatile uint16_t rb_overflow_count;		//缓冲区满时丢弃数据的计数

uint8_t RB_Write(uint8_t data);					//写入1字节，成功返回1，满了返回0
uint8_t RB_Read(uint8_t *data);					//读出1字节，成功返回1，空了返回0
uint8_t RB_IsEmpty(void);						//判断缓冲区是否为空
uint16_t RB_GetCount(void);						//获取当前可读数据数量

#endif
