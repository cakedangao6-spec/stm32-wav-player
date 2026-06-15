/**
 * @file    wav.h
 * @brief   WAV 文件头解析器
 */

#ifndef __WAV_H
#define __WAV_H

#include <stdint.h>

typedef enum {
    WAV_STATUS_OK = 0,              // 解析成功
    WAV_STATUS_IO_ERROR,            // 从 W25Q64 读取失败
    WAV_STATUS_RIFF_NOT_FOUND,      // 没找到 RIFF 标志
    WAV_STATUS_WAVE_NOT_FOUND,      // 没找到 WAVE 标志
    WAV_STATUS_FMT_NOT_FOUND,       // 没找到 fmt 格式块
    WAV_STATUS_DATA_NOT_FOUND,      // 没找到 data 数据块
    WAV_STATUS_INVALID_CHUNK,       // chunk 长度或地址异常
    WAV_STATUS_UNSUPPORTED_FORMAT   // 格式不支持
} WavStatus_t;

typedef struct {
    uint32_t riff_offset;       // RIFF 标志相对文件起始地址的偏移
    uint16_t audio_format;      // 音频格式，1 表示 PCM
    uint16_t channels;          // 声道数，1 单声道，2 双声道
    uint32_t sample_rate;       // 采样率
    uint32_t byte_rate;         // 每秒字节数
    uint16_t block_align;       // 每个采样帧占用字节数
    uint16_t bits_per_sample;   // 位深，当前支持 8/16 bit
    uint32_t pcm_offset;        // PCM 数据相对文件起始地址的偏移
    uint32_t pcm_size;          // PCM 数据长度
} WavInfo_t;

WavStatus_t WAV_ParseFromFlash(uint32_t base_addr, uint32_t file_size, WavInfo_t *info);
const char *WAV_StatusString(WavStatus_t status);

#endif
