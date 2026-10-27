#ifndef USB_DMA_MEMORY_H
#define USB_DMA_MEMORY_H

/* C 与链接脚本共用，只在 D-Cache 和 USB DMA 同时启用时保留非缓存区。 */
#include "dma_memory_config.h"
#if (DMA_MEMORY_DCACHE_ENABLED != 0 && DMA_MEMORY_DCACHE_ENABLED != 1) || \
    (DMA_MEMORY_USB_DMA_ENABLED != 0 && DMA_MEMORY_USB_DMA_ENABLED != 1)
#error "DMA memory flags must be 0 or 1"
#endif
#define USB_DMA_PROTECTED_REQUIRED (DMA_MEMORY_DCACHE_ENABLED && DMA_MEMORY_USB_DMA_ENABLED)
#define USB_DMA_REGION_BASE 0x24048000
#define USB_DMA_REGION_BYTES 0x00004000
#define USB_DMA_MPU_REGION 7U

#endif
