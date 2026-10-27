#ifndef DMA_MEMORY_CONFIG_H
#define DMA_MEMORY_CONFIG_H
/* 未同步时保守保留 USB 非缓存区；构建前脚本会按目标 .ioc 更新本文件。 */
#define DMA_MEMORY_DCACHE_ENABLED 1
#define DMA_MEMORY_USB_DMA_ENABLED 1
#endif