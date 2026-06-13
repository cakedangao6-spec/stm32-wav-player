#ifndef __APP_H
#define __APP_H

#include "stm32f10x.h"

typedef enum
{
	APP_STATE_IDLE = 0,
	APP_STATE_RECEIVING
} App_State_t;

App_State_t App_GetState(void);
uint32_t App_GetReceivedSize(void);
void App_HandleLine(char *line);
void App_HandleDataByte(uint8_t data);

#endif
