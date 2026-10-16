# F405 首次 StopHost 接收通道停止失败锁存

> 后续已按用户要求加入 [受控 Host 内核恢复](../usb_host_recovery/README.md)。首次失败锁存仍保留；本目录固件和本文诊断版行为用于对应恢复功能加入前的历史测试。

只增加诊断，不修复原等待循环、不改写 USB 恢复流程、不主动复位或取消接收。原返回值保持不变。新增中文注释采用 UTF-8。

## 复现后直接看这个变量

```c
g_usb_stop_first_failure
```

- `valid=0`：尚未锁存失败。此时内容可能是正在更新或已成功完成的候选记录，不作为固定故障证据。
- `valid=1`：已锁存首次失败，之后的 StopHost、重连和环形覆盖都不会清掉或替换它。
- `complete=1` 且 `valid=1`：同一轮 StopHost 的出口和各索引等待结果已补齐，可以保存整个结构。
- 若 `valid=1、complete=0`，说明已在等待退出处锁住失败，但尚未观察到该轮 STOP_EXIT。先保存现场；不要将未完成字段解释为有效出口数据。

记录在普通 RAM，F405 复位/重新下载后清空，不写 SD、不跨复位保留。不要运行中手动清零。主板复位不清此 F405 锁存。

## 触发范围

进入 StopHost 时，选取 `tracked_in_mask` 中、配置范围内最小索引的 Bulk IN 通道作为候选。本工程只有一个日志 Bulk IN 接收通道，通常为 4；不写死该编号。未使用索引（例如本工程配置范围外的 12）不会抢先占用首次失败锁存。

触发条件为：该候选通道的原等待循环退出时，观察到 **`count > 1000` 且 `CHENA=1`**。这表示原函数未等到通道关闭，不等同于已经证明最终通信必然卡死，也不保证锁存的就是最初硬件触发事件。请用 `stop_call`、`failure_tick` 与当前超时及生命周期记录对应。

候选 StopHost 成功时允许复用同一块内存；首次失败一旦锁住，后续不再复用。若将来增加多个 Bulk IN 通道，此版只诊断本轮选中的最小索引通道，不能直接作为多接收通道的通用首次失败收集器。

## 优先保存的字段

```c
g_usb_stop_first_failure.valid
g_usb_stop_first_failure.complete
g_usb_stop_first_failure.stop_call
g_usb_stop_first_failure.channel
g_usb_stop_first_failure.caller
g_usb_stop_first_failure.failure_tick
g_usb_stop_first_failure.failure_count
g_usb_stop_first_failure.failure_hcchar
g_usb_stop_first_failure.stop_result
g_usb_stop_first_failure.stage_valid_mask
g_usb_stop_first_failure.stages
```

`caller` 是 StopHost 调用者的返回地址，分析时使用本次固件 AXF/MAP。`request_id`、应用状态、Host 状态、握手 ACK 和会话在该轮入口保存。`failure_hcchar` 保存判定失败时的那次实际读回值；阶段快照是稍后顺序读取的，即使硬件在期间变化，仍保留触发依据。

## 七个阶段

`stage_valid_mask` 的对应位为 1，才说明该阶段已采样；全部补齐为 `0x7F`。

| stages 下标 | 阶段 |
|---|---|
| `[0]` | StopHost 入口，在 FIFO flush 和通道写入前 |
| `[1]` | 第一阶段清理写 HCCHAR 前 |
| `[2]` | 第一阶段写后 |
| `[3]` | 第二阶段请求停止写 HCCHAR 前 |
| `[4]` | 第二阶段写后 |
| `[5]` | 原等待循环退出 |
| `[6]` | StopHost 出口，在重新开启 USB 全局中断前 |

每个阶段保存：HAL tick、DWT 周期计数及有效标志、IPSR、通道控制/中断/长度/DMA 地址、HCD 和 URB 状态、错误计数、接收长度、PID/切换位、GAHBCFG、GRSTCTL、HPRT、全局中断状态/使能、HFNUM、两类发送 FIFO 状态、三项 FIFO 分区配置、HAINT/HAINTMSK。

CPU 临界区避免其他任务/中断写坏记录，但 USB 硬件仍运行，寄存器不是硬件同一时刻锁存。主环冻结不影响此首次失败区；生命周期环仍按第二版规则运行。

## 等待计数和耗时

所有数组下标对应原 StopHost 扫描的通道索引 0～15，实际已检查索引看 `wait_seen_mask`。本工程只配置 12 个通道，超出配置范围的索引不能据此认定为真实接收故障。

| 字段 | 含义 |
|---|---|
| `wait_start_count[n] / wait_end_count[n]` | 原共用循环计数器的前后值 |
| `wait_iterations[n]` | 两者无符号差值，该索引实际执行的循环次数 |
| `wait_elapsed_ms[n]` | HAL tick 差值；在中断或关中断期间可能为 0，不代表没有等待 |
| `wait_elapsed_cycles[n]` | DWT 周期差值，仅对应 `wait_cycle_valid_mask` 位为 1 时解释 |
| `exhausted_mask / enabled_mask` | 该轮 StopHost 结束时补齐的超限及仍使能统计 |

周期耗时换算：`微秒 = wait_elapsed_cycles[n] * 1000000 / core_clock_hz`，离线计算时先转 64 位，避免乘法溢出。计量边界包含少量函数和诊断开销，不是精确 USB 总线事务时长。整个 StopHost 入口至出口的 CPU 周期也可对阶段 6 与阶段 0 的 `cycles` 作无符号差值；两阶段 `cycle_valid` 都应为 1。

仅在有跟踪接收通道的 StopHost 入口启用 CoreDebug trace 和 DWT CYCCNT，不清零已存在的 CYCCNT，也不占用定时器或增加中断。计数器不可用时显式标记无效；HAL tick 仍记录。调试暂停、低功耗或外部代码修改周期计数会影响测量；保存的 `core_clock_hz` 假定该轮等待中内核频率未变。周期差支持一次自然回绕，在 168MHz 下回绕周期约 25.6 秒，正常短等待应远小于这个范围。

## 验证和资料

- 功能源码改动：`User/Task/usb_task.c`、`usb_rx_trace.h`、`stm32f4xx_ll_usb.c`。沿用既有源文件，无需增加 Keil 文件条目。
- 新锁存结构 1320 字节 RAM，无另一份完整候选副本。Keil 最终 RW+ZI=128840 字节，链接通过；静态 USB 任务最大调用深度估计 692 字节，任务原栈为 2048 字节。这不是运行时高水位测量。
- AC6.22 验证项目编译：0 错误、0 警告。未烧录或操作硬件复现现场。
- 实际诊断函数及实际 StopHost 的模拟测试验证：七阶段保留、各索引循环次数、首次锁存后不覆盖、成功候选可复用、valid/complete 发布、CYCCNT 不清零、毫秒和周期计数回绕、无周期计数器、未使用索引不抢占，以及原超时/双环行为回归。
- 删除新增诊断调用后，底层原条件、寄存器写入、循环计数和返回值仍与未加钩子备份逐 token 一致。原 1000 轮上限及跨通道共用 count 没有被修复。
- 本目录保存 `before`、构建/测试日志、固件和源码校验清单；当前测试入口仍为 `diagnostics/usb_rx_trace/test_trace.py`。

重新生成代码或更新 HAL 可能覆盖底层钩子。锁存增加了少量诊断执行时间，可能影响故障复现概率；若发生与当前冻结记录不同的更早失败，先分析此处的时间和阶段，不要直接将它当作最终卡住那一轮。
