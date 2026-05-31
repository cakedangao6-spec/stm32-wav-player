/**
 * @file    w25q64.c
 * @brief   Verification-facing W25Q64 wrapper.
 */

#include "w25q64.h"
#include "bsp_w25q64.h"

void W25Q64_DriverInit(void)
{
    W25Q64_Init();
}

uint32_t W25Q64_ReadJedecID(void)
{
    return W25Q64_ReadID();
}

uint8_t W25Q64_Is64MbitCompatible(uint32_t jedec_id)
{
    return ((uint8_t)jedec_id == W25Q64_64MBIT_CAPACITY_CODE) ? 1U : 0U;
}

uint8_t W25Q64_Read(uint32_t addr, uint8_t *data, uint32_t len)
{
    if (data == 0) {
        return 1;
    }

    if (len == 0) {
        return 0;
    }

    if (addr > W25Q64_CAPACITY_BYTES || len > (W25Q64_CAPACITY_BYTES - addr)) {
        return 1;
    }

    W25Q64_ReadData(addr, data, len);
    return 0;
}
