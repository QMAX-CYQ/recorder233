from pathlib import Path
import shutil

base=Path(__file__).resolve().parent
src=Path('D:/RM/rm_main_gimbal/User/Support')
(base/'before').mkdir(exist_ok=True)
(base/'staged').mkdir(exist_ok=True)
for name in ('fault_log.c','fault_log.h'):
    shutil.copy2(src/name,base/'before'/name)
h=(src/'fault_log.h').read_text(encoding='utf-8')
h=h.replace(' * 临时停用新增的启动快照、五阶段标记和 [fault_boot] 输出。\n * 改为 1 可恢复诊断；不影响原有故障保存、校验和三段 ERROR 补报。', ' * 启动快照及 [fault_boot] 输出独立启用，五阶段标记独立关闭。\n * 保持原始 fault_commit 路径，供全速运行对照测试。')
h=h.replace('#define FAULT_LOG_ENABLE_BOOT_DIAG 0','#define FAULT_LOG_ENABLE_BOOT_DIAG 1')
h=h.replace('/* [FAULT_BOOT 诊断开关 END] */', '''#ifndef FAULT_LOG_ENABLE_CAPTURE_TRACE
#define FAULT_LOG_ENABLE_CAPTURE_TRACE 0
#endif
/* [FAULT_BOOT 诊断开关 END] */''')
c=(src/'fault_log.c').read_text(encoding='utf-8')
for label in ('五阶段辅助函数','阶段1','阶段2至5'):
    a=f'/* [FAULT_BOOT {label} BEGIN] */\n#if FAULT_LOG_ENABLE_BOOT_DIAG'
    assert a in c
    c=c.replace(a,f'/* [FAULT_BOOT {label} BEGIN] */\n#if FAULT_LOG_ENABLE_CAPTURE_TRACE',1)
c=c.replace('    {\n        uint32_t word = g_fault_capture_trace.stage_word;', '    #if FAULT_LOG_ENABLE_CAPTURE_TRACE\n    {\n        uint32_t word = g_fault_capture_trace.stage_word;',1)
c=c.replace('    for (i = 0U; i < sizeof(*r) / sizeof(uint32_t); ++i) dst[i] = src[i];', '    #endif\n    for (i = 0U; i < sizeof(*r) / sizeof(uint32_t); ++i) dst[i] = src[i];',1)
c=c.replace('    /* 快照完成后重新布防；', '#endif\n#if FAULT_LOG_ENABLE_CAPTURE_TRACE\n    /* 快照完成后重新布防；',1)
old=' session=%lu capture_valid=%lu capture_stage=%lu capture_mask=%02lX capture_word=%08lX capture_count=%lu capture_tick=%lu\\r\\n",'
assert old in c
c=c.replace(old, ' session=%lu\\r\\n",',1)
oldargs='''            (unsigned long)boot_report_session, (unsigned long)g_fault_boot_diag.capture_trace_valid,
            (unsigned long)g_fault_boot_diag.capture_stage, (unsigned long)g_fault_boot_diag.capture_mask,
            (unsigned long)g_fault_boot_diag.capture_word, (unsigned long)g_fault_boot_diag.capture_count,
            (unsigned long)g_fault_boot_diag.capture_tick);'''
assert oldargs in c
c=c.replace(oldargs,'            (unsigned long)boot_report_session);',1)
(base/'staged/fault_log.c').write_text(c,encoding='utf-8')
(base/'staged/fault_log.h').write_text(h,encoding='utf-8')
shutil.copy2(base.parent/'fault_boot_log/test_boot.py',base/'test_boot.py')
print('Boot diagnostics enabled, capture trace disabled')
