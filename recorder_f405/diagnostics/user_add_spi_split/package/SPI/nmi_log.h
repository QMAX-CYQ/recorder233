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
/* 两份记录、两个应急栈、对齐及可选测试标记合计不超过 4 KB。 */
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
/* 由故障框架调用，应用任务无需单独调用。 */
void NmiLog_BootInit(uint32_t reset);
uint8_t NmiLog_Service(void);
void NmiLog_TestService(void);
#if FAULT_LOG_ENABLE_TEST
/* 调试器写 1 后，由日志任务消费请求并挂起 NMI，不改变时钟。 */
extern volatile uint32_t g_nmi_test_request;
void NmiLog_TestTrigger(void);
#endif
#endif
