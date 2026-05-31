/**
 * @file    main.h
 * @brief   主程序头文件 —— 引脚定义（标准库版本）
 */

#ifndef __MAIN_H
#define __MAIN_H

#include "stm32f10x.h"

/* LED - PC13 */
#define LED_PORT            GPIOC
#define LED_PIN             GPIO_Pin_13

/* W25Q64 CS - PA4 */
#define W25Q64_CS_PORT      GPIOA
#define W25Q64_CS_PIN       GPIO_Pin_4

/* PWM audio output - PB0 / TIM3_CH3 */
#define AUDIO_PWM_PORT      GPIOB
#define AUDIO_PWM_PIN       GPIO_Pin_0

void RCC_Configuration(void);
void GPIO_Configuration(void);
void SPI1_Configuration(void);
void NVIC_Configuration(void);

#define LED_ON()    GPIO_ResetBits(LED_PORT, LED_PIN)
#define LED_OFF()   GPIO_SetBits(LED_PORT, LED_PIN)
#define LED_TOGGLE() do { \
    if (GPIO_ReadOutputDataBit(LED_PORT, LED_PIN)) \
        GPIO_ResetBits(LED_PORT, LED_PIN); \
    else \
        GPIO_SetBits(LED_PORT, LED_PIN); \
} while(0)

#endif
