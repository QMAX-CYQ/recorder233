from pathlib import Path
import shutil

base=Path(__file__).resolve().parent
src=Path('D:/RM/rm_main_gimbal/User/Support')
(base/'before').mkdir(exist_ok=True)
(base/'staged').mkdir(exist_ok=True)
for name in ('fault_log.c','fault_log.h'):
    shutil.copy2(src/name,base/'before'/name)
h=(src/'fault_log.h').read_text(encoding='utf-8')
h=h.replace('启动快照及 [fault_boot] 输出独立启用，五阶段标记独立关闭。','启动快照及 [fault_boot] 输出启用；捕获标记仅保留提交前阶段1、2。')
h=h.replace('#define FAULT_LOG_ENABLE_CAPTURE_TRACE 0','#define FAULT_LOG_ENABLE_CAPTURE_TRACE 1')
h=h.replace('阶段：1进入、2字段完成、\n * 3开始校验、4校验和已提交、5 magic 已提交。只保留最近一次捕获。','阶段：1进入、2字段完成。\n * 不记录提交内部的阶段3至5，提交仍使用原始 fault_commit。只保留最近一次捕获。')
c=(src/'fault_log.c').read_text(encoding='utf-8')
start=c.index('/* 普通初始化及补报清除继续使用 fault_commit，不覆盖捕获进度。 */')
end=c.index('\n#endif',start)
c=c[:start]+'/* 此对照版本不实现阶段3至5，故障提交使用原始 fault_commit。 */\n'+c[end:]
c=c.replace('五阶段辅助函数','提交前两阶段辅助函数')
c=c.replace('stage <= 5U','stage <= 2U')
old='''    /* [FAULT_BOOT 阶段2至5 BEGIN] */
#if FAULT_LOG_ENABLE_CAPTURE_TRACE
    fault_capture_mark(2U);
    fault_capture_commit();
#else
    fault_commit();
#endif
    /* [FAULT_BOOT 阶段2至5 END] */'''
new='''    /* [FAULT_BOOT 阶段2 BEGIN] */
#if FAULT_LOG_ENABLE_CAPTURE_TRACE
    fault_capture_mark(2U);
#endif
    /* [FAULT_BOOT 阶段2 END] */
    /* 对照关键：此函数内部没有新增标记或额外 DSB。 */
    fault_commit();'''
assert old in c
c=c.replace(old,new,1)
old='replay_pending=%lu session=%lu\\r\\n",'
assert old in c
c=c.replace(old,'replay_pending=%lu session=%lu capture_steps=2 capture_valid=%lu capture_stage=%lu capture_mask=%02lX capture_count=%lu capture_tick=%lu\\r\\n",',1)
needle='            (unsigned long)boot_report_session);\n    } else {'
assert needle in c
c=c.replace(needle,'''            (unsigned long)boot_report_session, (unsigned long)g_fault_boot_diag.capture_trace_valid,
            (unsigned long)g_fault_boot_diag.capture_stage, (unsigned long)g_fault_boot_diag.capture_mask,
            (unsigned long)g_fault_boot_diag.capture_count, (unsigned long)g_fault_boot_diag.capture_tick);
    } else {''',1)
(base/'staged/fault_log.c').write_text(c,encoding='utf-8')
(base/'staged/fault_log.h').write_text(h,encoding='utf-8')
test=(base.parent/'fault_capture_stage/test_stage.py').read_text(encoding='utf-8')
test=test.replace('target<=5','target<=2').replace('stage<=5','stage<=2')
test=test.replace('assert(observed_mask==31 && fault_valid(&g_fault_record));','assert(observed_mask==3 && fault_valid(&g_fault_record));')
test=test.replace('fault_capture_mark(3);','fault_capture_mark(2);').replace('stage_word&255)==3','stage_word&255)==2')
test=test.replace('each of five actual capture stages','each of two precommit stages').replace('全部五阶段','两个提交前阶段')
(base/'test_stage.py').write_text(test,encoding='utf-8')
print('Prepared stages 1/2 plus unchanged original fault_commit')
