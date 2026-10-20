
#ifndef FAULT_MOCK_H
#define FAULT_MOCK_H
#include <stdint.h>
#include <stddef.h>
#define __attribute__(x)
#define FAULT_LOG_NATIVE_TEST 1
typedef struct {uint32_t RSR,HSICFGR,CRRCR,CSICFGR,CR,CFGR,CIFR,CIER,BDCR,CSR,D1CFGR,D2CFGR,D3CFGR,PLLCKSELR,PLLCFGR,PLL1DIVR,PLL1FRACR,PLL2DIVR,PLL2FRACR,PLL3DIVR,PLL3FRACR,D2CCIP2R;} mock_rcc_t;
typedef struct {uint32_t CFSR,HFSR,DFSR,AFSR,BFAR,MMFAR,SHCSR,ICSR,VTOR,ABFSR,AIRCR,CCR;} mock_scb_t;
typedef struct {uint32_t FPCCR;} mock_fpu_t;
extern mock_rcc_t mock_rcc;
extern mock_scb_t mock_scb;
extern mock_fpu_t mock_fpu;
#define RCC (&mock_rcc)
#define SCB (&mock_scb)
#define FPU (&mock_fpu)
#define RCC_RSR_RMVF (1UL<<16)
#define RCC_RSR_PORRSTF (1UL<<23)
#define RCC_RSR_BORRSTF (1UL<<21)
#define SCB_CFSR_MSTKERR_Msk (1UL<<4)
#define SCB_CFSR_MUNSTKERR_Msk (1UL<<3)
#define SCB_CFSR_MLSPERR_Msk (1UL<<5)
#define SCB_CFSR_STKERR_Msk (1UL<<12)
#define SCB_CFSR_UNSTKERR_Msk (1UL<<11)
#define SCB_CFSR_LSPERR_Msk (1UL<<13)
#define SCB_AIRCR_VECTKEY_Pos 16U
#define SCB_AIRCR_PRIGROUP_Msk (7UL<<8)
#define SCB_AIRCR_SYSRESETREQ_Msk (1UL<<2)
#define __DSB() ((void)0)
#define __ISB() ((void)0)
#define SCB_SHCSR_USGFAULTENA_Msk (1UL<<18)
void FaultLog_NativeTestTrap(void);
void mock_watchdog_wait(void);
#define __NOP() mock_watchdog_wait()
uint32_t __get_IPSR(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
uint32_t osKernelSysTick(void);
#define USBD_OK 0
#define USBD_BUSY 1
#define USBD_FAIL 2
extern volatile uint8_t g_log_link_ready;
extern volatile uint32_t cdc_tx_pending_count,cdc_tx_inflight;
typedef struct {uint32_t session;} log_link_diag_t;
typedef struct {uint32_t deinit_count;} cdc_tx_diag_t;
extern volatile log_link_diag_t g_log_link_diag;
extern volatile cdc_tx_diag_t g_cdc_tx_diag;
#endif

#define RCC_CIFR_HSECSSF (1UL<<10)
#define RCC_CIFR_LSECSSF (1UL<<9)
#define SCB_ICSR_NMIPENDSET_Msk (1UL<<31)
