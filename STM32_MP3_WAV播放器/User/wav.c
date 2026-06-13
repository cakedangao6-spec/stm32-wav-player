/**
 * @file    wav.c
 * @brief   WAV 文件头解析器，用于从 W25Q64 中找到 PCM 音频数据
 */

#include "wav.h"
#include "w25q64.h"
#include <string.h>

#define WAV_RIFF_SCAN_LIMIT  64U
#define WAV_MIN_HEADER_SIZE  12U
#define WAV_CHUNK_HEADER_SIZE 8U

/**
  * 函    数：读取小端格式的 16 位数据
  * 参    数：data 指向 2 个字节数据的指针
  * 返 回 值：转换后的 16 位整数
  */
static uint16_t WAV_ReadLe16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

/**
  * 函    数：读取小端格式的 32 位数据
  * 参    数：data 指向 4 个字节数据的指针
  * 返 回 值：转换后的 32 位整数
  * 说    明：WAV 文件中的多字节数字使用小端格式保存
  */
static uint32_t WAV_ReadLe32(const uint8_t *data)
{
    return (uint32_t)data[0]
         | ((uint32_t)data[1] << 8)
         | ((uint32_t)data[2] << 16)
         | ((uint32_t)data[3] << 24);
}

/**
  * 函    数：从 W25Q64 读取 WAV 文件数据
  * 参    数：addr 读取地址
  * 参    数：data 接收数据的数组
  * 参    数：len  读取长度
  * 返 回 值：0 表示成功，非 0 表示失败
  */
static uint8_t WAV_Read(uint32_t addr, uint8_t *data, uint32_t len)
{
    return W25Q64_Read(addr, data, len);        // 通过 W25Q64 应用层接口读取，带地址越界检查
}

/**
  * 函    数：在文件开头附近查找 RIFF 标志
  * 参    数：base_addr WAV 文件在 Flash 中的起始地址
  * 参    数：file_size WAV 文件大小
  * 参    数：riff_addr 用于返回 RIFF 标志所在地址
  * 返 回 值：1 表示找到 RIFF，0 表示未找到
  */
static uint8_t WAV_FindRiff(uint32_t base_addr, uint32_t file_size, uint32_t *riff_addr)
{
    uint8_t fourcc[4];
    uint32_t scan_limit = file_size;

    if (scan_limit > WAV_RIFF_SCAN_LIMIT) {
        scan_limit = WAV_RIFF_SCAN_LIMIT;
    }
    if (scan_limit < 4U) {
        return 0U;
    }

    for (uint32_t offset = 0; offset <= (scan_limit - 4U); offset++) { // 在前 64 字节内逐字节查找 "RIFF"
        if (WAV_Read(base_addr + offset, fourcc, sizeof(fourcc)) != 0U) {
            return 0U;
        }
        if (memcmp(fourcc, "RIFF", 4U) == 0) {
            *riff_addr = base_addr + offset;          // 返回 RIFF 的实际地址
            return 1U;
        }
    }

    return 0U;
}

/**
  * 函    数：从 W25Q64 中解析 WAV 文件
  * 参    数：base_addr WAV 文件起始地址
  * 参    数：file_size WAV 文件总大小
  * 参    数：info 解析结果输出结构体
  * 返 回 值：WAV_STATUS_OK 表示解析成功，其余值表示具体错误
  */
WavStatus_t WAV_ParseFromFlash(uint32_t base_addr, uint32_t file_size, WavInfo_t *info)
{
    uint8_t header[WAV_MIN_HEADER_SIZE];
    uint8_t chunk_header[WAV_CHUNK_HEADER_SIZE];
    uint8_t fmt_buf[16];
    uint32_t riff_addr;
    uint32_t cursor;
    uint32_t file_end;
    uint8_t fmt_found = 0U;
    uint8_t data_found = 0U;

    if (info == 0 || file_size < WAV_MIN_HEADER_SIZE) {
        return WAV_STATUS_INVALID_CHUNK;
    }

    memset(info, 0, sizeof(*info));                 // 先清空输出结构体，避免残留旧数据

    if (WAV_FindRiff(base_addr, file_size, &riff_addr) == 0U) {
        return WAV_STATUS_RIFF_NOT_FOUND;           // WAV 文件必须以 RIFF 块开始
    }
    if (WAV_Read(riff_addr, header, sizeof(header)) != 0U) {
        return WAV_STATUS_IO_ERROR;
    }
    if (memcmp(&header[8], "WAVE", 4U) != 0) {
        return WAV_STATUS_WAVE_NOT_FOUND;           // RIFF 后面必须声明文件类型为 WAVE
    }

    info->riff_offset = riff_addr - base_addr;      // 记录 RIFF 相对文件起始位置的偏移
    cursor = riff_addr + WAV_MIN_HEADER_SIZE;       // 跳过 RIFF 头，开始扫描子块
    file_end = base_addr + file_size;               // 计算文件结束地址

    while ((cursor + WAV_CHUNK_HEADER_SIZE) <= file_end) {
        uint32_t chunk_size;
        uint32_t chunk_data_addr;
        uint32_t next_chunk_addr;

        if (WAV_Read(cursor, chunk_header, sizeof(chunk_header)) != 0U) {
            return WAV_STATUS_IO_ERROR;
        }

        chunk_size = WAV_ReadLe32(&chunk_header[4]);            // 子块数据长度
        chunk_data_addr = cursor + WAV_CHUNK_HEADER_SIZE;       // 子块数据起始地址
        next_chunk_addr = chunk_data_addr + chunk_size + (chunk_size & 1U); // WAV 子块按偶数字节对齐

        if (chunk_data_addr > file_end || next_chunk_addr > file_end || next_chunk_addr < chunk_data_addr) {
            return WAV_STATUS_INVALID_CHUNK;         // 子块长度异常，防止越界读取
        }

        if (memcmp(chunk_header, "fmt ", 4U) == 0) {
            if (chunk_size < sizeof(fmt_buf)) {
                return WAV_STATUS_INVALID_CHUNK;
            }
            if (WAV_Read(chunk_data_addr, fmt_buf, sizeof(fmt_buf)) != 0U) {
                return WAV_STATUS_IO_ERROR;
            }

            info->audio_format = WAV_ReadLe16(&fmt_buf[0]);     // 音频格式，1 表示 PCM
            info->channels = WAV_ReadLe16(&fmt_buf[2]);         // 声道数，1 单声道，2 双声道
            info->sample_rate = WAV_ReadLe32(&fmt_buf[4]);      // 采样率，例如 8000/16000/44100
            info->byte_rate = WAV_ReadLe32(&fmt_buf[8]);        // 每秒字节数
            info->block_align = WAV_ReadLe16(&fmt_buf[12]);     // 每个采样帧占用的字节数
            info->bits_per_sample = WAV_ReadLe16(&fmt_buf[14]); // 每个声道的位深
            fmt_found = 1U;                                     // 标记已经找到格式块
        } else if (memcmp(chunk_header, "data", 4U) == 0) {
            info->pcm_offset = chunk_data_addr - base_addr;     // PCM 数据相对文件起始地址的偏移
            info->pcm_size = chunk_size;                        // PCM 数据长度
            data_found = 1U;                                    // 标记已经找到数据块
        }

        if (fmt_found && data_found) {
            break;                                              // 格式块和数据块都找到后即可停止扫描
        }

        cursor = next_chunk_addr;                               // 跳到下一个子块
    }

    if (fmt_found == 0U) {
        return WAV_STATUS_FMT_NOT_FOUND;
    }
    if (data_found == 0U) {
        return WAV_STATUS_DATA_NOT_FOUND;
    }
    if (info->audio_format != 1U ||
        (info->channels != 1U && info->channels != 2U) ||
        (info->bits_per_sample != 8U && info->bits_per_sample != 16U) ||
        info->sample_rate == 0U ||
        info->block_align == 0U ||
        info->pcm_size < info->block_align) {
        return WAV_STATUS_UNSUPPORTED_FORMAT;       // 当前播放器只支持 PCM、单/双声道、8/16 位
    }

    return WAV_STATUS_OK;
}

/**
  * 函    数：将 WAV 状态码转换为可读字符串
  * 参    数：status WAV 解析状态码
  * 返 回 值：错误说明字符串
  */
const char *WAV_StatusString(WavStatus_t status)
{
    switch (status) {
    case WAV_STATUS_OK:
        return "OK";
    case WAV_STATUS_IO_ERROR:
        return "W25Q64 read error";
    case WAV_STATUS_RIFF_NOT_FOUND:
        return "RIFF not found";
    case WAV_STATUS_WAVE_NOT_FOUND:
        return "WAVE not found";
    case WAV_STATUS_FMT_NOT_FOUND:
        return "fmt chunk not found";
    case WAV_STATUS_DATA_NOT_FOUND:
        return "data chunk not found";
    case WAV_STATUS_INVALID_CHUNK:
        return "invalid WAV chunk";
    case WAV_STATUS_UNSUPPORTED_FORMAT:
        return "unsupported WAV format";
    default:
        return "unknown WAV error";
    }
}
