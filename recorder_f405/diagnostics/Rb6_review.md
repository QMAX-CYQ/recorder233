# Rb6 故障注入测试日志审核

文件 2,179,424 字节；RTC 范围 10 月 15 日 18:15:22～18:16:08。

| 项目 | 结果 |
|---|---:|
| 完整 INFO | 4362 |
| WARN | 288 |
| ERROR | 0 |
| 可辨识启动片段 | 2 |
| tick/seq 同时回退 | 1 |
| 片段内完整 INFO 的 tick/seq 跳变 | 0 |
| 完整物理行格式异常 | 0 |

第一段 INFO：tick=1766～9996ms，seq=0～823，共 824 条，RTC 至 18:15:30。下一轮预期 tick=10006ms，正好满足 10000ms 注入门限，但在发送该条普通 INFO 之前会先执行测试服务，因此正常 INFO 停在 9996ms 与注入逻辑相符。

第二段 INFO：18:15:32 起，tick=446～35816ms，seq=0～3537，共 3538 条。未再次观察到回退；文件尾部还有 tick=35826、seq=3538 的未完成行。实际尾部残行字节数见 rb6_analysis.json。不能将第一次主动测试引起的复位算作普通连续区间的记录丢失。

WARN 涵盖 vision、vtm、gimbal_imu、board、fric_right、fric_left、yaw、pit，各 36 条，last_rx_tick 均为 0。首次约 5 秒开始；offline_ms 按 1023ms 步长增长，符合 33ms 检测周期下约每秒提交一次。复位后离线累计重新计时。没有 recovered 行，与这些模块未收到数据的状态相符。

采样到的诊断中 USB 队列溢出、发送启动失败、格式化错误和截断均为 0；debug_task 最大间隔 10ms，late/catchup 为 0，最大运行时间 2ms，USB 队列高水位 4。

## 当前结论

删除板间在线状态作为喂狗前提后，已经能够运行到计划注入点；复位一次后超过 10 秒不再重触发，形态符合测试一次性标记被保存。但这只是日志支持的推断，不等于已经验证实际异常号、复位来源和故障现场保存成功。

本文件没有任意 ERROR: 字节，也没有 hardfault 补报三分片。故障保存/恢复/补报链路尚未验证通过。当前补报优先级高于普通日志，如果一直 pending，应等待补报；现在普通 INFO 已恢复，说明需重点核查启动是否判定无待报记录，或认为补报已完成但接收端未保留。

## 下一步 Watch

请在本次看门狗复位并恢复运行后读取（不要先重新下载或断电）：

```
g_fault_log_diag
g_fault_record
g_fault_test_latch
```

重点字段及分支：

- reset_after & 0x04000000 非零：启动时记录到 IWDG1 复位标志。
- g_fault_log_diag.invalid=1：启动时保留记录校验不通过；可能未完整保存、未进入保存入口或内容被破坏，不能只凭 invalid 判断是 RAM 物理丢失。
- cold_boot=1：本次按 POR/BOR 冷启动处理，故障记录被初始化；需结合 reset_after 判断。
- valid=1、pending=0、reports=0：保留区有效但没有被识别为待报；查看 g_fault_record.count、pending 和 magic，定位捕获是否提交。
- reports=1：发送端已认为三片完成，应核查接收端日志边界和保存过程。
- pending=1：查看 part、retries、READY/USB 状态，判断补报卡在哪一片。
- g_fault_test_latch 的有效值为 0x46544F4E、0xB9ABB0B1，表示曾到达注入前的标记提交步骤，不能单凭该标记证明 FaultLog_Capture 已执行完成。

若启动后变量仍不足以定位，再在 FaultLog_Capture 保存完毕的死循环处设置断点，核实复位前 g_fault_record.magic、pending、checksum、exception、PC/LR；然后在 FaultLog_BootInit 进入校验前观察是否保持一致。

本次仅分析日志与当前源码，没有修改故障保存或看门狗功能。
