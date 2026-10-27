#ifndef NMI_LOG_H
#define NMI_LOG_H
#include "fault_log.h"
#define NMI_LOG_MAGIC 0x4E4D494CUL
#define NMI_LOG_VERSION 1U
#define NMI_LOG_STACK_BYTES 1024U

typedef struct {
    fault_record_t context;
    uint32_t rcc_cr, rcc_cfgr, rcc_cifr, rcc_cier, rcc_bdcr, rcc_csr;
    uint32_t rcc_hsicfgr, rcc_crrcr, rcc_csicfgr;
    uint32_t rcc_d1cfgr, rcc_d2cfgr, rcc_d3cfgr;
    uint32_t rcc_pllckselr, rcc_pllcfgr;
    uint32_t rcc_pll1divr, rcc_pll1fracr, rcc_pll2divr, rcc_pll2fracr;
    uint32_t rcc_pll3divr, rcc_pll3fracr, rcc_d2ccip2r;
    uint32_t scb_ccr, interrupted_capture;
} nmi_record_t;
/* Both records, both stacks, alignment and optional test latch fit 4KB. */
typedef char nmi_retained_size_check[
    (sizeof(nmi_record_t) + sizeof(fault_record_t) + NMI_LOG_STACK_BYTES +
     FAULT_LOG_STACK_BYTES + 32U <= 4096U) ? 1 : -1];
typedef char nmi_asm_stack_size_check[(NMI_LOG_STACK_BYTES == 1024U) ? 1 : -1];

typedef struct {
    uint32_t pending, valid, invalid, cold_boot, reset_after;
    uint32_t reports, retries, replay_restarts, part;
} nmi_log_diag_t;
extern volatile nmi_record_t g_nmi_record;
extern volatile nmi_log_diag_t g_nmi_log_diag;
/* Called by the fault framework, not separately by application tasks. */
void NmiLog_BootInit(uint32_t reset);
uint8_t NmiLog_Service(void);
void NmiLog_TestService(void);
#if FAULT_LOG_ENABLE_TEST
/* Watch: write 1, consumed in log_task, then pend NMI without changing clocks. */
extern volatile uint32_t g_nmi_test_request;
void NmiLog_TestTrigger(void);
#endif
#endif
