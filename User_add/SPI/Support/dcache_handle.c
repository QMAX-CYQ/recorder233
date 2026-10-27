#include "dcache_handle.h"
#include "stm32h7xx.h"
#include <limits.h>

static int dcache_range(const void *addr, uint32_t len,
                        uintptr_t *aligned, int32_t *bytes)
{
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end;
    uintptr_t last;
    if (addr == 0 || len == 0U || (SCB->CCR & SCB_CCR_DC_Msk) == 0U)
        return 0;
    if ((uintptr_t)len > UINTPTR_MAX - start) return 0;
    end = start + len;
    if (end > UINTPTR_MAX - (DCACHE_LINE_SIZE - 1U)) return 0;
    *aligned = start & ~((uintptr_t)DCACHE_LINE_SIZE - 1U);
    last = (end + DCACHE_LINE_SIZE - 1U) & ~((uintptr_t)DCACHE_LINE_SIZE - 1U);
    if (last - *aligned > INT32_MAX) return 0;
    *bytes = (int32_t)(last - *aligned);
    return 1;
}

void CleanDCacheForDmaTx(const void *addr, uint32_t len)
{
    uintptr_t aligned;
    int32_t bytes;
    if (dcache_range(addr, len, &aligned, &bytes))
        SCB_CleanDCache_by_Addr((uint32_t *)aligned, bytes);
}

void InvalidateDCacheForDmaRx(void *addr, uint32_t len)
{
    uintptr_t aligned;
    int32_t bytes;
    if (dcache_range(addr, len, &aligned, &bytes))
        SCB_InvalidateDCache_by_Addr((uint32_t *)aligned, bytes);
}
