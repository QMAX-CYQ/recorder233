# 五阶段故障捕获诊断

2026-10-16。主板 fault_log.c/.h 增加独立的 DTCM UNINIT 进度字，保持 fault_record_t 格式不变。
异常处理不输出日志，不调用 RTOS，不喂狗。每个阶段写一个对齐的 32 位阶段字并执行 DSB。
stage_word 高16位为 0x4653，低字节为阶段，中间字节为阶段反码；每次捕获覆盖前次进度。
count/tick 在阶段1前取自已有软件变量，用来与本次故障记录关联。

| capture_stage | capture_mask | 含义 |
|---|---|---|
| 1 | 01 | 已进入 FaultLog_Capture 的 C 函数体；还未清空并填写现场 |
| 2 | 03 | 现场字段填写完成，包括允许读取的异常帧 |
| 3 | 07 | 即将计算校验和 |
| 4 | 0F | 校验和已经写入记录并执行 DSB |
| 5 | 1F | magic 已写入并执行 DSB，捕获提交完成 |

capture_mask 由可信的最后阶段计算，表示按代码顺序已到达的阶段，并非五个独立时间戳。
标记停在3只表明未确认阶段4，范围包括校验计算、校验和存储/屏障或阶段4写入前；
不单凭这个值断言 CPU 永久卡在某个函数。

启动时先复制到 g_fault_boot_diag 的 capture_* 字段，再将保留进度重新布防为0。
冷启动不读取原有进度，按初始化顺序写零，避免未初始化 ECC RAM 读取。
普通 fault_commit（初始化空记录、补报完成后的清除）不会触碰捕获标记。
fault_boot 的 part=2/2 增加：capture_valid、capture_stage、capture_mask、capture_word、capture_count、capture_tick。
capture_valid=0 时不解释 capture_stage；可能是本次启动前无捕获、旧固件没有标记或标记损坏。
完整进度5与启动时无效记录同时出现，提示提交后记录发生变化，应继续检查复位过程及启动阶段。
阶段标记仍有自身开销，会改变故障现场保存时序；它不能完全替代硬件时序证据。

## 链接与验证

AC6.22 validation 工程：0 errors / 0 warnings。
Code=248112、RO=4888、RW=332、ZI=115244。
MAP：g_fault_record=0x2001F000（保持原地址），g_fault_emergency_stack=0x2001F0B8，
g_fault_capture_trace=0x2001F4B8（12字节），g_fault_test_latch=0x2001F4C8。
UNINIT 使用 0x4D0 / 0x1000 字节。
仅测试 latch 地址移动，使用符号观察，勿沿用旧的绝对地址。

test_stage.py 使用实际 staged 源码，在每个阶段屏障模拟复位，验证五阶段前缀、
不同提交状态、完整捕获、启动快照/重新布防、损坏标记、冷启动以及普通提交不覆盖进度。
同时回归原先 boot 日志 BUSY/握手重发和完整 HardFault 三段补报测试。

## 使用

首次下载新诊断固件后下电再上电一次，初始化新增保留区。
后续复现使用看门狗热复位，不需要下电，不在异常捕获或提交函数设断点。
USB 恢复后保存日志，搜索 fault_boot 的两段和 hardfault，提供完整文件即可。
Watch 可查看 g_fault_boot_diag.capture_stage/capture_mask；复位后原始 trace 已重新布防为0，
因此应查看启动快照，不将 g_fault_capture_trace.stage_word=0 当作捕获失败。
