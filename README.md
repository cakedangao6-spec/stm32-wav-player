# STM32 WAV Player

这是一个个人嵌入式学习项目，目标是在 `STM32F103C8T6` 上完成串口写入 SPI Flash、解析 WAV 文件并通过 PWM 输出音频。项目整理和文档编写过程中使用了 Codex 辅助，但功能状态以当前源码和实测记录为准，不夸大为完整商品化播放器。

## 快速状态

- 主控：`STM32F103C8T6`
- 外部存储：`W25Q64` SPI Flash
- 音频格式：已围绕 PCM WAV 做学习和验证
- 当前输出方式：`TIM3_CH3 / PB0` PWM 输出，经 RC 低通后接功放
- 当前默认模式：播放模式，详见 `STM32_MP3_WAV播放器/User/main.c`
- 已验证样例：`666.wav` 曾完成写入、回读比对和播放验证
- 未完成/未宣称：不是 MP3 解码器；当前不是 `DAC + DMA` 输出版本

## 目录说明

```text
MP3
├─ STM32_MP3_WAV播放器     # 主 Keil 工程、源码、文档、硬件设计
├─ MP3-Learn-01-Downloader # 串口下载到 Flash 的拆分学习工程
├─ MP3-Learn-02-Player     # WAV 播放链路的拆分学习工程
├─ 工具                    # 串口校验脚本；外部 exe 工具不上传
├─ 音频素材                # 本机测试音频，不上传
└─ 验证产物                # 本机回读和验证输出，不上传
```

## 主要内容

| 目录 | 内容 |
| --- | --- |
| `STM32_MP3_WAV播放器` | 当前主工程，包含源码、硬件设计和软件资料 |
| `MP3-Learn-01-Downloader` | 文件传输/写入 Flash 的阶段性学习工程 |
| `MP3-Learn-02-Player` | WAV 解析和 PWM 播放的阶段性学习工程 |
| `工具/串口验证/compare.py` | 电脑端串口回读和文件比对脚本 |

## 编译环境

- Windows
- Keil uVision 5 / ARMCC
- STM32F10x Standard Peripheral Library
- Python 3，用于运行串口验证脚本
- 串口工具或 USB-TTL 模块，用于下载音频和查看日志

打开主工程：

```text
STM32_MP3_WAV播放器/Project.uvprojx
```

Keil 命令行编译示例：

```powershell
& "<Keil安装目录>\UV4\UV4.exe" -b ".\STM32_MP3_WAV播放器\Project.uvprojx" -o ".\build_playback_log.txt"
```

## 硬件连接摘要

### W25Q64

| 信号 | STM32 引脚 |
| --- | --- |
| CS | `PA4` |
| SCK | `PA5` |
| MISO | `PA6` |
| MOSI | `PA7` |

### 串口

| 信号 | STM32 引脚 |
| --- | --- |
| TX | `PA9` |
| RX | `PA10` |

### 音频输出

当前工程使用：

```text
PB0 / TIM3_CH3 -> RC 低通 -> 音量电位器/功放 -> 喇叭
```

注意：

1. `PA4` 是 W25Q64 片选，不是音频输出；
2. STM32、Flash、功放模块需要共地；
3. 不要把普通喇叭直接接到 STM32 IO；
4. 当前工程不是 DAC 输出版本。

## 运行和验证

在 `STM32_MP3_WAV播放器/User/main.c` 中通过 `APP_MODE` 选择模式：

```c
#define APP_MODE_TRANSFER 0U
#define APP_MODE_VERIFY   1U
#define APP_MODE_PLAYBACK 2U
#define APP_MODE          APP_MODE_PLAYBACK
```

验证 Flash 内容时，可在切换到验证模式并烧录后运行：

```powershell
python ".\工具\串口验证\compare.py" --port COM4
```

串口号需要按本机实际设备修改。原始测试音频和回读产物属于本机验证数据，默认不会提交到 GitHub。

## 最常用入口

| 目的 | 入口 |
| --- | --- |
| 看项目总说明 | `STM32_MP3_WAV播放器\README.md` |
| 打开 Keil 工程 | `STM32_MP3_WAV播放器\Project.uvprojx` |
| 看软件学习笔记 | `STM32_MP3_WAV播放器\文档\软件资料\STUDY_NOTE.md` |
| 看硬件学习手册 | `STM32_MP3_WAV播放器\硬件设计\STM32_MP3硬件设计学习手册.docx` |
| 运行串口回读校验 | `工具\串口验证\compare.py` |

## GitHub 上传范围

会上传：

- Keil 工程文件；
- `User`、`Hardware`、`Library`、`Start`、`System` 源码；
- 项目文档和硬件设计资料；
- 串口验证 Python 脚本；
- 两个阶段性学习工程。

不会上传：

- `.venv`、`__pycache__`；
- Keil `Objects`、`Listings`、`.hex`、`.axf`、`.map` 等构建产物；
- 本机日志、Flash 回读 WAV、原始音频素材；
- 外部串口助手 exe；
- 历史本机归档备份。
