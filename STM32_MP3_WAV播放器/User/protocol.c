/**
 * @file    protocol.c
 * @brief   传输协议层，将串口文本命令解析为应用命令
 */

#include "protocol.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define PARSE_STATE_IDLE    0
#define PARSE_STATE_RCV     1

static uint8_t  parse_state = PARSE_STATE_IDLE;
static char     line_buf[PROTOCOL_LINE_MAX];
static uint16_t line_len = 0;

/**
  * 函    数：判断字符串是否以指定前缀开头（忽略大小写）
  * 参    数：str    被检查的字符串
  * 参    数：prefix 期望的前缀
  * 返 回 值：1 表示匹配，0 表示不匹配
  */
static uint8_t StrStartsWith(const char *str, const char *prefix)
{
    while (*prefix) {
        char c1 = *str++;
        char c2 = *prefix++;
        if (c1 >= 'A' && c1 <= 'Z') c1 += ('a' - 'A');
        if (c2 >= 'A' && c2 <= 'Z') c2 += ('a' - 'A');
        if (c1 != c2) return 0;
    }
    return 1;
}

/**
  * 函    数：向协议解析器喂入 1 个字节
  * 参    数：b 串口收到的字节
  * 返 回 值：解析结果，未凑齐一行命令时返回 PROTO_CMD_NONE
  * 说    明：命令以 '\n' 作为结束标志，例如 PING、WRITE:123、VERIFY
  */
ProtocolParseResult_t Protocol_FeedByte(uint8_t b)
{
    ProtocolParseResult_t result = { PROTO_CMD_NONE, 0 };

    if (parse_state == PARSE_STATE_IDLE) {
        if (b == '\r' || b == '\n' || b == ' ' || b == '\t') {
            return result;                          // 空白字符不作为命令开始
        }
        parse_state = PARSE_STATE_RCV;              // 收到普通字符，进入接收一行命令的状态
        line_len = 0;                               // 新命令从 line_buf[0] 开始存放
    }

    if (parse_state == PARSE_STATE_RCV) {
        if (b == '\r') {
            return result;                          // 忽略 '\r'，等待 '\n' 作为真正结束
        }
        if (b == '\n') {
            parse_state = PARSE_STATE_IDLE;         // 一行命令接收完成，回到空闲状态
            line_buf[line_len] = '\0';              // 添加字符串结束符，便于后面比较
            if (line_len == 0) return result;

            if (StrStartsWith(line_buf, "PING")) {
                result.cmd = PROTO_CMD_PING;        // 上位机测试连接
            }
            else if (StrStartsWith(line_buf, "WRITE:")) {
                result.cmd = PROTO_CMD_WRITE;       // 上位机准备发送文件，冒号后面是文件大小
                const char *param = line_buf + 6;
                result.param = 0;
                while (*param >= '0' && *param <= '9') {
                    result.param = result.param * 10 + (*param - '0'); // 将十进制字符转换为整数
                    param++;
                }
            }
            else if (StrStartsWith(line_buf, "VERIFY")) {
                result.cmd = PROTO_CMD_VERIFY;      // 请求 STM32 计算 Flash 中数据的 CRC32
            }
            else {
                result.cmd = PROTO_CMD_UNKNOWN;     // 未识别命令
            }
            return result;
        }

        if (line_len < PROTOCOL_LINE_MAX - 1) {
            line_buf[line_len++] = (char)b;         // 命令还没结束，继续保存字符
        }
    }

    return result;
}

/**
  * 函    数：复位协议解析器
  * 参    数：无
  * 返 回 值：无
  */
void Protocol_Reset(void)
{
    parse_state = PARSE_STATE_IDLE;                 // 回到等待新命令状态
    line_len = 0;                                   // 丢弃已接收但未完成的命令
}

/**
  * 函    数：构造协议响应字符串
  * 参    数：buf    输出缓冲区
  * 参    数：format printf 风格格式字符串
  * 返 回 值：响应字符串长度
  * 说    明：自动在末尾添加 "\r\n"，便于上位机按行读取
  */
uint16_t Protocol_BuildResponse(char *buf, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buf, PROTOCOL_LINE_MAX - 2, format, args);
    va_end(args);
    if (len < 0) len = 0;
    if ((uint16_t)len > PROTOCOL_LINE_MAX - 3) len = PROTOCOL_LINE_MAX - 3;
    buf[len]     = '\r';                             // 添加回车
    buf[len + 1] = '\n';                             // 添加换行
    buf[len + 2] = '\0';                             // 添加字符串结束符
    return (uint16_t)(len + 2);
}
