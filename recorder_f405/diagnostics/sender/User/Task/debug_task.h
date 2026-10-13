#ifndef __DEFBUG_TASK_H
#define __DEFBUG_TASK_H

#include "stdint.h"
typedef struct {
    uint32_t loops, last_tick, max_gap_ms, late_count, catchup_count;
    uint32_t group_count[8];
} debug_task_diag_t;
extern volatile debug_task_diag_t g_debug_task_diag;

void debug_task(void const* argument);

#endif
