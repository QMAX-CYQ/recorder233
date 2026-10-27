#ifndef USER_ADD_DCACHE_HANDLE_H
#define USER_ADD_DCACHE_HANDLE_H
#include <stdint.h>
#define DCACHE_LINE_SIZE 32U
/* Optional UART/SPI/etc. helpers. USB uses the dedicated non-cacheable region. */
void CleanDCacheForDmaTx(const void *addr, uint32_t len);
void InvalidateDCacheForDmaRx(void *addr, uint32_t len);
#endif
