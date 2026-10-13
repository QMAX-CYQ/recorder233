# 发送端 BUSY 缓冲

已应用到 `D:/RM/rm_main_gimbal` 的四个文件：

- `USB_DEVICE/App/usbd_cdc_if.c`：8 槽 FIFO，各槽最多 2048 字节；排队复制数据，完成回调继续启动队首；PRIMASK 临界区保护多任务/回调访问。
- `USB_DEVICE/App/usbd_cdc_if.h`：发送语义说明、服务接口及新增统计。
- `User/Task/debug_task.c`：每个调试循环尝试启动队首，保证枚举完成或启动失败后可继续。
- `User/Support/log.c`：更新发送统计的语义注释。

`USBD_OK` 表示数据已复制并接受，不代表已发送完成。`USBD_BUSY` 现在表示待发 FIFO 满；保留旧数据，拒收新数据并计数。`USBD_FAIL` 表示参数错误。日志和视觉发送共用 FIFO，保持提交顺序。

最多 8 条待发，另有 1 条在途。额外静态内存约 16KB。一般 10ms 日志周期下可缓存约 80ms 的待发日志，视觉数据也占槽；长期停传或长期未枚举仍会满队列。

断开连接时保留尚未启动的队列。已启动但尚未收到完成回调的传输计入 disconnect_lost，不自动重发，避免部分已到达的数据重复。没有加入 USB 超时恢复或重新枚举逻辑。

## Watch

- `cdc_tx_pending_count`：尚未启动的队列深度，0～8。
- `cdc_tx_inflight`：是否有本接口启动的在途传输，0/1。
- `cdc_tx_queue_overflow_count`：队列满、拒收次数。
- `g_cdc_tx_diag.buffered`：提交时 USB 忙/未连接/已有排队的累计接受次数。
- `g_cdc_tx_diag.queue_high_water`：入队后峰值，立即发送的数据也会短暂占槽。
- `g_cdc_tx_diag.accepted` / `g_log_tx_diag.accepted`：接受入队次数，不再等同于立即启动次数。
- `g_cdc_tx_diag.started` / `started_bytes`：成功启动 USB 传输次数/字节数。
- `g_cdc_tx_diag.completed` / `completed_bytes`：完成回调次数/字节数。
- `g_cdc_tx_diag.busy` / `g_log_tx_diag.busy`：FIFO 满导致的拒收次数。
- `g_cdc_tx_diag.start_failed`：启动失败尝试次数，数据仍留在队首。
- `g_cdc_tx_diag.disconnect_lost` / `disconnect_lost_bytes`：断开时尚未完成的传输数量/字节数，不保证这些字节全部未到达。

稳态同一快照应满足 accepted = completed + pending_count + inflight + disconnect_lost；调试器运行中分项读取可能产生暂时差异。

## 验证

原工程仍指定本机缺少的 ARM Compiler V6.24。独立副本 `D:/RM/rm_main_gimbal/MDK-ARM/rm_main_tx_queue_validation.uvprojx` 使用已安装的 V6.22，构建通过，0 错误、0 警告；ZI 从 44384 增加到 60848 字节。未烧录。

`make_queue_test.py` 从实际队列实现提取代码，配合原生 C 测试桩，验证 BUSY 缓冲、FIFO 顺序、源缓冲生命周期、满队列拒收、完成连续排空、启动失败重试、未连接入队、断开计数和队列保留、IRQ 状态恢复及参数检查，全部通过。测试不能替代硬件 USB 中断与断开重连验证。

原始文件备份在 `originals`，编译日志为 `sender_queue_build.log`。
