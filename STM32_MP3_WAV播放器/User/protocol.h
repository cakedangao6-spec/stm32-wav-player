/**
 * @file    protocol.h
 * @brief   传输协议层
 */

#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include <stdint.h>

#define PROTOCOL_LINE_MAX   256U

typedef enum {
    PROTO_CMD_NONE = 0,     // 尚未解析出完整命令
    PROTO_CMD_PING,         // PING 连接测试命令
    PROTO_CMD_WRITE,        // WRITE:size 写文件命令
    PROTO_CMD_VERIFY,       // VERIFY 校验命令
    PROTO_CMD_UNKNOWN       // 未识别命令
} ProtocolCmd_t;

typedef struct {
    ProtocolCmd_t cmd;      // 解析出的命令类型
    uint32_t      param;    // 命令参数，例如 WRITE 后面的文件大小
} ProtocolParseResult_t;

ProtocolParseResult_t Protocol_FeedByte(uint8_t b);
void     Protocol_Reset(void);
uint16_t Protocol_BuildResponse(char *buf, const char *format, ...);

#endif
