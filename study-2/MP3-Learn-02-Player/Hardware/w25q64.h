/**
 * @file    w25q64.h
 * @brief   Verification-facing W25Q64 wrapper.
 */

#ifndef __W25Q64_VERIFY_WRAPPER_H
#define __W25Q64_VERIFY_WRAPPER_H

#include <stdint.h>

#define W25Q64_EXPECTED_JEDEC_ID    0x00EF4017UL
#define W25Q64_64MBIT_CAPACITY_CODE 0x17U
#define W25Q64_CAPACITY_BYTES       8388608UL

void     W25Q64_DriverInit(void);
uint32_t W25Q64_ReadJedecID(void);
uint8_t  W25Q64_Is64MbitCompatible(uint32_t jedec_id);
uint8_t  W25Q64_Read(uint32_t addr, uint8_t *data, uint32_t len);

#endif
