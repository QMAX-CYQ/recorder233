#include "spi_log.h"
#include "spi.h"
#include "dcache_handle.h"
#include <string.h>

typedef struct {
    uint16_t length;
    uint8_t data[LOG_SPI_MAX_PACKET];
} spi_log_slot_t;

static spi_log_slot_t queue[LOG_SPI_QUEUE_CAPACITY];
/* DMA 只访问独立且按缓存行对齐的暂存区；普通队列可由链接器放入 DTCM。 */
__attribute__((section(".bss.spi_log_dma"), aligned(32)))
static uint8_t dma_buffer[LOG_SPI_MAX_PACKET];
static uint32_t head, tail;
static uint16_t inflight_bytes;
static uint8_t initialized, config_valid;
static volatile uint8_t receiver_ready, recovery_needed;

volatile uint8_t g_spi_log_link_ready;
volatile spi_log_link_diag_t g_spi_log_link_diag;
volatile spi_tx_diag_t g_spi_tx_diag;
volatile uint32_t spi_tx_pending_count, spi_tx_inflight;
volatile uint32_t spi_tx_queue_overflow_count;

/* 调用者须屏蔽中断；HAL 启动为非阻塞操作，回调将在恢复中断后执行。 */
static void start_locked(void)
{
    HAL_StatusTypeDef result;
    uint16_t length;
    if (!g_spi_log_link_ready || recovery_needed || spi_tx_inflight ||
        spi_tx_pending_count == 0U) return;
    if (HAL_SPI_GetState(&LOG_SPI_HANDLE) != HAL_SPI_STATE_READY) return;
    length = queue[tail].length;
    memcpy(dma_buffer, queue[tail].data, length);
    CleanDCacheForDmaTx(dma_buffer, length);
    __DSB();
    inflight_bytes = length;
    spi_tx_inflight = 1U;
    result = HAL_SPI_Transmit_DMA(&LOG_SPI_HANDLE, dma_buffer, length);
    if (result == HAL_OK) {
        tail = (tail + 1U) % LOG_SPI_QUEUE_CAPACITY;
        spi_tx_pending_count--;
        g_spi_tx_diag.buffered = spi_tx_pending_count;
        g_spi_tx_diag.started++;
        g_spi_tx_diag.started_bytes += length;
    } else {
        /* 启动失败时仍保留队首；下一轮重试，不误报发送完成。 */
        spi_tx_inflight = 0U;
        inflight_bytes = 0U;
        g_spi_tx_diag.start_failed++;
    }
}

void SPI_LogInit(void)
{
    uintptr_t address = (uintptr_t)dma_buffer;
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if (!initialized) {
        initialized = 1U;
        g_spi_tx_diag.init_count++;
        g_spi_log_link_diag.session++;
        config_valid = LOG_SPI_HANDLE.hdmatx != NULL &&
            LOG_SPI_HANDLE.Init.Mode == SPI_MODE_MASTER &&
            LOG_SPI_HANDLE.Init.Direction == SPI_DIRECTION_2LINES_TXONLY &&
            LOG_SPI_HANDLE.Init.DataSize == SPI_DATASIZE_8BIT &&
            LOG_SPI_HANDLE.hdmatx->Init.Mode == DMA_NORMAL &&
            LOG_SPI_HANDLE.hdmatx->Init.PeriphDataAlignment == DMA_PDATAALIGN_BYTE &&
            LOG_SPI_HANDLE.hdmatx->Init.MemDataAlignment == DMA_MDATAALIGN_BYTE &&
            address >= LOG_SPI_DMA_BASE &&
            address + sizeof(dma_buffer) <= LOG_SPI_DMA_BASE + LOG_SPI_DMA_BYTES;
    }
    __set_PRIMASK(key);
}

void SPI_LogSetReceiverReady(uint8_t ready)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if (!ready && receiver_ready) {
        g_spi_log_link_ready = 0U;
        g_spi_tx_diag.link_reset_count++;
        g_spi_log_link_diag.session++;
    }
    receiver_ready = ready != 0U;
    __set_PRIMASK(key);
}

uint8_t SPI_LogTransmit(const uint8_t *data, uint16_t length)
{
    uint8_t result;
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    g_spi_tx_diag.attempts++;
    if (!initialized || !config_valid || data == NULL || length == 0U ||
        length > LOG_SPI_MAX_PACKET) {
        g_spi_tx_diag.invalid++;
        result = LOG_SPI_FAIL;
    } else if (spi_tx_pending_count == LOG_SPI_QUEUE_CAPACITY) {
        spi_tx_queue_overflow_count++;
        g_spi_tx_diag.busy++;
        result = LOG_SPI_BUSY;
    } else {
        memcpy(queue[head].data, data, length);
        queue[head].length = length;
        head = (head + 1U) % LOG_SPI_QUEUE_CAPACITY;
        spi_tx_pending_count++;
        g_spi_tx_diag.buffered = spi_tx_pending_count;
        if (spi_tx_pending_count > g_spi_tx_diag.queue_high_water)
            g_spi_tx_diag.queue_high_water = spi_tx_pending_count;
        g_spi_tx_diag.accepted++;
        g_spi_tx_diag.accepted_bytes += length;
        start_locked();
        result = LOG_SPI_OK;
    }
    __set_PRIMASK(key);
    return result;
}

void SPI_LogService(void)
{
    uint32_t key;
    if (!initialized || !config_valid) return;
    if (recovery_needed) {
        /* 中止操作只在任务中执行；未知发送进度的数据不自动重发。 */
        if (HAL_SPI_Abort(&LOG_SPI_HANDLE) != HAL_OK) return;
        key = __get_PRIMASK();
        __disable_irq();
        recovery_needed = 0U;
        g_spi_tx_diag.recoveries++;
        __set_PRIMASK(key);
    }
    key = __get_PRIMASK();
    __disable_irq();
    g_spi_log_link_ready = receiver_ready && !recovery_needed;
    start_locked();
    __set_PRIMASK(key);
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    uint32_t key;
    if (hspi != &LOG_SPI_HANDLE) return;
    key = __get_PRIMASK();
    __disable_irq();
    if (spi_tx_inflight) {
        /* H7 的 HAL 在 EOT 结束并恢复 READY 后调用这里，才可复用暂存区。 */
        g_spi_tx_diag.completed++;
        g_spi_tx_diag.completed_bytes += inflight_bytes;
        spi_tx_inflight = 0U;
        inflight_bytes = 0U;
        start_locked();
    }
    __set_PRIMASK(key);
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    uint32_t key;
    if (hspi != &LOG_SPI_HANDLE || !initialized) return;
    key = __get_PRIMASK();
    __disable_irq();
    g_spi_tx_diag.errors++;
    g_spi_tx_diag.last_error = hspi->ErrorCode;
    g_spi_tx_diag.error_lost_bytes += inflight_bytes;
    g_spi_tx_diag.link_reset_count++;
    g_spi_log_link_diag.session++;
    inflight_bytes = 0U;
    spi_tx_inflight = 0U;
    g_spi_log_link_ready = 0U;
    recovery_needed = 1U;
    __set_PRIMASK(key);
}
