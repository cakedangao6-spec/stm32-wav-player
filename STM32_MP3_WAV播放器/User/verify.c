/**
 * @file    verify.c
 * @brief   Command-triggered W25Q64 readback over USART1.
 */

#include "verify.h"
#include "w25q64.h"
#include "uart_tx.h"
#include "bsp_usart.h"
#include "ring_buffer.h"
#include "OLED.h"
#include "main.h"
#include "Delay.h"
#include "stm32f10x.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CRC32_POLY          0xEDB88320UL
#define CMD_LINE_MAX        48U

static VerifyState_t verify_state = VERIFY_STATE_IDLE;
static uint8_t       read_buf[VERIFY_READ_CHUNK_SIZE];
static char          line_buf[CMD_LINE_MAX];
static uint8_t       line_pos = 0;
static uint32_t      bytes_sent = 0;
static uint32_t      last_crc32 = 0;
static uint32_t      last_jedec_id = 0;

static void Verify_DisableDmaRx(void)
{
    USART_DMACmd(USART1, USART_DMAReq_Rx, DISABLE);
}

static uint8_t Verify_ReadByte(uint8_t *byte)
{
    return RingBuffer_Get(&usart1_rx_ring, byte);
}

static uint32_t Verify_CRC32Update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    crc = ~crc;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1) ^ CRC32_POLY;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

static uint8_t Verify_StrStartsWithNoCase(const char *str, const char *prefix)
{
    while (*prefix != '\0') {
        char a = *str++;
        char b = *prefix++;

        if (a >= 'A' && a <= 'Z') {
            a = (char)(a + ('a' - 'A'));
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b + ('a' - 'A'));
        }
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

static uint8_t Verify_ParseSize(const char *line, const char *cmd, uint32_t *size)
{
    const char *p;
    uint32_t value = 0;
    uint8_t has_digit = 0;

    if (!Verify_StrStartsWithNoCase(line, cmd)) {
        return 0;
    }

    p = line + strlen(cmd);
    while (*p == ':' || *p == ' ' || *p == '\t') {
        p++;
    }

    if (*p == '\0') {
        *size = VERIFY_WAV_SIZE;
        return 1;
    }

    while (*p >= '0' && *p <= '9') {
        has_digit = 1;
        value = value * 10UL + (uint32_t)(*p - '0');
        p++;
    }

    if (has_digit == 0U) {
        return 0;
    }

    *size = value;
    return 1;
}

static void Verify_SendLine(const char *format, ...)
{
    char out[96];
    va_list args;
    int len;

    va_start(args, format);
    len = vsnprintf(out, sizeof(out) - 3U, format, args);
    va_end(args);

    if (len < 0) {
        len = 0;
    }
    if ((uint32_t)len > (sizeof(out) - 3U)) {
        len = (int)(sizeof(out) - 3U);
    }

    out[len++] = '\r';
    out[len++] = '\n';
    out[len] = '\0';
    UART_TX_SendBuffer((const uint8_t *)out, (uint32_t)len);
}

static void Verify_UpdateOLEDProgress(void)
{
    char buf[17];

    OLED_ShowString(2, 1, "State: DUMPING  ");
    sprintf(buf, "%lu/%lu", bytes_sent, (uint32_t)VERIFY_WAV_SIZE);
    buf[16] = '\0';
    OLED_ShowString(3, 1, "Sent bytes:     ");
    OLED_ShowString(4, 1, "                ");
    OLED_ShowString(4, 1, buf);
}

static void Verify_DumpFlash(uint32_t requested_size)
{
    uint32_t remaining;

    if (requested_size != VERIFY_WAV_SIZE) {
        Verify_SendLine("ERR:SIZE:%lu:%lu", requested_size, (uint32_t)VERIFY_WAV_SIZE);
        verify_state = VERIFY_STATE_ERROR;
        return;
    }

    last_jedec_id = W25Q64_ReadJedecID();
    if (W25Q64_Is64MbitCompatible(last_jedec_id) == 0U) {
        Verify_SendLine("ERR:JEDEC:0x%08lX", last_jedec_id);
        verify_state = VERIFY_STATE_ERROR;
        return;
    }

    if (W25Q64_Read(VERIFY_FLASH_BASE_ADDR, read_buf, 1U) != 0U) {
        Verify_SendLine("ERR:RANGE");
        verify_state = VERIFY_STATE_ERROR;
        return;
    }

    verify_state = VERIFY_STATE_DUMPING;
    bytes_sent = 0;
    last_crc32 = 0;
    Verify_SendLine("READY:%lu", (uint32_t)VERIFY_WAV_SIZE);
    Delay_ms(20);

    remaining = VERIFY_WAV_SIZE;
    while (remaining > 0U) {
        uint32_t chunk = remaining;
        if (chunk > VERIFY_READ_CHUNK_SIZE) {
            chunk = VERIFY_READ_CHUNK_SIZE;
        }

        if (W25Q64_Read(VERIFY_FLASH_BASE_ADDR + bytes_sent, read_buf, chunk) != 0U) {
            verify_state = VERIFY_STATE_ERROR;
            return;
        }

        last_crc32 = Verify_CRC32Update(last_crc32, read_buf, chunk);
        UART_TX_SendBuffer(read_buf, chunk);
        bytes_sent += chunk;
        remaining -= chunk;

        if ((bytes_sent & 0x0FFFU) == 0U || remaining == 0U) {
            LED_TOGGLE();
            Verify_UpdateOLEDProgress();
        }
    }

    UART_TX_WaitIdle();
    Verify_SendLine("DONE:%lu:0x%08lX", bytes_sent, last_crc32);
    verify_state = VERIFY_STATE_DONE;
}

static void Verify_HandleLine(const char *line)
{
    uint32_t requested_size;

    if (Verify_StrStartsWithNoCase(line, "PING")) {
        Verify_SendLine("PONG");
        return;
    }

    if (Verify_StrStartsWithNoCase(line, "ID?")) {
        last_jedec_id = W25Q64_ReadJedecID();
        Verify_SendLine("ID:0x%08lX", last_jedec_id);
        return;
    }

    if (Verify_StrStartsWithNoCase(line, "HELP")) {
        Verify_SendLine("CMD:READBACK:%lu", (uint32_t)VERIFY_WAV_SIZE);
        Verify_SendLine("CMD:ID?");
        return;
    }

    if (Verify_ParseSize(line, "READBACK", &requested_size) ||
        Verify_ParseSize(line, "DUMP", &requested_size)) {
        Verify_DumpFlash(requested_size);
        return;
    }

    if (line[0] != '\0') {
        Verify_SendLine("ERR:CMD");
    }
}

void Verify_Init(void)
{
    Verify_DisableDmaRx();
    verify_state = VERIFY_STATE_IDLE;
    bytes_sent = 0;
    last_crc32 = 0;
    line_pos = 0;
    RingBuffer_Reset(&usart1_rx_ring);
    last_jedec_id = W25Q64_ReadJedecID();
    Verify_SendLine("VERIFY_FW_READY:%lu:0x%08lX:%lu",
                    (uint32_t)VERIFY_WAV_SIZE,
                    last_jedec_id,
                    (uint32_t)USART1_BAUDRATE);
}

void Verify_Process(void)
{
    uint8_t byte;

    if (verify_state == VERIFY_STATE_DUMPING) {
        return;
    }

    while (Verify_ReadByte(&byte)) {
        if (byte == '\r') {
            continue;
        }

        if (byte == '\n') {
            line_buf[line_pos] = '\0';
            Verify_HandleLine(line_buf);
            line_pos = 0;
            if (verify_state == VERIFY_STATE_DUMPING) {
                break;
            }
            continue;
        }

        if (line_pos < (CMD_LINE_MAX - 1U)) {
            line_buf[line_pos++] = (char)byte;
        } else {
            line_pos = 0;
            Verify_SendLine("ERR:LINE_TOO_LONG");
        }
    }

}

void Verify_OLED_Refresh(void)
{
    char buf[17];

    switch (verify_state) {
    case VERIFY_STATE_IDLE:
        OLED_ShowString(2, 1, "State: WAIT CMD ");
        sprintf(buf, "ID:%06lX", last_jedec_id);
        OLED_ShowString(3, 1, "Flash ready     ");
        OLED_ShowString(4, 1, "READBACK wait   ");
        OLED_ShowString(1, 1, buf);
        break;

    case VERIFY_STATE_DUMPING:
        Verify_UpdateOLEDProgress();
        break;

    case VERIFY_STATE_DONE:
        OLED_ShowString(2, 1, "State: DONE     ");
        sprintf(buf, "CRC:%08lX", last_crc32);
        OLED_ShowString(3, 1, "Sent all bytes  ");
        OLED_ShowString(4, 1, buf);
        break;

    case VERIFY_STATE_ERROR:
        OLED_ShowString(2, 1, "State: ERROR    ");
        OLED_ShowString(3, 1, "Check UART/SPI  ");
        OLED_ShowString(4, 1, "Send HELP       ");
        break;

    default:
        break;
    }
}

VerifyState_t Verify_GetState(void)
{
    return verify_state;
}

uint32_t Verify_GetBytesSent(void)
{
    return bytes_sent;
}

uint32_t Verify_GetLastCRC32(void)
{
    return last_crc32;
}
