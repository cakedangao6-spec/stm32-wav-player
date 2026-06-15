/**
 * @file    audio_player.h
 * @brief   PWM 音频播放器
 */

#ifndef __AUDIO_PLAYER_H
#define __AUDIO_PLAYER_H

#include <stdint.h>
#include "wav.h"

typedef enum {
    AUDIO_PLAYER_IDLE = 0,      // 空闲，尚未开始播放
    AUDIO_PLAYER_PLAYING,       // 正在播放
    AUDIO_PLAYER_DONE,          // 播放完成
    AUDIO_PLAYER_ERROR          // 播放出错
} AudioPlayerState_t;

typedef enum {
    AUDIO_PLAYER_ERR_NONE = 0,      // 无错误
    AUDIO_PLAYER_ERR_BAD_FORMAT,    // WAV 格式不支持
    AUDIO_PLAYER_ERR_READ,          // 读取 W25Q64 失败
    AUDIO_PLAYER_ERR_UNDERFLOW      // 双缓冲欠载，来不及补数据
} AudioPlayerError_t;

uint8_t AudioPlayer_Init(const WavInfo_t *wav_info);
uint8_t AudioPlayer_Start(void);
void AudioPlayer_Process(void);
AudioPlayerState_t AudioPlayer_GetState(void);
AudioPlayerError_t AudioPlayer_GetLastError(void);
uint32_t AudioPlayer_GetSamplesPlayed(void);
uint32_t AudioPlayer_GetTotalSamples(void);

#endif
