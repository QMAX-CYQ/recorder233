#include "nmi_log.h"
#include "main.h"
#include "fault_log_port.h"
#include <stddef.h>

__attribute__((section(".bss.noinit.fault_nmi_record"), aligned(8), used))
volatile nmi_record_t g_nmi_record;
__attribute__((section(".bss.noinit.fault_nmi_stack"), aligned(8), used))
uint8_t g_nmi_emergency_stack[NMI_LOG_STACK_BYTES];
volatile nmi_log_diag_t g_nmi_log_diag;
static nmi_record_t nmi_replay;
static uint32_t nmi_next_count = 1U, nmi_session, nmi_deinit;
static uint8_t nmi_started;

static uint32_t nmi_checksum(const volatile nmi_record_t *r)
{
    const volatile uint32_t *w = (const volatile uint32_t *)r;
    uint32_t i, hash = 2166136261UL;
    for (i = 1U; i < sizeof(*r) / 4U; ++i)
        if (i != offsetof(fault_record_t, checksum) / 4U)
            hash = (hash ^ w[i]) * 16777619UL;
    return hash;
}
static void nmi_commit(void)
{
    uint32_t i;
    g_nmi_record.context.checksum = nmi_checksum(&g_nmi_record);
    __DSB();
    g_nmi_record.context.magic = NMI_LOG_MAGIC;
    __DSB();
#if FAULT_LOG_TEST_IRAM1
    /* Readback makes DTCM writes effective before watchdog reset. */
    for (i = 0U; i < sizeof(g_nmi_record) / 4U; ++i)
        (void)((const volatile uint32_t *)&g_nmi_record)[i];
#else
    /* Whole ECC words for the final header writes; AXI cache policy is external. */
    typedef uint64_t nmi_ecc_word_t __attribute__((may_alias));
    volatile nmi_ecc_word_t *h = (volatile nmi_ecc_word_t *)(uintptr_t)&g_nmi_record;
    nmi_ecc_word_t value;
    (void)i;
    value = h[1]; h[1] = value;
    value = h[0]; h[0] = value;
#endif
    __DSB();
}
static void nmi_empty(void)
{
    uint32_t i;
    for (i = 0U; i < sizeof(g_nmi_record) / 4U; ++i)
        ((volatile uint32_t *)&g_nmi_record)[i] = 0U;
    g_nmi_record.context.version = NMI_LOG_VERSION;
    g_nmi_record.context.size = sizeof(g_nmi_record);
    nmi_commit();
}
void NmiLog_BootInit(uint32_t reset)
{
    uint32_t i;
    nmi_started = 0U;
    for (i = 0U; i < sizeof(g_nmi_log_diag) / 4U; ++i)
        ((volatile uint32_t *)&g_nmi_log_diag)[i] = 0U;
    g_nmi_log_diag.reset_after = reset;
    if ((reset & (RCC_RSR_PORRSTF | RCC_RSR_BORRSTF)) != 0U) {
        g_nmi_log_diag.cold_boot = 1U;
        nmi_empty();
    } else if (g_nmi_record.context.magic != NMI_LOG_MAGIC ||
               g_nmi_record.context.version != NMI_LOG_VERSION ||
               g_nmi_record.context.size != sizeof(g_nmi_record) ||
               g_nmi_record.context.pending > 1U ||
               g_nmi_record.context.checksum != nmi_checksum(&g_nmi_record)) {
        g_nmi_log_diag.invalid = 1U;
        nmi_empty();
    } else g_nmi_log_diag.valid = 1U;
    nmi_next_count = g_nmi_record.context.count + 1U;
    if (nmi_next_count == 0U) nmi_next_count = 1U;
    if (g_nmi_record.context.pending != 0U) {
        for (i = 0U; i < sizeof(nmi_replay) / 4U; ++i)
            ((uint32_t *)&nmi_replay)[i] = ((volatile uint32_t *)&g_nmi_record)[i];
        g_nmi_log_diag.pending = 1U;
    }
}

/* No HAL RCC handler: capture CSS flags before any write-to-clear operation.
 * CSS hardware may already have switched the clock when this code runs. */
__attribute__((noinline, noreturn))
void NmiLog_Capture(uint32_t msp, uint32_t psp, uint32_t exc_return,
                    const uint32_t *entry_regs)
{
    g_nmi_record.context.magic = 0U;
    __DSB();
    g_nmi_record.rcc_cr = RCC->CR;
    g_nmi_record.rcc_hsicfgr = RCC->HSICFGR;
    g_nmi_record.rcc_crrcr = RCC->CRRCR;
    g_nmi_record.rcc_csicfgr = RCC->CSICFGR;
    g_nmi_record.rcc_cfgr = RCC->CFGR;
    g_nmi_record.rcc_cifr = RCC->CIFR;
    g_nmi_record.rcc_cier = RCC->CIER;
    g_nmi_record.rcc_bdcr = RCC->BDCR;
    g_nmi_record.rcc_csr = RCC->CSR;
    g_nmi_record.rcc_d1cfgr = RCC->D1CFGR;
    g_nmi_record.rcc_d2cfgr = RCC->D2CFGR;
    g_nmi_record.rcc_d3cfgr = RCC->D3CFGR;
    g_nmi_record.rcc_pllckselr = RCC->PLLCKSELR;
    g_nmi_record.rcc_pllcfgr = RCC->PLLCFGR;
    g_nmi_record.rcc_pll1divr = RCC->PLL1DIVR;
    g_nmi_record.rcc_pll1fracr = RCC->PLL1FRACR;
    g_nmi_record.rcc_pll2divr = RCC->PLL2DIVR;
    g_nmi_record.rcc_pll2fracr = RCC->PLL2FRACR;
    g_nmi_record.rcc_pll3divr = RCC->PLL3DIVR;
    g_nmi_record.rcc_pll3fracr = RCC->PLL3FRACR;
    g_nmi_record.rcc_d2ccip2r = RCC->D2CCIP2R;
    g_nmi_record.scb_ccr = SCB->CCR;
    g_nmi_record.interrupted_capture = g_fault_capture_active;
    FaultLog_SaveContext(&g_nmi_record.context, nmi_next_count, msp, psp,
                         exc_return, entry_regs);
    g_nmi_record.context.version = NMI_LOG_VERSION;
    g_nmi_record.context.size = sizeof(g_nmi_record);
    nmi_commit();
    /* Fatal policy matches the previous NMI handler. IWDG must be enabled
     * for automatic reset/replay; no RTOS, USB, printf or watchdog refresh. */
    for (;;) __NOP();
}
#ifndef FAULT_LOG_NATIVE_TEST
__attribute__((naked, noreturn, used)) void NMI_Handler(void)
{
    __asm volatile(
        "mrs r0, msp\n"
        "mrs r1, psp\n"
        "mov r2, lr\n"
        "ldr r3, =g_nmi_emergency_stack + 1024\n"
        "msr msp, r3\n"
        "isb\n"
        "sub sp, sp, #48\n"
        "stmia sp, {r4-r11}\n"
        "mrs r3, primask\n"
        "str r3, [sp, #32]\n"
        "cpsid i\n"
        "mrs r3, control\n"
        "str r3, [sp, #36]\n"
        "mrs r3, basepri\n"
        "str r3, [sp, #40]\n"
        "mrs r3, faultmask\n"
        "str r3, [sp, #44]\n"
        "mov r3, sp\n"
        "b NmiLog_Capture\n");
}
#endif

uint8_t NmiLog_Service(void)
{
    const nmi_record_t *n = &nmi_replay;
    const fault_record_t *r = &n->context;
    uint8_t result;
    if (!g_nmi_log_diag.pending) return 0U;
    if (!FaultLog_PortLinkReady()) return 1U;
    if (!nmi_started || nmi_session != FaultLog_PortSession() ||
        nmi_deinit != FaultLog_PortDisconnectCount()) {
        if (nmi_started) g_nmi_log_diag.replay_restarts++;
        nmi_started = 1U;
        nmi_session = FaultLog_PortSession();
        nmi_deinit = FaultLog_PortDisconnectCount();
        g_nmi_log_diag.part = 0U;
    }
    if (g_nmi_log_diag.part == 0U) {
        result = FaultLog_PortError("[nmi] id=%lu part=1/4 fault_tick=%08lX tick_valid=%08lX reset_before=%08lX reset_after=%08lX hse_css=%08lX lse_css=%08lX cr=%08lX cfgr=%08lX cifr=%08lX cier=%08lX bdcr=%08lX csr=%08lX hsicfgr=%08lX crrcr=%08lX csicfgr=%08lX\r\n",
            (unsigned long)r->count,
            (unsigned long)r->tick,
            (unsigned long)r->tick_valid,
            (unsigned long)r->reset_before,
            (unsigned long)g_nmi_log_diag.reset_after,
            (unsigned long)((n->rcc_cifr & RCC_CIFR_HSECSSF) != 0U),
            (unsigned long)((n->rcc_cifr & RCC_CIFR_LSECSSF) != 0U),
            (unsigned long)n->rcc_cr,
            (unsigned long)n->rcc_cfgr,
            (unsigned long)n->rcc_cifr,
            (unsigned long)n->rcc_cier,
            (unsigned long)n->rcc_bdcr,
            (unsigned long)n->rcc_csr,
            (unsigned long)n->rcc_hsicfgr,
            (unsigned long)n->rcc_crrcr,
            (unsigned long)n->rcc_csicfgr);
    } else if (g_nmi_log_diag.part == 1U) {
        result = FaultLog_PortError("[nmi] id=%lu part=2/4 d1cfgr=%08lX d2cfgr=%08lX d3cfgr=%08lX pllckselr=%08lX pllcfgr=%08lX pll1divr=%08lX pll1fracr=%08lX pll2divr=%08lX pll2fracr=%08lX pll3divr=%08lX pll3fracr=%08lX d2ccip2r=%08lX ccr=%08lX\r\n",
            (unsigned long)r->count,
            (unsigned long)n->rcc_d1cfgr,
            (unsigned long)n->rcc_d2cfgr,
            (unsigned long)n->rcc_d3cfgr,
            (unsigned long)n->rcc_pllckselr,
            (unsigned long)n->rcc_pllcfgr,
            (unsigned long)n->rcc_pll1divr,
            (unsigned long)n->rcc_pll1fracr,
            (unsigned long)n->rcc_pll2divr,
            (unsigned long)n->rcc_pll2fracr,
            (unsigned long)n->rcc_pll3divr,
            (unsigned long)n->rcc_pll3fracr,
            (unsigned long)n->rcc_d2ccip2r,
            (unsigned long)n->scb_ccr);
    } else if (g_nmi_log_diag.part == 2U) {
        result = FaultLog_PortError("[nmi] id=%lu part=3/4 exception=%08lX fault_capture=%08lX stack_valid=%08lX msp=%08lX psp=%08lX exc_return=%08lX pc=%08lX lr=%08lX xpsr=%08lX r0=%08lX r1=%08lX r2=%08lX r3=%08lX r4=%08lX r5=%08lX r6=%08lX r7=%08lX r8=%08lX r9=%08lX r10=%08lX r11=%08lX r12=%08lX\r\n",
            (unsigned long)r->count,
            (unsigned long)r->exception,
            (unsigned long)n->interrupted_capture,
            (unsigned long)r->stack_valid,
            (unsigned long)r->msp,
            (unsigned long)r->psp,
            (unsigned long)r->exc_return,
            (unsigned long)r->frame[6],
            (unsigned long)r->frame[5],
            (unsigned long)r->frame[7],
            (unsigned long)r->frame[0],
            (unsigned long)r->frame[1],
            (unsigned long)r->frame[2],
            (unsigned long)r->frame[3],
            (unsigned long)r->callee[0],
            (unsigned long)r->callee[1],
            (unsigned long)r->callee[2],
            (unsigned long)r->callee[3],
            (unsigned long)r->callee[4],
            (unsigned long)r->callee[5],
            (unsigned long)r->callee[6],
            (unsigned long)r->callee[7],
            (unsigned long)r->frame[4]);
    } else if (g_nmi_log_diag.part == 3U) {
        result = FaultLog_PortError("[nmi] id=%lu part=4/4 control=%08lX primask=%08lX basepri=%08lX faultmask=%08lX cfsr=%08lX hfsr=%08lX dfsr=%08lX afsr=%08lX bfar=%08lX mmfar=%08lX shcsr=%08lX icsr=%08lX vtor=%08lX abfsr=%08lX fpccr=%08lX checksum=%08lX\r\n",
            (unsigned long)r->count,
            (unsigned long)r->control,
            (unsigned long)r->primask,
            (unsigned long)r->basepri,
            (unsigned long)r->faultmask,
            (unsigned long)r->cfsr,
            (unsigned long)r->hfsr,
            (unsigned long)r->dfsr,
            (unsigned long)r->afsr,
            (unsigned long)r->bfar,
            (unsigned long)r->mmfar,
            (unsigned long)r->shcsr,
            (unsigned long)r->icsr,
            (unsigned long)r->vtor,
            (unsigned long)r->abfsr,
            (unsigned long)r->fpccr,
            (unsigned long)r->checksum);
    } else {
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        if (FaultLog_PortLinkReady() && nmi_session == FaultLog_PortSession() &&
            nmi_deinit == FaultLog_PortDisconnectCount() && FaultLog_PortTxIdle()) {
            g_nmi_record.context.magic = 0U;
            g_nmi_record.context.pending = 0U;
            nmi_commit();
            g_nmi_log_diag.pending = 0U;
            g_nmi_log_diag.reports++;
        }
        __set_PRIMASK(mask);
        return g_nmi_log_diag.pending != 0U;
    }
    if (result == FAULT_LOG_PORT_OK) g_nmi_log_diag.part++;
    else g_nmi_log_diag.retries++;
    return 1U;
}
#if FAULT_LOG_ENABLE_TEST
volatile uint32_t g_nmi_test_request;
void NmiLog_TestTrigger(void)
{
    SCB->ICSR = SCB_ICSR_NMIPENDSET_Msk;
    __DSB();
    __ISB();
}
#endif
void NmiLog_TestService(void)
{
#if FAULT_LOG_ENABLE_TEST
    uint32_t request = g_nmi_test_request;
    g_nmi_test_request = 0U;
    if (request == 1U) NmiLog_TestTrigger();
#endif
}
