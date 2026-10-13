#include "log.h"
#include "stdio.h"
#include "FreeRTOS.h"
#include "usbd_cdc_if.h"
#include "usb_device.h"
#include "cmsis_os.h"

volatile log_tx_diag_t g_log_tx_diag;

void sys_printf(uint8_t level, const char *tag, const char *fmt, ...)
{
    if (level >= LOG_LEVEL_NUM || level >= LEVEL_SET || fmt == NULL)
        return;
    size_t len;
    char buf[256 + 32];
    int off = 0;
    int format_len;
    uint8_t tx_status;
    uint32_t tx_tick;
    const char *log_tag = (tag != NULL) ? tag : "";

	off = snprintf(buf, sizeof(buf), "%s",log_tag);
    if (off < 0 || off >= (int)sizeof(buf))
        return;
    
    va_list args;
    va_start(args, fmt);
    format_len = vsnprintf(buf + off, sizeof(buf) - off, fmt, args);
    va_end(args);
	len = strlen(buf);
	
    /* Observe the original single send attempt; do not retry or change payload. */
    if (format_len < 0) g_log_tx_diag.format_error++;
    else if ((size_t)format_len >= sizeof(buf) - (size_t)off)
        g_log_tx_diag.format_truncated++;
    tx_tick = osKernelSysTick();
    g_log_tx_diag.attempts++;
    g_log_tx_diag.attempted_bytes += (uint32_t)len;
    tx_status = CDC_Transmit_HS((uint8_t*)buf, len);
    g_log_tx_diag.last_status = tx_status;
    if (tx_status == USBD_OK) {
        uint32_t gap = tx_tick - g_log_tx_diag.last_accepted_tick;
        if ((g_log_tx_diag.accepted != 0U) && (gap > g_log_tx_diag.max_accepted_gap_ms))
            g_log_tx_diag.max_accepted_gap_ms = gap;
        g_log_tx_diag.accepted++;
        g_log_tx_diag.accepted_bytes += (uint32_t)len;
        g_log_tx_diag.last_accepted_tick = tx_tick;
        g_log_tx_diag.failure_streak = 0U;
    } else {
        if (tx_status == USBD_BUSY) g_log_tx_diag.busy++;
        else g_log_tx_diag.failed++;
        uint32_t failure_slot = g_log_tx_diag.failure_history_next % 8U;
        g_log_tx_diag.failure_tick[failure_slot] = tx_tick;
        g_log_tx_diag.failure_status[failure_slot] = tx_status;
        g_log_tx_diag.failure_history_next++;
        g_log_tx_diag.last_failure_tick = tx_tick;
        g_log_tx_diag.failure_streak++;
        if (g_log_tx_diag.failure_streak > g_log_tx_diag.max_failure_streak)
            g_log_tx_diag.max_failure_streak = g_log_tx_diag.failure_streak;
        osDelay(1);
    }
}




