#ifndef USER_ADD_DCACHE_HANDLE_H
#define USER_ADD_DCACHE_HANDLE_H
#include <stdint.h>
#define DCACHE_LINE_SIZE 32U
/* DMA 缓存辅助函数；SPI 发送前清理缓存，关闭 D-Cache 时跳过维护。 */
void CleanDCacheForDmaTx(const void *addr, uint32_t len);
void InvalidateDCacheForDmaRx(void *addr, uint32_t len);
#endif
