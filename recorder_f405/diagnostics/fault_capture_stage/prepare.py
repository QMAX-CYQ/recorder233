from pathlib import Path
import shutil

base=Path(__file__).resolve().parent
src=Path('D:/RM/rm_main_gimbal/User/Support')
(base/'before').mkdir(exist_ok=True)
(base/'staged').mkdir(exist_ok=True)
for name in ('fault_log.c','fault_log.h'):
    shutil.copy2(src/name,base/'before'/name)
h=(src/'fault_log.h').read_text(encoding='utf-8')
h=h.replace('/* 启动前记录快照', '''/* 与故障记录独立的保留区。stage_word 将标识、阶段及其反码合在单个字中，
 * 避免阶段和反码分别写入时复位造成配对不一致。阶段：1进入、2字段完成、
 * 3开始校验、4校验和已提交、5 magic 已提交。只保留最近一次捕获。 */
typedef struct {
    uint32_t stage_word, count, tick;
} fault_capture_trace_t;
extern volatile fault_capture_trace_t g_fault_capture_trace;

/* 启动前记录快照''')
h=h.replace('    fault_record_t raw;','''    uint32_t capture_trace_valid, capture_stage, capture_mask, capture_word;
    uint32_t capture_count, capture_tick;
    fault_record_t raw;''')
c=(src/'fault_log.c').read_text(encoding='utf-8')
c=c.replace('volatile fault_log_diag_t g_fault_log_diag;', '''/* 名称匹配链接脚本 fault_*，与记录共处 DTCM UNINIT，复位后不清零。 */
__attribute__((section(".bss.noinit.fault_stage_trace"), aligned(8), used))
volatile fault_capture_trace_t g_fault_capture_trace;
volatile fault_log_diag_t g_fault_log_diag;''')
anchor='static void fault_empty(void)'
c=c.replace(anchor,'''/* 每阶段一个对齐的 32 位写入，DSB 保证完成后才继续下一阶段。
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

'''+anchor)
c=c.replace('    if (g_fault_boot_diag.cold_boot) return;', '''    g_fault_boot_diag.capture_trace_valid = 0U;
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
    }''')
c=c.replace('    fault_boot_snapshot(reset);','''    fault_boot_snapshot(reset);
    /* 快照完成后重新布防；冷启动仅写不读，避免读取新保留区的未初始化 ECC。 */
    g_fault_capture_trace.stage_word = 0U;
    g_fault_capture_trace.count = 0U;
    g_fault_capture_trace.tick = 0U;
    __DSB();''')
c=c.replace('    for (i = 0U; i < sizeof(g_fault_record) / sizeof(uint32_t); ++i)\n        words[i] = 0U;\n    g_fault_record.version = FAULT_LOG_VERSION;', '''    for (i = 0U; i < sizeof(g_fault_record) / sizeof(uint32_t); ++i)
        words[i] = 0U;
    g_fault_record.version = FAULT_LOG_VERSION;''') # 保持普通清空函数不插入捕获标记
cap=c.index('void FaultLog_Capture(')
prefix,capture=c[:cap],c[cap:]
capture=capture.replace('    for (i = 0U; i < sizeof(g_fault_record)', '''    g_fault_capture_trace.stage_word = 0U;
    g_fault_capture_trace.count = next_fault_count;
    g_fault_capture_trace.tick = g_fault_log_diag.last_tick;
    fault_capture_mark(1U);
    for (i = 0U; i < sizeof(g_fault_record)''',1)
capture=capture.replace('    fault_commit();','    fault_capture_mark(2U);\n    fault_capture_commit();',1)
c=prefix+capture
c=c.replace('replay_pending=%lu session=%lu\\r\\n",','replay_pending=%lu session=%lu capture_valid=%lu capture_stage=%lu capture_mask=%02lX capture_word=%08lX capture_count=%lu capture_tick=%lu\\r\\n",',1)
c=c.replace('            (unsigned long)boot_report_session);\n    } else {','''            (unsigned long)boot_report_session, (unsigned long)g_fault_boot_diag.capture_trace_valid,
            (unsigned long)g_fault_boot_diag.capture_stage, (unsigned long)g_fault_boot_diag.capture_mask,
            (unsigned long)g_fault_boot_diag.capture_word, (unsigned long)g_fault_boot_diag.capture_count,
            (unsigned long)g_fault_boot_diag.capture_tick);
    } else {''',1)
(base/'staged/fault_log.c').write_text(c,encoding='utf-8')
(base/'staged/fault_log.h').write_text(h,encoding='utf-8')
assert c.count('fault_capture_mark(1U)')==1
assert c.count('fault_capture_mark(2U)')==1
assert c.count('fault_capture_mark(3U)')==1
assert c.count('fault_capture_mark(4U)')==1
assert c.count('fault_capture_mark(5U)')==1
print('Prepared five-stage retained capture trace')
