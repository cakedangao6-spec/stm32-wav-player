#ifndef __APP_H
#define __APP_H

#include "stm32f10x.h"

typedef enum
{
	APP_STATE_IDLE = 0,	//等待命令状态
	APP_STATE_RECEIVING	//正在接收数据状态
} App_State_t;

App_State_t App_GetState(void);
uint32_t App_GetReceivedSize(void);
void App_HandleLine(char *line);
void App_HandleDataByte(uint8_t data);

#endif
