/**
 * @file    bsp_usart.c
 * @brief   USART1 中断收发驱动（标准库），用于接收电脑发送的命令和 WAV 数据
 */

#include "bsp_usart.h"
#include "main.h"
#include <string.h>

RingBuffer_t     usart1_rx_ring;

/**
  * 函    数：USART1 发送 1 个字节
  * 参    数：byte 要发送的字节
  * 返 回 值：无
  * 说    明：阻塞等待发送完成，适合发送调试信息和协议应答
  */
static void USART1_SendByte(uint8_t byte)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);      // 等待发送数据寄存器为空
    USART_SendData(USART1, byte);                                      // 写入数据寄存器，硬件自动发送
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);       // 等待整个字节发送完成
}

/**
  * 函    数：USART1 初始化
  * 参    数：无
  * 返 回 值：无
  * 说    明：PA9 为 TX，PA10 为 RX，接收采用 RXNE 中断方式
  */
void USART1_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);  // 开启 GPIOA 和 USART1 时钟

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);                         // PA9 配置为复用推挽输出，用作 USART1_TX

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);                         // PA10 配置为上拉输入，用作 USART1_RX

    USART_InitStructure.USART_BaudRate            = USART1_BAUDRATE;                // 设置波特率
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;            // 8 位数据位
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;               // 1 位停止位
    USART_InitStructure.USART_Parity              = USART_Parity_No;                // 无校验
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;  // 同时开启接收和发送
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; // 不使用硬件流控
    USART_Init(USART1, &USART_InitStructure);                                      // 将配置写入 USART1

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);                 // 开启接收非空中断，每收到 1 字节进入中断

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);                 // 设置 NVIC 中断优先级分组
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;               // 选择 USART1 中断通道
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);                                 // 使能 USART1 中断

    USART_Cmd(USART1, ENABLE);                                      // 使能 USART1 外设

    RingBuffer_Init(&usart1_rx_ring);                               // 初始化串口接收环形缓冲区
}

/**
  * 函    数：USART1 发送字符串
  * 参    数：str 要发送的字符串，以 '\0' 结尾
  * 返 回 值：无
  */
void USART1_SendString(const char *str)
{
    while (*str) {
        USART1_SendByte((uint8_t)*str++);                           // 逐字节发送字符串
    }
}

/**
  * 函    数：USART1 中断服务函数
  * 参    数：无
  * 返 回 值：无
  * 注意事项：函数名由启动文件规定，不能随意修改
  */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        RingBuffer_Put(&usart1_rx_ring, (uint8_t)USART_ReceiveData(USART1)); // 读取数据寄存器，并放入环形缓冲区
    }
}
