from pathlib import Path
import shutil

base=Path(__file__).resolve().parent
src=Path('D:/RM/rm_main_gimbal/User/Support')
(base/'before').mkdir(exist_ok=True)
(base/'staged').mkdir(exist_ok=True)
for name in ('fault_log.c','fault_log.h'):
    shutil.copy2(src/name,base/'before'/name)
h=(src/'fault_log.h').read_text(encoding='utf-8')
h=h.replace('#define FAULT_LOG_TEST_DELAY_MS', '''/* [FAULT_BOOT 诊断开关 BEGIN]
 * 临时停用新增的启动快照、五阶段标记和 [fault_boot] 输出。
 * 改为 1 可恢复诊断；不影响原有故障保存、校验和三段 ERROR 补报。
 * 保留诊断变量及其存储位置，避免改变已使用的保留区地址。 */
#ifndef FAULT_LOG_ENABLE_BOOT_DIAG
#define FAULT_LOG_ENABLE_BOOT_DIAG 0
#endif
/* [FAULT_BOOT 诊断开关 END] */

#define FAULT_LOG_TEST_DELAY_MS''',1)
c=(src/'fault_log.c').read_text(encoding='utf-8')
c=c.replace('static uint32_t boot_report_session, boot_report_deinit;\nstatic uint8_t boot_report_started;', '''/* [FAULT_BOOT 发送状态 BEGIN] */
#if FAULT_LOG_ENABLE_BOOT_DIAG
static uint32_t boot_report_session, boot_report_deinit;
static uint8_t boot_report_started;
#endif
/* [FAULT_BOOT 发送状态 END] */''')
c=c.replace('/* 每阶段一个对齐的', '/* [FAULT_BOOT 五阶段辅助函数 BEGIN] */\n#if FAULT_LOG_ENABLE_BOOT_DIAG\n/* 每阶段一个对齐的',1)
c=c.replace('static void fault_empty(void)', '#endif\n/* [FAULT_BOOT 五阶段辅助函数 END] */\n\nstatic void fault_empty(void)',1)
c=c.replace('/* 仅在启动时复制一次', '/* [FAULT_BOOT 启动快照函数 BEGIN] */\n#if FAULT_LOG_ENABLE_BOOT_DIAG\n/* 仅在启动时复制一次',1)
c=c.replace('void FaultLog_BootInit(void)', '#endif\n/* [FAULT_BOOT 启动快照函数 END] */\n\nvoid FaultLog_BootInit(void)',1)
c=c.replace('    fault_boot_snapshot(reset);','    /* [FAULT_BOOT 启动快照调用 BEGIN] */\n#if FAULT_LOG_ENABLE_BOOT_DIAG\n    fault_boot_snapshot(reset);',1)
c=c.replace('    g_fault_log_diag.reset_after = reset;', '#endif\n    /* [FAULT_BOOT 启动快照调用 END] */\n    g_fault_log_diag.reset_after = reset;',1)
cap=c.index('void FaultLog_Capture(')
prefix,rest=c[:cap],c[cap:]
rest=rest.replace('    g_fault_capture_trace.stage_word = 0U;', '    /* [FAULT_BOOT 阶段1 BEGIN] */\n#if FAULT_LOG_ENABLE_BOOT_DIAG\n    g_fault_capture_trace.stage_word = 0U;',1)
rest=rest.replace('    fault_capture_mark(1U);','    fault_capture_mark(1U);\n#endif\n    /* [FAULT_BOOT 阶段1 END] */',1)
rest=rest.replace('    fault_capture_mark(2U);\n    fault_capture_commit();','''    /* [FAULT_BOOT 阶段2至5 BEGIN] */
#if FAULT_LOG_ENABLE_BOOT_DIAG
    fault_capture_mark(2U);
    fault_capture_commit();
#else
    fault_commit();
#endif
    /* [FAULT_BOOT 阶段2至5 END] */''',1)
c=prefix+rest
c=c.replace('/* 普通任务上下文补发启动判定', '/* [FAULT_BOOT 日志发送函数 BEGIN] */\n#if FAULT_LOG_ENABLE_BOOT_DIAG\n/* 普通任务上下文补发启动判定',1)
c=c.replace('uint8_t FaultLog_Service(void)', '#endif\n/* [FAULT_BOOT 日志发送函数 END] */\n\nuint8_t FaultLog_Service(void)',1)
c=c.replace('    if (fault_boot_service()) return 1U;','''    /* [FAULT_BOOT 日志发送调用 BEGIN] */
#if FAULT_LOG_ENABLE_BOOT_DIAG
    if (fault_boot_service()) return 1U;
#endif
    /* [FAULT_BOOT 日志发送调用 END] */''',1)
(base/'staged/fault_log.c').write_text(c,encoding='utf-8')
(base/'staged/fault_log.h').write_text(h,encoding='utf-8')
test_script=Path('D:/RM/rm_main_gimbal/diagnostics/fault_manual/test_fault_log.py').read_text(encoding='utf-8')
(base/'test_original.py').write_text(test_script.replace('staged/User/Support','staged'),encoding='utf-8')
print('FAULT_BOOT diagnostic sections labeled and disabled by default')
