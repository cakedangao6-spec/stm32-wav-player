#include "stm32f10x.h"                  // Device header
#include <stdio.h>
#include <stdarg.h>
#include "OLED.h"
#include "RB.h"

#define SERIAL_DMA_RX_BUFFER_SIZE 64

uint8_t Serial_RxData;		//定义串口接收的数据变量
uint8_t Serial_RxFlag;		//定义串口接收的标志位变量
static uint8_t serial_dma_rx_buffer[SERIAL_DMA_RX_BUFFER_SIZE];		//DMA循环接收缓冲区
static uint16_t serial_dma_rx_scan_index = 0;						//软件已经扫描到的位置

/**
  * @brief  配置USART1 RX对应的DMA循环接收通道
  * @param  无
  * @retval 无
  * @note   USART1_RX对应DMA1_Channel5，本函数只配置DMA搬运规则，USART的DMA接收请求在后续小步开启。
  */
static void Serial_DMARxConfig(void)
{
	DMA_InitTypeDef DMA_InitStructure;
	
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);		//DMA1挂在AHB总线上，需要先开启时钟
	
	DMA_DeInit(DMA1_Channel5);									//先把通道5恢复到默认状态，避免残留配置影响本次初始化
	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;	//外设地址固定为USART1数据寄存器，DMA从这里读取收到的字节
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)serial_dma_rx_buffer;	//内存地址指向DMA接收缓冲区，DMA把字节写到这里
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;			//传输方向为外设到内存，外设是数据源
	DMA_InitStructure.DMA_BufferSize = SERIAL_DMA_RX_BUFFER_SIZE;	//循环缓冲区大小，DMA写满后从头继续写
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;	//USART1->DR只有一个地址，外设地址不能递增
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;		//内存地址需要递增，让连续字节依次放入数组
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;	//串口一次收到1字节，外设数据宽度选字节
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;	//接收缓冲区是uint8_t数组，内存数据宽度也选字节
	DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;				//循环模式，写到数组末尾后自动回到开头继续写
	DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;		//设置中等优先级，当前工程里只有这个DMA任务
	DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;					//不是内存到内存搬运，而是USART外设到内存
	DMA_Init(DMA1_Channel5, &DMA_InitStructure);					//把上面的配置写入DMA1通道5寄存器
	
	serial_dma_rx_scan_index = 0;
	DMA_Cmd(DMA1_Channel5, ENABLE);								//先打开DMA通道，后续再让USART1发出DMA接收请求
}

/**
  * @brief  获取USART DMA循环接收缓冲区首地址
  * @param  无
  * @retval DMA接收缓冲区首地址
  */
uint8_t *Serial_GetDMARxBuffer(void)
{
	return serial_dma_rx_buffer;
}

/**
  * @brief  获取USART DMA循环接收缓冲区大小
  * @param  无
  * @retval DMA接收缓冲区大小
  */
uint16_t Serial_GetDMARxBufferSize(void)
{
	return SERIAL_DMA_RX_BUFFER_SIZE;
}

/**
  * @brief  获取当前软件扫描位置
  * @param  无
  * @retval 当前软件扫描下标
  */
uint16_t Serial_GetDMARxScanIndex(void)
{
	return serial_dma_rx_scan_index;
}

/**
  * @brief  设置当前软件扫描位置
  * @param  index 新的软件扫描下标
  * @retval 无
  */
void Serial_SetDMARxScanIndex(uint16_t index)
{
	serial_dma_rx_scan_index = index;
}

/**
  * @brief  扫描DMA循环接收缓冲区中的新字节
  * @param  无
  * @retval 无
  * @note   DMA只负责把USART1->DR搬到数组，本函数负责把数组里的新字节转存到RB环形缓冲区。
  */
void Serial_ProcessDMARx(void)
{
	uint16_t dma_write_index;
	
	dma_write_index = SERIAL_DMA_RX_BUFFER_SIZE - DMA_GetCurrDataCounter(DMA1_Channel5);	//根据DMA剩余计数换算当前写入位置
	if (dma_write_index >= SERIAL_DMA_RX_BUFFER_SIZE)
	{
		dma_write_index = 0;								//DMA刚好写到末尾时，软件按回到数组开头处理
	}
	
	while (serial_dma_rx_scan_index != dma_write_index)
	{
		RB_Write(serial_dma_rx_buffer[serial_dma_rx_scan_index]);	//把DMA收到的新字节交给原来的软件环形缓冲区
		serial_dma_rx_scan_index ++;
		if (serial_dma_rx_scan_index >= SERIAL_DMA_RX_BUFFER_SIZE)
		{
			serial_dma_rx_scan_index = 0;					//扫描位置到末尾后也回到数组开头
		}
	}
}

/**
  * 函    数：串口初始化
  * 参    数：无
  * 返 回 值：无
  */
void Serial_Init(void)
{
	/*开启时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);	//开启USART1的时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);	//开启GPIOA的时钟
	
	/*GPIO初始化*/
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);					//将PA9引脚初始化为复用推挽输出
	
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);					//将PA10引脚初始化为上拉输入
	
	/*USART初始化*/
	USART_InitTypeDef USART_InitStructure;					//定义结构体变量
	USART_InitStructure.USART_BaudRate = 9600;				//波特率
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;	//硬件流控制，不需要
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;	//模式，发送模式和接收模式均选择
	USART_InitStructure.USART_Parity = USART_Parity_No;		//奇偶校验，不需要
	USART_InitStructure.USART_StopBits = USART_StopBits_1;	//停止位，选择1位
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;		//字长，选择8位
	USART_Init(USART1, &USART_InitStructure);				//将结构体变量交给USART_Init，配置USART1
	Serial_DMARxConfig();									//配置USART1_RX对应的DMA循环接收通道
	USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);			//允许USART1收到数据时向DMA发出接收搬运请求
	
	/*中断输出配置*/
	USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);			//关闭RXNE接收中断，接收搬运改由DMA负责
	
	/*NVIC中断分组*/
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);			//配置NVIC为分组2
	
	/*NVIC配置*/
	NVIC_InitTypeDef NVIC_InitStructure;					//定义结构体变量
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;		//选择配置NVIC的USART1线
	NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;		//USART1接收不再进入中断，保留配置位置便于对照学习
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;		//指定NVIC线路的抢占优先级为1
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;		//指定NVIC线路的响应优先级为1
	NVIC_Init(&NVIC_InitStructure);							//将结构体变量交给NVIC_Init，配置NVIC外设
	
	/*USART使能*/
	USART_Cmd(USART1, ENABLE);								//使能USART1，串口开始运行
}

/**
  * 函    数：串口发送一个字节
  * 参    数：Byte 要发送的一个字节
  * 返 回 值：无
  */
void Serial_SendByte(uint8_t Byte)
{
	USART_SendData(USART1, Byte);		//将字节数据写入数据寄存器，写入后USART自动生成时序波形
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);	//等待发送完成
	/*下次写入数据寄存器会自动清除发送完成标志位，故此循环后，无需清除标志位*/
}

/**
  * 函    数：串口发送一个数组
  * 参    数：Array 要发送数组的首地址
  * 参    数：Length 要发送数组的长度
  * 返 回 值：无
  */
void Serial_SendArray(uint8_t *Array, uint16_t Length)
{
	uint16_t i;
	for (i = 0; i < Length; i ++)		//遍历数组
	{
		Serial_SendByte(Array[i]);		//依次调用Serial_SendByte发送每个字节数据
	}
}

/**
  * 函    数：串口发送一个字符串
  * 参    数：String 要发送字符串的首地址
  * 返 回 值：无
  */
void Serial_SendString(char *String)
{
	uint8_t i;
	for (i = 0; String[i] != '\0'; i ++)//遍历字符数组（字符串），遇到字符串结束标志位后停止
	{
		Serial_SendByte(String[i]);		//依次调用Serial_SendByte发送每个字节数据
	}
}

/**
  * 函    数：次方函数（内部使用）
  * 返 回 值：返回值等于X的Y次方
  */
uint32_t Serial_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;	//设置结果初值为1
	while (Y --)			//执行Y次
	{
		Result *= X;		//将X累乘到结果
	}
	return Result;
}

/**
  * 函    数：串口发送数字
  * 参    数：Number 要发送的数字，范围：0~4294967295
  * 参    数：Length 要发送数字的长度，范围：0~10
  * 返 回 值：无
  */
void Serial_SendNumber(uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i ++)		//根据数字长度遍历数字的每一位
	{
		Serial_SendByte(Number / Serial_Pow(10, Length - i - 1) % 10 + '0');	//依次调用Serial_SendByte发送每位数字
	}
}

/**
  * 函    数：使用printf需要重定向的底层函数
  * 参    数：保持原始格式即可，无需变动
  * 返 回 值：保持原始格式即可，无需变动
  */
int fputc(int ch, FILE *f)
{
	Serial_SendByte(ch);			//将printf的底层重定向到自己的发送字节函数
	return ch;
}

/**
  * 函    数：自己封装的prinf函数
  * 参    数：format 格式化字符串
  * 参    数：... 可变的参数列表
  * 返 回 值：无
  */
void Serial_Printf(char *format, ...)
{
	char String[100];				//定义字符数组
	va_list arg;					//定义可变参数列表数据类型的变量arg
	va_start(arg, format);			//从format开始，接收参数列表到arg变量
	vsprintf(String, format, arg);	//使用vsprintf打印格式化字符串和参数列表到字符数组中
	va_end(arg);					//结束变量arg
	Serial_SendString(String);		//串口发送字符数组（字符串）
}

/**
  * 函    数：获取串口接收标志位
  * 参    数：无
  * 返 回 值：串口接收标志位，范围：0~1，接收到数据后，标志位置1，读取后标志位自动清零
  */
uint8_t Serial_GetRxFlag(void)
{
	if (Serial_RxFlag == 1)			//如果标志位为1
	{
		Serial_RxFlag = 0;
		return 1;					//则返回1，并自动清零标志位
	}
	return 0;						//如果标志位为0，则返回0
}

/**
  * 函    数：获取串口接收的数据
  * 参    数：无
  * 返 回 值：接收的数据，范围：0~255
  */
uint8_t Serial_GetRxData(void)
{
	return Serial_RxData;			//返回接收的数据变量
}

/**
  * 函    数：USART1中断函数
  * 参    数：无
  * 返 回 值：无
  * 注意事项：此函数为中断函数，无需调用，中断触发后自动执行
  *           函数名为预留的指定名称，可以从启动文件复制
  *           请确保函数名正确，不能有任何差异，否则中断函数将不能进入
  */
void USART1_IRQHandler(void)
{
	if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)		//判断是否是USART1的接收事件触发的中断
	{
		uint8_t data = USART_ReceiveData(USART1);				//读取串口数据
		RB_Write(data);
		
		USART_ClearITPendingBit(USART1, USART_IT_RXNE);			//清除USART1的RXNE标志位
																//读取数据寄存器会自动清除此标志位
																//如果已经读取了数据寄存器，也可以不执行此代码
	}
}
