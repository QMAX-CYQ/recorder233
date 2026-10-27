/* H723 日志与 SPI DMA 发送适配；迁移到其他平台时替换本文件实现。 */
#include "fault_log_port.h"
#include "log.h"
#include "spi_log.h"
#include "cmsis_os.h"
#include <stdarg.h>
#include <stdio.h>

uint8_t FaultLog_PortLinkReady(void)
{
    return g_spi_log_link_ready;
}

uint32_t FaultLog_PortSession(void)
{
    return g_spi_log_link_diag.session;
}

uint32_t FaultLog_PortDisconnectCount(void)
{
    return g_spi_tx_diag.link_reset_count;
}

uint8_t FaultLog_PortTxIdle(void)
{
    /* 队列为空并不代表最后一块已发送完成，须同时检查 DMA 在途状态。 */
    return spi_tx_pending_count == 0U && spi_tx_inflight == 0U;
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
    /* sys_printf 会在同等大小的缓冲区中添加标签，长度不足时拒绝发送，
     * 避免接受被截断的 ERROR 行；sizeof 包含末尾字符串结束符。 */
    if (length < 0 || (size_t)length + sizeof("ERROR: ") > sizeof(line))
        return FAULT_LOG_PORT_FAIL;
    result = sys_printf(LOG_ERROR_LEVEL, "ERROR: ", "%s", line);
    if (result == LOG_SPI_OK) return FAULT_LOG_PORT_OK;
    if (result == LOG_SPI_BUSY) return FAULT_LOG_PORT_BUSY;
    return FAULT_LOG_PORT_FAIL;
}
