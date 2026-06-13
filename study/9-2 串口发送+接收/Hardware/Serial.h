#ifndef __SERIAL_H
#define __SERIAL_H

#include <stdio.h>

void Serial_Init(void);
void Serial_SendByte(uint8_t Byte);
void Serial_SendArray(uint8_t *Array, uint16_t Length);
void Serial_SendString(char *String);
void Serial_SendNumber(uint32_t Number, uint8_t Length);
void Serial_Printf(char *format, ...);

uint8_t Serial_GetRxFlag(void);
uint8_t Serial_GetRxData(void);
uint8_t *Serial_GetDMARxBuffer(void);
uint16_t Serial_GetDMARxBufferSize(void);
uint16_t Serial_GetDMARxScanIndex(void);
void Serial_SetDMARxScanIndex(uint16_t index);
void Serial_ProcessDMARx(void);

#endif
