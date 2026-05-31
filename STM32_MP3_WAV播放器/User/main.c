/**
 * @file    main.c
 * @brief   主程序（标准库版本）
 *
 * 时钟: HSE 8MHz -> PLL x9 -> 72MHz
 * 引脚: PA4=CS, PA5=SCK, PA6=MISO, PA7=MOSI (SPI1)
 *       PA9=TX, PA10=RX (USART1)
 *       PB8=SCL, PB9=SDA (OLED I2C)
 *       PC13=LED
 */

#include "main.h"
#include "Delay.h"
#include "OLED.h"
#include "bsp_usart.h"
#include "w25q64.h"
#include "verify.h"
#include "uart_tx.h"
#include "app_file_transfer.h"
#include "wav.h"
#include "audio_player.h"
#include <stdio.h>

#define APP_MODE_TRANSFER 0U
#define APP_MODE_VERIFY   1U
#define APP_MODE_PLAYBACK 2U
#define APP_MODE          APP_MODE_PLAYBACK
#define WAV_FILE_BASE_ADDR 0UL
#define WAV_FILE_SIZE      301810UL

static void LED_Blink(uint8_t times, uint32_t delay_ms)
{
    for (uint8_t i = 0; i < times; i++) {
        LED_ON();  Delay_ms(delay_ms);
        LED_OFF(); Delay_ms(delay_ms);
    }
}

static void Debug_PrintWavInfo(const WavInfo_t *wav_info)
{
    char buf[64];

    UART_TX_SendString("WAV format: PCM\r\n");
    sprintf(buf, "Sample rate: %lu Hz\r\n", wav_info->sample_rate);
    UART_TX_SendString(buf);
    sprintf(buf, "Bits: %u\r\n", wav_info->bits_per_sample);
    UART_TX_SendString(buf);
    sprintf(buf, "Channels: %u\r\n", wav_info->channels);
    UART_TX_SendString(buf);
    sprintf(buf, "PCM offset: %lu\r\n", wav_info->pcm_offset);
    UART_TX_SendString(buf);
    sprintf(buf, "PCM size: %lu\r\n", wav_info->pcm_size);
    UART_TX_SendString(buf);
}

static void Debug_PrintAudioError(AudioPlayerError_t error)
{
    switch (error) {
    case AUDIO_PLAYER_ERR_BAD_FORMAT:
        UART_TX_SendString("Audio error: bad format\r\n");
        break;
    case AUDIO_PLAYER_ERR_READ:
        UART_TX_SendString("Audio error: W25Q64 read\r\n");
        break;
    case AUDIO_PLAYER_ERR_UNDERFLOW:
        UART_TX_SendString("Audio error: buffer underflow\r\n");
        break;
    default:
        UART_TX_SendString("Audio error: unknown\r\n");
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
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = LED_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin   = W25Q64_CS_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

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
    RCC_Configuration();
    USART1_Init();

    for (uint8_t i = 0; i < 5; i++) {
        UART_TX_SendString("BOOT_UART\r\n");
        Delay_ms(50);
    }

    GPIO_Configuration();
    SPI1_Configuration();
    W25Q64_DriverInit();

    OLED_Init();
    OLED_Clear();

    uint32_t jedec_id = W25Q64_ReadJedecID();

    OLED_ShowString(1, 1, "W25Q64:");
    if (W25Q64_Is64MbitCompatible(jedec_id)) {
        OLED_ShowString(1, 9, "OK");
        LED_Blink(2, 60);
    } else {
        OLED_ShowString(1, 9, "FAIL");
        LED_Blink(2, 120);
    }

#if APP_MODE == APP_MODE_VERIFY
    Verify_Init();
    OLED_ShowString(2, 1, "State: IDLE");
    OLED_ShowString(3, 1, "Size: ----");
    OLED_ShowString(4, 1, "Ready...");
#elif APP_MODE == APP_MODE_TRANSFER
    App_FileTransfer_Init();
    OLED_ShowString(2, 1, "State: IDLE");
    OLED_ShowString(3, 1, "Size: ----");
    OLED_ShowString(4, 1, "WRITE wait...");
#else
    WavInfo_t wav_info;
    WavStatus_t wav_status;

    UART_TX_SendString("Playback boot\r\n");
    Delay_ms(5000);
    wav_status = WAV_ParseFromFlash(WAV_FILE_BASE_ADDR, WAV_FILE_SIZE, &wav_info);
    if (wav_status != WAV_STATUS_OK) {
        UART_TX_SendString("WAV error: ");
        UART_TX_SendString(WAV_StatusString(wav_status));
        UART_TX_SendString("\r\n");
        OLED_ShowString(2, 1, "State: WAV ERR  ");
        OLED_ShowString(3, 1, "Check file data ");
        OLED_ShowString(4, 1, "See UART log    ");
    } else {
        Debug_PrintWavInfo(&wav_info);
        OLED_ShowString(2, 1, "State: PLAYING  ");
        OLED_ShowString(3, 1, "PCM ready       ");
        OLED_ShowString(4, 1, "PB0 audio out   ");

        if (AudioPlayer_Init(&wav_info) == 0U || AudioPlayer_Start() == 0U) {
            Debug_PrintAudioError(AudioPlayer_GetLastError());
            OLED_ShowString(2, 1, "State: AUDIOERR ");
            OLED_ShowString(3, 1, "Check UART log  ");
            OLED_ShowString(4, 1, "Playback stop   ");
        } else {
            UART_TX_SendString("Playback start\r\n");
        }
    }
#endif

    uint32_t loop_tick = 0;
    while (1) {
#if APP_MODE == APP_MODE_VERIFY
        Verify_Process();
        loop_tick++;
        VerifyState_t state = Verify_GetState();

        if (loop_tick % 500 == 0) {
            Verify_OLED_Refresh();
        }

        switch (state) {
        case VERIFY_STATE_IDLE:
        case VERIFY_STATE_DONE:
            LED_OFF();
            break;
        case VERIFY_STATE_DUMPING:
            if (loop_tick % 30 == 0) { LED_TOGGLE(); }
            break;
        case VERIFY_STATE_ERROR:
            if (loop_tick % 120 == 0) { LED_TOGGLE(); }
            break;
        default: break;
        }
#elif APP_MODE == APP_MODE_TRANSFER
        App_FileTransfer_Process();
        loop_tick++;
        AppState_t state = App_GetState();

        if (loop_tick % 500 == 0) {
            App_OLED_Refresh();
        }

        switch (state) {
        case APP_STATE_IDLE:
            LED_OFF();
            break;
        case APP_STATE_ERASING:
        case APP_STATE_RECEIVING:
        case APP_STATE_VERIFYING:
            if (loop_tick % 30 == 0) { LED_TOGGLE(); }
            break;
        case APP_STATE_READY:
            LED_ON();
            break;
        default:
            break;
        }
#else
        AudioPlayer_Process();
        loop_tick++;

        if (AudioPlayer_GetState() == AUDIO_PLAYER_DONE) {
            UART_TX_SendString("Playback end\r\n");
            OLED_ShowString(2, 1, "State: DONE     ");
            OLED_ShowString(3, 1, "Playback end    ");
            OLED_ShowString(4, 1, "                ");
            LED_OFF();
            while (1) {
            }
        } else if (AudioPlayer_GetState() == AUDIO_PLAYER_ERROR) {
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
#endif
    }
}
