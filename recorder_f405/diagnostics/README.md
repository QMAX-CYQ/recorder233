# 日志链路诊断

> 资料归档更新：发送端脚本、暂存代码、源码备份和编译记录已移至 `D:/RM/rm_main_gimbal/diagnostics`。本目录保留日志分析及 F405 接收端资料。下面的初期诊断说明属于历史记录；发送队列的当前说明见主板目录 `diagnostics/README.md` 和 `diagnostics/tx_queue/README.md`。

本次只增加 RAM 统计、时长测量和接收侧 tick 观察。没有新增线上日志、改变日志格式、任务周期、优先级、发送重试、USB 队列、缓存消费方式或 SD 写入策略。统计会带来少量 CPU/RAM 开销。

主板 Keil Watch 添加 `g_debug_task_diag`、`g_log_tx_diag`、`g_cdc_tx_diag`；F405 添加 `g_usb_rx_diag`、`g_recorder_diag`。在连接稳定后记录开始快照，运行 30～60 秒后记录结束快照，用计数增量比较。同一窗口内同时保存日志文件。尽量使用运行中观察，停机断点会影响 USB 时序。

所有计数器从复位起累计，uint32_t 自然回绕。整体结构不是原子快照；发送回调、接收回调和任务的统计更新时间不同，读数暂时不平衡不等于丢失。不要在运行中手工清零；测试之间复位。两块板的 tick 起点不同，使用接收缺口历史中的 `before_tick/after_tick` 对照主板失败历史中的 tick。

## 主板

| 变量 | 含义 |
|---|---|
| `g_debug_task_diag.loops/group_count` | debug_task 总循环及八个日志组执行次数；motor 两个组分别计数 |
| `max_gap_ms/late_count/catchup_count` | 实际循环间隔最大值、超过/小于 10 ms 的次数；用于识别任务未获得调度 |
| `g_log_tx_diag.attempts/accepted/busy/failed` | 仅 sys_printf 日志的发送尝试、USB 类接受、忙、其他失败 |
| `failure_streak/max_failure_streak` | 当前及最长连续失败条数；每条失败仍按原代码丢弃，没有重试 |
| `failure_tick/failure_status/failure_history_next` | 最近八次失败，槽位按累计序号取模 8；USB 状态值见工程 USBD 定义 |
| `format_error/format_truncated` | 格式化失败或超出原有日志缓冲区；只统计，不修正 |
| `g_cdc_tx_diag` | 所有 CDC 发送调用者的统计，包括视觉协议；多任务写入的总计数使用 relaxed 原子加法 |
| `accepted/completed` | USB 类接受的传输与实际完成回调次数，不能直接当作物理 USB 包数 |
| `accepted_bytes/completed_bytes` | 未添加 F405 时间戳的原始发送字节数 |
| `init_count/deinit_count` | USB 类初始化/反初始化次数，用于识别断开重连 |

主板 `USBD_CDC_TransmitPacket()` 会忽略下层提交返回值，所以 `accepted` 本身不证明传输成功，需要结合 `completed`。

## F405

| 变量 | 含义 |
|---|---|
| `g_usb_rx_diag.raw_bytes` | 接收回调实际报告的原始字节数，不含接收侧时间戳 |
| `complete_lines/tick_lines/non_tick_lines` | 按换行重组的行数、解析出 tick 的 INFO 行数、其余行数；可以跨 USB 包解析 |
| `tick_gap_count/estimated_missing` | 相邻 INFO tick 超过 10 ms 的次数，以及间隔为 10 ms 整数倍时推算的缺失条数；调度延迟也可能造成 tick 跳变，需要与主板循环统计核对 |
| `gap_history/gap_history_next` | 最近八处缺口，包括前后主板 tick、F405 检测时间和推算缺失数 |
| `duplicate_tick/backward_tick/irregular_tick_gap` | tick 重复、倒退及非 10 ms 整数倍间隔；uint32 tick 正常回绕按差值处理 |
| `line_bytes_pending/non_crlf_lines` | 当前尚未收到换行的行长度、非 CRLF 结尾行数；未换行本身不证明断帧 |
| `max_callback_gap_ms/max_task_gap_ms` | 收到非空数据的最大间隔，以及 usb_task 的最大运行间隔 |
| `max_pending_ms/max_ready_hold_ms/max_ring_wait_ms` | 等待接收回调、收到数据未入环形缓存、等待缓存空间的最长观察时间；计时不含超时恢复操作 |
| `min_ring_free/format_fail` | 观察到的最低缓存剩余空间、时间戳格式化失败尝试次数 |
| `disconnect_transitions/discarded_ready_packets` | usb_task 观察到的断开转换、转换时尚未处理的接收块数；很短的状态转换可能未被任务观察到 |
| `g_recorder_diag.ring_accepted_bytes/ring_rejected` | 加时间戳之后成功入缓存的字节数及拒绝尝试次数；拒绝不一定丢失，USB 任务会保留并再次尝试 |
| `dequeued_bytes/write_reported_bytes/unconfirmed_bytes` | 从缓存出队、f_write 报告已写、短写未确认的字节数；写入数据不包括文件头和索引元数据 |
| `write_error/write_short/sync_error` | 数据写入错误、非完整块写入、同步错误 |
| `last_write_result/last_sync_result` | 最近 FatFs 返回值，FR_OK 为 0 |
| `write_active/last_write_tick/max_write_ms/max_sync_ms` | 是否正在写、最近开始时间、写入/同步最大耗时 |

已有 `usb_rx_ring_blocked_count`、`usb_rx_receive_fail_count`、`usb_rx_write_fail_count`、`max_lwrb_size` 仍可一起观察。

## 判断方向

1. 主板日志 `busy/failed` 增量与日志缺失相符：优先确认发送端未接受记录。接收 `gap_history` 与主板失败 tick 可以辅助对照。
2. 主板 `max_gap_ms` 较大并出现 catchup：有任务调度延迟；不能仅用 tick 差推算丢包。
3. 主板 accepted 持续增加，但 completed 长期停止：传输完成链路或连接状态异常。
4. F405 `min_ring_free` 低、缓存等待变长，同时写卡耗时大：SD/缓存背压可能使主板发送返回忙。背压是原因候选，发送拒绝是丢日志发生的位置。
5. F405 在原始接收阶段已经观察到 tick 缺口：缺口发生在该解析点之前；仍需主板统计区分未生成、发送拒绝或接收遗漏。
6. 原始接收 tick 连续，但最终文件缺失，同时 write_error/write_short/sync_error 增长：优先检查写盘链路。
7. 只有文件尾截断，写盘错误为 0，缓存仍有不足 16 KB 数据：与现有整块写盘策略一致。

不要直接比较两端“包数”：一个发送记录可能拆成多个接收块。原始字节也应在同一稳定连接窗口、传输排空后比较，视觉发送也计入 CDC 总量。F405 入缓存和写盘字节含 RTC 前缀，不能直接与主板原始字节相减。
