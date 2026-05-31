/**
 * @file    audio_player.h
 * @brief   PWM audio player for PCM WAV data stored in W25Q64.
 */

#ifndef __AUDIO_PLAYER_H
#define __AUDIO_PLAYER_H

#include <stdint.h>
#include "wav.h"

typedef enum {
    AUDIO_PLAYER_IDLE = 0,
    AUDIO_PLAYER_PLAYING,
    AUDIO_PLAYER_DONE,
    AUDIO_PLAYER_ERROR
} AudioPlayerState_t;

typedef enum {
    AUDIO_PLAYER_ERR_NONE = 0,
    AUDIO_PLAYER_ERR_BAD_FORMAT,
    AUDIO_PLAYER_ERR_READ,
    AUDIO_PLAYER_ERR_UNDERFLOW
} AudioPlayerError_t;

uint8_t AudioPlayer_Init(const WavInfo_t *wav_info);
uint8_t AudioPlayer_Start(void);
void AudioPlayer_Process(void);
AudioPlayerState_t AudioPlayer_GetState(void);
AudioPlayerError_t AudioPlayer_GetLastError(void);
uint32_t AudioPlayer_GetSamplesPlayed(void);
uint32_t AudioPlayer_GetTotalSamples(void);

#endif
