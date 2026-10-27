#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "spi.h"
#include "spi_comm.h"
#include "calendar.h"

static DMA_HandleTypeDef dma = {{DMA_CIRCULAR}, SPI_COMM_DMA_SIZE, 0U};
SPI_HandleTypeDef hspi1 = {&dma, 0U};
static uint8_t *buffer;
static uint32_t pos, received, output_len, starts, aborts;
static uint8_t output[32768], expected[32768];
static uint8_t formatted_output[65536];
static size_t formatted_size;
static uint8_t output_line_start = 1U, rtc_second = 56U;
static unsigned timestamp_count;
static size_t ring_free = sizeof(output);
static int fail_write, fail_start;
static uint8_t spi_busy;
static unsigned inject_barrier;

uint8_t Calendar_ReadTime(time_t *value)
{
    value->year = 2026U; value->month = 10U; value->day = 25U;
    value->hour = 12U; value->min = 34U; value->second = rtc_second;
    return 1U;
}
uint32_t HAL_GetTick(void) { return 123U; }

HAL_StatusTypeDef HAL_SPI_Receive_DMA(SPI_HandleTypeDef *h, uint8_t *p, uint16_t n)
{
    (void)h;
    assert(n == SPI_COMM_DMA_SIZE);
    if (spi_busy) return HAL_BUSY;
    starts++;
    spi_busy = 1U;
    if (fail_start) { fail_start = 0; hspi1.ErrorCode = 16U; return HAL_ERROR; }
    buffer = p; pos = 0U; dma.count = n; dma.tc = 0U;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *h)
{
    (void)h; aborts++; spi_busy = 0U; return HAL_OK;
}
uint8_t RecorderDataGetBufferStatus(size_t *free_size)
{
    *free_size = ring_free; return 1U;
}
uint8_t RecorderDataWrite(const char *p, size_t n)
{
    size_t i;
    if (fail_write) { fail_write = 0; return 0U; }
    assert(n <= ring_free && formatted_size + n <= sizeof(formatted_output));
    memcpy(formatted_output + formatted_size, p, n); formatted_size += n;
    /* 独立检查每个非空行恰有一个前缀，去掉前缀后继续执行原 DMA 字节校验。 */
    for (i = 0U; i < n; i++) {
        if (output_line_start && p[i] != '\r' && p[i] != '\n') {
            assert(n - i >= 15U);
            assert(memcmp(p + i, "1025-12:34:56 ", 14U) == 0 ||
                   memcmp(p + i, "1025-12:34:57 ", 14U) == 0);
            i += 14U;
            output_line_start = 0U;
            timestamp_count++;
        }
        assert(output_len < sizeof(output));
        output[output_len++] = (uint8_t)p[i];
        if (p[i] == '\n') output_line_start = 1U;
    }
    return 1U;
}
static void feed_byte(uint8_t ch, int pending_irq)
{
    expected[received++] = ch;
    buffer[pos++] = ch;
    if (pos == SPI_COMM_DMA_SIZE) {
        pos = 0U;
        if (pending_irq) dma.tc = 1U;
        else HAL_SPI_RxCpltCallback(&hspi1);
    }
    dma.count = SPI_COMM_DMA_SIZE - pos;
}
static void feed(uint32_t n, int pending_irq)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint8_t ch = (uint8_t)(received * 37U + received / 251U);
        feed_byte(ch, pending_irq);
    }
}
static void feed_text(const char *text)
{
    while (*text) feed_byte((uint8_t)*text++, 0);
}
void test_barrier(void)
{
    if (inject_barrier != 0U && --inject_barrier == 0U)
        feed(SPI_COMM_DMA_SIZE + 7U, 0);
}
static void drain(void)
{
    unsigned i;
    for (i = 0; i < 4U; i++) spi_comm_process();
}
int main(void)
{
    comm_status_t status;
    uint32_t checkpoint, before_drop;
    size_t formatted_checkpoint;
    unsigned timestamps_before;
    spi_comm_process(); assert(starts == 0U);
    hspi1.hdmarx = NULL;
    spi_comm_init();
    assert(g_spi_comm_diag.start_failed == 1U);
    assert(g_spi_comm_diag.start_reject_reason == SPI_COMM_START_NO_RX_DMA);
    hspi1.hdmarx = &dma; dma.Init.Mode = 0U;
    spi_comm_process();
    assert(g_spi_comm_diag.start_reject_reason == SPI_COMM_START_DMA_NOT_CIRCULAR);
    dma.Init.Mode = DMA_CIRCULAR; fail_start = 1;
    spi_comm_process();
    assert(g_spi_comm_diag.start_failed == 3U);
    assert(g_spi_comm_diag.start_reject_reason == SPI_COMM_START_HAL_FAILED);
    assert(g_spi_comm_diag.last_start_status == HAL_ERROR);
    assert(g_spi_comm_diag.last_start_error == 16U);
    spi_comm_process(); assert(starts == 2U);
    assert(aborts == 1U && g_spi_comm_diag.last_start_status == HAL_OK);
    assert(g_spi_comm_diag.start_reject_reason == SPI_COMM_START_OK);
    spi_comm_init(); assert(starts == 2U);
    feed(13U, 0); drain();
    assert(output_len == 13U && memcmp(output, expected, output_len) == 0);
    assert(g_spi_comm_diag.first_sample_len == 13U);
    assert(g_spi_comm_diag.last_sample_len == 13U);
    assert(memcmp((const void *)g_spi_comm_diag.first_sample, expected, 13U) == 0);
    assert(memcmp((const void *)g_spi_comm_diag.last_sample, expected, 13U) == 0);
    feed(SPI_COMM_DMA_SIZE - 1013U, 0); drain(); feed(2000U, 1); drain();
    assert(output_len == received && memcmp(output, expected, output_len) == 0);
    dma.tc = 0U; HAL_SPI_RxCpltCallback(&hspi1);
    drain(); assert(output_len == received); /* 挂起的 TC 只计入一次。 */
    feed(100U, 0); fail_write = 1; spi_comm_process();
    assert(g_spi_comm_diag.write_failed == 1U);
    drain(); assert(output_len == received && memcmp(output, expected, output_len) == 0);
    ring_free = 0U; checkpoint = output_len;
    feed(512U, 0); spi_comm_process(); /* 总环阻塞时保留已复制的数据块。 */
    feed(SPI_COMM_DMA_SIZE + 123U, 0); spi_comm_process();
    assert(output_len == checkpoint && g_spi_comm_diag.ring_blocked != 0U);
    ring_free = sizeof(output); before_drop = received - SPI_COMM_DMA_SIZE + SPI_COMM_CHUNK_SIZE;
    drain();
    assert(g_spi_comm_diag.overwritten_bytes == 123U);
    assert(g_spi_comm_diag.resync_discarded_bytes == SPI_COMM_CHUNK_SIZE);
    assert(memcmp(output + checkpoint, expected + checkpoint, 512U) == 0);
    assert(memcmp(output + checkpoint + 512U, expected + before_drop,
                  SPI_COMM_DMA_SIZE - SPI_COMM_CHUNK_SIZE) == 0);
    assert(output_len == received - 123U - SPI_COMM_CHUNK_SIZE);
    /* 错误恢复保留已复制块，尚未复制的 DMA 数据失效。 */
    feed(23U, 0); ring_free = 0U; spi_comm_process();
    hspi1.ErrorCode = 4U; HAL_SPI_ErrorCallback(&hspi1);
    spi_comm_get_status(&status); assert(!status.ready);
    ring_free = sizeof(output); drain();
    assert(aborts == 2U && g_spi_comm_diag.recoveries == 2U);
    assert(memcmp(output + output_len - 23U, expected + received - 23U, 23U) == 0);
    spi_comm_get_status(&status); assert(status.ready && status.rx_count != 0U);
    checkpoint = output_len;
    feed(31U, 0);
    inject_barrier = 2U; /* 模拟复制完成后 DMA 立即覆盖源数据。 */
    spi_comm_process();
    assert(output_len == checkpoint);
    drain();
    assert(output_len == checkpoint + SPI_COMM_DMA_SIZE - SPI_COMM_CHUNK_SIZE);
    assert(memcmp(output + checkpoint,
                  expected + received - SPI_COMM_DMA_SIZE + SPI_COMM_CHUNK_SIZE,
                  SPI_COMM_DMA_SIZE - SPI_COMM_CHUNK_SIZE) == 0);
    feed_text("\n"); drain();
    formatted_checkpoint = formatted_size;
    timestamps_before = timestamp_count;
    feed_text("INFO: A"); fail_write = 1; spi_comm_process();
    assert(formatted_size == formatted_checkpoint);
    rtc_second = 57U;
    drain();
    assert(memcmp(formatted_output + formatted_checkpoint,
                  "1025-12:34:56 INFO: A", 21U) == 0);
    feed_text("BC\r"); drain();
    assert(timestamp_count == timestamps_before + 1U);
    ring_free = 0U; feed_text("\nWARN: X\r"); spi_comm_process();
    ring_free = sizeof(output); drain();
    feed_text("\n"); drain();
    assert(timestamp_count == timestamps_before + 2U);
    assert(formatted_size - formatted_checkpoint ==
           strlen("1025-12:34:56 INFO: ABC\r\n1025-12:34:57 WARN: X\r\n"));
    assert(memcmp(formatted_output + formatted_checkpoint,
                  "1025-12:34:56 INFO: ABC\r\n1025-12:34:57 WARN: X\r\n",
                  formatted_size - formatted_checkpoint) == 0);
    /* 原始数据能放下但格式化结果放不下时，也必须保留整块等待。 */
    formatted_checkpoint = formatted_size;
    timestamps_before = timestamp_count;
    for (checkpoint = 0U; checkpoint < SPI_COMM_CHUNK_SIZE; checkpoint++)
        feed_byte(checkpoint % 2U ? '\n' : 'a', 0);
    ring_free = SPI_COMM_CHUNK_SIZE;
    spi_comm_process();
    assert(formatted_size == formatted_checkpoint);
    ring_free = 4096U; drain();
    assert(formatted_size - formatted_checkpoint == 4096U);
    assert(timestamp_count == timestamps_before + 256U);
    assert(g_spi_comm_diag.format_failed == 0U);
    assert(g_spi_comm_diag.written_bytes == formatted_size);
    assert(memcmp((const void *)g_spi_comm_diag.first_sample, expected, 13U) == 0);
    puts("PASS: DMA recovery, overflow, copy race, per-line timestamps, split CRLF, retry stability");
    return 0;
}
