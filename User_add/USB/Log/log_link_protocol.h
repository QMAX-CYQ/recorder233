#ifndef LOG_LINK_PROTOCOL_H
#define LOG_LINK_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define LOG_LINK_FRAME_SIZE 16U
#define LOG_LINK_READY 1U
#define LOG_LINK_ACK 2U
typedef struct { uint8_t bytes[16]; uint8_t used; uint32_t discarded; } log_link_parser_t;
typedef void (*log_link_frame_callback_t)(uint8_t type, uint32_t session);

static inline uint32_t LogLinkRead32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline void LogLinkWrite32(uint8_t *p, uint32_t value)
{
    p[0]=(uint8_t)value; p[1]=(uint8_t)(value>>8); p[2]=(uint8_t)(value>>16); p[3]=(uint8_t)(value>>24);
}
/* 控制帧：RMLG、类型、版本、长度、会话号、FNV-1a 校验值。 */
static inline uint32_t LogLinkChecksum(const uint8_t *p)
{
    uint32_t value=2166136261UL; uint32_t i;
    for(i=0;i<12U;i++) value=(value^p[i])*16777619UL;
    return value;
}
static inline void LogLinkBuild(uint8_t *p, uint8_t type, uint32_t session)
{
    memcpy(p,"RMLG",4); p[4]=type; p[5]=1U; p[6]=16U; p[7]=0U;
    LogLinkWrite32(p+8,session); LogLinkWrite32(p+12,LogLinkChecksum(p));
}
/* 控制帧可能跨包或与业务数据合包；只消费完整且校验正确的帧。
 * 非控制数据按原顺序输出，最多额外释放上一包暂存的 15 字节。
 * 调用者必须提供 input_len+15 字节空间。allow 为 0 时丢弃握手前数据。 */
static inline size_t LogLinkFilter(log_link_parser_t *state, const uint8_t *input, size_t len,
    uint8_t *output, volatile uint8_t *allow, log_link_frame_callback_t callback)
{
    size_t i, out=0; static const uint8_t magic[4]={'R','M','L','G'};
    for(i=0;i<len;i++) {
        state->bytes[state->used++]=input[i];
        while(state->used) {
            size_t prefix=state->used<4U ? state->used : 4U;
            if(memcmp(state->bytes,magic,prefix)!=0 ||
               (state->used>=8U && (state->bytes[5]!=1U || state->bytes[6]!=16U || state->bytes[7]!=0U ||
                (state->bytes[4]!=LOG_LINK_READY && state->bytes[4]!=LOG_LINK_ACK))) ||
               (state->used==16U && LogLinkRead32(state->bytes+12)!=LogLinkChecksum(state->bytes))) {
                if(allow==NULL || *allow) output[out++]=state->bytes[0];
                else state->discarded++;
                state->used--; memmove(state->bytes,state->bytes+1,state->used);
                continue;
            }
            if(state->used==16U) {
                callback(state->bytes[4],LogLinkRead32(state->bytes+8)); state->used=0U;
            }
            break;
        }
    }
    return out;
}
#endif
