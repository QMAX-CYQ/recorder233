# F405 底层 USB 接收事件环形记录

> 本文为初版说明。当前工程已加入独立生命周期环，主环冻结后仍可记录断开、StopHost 和初始化。请优先阅读 [第二版说明](../usb_rx_trace_v2/README.md)。本目录 `firmware` 仍保留初版固件，第二版固件单独归档，不混用调用地址。

本版配合已有 1000ms 接收超时快照，追踪超时前的驱动事件。仅写普通 RAM，不主动停止/重启 USB，不改变原驱动返回值、寄存器写入、握手或 SD 日志内容。新增注释采用 UTF-8 中文。

## 复现时看什么

Keil Watch 添加：

```c
g_usb_rx_trace_diag
g_usb_rx_timeout_diag
```

正常运行时 `written` 不断增加，64 条记录循环覆盖，`overwritten` 增加是正常的环形覆盖计数，不是 USB 丢包数。

同一次接收请求超过 1000ms，抓取超时快照后自动追加 TIMEOUT 事件并冻结环形记录，此时：

- `frozen=1`：事件证据已冻结，后续中断不会覆盖。
- `freeze_request_id`：与超时快照的 `request_id` 对应。
- `freeze_tick`：冻结的 HAL 毫秒 tick。
- `valid_count`：有效记录数，上限 64。
- `next_index`：下一写入位置；满 64 条时也是最旧记录的位置。
- `entries[]`：展开查看事件，`valid=1` 才表示写入完成。

保存整个结构以及 `g_usb_rx_timeout_diag.history[latest_index]`（该下标来自超时诊断结构）。首次超时和最近超时可能不是同一次，不要总是只看 `first`。本次仍未实现自动恢复，若真卡住应保持冻结，方便读取。

如果仅是正常静默/启动等待而触发超时，之后接收完成、请求替换或离开就绪态，usb_task 会解除冻结并继续记录，`resume_count` 增加。环形记录不额外复制首次冻结内容；如已恢复，其旧证据会逐渐被覆盖。与超时结构的首次快照保留策略不同。

记录没有写入 SD，复位或重新下载会清空；不建议运行中手工清零或更改元数据。调试暂停也可能影响 USB 时序并触发超时。

## 顺序和字段

不足 64 条时从 `entries[0]` 读到 `entries[valid_count-1]`；满 64 条时从 `entries[next_index]` 开始，读到 63 后从 0 继续，共 64 条。最新一条位于 `(next_index + 63) % 64`，冻结时通常是 TIMEOUT。

`sequence` 从 0 累计，自然回绕；`tick` 为 HAL 毫秒 tick。同一毫秒内用环形顺序/sequence 区分先后。`ipsr=0` 表示线程模式，非零为异常号。`channel=0xFFFFFFFF` 是连接或超时等全局事件，通道字段填 0，不能将其解释为实际通道状态。

每条记录保存：事件编号、detail、执行上下文，以及 HCCHAR、HCINT、HCINTMSK、HCTSIZ、HCDMA、HCD 状态/URB 状态/错误计数/已接收长度/PID/切换位、HFNUM、GINTSTS 和 HNPTXSTS。CPU 临界区保证记录不被中断嵌套写坏；USB 硬件仍运行，寄存器不是同时刻硬件锁存。

| 编号 | 名称 | 抓取位置 / detail |
|---|---|---|
| 1 | SUBMIT_BEFORE | HAL 提交入口，尚未清除旧软件状态；detail=长度 |
| 2 | SUBMIT_PREPARED | HAL 已准备新状态，尚未启动通道；detail=长度 |
| 3 | START_BEFORE | LL 已准备长度/DMA，即将写 HCCHAR；detail=长度 |
| 4 | START_AFTER | 写入通道启动控制后；detail=长度 |
| 5 | SUBMIT_RETURN | LL 启动返回；detail=HAL 返回码 |
| 6 | IRQ_ENTER | IN 中断处理前、清除标志前；detail=有效中断位（去除 NAK/ACK） |
| 7 | IRQ_EXIT | IN 中断处理结束；detail=入口有效中断位 |
| 8 | HALT_ENTER | LL 停止通道入口；detail=调用者返回地址，可用同版本 AXF 定位 |
| 9 | HALT_EXIT | LL 停止函数返回，包括无需停止的提前返回；detail=源码行号 |
| 10 | REARM_BEFORE | IN 中断中重新激活前；detail=源码行号 |
| 11 | REARM_AFTER | IN 中断中重新激活后；detail=源码行号 |
| 12 | APP_SUBMIT | usb_task 提交接收前；detail=上层请求编号 |
| 13 | APP_RETURN | USBH_CDC_Receive 返回后；detail=USBH 返回码 |
| 14 | CALLBACK | CDC 接收完成回调入口；detail=累计回调编号 |
| 15 | TIMEOUT | 接收超时并冻结；detail=请求编号 |
| 16 | CONNECT | Host 连接回调 |
| 17 | DISCONNECT | Host 断开回调 |
| 18 | PORT_ENABLED | Host 端口使能回调 |
| 19 | PORT_DISABLED | Host 端口禁用回调 |
| 20 | URB_NOTIFY | HAL 通知上层 URB 状态变化；detail=URB 状态 |

只追踪当前 HS 实例、实际配置为 Bulk IN 的通道，不写死通道 4；枚举控制传输和 READY 的 Bulk OUT 不记录。普通 NAK/ACK 不单独产生 IRQ_ENTER/EXIT，相关停止/重激活若发生仍会记录。事件开销会对时序有一定影响，不代表零扰动测量。

HALT_EXIT 只表示原函数已返回，不能当作硬件停止完成；是否收到 CHH 需要看后续 IRQ_ENTER 的 HCINT。SUBMIT_RETURN/APP_RETURN 成功也不证明实际收到数据。

## 集成和验证

- 新头文件：`User/Task/usb_rx_trace.h`；实现放在现有 `usb_task.c`，无需新增 Keil 源文件条目。
- 驱动钩子：`stm32f4xx_hal_hcd.c`、`stm32f4xx_ll_usb.c`；连接及 URB 回调钩子：`USB_HOST/Target/usbh_conf.c`。这些钩子在重新生成目标文件或更新 HAL 时可能被覆盖；本文和 `before` 备份保留了位置依据。`instrument_driver.py` 是本次一次性补丁脚本，不是构建步骤，不要对已有其他改动的文件盲目重跑。
- 环形诊断结构占 5500 字节 RAM。编译检查 RAM 区域可容纳；usb_task 静态最大调用栈估计 568 字节，原任务栈 2048 字节，不等于运行时高水位。
- AC6.22 Keil 验证项目：0 错误、0 警告。未下载固件或操作调试现场。
- `test_trace.py` 抽取实际记录/冻结/超时函数，测试环形覆盖、筛选、原始标志保存、冻结和恢复、IRQ 掩码、序号回绕、回调竞争及超时联动；还对比删除诊断钩子后的底层源码，确认原驱动控制流程与备份一致。
- 原 `usb_rx_timeout/test_timeout.py` 回归通过。
- `firmware` 归档同次构建的 AXF/MAP/HEX/调用栈报告，分析 HALT_ENTER 的调用地址时使用对应版本。

事件历史只有 64 条，持续高速错误可能覆盖较早的触发点；该诊断有助于定位因果顺序，但不保证捕获任何形式的硬件故障根因。
