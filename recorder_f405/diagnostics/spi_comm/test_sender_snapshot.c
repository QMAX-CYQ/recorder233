#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "spi_log_config.h"
/* 仅测试桩以本机数组地址替代 H723 的固定 SRAM 地址。 */
#undef LOG_SPI_DMA_BASE
#undef LOG_SPI_DMA_BYTES
#define LOG_SPI_DMA_BASE ((uintptr_t)dma_buffer)
#define LOG_SPI_DMA_BYTES LOG_SPI_MAX_PACKET
#include "D:/RM/wheel_leg_2026/wheel_leg_2025--4/rm_main/User/Log/spi_log.c"

static DMA_HandleTypeDef dma = {{DMA_NORMAL, DMA_PDATAALIGN_BYTE, DMA_MDATAALIGN_BYTE}};
SPI_HandleTypeDef hspi1 = {{SPI_MODE_MASTER, SPI_DIRECTION_2LINES_TXONLY, SPI_DATASIZE_8BIT}, &dma, 0U, HAL_SPI_STATE_READY};
static uint8_t *inflight;
static uint16_t inflight_length;
static uint8_t output[40000];
static size_t output_length;
static unsigned clean_count, abort_count;
static int fail_start;

void CleanDCacheForDmaTx(const void *addr, uint32_t len)
{
    assert(addr == dma_buffer && len > 0U && len <= LOG_SPI_MAX_PACKET);
    clean_count++;
}
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *h, uint8_t *p, uint16_t n)
{
    assert(h == &hspi1 && h->State == HAL_SPI_STATE_READY);
    assert(p == dma_buffer && clean_count != 0U);
    if (fail_start) { fail_start = 0; return 1; }
    inflight = p; inflight_length = n; h->State = 1U;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *h)
{
    abort_count++; h->State = HAL_SPI_STATE_READY; return HAL_OK;
}
uint32_t HAL_SPI_GetState(SPI_HandleTypeDef *h) { return h->State; }
static void complete(void)
{
    assert(spi_tx_inflight && output_length + inflight_length <= sizeof(output));
    memcpy(output + output_length, inflight, inflight_length);
    output_length += inflight_length;
    hspi1.State = HAL_SPI_STATE_READY;
    HAL_SPI_TxCpltCallback(&hspi1);
}
int main(void)
{
    uint8_t a[] = "first\r\n", b[] = "second\r\n";
    uint8_t large[LOG_SPI_MAX_PACKET] = {0};
    uint32_t session;
    unsigned i;
    SPI_LogService();
    assert(SPI_LogTransmit(a, sizeof(a)) == LOG_SPI_FAIL);
    SPI_LogInit(); SPI_LogInit();
    assert(g_spi_tx_diag.init_count == 1U && config_valid);
    assert(!g_spi_log_link_ready);
    assert(SPI_LogTransmit(a, sizeof(a)) == LOG_SPI_OK);
    memset(a, 'x', sizeof(a));
    assert(!spi_tx_inflight && spi_tx_pending_count == 1U);
    SPI_LogSetReceiverReady(1U); SPI_LogService();
    assert(spi_tx_inflight && spi_tx_pending_count == 0U);
    assert(g_spi_tx_diag.completed == 0U);
    assert(g_spi_tx_diag.first_sample_len == sizeof(a));
    assert(memcmp((const void *)g_spi_tx_diag.first_sample, "first\r\n", sizeof(a)) == 0);
    assert(SPI_LogTransmit(b, sizeof(b)) == LOG_SPI_OK);
    complete();
    assert(spi_tx_inflight && spi_tx_pending_count == 0U);
    assert(output_length == sizeof(a) && memcmp(output, "first\r\n", sizeof(a)) == 0);
    complete();
    assert(!spi_tx_inflight && g_spi_tx_diag.completed == 2U);
    assert(memcmp((const void *)g_spi_tx_diag.last_sample, b, sizeof(b)) == 0);
    assert(memcmp(output + sizeof(a), b, sizeof(b)) == 0);
    fail_start = 1;
    assert(SPI_LogTransmit(b, sizeof(b)) == LOG_SPI_OK);
    assert(spi_tx_pending_count == 1U && !spi_tx_inflight);
    SPI_LogService(); complete();
    assert(g_spi_tx_diag.start_failed == 1U);
    SPI_LogSetReceiverReady(0U);
    for (i = 0U; i < LOG_SPI_QUEUE_CAPACITY; i++)
        assert(SPI_LogTransmit(large, sizeof(large)) == LOG_SPI_OK);
    assert(SPI_LogTransmit(b, sizeof(b)) == LOG_SPI_BUSY);
    assert(spi_tx_queue_overflow_count == 1U);
    SPI_LogSetReceiverReady(1U); SPI_LogService();
    for (i = 0U; i < LOG_SPI_QUEUE_CAPACITY; i++) complete();
    assert(!spi_tx_inflight && !spi_tx_pending_count);
    assert(SPI_LogTransmit(NULL, 3U) == LOG_SPI_FAIL);
    assert(SPI_LogTransmit(large, 0U) == LOG_SPI_FAIL);
    assert(SPI_LogTransmit(large, LOG_SPI_MAX_PACKET + 1U) == LOG_SPI_FAIL);
    assert(SPI_LogTransmit(b, sizeof(b)) == LOG_SPI_OK);
    assert(SPI_LogTransmit(b, sizeof(b)) == LOG_SPI_OK);
    session = g_spi_log_link_diag.session;
    hspi1.ErrorCode = 8U; HAL_SPI_ErrorCallback(&hspi1);
    assert(!g_spi_log_link_ready && !spi_tx_inflight);
    assert(g_spi_tx_diag.error_lost_bytes == sizeof(b));
    assert(g_spi_log_link_diag.session != session && spi_tx_pending_count == 1U);
    SPI_LogService();
    assert(abort_count == 1U && g_spi_tx_diag.recoveries == 1U);
    assert(spi_tx_inflight && !spi_tx_pending_count);
    complete();
    assert(!spi_tx_inflight);
    puts("PASS: copy ownership, ready gating, completion chaining, retry, full queue, errors, recovery");
    return 0;
}
