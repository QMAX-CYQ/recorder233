# F405 启停与重连诊断（第二版）

> 当前工程进一步加入了 [首次停止失败锁存](../usb_stop_first_failure/README.md)。复现后优先保存 `g_usb_stop_first_failure`，本目录固件仍保留第二版，用于对应历史调用地址。

此次只补诊断，不自动恢复 USB、不清软件 pending、不修复原等待循环、不改变原驱动返回值，也不修改主板或 SD 文件内容。新增注释采用 UTF-8 中文。

## 本次新增

1. 在 `USB_StopHost()` 入口、逐通道两阶段写入前后、等待循环退出及函数结束时记录事件。在 `HAL_HCD_HC_Init()` 配置接收通道前后记录事件。
2. 保留原 64 条接收主环 `entries[]`，新增 32 条生命周期环 `lifecycle[]`。首次接收等待满 1000ms 时仍抓超时快照并冻结主环，但生命周期环始终记录连接、断开、端口变化、StopHost、通道初始化和超时标记。

因此，首次超时后的断开清理不再因主环冻结而漏记。普通数据包不会占用生命周期环，两个环中的同一个事件使用相同 `sequence`，可与 tick 合并还原顺序。

## 复现时的 Watch

```c
g_usb_rx_trace_diag
g_usb_rx_timeout_diag
```

优先保存以下字段：

```c
g_usb_rx_trace_diag.frozen
g_usb_rx_trace_diag.freeze_request_id
g_usb_rx_trace_diag.next_index
g_usb_rx_trace_diag.entries
g_usb_rx_trace_diag.lifecycle_next_index
g_usb_rx_trace_diag.lifecycle_valid_count
g_usb_rx_trace_diag.lifecycle
g_usb_rx_trace_diag.stop_calls
g_usb_rx_trace_diag.stop_wait_seen_mask
g_usb_rx_trace_diag.stop_wait_exhausted_mask
g_usb_rx_trace_diag.stop_wait_enabled_mask
g_usb_rx_trace_diag.last_stop_result
```

同时保存 `g_usb_rx_timeout_diag` 的最近超时快照。最近快照下标为该结构的 `latest_index`；`first` 可能只是主板停传期间的首次正常等待。

`frozen=1` **只代表主环冻结**，不是两个环都冻结。生命周期环继续更新；读取完成前不要复位。若通信正常恢复或离开就绪态，usb_task 仍按原诊断行为恢复主环记录，不影响生命周期环。

## 环形顺序

- 主环不足 64 条时从 0 开始；满环时从 `next_index` 开始读，向后回绕，共读 64 条。
- 生命周期环不足 32 条时从 0 开始；满环时从 `lifecycle_next_index` 开始读，向后回绕，共读 32 条。
- 生命周期最新一条下标为 `(lifecycle_next_index + 31) % 32`；主环最新一条下标为 `(next_index + 63) % 64`。
- `valid=1` 才表示一条记录已写完。`sequence` 为两个环共用的事件序号，主环冻结期间仍可能因生命周期事件递增；与 `written` 不再一一对应。序号和 tick 均自然回绕。
- `written/overwritten` 属于主环；`lifecycle_written/lifecycle_overwritten` 属于生命周期环。覆盖计数不是丢包率。32 条容量有限，多次重连仍可能覆盖更早的生命周期事件。

## 新事件编号

原编号 1～20 保持不变；新增如下：

| event | 名称 | detail 与状态说明 |
|---|---|---|
| 21 | STOP_ENTER | StopHost 入口；detail=调用者返回地址 |
| 22 | STOP_FLUSH_BEFORE | 第一阶段写 HCCHAR 前；detail=即将写入值，hcchar=实际读回值 |
| 23 | STOP_FLUSH_AFTER | 第一阶段写后；detail=请求写入值，hcchar=实际读回值 |
| 24 | STOP_HALT_BEFORE | 第二阶段请求停止前；detail=即将写入值 |
| 25 | STOP_HALT_AFTER | 第二阶段写后；detail=请求写入值 |
| 26 | STOP_WAIT_EXIT | 原等待循环退出；detail=原始 count，hcchar 显示通道是否仍使能 |
| 27 | STOP_EXIT | StopHost 清理完成、重新开启全局中断前；detail=原函数将返回的 HAL 状态 |
| 28 | INIT_BEFORE | HAL 配置接收通道前；detail=(ep_type << 8) | epnum，Bulk IN 0x81 对应 0x281 |
| 29 | INIT_AFTER | HAL 配置后；detail=USB_HC_Init 返回码 |

`tracked_in_mask` 跟踪已识别的 Bulk IN 通道：即使 StopHost 清除了 HCCHAR 的 EPDIR，也继续记录该通道。通道重新配置为其他类型/方向时解除跟踪。不固定为通道 4。

## 等待结果掩码

每次 STOP_ENTER 清空下列“最近一次 StopHost”掩码，随后逐通道累加。对应位 n 表示硬件通道 n：

| 字段 | 含义 |
|---|---|
| `stop_calls` | 观察到的 StopHost 次数 |
| `stop_wait_seen_mask` | 原等待循环已扫描的通道索引；原函数扫描 0～15，完整扫描为 0xFFFF |
| `stop_wait_enabled_mask` | 原循环退出后观察到 CHENA 仍为 1 的通道 |
| `stop_wait_exhausted_mask` | 原循环退出后同时满足 count > 1000 且 CHENA=1 的通道 |
| `last_stop_result` | 原函数将返回的 HAL 状态；入口时先设 UINT32_MAX 表示尚未走到出口 |

掩码统计覆盖原函数扫描的全部 16 个通道索引；本工程配置的 `Host_channels` 为 12，详细逐通道事件只读取配置范围内跟踪的接收通道，以节省记录容量。不要将未使用索引的读回统计单独认定为接收故障，重点结合实际 `in_pipe`、INIT 前后与接收结果判断。

`count` 是循环次数，**不是毫秒**。本次保留其跨通道共用的原始行为，未逐通道清零。函数返回 HAL_OK 仍可能与通道未停并存，正是本版要记录的证据，不代表已修复。

## 开销与验证

- `g_usb_rx_trace_diag` 从 5500 增至 8272 字节，新增 2772 字节 RAM；链接检查通过。RAM 总静态占用（含链接统计的堆栈区域）RW+ZI=127524 字节。
- usb_task 任务栈仍为 2048 字节，链接器估计本次最大静态调用深度 600 字节；此数不等于运行时高水位。
- Keil AC6.22 验证项目：0 错误、0 警告，见 `build.log`。未烧录、未复位硬件。
- `diagnostics/usb_rx_trace/test_trace.py` 测试实际记录、超时函数及实际 StopHost 的模拟执行；覆盖主环冻结后生命周期继续记录、方向位被清后仍跟踪、重新分配通道、独立环覆盖、全通道等待结果和原返回值保留。模拟为硬件通道不自行清使能的情况，不等同于真实硬件复现。
- 原超时回归测试通过。删除新增诊断调用后，底层原条件、寄存器写入、等待循环和返回值与初版未加钩子备份逐 token 一致。
- 本次源码改动为 `usb_task.c`、`usb_rx_trace.h`、`stm32f4xx_hal_hcd.c`、`stm32f4xx_ll_usb.c`。补丁脚本和测试同步更新；本目录 `before` 保存上版文件。
- 同次固件归档于本目录 `firmware`。调用地址请用对应版本的 AXF/MAP，原版地址不要沿用。

记录使用短临界区，不调用 printf、RTOS 阻塞接口或 SD 写入。观测本身会增加执行时间，可能改变复现概率；USB 寄存器仍是顺序读取，并非硬件同时锁存。重新生成驱动或更新 HAL 可能覆盖钩子，需对照诊断说明检查保留情况。
