# 日志模块启动握手

两端固件必须配套烧录。本次实现保留 10 通道、每 10ms 一条日志、32 槽队列；仅增加启动就绪握手。

## 启动顺序

1. F405 初始化环形缓冲、挂载 SD 卡、创建日志文件，写入文件头并 f_sync 成功，RecorderLogIsReady 才返回 1。创建失败保持 RS_INIT，后续尝试创建。
2. USB Host 类激活，F405 成功提交接收请求。
3. F405 每 200ms 通过 CDC OUT 发送一次 READY，直到收到匹配会话号的 ACK。
4. H7 从 CDC 接收数据中识别 READY；debug_task 定期尝试将 ACK 加入发送 FIFO。ACK 入队失败时保持等待，不生成日志。
5. ACK 成功入队后 H7 才生成第一条日志；ACK 与日志共用 FIFO，因此 ACK 排在新日志之前。F405 收到 ACK 后接受日志数据。

等待期间不推进生成 seq 或通道，不占用日志队列，不补跑等待期间的循环。第一次生成从 seq=0/ch=0 开始，tick 使用实际运行时间，不强制从 10 开始。重连后保留已累计的 seq/通道计数。

## 控制帧

16 字节固定帧：0～3 为 RMLG；4 为类型（READY=1/ACK=2）；5 为版本 1；6～7 为小端长度 16；8～11 为小端会话号；12～15 为前 12 字节的 FNV-1a 校验值。

支持跨 USB 包、与业务数据合包、重复通知、过期 ACK 和校验失败。控制帧在两端入口过滤，READY 不进入 vision_get_data，ACK 不进入日志解析和 SD 数据。非控制数据保持顺序；F405 会丢弃握手前收到的非控制数据，避免旧会话残留污染新日志。

CDC 断开/重新初始化清除 H7 就绪标志，F405 非 APPLICATION_READY 时清除会话和 ACK 状态，重新建立握手。USB 挂起暂停生成，恢复后沿用已确认会话。已有 FIFO 在断开后仍按原有设计保留；重连前的残留数据可能被 F405 丢弃，本改动不保证断开期间数据恢复。

此协议确认启动可接收/可写卡条件，不是运行中的写卡完成 ACK，也不是 SD 错误恢复或无限期缓存机制。首次 READY 的 USB OUT 传输若底层卡住，主板会保持等待，可通过握手统计诊断。

## Watch

主板：

- g_log_link_ready：握手允许日志生成，正常为 1。
- g_log_link_diag.ready_rx：有效 READY 次数。
- ack_queued、ack_failed：ACK 入队成功/失败次数。
- session：当前会话号。
- waiting_loops：等待期间循环次数。
- cdc_tx_queue_overflow_count：启动阶段应为 0，需实机验证。

F405：

- RecorderLogIsReady 的内部 recorder_log_ready 标志：文件头已同步成功。
- g_log_host_ack_received：收到匹配会话 ACK，正常为 1。
- g_log_host_link_diag.session：应与主板会话号一致。
- ready_attempts、ready_started、ready_completed：READY 尝试、成功提交和完成次数。
- ack_received：首次匹配 ACK 次数。
- tx_failed：READY 提交失败次数。
- discarded_before_ready：握手前丢弃的非控制数据字节数。

正常首次启动应看到两端 ready/ack 为 1，生成 seq 从 0 连续，format_truncated=0、队列溢出不增加。等待时间可以比 32 槽容量更长，因为等待期间不生成日志。

## 修改与验证

主板改动位于 usbd_cdc_if.c/.h 的 CubeMX USER CODE 区及自有 debug_task.c，新增头文件 log_link_protocol.h。F405 改动位于自有 usb_task.c/.h、recorder.c/.h，新增同内容协议头。中文注释、UTF-8 编码。

两端独立 V6.22 工程副本编译通过，0 错误、0 警告。F405 验证副本显式添加 V6.22 标准头文件包含目录，原 V6.24 工程配置保留。

原生 C 测试覆盖每个拆包位置、ACK 与首条日志合包、握手前数据过滤、校验失败、旧会话 ACK、部分标识不匹配、实际主板服务函数的队列满重试、重复 READY、新会话、断开与挂起恢复，均通过。测试不能替代实际 USB 枚举、SD 初始化与开机顺序测试。尚未烧录。

originals 保存修改前两端源码。该目录脚本属于本次修改和测试工具，不保证重复运行安全，不要用历史暂存文件覆盖后续源码。

统计口径：主板 g_cdc_tx_diag 的 accepted/completed 包含 ACK 控制帧，g_log_tx_diag 只统计日志；F405 raw_bytes/tick_lines 已过滤 ACK。握手后初期两端字节/次数可能相差控制帧数量，不能直接按相等关系判断日志丢失。
