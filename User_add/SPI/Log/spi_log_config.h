#ifndef SPI_LOG_CONFIG_H
#define SPI_LOG_CONFIG_H

/* SPI 专供日志使用，不得与灯带或其他驱动共用同一个句柄。 */
#define LOG_SPI_HANDLE hspi1
#define LOG_SPI_QUEUE_CAPACITY 32U
#define LOG_SPI_MAX_PACKET 768U

/* 与链接脚本保持一致，DMA 暂存区必须位于 H723 的 AXI SRAM。 */
#define LOG_SPI_DMA_BASE 0x2404E000
#define LOG_SPI_DMA_BYTES 0x00001000

#endif
