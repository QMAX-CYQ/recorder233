# 阶段1至4对照

2026-10-16：保留阶段1/2，增加阶段3（校验计算之前）、阶段4（校验和写入及DSB之后）。
阶段4后照常写magic、执行原有DSB，但不记录阶段5。
普通 fault_commit 保持原样，仅故障捕获使用包含阶段3/4的 fault_capture_commit。

启动诊断输出 capture_steps=4、capture_valid、capture_stage、capture_mask、count、tick。
正常完成时最高 stage=4/mask=0F，不因缺少阶段5判失败；仍结合 magic、
stored_crc/calc_crc、check_failed 和 boot_valid 判断记录是否有效。
stage=3只表明尚未确认到阶段4，不等价于已证明校验函数死循环。
五阶段版本与本版本的共同成功仍只能说明修改影响了表现，不证明根因已修复。

原生测试实际生产源码：阶段1至4分别模拟中断、完整捕获提交、冷/热启动、
标记完整性、重新布防、普通提交不覆盖进度、BUSY及会话重发、三段补报回归。
全部通过。AC6.22 validation编译0 errors/0 warnings：Code248096、RO4884、RW332、ZI115244。
反汇编检查 capture_commit 中阶段3、4存在，写magic后无阶段5调用。
固件仅编译归档，未烧录。下载后取消异常捕获及提交断点，全速复现并保存日志。
