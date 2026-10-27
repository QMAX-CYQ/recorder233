#ifndef USER_ADD_STATUS_LOG_H
#define USER_ADD_STATUS_LOG_H
#include "log.h"

#ifndef STATUS_LOG_STARTUP_GRACE_MS
#define STATUS_LOG_STARTUP_GRACE_MS 5000U
#endif
#ifndef STATUS_LOG_WARN_PERIOD_MS
#define STATUS_LOG_WARN_PERIOD_MS 1000U
#endif

/* One owning status task. Initialize once at that task's startup. */
void StatusLog_Init(uint32_t startup_tick);
/* Times are milliseconds: now is RTOS time; now_hal/last_rx_hal are HAL time.
 * active=0 ignores an unregistered/unused module; online!=0 means healthy.
 * This observes existing decisions; it does not implement timeout detection. */
void StatusLog_Observe(log_module_t module, uint8_t active, uint8_t online,
                       uint32_t last_rx_hal, uint32_t can_index,
                       uint32_t can_id, uint32_t now, uint32_t now_hal);
extern volatile uint32_t g_status_log_recovery_coalesced;
extern volatile uint32_t g_status_log_invalid_module;
#endif
