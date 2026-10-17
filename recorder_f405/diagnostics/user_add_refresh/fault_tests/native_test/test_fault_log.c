
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include "mock.h"
#include "fault_log.h"
mock_rcc_t mock_rcc;mock_scb_t mock_scb;mock_fpu_t mock_fpu;
volatile uint8_t g_log_link_ready;
volatile uint32_t cdc_tx_pending_count,cdc_tx_inflight;
volatile log_link_diag_t g_log_link_diag;
volatile cdc_tx_diag_t g_cdc_tx_diag;
static uint8_t usb_result;
static unsigned sent;
static char lines[16][768];
static jmp_buf reset_jump;
static uint32_t irq_mask;
static unsigned trap_count;
void FaultLog_NativeTestTrap(void){trap_count++;}
void mock_watchdog_wait(void){assert(mock_scb.AIRCR==0);longjmp(reset_jump,1);}
uint32_t __get_IPSR(void){return 3;}
uint32_t __get_PRIMASK(void){return irq_mask;}
void __disable_irq(void){irq_mask=1;}
void __set_PRIMASK(uint32_t value){irq_mask=value;}
uint32_t osKernelSysTick(void){return 100;}
uint8_t sys_printf(uint8_t level,const char *tag,const char *fmt,...)
{
    va_list args;int len;
    assert(level==0 && strcmp(tag,"ERROR: ")==0);
    if(usb_result!=USBD_OK)return usb_result;
    assert(sent<16);va_start(args,fmt);len=vsnprintf(lines[sent],sizeof(lines[0]),fmt,args);va_end(args);
    assert(len>0 && len<768);sent++;cdc_tx_pending_count++;return USBD_OK;
}

#include "fault_log.h"
#include "main.h"
#include "fault_log_port.h"
#include <stddef.h>

/* fault_noinit.sct selects the top 4KB of IRAM1/DTCM or IRAM2/AXI as UNINIT.
 * Current application does not enable D-cache; enabling it later requires
 * a non-cacheable MPU region or an explicit cache writeback strategy. */
__attribute__((section(".bss.noinit.fault_record"), aligned(8), used))
volatile fault_record_t g_fault_record;
__attribute__((section(".bss.noinit.fault_stack"), aligned(8), used))
uint8_t g_fault_emergency_stack[FAULT_LOG_STACK_BYTES];
volatile fault_log_diag_t g_fault_log_diag;

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
/* AXI SRAM ECC word is 64 bits. Flush the two aligned header words by
 * reading and writing the same complete word, per ST SRAM retention FAQ.
 * Flush size/checksum first, then magic/version (the final commit word).
 * may_alias permits this deliberate whole-word access to the uint32_t fields. */
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
    /* RM0468: read back programmed DTCM/ITCM data to make writes effective.
     * Read every word, including the final checksum and magic writes.
     * volatile guarantees actual loads even though their values are unused. */
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
    if (!((sp >= 0x20000000UL && sp <= 0x2001F000UL - frame_bytes) ||
          (sp >= 0x24000000UL && sp <= 0x2404F000UL - frame_bytes)))
        return 0U;
    *address = sp;
    return 1U;
}

/* 入口已切换应急 MSP；只做整数运算和直接寄存器访问，不调用系统服务。 */
__attribute__((noinline, noreturn))
void FaultLog_Capture(uint32_t msp, uint32_t psp, uint32_t exc_return,
                      const uint32_t *entry_regs)
{
    uint32_t i, address;
    volatile uint32_t *words = (volatile uint32_t *)&g_fault_record;
    for (i = 0U; i < sizeof(g_fault_record) / sizeof(uint32_t); ++i)
        words[i] = 0U;
    g_fault_record.version = FAULT_LOG_VERSION;
    g_fault_record.size = sizeof(g_fault_record);
    g_fault_record.pending = 1U;
    g_fault_record.count = next_fault_count;
    g_fault_record.exception = __get_IPSR();
    g_fault_record.tick = g_fault_log_diag.last_tick;
    g_fault_record.tick_valid = g_fault_log_diag.tick_valid;
    g_fault_record.reset_before = RCC->RSR;
    g_fault_record.msp = msp;
    g_fault_record.psp = psp;
    g_fault_record.exc_return = exc_return;
    for (i = 0U; i < 8U; ++i) g_fault_record.callee[i] = entry_regs[i];
    g_fault_record.primask = entry_regs[8];
    g_fault_record.control = entry_regs[9];
    g_fault_record.basepri = entry_regs[10];
    g_fault_record.faultmask = entry_regs[11];
    g_fault_record.cfsr = SCB->CFSR;
    g_fault_record.hfsr = SCB->HFSR;
    g_fault_record.dfsr = SCB->DFSR;
    g_fault_record.afsr = SCB->AFSR;
    g_fault_record.bfar = SCB->BFAR;
    g_fault_record.mmfar = SCB->MMFAR;
    g_fault_record.shcsr = SCB->SHCSR;
    g_fault_record.icsr = SCB->ICSR;
    g_fault_record.vtor = SCB->VTOR;
    g_fault_record.abfsr = SCB->ABFSR;
    g_fault_record.fpccr = FPU->FPCCR;
    if (fault_frame_address((exc_return & 4U) != 0U ? psp : msp,
                             exc_return, g_fault_record.cfsr, &address)) {
        const volatile uint32_t *frame = (const volatile uint32_t *)(uintptr_t)address;
        for (i = 0U; i < 8U; ++i) g_fault_record.frame[i] = frame[i];
        g_fault_record.stack_valid = 1U;
    }
    fault_commit();
    /* Retained-memory readback is performed inside fault_commit(). */
    /* 现场已提交到保留区，停留在异常上下文且不喂狗，等待 IWDG 复位。
     * 不请求软件复位，不调用 RTOS、USB 或任何可能喂狗的系统服务。 */
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
            /* 手动请求允许重复注入，也允许在 USB 卡住或补报等待时注入。 */
            g_fault_test_state = 2U;
            g_fault_test_remaining_ms = 0U;
            fault_test_inject();
        }
        return;
    }
    /* 周期自动测试：每次重启、链路就绪且补报完成后重新等待10秒。
     * 不覆盖仍待补报的现场；断链或重新进入补报时重置自动倒计时。 */
    if (g_fault_test_state == 2U) return;
    if (!FaultLog_PortLinkReady() || g_fault_log_diag.pending != 0U) {
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

static void make_pending(void)
{
    fault_empty();g_fault_record.pending=1;g_fault_record.count=7;
    g_fault_record.exception=3;g_fault_record.frame[6]=0x08001234;
    g_fault_record.cfsr=0x02000000;g_fault_record.tick=12345;g_fault_record.tick_valid=1;
    fault_commit();
}
static void warm_boot(void)
{
    memset((void*)&g_fault_log_diag,0,sizeof(g_fault_log_diag));
    memset(&replay_record,0,sizeof(replay_record));replay_started=0;
    mock_rcc.RSR=0x01000000;FaultLog_BootInit();
}
int main(void)
{
    unsigned i;uint32_t address,regs[12];uint32_t *ram;
    uint32_t saved_record[sizeof(fault_record_t)/sizeof(uint32_t)];
    for(i=0;i<sizeof(saved_record)/sizeof(saved_record[0]);i++)
        ((volatile uint32_t*)&g_fault_record)[i]=0x13570000U+i;
    memcpy(saved_record,(const void*)&g_fault_record,sizeof(saved_record));
    fault_flush_record();
    assert(memcmp(saved_record,(const void*)&g_fault_record,sizeof(saved_record))==0);
    assert(fault_frame_address(0x2404F000UL-32U,0xFFFFFFFDUL,0,&address));
    assert(!fault_frame_address(0x2404F000UL,0xFFFFFFFDUL,0,&address));
    assert(!fault_frame_address(0x2404FFF0UL,0xFFFFFFFDUL,0,&address));
    memset((void*)&g_fault_record,0xA5,sizeof(g_fault_record));
    mock_rcc.RSR=RCC_RSR_PORRSTF;FaultLog_BootInit();
    assert(fault_valid(&g_fault_record) && !g_fault_record.pending && g_fault_log_diag.cold_boot);
    make_pending();warm_boot();assert(g_fault_log_diag.pending && replay_record.count==7 && next_fault_count==8);
    assert(FaultLog_Service() && sent==0); /* 等待握手 */
    g_log_link_ready=1;usb_result=USBD_BUSY;
    assert(FaultLog_Service() && !g_fault_log_diag.part && g_fault_log_diag.retries==1);
    assert(g_fault_record.pending);
    usb_result=USBD_OK;
    for(i=0;i<3;i++)assert(FaultLog_Service());
    assert(sent==3 && g_fault_log_diag.part==3 && g_fault_record.pending);
    assert(strstr(lines[0],"part=1/3") && strstr(lines[0],"pc=08001234"));
    assert(strstr(lines[1],"part=2/3") && strstr(lines[2],"part=3/3"));
    assert(FaultLog_Service() && g_fault_record.pending); /* 入队不等于完成 */
    cdc_tx_pending_count=0;cdc_tx_inflight=1;assert(FaultLog_Service());
    cdc_tx_inflight=0;assert(!FaultLog_Service());
    assert(fault_valid(&g_fault_record) && !g_fault_record.pending && g_fault_log_diag.reports==1 && irq_mask==0);
    warm_boot();assert(!g_fault_log_diag.pending); /* 成功补报后的复位不再重复 */
    make_pending();warm_boot();assert(FaultLog_Service());
    g_log_link_ready=0;assert(FaultLog_Service() && g_fault_record.pending);
    g_log_link_ready=1;g_log_link_diag.session++;g_cdc_tx_diag.deinit_count++;
    assert(FaultLog_Service());assert(g_fault_log_diag.part==1 && g_fault_log_diag.replay_restarts==1);
    assert(strstr(lines[sent-1],"part=1/3"));
    /* 补报未完成又复位，记录仍可校验并再次补发。 */
    warm_boot();assert(g_fault_log_diag.pending && replay_record.count==7);
    g_fault_record.frame[6]^=1;warm_boot();assert(!g_fault_log_diag.pending && g_fault_log_diag.invalid==1);
    make_pending();mock_rcc.RSR=RCC_RSR_BORRSTF;FaultLog_BootInit();assert(!g_fault_record.pending);
    assert(fault_frame_address(0x24001000,0xFFFFFFFD,0,&address) && address==0x24001000);
    assert(fault_frame_address(0x20001000,0xFFFFFFED,0,&address) && address==0x20001000);
    assert(!fault_frame_address(0,0xFFFFFFFD,0,&address));
    assert(!fault_frame_address(0x2001F000,0xFFFFFFFD,0,&address));
    assert(!fault_frame_address(0x24001001,0xFFFFFFFD,0,&address));
    assert(!fault_frame_address(0x24001000,0xFFFFFFFD,SCB_CFSR_STKERR_Msk,&address));
    assert(!fault_frame_address(0x2404FFF0,0xFFFFFFED,0,&address));
    assert(!fault_frame_address(0xFFFFFFF0,0xFFFFFFED,0,&address));
    assert(!fault_frame_address(0x24001000,0x12345678,0,&address));
    /* 将模拟异常帧放在真实可解引用的低地址 RAM，运行实际保存函数。 */
    ram=(uint32_t*)VirtualAlloc((void*)0x24000000,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(ram==(void*)0x24000000);
    for(i=0;i<8;i++)ram[0x1000/4+i]=0x100+i;
    for(i=0;i<12;i++)regs[i]=0x200+i;
    mock_scb.CFSR=0;FaultLog_Heartbeat(9876);
    if(setjmp(reset_jump)==0)FaultLog_Capture(0x24002000,0x24001000,0xFFFFFFFD,regs);
    assert(fault_valid(&g_fault_record) && g_fault_record.pending && g_fault_record.stack_valid);
    assert(g_fault_record.frame[6]==0x106 && g_fault_record.callee[7]==0x207);
    assert(g_fault_record.tick==9876 && g_fault_record.tick_valid && g_fault_record.primask==0x208);
    warm_boot();assert(g_fault_log_diag.pending && replay_record.frame[6]==0x106);
    /* 扩展浮点帧的核心寄存器仍位于 SP+0，不应偏移 18 个字。 */
    for(i=8;i<26;i++)ram[0x1000/4+i]=0xBAD00000+i;
    if(setjmp(reset_jump)==0)FaultLog_Capture(0x24002000,0x24001000,0xFFFFFFED,regs);
    assert(g_fault_record.stack_valid && g_fault_record.frame[6]==0x106);
    mock_scb.CFSR=SCB_CFSR_STKERR_Msk;
    if(setjmp(reset_jump)==0)FaultLog_Capture(0,0,0xFFFFFFFD,regs);
    assert(fault_valid(&g_fault_record) && !g_fault_record.stack_valid && !g_fault_record.frame[6]);
    assert(VirtualFree(ram,0,MEM_RELEASE));

#if FAULT_LOG_ENABLE_TEST
    /* Automatic timer starts only after link/replay readiness, and repeats after warm reset. */
    memset((void*)&g_fault_log_diag,0,sizeof(g_fault_log_diag));
    mock_rcc.RSR=RCC_RSR_PORRSTF;FaultLog_BootInit();
    g_log_link_ready=0;FaultLog_TestService(50000);assert(trap_count==0 && !g_fault_test_state);
    g_log_link_ready=1;g_fault_log_diag.pending=1;FaultLog_TestService(50000);assert(trap_count==0);
    g_fault_log_diag.pending=0;FaultLog_TestService(50000);assert(g_fault_test_state==3);
    FaultLog_TestService(59999);assert(trap_count==0 && g_fault_test_remaining_ms==1);
    FaultLog_TestService(60000);assert(trap_count==1 && g_fault_test_state==2 && fault_test_fired());
    FaultLog_TestService(90000);assert(trap_count==1);
    for(i=0;i<20;i++) {
        if(setjmp(reset_jump)==0)FaultLog_Capture(0,0,0xFFFFFFFD,regs);
        warm_boot();assert(g_fault_log_diag.pending && fault_test_fired());
        g_log_link_ready=1;sent=0;usb_result=USBD_OK;cdc_tx_pending_count=0;cdc_tx_inflight=0;
        FaultLog_TestService(20000);assert(trap_count==i+1 && g_fault_test_state==0);
        assert(FaultLog_Service());assert(FaultLog_Service());assert(FaultLog_Service());
        assert(sent==3 && g_fault_log_diag.pending);
        cdc_tx_pending_count=0;assert(!FaultLog_Service());
        assert(!g_fault_log_diag.pending && g_fault_log_diag.reports==1);
        FaultLog_TestService(30000);assert(g_fault_test_state==3 && g_fault_test_remaining_ms==10000);
        FaultLog_TestService(39999);assert(trap_count==i+1 && g_fault_test_remaining_ms==1);
        FaultLog_TestService(40000);assert(trap_count==i+2 && g_fault_test_state==2);
    }
    /* Manual request remains available offline and while replay is pending. */
    g_log_link_ready=0;g_fault_log_diag.pending=1;
    g_fault_test_request=1;FaultLog_TestService(50000);
    assert(g_fault_test_state==1 && g_fault_test_remaining_ms==10000);
    FaultLog_TestService(59999);assert(trap_count==21);
    FaultLog_TestService(60000);assert(trap_count==22);
    g_fault_test_request=1;FaultLog_TestService(71000);
    g_fault_test_request=2;FaultLog_TestService(72000);assert(g_fault_test_state==0);
    FaultLog_TestService(90000);assert(trap_count==22);
    /* Link interruption restarts the auto timer; unsigned subtraction handles wraparound. */
    g_log_link_ready=1;g_fault_log_diag.pending=0;FaultLog_TestService(100000);
    FaultLog_TestService(105000);assert(g_fault_test_remaining_ms==5000);
    g_log_link_ready=0;FaultLog_TestService(106000);assert(g_fault_test_state==0);
    g_log_link_ready=1;FaultLog_TestService(0xFFFFFF00U);
    FaultLog_TestService(9743);assert(trap_count==22 && g_fault_test_remaining_ms==1);
    FaultLog_TestService(9744);assert(trap_count==23);

#else
    FaultLog_TestService(10000);assert(trap_count==0);
#endif

    puts("PASS: 20 repeated warm-reset/capture/three-part-replay/10s-rearm cycles; offline/pending gate, manual requests, disconnect restart, tick wraparound; cold/warm reset, checksum rejection, actual capture and watchdog wait without software reset, safe/FP stack decoding, invalid-stack fallback, READY gate, BUSY retry, three-part replay, drain-before-clear, disconnect replay, repeated reboot retention");
    return 0;
}
