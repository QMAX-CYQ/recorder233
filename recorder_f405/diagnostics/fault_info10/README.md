# 第10个INFO通道故障诊断

2026-10-16：仅修改主板 User/Task/debug_task.c，新增块标为 FAULT_INFO10 BEGIN/END。
仍为10通道轮转，每10ms一条，第10个通道ch=9/tag=imu正常每100ms一轮。
前32个IMU字保持不变，最后16字覆盖该通道原SCB/DWT公共诊断字段。
其他通道公共字段不变。一行仍n=48，8位十六进制，不增加行或负载长度。
旧stress schema对ch=9最后16字的解释已不适用，使用本表。

索引从0开始，w[32]是斜杠分隔的第33个数，以下均是十六进制。

| w索引 | 字段 | 时间含义 |
|---|---|---|
| 32 | g_fault_record.magic | 当前记录 |
| 33 | g_fault_record.checksum | 当前记录 |
| 34 | g_fault_record.pending | 当前记录 |
| 35 | g_fault_log_diag.pending | 当前待补报状态 |
| 36 | g_fault_boot_diag.raw.magic | 启动校验前旧记录 |
| 37 | g_fault_boot_diag.raw.checksum | 启动校验前旧记录 |
| 38 | g_fault_boot_diag.calculated_checksum | 旧快照重算校验和 |
| 39 | g_fault_boot_diag.check_failed | 启动失败位图 |
| 40 | g_fault_boot_diag.raw.pending | 旧记录待补报标志 |
| 41 | g_fault_boot_diag.raw_valid | 1表示有旧快照，不表示校验通过 |
| 42 | g_fault_boot_diag.reset_after | 启动复位原因 |
| 43 | g_fault_log_diag.invalid | 本次启动校验失败计数 |
| 44 | g_fault_log_diag.reports | 本次启动故障补报完成次数 |
| 45 | g_fault_log_diag.retries | 故障补报入队重试次数 |
| 46 | g_fault_boot_diag.capture_stage | 启动快照的捕获阶段（本版最高4） |
| 47 | cdc_tx_queue_overflow_count | 本次启动累计入队失败次数 |

正常空记录也会在 FaultLog_BootInit/fault_empty 中获得magic及checksum，发生在正常任务运行前。
不是必须故障后才有值。无效记录清空后同样会生成有效空记录，因此当前magic有效、
而boot快照magic为0可以同时成立；不能单凭两者差异认定运行中再次覆盖。
故障补报完成时pending从1变0，重新提交也会改变checksum，属于正常变化。
HardFault、复位、USB未握手、启动诊断/故障补报等待期间没有INFO。
INFO采样是任务内顺序读取，不是异常入口硬件快照；无法捕获提交指令之间的微秒级变化。
不在INFO中重新计算校验和，不写保留区，不增加临界区。

原故障阶段1至4及fault_boot两段保持原样，未修改fault_log.c/.h或F405。
AC6.22 validation编译0 errors/0 warnings：Code248272、RO4884、RW332、ZI115244。
本次为静态诊断映射修改，编译确认符号、数组及格式路径；物理100ms节奏仍由实机测试验证。
下载后取消相关断点，全速复现并提供日志。固件未烧录。
