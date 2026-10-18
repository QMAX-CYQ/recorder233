#include <assert.h>
#include <stdio.h>
#include "stm32h7xx.h"
#include "dcache_handle.h"
TestSCB test_scb;
static unsigned clean_calls, invalidate_calls;
static uintptr_t last_address;
static int32_t last_size;
void SCB_CleanDCache_by_Addr(uint32_t *p,int32_t n){clean_calls++;last_address=(uintptr_t)p;last_size=n;}
void SCB_InvalidateDCache_by_Addr(uint32_t *p,int32_t n){invalidate_calls++;last_address=(uintptr_t)p;last_size=n;}
int main(void){
 CleanDCacheForDmaTx((void*)0x24000003,35);InvalidateDCacheForDmaRx((void*)0x24000003,35);
 assert(clean_calls==0 && invalidate_calls==0);
 test_scb.CCR=SCB_CCR_DC_Msk;
 CleanDCacheForDmaTx(0,32);CleanDCacheForDmaTx((void*)0x24000000,0);
 InvalidateDCacheForDmaRx(0,32);InvalidateDCacheForDmaRx((void*)0x24000000,0);
 assert(clean_calls==0 && invalidate_calls==0);
 CleanDCacheForDmaTx((void*)0x24000003,35);
 assert(clean_calls==1 && last_address==0x24000000 && last_size==64);
 InvalidateDCacheForDmaRx((void*)0x24000020,32);
 assert(invalidate_calls==1 && last_address==0x24000020 && last_size==32);
 CleanDCacheForDmaTx((void*)(UINTPTR_MAX-3),8);
 CleanDCacheForDmaTx((void*)0x24000000,0x80000000U);
 assert(clean_calls==1);
 puts("PASS: D-Cache off skips maintenance; null/zero/overflow guards; TX/RX cache-line ranges");return 0;
}
