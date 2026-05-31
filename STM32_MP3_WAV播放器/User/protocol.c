/**
 * @file    protocol.c
 * @brief   传输协议层
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

ProtocolParseResult_t Protocol_FeedByte(uint8_t b)
{
    ProtocolParseResult_t result = { PROTO_CMD_NONE, 0 };

    if (parse_state == PARSE_STATE_IDLE) {
        if (b == '\r' || b == '\n' || b == ' ' || b == '\t') {
            return result;
        }
        parse_state = PARSE_STATE_RCV;
        line_len = 0;
    }

    if (parse_state == PARSE_STATE_RCV) {
        if (b == '\r') {
            return result;
        }
        if (b == '\n') {
            parse_state = PARSE_STATE_IDLE;
            line_buf[line_len] = '\0';
            if (line_len == 0) return result;

            if (StrStartsWith(line_buf, "PING")) {
                result.cmd = PROTO_CMD_PING;
            }
            else if (StrStartsWith(line_buf, "WRITE:")) {
                result.cmd = PROTO_CMD_WRITE;
                const char *param = line_buf + 6;
                result.param = 0;
                while (*param >= '0' && *param <= '9') {
                    result.param = result.param * 10 + (*param - '0');
                    param++;
                }
            }
            else if (StrStartsWith(line_buf, "VERIFY")) {
                result.cmd = PROTO_CMD_VERIFY;
            }
            else {
                result.cmd = PROTO_CMD_UNKNOWN;
            }
            return result;
        }

        if (line_len < PROTOCOL_LINE_MAX - 1) {
            line_buf[line_len++] = (char)b;
        }
    }

    return result;
}

void Protocol_Reset(void)
{
    parse_state = PARSE_STATE_IDLE;
    line_len = 0;
}

uint16_t Protocol_BuildResponse(char *buf, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buf, PROTOCOL_LINE_MAX - 2, format, args);
    va_end(args);
    if (len < 0) len = 0;
    if ((uint16_t)len > PROTOCOL_LINE_MAX - 3) len = PROTOCOL_LINE_MAX - 3;
    buf[len]     = '\r';
    buf[len + 1] = '\n';
    buf[len + 2] = '\0';
    return (uint16_t)(len + 2);
}
