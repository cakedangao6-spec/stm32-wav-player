# STM32 USART接收与MP3下载学习日志

> 本文件主要用于学习者复盘。后续协作时，Codex 应优先读取 `学习步骤.md`、当前代码和必要头文件；只有需要追溯历史原因时，再读取本日志的相关小节，避免每次读取过多无关内容。

## 学习目标

在理解基础串口发送/接收程序的基础上，逐步实现一个稳定的文件下载链路。当前重点是先把 USART 接收、环形缓冲区、按行命令解析做扎实，再逐步进入 DMA、协议状态机和 Flash 写入。

## 阶段规划

### 第一阶段：基础 USART 接收与环形缓冲区

- 使用 USART1 RXNE 中断接收单字节数据。
- 中断中只负责读取串口数据并写入环形缓冲区。
- 主循环从环形缓冲区读取数据并处理。
- 实现读写指针回绕，避免数组越界。
- 实现缓冲区满判断，避免覆盖未读取数据。
- 记录缓冲区溢出次数，便于后续调试。
- 封装 `RB_Write`、`RB_Read`、`RB_IsEmpty`、`RB_GetCount`。
- 支持按行接收字符串，遇到 `\r` 或 `\n` 认为一行结束。
- 实现基础命令 `PING -> PONG`。

### 第二阶段：轻量协议解析

- 在 `App_HandleLine()` 中继续扩展命令。
- 计划命令：
  - `PING` -> `PONG`
  - `HELP` -> 输出支持的命令
  - `WRITE:size` -> 解析数据长度，回复 `READY`
  - `VERIFY` -> 后续校验接收结果
- 目标是先把命令解析做清楚，不急着进入 DMA 和 Flash。

### 第三阶段：DMA 循环接收

- 配置 DMA 循环模式接收 USART 数据。
- 理解 DMA 写指针和 APP 读指针的关系。
- 保持生产者和消费者分离。
- 用 DMA 替代 RXNE 中断逐字节搬运。

### 第四阶段：Flash 写入

- 引入 W25Q64。
- 按页写入接收到的数据。
- 结合缓冲区和协议状态机，保证接收和写入不冲突。
- 最终目标是完成 MP3/WAV 文件下载链路。

## 今日学习日志：2026-06-05

### 今日完成

- 配置 VS Code 工程环境：
  - 添加 `.vscode/c_cpp_properties.json`
  - 添加 `.vscode/tasks.json`
  - 添加 `.vscode/settings.json`
  - 安装 C/C++ 扩展
  - 支持在 VS Code 中调用 Keil 编译
- 完成环形缓冲区模块 `RB`：
  - `rx_buffer`
  - `rx_write_index`
  - `rx_read_index`
  - `rb_overflow_count`
- 将 `rx_buffer`、`rx_write_index`、`rx_read_index` 改为 `static`，隐藏模块内部变量。
- 保留 `rb_overflow_count` 为外部可见变量，便于 Keil Watch 调试。
- 实现并验证：
  - `RB_Write(uint8_t data)`
  - `RB_Read(uint8_t *data)`
  - `RB_IsEmpty(void)`
  - `RB_GetCount(void)`
- USART1 中断中调用 `RB_Write(data)` 写入环形缓冲区。
- 主循环中调用 `RB_Read(&data)` 读取数据。
- OLED 显示：
  - 最近接收字节
  - 当前缓冲区可读数量
  - 缓冲区溢出计数
  - 最近接收的一整行字符串
- 理解并验证普通发送和 16 进制发送的区别：
  - 普通发送 `A` 实际发送 `0x41`
  - 普通发送 `0` 实际发送 `0x30`
  - 16 进制发送 `00` 实际发送 `0x00`
  - 16 进制发送 `44` 实际发送 `0x44`
- 实现按行接收：
  - 普通字符进入 `line_buffer`
  - 遇到 `\r` 或 `\n` 结束一行
  - 自动补 `'\0'` 形成 C 字符串
- 实现轻量命令处理函数：
  - `App_HandleLine(char *line)`
  - `PING` 回复 `PONG`
  - 未识别命令回复 `UNKNOWN`
  - 函数注释补充了当前具体功能：识别 `PING`，其他命令返回 `UNKNOWN`

### 今日验证结果

- Keil 编译通过：

```text
0 Error(s), 0 Warning(s)
```

## 今日学习日志：2026-06-08

### 第四阶段：DMA 循环接收

- 本阶段目标是用 DMA 替代 USART1 RXNE 中断里的逐字节搬运。
- 已完成的接收链路调整：
  - 定义 `serial_dma_rx_buffer[64]` 作为 DMA 循环接收缓冲区。
  - 定义 `serial_dma_rx_scan_index`，记录软件已经扫描到 DMA 缓冲区的哪个位置。
  - 配置 `DMA1_Channel5`：
    - 源地址：`USART1->DR`
    - 目标地址：`serial_dma_rx_buffer`
    - 方向：外设到内存
    - 模式：循环模式
    - 数据宽度：字节
  - 开启 `USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE)`，允许 USART1 收到数据时请求 DMA 搬运。
  - 在主循环中调用 `Serial_ProcessDMARx()`，扫描 DMA 缓冲区里的新字节，再写入原来的 `RB` 环形缓冲区。
  - 关闭 USART1 RXNE 接收中断，让 DMA 正式接管接收搬运。

### 当前关键理解

- DMA 配置阶段只是告诉 DMA“从哪里搬、搬到哪里、怎么循环搬”，在开启 USART DMA 接收请求前，DMA 还不会参与 USART 接收。
- `USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE)` 打开后，USART1 收到字节会请求 DMA 从 `USART1->DR` 搬到内存数组。
- `DMA_GetCurrDataCounter(DMA1_Channel5)` 读到的是 DMA 当前剩余传输数量，不是写入下标。
- DMA 当前写入位置可用 `缓冲区大小 - DMA剩余数量` 计算。
- `serial_dma_rx_scan_index` 是软件处理位置；只要它没追上 DMA 当前写入位置，就说明中间有新字节需要转存到 `RB`。
- 关闭 RXNE 中断后，`USART1_IRQHandler()` 正常不会再处理接收字节；它保留在代码里只是用于学习对照。
- 当前 APP 层逻辑没有重写，仍然是：
  - `RB_Read(&data)`
  - 命令模式按行解析
  - 数据模式统计 `WRITE:size` 后的数据字节数

### 推荐串口测试流程

1. 烧录程序后，串口助手使用 9600 波特率。
2. 普通文本发送，并勾选“发送新行”。
3. 发送：

```text
PING
```

预期：

```text
Line:PING
PONG
```

4. 发送：

```text
WRITE:5
ABCDE
STATUS
```

预期：

```text
Line:WRITE:5
READY
DONE
Line:STATUS
READY=1
SIZE=5
RECEIVED=5
STATE=IDLE
```

### 阶段验证状态

- Keil 编译通过。
- 板上串口测试待验证。

## 今日学习日志：2026-06-07

### 第三阶段：最小数据接收模式

- 本阶段目标是不进入 DMA、不写 W25Q64，只让 `WRITE:size` 后面的原始数据字节进入最小接收流程。
- 按学习步骤一小步一小步完成：
  - 定义 `App_State_t`，区分 `APP_STATE_IDLE` 和 `APP_STATE_RECEIVING`。
  - 增加 `app_received_size`，用于记录当前已经收到的数据字节数。
  - `WRITE:size` 解析成功后，清零 `app_received_size`，进入 `APP_STATE_RECEIVING`，并回复 `READY`。
  - `main.c` 主循环先从 `RB_Read(&data)` 取出一个字节，再根据 APP 状态决定处理方式。
  - 命令模式继续按行接收，数据模式调用 `App_HandleDataByte(data)`。
  - `App_HandleDataByte()` 每收到一个数据字节就让 `app_received_size++`。
  - 当 `app_received_size >= app_write_size` 时，回复 `DONE`，并回到 `APP_STATE_IDLE`。
  - `STATUS` 扩展输出 `RECEIVED` 和 `STATE`，方便观察内部状态。

### 当前关键理解

- `RB_Read(&data)` 每次只从环形缓冲区取出 1 个字节，`data` 只是单字节中转变量。
- APP 状态决定“这个字节怎么解释”：
  - `APP_STATE_IDLE`：当作命令字符，进入 `line_buffer`。
  - `APP_STATE_RECEIVING`：当作数据字节，不再进入 `line_buffer`。
- `line_buffer[line_index] = '\0'` 是手动补 C 字符串结束符，串口本身通常不会发送 `'\0'`。
- 串口助手发送新行时常见实际字节是 `\r\n`，也就是 `0x0D 0x0A`。
- 如果 `WRITE:5\r\n` 在 `\r` 处已经进入数据模式，紧跟的 `\n` 会被误算成第一个数据字节。
- 通过 Keil Watch 观察到 `data='A'` 时 `app_received_size=2`，确认 `\n` 已经被算作第一个数据字节。
- 修复方式是在 `main.c` 中增加 `ignore_next_lf` 标志：
  - 当 `WRITE:size` 由 `\r` 结束并进入数据模式后，设置 `ignore_next_lf = 1`。
  - 下一轮如果读到 `\n`，只清除标志，不调用 `App_HandleDataByte(data)`。
  - 这样 `\n` 被读出但不计数，后续 `A` 才会成为第一个数据字节。

### 阶段验证结果

- Keil 编译通过：

```text
0 Error(s), 0 Warning(s)
```

- 串口助手验证流程：

```text
WRITE:5
ABCDE
STATUS
```

- 验证结果：

```text
Line:WRITE:5
READY
DONE
Line:STATUS
READY=1
SIZE=5
RECEIVED=5
STATE=IDLE
```

### 阶段结论

第三阶段已经完成。当前工程已经能在不使用 DMA、不写 W25Q64 的情况下，实现 `WRITE:size` 后的最小数据接收模式：只统计数据字节数，收够指定大小后返回 `DONE`，再回到命令模式。

- 串口助手验证通过：
  - 发送 `PING` 并勾选发送新行
  - 收到：

```text
Line:PING
PONG
```

### 今日关键理解

- `uint8_t` 适合表示一个字节的数据，例如 USART 接收到的字节。
- `uint16_t` 适合表示下标、长度和计数，例如环形缓冲区读写指针。
- 环形缓冲区用一个空位区分空和满：
  - 空：`rx_write_index == rx_read_index`
  - 满：`(rx_write_index + 1) % RING_BUFFER_SIZE == rx_read_index`
- 中断和主循环分工：
  - 中断只写入缓冲区
  - 主循环只读取缓冲区并处理业务
- `data == '\n' || data == '\r'` 表示收到换行或回车任意一个就结束一行，不能写成 `&&`。

## 当前阶段结论

第一阶段已经完成，并且已经开始第二阶段的轻量协议解析。

当前工程已经具备：

- 基础 USART 接收能力
- 环形缓冲区模块
- 简单命令行接收能力
- `PING -> PONG` 协议雏形

## 下次继续

建议下一步继续扩展轻量协议：

1. 增加 `HELP` 命令，返回当前支持的命令列表。
2. 再增加 `WRITE:size` 命令，只解析 size，不接收文件数据。
3. 等命令解析稳定后，再考虑进入 DMA 或 W25Q64。

## 今日学习日志：2026-06-07

### 第二阶段：轻量协议解析起步

- 在 `App_HandleLine()` 中继续扩展命令解析，保持第一阶段的按行接收结构不变。
- 在 `main.c` 顶部加入第二阶段进度表，方便学习时随时确认当前学到哪一步。
- 增加 `HELP` 命令：
  - 返回当前支持的命令列表。
  - 作用是让串口助手端可以快速确认协议入口。
- 增加 `WRITE:size` 命令：
  - 先通过 `WRITE:` 前缀判断是否为写入命令。
  - 再逐字符解析冒号后的十进制 size。
  - 解析成功后保存到 `app_write_size`。
  - 设置 `app_write_ready = 1`，表示已经收到有效的 `WRITE:size`。
  - 当前只回复 `READY`，不接收文件数据。
- 增加 `VERIFY` 命令：
  - 如果还没有收到有效的 `WRITE:size`，返回 `NO WRITE`。
  - 如果已经收到有效的 `WRITE:size`，返回 `OK`。
  - 当前只是检查协议准备状态，后续接入真实文件校验后，再替换为校验结果。
- 增加 `STATUS` 命令：
  - 输出 `READY=0/1`。
  - 输出当前保存的 `SIZE`。
  - 作用是从串口助手直接观察单片机内部协议状态。

### 当前关键理解

- `WRITE:size` 比 `WRITE` 多了参数，所以不能只用 `strcmp()` 判断完整字符串。
- 解析参数时先判断固定前缀，再处理后面的参数字段，结构会更清楚。
- size 字段必须检查每个字符是不是 `'0'` 到 `'9'`，否则 `WRITE:abc` 这类错误命令也可能被误处理。
- `write_size` 是函数内部的局部变量，只在 `App_HandleLine()` 本次执行期间有效。
- `app_write_size` 是全局变量，用来保存最近一次有效 `WRITE:size` 中的 size，后续接收文件数据时还可以继续使用。
- `app_write_ready` 是一个简单状态标志，表示当前是否已经进入“准备写入”的协议状态。
- `VERIFY` 和 `STATUS` 的区别：
  - `VERIFY` 用来判断当前状态是否满足验证条件。
  - `STATUS` 用来观察内部变量，方便学习和调试。
- `Serial_Printf("%lu", app_write_size)` 用于把 `uint32_t` 数字转换成十进制文本并通过串口输出。
- 本阶段只做命令解析，不进入 DMA、W25Q64 或完整状态机。

### 建议串口测试流程

```text
HELP
STATUS
VERIFY
WRITE:1024
STATUS
VERIFY
WRITE:
WRITE:abc
ABC
```

### 预期现象

- `HELP` 返回当前支持的命令列表。
- 上电后直接发送 `STATUS`，应看到 `READY=0`、`SIZE=0`。
- 上电后直接发送 `VERIFY`，应返回 `NO WRITE`。
- 发送 `WRITE:1024`，应返回 `READY`。
- 再发送 `STATUS`，应看到 `READY=1`、`SIZE=1024`。
- 再发送 `VERIFY`，应返回 `OK`。
- `WRITE:`、`WRITE:abc`、`ABC` 都应返回 `UNKNOWN`。

### 今日验证结果

- Keil 编译通过：

```text
0 Error(s), 0 Warning(s)
``````

---

## 日志：第五阶段调试总结 — 2026-06-15

### 验证结果

- WRITE:size → 擦除扇区 → 接收数据 → DONE → READBACK 读回验证
- 256 字节测试 ✅ 满页写入、SPI 传输正常
- 300 字节测试 ✅ 跨页拼接正确（256+44），flush 剩余数据正常
- 回传数据与发送数据完全一致，无错位、无丢包

### 核心概念澄清

> **一个字节就是一个可以用两位十六进制数表示的数值。**

- 1个字节 = 8位二进制 = 取值范围 0~255
- 2位十六进制数 = 1个字节
- 1个ASCII字符（如 A、B、0、换行等）= 1个字节

### 测试数据设计方法

采用**递增循环序列**（00 01 02 ... FD FE FF），便于快速定位错位、丢包等问题：
- 先测一页（256字节），再测跨页（300字节）
- 跨页验证关注 FF 后是否正确跟 00

### 经验总结

1. **调试数据必须看十六进制** — 文本视图会被帧头帧尾干扰，十六进制能准确定位有效数据区间。
2. **规律数据是最佳调试工具** — 递增序列能一眼发现错位、丢包、重复。
3. **先测一页，再测跨页** — 分步验证能快速定位问题来源。
4. **帧格式要解析清楚** — 提取数据时需要跳过帧头（DATA:\r\n），截取到帧尾（\r\nREADBACK）之前。
5. **一个字节 = 两位十六进制数** — 这是嵌入式调试的底层基础。

### 速查表

| 概念 | 表示方式 | 示例 |
|------|----------|------|
| 1字节（十进制0~255） | 2位十六进制 | 00 ~ FF |
| 1位十六进制数 | 4位二进制 | F = 1111 |
| 1字节 | 8位二进制 | 01000001 |
| 字符 A | 十六进制 41 | ASCII 65 |
| 回车换行 | 0D 0A | CR+LF |

---

## 项目学习总结 — 2026-06-15

### 学习路径回顾

从零开始，分五个阶段循序渐进，每个阶段只解决一个核心问题：

**第一阶段：USART 接收与环形缓冲区**
- USART1 RXNE 中断接收单字节
- 中断中写入环形缓冲区（RB），主循环读取
- 读写指针回绕、缓冲区满判断、溢出计数
- 按行接收字符串，遇到 `\r` 或 `\n` 结束一行
- 基础命令 `PING` → `PONG`

**第二阶段：轻量协议解析**
- `HELP`、`WRITE:size`、`VERIFY`、`STATUS` 命令
- 解析 `WRITE:size` 参数（前缀判断 + 十进制数值转换）
- 协议状态保持（`app_write_ready`、`app_write_size`）
- 封装 APP 模块，`main.c` 只负责按行接收，业务逻辑分离

**第三阶段：最小数据接收模式**
- 区分空闲状态和数据接收状态（`APP_STATE_IDLE` / `APP_STATE_RECEIVING`）
- `WRITE:size` 进入数据模式，按字节计数
- 解决 CRLF 中 `\n` 被误算数据的问题（`ignore_next_lf` 标志）

**第四阶段：DMA 循环接收**
- DMA1_Channel5 替代 RXNE 中断逐字节搬运
- 理解 DMA 写指针与软件读指针的关系
- DMA 循环模式实现自动回绕
- 关闭 RXNE 中断，DMA 正式接管接收

**第五阶段：W25Q64 Flash 写入**
- W25Q64 驱动 + READID 验证硬件
- 页缓冲区设计：每256字节自动写入 Flash
- 写入前必须先擦除扇区（NOR Flash 特性）
- 收满数据后 flush 剩余数据
- READBACK 读回验证，确认数据完整性

### 关键理解

| 概念 | 理解 |
|------|------|
| **串口接收方式对比** | RXNE 中断（逐字节 CPU 介入）→ DMA（硬件自动搬运） |
| **环形缓冲区** | 生产者和消费者分离，用一个空位区分空/满 |
| **数据 vs 命令** | APP 状态决定字节解释方式（字符进入 line_buffer / 字节进入页缓冲区） |
| **字节的本质** | 一个字节 = 两位十六进制数 = 8位二进制，范围 0~255 |
| **NOR Flash 特性** | 只能 1→0，写入前必须擦除恢复成全 1（0xFF）|
| **扇区 vs 页** | 擦除最小单位是扇区（4KB），写入最小单位是页（256字节）|
| **分步验证** | 先测一页（256B），再测跨页（300B），逐步增加复杂度 |

### 工程结构

```
User/main.c           — 主循环：DMA→RB→APP 数据流水线
Hardware/Serial.c/.h  — 串口初始化、DMA配置、发送函数
Hardware/RB.c/.h      — 环形缓冲区模块
Hardware/App.c/.h     — 应用层：命令解析、数据接收、Flash写入
Hardware/W25Q64.c/.h  — W25Q64 Flash 驱动
Hardware/MySPI.c/.h   — SPI 底层驱动
```

### 最终协议命令

```
PING              → PONG
HELP              → 命令列表
WRITE:size        → ERASING → ERASE OK → READY → (数据) → DONE
READBACK          → DATA:... → READBACK OK
VERIFY            → OK / NO WRITE
STATUS            → READY= SIZE= RECEIVED= STATE=
READID            → MID= DID=
```

### 测试验证结果

- 256 字节满页写入 + 读回 ✅
- 300 字节跨页写入 + 读回 ✅
- 递增序列验证无错位、无丢包 ✅

---

*学习至此告一段落。从串口中断接收一个字节开始，到完整实现串口 DMA 接收 + Flash 写入 + 读回验证，建立了嵌入式数据链路的完整认知。*
