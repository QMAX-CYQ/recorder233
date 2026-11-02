#ifndef USER_ADD_USB_DMA_PORT_H
#define USER_ADD_USB_DMA_PORT_H

/* 仅包含在 usbd_conf.c 的 USER CODE BEGIN 0 中，位于 HAL 头文件之后。 */
#include "../Support/usb_dma_memory.h"

/* 只有受保护区启用时才引用对应链接符号，关闭时不产生此依赖。 */
#if USB_DMA_PROTECTED_REQUIRED
extern uint8_t usb_dma_region_start[] __asm("Image$$RW_USB_DMA$$Base");
extern uint8_t usb_dma_region_limit[] __asm("Image$$RW_USB_DMA$$Limit");

/* USB DMA 启动前配置一次，实际关闭 D-Cache 时跳过 MPU 设置。 */
static void USB_DMA_ConfigMemory(void)
{
  static uint8_t configured;
  MPU_Region_InitTypeDef region = {0};
  uint32_t irq_state;
  uint32_t saved_ctrl;
  uint32_t saved_rnr;

  if ((uintptr_t)usb_dma_region_start != USB_DMA_REGION_BASE ||
      (uintptr_t)usb_dma_region_limit > USB_DMA_REGION_BASE + USB_DMA_REGION_BYTES) {
    Error_Handler();
    return;
  }
  if ((SCB->CCR & SCB_CCR_DC_Msk) == 0U || configured != 0U) return;

  irq_state = __get_PRIMASK();
  __disable_irq();
  saved_ctrl = MPU->CTRL;
  saved_rnr = MPU->RNR;
  MPU->RNR = USB_DMA_MPU_REGION;
  /* 此 MPU 区专供 USB 使用，不能覆盖其他模块已配置的区域。 */
  if ((MPU->RASR & MPU_RASR_ENABLE_Msk) != 0U) {
    MPU->RNR = saved_rnr;
    __set_PRIMASK(irq_state);
    Error_Handler();
    return;
  }

  /* USBD_Init 可能已在缓存开启时写入句柄，修改内存属性前先提交这些数据。 */
  SCB_CleanInvalidateDCache_by_Addr((uint32_t *)USB_DMA_REGION_BASE,
                                  (int32_t)USB_DMA_REGION_BYTES);
  __DSB();
  MPU->CTRL = 0U;
  __DSB();
  __ISB();

  region.Enable = MPU_REGION_ENABLE;
  region.Number = USB_DMA_MPU_REGION;
  region.BaseAddress = USB_DMA_REGION_BASE;
  region.Size = MPU_REGION_SIZE_16KB;
  region.SubRegionDisable = 0U;
  region.TypeExtField = MPU_TEX_LEVEL1; /* 普通非缓存内存。 */
  region.AccessPermission = MPU_REGION_FULL_ACCESS;
  region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  region.IsShareable = MPU_ACCESS_SHAREABLE;
  region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&region);
  MPU->RNR = saved_rnr;
  MPU->CTRL = (saved_ctrl & MPU_CTRL_ENABLE_Msk) != 0U ? saved_ctrl :
              (MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk);
  __DSB();
  __ISB();
  configured = 1U;
  __set_PRIMASK(irq_state);
}

#else
/* 未使用非缓存区时不访问任何 RW_USB_DMA 链接符号。 */
static void USB_DMA_ConfigMemory(void)
{
#if DMA_MEMORY_USB_DMA_ENABLED
  /* 若实际缓存状态与生成配置不符，拒绝在缺少保护区时启动 USB DMA。 */
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) Error_Handler();
#endif
}
#endif

/* 本地封装保留 CubeMX 原 HAL 调用，别名须放在封装定义之后。 */
static HAL_StatusTypeDef USB_DMA_EP_Transmit(PCD_HandleTypeDef *pcd,
    uint8_t ep, uint8_t *buffer, uint32_t size)
{
  __DSB();
  return HAL_PCD_EP_Transmit(pcd, ep, buffer, size);
}

static HAL_StatusTypeDef USB_DMA_EP_Receive(PCD_HandleTypeDef *pcd,
    uint8_t ep, uint8_t *buffer, uint32_t size)
{
  __DSB();
  return HAL_PCD_EP_Receive(pcd, ep, buffer, size);
}

#define HAL_PCD_EP_Transmit USB_DMA_EP_Transmit
#define HAL_PCD_EP_Receive  USB_DMA_EP_Receive

#endif
