#include "fault_log.h"
#include "main.h"
#include "fault_log_port.h"
#include "nmi_log.h"
#include <stddef.h>

/* fault_noinit.sct 为普通故障及 NMI 的记录和应急栈预留 4 KB。
 * DTCM 不经过缓存，AXI 保留区仍须单独配置缓存策略。 */
__attribute__((section(".bss.noinit.fault_record"), aligned(8), used))
volatile fault_record_t g_fault_record;
__attribute__((section(".bss.noinit.fault_stack"), aligned(8), used))
uint8_t g_fault_emergency_stack[FAULT_LOG_STACK_BYTES];
volatile fault_log_diag_t g_fault_log_diag;
volatile uint32_t g_fault_capture_active;

static fault_record_t replay_record;
static uint32_t next_fault_count = 1U;
static uint32_t replay_session, replay_deinit;
static uint8_t replay_started;

#if FAULT_LOG_ENABLE_TEST
#define FAULT_TEST_LATCH_MAGIC 0x46544F4EUL
/* 不带 noinit 属性，启动时正常清零，手动请求不会跨复位自动重放。 */
volatile uint32_t g_fault_test_request;
volatile uint32_t g_fault_test_state;
volatile uint32_t g_fault_test_remaining_ms;
static uint32_t fault_test_request_tick;
static void fault_test_inject(void);
/* 测试保留标记；不阻止热复位后的下一轮周期注入。 */
__attribute__((section(".bss.noinit.fault_test"), aligned(8), used))
volatile uint32_t g_fault_test_latch[2];

static uint8_t fault_test_fired(void)
{
    return g_fault_test_latch[0] == FAULT_TEST_LATCH_MAGIC &&
        g_fault_test_latch[1] == (uint32_t)~FAULT_TEST_LATCH_MAGIC;
}

static void fault_test_boot_init(uint32_t reset)
{
    g_fault_test_request = 0U;
    g_fault_test_state = 0U;
    g_fault_test_remaining_ms = 0U;
    fault_test_request_tick = 0U;
    /* 冷启动先写后读，避免读取未初始化 ECC 数据；热复位保留一次性标记。 */
    if ((reset & (RCC_RSR_PORRSTF | RCC_RSR_BORRSTF)) != 0U || !fault_test_fired()) {
        g_fault_test_latch[0] = 0U;
        g_fault_test_latch[1] = 0U;
        __DSB();
    }
}
#endif

/* 校验除 magic/checksum 外的全部 32 位字，最后提交 magic 防止半写记录。 */
static uint32_t fault_checksum(const volatile fault_record_t *record)
{
    const volatile uint32_t *words = (const volatile uint32_t *)record;
    uint32_t hash = 2166136261UL;
    uint32_t i;
    for (i = 1U; i < sizeof(*record) / sizeof(uint32_t); ++i) {
        if (i != offsetof(fault_record_t, checksum) / sizeof(uint32_t))
            hash = (hash ^ words[i]) * 16777619UL;
    }
    return hash;
}

static uint8_t fault_valid(const volatile fault_record_t *record)
{
    return record->magic == FAULT_LOG_MAGIC && record->version == FAULT_LOG_VERSION &&
        record->size == sizeof(*record) && record->pending <= 1U &&
        record->checksum == fault_checksum(record);
}

#if !FAULT_LOG_TEST_IRAM1
/* AXI SRAM 的 ECC 字宽为 64 位，按 ST 保留区说明读回并重写完整头部字。
 * 先处理 size/checksum，再处理作为最终提交标记的 magic/version。
 * may_alias 允许对 uint32_t 字段进行此处的完整 64 位字访问。 */
typedef uint64_t fault_ecc_word_t __attribute__((may_alias));
typedef char fault_header_layout_check[
    (offsetof(fault_record_t, magic) == 0U &&
     offsetof(fault_record_t, version) == 4U &&
     offsetof(fault_record_t, size) == 8U &&
     offsetof(fault_record_t, checksum) == 12U) ? 1 : -1];
#endif

static void fault_flush_record(void)
{
#if FAULT_LOG_TEST_IRAM1
    /* 按 RM0468 要求读回 DTCM/ITCM 数据，确保写入生效。
     * 逐字读回，包括最后写入的 checksum 和 magic；
     * volatile 保证即使不使用读出值，也会实际执行读取。 */
    const volatile uint32_t *words = (const volatile uint32_t *)&g_fault_record;
    uint32_t i;
    for (i = 0U; i < sizeof(g_fault_record) / sizeof(uint32_t); ++i)
        (void)words[i];
#else
    volatile fault_ecc_word_t *header =
        (volatile fault_ecc_word_t *)(uintptr_t)&g_fault_record;
    fault_ecc_word_t value;
    value = header[1];
    header[1] = value;
    value = header[0];
    header[0] = value;
#endif
    __DSB();
}

static void fault_commit(void)
{
    g_fault_record.checksum = fault_checksum(&g_fault_record);
    __DSB();
    g_fault_record.magic = FAULT_LOG_MAGIC;
    __DSB();
    fault_flush_record();
}

static void fault_empty(void)
{
    volatile uint32_t *words = (volatile uint32_t *)&g_fault_record;
    uint32_t i;
    for (i = 0U; i < sizeof(g_fault_record) / sizeof(uint32_t); ++i)
        words[i] = 0U;
    g_fault_record.version = FAULT_LOG_VERSION;
    g_fault_record.size = sizeof(g_fault_record);
    fault_commit();
}

void FaultLog_BootInit(void)
{
    uint32_t reset = RCC->RSR;
    uint32_t i;
    volatile uint32_t *retained = (volatile uint32_t *)&g_fault_record;
    uint32_t *copy = (uint32_t *)&replay_record;
    g_fault_capture_active = 0U;
    NmiLog_BootInit(reset);
    g_fault_log_diag.reset_after = reset;
#if FAULT_LOG_ENABLE_TEST
    fault_test_boot_init(reset);
#endif
    /* 冷启动先写后读，避免对未初始化 ECC RAM 作有效性探测。 */
    if ((reset & (RCC_RSR_PORRSTF | RCC_RSR_BORRSTF)) != 0U) {
        g_fault_log_diag.cold_boot = 1U;
        fault_empty();
    } else if (!fault_valid(&g_fault_record)) {
        g_fault_log_diag.invalid++;
        fault_empty();
    } else {
        g_fault_log_diag.valid = 1U;
    }
    next_fault_count = g_fault_record.count + 1U;
    if (next_fault_count == 0U) next_fault_count = 1U;
    if (g_fault_record.pending != 0U) {
        for (i = 0U; i < sizeof(replay_record) / sizeof(uint32_t); ++i)
            copy[i] = retained[i];
        g_fault_log_diag.pending = 1U;
    }
    /* 本次启动已保存复位原因，清除粘滞标记以便下次区分冷、热复位。 */
    RCC->RSR |= RCC_RSR_RMVF;
}

void FaultLog_Heartbeat(uint32_t tick)
{
    g_fault_log_diag.last_tick = tick;
    g_fault_log_diag.tick_valid = 1U;
}

/* 只允许读取当前链接配置使用的 DTCM/AXI 栈；异常栈错误时跳过解引用。 */
static uint8_t fault_frame_address(uint32_t sp, uint32_t exc_return, uint32_t cfsr,
                                  uint32_t *address)
{
    uint32_t frame_bytes;
    const uint32_t stack_errors = SCB_CFSR_MSTKERR_Msk | SCB_CFSR_MUNSTKERR_Msk |
        SCB_CFSR_MLSPERR_Msk | SCB_CFSR_STKERR_Msk | SCB_CFSR_UNSTKERR_Msk | SCB_CFSR_LSPERR_Msk;
    if ((cfsr & stack_errors) != 0U || (sp & 3U) != 0U)
        return 0U;
    switch (exc_return) {
    case 0xFFFFFFF1UL: case 0xFFFFFFF9UL: case 0xFFFFFFFDUL:
        frame_bytes = 32U; break;
    case 0xFFFFFFE1UL: case 0xFFFFFFE9UL: case 0xFFFFFFEDUL:
        /* PM0253 图 11：扩展帧的 R0 仍在 SP，浮点区位于核心帧之后。 */
        frame_bytes = 26U * sizeof(uint32_t); break;
    default: return 0U;
    }
    if (!((sp >= 0x20000000UL && sp <= 0x20020000UL - frame_bytes) ||
          (sp >= 0x24000000UL && sp <= 0x24050000UL - frame_bytes)))
        return 0U;
    *address = sp;
    return 1U;
}

/* 不使用静态临时状态，避免 NMI 打断故障采样时相互覆盖。 */
void FaultLog_SaveContext(volatile fault_record_t *record, uint32_t count,
                         uint32_t msp, uint32_t psp, uint32_t exc_return,
                         const uint32_t *entry_regs)
{
    uint32_t i, address;
    volatile uint32_t *words = (volatile uint32_t *)record;
    for (i = 0U; i < sizeof(*record) / sizeof(uint32_t); ++i)
        words[i] = 0U;
    record->version = FAULT_LOG_VERSION;
    record->size = sizeof(*record);
    record->pending = 1U;
    record->count = count;
    record->exception = __get_IPSR();
    record->tick = g_fault_log_diag.last_tick;
    record->tick_valid = g_fault_log_diag.tick_valid;
    record->reset_before = RCC->RSR;
    record->msp = msp;
    record->psp = psp;
    record->exc_return = exc_return;
    for (i = 0U; i < 8U; ++i) record->callee[i] = entry_regs[i];
    record->primask = entry_regs[8];
    record->control = entry_regs[9];
    record->basepri = entry_regs[10];
    record->faultmask = entry_regs[11];
    record->cfsr = SCB->CFSR;
    record->hfsr = SCB->HFSR;
    record->dfsr = SCB->DFSR;
    record->afsr = SCB->AFSR;
    record->bfar = SCB->BFAR;
    record->mmfar = SCB->MMFAR;
    record->shcsr = SCB->SHCSR;
    record->icsr = SCB->ICSR;
    record->vtor = SCB->VTOR;
    record->abfsr = SCB->ABFSR;
    record->fpccr = FPU->FPCCR;
    if (fault_frame_address((exc_return & 4U) != 0U ? psp : msp,
                             exc_return, record->cfsr, &address)) {
        const volatile uint32_t *frame = (const volatile uint32_t *)(uintptr_t)address;
        for (i = 0U; i < 8U; ++i) record->frame[i] = frame[i];
        record->stack_valid = 1U;
    }
}

/* 普通故障和 NMI 分别使用独立记录及应急栈。 */
__attribute__((noinline, noreturn))
void FaultLog_Capture(uint32_t msp, uint32_t psp, uint32_t exc_return,
                      const uint32_t *entry_regs)
{
    g_fault_capture_active = 1U;
    FaultLog_SaveContext(&g_fault_record, next_fault_count, msp, psp,
                         exc_return, entry_regs);
    fault_commit();
    /* 保留区数据的读回确认在 fault_commit() 内完成。 */
    /* 现场已提交到保留区，停留在异常上下文且不喂狗，等待 IWDG 复位。
     * 不请求软件复位，不调用 RTOS、SPI 或任何可能喂狗的系统服务。 */
    __DSB();
    for (;;) __NOP();
}

#ifndef FAULT_LOG_NATIVE_TEST
/* 无 C 函数序言，先保存原始栈地址和 EXC_RETURN，再切换到专用应急栈。
 * R4-R11 在入口保存，避免 C 编译器改写尚未压栈的寄存器。 */
__attribute__((naked, noreturn, used)) void FaultLog_ExceptionEntry(void)
{
    __asm volatile(
        "mrs r0, msp\n"
        "mrs r1, psp\n"
        "mov r2, lr\n"
        "ldr r3, =g_fault_emergency_stack + 1024\n"
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
        "b FaultLog_Capture\n");
}

#define FAULT_ENTRY(name) \
    __attribute__((naked, noreturn)) void name(void) { __asm volatile("b FaultLog_ExceptionEntry"); }
FAULT_ENTRY(HardFault_Handler)
FAULT_ENTRY(MemManage_Handler)
FAULT_ENTRY(BusFault_Handler)
FAULT_ENTRY(UsageFault_Handler)
#undef FAULT_ENTRY
#endif

uint8_t FaultLog_Service(void)
{
    uint8_t result = FAULT_LOG_PORT_BUSY;
    uint32_t part;
    const fault_record_t *r = &replay_record;
    if (NmiLog_Service()) return 1U;
    if (g_fault_log_diag.pending == 0U) return 0U;
    if (!FaultLog_PortLinkReady()) return 1U;
    if (!replay_started || replay_session != FaultLog_PortSession() ||
        replay_deinit != FaultLog_PortDisconnectCount()) {
        if (replay_started) g_fault_log_diag.replay_restarts++;
        replay_started = 1U;
        replay_session = FaultLog_PortSession();
        replay_deinit = FaultLog_PortDisconnectCount();
        g_fault_log_diag.part = 0U;
    }
    part = g_fault_log_diag.part;
    if (part == 0U) {
        result = FaultLog_PortError("[hardfault] id=%lu part=1/3 exception=%lu fault_tick=%lu tick_valid=%lu send_tick=%lu reset_before=%08lX reset_after=%08lX stack_valid=%lu msp=%08lX psp=%08lX exc_return=%08lX pc=%08lX lr=%08lX xpsr=%08lX\r\n",
            (unsigned long)r->count, (unsigned long)r->exception, (unsigned long)r->tick,
            (unsigned long)r->tick_valid, (unsigned long)FaultLog_PortTick(),
            (unsigned long)r->reset_before, (unsigned long)g_fault_log_diag.reset_after,
            (unsigned long)r->stack_valid, (unsigned long)r->msp, (unsigned long)r->psp,
            (unsigned long)r->exc_return, (unsigned long)r->frame[6], (unsigned long)r->frame[5], (unsigned long)r->frame[7]);
    } else if (part == 1U) {
        result = FaultLog_PortError("[hardfault] id=%lu part=2/3 r0=%08lX r1=%08lX r2=%08lX r3=%08lX r4=%08lX r5=%08lX r6=%08lX r7=%08lX r8=%08lX r9=%08lX r10=%08lX r11=%08lX r12=%08lX control=%08lX primask=%08lX basepri=%08lX faultmask=%08lX\r\n",
            (unsigned long)r->count, (unsigned long)r->frame[0], (unsigned long)r->frame[1],
            (unsigned long)r->frame[2], (unsigned long)r->frame[3],
            (unsigned long)r->callee[0], (unsigned long)r->callee[1], (unsigned long)r->callee[2], (unsigned long)r->callee[3],
            (unsigned long)r->callee[4], (unsigned long)r->callee[5], (unsigned long)r->callee[6], (unsigned long)r->callee[7],
            (unsigned long)r->frame[4], (unsigned long)r->control, (unsigned long)r->primask,
            (unsigned long)r->basepri, (unsigned long)r->faultmask);
    } else if (part == 2U) {
        result = FaultLog_PortError("[hardfault] id=%lu part=3/3 cfsr=%08lX hfsr=%08lX dfsr=%08lX afsr=%08lX bfar=%08lX mmfar=%08lX shcsr=%08lX icsr=%08lX vtor=%08lX abfsr=%08lX fpccr=%08lX checksum=%08lX\r\n",
            (unsigned long)r->count, (unsigned long)r->cfsr, (unsigned long)r->hfsr,
            (unsigned long)r->dfsr, (unsigned long)r->afsr, (unsigned long)r->bfar,
            (unsigned long)r->mmfar, (unsigned long)r->shcsr, (unsigned long)r->icsr,
            (unsigned long)r->vtor, (unsigned long)r->abfsr, (unsigned long)r->fpccr, (unsigned long)r->checksum);
    } else {
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        /* 等待整个 FIFO 排空，且确认本轮未断开，避免仅凭入队成功清记录。 */
        if (FaultLog_PortLinkReady() && replay_session == FaultLog_PortSession() &&
            replay_deinit == FaultLog_PortDisconnectCount() && FaultLog_PortTxIdle()) {
            g_fault_record.magic = 0U;
            g_fault_record.pending = 0U;
            fault_commit();
            g_fault_log_diag.pending = 0U;
            g_fault_log_diag.reports++;
        }
        __set_PRIMASK(mask);
        return g_fault_log_diag.pending != 0U;
    }
    if (result == FAULT_LOG_PORT_OK) g_fault_log_diag.part++;
    else g_fault_log_diag.retries++;
    return 1U;
}

void FaultLog_TestService(uint32_t tick)
{
    NmiLog_TestService();
#if FAULT_LOG_ENABLE_TEST
    uint32_t request = g_fault_test_request;
    if (request != 0U) {
        g_fault_test_request = 0U;
        if (request == 1U) {
            fault_test_request_tick = tick;
            g_fault_test_remaining_ms = FAULT_LOG_TEST_DELAY_MS;
            g_fault_test_state = 1U;
        } else if (request == 2U) {
            g_fault_test_state = 0U;
            g_fault_test_remaining_ms = 0U;
            return;
        }
    }
    if (g_fault_test_state == 1U) {
        uint32_t elapsed = (uint32_t)(tick - fault_test_request_tick);
        if (elapsed < FAULT_LOG_TEST_DELAY_MS) {
            g_fault_test_remaining_ms = FAULT_LOG_TEST_DELAY_MS - elapsed;
        } else {
            /* 手动请求允许重复注入，也允许在 SPI 卡住或补报等待时注入。 */
            g_fault_test_state = 2U;
            g_fault_test_remaining_ms = 0U;
            fault_test_inject();
        }
        return;
    }
    /* 周期自动测试：每次重启、链路就绪且补报完成后重新等待10秒。
     * 不覆盖仍待补报的现场；断链或重新进入补报时重置自动倒计时。 */
    if (g_fault_test_state == 2U) return;
    if (!FaultLog_PortLinkReady() || g_fault_log_diag.pending != 0U || g_nmi_log_diag.pending != 0U) {
        g_fault_test_state = 0U;
        g_fault_test_remaining_ms = 0U;
        return;
    }
    if (g_fault_test_state != 3U) {
        fault_test_request_tick = tick;
        g_fault_test_state = 3U;
        g_fault_test_remaining_ms = FAULT_LOG_TEST_DELAY_MS;
    } else {
        uint32_t elapsed = (uint32_t)(tick - fault_test_request_tick);
        if (elapsed < FAULT_LOG_TEST_DELAY_MS)
            g_fault_test_remaining_ms = FAULT_LOG_TEST_DELAY_MS - elapsed;
        else
            FaultLog_TestTrigger();
    }
#else
    (void)tick;
#endif
}

#if FAULT_LOG_ENABLE_TEST
void FaultLog_TestTrigger(void)
{
    if (g_fault_test_state == 2U) return;
    g_fault_test_state = 2U;
    g_fault_test_remaining_ms = 0U;
    fault_test_inject();
}

static void fault_test_inject(void)
{
    /* 保留原测试标记写入；热复位后先补报，再重新开始10秒倒计时。 */
    g_fault_test_latch[1] = (uint32_t)~FAULT_TEST_LATCH_MAGIC;
    __DSB();
    g_fault_test_latch[0] = FAULT_TEST_LATCH_MAGIC;
    __DSB();
    /* 禁用独立 UsageFault，使 UDF 的未定义指令异常升级为 HardFault。 */
    SCB->SHCSR &= ~SCB_SHCSR_USGFAULTENA_Msk;
    __DSB();
    __ISB();
#ifndef FAULT_LOG_NATIVE_TEST
    __asm volatile("udf #0");
#else
    FaultLog_NativeTestTrap();
#endif
}
#endif
