/**
 * @file    audio_player.c
 * @brief   PWM audio player for PCM WAV data stored in W25Q64.
 */

#include "audio_player.h"
#include "main.h"
#include "w25q64.h"
#include "stm32f10x.h"
#include "misc.h"

#define AUDIO_TIMER_CLOCK_HZ       72000000UL
#define AUDIO_PWM_PERIOD           1023U
#define AUDIO_PWM_MIDPOINT         (AUDIO_PWM_PERIOD / 2U)
#define AUDIO_BUFFER_SAMPLES        512U
#define AUDIO_MAX_SOURCE_BYTES     (AUDIO_BUFFER_SAMPLES * 4U)

static WavInfo_t wav;
static uint16_t pwm_buffers[2][AUDIO_BUFFER_SAMPLES];
static uint8_t  source_buffer[AUDIO_MAX_SOURCE_BYTES];

static volatile AudioPlayerState_t player_state = AUDIO_PLAYER_IDLE;
static volatile AudioPlayerError_t last_error = AUDIO_PLAYER_ERR_NONE;
static volatile uint8_t  buffer_ready[2] = {0U, 0U};
static volatile uint16_t buffer_valid_samples[2] = {0U, 0U};
static volatile uint8_t  active_buffer = 0U;
static volatile uint16_t active_index = 0U;
static volatile uint32_t samples_played = 0U;

static uint32_t next_source_addr = 0U;
static uint32_t source_frames_remaining = 0U;
static uint32_t total_frames = 0U;

static int16_t AudioPlayer_ReadS16(const uint8_t *data)
{
    return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint16_t AudioPlayer_U16ToPwm(uint16_t sample)
{
    return (uint16_t)(((uint32_t)sample * AUDIO_PWM_PERIOD) / 65535UL);
}

static uint8_t AudioPlayer_FillBuffer(uint8_t buffer_id)
{
    uint32_t frames_to_read;
    uint32_t bytes_to_read;

    if (source_frames_remaining == 0U) {
        return 1U;
    }

    frames_to_read = source_frames_remaining;
    if (frames_to_read > AUDIO_BUFFER_SAMPLES) {
        frames_to_read = AUDIO_BUFFER_SAMPLES;
    }
    bytes_to_read = frames_to_read * wav.block_align;

    if (bytes_to_read > sizeof(source_buffer)) {
        last_error = AUDIO_PLAYER_ERR_BAD_FORMAT;
        player_state = AUDIO_PLAYER_ERROR;
        return 0U;
    }
    if (W25Q64_Read(next_source_addr, source_buffer, bytes_to_read) != 0U) {
        last_error = AUDIO_PLAYER_ERR_READ;
        player_state = AUDIO_PLAYER_ERROR;
        return 0U;
    }

    for (uint32_t i = 0; i < frames_to_read; i++) {
        uint32_t offset = i * wav.block_align;
        uint16_t unsigned_sample;

        if (wav.bits_per_sample == 8U) {
            uint16_t left = source_buffer[offset];
            if (wav.channels == 2U) {
                uint16_t right = source_buffer[offset + 1U];
                left = (uint16_t)((left + right) / 2U);
            }
            unsigned_sample = (uint16_t)(left << 8);
        } else {
            int32_t left = AudioPlayer_ReadS16(&source_buffer[offset]);
            if (wav.channels == 2U) {
                int32_t right = AudioPlayer_ReadS16(&source_buffer[offset + 2U]);
                left = (left + right) / 2;
            }
            left += 32768;
            if (left < 0) {
                left = 0;
            }
            if (left > 65535) {
                left = 65535;
            }
            unsigned_sample = (uint16_t)left;
        }

        pwm_buffers[buffer_id][i] = AudioPlayer_U16ToPwm(unsigned_sample);
    }

    next_source_addr += bytes_to_read;
    source_frames_remaining -= frames_to_read;
    buffer_valid_samples[buffer_id] = (uint16_t)frames_to_read;
    buffer_ready[buffer_id] = 1U;
    return 1U;
}

static void AudioPlayer_ConfigPwm(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef time_base;
    TIM_OCInitTypeDef oc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    gpio.GPIO_Pin = AUDIO_PWM_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(AUDIO_PWM_PORT, &gpio);

    TIM_TimeBaseStructInit(&time_base);
    time_base.TIM_Prescaler = 0U;
    time_base.TIM_Period = AUDIO_PWM_PERIOD;
    time_base.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &time_base);

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse = AUDIO_PWM_MIDPOINT;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC3Init(TIM3, &oc);
    TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
}

static uint8_t AudioPlayer_ConfigSampleTimer(uint32_t sample_rate)
{
    TIM_TimeBaseInitTypeDef time_base;
    NVIC_InitTypeDef nvic;
    uint32_t period;

    if (sample_rate == 0U) {
        return 0U;
    }

    period = (AUDIO_TIMER_CLOCK_HZ + (sample_rate / 2U)) / sample_rate;
    if (period == 0U || period > 65536U) {
        return 0U;
    }

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_TimeBaseStructInit(&time_base);
    time_base.TIM_Prescaler = 0U;
    time_base.TIM_Period = (uint16_t)(period - 1U);
    time_base.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &time_base);
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    nvic.NVIC_IRQChannel = TIM2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1U;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    return 1U;
}

uint8_t AudioPlayer_Init(const WavInfo_t *wav_info)
{
    if (wav_info == 0 ||
        wav_info->audio_format != 1U ||
        (wav_info->channels != 1U && wav_info->channels != 2U) ||
        (wav_info->bits_per_sample != 8U && wav_info->bits_per_sample != 16U) ||
        wav_info->block_align == 0U ||
        wav_info->pcm_size < wav_info->block_align) {
        last_error = AUDIO_PLAYER_ERR_BAD_FORMAT;
        player_state = AUDIO_PLAYER_ERROR;
        return 0U;
    }

    wav = *wav_info;
    next_source_addr = wav.pcm_offset;
    total_frames = wav.pcm_size / wav.block_align;
    source_frames_remaining = total_frames;
    buffer_ready[0] = 0U;
    buffer_ready[1] = 0U;
    buffer_valid_samples[0] = 0U;
    buffer_valid_samples[1] = 0U;
    active_buffer = 0U;
    active_index = 0U;
    samples_played = 0U;
    last_error = AUDIO_PLAYER_ERR_NONE;
    player_state = AUDIO_PLAYER_IDLE;

    AudioPlayer_ConfigPwm();
    if (AudioPlayer_ConfigSampleTimer(wav.sample_rate) == 0U) {
        last_error = AUDIO_PLAYER_ERR_BAD_FORMAT;
        player_state = AUDIO_PLAYER_ERROR;
        return 0U;
    }

    return 1U;
}

uint8_t AudioPlayer_Start(void)
{
    if (player_state == AUDIO_PLAYER_ERROR || total_frames == 0U) {
        return 0U;
    }
    if (AudioPlayer_FillBuffer(0U) == 0U) {
        return 0U;
    }
    if (source_frames_remaining > 0U && AudioPlayer_FillBuffer(1U) == 0U) {
        return 0U;
    }

    active_buffer = 0U;
    active_index = 0U;
    samples_played = 0U;
    player_state = AUDIO_PLAYER_PLAYING;
    TIM_SetCounter(TIM2, 0U);
    TIM_Cmd(TIM2, ENABLE);
    return 1U;
}

void AudioPlayer_Process(void)
{
    if (player_state != AUDIO_PLAYER_PLAYING) {
        return;
    }

    for (uint8_t i = 0U; i < 2U; i++) {
        if (buffer_ready[i] == 0U && source_frames_remaining > 0U) {
            if (AudioPlayer_FillBuffer(i) == 0U) {
                TIM_Cmd(TIM2, DISABLE);
                TIM3->CCR3 = AUDIO_PWM_MIDPOINT;
                return;
            }
        }
    }
}

AudioPlayerState_t AudioPlayer_GetState(void)
{
    return player_state;
}

AudioPlayerError_t AudioPlayer_GetLastError(void)
{
    return last_error;
}

uint32_t AudioPlayer_GetSamplesPlayed(void)
{
    return samples_played;
}

uint32_t AudioPlayer_GetTotalSamples(void)
{
    return total_frames;
}

void TIM2_IRQHandler(void)
{
    uint8_t next_buffer;

    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == RESET) {
        return;
    }
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

    if (player_state != AUDIO_PLAYER_PLAYING) {
        TIM3->CCR3 = AUDIO_PWM_MIDPOINT;
        return;
    }
    if (buffer_ready[active_buffer] == 0U ||
        active_index >= buffer_valid_samples[active_buffer]) {
        last_error = AUDIO_PLAYER_ERR_UNDERFLOW;
        player_state = AUDIO_PLAYER_ERROR;
        TIM_Cmd(TIM2, DISABLE);
        TIM3->CCR3 = AUDIO_PWM_MIDPOINT;
        return;
    }

    TIM3->CCR3 = pwm_buffers[active_buffer][active_index++];
    samples_played++;

    if (samples_played >= total_frames) {
        player_state = AUDIO_PLAYER_DONE;
        TIM_Cmd(TIM2, DISABLE);
        TIM3->CCR3 = AUDIO_PWM_MIDPOINT;
        return;
    }

    if (active_index >= buffer_valid_samples[active_buffer]) {
        buffer_ready[active_buffer] = 0U;
        buffer_valid_samples[active_buffer] = 0U;
        next_buffer = (uint8_t)(active_buffer ^ 1U);

        if (buffer_ready[next_buffer] == 0U) {
            last_error = AUDIO_PLAYER_ERR_UNDERFLOW;
            player_state = AUDIO_PLAYER_ERROR;
            TIM_Cmd(TIM2, DISABLE);
            TIM3->CCR3 = AUDIO_PWM_MIDPOINT;
            return;
        }

        active_buffer = next_buffer;
        active_index = 0U;
    }
}
