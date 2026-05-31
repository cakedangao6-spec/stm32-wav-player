/**
 * @file    bsp_w25q64.c
 * @brief   W25Q64 SPI Flash 驱动（标准库）
 */

#include "bsp_w25q64.h"
#include "main.h"
#include "Delay.h"

#define W25Q64_CS_LOW()   GPIO_ResetBits(W25Q64_CS_PORT, W25Q64_CS_PIN)
#define W25Q64_CS_HIGH()  GPIO_SetBits(W25Q64_CS_PORT, W25Q64_CS_PIN)

static uint8_t W25Q64_SPI_SwapByte(uint8_t tx_data)
{
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SPI1, tx_data);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);
    return (uint8_t)SPI_I2S_ReceiveData(SPI1);
}

static uint8_t W25Q64_IsBusy(void)
{
    uint8_t status;
    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_CMD_READ_STATUS1);
    status = W25Q64_SPI_SwapByte(0xFF);
    W25Q64_CS_HIGH();
    return (status & 0x01);
}

static uint8_t W25Q64_WaitBusy(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (W25Q64_IsBusy()) {
        Delay_us(100);
        elapsed++;
        if (elapsed * 100 / 1000 >= timeout_ms) return 1;
    }
    return 0;
}

static void W25Q64_WriteEnable(void)
{
    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_CMD_WRITE_ENABLE);
    W25Q64_CS_HIGH();
}

void W25Q64_Init(void)
{
    W25Q64_CS_HIGH();
}

uint32_t W25Q64_ReadID(void)
{
    uint8_t id[3] = {0};
    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_CMD_JEDEC_ID);
    id[0] = W25Q64_SPI_SwapByte(0xFF);
    id[1] = W25Q64_SPI_SwapByte(0xFF);
    id[2] = W25Q64_SPI_SwapByte(0xFF);
    W25Q64_CS_HIGH();
    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
}

uint8_t W25Q64_SectorErase(uint32_t sector_addr)
{
    sector_addr &= ~(W25Q64_SECTOR_SIZE - 1);
    W25Q64_WriteEnable();
    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_CMD_SECTOR_ERASE);
    W25Q64_SPI_SwapByte((uint8_t)(sector_addr >> 16));
    W25Q64_SPI_SwapByte((uint8_t)(sector_addr >> 8));
    W25Q64_SPI_SwapByte((uint8_t)(sector_addr));
    W25Q64_CS_HIGH();
    return W25Q64_WaitBusy(W25Q64_SECTOR_ERASE_TIMEOUT);
}

uint8_t W25Q64_PageProgram(uint32_t addr, const uint8_t *pdata, uint16_t size)
{
    if (size == 0 || size > W25Q64_PAGE_SIZE) return 1;
    W25Q64_WriteEnable();
    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_CMD_PAGE_PROGRAM);
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 16));
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 8));
    W25Q64_SPI_SwapByte((uint8_t)(addr));
    for (uint16_t i = 0; i < size; i++) {
        W25Q64_SPI_SwapByte(pdata[i]);
    }
    W25Q64_CS_HIGH();
    return W25Q64_WaitBusy(W25Q64_PAGE_PROGRAM_TIMEOUT);
}

void W25Q64_ReadData(uint32_t addr, uint8_t *pdata, uint32_t size)
{
    if (size == 0) return;
    W25Q64_CS_LOW();
    W25Q64_SPI_SwapByte(W25Q64_CMD_READ_DATA);
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 16));
    W25Q64_SPI_SwapByte((uint8_t)(addr >> 8));
    W25Q64_SPI_SwapByte((uint8_t)(addr));
    for (uint32_t i = 0; i < size; i++) {
        pdata[i] = W25Q64_SPI_SwapByte(0xFF);
    }
    W25Q64_CS_HIGH();
}
