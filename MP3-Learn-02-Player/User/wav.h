/**
 * @file    wav.h
 * @brief   WAV header parser for files stored in W25Q64.
 */

#ifndef __WAV_H
#define __WAV_H

#include <stdint.h>

typedef enum {
    WAV_STATUS_OK = 0,
    WAV_STATUS_IO_ERROR,
    WAV_STATUS_RIFF_NOT_FOUND,
    WAV_STATUS_WAVE_NOT_FOUND,
    WAV_STATUS_FMT_NOT_FOUND,
    WAV_STATUS_DATA_NOT_FOUND,
    WAV_STATUS_INVALID_CHUNK,
    WAV_STATUS_UNSUPPORTED_FORMAT
} WavStatus_t;

typedef struct {
    uint32_t riff_offset;
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t pcm_offset;
    uint32_t pcm_size;
} WavInfo_t;

WavStatus_t WAV_ParseFromFlash(uint32_t base_addr, uint32_t file_size, WavInfo_t *info);
const char *WAV_StatusString(WavStatus_t status);

#endif
