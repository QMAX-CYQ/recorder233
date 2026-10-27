#ifndef SPI_LOG_H
#define SPI_LOG_H
#include <stdint.h>
#include "spi_log_config.h"

enum { LOG_SPI_OK = 0, LOG_SPI_BUSY = 1, LOG_SPI_FAIL = 2 };

/* 仅表示本地允许发送，不代表记录板已收到或落盘。 */
extern volatile uint8_t g_spi_log_link_ready;
typedef struct { uint32_t session, waiting_loops; } spi_log_link_diag_t;
extern volatile spi_log_link_diag_t g_spi_log_link_diag;
typedef struct {
    uint32_t attempts, accepted, busy, invalid, start_failed;
    uint32_t accepted_bytes, completed, completed_bytes;
    uint32_t init_count, link_reset_count;
    uint32_t buffered, queue_high_water, started, started_bytes;
    uint32_t errors, last_error, error_lost_bytes, recoveries;
} spi_tx_diag_t;
extern volatile spi_tx_diag_t g_spi_tx_diag;
extern volatile uint32_t spi_tx_pending_count, spi_tx_inflight;
extern volatile uint32_t spi_tx_queue_overflow_count;

/* 在 SPI/DMA 初始化后调用一次；日志任务入口已调用。 */
void SPI_LogInit(void);
/* 由应用确认记录板 DMA 已启动后置 1；置 0 暂停启动后续发送。
 * 允许在初始化前设置；不能用本地 DMA 完成代替接收端就绪确认。 */
void SPI_LogSetReceiverReady(uint8_t ready);
/* 仅任务调用；完整复制调用方数据，OK 表示入队而非 DMA 完成。 */
uint8_t SPI_LogTransmit(const uint8_t *data, uint16_t length);
/* 仅由日志任务周期调用，恢复错误并尝试启动队首。 */
void SPI_LogService(void);

#endif
