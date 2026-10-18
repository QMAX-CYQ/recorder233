#ifndef USER_ADD_USB_DMA_PORT_H
#define USER_ADD_USB_DMA_PORT_H

/* Include ONLY in usbd_conf.c USER CODE BEGIN 0, after HAL headers. */
#include "usb_dma_memory.h"

/* These symbols must be provided by the package fault_noinit.sct. */
#define usb_dma_region_start ((uint8_t *)USB_DMA_REGION_BASE)
#define usb_dma_region_limit ((uint8_t *)(USB_DMA_REGION_BASE+0x2000))

/* Configure once before USB DMA starts; no MPU change with D-Cache off. */
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
  /* This region is reserved for USB; never overwrite another MPU user. */
  if ((MPU->RASR & MPU_RASR_ENABLE_Msk) != 0U) {
    MPU->RNR = saved_rnr;
    __set_PRIMASK(irq_state);
    Error_Handler();
    return;
  }

  /* USBD_Init may already have written the device handle with cache on.
   * Publish those writes before changing the region's memory attributes. */
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
  region.TypeExtField = MPU_TEX_LEVEL1; /* Normal, non-cacheable memory. */
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

/* Local aliases keep generated LL calls intact. Define the aliases after
 * the wrappers, so the wrappers call the original HAL entry points. */
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
