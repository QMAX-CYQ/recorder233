from pathlib import Path
import shutil

base = Path(__file__).resolve().parent
src = Path('D:/RM/rm_main_gimbal/User/Support')
for name in ('fault_log.c', 'fault_log.h'):
    (base/'before').mkdir(exist_ok=True)
    shutil.copy2(src/name, base/'before'/name)
(base/'staged').mkdir(exist_ok=True)
h = (src/'fault_log.h').read_text(encoding='utf-8')
h = h.replace('/* main 最早期调用', '''/* 启动前记录快照；冷启动不读取未初始化的保留 RAM。
 * check_failed：bit0 magic、bit1 version、bit2 size、bit3 pending、bit4 checksum。
 * cold_boot=1 时 raw_valid=0、check_failed=0，不代表原记录通过校验。 */
typedef struct {
    uint32_t reset_after, cold_boot, raw_valid, check_failed, calculated_checksum;
    uint32_t pending, part, reports, retries, replay_restarts;
    fault_record_t raw;
} fault_boot_diag_t;
extern volatile fault_boot_diag_t g_fault_boot_diag;

/* main 最早期调用''')
c = (src/'fault_log.c').read_text(encoding='utf-8')
c = c.replace('volatile fault_log_diag_t g_fault_log_diag;', '''volatile fault_log_diag_t g_fault_log_diag;
volatile fault_boot_diag_t g_fault_boot_diag;
static uint32_t boot_report_session, boot_report_deinit;
static uint8_t boot_report_started;''')
c = c.replace('void FaultLog_BootInit(void)', '''/* 仅在启动时复制一次，必须早于 fault_empty；不在异常处理中增加日志或计算。 */
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
    if (g_fault_boot_diag.cold_boot) return;
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

void FaultLog_BootInit(void)''')
c = c.replace('    g_fault_log_diag.reset_after = reset;', '    fault_boot_snapshot(reset);\n    g_fault_log_diag.reset_after = reset;')
c = c.replace('uint8_t FaultLog_Service(void)', '''/* 普通任务上下文补发启动判定；BUSY 不前进，发送中换会话则重发两段。
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
        result = DEBUG_WARN("[fault_boot] part=1/2 reset=%08lX cold=%lu raw_valid=%lu check_failed=%08lX magic=%08lX version=%lu size=%lu stored_crc=%08lX calc_crc=%08lX pending=%lu count=%lu session=%lu\\r\\n",
            (unsigned long)g_fault_boot_diag.reset_after, (unsigned long)g_fault_boot_diag.cold_boot,
            (unsigned long)g_fault_boot_diag.raw_valid, (unsigned long)g_fault_boot_diag.check_failed,
            (unsigned long)r->magic, (unsigned long)r->version, (unsigned long)r->size,
            (unsigned long)r->checksum, (unsigned long)g_fault_boot_diag.calculated_checksum,
            (unsigned long)r->pending, (unsigned long)r->count, (unsigned long)boot_report_session);
    } else if (g_fault_boot_diag.part == 1U) {
        result = DEBUG_WARN("[fault_boot] part=2/2 exception=%lu fault_tick=%lu tick_valid=%lu pc=%08lX lr=%08lX cfsr=%08lX hfsr=%08lX msp=%08lX psp=%08lX exc_return=%08lX boot_valid=%lu boot_invalid=%lu replay_pending=%lu session=%lu\\r\\n",
            (unsigned long)r->exception, (unsigned long)r->tick, (unsigned long)r->tick_valid,
            (unsigned long)r->frame[6], (unsigned long)r->frame[5], (unsigned long)r->cfsr,
            (unsigned long)r->hfsr, (unsigned long)r->msp, (unsigned long)r->psp,
            (unsigned long)r->exc_return, (unsigned long)g_fault_log_diag.valid,
            (unsigned long)g_fault_log_diag.invalid, (unsigned long)g_fault_log_diag.pending,
            (unsigned long)boot_report_session);
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

uint8_t FaultLog_Service(void)''')
c = c.replace('    if (g_fault_log_diag.pending == 0U) return 0U;', '    if (fault_boot_service()) return 1U;\n    if (g_fault_log_diag.pending == 0U) return 0U;')
(base/'staged/fault_log.c').write_text(c, encoding='utf-8')
(base/'staged/fault_log.h').write_text(h, encoding='utf-8')
print('Prepared boot snapshot and deferred two-part report')
