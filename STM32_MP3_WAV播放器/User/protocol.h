/**
 * @file    protocol.h
 * @brief   传输协议层
 */

#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include <stdint.h>

#define PROTOCOL_LINE_MAX   256U

typedef enum {
    PROTO_CMD_NONE = 0,
    PROTO_CMD_PING,
    PROTO_CMD_WRITE,
    PROTO_CMD_VERIFY,
    PROTO_CMD_UNKNOWN
} ProtocolCmd_t;

typedef struct {
    ProtocolCmd_t cmd;
    uint32_t      param;
} ProtocolParseResult_t;

ProtocolParseResult_t Protocol_FeedByte(uint8_t b);
void     Protocol_Reset(void);
uint16_t Protocol_BuildResponse(char *buf, const char *format, ...);

#endif
