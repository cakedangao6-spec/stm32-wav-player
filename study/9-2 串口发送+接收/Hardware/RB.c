#include "stm32f10x.h"                  // Device header
#include "RB.h"

static uint8_t rx_buffer[RING_BUFFER_SIZE];
static volatile uint16_t rx_write_index = 0;
static volatile uint16_t rx_read_index = 0;
volatile uint16_t rb_overflow_count = 0;			//缓冲区满时丢弃数据的计数

/**
  * @brief  环形缓冲区写入一个字节
  * @param  data 要写入的数据
  * @retval 写入成功返回1，缓冲区满返回0
  */
uint8_t RB_Write(uint8_t data)
{
		uint16_t next_rx_write = (rx_write_index + 1) % RING_BUFFER_SIZE;	//下一个写入位置
		if (next_rx_write != rx_read_index)						//让写指针追不上读指针
		{
			rx_buffer[rx_write_index] = data;					//写入数据
			rx_write_index = next_rx_write;						//更新写入位置
			return 1;
		}
		rb_overflow_count++;						//缓冲区满，记录一次丢弃
		return 0;
}

/**
  * @brief  判断环形缓冲区是否为空
  * @param  无
  * @retval 为空返回1，非空返回0
  */
uint8_t RB_IsEmpty(void)
{
	return rx_write_index == rx_read_index;					
}

/**
  * @brief  获取环形缓冲区当前已有数据数量
  * @param  无
  * @retval 当前可读数据数量
  */
uint16_t RB_GetCount(void)
{
	if (rx_write_index >= rx_read_index)
	{
		return rx_write_index - rx_read_index;
	}
	else
	{
		return RING_BUFFER_SIZE - rx_read_index + rx_write_index;
	}
}

/**
  * @brief  从环形缓冲区读出一个字节
  * @param  data 读出数据的存放地址
  * @retval 读出成功返回1，缓冲区空返回0
  */
uint8_t RB_Read(uint8_t *data)
{
	if (RB_IsEmpty())											//读指针追上写指针，不读
	{
		return 0;
	}
																//没追上，可读
	*data = rx_buffer[rx_read_index];							//读数据
	rx_read_index = (rx_read_index + 1) % RING_BUFFER_SIZE;		//更新读出地址
	return 1;
}
