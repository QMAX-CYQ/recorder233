#include <assert.h>
#include <stdio.h>
#include <string.h>
#define LOG_SPI_POLLING_TEST 1U
#include "spi_log_config.h"
/* 仅测试桩替换固定 SRAM 地址，直接验证实际主板源码。 */
#undef LOG_SPI_DMA_BASE
#undef LOG_SPI_DMA_BYTES
#define LOG_SPI_DMA_BASE ((uintptr_t)dma_buffer)
#define LOG_SPI_DMA_BYTES LOG_SPI_MAX_PACKET
#include "D:/RM/wheel_leg_2026/wheel_leg_2025--4/rm_main/User/Log/spi_log.c"
static DMA_HandleTypeDef dma = {{DMA_NORMAL, DMA_PDATAALIGN_BYTE, DMA_MDATAALIGN_BYTE}};
SPI_HandleTypeDef hspi1 = {{SPI_MODE_MASTER, SPI_DIRECTION_2LINES_TXONLY, SPI_DATASIZE_8BIT}, &dma, 0U, HAL_SPI_STATE_READY};
static uint32_t tick, calls;
static int tx_status;
uint32_t HAL_GetTick(void) { return tick; }
uint32_t HAL_SPI_GetState(SPI_HandleTypeDef *h) { return h->State; }
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h, const uint8_t *p, uint16_t n, uint32_t timeout)
{
    static const char expected[] = "SPI_TEST: 0123456789 ABCDEF\r\n";
    assert(h == &hspi1 && n == sizeof(expected) - 1U && timeout == 10U);
    assert(memcmp(p, expected, n) == 0);
    calls++;
    return tx_status;
}
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *h, uint8_t *p, uint16_t n)
{ (void)h; (void)p; (void)n; assert(0); return 1; }
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *h)
{ (void)h; assert(0); return 1; }
void CleanDCacheForDmaTx(const void *p, uint32_t n)
{ (void)p; (void)n; assert(0); }
int main(void)
{
    const uint8_t data[] = "ordinary log";
    tick = 10U;
    SPI_LogService(); assert(calls == 0U);
    SPI_LogInit(); SPI_LogService(); assert(calls == 0U);
    assert(config_valid && g_spi_tx_diag.polling_test_mode == 1U);
    assert(SPI_LogTransmit(data, sizeof(data)) == LOG_SPI_BUSY);
    assert(spi_tx_pending_count == 0U);
    SPI_LogSetReceiverReady(1U); SPI_LogService(); assert(calls == 1U);
    assert(!g_spi_log_link_ready && !spi_tx_inflight);
    tick = 19U; SPI_LogService(); assert(calls == 1U);
    tick = 20U; SPI_LogService(); assert(calls == 2U);
    hspi1.State = 1U; tick = 30U; SPI_LogService(); assert(calls == 2U);
    hspi1.State = HAL_SPI_STATE_READY;
    tx_status = 1; SPI_LogService(); assert(calls == 3U);
    assert(g_spi_tx_diag.polling_test_attempts == 3U);
    assert(g_spi_tx_diag.polling_test_completed == 2U);
    assert(g_spi_tx_diag.polling_test_failed == 1U);
    assert(g_spi_tx_diag.polling_test_last_status == 1U);
    SPI_LogSetReceiverReady(0U); tick = 40U; SPI_LogService(); assert(calls == 3U);
    assert(g_spi_tx_diag.started == 0U && g_spi_tx_diag.accepted == 0U);
    puts("PASS: polling packet, pacing, readiness, busy HAL, failure accounting, DMA isolation");
    return 0;
}
