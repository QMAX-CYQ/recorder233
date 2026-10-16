# Rb5：约 1 秒反复复位，未达到故障注入时间

日志时间范围为 10 月 15 日 18:04:52～18:06:41，文件 1,638,752 字节。逐字节搜索 WARN: 和 ERROR: 均为 0。

完整 INFO 行 3,262 条，可辨识 57 个启动片段，tick/seq 回退 56 次。包含截断行在内，所有 INFO 行头的最大 tick 只有 1026ms。首轮完整记录 tick=226～996、seq=0～77；后续大部分在 tick≈1016～1026ms 结束，再从 tick≈436～456ms、seq≈0～1 重新开始。片段内完整行未发现额外 tick/seq 跳变。

因此本次未达到 STATUS_LOG_STARTUP_GRACE_MS=5000，离线 WARN 尚未开始；也未达到 FAULT_LOG_TEST_DELAY_MS=10000，计划中的 UDF/HardFault 注入没有运行到。看门狗超时本身是硬件复位，不会自动进入 HardFault_Handler 保存异常栈。

当前 status_task 启动即调用 MX_IWDG1_Init。iwdg.c 的预分频为 32、重装值 1000，按工程 LSI 标称 32000Hz 计算超时约 32×1001/32000=1.001 秒，实际受 LSI 频差等影响。这个配置与约 1 秒处反复重启的日志形态吻合，优先怀疑未满足正常喂狗条件。

喂狗要求 comm、gimbal、board、shoot、mode_switch 五个任务心跳全部置位，且 status.board=1。status.board 来自 board_comm_check_offline()，要求板间通信有有效收包且未超时。台架未连接另一块板时，这个条件可能始终为 0。当前日志未包含这些状态的完整快照，不能只凭文件断定具体哪个条件未满足。

确认方法：

- Watch g_fault_log_diag.reset_after。IWDG1 标志位为 0x04000000，应检查按位与，不要求整个寄存器等于该值。
- Watch status.task.comm、gimbal、board、shoot、mode_switch，以及 status.board，检查是否持续无法满足喂狗条件。任务心跳被成功喂狗逻辑清零是正常行为。
- Watch g_fault_test_latch[0]、g_fault_log_diag.pending、g_fault_record.pending：若从未到达注入点，测试标记应未置位且没有新增待报故障；这些值仍需实际硬件观察。
- RCC->RSR 已在启动时保存并清除，日志 sys 通道读取的实时 RCC->RSR 可能为 0；请用 g_fault_log_diag.reset_after 判断本次复位来源。

此外存在 55 条完整物理行格式异常，均含两个 INFO 行头，符合复位时旧 USB 行未结束、新启动行接入同一行的形态；这不是普通连续记录。文件末尾另有 355 字节残行。不能据此报告整个文件无断帧或 0% 丢包。

建议：为了验证 10 秒故障注入，可仅在 FAULT_LOG_ENABLE_TEST=1 的台架模式下允许 status_task 正常运行时持续喂狗，保留故障入口不喂狗的行为；正式模式继续使用原喂狗条件。也可先补齐正常喂狗所需的模块连接和任务心跳。本次只分析，未修改看门狗或故障逻辑。

注意：本报告定位的是为什么没有到达测试注入点，不构成“故障现场保存与补报已在真实硬件验证通过”的结论。
