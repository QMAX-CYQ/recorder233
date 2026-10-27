#ifndef __LOG_TASK_H
#define __LOG_TASK_H

#include <stdint.h>

#define LOG_CHANNEL_COUNT 10U
#define LOG_PERIOD_MS 10U

/* 仅供调试器观察，不额外发送诊断报文。 */
typedef struct {
    uint32_t loops, last_tick, max_gap_ms, late_count, catchup_count;
    uint32_t group_count[LOG_CHANNEL_COUNT];
    uint32_t last_run_ms, max_run_ms, overrun_count;
} log_task_diag_t;
extern volatile log_task_diag_t g_log_task_diag;
void log_task(void const *argument);

#endif
