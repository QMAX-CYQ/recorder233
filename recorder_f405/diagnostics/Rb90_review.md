# Rb90：10 通道压力测试日志审核

完整记录 2066 条，源 tick 跨度 20.65 秒；seq 推算缺失 0 条，缺失率 0.0000%，完整率 100.0000%。
首末 tick=10～20660，seq=0～2065。
tick 间断 0，seq 间断 0，RTC 异常 0；通道映射异常 0，100ms 通道周期异常 0，完整行格式异常 0。

原始行长度 470～482 字节，平均 477.63 字节。按记录覆盖的 10ms 槽位计算，原始吞吐约 47.76KB/s，加 RTC 时间戳后的吞吐约 49.16KB/s（十进制 KB）。
正文为 62 个完整 16KB 块；末尾残行 100 字节，不计入首末完整 seq 区间的缺失率。

| 通道 | 完整行数 |
|---|---:|
| sys | 207 |
| task | 207 |
| fdcan1 | 207 |
| fdcan23 | 207 |
| uart123 | 207 |
| uart6910 | 207 |
| nvic | 206 |
| dji | 206 |
| dm | 206 |
| imu | 206 |

## 日志携带的诊断统计

```json
{
  "debug_stack_min_words": 555,
  "last_debug_stack_words": 555,
  "max_gap_ms": 10,
  "late_count": 0,
  "catchup_count": 0,
  "max_run_ms": 1,
  "queue_overflow_count": 0,
  "queue_high_water": 21,
  "start_failed": 0,
  "format_truncated": 0,
  "format_error": 0,
  "last_line_bytes": 477,
  "max_line_bytes": 482
}
```

日志中诊断字段为对应采样时刻的值，栈水位以 32 位字为单位；不是测试结束后的同步 Watch 快照。仅说明本次短时间测试表现，不保证长期稳定性。

缺失率指生成日志记录缺失率，不是 USB 底层数据包丢包率。

## 文件末尾

```text
1014-16:10:40 INFO: [nvic] tick=20670 seq=2066 ch=6 n=48 w=121BF800/00000000/00000000/070080E8/00000
```
