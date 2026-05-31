/**
 * @file    verify.h
 * @brief   W25Q64 readback verification application.
 */

#ifndef __VERIFY_H
#define __VERIFY_H

#include <stdint.h>

#define VERIFY_FLASH_BASE_ADDR       0UL
#define VERIFY_WAV_SIZE              301810UL
#define VERIFY_READ_CHUNK_SIZE       256U

typedef enum {
    VERIFY_STATE_IDLE = 0,
    VERIFY_STATE_DUMPING,
    VERIFY_STATE_DONE,
    VERIFY_STATE_ERROR
} VerifyState_t;

void          Verify_Init(void);
void          Verify_Process(void);
void          Verify_OLED_Refresh(void);
VerifyState_t Verify_GetState(void);
uint32_t      Verify_GetBytesSent(void);
uint32_t      Verify_GetLastCRC32(void);

#endif
