# Rb26 原始字节快照定位

Rb26 正文 1736704 字节，00 占 33.852574%，FF 占 64.791179%，
正常 INFO/WARN/ERROR/tick/seq 标签均为 0。此前的 GPIO 速度及空闲电平修正
没有经实机证据证明已恢复通信，当前不能据此认定唯一故障原因。

本次只增加 CPU 可见原始数据快照，不改变 12 Mbit/s、相位、位序或日志格式。
主板及 log 板均需烧录本次诊断版本，并一起复位后运行再暂停读取。

主板 Watch：

```text
g_spi_tx_diag.first_sample_len
g_spi_tx_diag.first_sample
g_spi_tx_diag.last_sample
g_spi_tx_diag.started
g_spi_tx_diag.completed
g_spi_tx_diag.errors
```

log 板 Watch：

```text
g_spi_comm_diag.first_sample_len
g_spi_comm_diag.first_sample
g_spi_comm_diag.last_sample
g_spi_comm_diag.rx_chunks
g_spi_comm_diag.errors
```

数组按十六进制查看，最多保留 32 字节；有效长度由对应 sample_len 指示。
首次快照仅在第一个有效块取出时保存，之后不覆盖；最近快照持续更新。
主板快照表示已成功交给发送 DMA 的 CPU 数据，不能证明引脚上确实发出了它。
接收快照位于时间戳格式化前。正常行首可能为 INFO（49 4E 46 4F）或
ERROR（45 52 52 4F 52）。无 NSS 或启动噪声可能导致首次接收块并非完整行首。

判读：

- 主板 first_sample_len 为 0：未成功启动过发送，先检查主板服务和启动状态。
- 主板为 ASCII，接收端仍为 00/FF：缩小到发送 DMA、引脚链路或接收采样，不能直接断言硬件损坏。
- 接收快照已是正确 ASCII，文件仍异常：再检查接收后的格式化、总环及存储。
- 主板暂停且当前 DMA 已结束后，log 的 NDTR 仍持续变化：检查非预期 SCK 活动；
  若 NDTR 不变但软件接收计数持续增加，则检查生产位置统计。

先比较首次数组，再结合最近数组与错误计数。两端的最近快照不保证取自相同数据块。
仅缺少正常日志标签、或者 HAL 错误计数为 0，都不足以判定物理链路正常。
