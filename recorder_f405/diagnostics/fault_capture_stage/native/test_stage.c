
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include "mock.h"
mock_rcc_t mock_rcc; mock_scb_t mock_scb; mock_fpu_t mock_fpu;
volatile uint8_t g_log_link_ready;
volatile uint32_t cdc_tx_pending_count, cdc_tx_inflight;
volatile log_link_diag_t g_log_link_diag;
volatile cdc_tx_diag_t g_cdc_tx_diag;
static uint32_t mask_value;
static uint8_t usb_result;
static unsigned sent;
static char lines[32][768];
uint32_t __get_IPSR(void){return 3;}
uint32_t __get_PRIMASK(void){return mask_value;}
void __disable_irq(void){mask_value=1;}
void __set_PRIMASK(uint32_t x){mask_value=x;}
uint32_t osKernelSysTick(void){return 100;}
void FaultLog_NativeTestTrap(void){}
static jmp_buf reset_jump;
static unsigned capture_running, stop_stage, observed_mask;
void mock_watchdog_wait(void){if(capture_running)longjmp(reset_jump,1);abort();}
static void stage_barrier(void);
#undef __DSB
#define __DSB() stage_barrier()
uint8_t sys_printf(uint8_t level,const char *tag,const char *fmt,...){
    int n; va_list args;
    assert((level==0 && !strcmp(tag,"ERROR: ")) || (level==1 && !strcmp(tag,"WARN: ")));
    if(usb_result!=USBD_OK)return usb_result;
    assert(sent<32); va_start(args,fmt); n=vsnprintf(lines[sent],768,fmt,args);va_end(args);
    assert(n>0 && n<768);sent++;cdc_tx_pending_count++;return USBD_OK;
}

#include "fault_log.h"
#include "main.h"
#include "log.h"
#include "usbd_cdc_if.h"
#include "cmsis_os.h"
#include <stddef.h>

/* 由 fault_noinit.sct 放入 DTCM 顶部 UNINIT 区，普通启动清零不能覆盖。
 * DTCM 不经过 D-cache，故障后写入不依赖缓存清理。 */
__attribute__((section(".bss.noinit.fault_record"), aligned(8), used))
volatile fault_record_t g_fault_record;
__attribute__((section(".bss.noinit.fault_stack"), aligned(8), used))
uint8_t g_fault_emergency_stack[FAULT_LOG_STACK_BYTES];
/* 名称匹配链接脚本 fault_*，与记录共处 DTCM UNINIT，复位后不清零。 */
__attribute__((section(".bss.noinit.fault_stage_trace"), aligned(8), used))
volatile fault_capture_trace_t g_fault_capture_trace;
volatile fault_log_diag_t g_fault_log_diag;
volatile fault_boot_diag_t g_fault_boot_diag;
static uint32_t boot_report_session, boot_report_deinit;
static uint8_t boot_report_started;

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
/* 独立保留标记，不改变故障记录格式；看门狗复位和补报清除均不重新布防。 */
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

static void fault_commit(void)
{
    g_fault_record.checksum = fault_checksum(&g_fault_record);
    __DSB();
    g_fault_record.magic = FAULT_LOG_MAGIC;
    __DSB();
}

/* 每阶段一个对齐的 32 位写入，DSB 保证完成后才继续下一阶段。
 * 不调用计时、日志、RTOS 或喂狗；count/tick 使用已有软件值。 */
static void fault_capture_mark(uint32_t stage)
{
    g_fault_capture_trace.stage_word = 0x46530000UL | ((stage ^ 0xFFU) << 8) | stage;
    __DSB();
}

/* 普通初始化及补报清除继续使用 fault_commit，不覆盖捕获进度。 */
static void fault_capture_commit(void)
{
    fault_capture_mark(3U);
    g_fault_record.checksum = fault_checksum(&g_fault_record);
    __DSB();
    fault_capture_mark(4U);
    g_fault_record.magic = FAULT_LOG_MAGIC;
    __DSB();
    fault_capture_mark(5U);
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

/* 仅在启动时复制一次，必须早于 fault_empty；不在异常处理中增加日志或计算。 */
static void fault_boot_snapshot(uint32_t reset)
{
    uint32_t i, failed = 0U;
    volatile uint32_t *dst = (volatile uint32_t *)&g_fault_boot_diag.raw;
    const volatile uint32_t *src = (const volatile uint32_t *)&g_fault_record;
    const volatile fault_record_t *r = &g_fault_boot_diag.raw;
    g_fault_boot_diag.reset_after = reset;
    g_fault_boot_diag.cold_boot = (reset & (RCC_RSR_PORRSTF | RCC_RSR_BORRSTF)) != 0U;
    g_fault_boot_diag.raw_valid = 0U;
    g_fault_boot_diag.calculated_checksum = 0U;
    g_fault_boot_diag.check_failed = 0U;
    g_fault_boot_diag.part = 0U;
    g_fault_boot_diag.pending = 1U;
    boot_report_started = 0U;
    g_fault_boot_diag.capture_trace_valid = 0U;
    g_fault_boot_diag.capture_stage = 0U;
    g_fault_boot_diag.capture_mask = 0U;
    g_fault_boot_diag.capture_word = 0U;
    g_fault_boot_diag.capture_count = 0U;
    g_fault_boot_diag.capture_tick = 0U;
    if (g_fault_boot_diag.cold_boot) return;
    {
        uint32_t word = g_fault_capture_trace.stage_word;
        uint32_t stage = word & 0xFFU;
        g_fault_boot_diag.capture_word = word;
        g_fault_boot_diag.capture_count = g_fault_capture_trace.count;
        g_fault_boot_diag.capture_tick = g_fault_capture_trace.tick;
        if ((word & 0xFFFF0000UL) == 0x46530000UL && stage >= 1U && stage <= 5U &&
            ((word >> 8) & 0xFFU) == (stage ^ 0xFFU)) {
            g_fault_boot_diag.capture_trace_valid = 1U;
            g_fault_boot_diag.capture_stage = stage;
            g_fault_boot_diag.capture_mask = (1UL << stage) - 1U;
        }
    }
    for (i = 0U; i < sizeof(*r) / sizeof(uint32_t); ++i) dst[i] = src[i];
    g_fault_boot_diag.raw_valid = 1U;
    g_fault_boot_diag.calculated_checksum = fault_checksum(r);
    if (r->magic != FAULT_LOG_MAGIC) failed |= 1U;
    if (r->version != FAULT_LOG_VERSION) failed |= 2U;
    if (r->size != sizeof(*r)) failed |= 4U;
    if (r->pending > 1U) failed |= 8U;
    if (r->checksum != g_fault_boot_diag.calculated_checksum) failed |= 16U;
    g_fault_boot_diag.check_failed = failed;
}

void FaultLog_BootInit(void)
{
    uint32_t reset = RCC->RSR;
    uint32_t i;
    volatile uint32_t *retained = (volatile uint32_t *)&g_fault_record;
    uint32_t *copy = (uint32_t *)&replay_record;
    fault_boot_snapshot(reset);
    /* 快照完成后重新布防；冷启动仅写不读，避免读取新保留区的未初始化 ECC。 */
    g_fault_capture_trace.stage_word = 0U;
    g_fault_capture_trace.count = 0U;
    g_fault_capture_trace.tick = 0U;
    __DSB();
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
          (sp >= 0x24000000UL && sp <= 0x24050000UL - frame_bytes)))
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
    g_fault_capture_trace.stage_word = 0U;
    g_fault_capture_trace.count = next_fault_count;
    g_fault_capture_trace.tick = g_fault_log_diag.last_tick;
    fault_capture_mark(1U);
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
    fault_capture_mark(2U);
    fault_capture_commit();
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

/* 普通任务上下文补发启动判定；BUSY 不前进，发送中换会话则重发两段。
 * 队列排空仅表示 USB 发送完成，不等价于接收端 SD 已落盘。 */
static uint8_t fault_boot_service(void)
{
    uint8_t result;
    const volatile fault_record_t *r = &g_fault_boot_diag.raw;
    if (!g_fault_boot_diag.pending) return 0U;
    if (!g_log_link_ready) return 1U;
    if (!boot_report_started || boot_report_session != g_log_link_diag.session ||
        boot_report_deinit != g_cdc_tx_diag.deinit_count) {
        if (boot_report_started) g_fault_boot_diag.replay_restarts++;
        boot_report_started = 1U;
        boot_report_session = g_log_link_diag.session;
        boot_report_deinit = g_cdc_tx_diag.deinit_count;
        g_fault_boot_diag.part = 0U;
    }
    if (g_fault_boot_diag.part == 0U) {
        result = DEBUG_WARN("[fault_boot] part=1/2 reset=%08lX cold=%lu raw_valid=%lu check_failed=%08lX magic=%08lX version=%lu size=%lu stored_crc=%08lX calc_crc=%08lX pending=%lu count=%lu session=%lu\r\n",
            (unsigned long)g_fault_boot_diag.reset_after, (unsigned long)g_fault_boot_diag.cold_boot,
            (unsigned long)g_fault_boot_diag.raw_valid, (unsigned long)g_fault_boot_diag.check_failed,
            (unsigned long)r->magic, (unsigned long)r->version, (unsigned long)r->size,
            (unsigned long)r->checksum, (unsigned long)g_fault_boot_diag.calculated_checksum,
            (unsigned long)r->pending, (unsigned long)r->count, (unsigned long)boot_report_session);
    } else if (g_fault_boot_diag.part == 1U) {
        result = DEBUG_WARN("[fault_boot] part=2/2 exception=%lu fault_tick=%lu tick_valid=%lu pc=%08lX lr=%08lX cfsr=%08lX hfsr=%08lX msp=%08lX psp=%08lX exc_return=%08lX boot_valid=%lu boot_invalid=%lu replay_pending=%lu session=%lu capture_valid=%lu capture_stage=%lu capture_mask=%02lX capture_word=%08lX capture_count=%lu capture_tick=%lu\r\n",
            (unsigned long)r->exception, (unsigned long)r->tick, (unsigned long)r->tick_valid,
            (unsigned long)r->frame[6], (unsigned long)r->frame[5], (unsigned long)r->cfsr,
            (unsigned long)r->hfsr, (unsigned long)r->msp, (unsigned long)r->psp,
            (unsigned long)r->exc_return, (unsigned long)g_fault_log_diag.valid,
            (unsigned long)g_fault_log_diag.invalid, (unsigned long)g_fault_log_diag.pending,
            (unsigned long)boot_report_session, (unsigned long)g_fault_boot_diag.capture_trace_valid,
            (unsigned long)g_fault_boot_diag.capture_stage, (unsigned long)g_fault_boot_diag.capture_mask,
            (unsigned long)g_fault_boot_diag.capture_word, (unsigned long)g_fault_boot_diag.capture_count,
            (unsigned long)g_fault_boot_diag.capture_tick);
    } else {
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        if (g_log_link_ready && boot_report_session == g_log_link_diag.session &&
            boot_report_deinit == g_cdc_tx_diag.deinit_count &&
            cdc_tx_pending_count == 0U && cdc_tx_inflight == 0U) {
            g_fault_boot_diag.pending = 0U;
            g_fault_boot_diag.reports++;
        }
        __set_PRIMASK(mask);
        return g_fault_boot_diag.pending != 0U;
    }
    if (result == USBD_OK) g_fault_boot_diag.part++;
    else g_fault_boot_diag.retries++;
    return 1U;
}

uint8_t FaultLog_Service(void)
{
    uint8_t result = USBD_BUSY;
    uint32_t part;
    const fault_record_t *r = &replay_record;
    if (fault_boot_service()) return 1U;
    if (g_fault_log_diag.pending == 0U) return 0U;
    if (!g_log_link_ready) return 1U;
    if (!replay_started || replay_session != g_log_link_diag.session ||
        replay_deinit != g_cdc_tx_diag.deinit_count) {
        if (replay_started) g_fault_log_diag.replay_restarts++;
        replay_started = 1U;
        replay_session = g_log_link_diag.session;
        replay_deinit = g_cdc_tx_diag.deinit_count;
        g_fault_log_diag.part = 0U;
    }
    part = g_fault_log_diag.part;
    if (part == 0U) {
        result = DEBUG_ERROR("[hardfault] id=%lu part=1/3 exception=%lu fault_tick=%lu tick_valid=%lu send_tick=%lu reset_before=%08lX reset_after=%08lX stack_valid=%lu msp=%08lX psp=%08lX exc_return=%08lX pc=%08lX lr=%08lX xpsr=%08lX\r\n",
            (unsigned long)r->count, (unsigned long)r->exception, (unsigned long)r->tick,
            (unsigned long)r->tick_valid, (unsigned long)osKernelSysTick(),
            (unsigned long)r->reset_before, (unsigned long)g_fault_log_diag.reset_after,
            (unsigned long)r->stack_valid, (unsigned long)r->msp, (unsigned long)r->psp,
            (unsigned long)r->exc_return, (unsigned long)r->frame[6], (unsigned long)r->frame[5], (unsigned long)r->frame[7]);
    } else if (part == 1U) {
        result = DEBUG_ERROR("[hardfault] id=%lu part=2/3 r0=%08lX r1=%08lX r2=%08lX r3=%08lX r4=%08lX r5=%08lX r6=%08lX r7=%08lX r8=%08lX r9=%08lX r10=%08lX r11=%08lX r12=%08lX control=%08lX primask=%08lX basepri=%08lX faultmask=%08lX\r\n",
            (unsigned long)r->count, (unsigned long)r->frame[0], (unsigned long)r->frame[1],
            (unsigned long)r->frame[2], (unsigned long)r->frame[3],
            (unsigned long)r->callee[0], (unsigned long)r->callee[1], (unsigned long)r->callee[2], (unsigned long)r->callee[3],
            (unsigned long)r->callee[4], (unsigned long)r->callee[5], (unsigned long)r->callee[6], (unsigned long)r->callee[7],
            (unsigned long)r->frame[4], (unsigned long)r->control, (unsigned long)r->primask,
            (unsigned long)r->basepri, (unsigned long)r->faultmask);
    } else if (part == 2U) {
        result = DEBUG_ERROR("[hardfault] id=%lu part=3/3 cfsr=%08lX hfsr=%08lX dfsr=%08lX afsr=%08lX bfar=%08lX mmfar=%08lX shcsr=%08lX icsr=%08lX vtor=%08lX abfsr=%08lX fpccr=%08lX checksum=%08lX\r\n",
            (unsigned long)r->count, (unsigned long)r->cfsr, (unsigned long)r->hfsr,
            (unsigned long)r->dfsr, (unsigned long)r->afsr, (unsigned long)r->bfar,
            (unsigned long)r->mmfar, (unsigned long)r->shcsr, (unsigned long)r->icsr,
            (unsigned long)r->vtor, (unsigned long)r->abfsr, (unsigned long)r->fpccr, (unsigned long)r->checksum);
    } else {
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        /* 等待整个 FIFO 排空，且确认本轮未断开，避免仅凭入队成功清记录。 */
        if (g_log_link_ready && replay_session == g_log_link_diag.session &&
            replay_deinit == g_cdc_tx_diag.deinit_count && cdc_tx_pending_count == 0U && cdc_tx_inflight == 0U) {
            g_fault_record.magic = 0U;
            g_fault_record.pending = 0U;
            fault_commit();
            g_fault_log_diag.pending = 0U;
            g_fault_log_diag.reports++;
        }
        __set_PRIMASK(mask);
        return g_fault_log_diag.pending != 0U;
    }
    if (result == USBD_OK) g_fault_log_diag.part++;
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
    /* 从调度器 tick 起算 10 秒；未握手或仍在补报时延后，不丢弃待报故障。 */
    if (tick >= FAULT_LOG_TEST_DELAY_MS && g_log_link_ready &&
        g_fault_log_diag.pending == 0U && !fault_test_fired())
        FaultLog_TestTrigger();
#else
    (void)tick;
#endif
}

#if FAULT_LOG_ENABLE_TEST
void FaultLog_TestTrigger(void)
{
    if (fault_test_fired()) return;
    fault_test_inject();
}

static void fault_test_inject(void)
{
    /* 在触发前提交一次性标记，确保 IWDG 复位后可以正常补报而不循环注入。 */
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

static void reset_runtime(void){
    memset((void*)&g_fault_log_diag,0,sizeof(g_fault_log_diag));
    memset((void*)&g_fault_boot_diag,0,sizeof(g_fault_boot_diag));
    memset((void*)&g_log_link_diag,0,sizeof(g_log_link_diag));
    memset((void*)&g_cdc_tx_diag,0,sizeof(g_cdc_tx_diag));
    replay_started=0;boot_report_started=0;sent=0;usb_result=USBD_OK;
    cdc_tx_pending_count=0;cdc_tx_inflight=0;g_log_link_ready=0;mock_rcc.RSR=0;
}
static void boot_report(void){
    g_log_link_ready=1;
    assert(FaultLog_Service());assert(FaultLog_Service());
    assert(g_fault_boot_diag.part==2 && sent==2);
    assert(FaultLog_Service() && g_fault_boot_diag.pending);
    cdc_tx_pending_count=0;cdc_tx_inflight=0;
}
static void test_stages(void);
int main(void){
    uint32_t expected;
    test_stages();
    /* 冷启动即使 RAM 内有看似有效的旧记录，也不复制读取原记录。 */
    reset_runtime();fault_empty();g_fault_record.pending=1;fault_commit();
    mock_rcc.RSR=RCC_RSR_PORRSTF;FaultLog_BootInit();
    assert(g_fault_boot_diag.cold_boot && !g_fault_boot_diag.raw_valid);
    assert(!g_fault_boot_diag.raw.magic && !g_fault_boot_diag.calculated_checksum);
    assert(g_fault_log_diag.cold_boot && !g_fault_log_diag.pending);
    assert(FaultLog_Service() && !sent);boot_report();
    assert(!FaultLog_Service() && g_fault_boot_diag.reports==1);
    assert(strstr(lines[0],"cold=1 raw_valid=0"));

    /* 捕获实际观察到的半提交记录：清空后快照和计算结果仍可输出。 */
    reset_runtime();fault_empty();g_fault_record.pending=1;g_fault_record.count=7;
    g_fault_record.exception=3;g_fault_record.frame[6]=0x08001234;
    g_fault_record.magic=0;g_fault_record.checksum=0;expected=fault_checksum(&g_fault_record);
    FaultLog_BootInit();assert(g_fault_log_diag.invalid==1 && !g_fault_record.pending);
    assert(g_fault_boot_diag.raw.pending==1 && g_fault_boot_diag.raw.magic==0);
    assert(g_fault_boot_diag.raw.frame[6]==0x08001234);
    assert(g_fault_boot_diag.check_failed==17 && g_fault_boot_diag.calculated_checksum==expected);
    g_log_link_ready=1;usb_result=USBD_BUSY;
    assert(FaultLog_Service() && !sent && !g_fault_boot_diag.part && g_fault_boot_diag.retries==1);
    usb_result=USBD_OK;boot_report();assert(!FaultLog_Service());
    assert(strstr(lines[0],"check_failed=00000011") && strstr(lines[0],"pending=1 count=7"));
    assert(strstr(lines[1],"pc=08001234") && strstr(lines[1],"boot_invalid=1"));

    /* 全部失败位均按独立条件计算，不短路遗漏 checksum。 */
    reset_runtime();memset((void*)&g_fault_record,0,sizeof(g_fault_record));g_fault_record.pending=2;
    FaultLog_BootInit();assert(g_fault_boot_diag.check_failed==31);

    /* 有效记录保持完整三段补报；启动两段优先且不会清除待报记录。 */
    reset_runtime();fault_empty();g_fault_record.pending=1;g_fault_record.exception=3;fault_commit();
    expected=g_fault_record.checksum;FaultLog_BootInit();
    assert(g_fault_log_diag.valid && g_fault_log_diag.pending && !g_fault_boot_diag.check_failed);
    assert(g_fault_boot_diag.raw.checksum==expected);boot_report();
    assert(FaultLog_Service() && g_fault_boot_diag.reports==1 && g_fault_log_diag.part==1);
    assert(FaultLog_Service());assert(FaultLog_Service());
    assert(g_fault_log_diag.part==3 && sent==5 && strstr(lines[2],"part=1/3"));
    assert(FaultLog_Service() && !g_fault_log_diag.reports);
    cdc_tx_pending_count=0;assert(!FaultLog_Service());
    assert(g_fault_log_diag.reports==1 && !g_fault_record.pending && fault_valid(&g_fault_record));

    /* 两段发送中换会话、USB DeInit 均重发；直到新会话队列排空才结束。 */
    reset_runtime();fault_empty();FaultLog_BootInit();g_log_link_ready=1;
    assert(FaultLog_Service() && g_fault_boot_diag.part==1);
    g_log_link_diag.session++;
    assert(FaultLog_Service() && g_fault_boot_diag.part==1 && g_fault_boot_diag.replay_restarts==1);
    g_cdc_tx_diag.deinit_count++;
    assert(FaultLog_Service() && g_fault_boot_diag.part==1 && g_fault_boot_diag.replay_restarts==2);
    assert(FaultLog_Service());cdc_tx_pending_count=0;cdc_tx_inflight=1;
    assert(FaultLog_Service() && !g_fault_boot_diag.reports);
    cdc_tx_inflight=0;assert(!FaultLog_Service() && g_fault_boot_diag.reports==1 && !mask_value);
    puts("PASS: cold ECC-read avoidance, raw snapshot before clear, independent failure bits, checksum, offline/BUSY retry, two-part output, session/deinit replay, drain gate, intact HardFault replay");
    return 0;
}

static void stage_barrier(void){
    unsigned stage=g_fault_capture_trace.stage_word & 255U;
    if(capture_running && stage>=1 && stage<=5){
        observed_mask|=1U<<(stage-1);
        if(stop_stage==stage)longjmp(reset_jump,1);
    }
}
static void test_stages(void){
    static const uint32_t regs[12]={0};
    unsigned target;
    for(target=1;target<=5;target++){
        reset_runtime();fault_empty();
        mock_scb.CFSR=SCB_CFSR_STKERR_Msk; /* 跳过宿主不存在的目标地址。 */
        g_fault_log_diag.last_tick=12345;next_fault_count=9;
        observed_mask=0;stop_stage=target;capture_running=1;
        if(setjmp(reset_jump)==0)FaultLog_Capture(0,0,0xFFFFFFFD,regs);
        capture_running=0;
        assert(observed_mask==((1U<<target)-1U));
        assert((g_fault_capture_trace.stage_word & 255U)==target);
        assert(g_fault_capture_trace.count==9 && g_fault_capture_trace.tick==12345);
        if(target==2 || target==3){assert(g_fault_record.pending && !g_fault_record.magic && !g_fault_record.checksum);}
        if(target==4){assert(!g_fault_record.magic && g_fault_record.checksum==fault_checksum(&g_fault_record));}
        if(target==5){assert(fault_valid(&g_fault_record));}
        mock_rcc.RSR=0;FaultLog_BootInit();
        assert(g_fault_boot_diag.capture_trace_valid && g_fault_boot_diag.capture_stage==target);
        assert(g_fault_boot_diag.capture_mask==((1U<<target)-1U));
        assert(g_fault_boot_diag.capture_count==9 && g_fault_boot_diag.capture_tick==12345);
        assert(!g_fault_capture_trace.stage_word); /* 下次启动快照完成后重新布防。 */
        boot_report();
        {char expected[48];snprintf(expected,sizeof(expected),"capture_stage=%u",target);assert(strstr(lines[1],expected));}
    }
    /* 完整捕获直至等待看门狗，同样经过全部五阶段。 */
    reset_runtime();fault_empty();stop_stage=0;observed_mask=0;capture_running=1;
    if(setjmp(reset_jump)==0)FaultLog_Capture(0,0,0xFFFFFFFD,regs);
    capture_running=0;assert(observed_mask==31 && fault_valid(&g_fault_record));
    /* 普通清空和补报提交不能把阶段重写成5。 */
    fault_capture_mark(3);fault_empty();assert((g_fault_capture_trace.stage_word&255)==3);
    fault_commit();assert((g_fault_capture_trace.stage_word&255)==3);
    /* 错误反码不解析；冷启动忽略看似有效的旧阶段；无故障新启动不沿用旧阶段。 */
    reset_runtime();fault_empty();g_fault_capture_trace.stage_word=0x46530005;
    FaultLog_BootInit();assert(!g_fault_boot_diag.capture_trace_valid && !g_fault_boot_diag.capture_stage);
    fault_capture_mark(5);mock_rcc.RSR=RCC_RSR_PORRSTF;FaultLog_BootInit();
    assert(!g_fault_boot_diag.capture_trace_valid && !g_fault_boot_diag.capture_word);
    mock_rcc.RSR=0;FaultLog_BootInit();assert(!g_fault_boot_diag.capture_trace_valid);
    puts("PASS: interruption at each of five actual capture stages, retained snapshot/rearm, integrity, cold boot, normal commit isolation, actual log formatting");
}
