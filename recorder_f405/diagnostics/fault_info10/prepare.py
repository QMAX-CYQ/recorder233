from pathlib import Path
import shutil

base=Path(__file__).resolve().parent
src=Path('D:/RM/rm_main_gimbal/User/Task/debug_task.c')
(base/'before').mkdir(exist_ok=True)
(base/'staged').mkdir(exist_ok=True)
shutil.copy2(src,base/'before/debug_task.c')
c=src.read_text(encoding='utf-8')
needle='''    w[47] = DWT->FOLDCNT;
}'''
assert c.count(needle)==1
c=c.replace(needle,'''    w[47] = DWT->FOLDCNT;
    /* [FAULT_INFO10 BEGIN]
     * 第10通道(ch=9)保留前32字IMU，末16字改为故障诊断，仍为48字/100ms。
     * 当前记录与启动校验前的旧快照分别输出，避免误把不同时间的数据当成覆盖。
     * 仅在任务中读取普通内存，不重算校验和、不写保留区、不增加临界区。
     * 故障捕获、复位及补报等待期间没有INFO，不能代替指令级阶段诊断。
     * 索引均从0开始，全部按8位十六进制输出。 */
    if (channel == 9U) {
        w[32] = g_fault_record.magic;
        w[33] = g_fault_record.checksum;
        w[34] = g_fault_record.pending;
        w[35] = g_fault_log_diag.pending;
        w[36] = g_fault_boot_diag.raw.magic;
        w[37] = g_fault_boot_diag.raw.checksum;
        w[38] = g_fault_boot_diag.calculated_checksum;
        w[39] = g_fault_boot_diag.check_failed;
        w[40] = g_fault_boot_diag.raw.pending;
        w[41] = g_fault_boot_diag.raw_valid;
        w[42] = g_fault_boot_diag.reset_after;
        w[43] = g_fault_log_diag.invalid;
        w[44] = g_fault_log_diag.reports;
        w[45] = g_fault_log_diag.retries;
        w[46] = g_fault_boot_diag.capture_stage;
        w[47] = cdc_tx_queue_overflow_count;
    }
    /* [FAULT_INFO10 END] */
}''')
(base/'staged/debug_task.c').write_text(c,encoding='utf-8')
print('Prepared INFO ch=9 fault diagnostic words 32..47')
