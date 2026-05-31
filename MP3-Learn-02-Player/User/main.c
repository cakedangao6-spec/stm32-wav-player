/**
 * @file    main.c
 * @brief   Learn-02: read a WAV file from W25Q64 and play it by PWM.
 *
 * Clock: HSE 8MHz -> PLL x9 -> 72MHz
 * Pins : PA4=CS, PA5=SCK, PA6=MISO, PA7=MOSI (SPI1)
 *        PA9=TX, PA10=RX (USART1 debug log)
 *        PB0=TIM3_CH3 PWM audio output
 *        PB8=SCL, PB9=SDA (OLED software I2C)
 *        PC13=LED
 */

#include "main.h"
#include "Delay.h"
#include "OLED.h"
#include "bsp_usart.h"
#include "w25q64.h"
#include "wav.h"
#include "audio_player.h"
#include <stdio.h>

#define WAV_FILE_BASE_ADDR 0UL
#define WAV_FILE_SIZE      301810UL

static void LED_Blink(uint8_t times, uint32_t delay_ms)
{
    for (uint8_t i = 0; i < times; i++) {
        LED_ON();
        Delay_ms(delay_ms);
        LED_OFF();
        Delay_ms(delay_ms);
    }
}

static void Debug_PrintWavInfo(const WavInfo_t *wav_info)
{
    char buf[64];

    USART1_SendString("WAV format: PCM\r\n");
    sprintf(buf, "Sample rate: %lu Hz\r\n", wav_info->sample_rate);
    USART1_SendString(buf);
    sprintf(buf, "Bits: %u\r\n", wav_info->bits_per_sample);
    USART1_SendString(buf);
    sprintf(buf, "Channels: %u\r\n", wav_info->channels);
    USART1_SendString(buf);
    sprintf(buf, "PCM offset: %lu\r\n", wav_info->pcm_offset);
    USART1_SendString(buf);
    sprintf(buf, "PCM size: %lu\r\n", wav_info->pcm_size);
    USART1_SendString(buf);
}

static void Debug_PrintAudioError(AudioPlayerError_t error)
{
    switch (error) {
    case AUDIO_PLAYER_ERR_BAD_FORMAT:
        USART1_SendString("Audio error: bad format\r\n");
        break;
    case AUDIO_PLAYER_ERR_READ:
        USART1_SendString("Audio error: W25Q64 read\r\n");
        break;
    case AUDIO_PLAYER_ERR_UNDERFLOW:
        USART1_SendString("Audio error: buffer underflow\r\n");
        break;
    default:
        USART1_SendString("Audio error: unknown\r\n");
        break;
    }
}

void RCC_Configuration(void)
{
    ErrorStatus HSEStartUpStatus;

    RCC_DeInit();
    RCC_HSEConfig(RCC_HSE_ON);
    HSEStartUpStatus = RCC_WaitForHSEStartUp();

    if (HSEStartUpStatus == SUCCESS) {
        FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);
        FLASH_SetLatency(FLASH_Latency_2);
        RCC_HCLKConfig(RCC_SYSCLK_Div1);
        RCC_PCLK2Config(RCC_HCLK_Div1);
        RCC_PCLK1Config(RCC_HCLK_Div2);
        RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9);
        RCC_PLLCmd(ENABLE);
        while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);
        RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
        while (RCC_GetSYSCLKSource() != 0x08);
    }
}

void GPIO_Configuration(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO |
                           RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOC, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = LED_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(LED_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin   = W25Q64_CS_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(W25Q64_CS_PORT, &GPIO_InitStructure);

    LED_OFF();
    GPIO_SetBits(W25Q64_CS_PORT, W25Q64_CS_PIN);
}

void SPI1_Configuration(void)
{
    SPI_InitTypeDef SPI_InitStructure;
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1 | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial     = 7;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_Cmd(SPI1, ENABLE);
}

void NVIC_Configuration(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
}

int main(void)
{
    uint32_t loop_tick = 0;
    uint32_t jedec_id;
    WavInfo_t wav_info;
    WavStatus_t wav_status;

    RCC_Configuration();
    NVIC_Configuration();
    USART1_Init();
    GPIO_Configuration();
    SPI1_Configuration();
    W25Q64_DriverInit();

    OLED_Init();
    OLED_Clear();

    USART1_SendString("Learn-02 Player boot\r\n");

    jedec_id = W25Q64_ReadJedecID();
    OLED_ShowString(1, 1, "W25Q64:");
    if (W25Q64_Is64MbitCompatible(jedec_id)) {
        OLED_ShowString(1, 9, "OK");
        LED_Blink(2, 60);
    } else {
        OLED_ShowString(1, 9, "FAIL");
        USART1_SendString("W25Q64 JEDEC check failed\r\n");
        LED_Blink(5, 120);
    }

    wav_status = WAV_ParseFromFlash(WAV_FILE_BASE_ADDR, WAV_FILE_SIZE, &wav_info);
    if (wav_status != WAV_STATUS_OK) {
        USART1_SendString("WAV error: ");
        USART1_SendString(WAV_StatusString(wav_status));
        USART1_SendString("\r\n");
        OLED_ShowString(2, 1, "State: WAV ERR  ");
        OLED_ShowString(3, 1, "Check file data ");
        OLED_ShowString(4, 1, "See UART log    ");
        while (1) {
            LED_TOGGLE();
            Delay_ms(120);
        }
    }

    Debug_PrintWavInfo(&wav_info);
    OLED_ShowString(2, 1, "State: PLAYING  ");
    OLED_ShowString(3, 1, "PCM ready       ");
    OLED_ShowString(4, 1, "PB0 audio out   ");

    if (AudioPlayer_Init(&wav_info) == 0U || AudioPlayer_Start() == 0U) {
        Debug_PrintAudioError(AudioPlayer_GetLastError());
        OLED_ShowString(2, 1, "State: AUDIOERR ");
        OLED_ShowString(3, 1, "Check UART log  ");
        OLED_ShowString(4, 1, "Playback stop   ");
        while (1) {
            LED_TOGGLE();
            Delay_ms(120);
        }
    }

    USART1_SendString("Playback start\r\n");

    while (1) {
        AudioPlayer_Process();
        loop_tick++;

        if (AudioPlayer_GetState() == AUDIO_PLAYER_DONE) {
            USART1_SendString("Playback end\r\n");
            OLED_ShowString(2, 1, "State: DONE     ");
            OLED_ShowString(3, 1, "Playback end    ");
            OLED_ShowString(4, 1, "                ");
            LED_OFF();
            while (1) {
            }
        }

        if (AudioPlayer_GetState() == AUDIO_PLAYER_ERROR) {
            Debug_PrintAudioError(AudioPlayer_GetLastError());
            OLED_ShowString(2, 1, "State: AUDIOERR ");
            OLED_ShowString(3, 1, "Check UART log  ");
            OLED_ShowString(4, 1, "Playback stop   ");
            while (1) {
                LED_TOGGLE();
                Delay_ms(120);
            }
        }

        if (loop_tick % 30000U == 0U) {
            LED_TOGGLE();
        }
    }
}
