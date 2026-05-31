# STM32F103 + W25Q64 WAV 播放器项目说明

## 1. 项目一句话概述

这是一个基于 `STM32F103C8T6`、标准库 `StdPeriph` 和外部 SPI Flash `W25Q64` 的 WAV 播放器项目。

电脑先通过串口把 `666.wav` 发送给 STM32，STM32 将文件写入 W25Q64。播放时，STM32 再从 W25Q64 中读取 WAV 文件，解析出 PCM 音频数据，并按采样率输出音频，最后由功放驱动喇叭发声。

## 2. 先说明一个很重要的事实

项目最初的学习目标可以写成：

```text
电脑
 ↓
串口
 ↓
STM32
 ↓
W25Q64
 ↓
WAV
 ↓
PCM
 ↓
DMA
 ↓
DAC
 ↓
功放
 ↓
喇叭
```

但当前这份工程的真实实现不是 `DAC + DMA`，而是：

```text
电脑
 ↓
串口
 ↓
STM32
 ↓
W25Q64
 ↓
WAV
 ↓
PCM
 ↓
TIM2 采样节拍
 ↓
TIM3 PWM
 ↓
RC 低通
 ↓
功放
 ↓
喇叭
```

原因是当前芯片为 `STM32F103C8T6`，当前工程实际完成的是 `PWM 音频输出` 版本。  
所以，阅读本工程时请记住：

- `DAC` 和 `DMA` 是后续升级方向；
- 当前代码中真正负责声音输出的是 `TIM2_IRQHandler()` 和 `TIM3_CH3 / PB0`；
- `PA4` 在当前工程中是 `W25Q64_CS`，不是音频输出脚。

## 3. 当前项目已经完成什么

### 3.1 已完成功能

1. USART1 串口通信；
2. 将 `666.wav` 通过串口写入 W25Q64；
3. W25Q64 擦除、页写、连续读取；
4. W25Q64 全量回读；
5. 使用 `compare.py` 对原文件和回读文件做完整比对；
6. 从 W25Q64 解析 WAV 文件；
7. 支持 PCM WAV 的 8-bit / 16-bit、单声道 / 双声道输入；
8. 使用双缓冲从 Flash 连续取样；
9. 使用 PWM 输出音频；
10. 已实测完整播放 `666.wav`。

### 3.2 已验证的 `666.wav`

```text
文件大小: 301810 字节
格式: PCM
采样率: 8000 Hz
位宽: 16 bit
声道数: 1
PCM 起始偏移: 44
PCM 数据大小: 301766 字节
时长: 约 18.86 秒
```

### 3.3 已验证的输出结果

回读验证阶段：

```text
文件大小一致
MD5 一致
CRC32 一致
逐字节一致
PASS
W25Q64内容完全正确
```

播放阶段串口输出：

```text
Playback boot
WAV format: PCM
Sample rate: 8000 Hz
Bits: 16
Channels: 1
PCM offset: 44
PCM size: 301766
Playback start
Playback end
```

实际播放持续时间约为 `18.857 s`，与源文件长度基本一致。

## 4. 工程目录

```text
STM32_MP3_WAV播放器
├─ Hardware
│  ├─ OLED.c / OLED.h
│  ├─ LED.c / LED.h
│  └─ Key.c / Key.h
├─ Library
│  └─ STM32 标准外设库源码
├─ Start
│  └─ 启动文件、CMSIS、system 文件
├─ System
│  └─ Delay.c / Delay.h
├─ User
│  ├─ main.c
│  ├─ main.h
│  ├─ bsp_usart.c / bsp_usart.h
│  ├─ bsp_w25q64.c / bsp_w25q64.h
│  ├─ app_file_transfer.c / app_file_transfer.h
│  ├─ protocol.c / protocol.h
│  ├─ ring_buffer.c / ring_buffer.h
│  ├─ w25q64.c / w25q64.h
│  ├─ verify.c / verify.h
│  ├─ wav.c / wav.h
│  ├─ audio_player.c / audio_player.h
│  └─ uart_tx.c / uart_tx.h
├─ 文档
│  └─ 软件资料
│     ├─ 当前项目开发报告.md
│     └─ STUDY_NOTE.md
├─ 日志
│  ├─ build_playback_log.txt
│  └─ flash_playback_log.txt
├─ 归档
│  └─ backup_20260515_playback
├─ 硬件设计
│  ├─ STM32_MP3项目硬件说明书.md
│  ├─ STM32_MP3硬件设计学习手册.docx
│  ├─ STM32_MP3原理图连接表.csv
│  ├─ STM32_MP3_BOM.csv
│  └─ figures
│     ├─ 系统框图.png
│     ├─ 模块连接图.png
│     ├─ 原理图草图.png
│     └─ PCB布局草图.png
├─ Project.uvprojx
└─ README.md
```

工作区中与工程配套的资源目录如下：

```text
MP3
├─ 音频素材
│  └─ 666.wav
├─ 验证产物
│  ├─ readback.wav
│  └─ readback_before_rewrite.wav
└─ 工具
   ├─ 串口验证\compare.py
   └─ XCOM V2.6
```

## 5. 主要模块一览

| 模块 | 作用 |
| --- | --- |
| `main.c` | 系统入口，选择传输、验证或播放模式 |
| `bsp_w25q64.c` | W25Q64 底层 SPI 驱动 |
| `w25q64.c` | 给上层使用的安全包装层 |
| `wav.c` | 解析 WAV 文件头 |
| `audio_player.c` | 读取 PCM、双缓冲、PWM 播放 |
| `app_file_transfer.c` | 接收电脑文件并写入 Flash |
| `verify.c` | 把 Flash 内容回传给电脑做校验 |
| `compare.py` | 电脑端自动接收、保存、比对 |

## 6. 当前程序如何运行

### 6.1 `main.c` 中的模式选择

```c
#define APP_MODE_TRANSFER 0U
#define APP_MODE_VERIFY   1U
#define APP_MODE_PLAYBACK 2U
#define APP_MODE          APP_MODE_PLAYBACK
```

当前编译的是播放模式。

### 6.2 播放模式的主流程

```text
上电复位
 ↓
RCC_Configuration()
 ↓
USART1_Init()
 ↓
GPIO_Configuration()
 ↓
SPI1_Configuration()
 ↓
W25Q64_DriverInit()
 ↓
WAV_ParseFromFlash()
 ↓
AudioPlayer_Init()
 ↓
AudioPlayer_Start()
 ↓
AudioPlayer_Process() + TIM2_IRQHandler()
 ↓
播放结束
```

## 7. 硬件接线

### 7.1 W25Q64

| 信号 | STM32 引脚 |
| --- | --- |
| CS | `PA4` |
| SCK | `PA5` |
| MISO | `PA6` |
| MOSI | `PA7` |

### 7.2 串口

| 信号 | STM32 引脚 |
| --- | --- |
| TX | `PA9` |
| RX | `PA10` |

### 7.3 音频输出

当前工程真实音频输出脚：

```text
PB0 / TIM3_CH3
```

早期联调最小接法可以这样做：

```text
PB0
 ↓
1 kΩ 电阻
 ↓
音频节点 ───> 功放输入
 ↓
10 nF 电容
 ↓
GND
```

正式硬件设计已升级为：

```text
PB0
 ↓
2.2 kΩ + 10 nF 一级低通
 ↓
2.2 kΩ + 10 nF 二级低通
 ↓
10 kA 音量电位器
 ↓
PAM8403
 ↓
喇叭
```

完整解释见 `硬件设计\STM32_MP3硬件设计学习手册.docx`。

还要注意：

1. STM32 与功放模块必须共地；
2. 喇叭接在功放输出端；
3. `PA4` 不接功放，它是 W25Q64 的片选脚；
4. 不要把普通喇叭直接接到 STM32 引脚上。

## 8. 如何重新编译和烧录

### 8.1 编译

```powershell
& "<Keil安装目录>\UV4\UV4.exe" -b ".\Project.uvprojx" -o ".\build_playback_log.txt"
```

### 8.2 烧录

```powershell
& "<Keil安装目录>\UV4\UV4.exe" -f ".\Project.uvprojx" -o ".\flash_playback_log.txt"
```

## 9. 如何验证项目是否正常

### 9.1 验证 W25Q64 内容

切换到验证模式后，运行：

```powershell
python "..\工具\串口验证\compare.py" --port COM4
```

应看到：

```text
PASS
W25Q64内容完全正确
```

### 9.2 验证播放链路

播放模式下，上电后串口应打印：

```text
WAV format: PCM
Sample rate: 8000 Hz
Bits: 16
Channels: 1
Playback start
Playback end
```

如果 `Playback start` 和 `Playback end` 都出现，说明：

1. W25Q64 可以读取；
2. WAV 解析成功；
3. 采样定时器运行；
4. 播放状态机完整走完。

## 10. 最容易忘记的五件事

1. 当前音频输出是 `PB0`，不是 `PA4`；
2. 当前版本是 `PWM`，不是 `DAC + DMA`；
3. `compare.py` 在工作区的 `工具\串口验证` 目录；
4. 串口默认是 `115200 baud`；
5. 只看文件大小不够，必须做 readback 和逐字节验证。

## 11. 如果半年后重新开始，先看什么

建议顺序：

1. 先读本文件；
2. 再读 `文档\软件资料\STUDY_NOTE.md`；
3. 打开 `main.c` 看模式选择；
4. 打开 `wav.c` 看 WAV 怎么解析；
5. 打开 `audio_player.c` 看声音怎么出来；
6. 如果 Flash 有疑问，再看 `verify.c` 和 `compare.py`。
