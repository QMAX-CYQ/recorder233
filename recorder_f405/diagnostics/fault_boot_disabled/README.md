# 暂停 fault_boot 诊断

2026-10-16。fault_log.h 内 FAULT_LOG_ENABLE_BOOT_DIAG 默认设为0。
fault_log.c 内各新增区域用 `[FAULT_BOOT ... BEGIN/END]` 注释明确标注。
通过条件编译暂时屏蔽，可将宏改为1重新启用。

关闭范围包括启动快照、重新计算诊断校验和、异常捕获五阶段写入、
fault_boot 两段输出及发送等待。原故障捕获使用 fault_commit，
启动有效性校验、三段 ERROR 补报、手动故障请求均保留。

保留原新增诊断全局变量声明及 UNINIT 空间，以保持故障记录、应急栈、
阶段区和一次性测试标记的位置。关闭期间这些诊断变量不再更新。
诊断关闭时不应依据 g_fault_capture_trace/g_fault_boot_diag 的残留值判断新故障。
这不保证生成代码指令地址和原诊断前固件完全一致。

原生测试验证实际禁用后的生产源码：正常/扩展异常帧捕获、看门狗等待、
冷/热启动校验、BUSY重试、三段补报排空后清除、断开重发、手动注入与tick回绕。
编译 AC6.22 validation：0 errors / 0 warnings。
Code=246672，RO=4392，RW=332，ZI=114988。
g_fault_record=0x2001F000，g_fault_capture_trace=0x2001F4B8，g_fault_test_latch=0x2001F4C8。

用户反馈 RB62–65 均有 ERROR；此版本供关闭新增诊断后的对照测试，
不能据此断言此前间歇性记录失效问题已经解决。未烧录。
