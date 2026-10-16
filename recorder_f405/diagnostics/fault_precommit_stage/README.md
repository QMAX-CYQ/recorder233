# 阶段1/2加原始提交的对照固件

2026-10-16。BOOT_DIAG=1、CAPTURE_TRACE=1，本版本 CAPTURE_TRACE 仅实现阶段1、2。
阶段1：进入捕获C函数、清空现场前。阶段2：现场字段及允许读取的异常帧填写完毕。
两次标记各写一个对齐32位字并执行DSB，然后调用原始 fault_commit。
原始 fault_commit 源码与上轮逐字相同；没有 instrumented commit 辅助函数或阶段3至5。
故障记录格式及保留区地址不变。

启动诊断第二段增加 capture_steps=2、capture_valid、capture_stage、capture_mask、
capture_count、capture_tick。模式改变后只接受1、2，旧五阶段残留不当成本轮进度。
capture_valid=1、stage=2、mask=03 只确认填完现场；提交是否有效仍检查 magic、
stored_crc/calc_crc、check_failed 和 boot_valid。正常提交成功也只记录到阶段2。

原生测试在两个阶段分别模拟复位，检查原始快照、重新布防、完整捕获、冷启动、
普通提交不覆盖阶段、日志格式；回归BUSY、换会话/DeInit重发、排空门槛、三段补报。
AC6.22 validation 编译0 errors/0 warnings：Code248032、RO4884、RW332、ZI115244。
反汇编确认两次 fault_capture_mark（参数1、2），随后调用 fault_commit；
fault_capture_commit 不存在。故障提交函数本体与修改前源码一致。
g_fault_record=2001F000、g_fault_capture_trace=2001F4B8、g_fault_test_latch=2001F4C8。

固件未烧录。下载后取消异常捕获/提交相关断点，全速复现并提供日志。
若此模式也恢复ERROR，说明提交前的标记改动足以改变本轮表现，但仍需重复对照，
不能据此断定是特定DSB、函数调用或内存写入修复了根因。
