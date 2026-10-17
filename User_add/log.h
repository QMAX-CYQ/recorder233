#ifndef __LOG_H
#define __LOG_H

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

/* 仅供任务上下文提交；队列复制参数，不保存调用方的临时指针。 */
#define LOG_EVENT_QUEUE_CAPACITY 32U
typedef enum {
    LOG_MODULE_VISION, LOG_MODULE_VTM, LOG_MODULE_GIMBAL_IMU, LOG_MODULE_BOARD,
    LOG_MODULE_FRIC_RIGHT, LOG_MODULE_FRIC_LEFT, LOG_MODULE_DRIVER_0, LOG_MODULE_DRIVER_1,
    LOG_MODULE_YAW, LOG_MODULE_DJI_TRIGGER, LOG_MODULE_JOINT_0, LOG_MODULE_JOINT_1,
    LOG_MODULE_JOINT_2, LOG_MODULE_JOINT_3, LOG_MODULE_DM_TRIGGER, LOG_MODULE_PIT,
    LOG_MODULE_COUNT
} log_module_t;

typedef enum { LOG_EVENT_OFFLINE, LOG_EVENT_RECOVERED } log_event_kind_t;
typedef struct {
    /* 时间戳统一为 RTOS 毫秒；持续时间是事件采样时的值。 */
    uint32_t offline_ms, last_rx_tick, can_index, can_id;
    log_module_t module;
    log_event_kind_t kind;
} log_event_t;

typedef struct {
    uint32_t posted, sent, queue_full, invalid, not_initialized;
    uint32_t pending, high_water, usb_retry;
} log_event_diag_t;
extern volatile log_event_diag_t g_log_event_diag;

/* 初始化在调度器启动前完成；消费函数只能由 log_task 调用。 */
void LogEvent_Init(void);
uint8_t LogEvent_Post(const log_event_t *event);
void LogEvent_Service(void);

/* 768 字节缓冲覆盖压力测试行的最坏长度，包含字符串结束符。 */
#define LOG_FORMAT_BUFFER_SIZE 768U

uint8_t sys_printf(uint8_t level, const char *tag, const char *fmt, ...);

#define DEBUG_ERROR(fmt,...)   sys_printf(LOG_ERROR_LEVEL,"ERROR: ", fmt, ##__VA_ARGS__);
#define DEBUG_WARN(fmt,...)    sys_printf(LOG_WARN_LEVEL,"WARN: ", fmt, ##__VA_ARGS__);
#define DEBUG_INFO(fmt,...)    sys_printf(LOG_INFO_LEVEL,"INFO: ", fmt, ##__VA_ARGS__);
#define DEBUG_PRINTF(fmt,...)   sys_printf(LOG_PRINTF_LEVEL,"PRINTF: ", fmt, ##__VA_ARGS__);
#define LEVEL_SET LOG_PRINTF_LEVEL




#endif

