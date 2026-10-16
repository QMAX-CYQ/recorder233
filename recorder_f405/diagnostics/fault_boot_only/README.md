# 仅启用 fault_boot，不启用五阶段

2026-10-16：主板 fault_log.h 独立设置

```
FAULT_LOG_ENABLE_BOOT_DIAG=1
FAULT_LOG_ENABLE_CAPTURE_TRACE=0
```

启动时在清除无效记录前保存原记录、重新计算校验和，恢复后补发两段 fault_boot。
五阶段辅助函数、捕获阶段写入、启动读取/清除进度全部不编译。
保留原诊断全局变量占位，保证 UNINIT 布局不变；关闭期间不解释旧阶段残留数据。
fault_boot 不再输出 capture_* 字段，避免将残留阶段误认为本次记录。
故障捕获恢复原 fault_commit，不在故障路径添加阶段写入或额外 DSB。

原生测试覆盖冷/热启动、清除前原始记录、失败位图、BUSY重试、会话/DeInit重发、
排空门槛、原三段 ERROR 补报。Keil AC6.22 validation 编译0 errors/0 warnings。
Code=247760，RO=4776，RW=332，ZI=115244。
反汇编确认 fault_boot_snapshot/service 存在，FaultLog_Capture 调用 fault_commit；
fault_capture_mark 和 fault_capture_commit 不存在。

固件仅编译归档，未烧录。取消故障处理相关断点后全速复现，保存两段 fault_boot、
是否有 ERROR 及 overflow 变化。这一对照只取消捕获阶段标记，并不保证整体机器码地址
或其他启动/发送时序与关闭所有诊断时完全相同。
