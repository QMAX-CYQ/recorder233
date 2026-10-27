#ifndef SPI_COMM_H
#define SPI_COMM_H

#include "comm.h"

/* DMA 缓冲区字节数须为 2 的幂，存放于 DMA 可访问的 SRAM，不能放入 CCM。 */
#define SPI_COMM_DMA_SIZE 8192U
#define SPI_COMM_CHUNK_SIZE 512U

/* 启动拒绝原因，便于区分配置错误与 HAL 启动失败。 */
enum {
    SPI_COMM_START_OK = 0U,
    SPI_COMM_START_NO_RX_DMA,
    SPI_COMM_START_DMA_NOT_CIRCULAR,
    SPI_COMM_START_HAL_FAILED
};

typedef struct {
    uint32_t rx_chunks, written_chunks, written_bytes;
    uint32_t overwritten_bytes, resync_discarded_bytes, ring_blocked, write_failed;
    uint32_t start_failed, errors, recoveries, last_error, format_failed;
    uint32_t ring_free, dma_available;
    uint32_t init_calls, process_calls, start_attempts;
    uint32_t start_reject_reason, last_start_status, last_start_error;
    uint32_t abort_failed, last_abort_status;
    /* 首次及最近一次取出的原始字节，位于时间戳格式化之前。 */
    uint32_t first_sample_len, last_sample_len;
    uint8_t first_sample[32], last_sample[32];
} spi_comm_diag_t;

extern volatile spi_comm_diag_t g_spi_comm_diag;

/* 须在 CubeMX 完成 SPI1 和 DMA 初始化后调用。 */
void spi_comm_init(void);
/* 仅由通信任务调用，中断中不写入 lwrb。 */
void spi_comm_process(void);
void spi_comm_get_status(comm_status_t *status);

#endif
