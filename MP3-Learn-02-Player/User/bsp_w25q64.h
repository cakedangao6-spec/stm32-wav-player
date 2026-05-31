/**
 * @file    bsp_w25q64.h
 * @brief   W25Q64 SPI Flash 驱动
 */

#ifndef __BSP_W25Q64_H
#define __BSP_W25Q64_H

#include "stm32f10x.h"

#define W25Q64_CMD_WRITE_ENABLE      0x06
#define W25Q64_CMD_WRITE_DISABLE     0x04
#define W25Q64_CMD_READ_STATUS1      0x05
#define W25Q64_CMD_READ_DATA         0x03
#define W25Q64_CMD_PAGE_PROGRAM      0x02
#define W25Q64_CMD_SECTOR_ERASE      0x20
#define W25Q64_CMD_JEDEC_ID          0x9F

#define W25Q64_PAGE_SIZE             256U
#define W25Q64_SECTOR_SIZE           4096U
#define W25Q64_TOTAL_SIZE            8388608U

#define W25Q64_SECTOR_ERASE_TIMEOUT  400U
#define W25Q64_PAGE_PROGRAM_TIMEOUT  5U

void     W25Q64_Init(void);
uint32_t W25Q64_ReadID(void);
uint8_t  W25Q64_SectorErase(uint32_t sector_addr);
uint8_t  W25Q64_PageProgram(uint32_t addr, const uint8_t *pdata, uint16_t size);
void     W25Q64_ReadData(uint32_t addr, uint8_t *pdata, uint32_t size);

#endif
