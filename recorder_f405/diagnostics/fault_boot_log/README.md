# 主板启动故障记录诊断

2026-10-16：修改主板 `User/Support/fault_log.c`、`fault_log.h`。
F405、异常捕获函数、保留区格式及链接布局均未改动。

在 FaultLog_BootInit 内、fault_empty 之前，将完整原始记录复制到普通 RAM
`g_fault_boot_diag.raw`，保存复位原因、重新计算的校验和和失败位图。
冷启动跳过原保留区读取，raw_valid=0，避免未初始化 ECC RAM 访问。
这是 SystemInit 和 C 运行库初始化之后的快照，不能定位它们之前的改写，
也不能仅凭 magic/checksum 为零判断故障捕获具体在哪条指令停止。

USB READY/ACK 握手后，FaultLog_Service 优先发送两段 WARN `[fault_boot]`，
随后继续原有 ERROR `[hardfault]` 三段补报。启动诊断也会在无有效故障记录时输出。
USB BUSY 保留当前分段，发送期间会话变化或 DeInit 会从第一段重发。
发送队列排空后结束本次诊断补发；这不是 SD 落盘确认，且完成后的再次断线不自动重报。
快照会在下一次主板复位时被新快照替代，不是多次启动历史。

## 判读

- part=1/2：reset、cold、raw_valid、check_failed、magic、version、size、
  stored_crc、calc_crc、pending、count、session。
- part=2/2：异常号、故障 tick、PC/LR、CFSR/HFSR、栈地址、EXC_RETURN、
  boot_valid、boot_invalid、replay_pending、session。
- crc 字段沿用命名习惯，算法实际是原代码的 32 位 FNV 风格哈希，并非 CRC 多项式算法。
- check_failed bit0=magic，bit1=version，bit2=size，bit3=pending 超范围，bit4=checksum。
- raw_valid=1、check_failed=0：原记录通过校验；pending=0 代表无待补报故障，也属有效记录。
- raw_valid=1、check_failed=00000011：magic 与 checksum 两项失败，与此前半提交现场一致。
- cold=1、raw_valid=0：冷启动没有探测原记录；此时 check_failed=0 不表示原记录有效。

## 验证及复现

`test_boot.py` 从 staged 中实际生产源码生成原生测试程序。
native/run.cmd 在 native 目录执行；覆盖冷启动、原始快照留存、各失败位、
BUSY/未握手、换会话/DeInit、发送排空门槛以及完整三段 HardFault 补报。
Keil AC6.22 validation 工程编译：0 errors、0 warnings。
Code=247744，RO=4776，RW=332，ZI=115204。

下载主板固件后，取消故障捕获和启动路径的断点，全速复现。
待 USB 恢复后保存对应日志，搜索 `[fault_boot]` 与 `[hardfault]`。
无需为了获取启动判定而单步。仍需同时观察主板 overflow 和 F405 恢复计数。
新增诊断会带来启动时有限的两条日志流量及快照计算开销，测试结果需考虑这点。

Rb55 无 ERROR/HardFault 匹配；Rb56 第 2463–2465 行有完整三段，checksum=30B69711。
这两份是新增诊断之前的对照，不含 fault_boot。
