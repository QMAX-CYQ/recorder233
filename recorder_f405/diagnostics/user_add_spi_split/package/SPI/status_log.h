#ifndef USER_ADD_STATUS_LOG_H
#define USER_ADD_STATUS_LOG_H
#include "log.h"

#ifndef STATUS_LOG_STARTUP_GRACE_MS
#define STATUS_LOG_STARTUP_GRACE_MS 5000U
#endif
#ifndef STATUS_LOG_WARN_PERIOD_MS
#define STATUS_LOG_WARN_PERIOD_MS 1000U
#endif

/* 仅由一个状态任务管理，在该任务启动时初始化一次。 */
void StatusLog_Init(uint32_t startup_tick);
/* 时间单位为毫秒，now 使用 RTOS 时间，now_hal/last_rx_hal 使用 HAL 时间。
 * active 为 0 时忽略未注册或未使用的模块，online 非 0 表示在线。
 * 本接口只观察已有判定结果，不负责实现超时检测。 */
void StatusLog_Observe(log_module_t module, uint8_t active, uint8_t online,
                       uint32_t last_rx_hal, uint32_t can_index,
                       uint32_t can_id, uint32_t now, uint32_t now_hal);
extern volatile uint32_t g_status_log_recovery_coalesced;
extern volatile uint32_t g_status_log_invalid_module;
#endif
