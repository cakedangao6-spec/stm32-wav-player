/**
 * @file    app_file_transfer.c
 * @brief   文件传输状态机 + OLED 显示，用于把电脑发送的 WAV 文件写入 W25Q64
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

static AppState_t   app_state = APP_STATE_IDLE;             // 当前文件传输状态
static uint32_t     expected_size   = 0;                    // 上位机声明的 WAV 文件总大小
static uint32_t     total_received  = 0;                    // 已经接收并写入的字节数
static uint32_t     flash_write_addr = 0;                   // 下一次写入 W25Q64 的地址
static uint32_t     sector_count    = 0;                    // 本次文件需要擦除的扇区数量
static uint32_t     sector_index    = 0;                    // 当前正在擦除的扇区序号
static uint8_t      page_buf[W25Q64_PAGE_SIZE];             // W25Q64 页写缓冲区，大小 256 字节
static uint16_t     page_pos        = 0;                    // page_buf 当前已缓存的字节数
static uint32_t     verify_addr     = 0;                    // CRC 校验时当前读到的 Flash 地址
static uint32_t     verify_crc      = 0;                    // CRC32 计算过程值
static uint8_t      verify_buf[VERIFY_CHUNK_SIZE];          // CRC 校验时的临时读取缓冲区
static char         resp_buf[PROTOCOL_LINE_MAX];            // 发送给上位机的响应字符串缓冲区

/**
  * 函    数：CRC32 计算更新
  * 参    数：crc  上一次 CRC32 值
  * 参    数：data 本次参与计算的数据
  * 参    数：len  本次数据长度
  * 返 回 值：更新后的 CRC32 值
  * 说    明：用于校验 W25Q64 中保存的 WAV 数据是否和电脑端一致
  */
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

/**
  * 函    数：处理一条串口命令
  * 参    数：无
  * 返 回 值：1 表示尝试处理过数据，0 表示当前没有数据
  */
static uint8_t App_HandleCommand(void)
{
    uint8_t byte;
    if (!RingBuffer_Get(&usart1_rx_ring, &byte)) return 0;      // 从串口环形缓冲区取 1 个字节

    ProtocolParseResult_t result = Protocol_FeedByte(byte);     // 将字节交给协议解析器，可能得到一条完整命令

    switch (result.cmd) {
    case PROTO_CMD_PING:
        Protocol_BuildResponse(resp_buf, "PONG");               // 上位机测试连接，回复 PONG
        USART1_SendString(resp_buf);
        break;
    case PROTO_CMD_WRITE:
        expected_size = result.param;                           // WRITE 命令参数是即将写入的文件大小
        if (expected_size == 0 || expected_size > W25Q64_TOTAL_SIZE) {
            Protocol_BuildResponse(resp_buf, "ERR");            // 文件大小非法，返回错误
            USART1_SendString(resp_buf);
            break;
        }
        flash_write_addr = 0;                                   // 文件从 W25Q64 地址 0 开始写入
        sector_count = (expected_size + W25Q64_SECTOR_SIZE - 1) / W25Q64_SECTOR_SIZE; // 计算需要擦除几个 4KB 扇区
        sector_index = 0;                                       // 从第 0 个扇区开始擦除
        total_received = 0;                                     // 清空接收计数
        page_pos = 0;                                           // 清空页缓冲区位置
        app_state = APP_STATE_ERASING;                          // 进入擦除状态
        break;
    case PROTO_CMD_VERIFY:
        verify_addr = 0;                                        // 从 Flash 地址 0 开始校验
        verify_crc  = 0;                                        // CRC 初值清零
        app_state   = APP_STATE_VERIFYING;                      // 进入校验状态
        break;
    case PROTO_CMD_UNKNOWN:
        Protocol_BuildResponse(resp_buf, "ERR");                // 未识别命令，返回错误
        USART1_SendString(resp_buf);
        break;
    default: break;
    }
    return 1;
}

/**
  * 函    数：文件传输状态机处理函数
  * 参    数：无
  * 返 回 值：无
  * 说    明：需要在 main 的 while(1) 中不断调用
  */
void App_FileTransfer_Process(void)
{
    switch (app_state) {
    case APP_STATE_IDLE:
        while (RingBuffer_Available(&usart1_rx_ring) > 0) {
            App_HandleCommand();                                // 空闲状态只解析文本命令
            if (app_state != APP_STATE_IDLE) break;             // 收到 WRITE/VERIFY 后跳出，进入对应状态
        }
        break;

    case APP_STATE_ERASING: {
        uint32_t sector_addr = sector_index * W25Q64_SECTOR_SIZE; // 当前扇区起始地址
        if (W25Q64_SectorErase(sector_addr) != 0) {
            Protocol_BuildResponse(resp_buf, "ERR");            // 擦除失败，通知上位机
            USART1_SendString(resp_buf);
            app_state = APP_STATE_IDLE;
            Protocol_Reset();
            break;
        }
        sector_index++;                                         // 擦除成功，准备擦除下一个扇区
        if (sector_index >= sector_count) {
            Protocol_BuildResponse(resp_buf, "READY");          // 所有扇区擦完，通知上位机可以开始发文件数据
            USART1_SendString(resp_buf);
            app_state = APP_STATE_READY;                        // 进入等待原始文件数据状态
            RingBuffer_Reset(&usart1_rx_ring);                  // 清掉擦除期间可能残留的字节
            Protocol_Reset();                                   // 协议解析器复位，后面接收的是二进制数据
        }
        break;
    }

    case APP_STATE_READY:
        if (RingBuffer_Available(&usart1_rx_ring) > 0) {
            app_state = APP_STATE_RECEIVING;                    // 一旦收到文件数据字节，就进入接收状态
        } else break;
        /* fall-through */

    case APP_STATE_RECEIVING: {
        uint8_t byte;
        while (total_received < expected_size) {
            if (!RingBuffer_Get(&usart1_rx_ring, &byte)) break; // 没有新数据时先退出，等待下次主循环
            page_buf[page_pos++] = byte;                        // 将收到的字节暂存到 256 字节页缓冲
            total_received++;                                   // 已接收字节数加 1
            if (page_pos >= W25Q64_PAGE_SIZE || total_received >= expected_size) {
                // 页缓冲满 256 字节，或者文件最后不足 256 字节，都执行一次页编程
                if (W25Q64_PageProgram(flash_write_addr, page_buf, page_pos) != 0) {
                    Protocol_BuildResponse(resp_buf, "ERR");    // 页写入失败，通知上位机
                    USART1_SendString(resp_buf);
                    app_state = APP_STATE_IDLE;
                    Protocol_Reset();
                    goto proc_exit;
                }
                flash_write_addr += page_pos;                   // Flash 写入地址后移
                page_pos = 0;                                   // 页缓冲重新从 0 开始存放
            }
        }
        if (total_received >= expected_size) {
            Protocol_BuildResponse(resp_buf, "OK:%u", total_received); // 文件全部写入完成，返回实际写入字节数
            USART1_SendString(resp_buf);
            app_state = APP_STATE_IDLE;                         // 回到空闲状态，等待 VERIFY 或下一次 WRITE
            Protocol_Reset();
            RingBuffer_Reset(&usart1_rx_ring);
        }
        break;
    }

    case APP_STATE_VERIFYING: {
        while (verify_addr < expected_size) {
            uint32_t chunk = expected_size - verify_addr;       // 计算剩余待校验字节数
            if (chunk > VERIFY_CHUNK_SIZE) chunk = VERIFY_CHUNK_SIZE; // 每次最多读取 256 字节
            W25Q64_ReadData(verify_addr, verify_buf, chunk);    // 从 W25Q64 读出一段数据
            verify_crc  = CRC32_Update(verify_crc, verify_buf, chunk); // 更新 CRC32
            verify_addr += chunk;                               // 校验地址后移
        }
        Protocol_BuildResponse(resp_buf, "OK:0x%08X", verify_crc); // 返回 Flash 数据 CRC32 给上位机比较
        USART1_SendString(resp_buf);
        app_state = APP_STATE_IDLE;
        Protocol_Reset();
        break;
    }
    default: break;
    }
proc_exit: ;
}

/**
  * 函    数：刷新 OLED 显示
  * 参    数：无
  * 返 回 值：无
  */
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

/**
  * 函    数：文件传输状态机初始化
  * 参    数：无
  * 返 回 值：无
  */
void App_FileTransfer_Init(void)
{
    app_state       = APP_STATE_IDLE;                         // 初始为空闲状态
    expected_size   = 0;
    total_received  = 0;
    flash_write_addr = 0;
    sector_count    = 0;
    sector_index    = 0;
    page_pos        = 0;
    verify_addr     = 0;
    verify_crc      = 0;
    RingBuffer_Init(&usart1_rx_ring);                         // 初始化 USART 接收缓冲
    Protocol_Reset();                                         // 初始化文本命令解析器
}

/**
  * 函    数：获取当前传输状态
  * 参    数：无
  * 返 回 值：当前状态机状态
  */
AppState_t App_GetState(void) { return app_state; }
