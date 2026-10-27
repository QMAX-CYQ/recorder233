# Rb33：3 Mbit/s 对照与固定文本诊断

Rb33 原文件只读复制为 Rb33_snapshot.bin。正文 3031040 字节，00 占
0.044902%，FF 为 0；INFO: 出现 295 次、WARN: 出现 3 次，tick=、seq=
均为 0。开头只能识别到本地 RTC 前缀及 INFO9，正文仍然损坏。
其高频字节仍以 18、30、C0、80、01 等为主，与 Rb32 相似。
降至 3 Mbit/s 没有恢复完整日志，尚不能确认是发送 DMA、时钟采样还是信号问题。
接收端时间戳正常并不证明 SPI 正文正常。

## 独立诊断工程

主板目录：D:/RM/wheel_leg_2026/wheel_leg_2025--4/rm_main

- 正常工程：MDK-ARM/rm_main.uvprojx，默认 LOG_SPI_POLLING_TEST=0，保留 DMA。
- 诊断工程：MDK-ARM/rm_main_spi_polling_test.uvprojx，定义 LOG_SPI_POLLING_TEST=1。
- 诊断产物：MDK-ARM/spi_polling_test/rm_main.axf（同目录有 hex）。
- 使用相同 PA5/PA7、3 Mbit/s、模式 1 配置，仅把日志发送改为 CPU 阻塞发送
  只读常量 `SPI_TEST: 0123456789 ABCDEF\r\n`，每次发送至少间隔 10 ms。
- 测试时普通日志不入队，link_ready 保持 0，故 log_task 跳过普通日志及故障补报；
  任务心跳和 SPI_LogService 继续执行。此模式只用于诊断。

只给主板烧录诊断工程，log 板保留当前 SPI 固件。烧录后退出暂停状态，
两板重新同时上电，连续运行至少 15 秒，取得新日志。预期每行为接收端
时间戳加 `SPI_TEST: 0123456789 ABCDEF`。本阶段不会出现普通 INFO 标签。
若观察 Watch，g_spi_tx_diag.polling_test_mode 应为 1，polling_test_completed
应增长，polling_test_failed 应为 0；普通 started/completed 不增长属于预期。

固定文本正常：优先核查发送 DMA 路径及连续发送时序；由于包长与间隔也变了，
不能仅凭此断言 DCache 有问题。固定文本仍乱码：发送 DMA 不是唯一原因，
继续比较接收原始快照与线上采样。测试后烧回正常工程即可恢复 DMA 日志。

两个主板工程均编译 0 Error / 0 Warning，未自动烧录。
本机桩测试验证正常 DMA 队列/回调/恢复仍工作，并验证诊断固定内容、间隔、
就绪门控、HAL 忙态、失败计数及不会调用发送 DMA。尚未进行实机验收。

分析记录：Rb33_analysis.json、Rb33_bit_alignment.json。
编译记录：sender_polling_build.log、Rb33_sender_normal_build.log。
