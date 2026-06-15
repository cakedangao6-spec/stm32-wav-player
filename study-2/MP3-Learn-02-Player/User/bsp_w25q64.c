/**
 * @file    bsp_w25q64.c
 * @brief   W25Q64 SPI Flash 驱动（标准库），使用 SPI1 硬件接口
 */

#include "bsp_w25q64.h"
#include "main.h"
#include "Delay.h"

#define W25Q64_CS_LOW()   GPIO_ResetBits(W25Q64_CS_PORT, W25Q64_CS_PIN)
#define W25Q64_CS_HIGH()  GPIO_SetBits(W25Q64_CS_PORT, W25Q64_CS_PIN)

/**
  * 函    数：SPI1 交换 1 个字节
  * 参    数：tx_data 要发送的字节
  * 返 回 值：同时接收到的字节
  * 说    明：SPI 是全双工通信，发送 1 字节的同时也会收到 1 字节
  */
static uint8_t W25Q64_SPI_SwapByte(uint8_t tx_data)
{
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);   // 等待发送缓冲区为空
    SPI_I2S_SendData(SPI1, tx_data);                                  // 写入要发送的数据
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);  // 等待接收缓冲区非空
    return (uint8_t)SPI_I2S_ReceiveData(SPI1);                        // 读取接收到的数据
}

/**
  * 函    数：读取 W25Q64 忙标志
  * 参    数：无
  * 返 回 值：1 表示芯片忙，0 表示芯片空闲
  */
static uint8_t W25Q64_IsBusy(void)
{
    uint8_t status;
    W25Q64_CS_LOW();                                      // 片选拉低，开始一次 SPI 访问
    W25Q64_SPI_SwapByte(W25Q64_CMD_READ_STATUS1);         // 发送读状态寄存器 1 指令
    status = W25Q64_SPI_SwapByte(0xFF);                   // 发送空字节，同时读回状态寄存器
    W25Q64_CS_HIGH();                                     // 片选拉高，结束访问
    return (status & 0x01);                               // bit0 为 BUSY 位
}

/**
  * 函    数：等待 W25Q64 空闲
  * 参    数：timeout_ms 超时时间，单位毫秒
  * 返 回 值：0 表示等待成功，1 表示超时
  */
static uint8_t W25Q64_WaitBusy(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (W25Q64_IsBusy()) {
        Delay_us(100);                                    // 每 100us 查询一次忙标志
        elapsed++;
        if (elapsed * 100 / 1000 >= timeout_ms) return 1; // 超过指定时间则认为操作失败
    }
    return 0;
}

/**
  * 函    数：W25Q64 写使能
  * 参    数：无
  * 返 回 值：无
  * 说    明：擦除和页编程前必须先发送写使能命令
  */
static void W25Q64_WriteEnable(void)
{
    W25Q64_CS_LOW();                                      // SPI 起始
    W25Q64_SPI_SwapByte(W25Q64_CMD_WRITE_ENABLE);         // 发送写使能指令 0x06
    W25Q64_CS_HIGH();                                     // SPI 终止
}

/**
  * 函    数：W25Q64 初始化
  * 参    数：无
  * 返 回 值：无
  */
void W25Q64_Init(void)
{
    W25Q64_CS_HIGH();                                     // 空闲时片选保持高电平
}

/**
  * 函    数：读取 W25Q64 JEDEC ID
  * 参    数：无
  * 返 回 值：24 位 ID，通常 W25Q64 为 0xEF4017
  */
uint32_t W25Q64_ReadID(void)
{
    uint8_t id[3] = {0};
    W25Q64_CS_LOW();                                      // SPI 起始
    W25Q64_SPI_SwapByte(W25Q64_CMD_JEDEC_ID);             // 发送读取 JEDEC ID 指令 0x9F
    id[0] = W25Q64_SPI_SwapByte(0xFF);                    // 读取厂商 ID
    id[1] = W25Q64_SPI_SwapByte(0xFF);                    // 读取存储类型
    id[2] = W25Q64_SPI_SwapByte(0xFF);                    // 读取容量编码
    W25Q64_CS_HIGH();                                     // SPI 终止
    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
}

/**
  * 函    数：W25Q64 扇区擦除
  * 参    数：sector_addr 扇区内任意地址，函数内部会对齐到 4KB 扇区起始地址
  * 返 回 值：0 表示成功，1 表示超时失败
  */
uint8_t W25Q64_SectorErase(uint32_t sector_addr)
{
    sector_addr &= ~(W25Q64_SECTOR_SIZE - 1);             // 地址向下对齐到 4KB 扇区边界
    W25Q64_WriteEnable();                                 // 擦除前先写使能
    W25Q64_CS_LOW();                                      // SPI 起始
    W25Q64_SPI_SwapByte(W25Q64_CMD_SECTOR_ERASE);         // 发送 4KB 扇区擦除指令 0x20
    W25Q64_SPI_SwapByte((uint8_t)(sector_addr >> 16));    // 发送地址 23~16 位
    W25Q64_SPI_SwapByte((uint8_t)(sector_addr >> 8));     // 发送地址 15~8 位
    W25Q64_SPI_SwapByte((uint8_t)(sector_addr));          // 发送地址 7~0 位
    W25Q64_CS_HIGH();                                     // SPI 终止，芯片开始内部擦除
    return W25Q64_WaitBusy(W25Q64_SECTOR_ERASE_TIMEOUT);  // 等待擦除完成
}

/**
  * 函    数：W25Q64 页编程
  * 参    数：addr  写入起始地址
  * 参    数：pdata 要写入的数据数组
  * 参    数：size  写入字节数，范围 1~256
  * 返 回 值：0 表示成功，1 表示参数错误或超时失败
  * 注意事项：页编程不能跨 256 字节页，本项目上层用 page_buf 保证这一点
  */
uint8_t W25Q64_PageProgram(uint32_t addr, const uint8_t *pdata, uint16_t size)
{
    if (size == 0 || size > W25Q64_PAGE_SIZE) return 1;
    W25Q64_WriteEnable();                                 // 页编程前先写使能
    W25Q64_CS_LOW();                                      // SPI 起始
    W25Q64_SPI_SwapByte(W25Q64_CMD_PAGE_PROGRAM);         // 发送页编程指令 0x02
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 16));           // 发送地址 23~16 位
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 8));            // 发送地址 15~8 位
    W25Q64_SPI_SwapByte((uint8_t)(addr));                 // 发送地址 7~0 位
    for (uint16_t i = 0; i < size; i++) {
        W25Q64_SPI_SwapByte(pdata[i]);                    // 依次写入本页数据
    }
    W25Q64_CS_HIGH();                                     // SPI 终止，芯片开始内部写入
    return W25Q64_WaitBusy(W25Q64_PAGE_PROGRAM_TIMEOUT);  // 等待页编程完成
}

/**
  * 函    数：W25Q64 连续读取数据
  * 参    数：addr  读取起始地址
  * 参    数：pdata 接收读取数据的数组
  * 参    数：size  读取字节数
  * 返 回 值：无
  */
void W25Q64_ReadData(uint32_t addr, uint8_t *pdata, uint32_t size)
{
    if (size == 0) return;
    W25Q64_CS_LOW();                                      // SPI 起始
    W25Q64_SPI_SwapByte(W25Q64_CMD_READ_DATA);            // 发送读数据指令 0x03
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 16));           // 发送地址 23~16 位
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 8));            // 发送地址 15~8 位
    W25Q64_SPI_SwapByte((uint8_t)(addr));                 // 发送地址 7~0 位
    for (uint32_t i = 0; i < size; i++) {
        pdata[i] = W25Q64_SPI_SwapByte(0xFF);             // 发送空字节，同时读回 Flash 数据
    }
    W25Q64_CS_HIGH();                                     // SPI 终止
}
