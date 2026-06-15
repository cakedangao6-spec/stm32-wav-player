/**
 * @file    audio_player.c
 * @brief   PWM 音频播放器，用于播放 W25Q64 中保存的 PCM WAV 数据
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

static WavInfo_t wav;                                           // 当前播放 WAV 的格式信息
static uint16_t pwm_buffers[2][AUDIO_BUFFER_SAMPLES];           // 双缓冲：一个播放，一个填充
static uint8_t  source_buffer[AUDIO_MAX_SOURCE_BYTES];          // 从 Flash 读取的原始 PCM 数据缓冲

static volatile AudioPlayerState_t player_state = AUDIO_PLAYER_IDLE; // 播放器当前状态
static volatile AudioPlayerError_t last_error = AUDIO_PLAYER_ERR_NONE; // 最近一次错误
static volatile uint8_t  buffer_ready[2] = {0U, 0U};            // 标记两个 PWM 缓冲区是否已有可播放数据
static volatile uint16_t buffer_valid_samples[2] = {0U, 0U};   // 每个缓冲区中有效样本数量
static volatile uint8_t  active_buffer = 0U;                    // TIM2 中断当前正在播放的缓冲区编号
static volatile uint16_t active_index = 0U;                     // 当前缓冲区已经播放到的样本下标
static volatile uint32_t samples_played = 0U;                   // 已经播放的采样帧数量

static uint32_t next_source_addr = 0U;                          // 下一次从 Flash 读取 PCM 的地址
static uint32_t source_frames_remaining = 0U;                   // 剩余未读取的采样帧数量
static uint32_t total_frames = 0U;                              // WAV 文件总采样帧数量

/**
  * 函    数：读取小端格式的 16 位有符号采样值
  * 参    数：data 指向 2 字节 PCM 数据的指针
  * 返 回 值：转换后的 int16_t 采样值
  */
static int16_t AudioPlayer_ReadS16(const uint8_t *data)
{
    return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

/**
  * 函    数：将 16 位无符号音频样本转换为 PWM 比较值
  * 参    数：sample 0~65535 的音频样本
  * 返 回 值：0~AUDIO_PWM_PERIOD 的 PWM 比较值
  */
static uint16_t AudioPlayer_U16ToPwm(uint16_t sample)
{
    return (uint16_t)(((uint32_t)sample * AUDIO_PWM_PERIOD) / 65535UL);
}

/**
  * 函    数：填充指定 PWM 缓冲区
  * 参    数：buffer_id 缓冲区编号，取值 0 或 1
  * 返 回 值：1 表示成功，0 表示失败
  * 说    明：从 W25Q64 读取 PCM，转换为 TIM3 PWM 占空比数据
  */
static uint8_t AudioPlayer_FillBuffer(uint8_t buffer_id)
{
    uint32_t frames_to_read;
    uint32_t bytes_to_read;

    if (source_frames_remaining == 0U) {
        return 1U;
    }

    frames_to_read = source_frames_remaining;                 // 默认读取所有剩余采样帧
    if (frames_to_read > AUDIO_BUFFER_SAMPLES) {
        frames_to_read = AUDIO_BUFFER_SAMPLES;                // 单次最多填满一个 PWM 缓冲区
    }
    bytes_to_read = frames_to_read * wav.block_align;         // 采样帧数转换为字节数

    if (bytes_to_read > sizeof(source_buffer)) {
        last_error = AUDIO_PLAYER_ERR_BAD_FORMAT;
        player_state = AUDIO_PLAYER_ERROR;
        return 0U;
    }
    if (W25Q64_Read(next_source_addr, source_buffer, bytes_to_read) != 0U) { // 从 Flash 读取一段 PCM 数据
        last_error = AUDIO_PLAYER_ERR_READ;
        player_state = AUDIO_PLAYER_ERROR;
        return 0U;
    }

    for (uint32_t i = 0; i < frames_to_read; i++) {
        uint32_t offset = i * wav.block_align;                // 当前采样帧在 source_buffer 中的偏移
        uint16_t unsigned_sample;

        if (wav.bits_per_sample == 8U) {
            uint16_t left = source_buffer[offset];            // 8 位 PCM 通常是无符号数据
            if (wav.channels == 2U) {
                uint16_t right = source_buffer[offset + 1U];  // 双声道时再取右声道
                left = (uint16_t)((left + right) / 2U);       // 左右声道平均，混成单声道
            }
            unsigned_sample = (uint16_t)(left << 8);          // 8 位扩展为 16 位范围
        } else {
            int32_t left = AudioPlayer_ReadS16(&source_buffer[offset]); // 16 位 PCM 通常是有符号数据
            if (wav.channels == 2U) {
                int32_t right = AudioPlayer_ReadS16(&source_buffer[offset + 2U]);
                left = (left + right) / 2;                    // 双声道混音为单声道
            }
            left += 32768;                                    // 有符号 -32768~32767 转为无符号 0~65535
            if (left < 0) {
                left = 0;
            }
            if (left > 65535) {
                left = 65535;
            }
            unsigned_sample = (uint16_t)left;
        }

        pwm_buffers[buffer_id][i] = AudioPlayer_U16ToPwm(unsigned_sample); // 将音频幅值转换为 PWM 占空比
    }

    next_source_addr += bytes_to_read;                         // Flash 读取地址后移
    source_frames_remaining -= frames_to_read;                 // 剩余采样帧减少
    buffer_valid_samples[buffer_id] = (uint16_t)frames_to_read; // 记录本缓冲区有效样本数
    buffer_ready[buffer_id] = 1U;                              // 标记本缓冲区已经可以播放
    return 1U;
}

/**
  * 函    数：配置 TIM3 PWM 输出
  * 参    数：无
  * 返 回 值：无
  * 说    明：PB0/TIM3_CH3 输出 PWM，占空比代表当前音频样本幅值
  */
static void AudioPlayer_ConfigPwm(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef time_base;
    TIM_OCInitTypeDef oc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE); // 开启 GPIOB 和 AFIO 时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);                        // 开启 TIM3 时钟

    gpio.GPIO_Pin = AUDIO_PWM_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;                       // PWM 引脚必须配置为复用推挽输出
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(AUDIO_PWM_PORT, &gpio);

    TIM_TimeBaseStructInit(&time_base);
    time_base.TIM_Prescaler = 0U;
    time_base.TIM_Period = AUDIO_PWM_PERIOD;                 // ARR 决定 PWM 分辨率
    time_base.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &time_base);

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode = TIM_OCMode_PWM1;                         // PWM 模式 1
    oc.TIM_OutputState = TIM_OutputState_Enable;             // 使能输出比较通道
    oc.TIM_Pulse = AUDIO_PWM_MIDPOINT;                       // 初始占空比设为中点，避免突然偏置
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC3Init(TIM3, &oc);
    TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
}

/**
  * 函    数：配置 TIM2 采样率定时中断
  * 参    数：sample_rate WAV 文件采样率
  * 返 回 值：1 表示成功，0 表示采样率不支持
  */
static uint8_t AudioPlayer_ConfigSampleTimer(uint32_t sample_rate)
{
    TIM_TimeBaseInitTypeDef time_base;
    NVIC_InitTypeDef nvic;
    uint32_t period;

    if (sample_rate == 0U) {
        return 0U;
    }

    period = (AUDIO_TIMER_CLOCK_HZ + (sample_rate / 2U)) / sample_rate; // 根据采样率计算 TIM2 更新周期
    if (period == 0U || period > 65536U) {
        return 0U;
    }

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);      // 开启 TIM2 时钟

    TIM_TimeBaseStructInit(&time_base);
    time_base.TIM_Prescaler = 0U;
    time_base.TIM_Period = (uint16_t)(period - 1U);           // TIM2 每溢出一次，输出一个音频样本
    time_base.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &time_base);
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);                // 开启 TIM2 更新中断

    nvic.NVIC_IRQChannel = TIM2_IRQn;                         // 配置 TIM2 中断通道
    nvic.NVIC_IRQChannelPreemptionPriority = 1U;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    return 1U;
}

/**
  * 函    数：音频播放器初始化
  * 参    数：wav_info WAV 解析得到的格式信息
  * 返 回 值：1 表示成功，0 表示失败
  */
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

    wav = *wav_info;                                      // 保存 WAV 格式信息
    next_source_addr = wav.pcm_offset;                    // PCM 数据在 Flash 中的起始地址
    total_frames = wav.pcm_size / wav.block_align;        // 根据 PCM 大小计算总采样帧数
    source_frames_remaining = total_frames;               // 初始时所有采样帧都还未读取
    buffer_ready[0] = 0U;
    buffer_ready[1] = 0U;
    buffer_valid_samples[0] = 0U;
    buffer_valid_samples[1] = 0U;
    active_buffer = 0U;
    active_index = 0U;
    samples_played = 0U;
    last_error = AUDIO_PLAYER_ERR_NONE;
    player_state = AUDIO_PLAYER_IDLE;

    AudioPlayer_ConfigPwm();                              // 配置 TIM3_CH3 PWM 输出
    if (AudioPlayer_ConfigSampleTimer(wav.sample_rate) == 0U) {
        last_error = AUDIO_PLAYER_ERR_BAD_FORMAT;
        player_state = AUDIO_PLAYER_ERROR;
        return 0U;
    }

    return 1U;
}

/**
  * 函    数：开始播放
  * 参    数：无
  * 返 回 值：1 表示启动成功，0 表示启动失败
  */
uint8_t AudioPlayer_Start(void)
{
    if (player_state == AUDIO_PLAYER_ERROR || total_frames == 0U) {
        return 0U;
    }
    if (AudioPlayer_FillBuffer(0U) == 0U) {                // 先填充第 0 个缓冲区
        return 0U;
    }
    if (source_frames_remaining > 0U && AudioPlayer_FillBuffer(1U) == 0U) { // 再填充第 1 个缓冲区
        return 0U;
    }

    active_buffer = 0U;                                   // 从第 0 个缓冲区开始播放
    active_index = 0U;                                    // 从第 0 个样本开始
    samples_played = 0U;                                  // 已播放计数清零
    player_state = AUDIO_PLAYER_PLAYING;                  // 状态切换为播放中
    TIM_SetCounter(TIM2, 0U);                             // TIM2 计数器清零
    TIM_Cmd(TIM2, ENABLE);                                // 启动 TIM2，开始按采样率进入中断
    return 1U;
}

/**
  * 函    数：音频播放器后台处理
  * 参    数：无
  * 返 回 值：无
  * 说    明：主循环调用此函数，负责给空闲缓冲区补充数据
  */
void AudioPlayer_Process(void)
{
    if (player_state != AUDIO_PLAYER_PLAYING) {
        return;
    }

    for (uint8_t i = 0U; i < 2U; i++) {
        if (buffer_ready[i] == 0U && source_frames_remaining > 0U) {
            if (AudioPlayer_FillBuffer(i) == 0U) {         // 如果某个缓冲区已被播放完，就重新填充
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

/**
  * 函    数：TIM2 中断服务函数
  * 参    数：无
  * 返 回 值：无
  * 说    明：每次中断输出 1 个音频样本，是实际发声的关键函数
  */
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

    TIM3->CCR3 = pwm_buffers[active_buffer][active_index++]; // 将当前样本写入 TIM3_CCR3，改变 PWM 占空比
    samples_played++;                                        // 已播放样本数加 1

    if (samples_played >= total_frames) {
        player_state = AUDIO_PLAYER_DONE;                    // 所有样本播放完成
        TIM_Cmd(TIM2, DISABLE);                              // 关闭采样定时器
        TIM3->CCR3 = AUDIO_PWM_MIDPOINT;                     // PWM 回到中点
        return;
    }

    if (active_index >= buffer_valid_samples[active_buffer]) {
        buffer_ready[active_buffer] = 0U;                    // 当前缓冲区播完，标记为空，主循环会重新填充
        buffer_valid_samples[active_buffer] = 0U;
        next_buffer = (uint8_t)(active_buffer ^ 1U);         // 在 0 和 1 两个缓冲区之间切换

        if (buffer_ready[next_buffer] == 0U) {
            last_error = AUDIO_PLAYER_ERR_UNDERFLOW;         // 下一个缓冲区还没准备好，说明发生欠载
            player_state = AUDIO_PLAYER_ERROR;
            TIM_Cmd(TIM2, DISABLE);
            TIM3->CCR3 = AUDIO_PWM_MIDPOINT;
            return;
        }

        active_buffer = next_buffer;                         // 切换到下一个缓冲区继续播放
        active_index = 0U;                                   // 新缓冲区从第 0 个样本开始
    }
}
