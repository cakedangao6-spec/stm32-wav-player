/**
 * @file    app_file_transfer.c
 * @brief   文件传输状态机 + OLED 显示
 */

#include "app_file_transfer.h"
#include "bsp_usart.h"
#include "bsp_w25q64.h"
#include "protocol.h"
#include "ring_buffer.h"
#include "OLED.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

#define CRC32_POLY          0xEDB88320U
#define VERIFY_CHUNK_SIZE   256U

static AppState_t   app_state = APP_STATE_IDLE;
static uint32_t     expected_size   = 0;
static uint32_t     total_received  = 0;
static uint32_t     flash_write_addr = 0;
static uint32_t     sector_count    = 0;
static uint32_t     sector_index    = 0;
static uint8_t      page_buf[W25Q64_PAGE_SIZE];
static uint16_t     page_pos        = 0;
static uint32_t     verify_addr     = 0;
static uint32_t     verify_crc      = 0;
static uint8_t      verify_buf[VERIFY_CHUNK_SIZE];
static char         resp_buf[PROTOCOL_LINE_MAX];

static uint32_t CRC32_Update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    crc = ~crc;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ CRC32_POLY;
            else         crc >>= 1;
        }
    }
    return ~crc;
}

static uint8_t App_HandleCommand(void)
{
    uint8_t byte;
    if (!RingBuffer_Get(&usart1_rx_ring, &byte)) return 0;

    ProtocolParseResult_t result = Protocol_FeedByte(byte);

    switch (result.cmd) {
    case PROTO_CMD_PING:
        Protocol_BuildResponse(resp_buf, "PONG");
        USART1_SendString(resp_buf);
        break;
    case PROTO_CMD_WRITE:
        expected_size = result.param;
        if (expected_size == 0 || expected_size > W25Q64_TOTAL_SIZE) {
            Protocol_BuildResponse(resp_buf, "ERR");
            USART1_SendString(resp_buf);
            break;
        }
        flash_write_addr = 0;
        sector_count = (expected_size + W25Q64_SECTOR_SIZE - 1) / W25Q64_SECTOR_SIZE;
        sector_index = 0;
        total_received = 0;
        page_pos = 0;
        app_state = APP_STATE_ERASING;
        break;
    case PROTO_CMD_VERIFY:
        verify_addr = 0;
        verify_crc  = 0;
        app_state   = APP_STATE_VERIFYING;
        break;
    case PROTO_CMD_UNKNOWN:
        Protocol_BuildResponse(resp_buf, "ERR");
        USART1_SendString(resp_buf);
        break;
    default: break;
    }
    return 1;
}

void App_FileTransfer_Process(void)
{
    switch (app_state) {
    case APP_STATE_IDLE:
        while (RingBuffer_Available(&usart1_rx_ring) > 0) {
            App_HandleCommand();
            if (app_state != APP_STATE_IDLE) break;
        }
        break;

    case APP_STATE_ERASING: {
        uint32_t sector_addr = sector_index * W25Q64_SECTOR_SIZE;
        if (W25Q64_SectorErase(sector_addr) != 0) {
            Protocol_BuildResponse(resp_buf, "ERR");
            USART1_SendString(resp_buf);
            app_state = APP_STATE_IDLE;
            Protocol_Reset();
            break;
        }
        sector_index++;
        if (sector_index >= sector_count) {
            Protocol_BuildResponse(resp_buf, "READY");
            USART1_SendString(resp_buf);
            app_state = APP_STATE_READY;
            RingBuffer_Reset(&usart1_rx_ring);
            Protocol_Reset();
        }
        break;
    }

    case APP_STATE_READY:
        if (RingBuffer_Available(&usart1_rx_ring) > 0) {
            app_state = APP_STATE_RECEIVING;
        } else break;
        /* fall-through */

    case APP_STATE_RECEIVING: {
        uint8_t byte;
        while (total_received < expected_size) {
            if (!RingBuffer_Get(&usart1_rx_ring, &byte)) break;
            page_buf[page_pos++] = byte;
            total_received++;
            if (page_pos >= W25Q64_PAGE_SIZE || total_received >= expected_size) {
                if (W25Q64_PageProgram(flash_write_addr, page_buf, page_pos) != 0) {
                    Protocol_BuildResponse(resp_buf, "ERR");
                    USART1_SendString(resp_buf);
                    app_state = APP_STATE_IDLE;
                    Protocol_Reset();
                    goto proc_exit;
                }
                flash_write_addr += page_pos;
                page_pos = 0;
            }
        }
        if (total_received >= expected_size) {
            Protocol_BuildResponse(resp_buf, "OK:%u", total_received);
            USART1_SendString(resp_buf);
            app_state = APP_STATE_IDLE;
            Protocol_Reset();
            RingBuffer_Reset(&usart1_rx_ring);
        }
        break;
    }

    case APP_STATE_VERIFYING: {
        while (verify_addr < expected_size) {
            uint32_t chunk = expected_size - verify_addr;
            if (chunk > VERIFY_CHUNK_SIZE) chunk = VERIFY_CHUNK_SIZE;
            W25Q64_ReadData(verify_addr, verify_buf, chunk);
            verify_crc  = CRC32_Update(verify_crc, verify_buf, chunk);
            verify_addr += chunk;
        }
        Protocol_BuildResponse(resp_buf, "OK:0x%08X", verify_crc);
        USART1_SendString(resp_buf);
        app_state = APP_STATE_IDLE;
        Protocol_Reset();
        break;
    }
    default: break;
    }
proc_exit: ;
}

void App_OLED_Refresh(void)
{
    char buf[17];
    switch (app_state) {
    case APP_STATE_IDLE:
        OLED_ShowString(2, 1, "State: IDLE     ");
        if (expected_size > 0) {
            sprintf(buf, "Size: %-7lu  ", expected_size);
            OLED_ShowString(3, 1, buf);
            OLED_ShowString(4, 1, "Done. Send VERIFY");
        }
        break;
    case APP_STATE_ERASING:
        OLED_ShowString(2, 1, "State: ERASING  ");
        sprintf(buf, "Erasing %lu/%lu  ", sector_index, sector_count);
        OLED_ShowString(3, 1, buf);
        OLED_ShowString(4, 1, "Please wait...  ");
        break;
    case APP_STATE_READY:
        OLED_ShowString(2, 1, "State: READY    ");
        sprintf(buf, "Size: %-7lu  ", expected_size);
        OLED_ShowString(3, 1, buf);
        OLED_ShowString(4, 1, "Send file now!  ");
        break;
    case APP_STATE_RECEIVING:
        OLED_ShowString(2, 1, "State: RECV     ");
        sprintf(buf, "Size: %-7lu  ", expected_size);
        OLED_ShowString(3, 1, buf);
        if (expected_size > 0) {
            uint32_t pct = (uint32_t)((uint64_t)total_received * 100 / expected_size);
            sprintf(buf, "%lu%% (%lu/%lu)", pct, total_received, expected_size);
            buf[16] = '\0';
            int len = strlen(buf);
            while (len < 16) { memmove(buf + 1, buf, ++len); buf[0] = ' '; }
            OLED_ShowString(4, 1, buf);
        }
        break;
    case APP_STATE_VERIFYING:
        OLED_ShowString(2, 1, "State: VERIFY   ");
        OLED_ShowString(3, 1, "Checking CRC32..");
        if (expected_size > 0 && verify_addr > 0) {
            uint32_t pct = (uint32_t)((uint64_t)verify_addr * 100 / expected_size);
            sprintf(buf, "Progress: %lu%%   ", pct);
            OLED_ShowString(4, 1, buf);
        }
        break;
    default: break;
    }
}

void App_FileTransfer_Init(void)
{
    app_state       = APP_STATE_IDLE;
    expected_size   = 0;
    total_received  = 0;
    flash_write_addr = 0;
    sector_count    = 0;
    sector_index    = 0;
    page_pos        = 0;
    verify_addr     = 0;
    verify_crc      = 0;
    RingBuffer_Init(&usart1_rx_ring);
    Protocol_Reset();
}

AppState_t App_GetState(void) { return app_state; }
