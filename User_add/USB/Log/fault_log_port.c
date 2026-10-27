/* Adapter for the current H723 firmware. Replace this file's implementation
 * when integrating with a different logger/USB transport. */
#include "fault_log_port.h"
#include "log.h"
#include "usbd_cdc_if.h"
#include "cmsis_os.h"
#include <stdarg.h>
#include <stdio.h>

uint8_t FaultLog_PortLinkReady(void)
{
    return g_log_link_ready;
}

uint32_t FaultLog_PortSession(void)
{
    return g_log_link_diag.session;
}

uint32_t FaultLog_PortDisconnectCount(void)
{
    return g_cdc_tx_diag.deinit_count;
}

uint8_t FaultLog_PortTxIdle(void)
{
    /* Pending == 0 alone does not mean the final packet has completed. */
    return cdc_tx_pending_count == 0U && cdc_tx_inflight == 0U;
}

uint32_t FaultLog_PortTick(void)
{
    return osKernelSysTick();
}

uint8_t FaultLog_PortError(const char *format, ...)
{
    char line[LOG_FORMAT_BUFFER_SIZE];
    int length;
    uint8_t result;
    va_list args;
    va_start(args, format);
    length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    /* sys_printf adds the tag in a buffer of the same size. Reject instead
     * of accepting a truncated ERROR line. sizeof includes the final NUL. */
    if (length < 0 || (size_t)length + sizeof("ERROR: ") > sizeof(line))
        return FAULT_LOG_PORT_FAIL;
    result = sys_printf(LOG_ERROR_LEVEL, "ERROR: ", "%s", line);
    if (result == USBD_OK) return FAULT_LOG_PORT_OK;
    if (result == USBD_BUSY) return FAULT_LOG_PORT_BUSY;
    return FAULT_LOG_PORT_FAIL;
}
