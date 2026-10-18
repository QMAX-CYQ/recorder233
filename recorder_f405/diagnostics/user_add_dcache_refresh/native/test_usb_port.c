#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
typedef struct {uint32_t CCR;} SCB_Type;
typedef struct {uint32_t CTRL,RNR,RASR;} MPU_Type;
static SCB_Type scb;static MPU_Type mpu;
#define SCB (&scb)
#define MPU (&mpu)
#define SCB_CCR_DC_Msk (1U<<16)
#define MPU_RASR_ENABLE_Msk 1U
#define MPU_CTRL_ENABLE_Msk 1U
#define MPU_CTRL_PRIVDEFENA_Msk 4U
#define MPU_REGION_ENABLE 1U
#define MPU_REGION_SIZE_16KB 13U
#define MPU_TEX_LEVEL1 1U
#define MPU_REGION_FULL_ACCESS 3U
#define MPU_INSTRUCTION_ACCESS_DISABLE 1U
#define MPU_ACCESS_SHAREABLE 1U
#define MPU_ACCESS_NOT_CACHEABLE 0U
#define MPU_ACCESS_NOT_BUFFERABLE 0U
typedef struct {uint32_t Enable,Number,BaseAddress,Size,SubRegionDisable,TypeExtField,AccessPermission,DisableExec,IsShareable,IsCacheable,IsBufferable;} MPU_Region_InitTypeDef;
typedef struct {int unused;} PCD_HandleTypeDef;
typedef int HAL_StatusTypeDef;
static uint32_t primask;static unsigned dsb,isb,clean,configs,errors,tx,rx;
static MPU_Region_InitTypeDef last_region;
static uint32_t __get_PRIMASK(void){return primask;}
static void __disable_irq(void){primask=1;}
static void __set_PRIMASK(uint32_t x){primask=x;}
static void __DSB(void){dsb++;}static void __ISB(void){isb++;}
static void Error_Handler(void){errors++;}
static void SCB_CleanInvalidateDCache_by_Addr(uint32_t *p,int32_t n){assert(primask==1 && (uintptr_t)p==0x24048000 && n==0x4000);clean++;}
static void HAL_MPU_ConfigRegion(MPU_Region_InitTypeDef *p){assert(primask==1 && mpu.CTRL==0);last_region=*p;configs++;}
static HAL_StatusTypeDef HAL_PCD_EP_Transmit(PCD_HandleTypeDef *p,uint8_t ep,uint8_t *b,uint32_t n){assert(p && b && ep==0x81 && n==64 && dsb);tx++;return 7;}
static HAL_StatusTypeDef HAL_PCD_EP_Receive(PCD_HandleTypeDef *p,uint8_t ep,uint8_t *b,uint32_t n){assert(p && b && ep==1 && n==64 && dsb);rx++;return 8;}
#include "usb_dma_port_tested.h"
int main(int argc,char**argv){
 int mode=argc>1?atoi(argv[1]):0;PCD_HandleTypeDef p;uint8_t b[64];
 mpu.RNR=3;primask=mode==2?1:0;mpu.CTRL=mode==2?3:0;
 if(mode)scb.CCR=SCB_CCR_DC_Msk;
 if(mode==3)mpu.RASR=1;
 USB_DMA_ConfigMemory();
 if(mode==0){assert(!clean && !configs && !errors && mpu.CTRL==0 && mpu.RNR==3 && primask==0);}
 else if(mode==3){assert(errors==1 && !clean && !configs && mpu.RNR==3 && primask==0);}
 else{assert(clean==1 && configs==1 && !errors && mpu.RNR==3);assert(mpu.CTRL==(mode==2?3:5));assert(primask==(mode==2?1:0));assert(last_region.Number==7 && last_region.BaseAddress==0x24048000 && last_region.Size==13 && !last_region.IsCacheable && !last_region.IsBufferable && last_region.TypeExtField==1);USB_DMA_ConfigMemory();assert(clean==1 && configs==1);}
 assert(HAL_PCD_EP_Transmit(&p,0x81,b,64)==7);assert(HAL_PCD_EP_Receive(&p,1,b,64)==8);assert(tx==1 && rx==1);
 printf("PASS: USB port scenario %d (off/on/preserve/conflict), DMA barriers and return values\n",mode);return 0;
}
