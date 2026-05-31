/**
 * @file    wav.c
 * @brief   WAV header parser for files stored in W25Q64.
 */

#include "wav.h"
#include "w25q64.h"
#include <string.h>

#define WAV_RIFF_SCAN_LIMIT  64U
#define WAV_MIN_HEADER_SIZE  12U
#define WAV_CHUNK_HEADER_SIZE 8U

static uint16_t WAV_ReadLe16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t WAV_ReadLe32(const uint8_t *data)
{
    return (uint32_t)data[0]
         | ((uint32_t)data[1] << 8)
         | ((uint32_t)data[2] << 16)
         | ((uint32_t)data[3] << 24);
}

static uint8_t WAV_Read(uint32_t addr, uint8_t *data, uint32_t len)
{
    return W25Q64_Read(addr, data, len);
}

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

    for (uint32_t offset = 0; offset <= (scan_limit - 4U); offset++) {
        if (WAV_Read(base_addr + offset, fourcc, sizeof(fourcc)) != 0U) {
            return 0U;
        }
        if (memcmp(fourcc, "RIFF", 4U) == 0) {
            *riff_addr = base_addr + offset;
            return 1U;
        }
    }

    return 0U;
}

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

    memset(info, 0, sizeof(*info));

    if (WAV_FindRiff(base_addr, file_size, &riff_addr) == 0U) {
        return WAV_STATUS_RIFF_NOT_FOUND;
    }
    if (WAV_Read(riff_addr, header, sizeof(header)) != 0U) {
        return WAV_STATUS_IO_ERROR;
    }
    if (memcmp(&header[8], "WAVE", 4U) != 0) {
        return WAV_STATUS_WAVE_NOT_FOUND;
    }

    info->riff_offset = riff_addr - base_addr;
    cursor = riff_addr + WAV_MIN_HEADER_SIZE;
    file_end = base_addr + file_size;

    while ((cursor + WAV_CHUNK_HEADER_SIZE) <= file_end) {
        uint32_t chunk_size;
        uint32_t chunk_data_addr;
        uint32_t next_chunk_addr;

        if (WAV_Read(cursor, chunk_header, sizeof(chunk_header)) != 0U) {
            return WAV_STATUS_IO_ERROR;
        }

        chunk_size = WAV_ReadLe32(&chunk_header[4]);
        chunk_data_addr = cursor + WAV_CHUNK_HEADER_SIZE;
        next_chunk_addr = chunk_data_addr + chunk_size + (chunk_size & 1U);

        if (chunk_data_addr > file_end || next_chunk_addr > file_end || next_chunk_addr < chunk_data_addr) {
            return WAV_STATUS_INVALID_CHUNK;
        }

        if (memcmp(chunk_header, "fmt ", 4U) == 0) {
            if (chunk_size < sizeof(fmt_buf)) {
                return WAV_STATUS_INVALID_CHUNK;
            }
            if (WAV_Read(chunk_data_addr, fmt_buf, sizeof(fmt_buf)) != 0U) {
                return WAV_STATUS_IO_ERROR;
            }

            info->audio_format = WAV_ReadLe16(&fmt_buf[0]);
            info->channels = WAV_ReadLe16(&fmt_buf[2]);
            info->sample_rate = WAV_ReadLe32(&fmt_buf[4]);
            info->byte_rate = WAV_ReadLe32(&fmt_buf[8]);
            info->block_align = WAV_ReadLe16(&fmt_buf[12]);
            info->bits_per_sample = WAV_ReadLe16(&fmt_buf[14]);
            fmt_found = 1U;
        } else if (memcmp(chunk_header, "data", 4U) == 0) {
            info->pcm_offset = chunk_data_addr - base_addr;
            info->pcm_size = chunk_size;
            data_found = 1U;
        }

        if (fmt_found && data_found) {
            break;
        }

        cursor = next_chunk_addr;
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
        return WAV_STATUS_UNSUPPORTED_FORMAT;
    }

    return WAV_STATUS_OK;
}

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
