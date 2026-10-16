# F405 受控 USB Host 内核重建与重新枚举

本版按用户要求先实现异常后的恢复，不修改 `USB_StopHost()` 的共用计数、等待上限或原返回值；底层 LL 文件与修改前逐字节相同。主板工程、SD 文件系统、RTC 和写卡任务未修改。新增注释采用 UTF-8 中文。

## 测试时看什么

重新编译下载 F405，Watch 添加：

```c
g_usb_host_recovery_diag
g_usb_rx_diag.raw_bytes
g_usb_stop_first_failure
```

| 字段 | 含义 |
|---|---|
| `requests / attempts` | 应用提出的恢复请求 / Host 线程已执行的恢复次数 |
| `core_restarted` | 底层初始化、启动和 VBUS 接口成功返回的次数，尚不等于通信恢复 |
| `restored` | 重新枚举后，应用 READY、新会话 ACK 到达且有新的接收完成回调 |
| `failed / last_status` | 内核重建接口失败次数 / 最近失败阶段及状态码 |
| `reason` | 最近请求原因，见下表 |
| `request_tick / start_tick / restart_tick / restored_tick` | 请求、执行、内核启动成功、握手确认时的 HAL 毫秒 tick |
| `last_duration_ms` | 本次内核重建接口执行时长，不含后续完整枚举/握手等待 |
| `last_hcchar / last_urb_state` | 触发通道异常恢复时的状态；枚举超时和失败重试填 0 |
| `discarded_ready_packets / discarded_ready_bytes` | 恢复执行时已收到但未进入记录环形缓存的暂存数据；不包含无法精确统计的 USB 在途数据 |

复现测试的预期是 `attempts` 增加、`core_restarted` 随后增加，接着 `restored` 增加，并观察 `raw_bytes` 恢复持续增长。若只见 `core_restarted` 增长、`restored` 不变，说明初始化接口返回成功，但枚举/握手还没有确认成功。

首次失败锁存和原诊断结构不清零。USB 内核恢复后，`g_usb_stop_first_failure` 仍可读。重新编译会改变地址，请使用具名变量，不沿用旧截图的固定 RAM 地址或旧版 SAVE 范围。

## 状态和触发条件

| state | 状态 |
|---|---|
| 0 | IDLE，正常检测 |
| 1 | REQUESTED，等待 Host 线程执行；应用暂停访问 Host/CDC |
| 2 | RUNNING，正在重建；应用暂停访问 Host/CDC |
| 3 | WAIT_LINK，内核已启动，允许正常枚举和应用握手 |
| 4 | FAILED，底层接口失败，关闭 USB 中断并等待重试 |

| reason | 触发原因 |
|---|---|
| 1 | 接收 pending 超过 2000ms，实际 IN 通道 CHENA、CHDIS 同时为 1 |
| 2 | 接收 pending 超过 2000ms，URB 为 ERROR 或 STALL |
| 3 | 重建成功后超过 10000ms 仍未确认握手，且端口检测到设备连接 |
| 4 | 重建接口失败后，达到 5000ms 冷却时间重试 |

通道异常检测要求应用为 APPLICATION_READY、无待消费接收包、无环形缓存反压、CDC 和通道下标有效。普通无数据、URB_IDLE 且没有异常启停位时，不因 pending 很久就强制重建。每次尝试之间至少 5000ms，避免持续快速复位。重建后等待设备连接时，不会仅因物理设备不在就每 10 秒反复重建；底层初始化失败的重试单独按 reason=4 执行。

接收触发阈值由 `usb_task.h` 的 `USB_HOST_RECOVERY_TIMEOUT_MS` 控制，默认 2000ms；原 1000ms 超时快照仍保留。恢复过程期间先检查恢复状态，再访问诊断中的 Host/CDC 指针，避免重建释放类数据时另一个任务读取旧指针。

## 恢复顺序

1. usb_task 标记 REQUESTED 并向原 Host 消息队列发送唤醒消息。队列满时已有消息可唤醒；任意消息处理前先检查恢复请求。应用后续循环不再提交接收/READY，不访问可能被释放的 CDC 对象。
2. 原 `USBH_Process_OS` 线程调用恢复钩子，先跳过这一轮普通 `USBH_Process`。它与枚举、类处理及接收回调在同一个 Host 线程串行执行，不从中断执行重建。
3. 禁用三个 USB HS NVIC 中断，经 RCC 强制复位 USB HS 内核，终止旧 DMA。
4. 在硬件复位期间释放旧 CDC 类分配并清除 Host 设备、控制管道和枚举状态。保留注册类、用户回调、原 Host 线程和消息队列；不调用 USBH_DeInit/USBH_Init，因此不终止当前线程，不重复创建 RTOS 资源。当前 `USBH_LL_ClosePipe()` 为无硬件操作；这一依赖已核对。
5. 反初始化旧 HCD，恢复原配置并清零旧 HCD 运行状态，重新绑定 Host/HCD；清理应用当前会话和接收暂存，不清 SD 记录环形缓存，也不清首次失败锁存。
6. 释放 RCC 复位、清 USB NVIC pending，在任务上下文等待 20ms，然后检查 HAL_HCD_Init、HAL_HCD_Start、VBUS 接口返回值。绕开原 LL_Init 中直接进入 Error_Handler 的失败路径。
7. 原 Host 线程继续正常枚举。usb_task 在新的 APPLICATION_READY 下提交接收、使用新会话发送 READY 并等待 ACK；收到 ACK 和新的完成回调后计入 restored。

此处的“内核复位”仅作用于 USB HS 外设，不是 MCU 整板复位；实际端口复位和重新枚举由标准 Host 状态机完成。数据在途或应用暂存可能丢失，恢复不提供逐条日志的端到端持久化确认。已进入 SD 环形缓存的数据仍由原任务写入；恢复不改变已有换行和断线日志处理策略，因此跨断线的半行仍需在日志审查中识别。

## 失败状态码

`last_status=0` 表示最近内核重建流程的接口成功。非零高 16 位为阶段，低 16 位为原 API 返回值：

| 高 16 位 | 阶段 |
|---|---|
| 1 | 类清理/软件 Host 状态清理 |
| 2 | HAL_HCD_DeInit |
| 3 | HAL_HCD_Init |
| 4 | HAL_HCD_Start |
| 5 | VBUS 接口 |

例如 `0x00030003` 表示 HAL_HCD_Init 返回 HAL_TIMEOUT。失败后释放 RCC 复位、关闭 USB 中断并阻止普通 Host 处理；到冷却时间再由原 Host 线程重试。不在失败路径里主动复位整板。

生命周期事件新增编号：30=RECOVERY_ENTER（detail=reason），31=RECOVERY_EXIT（detail=attempts），32=RECOVERY_FAILED（detail=last_status）。独立生命周期环在主环冻结后仍会记录这些事件。

## 验证及文件

- 源码修改：`User/Task/usb_task.c`、`.h`、`usb_rx_trace.h`，USB Host Core 的 `usbh_core.c`、`.h`。未新增 Keil 源文件条目。
- Core 修改仅增加软件状态清理接口和串行恢复钩子；原停止计数与返回值未改。重新生成或更新 Host 中间件时需保留该钩子，见 `before` 和源码校验清单。
- Keil AC6.22 验证项目编译：0 错误、0 警告。链接图确认 Host 线程调用的是本工程的强恢复函数，而不是默认弱空钩子。
- 新恢复诊断结构为 72 字节；总 RW+ZI=128912 字节，链接通过。静态估计 Host 线程最大调用深度 660 字节，配置栈 1024 字节；usb_task 最大 692 字节，配置栈 2048 字节。这些不是运行时高水位测量。
- `test_recovery.py` 抽取实际恢复服务、触发检测和软件状态清理函数，以模拟接口验证触发边界、正常静默、反压、回调竞争、ISR 拒绝、初始化/启动/类清理失败、枚举超时、计时回绕、失败冷却重试以及 100 次恢复的 CDC 分配/释放平衡与队列/线程身份保留。
- 原事件环、首次失败锁存和超时状态机回归通过；底层 LL 文件与恢复前备份逐字节一致。
- `firmware` 归档同次 AXF/MAP/HEX/调用栈报告；`before` 为恢复改动前源码，`manifest.json` 为源码与固件校验清单。

本次没有下载固件或操作真实硬件。是否能解除已复现的内核卡住，需通过上面的 restored 及持续接收数据在实机上验证。
