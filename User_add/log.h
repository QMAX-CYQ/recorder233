#ifndef __DEBUG_H
#define __DEBUG_H

#include "string.h"
#include "stdarg.h"  // 包含标准可变参数宏的头文件
#include "stdint.h"

enum 
{
    LOG_ERROR_LEVEL = 0,
    LOG_WARN_LEVEL,
    LOG_INFO_LEVEL,
    LOG_PRINTF_LEVEL,
    LOG_LEVEL_NUM
};

/* Debugger-only counters; no extra log traffic. Counts reset on MCU reset. */
typedef struct {
    uint32_t attempts, accepted, busy, failed;
    uint32_t attempted_bytes, accepted_bytes;
    uint32_t format_error, format_truncated;
    uint32_t last_status, last_failure_tick;
    uint32_t failure_streak, max_failure_streak;
    uint32_t failure_history_next;
    uint32_t failure_tick[8], failure_status[8];
    uint32_t last_accepted_tick, max_accepted_gap_ms;
    uint32_t last_line_bytes, max_line_bytes;
} log_tx_diag_t;
extern volatile log_tx_diag_t g_log_tx_diag;

/* 768 字节缓冲覆盖压力测试行的最坏长度，包含字符串结束符。 */
#define LOG_FORMAT_BUFFER_SIZE 768U

void sys_printf(uint8_t level, const char *tag, const char *fmt, ...);

#define DEBUG_ERROR(fmt,...)   sys_printf(LOG_ERROR_LEVEL,"ERROR: ", fmt, ##__VA_ARGS__);
#define DEBUG_WARN(fmt,...)    sys_printf(LOG_WARN_LEVEL,"WARN: ", fmt, ##__VA_ARGS__);
#define DEBUG_INFO(fmt,...)    sys_printf(LOG_INFO_LEVEL,"INFO: ", fmt, ##__VA_ARGS__);
#define DEBUG_PRINTF(fmt,...)   sys_printf(LOG_PRINTF_LEVEL,"PRINTF: ", fmt, ##__VA_ARGS__);
#define LEVEL_SET LOG_PRINTF_LEVEL




#endif

