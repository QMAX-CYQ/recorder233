#include "status_log.h"
#include <string.h>

/* Extracted from rm_main_gimbal status_task; no application driver dependency. */
typedef struct {
    uint32_t offline_since, last_warn_tick;
    uint8_t offline, warned, recovery_pending;
    log_event_t recovery;
} status_log_state_t;
static status_log_state_t status_log_state[LOG_MODULE_COUNT];
static uint32_t status_log_startup_tick;
static uint8_t status_log_initialized;
volatile uint32_t g_status_log_recovery_coalesced;
volatile uint32_t g_status_log_invalid_module;

void StatusLog_Init(uint32_t startup_tick)
{
    memset(status_log_state, 0, sizeof(status_log_state));
    status_log_startup_tick = startup_tick;
    g_status_log_recovery_coalesced = 0U;
    g_status_log_invalid_module = 0U;
    status_log_initialized = 1U;
}

void StatusLog_Observe(log_module_t module, uint8_t active, uint8_t online,
                               uint32_t last_rx_tick, uint32_t can_index,
                               uint32_t can_id, uint32_t now, uint32_t now_hal)
{
    status_log_state_t *state;
    if ((uint32_t)module >= LOG_MODULE_COUNT) {
        g_status_log_invalid_module++;
        return;
    }
    if (!status_log_initialized ||
        (uint32_t)(now - status_log_startup_tick) < STATUS_LOG_STARTUP_GRACE_MS)
        return;
    state = &status_log_state[module];
    log_event_t event;
    uint8_t retried_recovery = state->recovery_pending;
    if (state->recovery_pending && LogEvent_Post(&state->recovery))
        state->recovery_pending = 0U;
    if (!active) {
        state->offline = 0U;
        state->warned = 0U;
        return;
    }
    event.module = module;
    event.last_rx_tick = (last_rx_tick == 0U ? 0U : now - (uint32_t)(now_hal - last_rx_tick));
    event.can_index = can_index;
    event.can_id = can_id;
    if (online) {
        if (state->offline) {
            event.kind = LOG_EVENT_RECOVERED;
            event.offline_ms = (uint32_t)(now - state->offline_since);
            if (state->recovery_pending)
                g_status_log_recovery_coalesced++;
            /* 长期拥塞时每模块保留最近一次恢复快照，合并有单独统计。 */
            state->recovery = event;
            state->recovery_pending = 1U;
            if (!retried_recovery && LogEvent_Post(&state->recovery))
                state->recovery_pending = 0U;
        }
        state->offline = 0U;
        state->warned = 0U;
        return;
    }
    if (!state->offline) {
        state->offline_since = now;
        state->offline = 1U;
        state->warned = 0U;
    }
    /* 先提交上次恢复，再提交本次离线；阻塞期间不补发历史周期告警。 */
    if (!state->recovery_pending && (!state->warned ||
        (uint32_t)(now - state->last_warn_tick) >= STATUS_LOG_WARN_PERIOD_MS)) {
        event.kind = LOG_EVENT_OFFLINE;
        event.offline_ms = (uint32_t)(now - state->offline_since);
        if (LogEvent_Post(&event)) {
            state->warned = 1U;
            state->last_warn_tick = now;
        }
    }
}

