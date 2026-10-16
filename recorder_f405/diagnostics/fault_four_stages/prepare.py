from pathlib import Path
import shutil

base=Path(__file__).resolve().parent
src=Path('D:/RM/rm_main_gimbal/User/Support')
(base/'before').mkdir(exist_ok=True)
(base/'staged').mkdir(exist_ok=True)
for name in ('fault_log.c','fault_log.h'):
    shutil.copy2(src/name,base/'before'/name)
h=(src/'fault_log.h').read_text(encoding='utf-8')
h=h.replace('捕获标记仅保留提交前阶段1、2。','捕获标记启用阶段1至4，阶段5保持关闭。')
h=h.replace('保持原始 fault_commit 路径，供全速运行对照测试。','仅故障捕获提交增加阶段3、4；普通初始化和补报清除仍使用原始 fault_commit。')
h=h.replace('阶段：1进入、2字段完成。\n * 不记录提交内部的阶段3至5，提交仍使用原始 fault_commit。只保留最近一次捕获。', '阶段：1进入、2字段完成、\n * 3开始校验、4校验和已提交。阶段5关闭，只保留最近一次捕获。')
c=(src/'fault_log.c').read_text(encoding='utf-8')
c=c.replace('提交前两阶段辅助函数','阶段1至4辅助函数')
old='/* 此对照版本不实现阶段3至5，故障提交使用原始 fault_commit。 */'
assert old in c
c=c.replace(old,'''/* 仅故障捕获使用；阶段4在校验和写入及DSB之后、magic写入之前。
 * 阶段5保持关闭，普通初始化和补报清除不改写捕获进度。 */
static void fault_capture_commit(void)
{
    fault_capture_mark(3U);
    g_fault_record.checksum = fault_checksum(&g_fault_record);
    __DSB();
    fault_capture_mark(4U);
    g_fault_record.magic = FAULT_LOG_MAGIC;
    __DSB();
}''',1)
c=c.replace('stage <= 2U','stage <= 4U')
old='''    /* 对照关键：此函数内部没有新增标记或额外 DSB。 */
    fault_commit();'''
assert old in c
c=c.replace(old,'''#if FAULT_LOG_ENABLE_CAPTURE_TRACE
    fault_capture_commit();
#else
    fault_commit();
#endif''',1)
c=c.replace('capture_steps=2','capture_steps=4')
(base/'staged/fault_log.c').write_text(c,encoding='utf-8')
(base/'staged/fault_log.h').write_text(h,encoding='utf-8')
t=(base.parent/'fault_precommit_stage/test_stage.py').read_text(encoding='utf-8')
t=t.replace('target<=2','target<=4').replace('stage<=2','stage<=4')
t=t.replace('observed_mask==3 && fault_valid','observed_mask==15 && fault_valid')
t=t.replace('each of two precommit stages','each of four capture stages').replace('两个提交前阶段','四个捕获阶段')
(base/'test_stage.py').write_text(t,encoding='utf-8')
print('Prepared stages 1-4; stage 5 disabled')
