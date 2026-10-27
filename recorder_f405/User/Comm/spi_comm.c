#include "control_def.h"
#if COMM_INTERFACE == COMM_INTERFACE_SPI
#include "spi_comm.h"
#include "spi.h"
#include "recorder.h"
#include "log_rx_timestamp.h"
#include <string.h>

volatile spi_comm_diag_t g_spi_comm_diag;
static uint8_t dma_buffer[SPI_COMM_DMA_SIZE] __attribute__((aligned(4)));
static uint8_t pending_buffer[SPI_COMM_CHUNK_SIZE];
static size_t pending_len;
static uint8_t timestamp_buffer[LOG_RX_TIMESTAMP_CAPACITY(SPI_COMM_CHUNK_SIZE)];
static size_t timestamp_len;
static uint8_t line_start = 1U;
static uint8_t formatted_line_start, formatted_ready;
static volatile uint32_t dma_wraps;
static volatile uint8_t active, fault;
static uint32_t consumed;
static uint8_t initialized;

/* TC 中断累计完整回绕次数；若 TC 已挂起但回调尚未执行，也计入本次位置。
 * 使用 PRIMASK 防止采样期间 HAL 回调修改计数，DMA 硬件仍继续运行。
 * 若采样期间新出现 TC 标志，则重新读取 NDTR。
 * 每次回绕至少处理一次 TC 中断；8192 字节在 12 Mbit/s 下约 5.46 ms 回绕。 */
static uint32_t producer_position(void)
{
    uint32_t key = __get_PRIMASK();
    uint32_t wraps, remaining, before, after;
    __disable_irq();
    wraps = dma_wraps;
    before = __HAL_DMA_GET_FLAG(hspi1.hdmarx,
                                __HAL_DMA_GET_TC_FLAG_INDEX(hspi1.hdmarx));
    remaining = __HAL_DMA_GET_COUNTER(hspi1.hdmarx);
    after = __HAL_DMA_GET_FLAG(hspi1.hdmarx,
                               __HAL_DMA_GET_TC_FLAG_INDEX(hspi1.hdmarx));
    if (after && !before) remaining = __HAL_DMA_GET_COUNTER(hspi1.hdmarx);
    if (after && remaining != 0U) wraps++;
    __DMB();
    __set_PRIMASK(key);
    return wraps * SPI_COMM_DMA_SIZE + SPI_COMM_DMA_SIZE - remaining;
}

static void start_receive(void)
{
    HAL_StatusTypeDef result;
    g_spi_comm_diag.start_attempts++;
    if (hspi1.hdmarx == NULL) {
        g_spi_comm_diag.start_reject_reason = SPI_COMM_START_NO_RX_DMA;
        g_spi_comm_diag.start_failed++;
        return;
    }
    if (hspi1.hdmarx->Init.Mode != DMA_CIRCULAR) {
        g_spi_comm_diag.start_reject_reason = SPI_COMM_START_DMA_NOT_CIRCULAR;
        g_spi_comm_diag.start_failed++;
        return;
    }
    g_spi_comm_diag.start_reject_reason = SPI_COMM_START_OK;
    dma_wraps = 0U;
    consumed = 0U;
    /* 提交前先标记接收状态，避免覆盖立即到达的完成或错误回调结果。 */
    active = 1U;
    result = HAL_SPI_Receive_DMA(&hspi1, dma_buffer, SPI_COMM_DMA_SIZE);
    g_spi_comm_diag.last_start_status = (uint32_t)result;
    g_spi_comm_diag.last_start_error = hspi1.ErrorCode;
    if (result != HAL_OK) {
        active = 0U;
        /* HAL 在 DMA 启动失败时可能保留 BUSY_RX；先中止再重启，
         * 避免后续永久返回 HAL_BUSY，也不直接改写 HAL 内部状态。 */
        fault = 1U;
        g_spi_comm_diag.start_reject_reason = SPI_COMM_START_HAL_FAILED;
        g_spi_comm_diag.start_failed++;
    }
}

void spi_comm_init(void)
{
    g_spi_comm_diag.init_calls++;
    if (initialized) return;
    initialized = 1U;
    start_receive();
}

void spi_comm_get_status(comm_status_t *status)
{
    /* 只接收的从机无法确认发送端是否连接，就绪仅表示本地接收已启动。 */
    status->ready = active && !fault;
    status->rx_count = g_spi_comm_diag.rx_chunks;
}

void spi_comm_process(void)
{
    uint32_t producer, available, offset, count, first;
    size_t free_size = 0U;
    unsigned budget;
    g_spi_comm_diag.process_calls++;
    if (!initialized) return;
    if (fault) {
        /* 在任务中执行 HAL 中止和重启，不在 SPI 中断中执行。
         * 恢复期间保留已复制的待写数据块。 */
        HAL_StatusTypeDef result = HAL_SPI_Abort(&hspi1);
        g_spi_comm_diag.last_abort_status = (uint32_t)result;
        if (result != HAL_OK) {
            g_spi_comm_diag.abort_failed++;
            return;
        }
        __HAL_SPI_CLEAR_OVRFLAG(&hspi1);
        fault = 0U;
        g_spi_comm_diag.recoveries++;
    }
    if (!active) start_receive();

    /* 限制每轮处理量，处理预算可覆盖 12 Mbit/s 下每 1 ms 约 1500 字节的输入。 */
    for (budget = 0U; budget < SPI_COMM_DMA_SIZE / SPI_COMM_CHUNK_SIZE; budget++) {
        if (pending_len != 0U) {
            if (!formatted_ready) {
                if (!LogRxFormatTimestamp(pending_buffer, pending_len, line_start,
                        timestamp_buffer, sizeof(timestamp_buffer),
                        &timestamp_len, &formatted_line_start)) {
                    g_spi_comm_diag.format_failed++;
                    return;
                }
                /* 重试写入时复用此结果，避免重复插入或刷新同一行的时间戳。 */
                formatted_ready = 1U;
            }
            if (!RecorderDataGetBufferStatus(&free_size) || free_size < timestamp_len) {
                g_spi_comm_diag.ring_free = (uint32_t)free_size;
                g_spi_comm_diag.ring_blocked++;
                return;
            }
            g_spi_comm_diag.ring_free = (uint32_t)free_size;
            if (!RecorderDataWrite((const char *)timestamp_buffer, timestamp_len)) {
                g_spi_comm_diag.write_failed++;
                return;
            }
            g_spi_comm_diag.written_chunks++;
            g_spi_comm_diag.written_bytes += (uint32_t)timestamp_len;
            line_start = formatted_line_start;
            pending_len = 0U;
            formatted_ready = 0U;
        }
        if (!active || fault) return;
        producer = producer_position();
        available = producer - consumed;
        if (available > SPI_COMM_DMA_SIZE) {
            /* 留出一个数据块的复制余量；若从已满 DMA 环的最旧字节开始复制，
             * 持续发送时会反复与下一次 DMA 写入冲突，导致始终无法提交数据。 */
            uint32_t keep = SPI_COMM_DMA_SIZE - SPI_COMM_CHUNK_SIZE;
            g_spi_comm_diag.overwritten_bytes += available - SPI_COMM_DMA_SIZE;
            g_spi_comm_diag.resync_discarded_bytes += SPI_COMM_CHUNK_SIZE;
            consumed = producer - keep;
            available = keep;
        }
        g_spi_comm_diag.dma_available = available;
        if (available == 0U) return;
        count = available < SPI_COMM_CHUNK_SIZE ? available : SPI_COMM_CHUNK_SIZE;
        offset = consumed & (SPI_COMM_DMA_SIZE - 1U);
        first = SPI_COMM_DMA_SIZE - offset;
        if (first > count) first = count;
        memcpy(pending_buffer, dma_buffer + offset, first);
        memcpy(pending_buffer + first, dma_buffer, count - first);
        __DMB();
        /* DMA 可能在复制期间回绕，存在覆盖风险的副本不得交给后续写入。 */
        if (fault || producer_position() - consumed > SPI_COMM_DMA_SIZE) return;
        {
            uint32_t i, sample_len = count < 32U ? count : 32U;
            g_spi_comm_diag.last_sample_len = 0U;
            for (i = 0U; i < sample_len; i++) g_spi_comm_diag.last_sample[i] = pending_buffer[i];
            g_spi_comm_diag.last_sample_len = sample_len;
            if (g_spi_comm_diag.first_sample_len == 0U) {
                for (i = 0U; i < sample_len; i++) g_spi_comm_diag.first_sample[i] = pending_buffer[i];
                g_spi_comm_diag.first_sample_len = sample_len;
            }
        }
        consumed += count;
        pending_len = count;
        g_spi_comm_diag.rx_chunks++;
    }
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == &hspi1 && active) dma_wraps++;
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == &hspi1 && initialized) {
        g_spi_comm_diag.last_error = hspi->ErrorCode;
        g_spi_comm_diag.errors++;
        active = 0U;
        fault = 1U;
    }
}
#endif
