/**
 * @file    app_file_transfer.h
 * @brief   文件传输状态机
 */

#ifndef __APP_FILE_TRANSFER_H
#define __APP_FILE_TRANSFER_H

#include <stdint.h>

typedef enum {
    APP_STATE_IDLE       = 0,
    APP_STATE_ERASING    = 1,
    APP_STATE_READY      = 2,
    APP_STATE_RECEIVING  = 3,
    APP_STATE_VERIFYING  = 4,
} AppState_t;

void       App_FileTransfer_Init(void);
void       App_FileTransfer_Process(void);
AppState_t App_GetState(void);
void       App_OLED_Refresh(void);

#endif
