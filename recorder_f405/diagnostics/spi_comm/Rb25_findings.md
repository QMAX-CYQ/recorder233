# Rb25 日志分析及 SPI 空闲电平修正

原文件 `E:/Rb25.txt` 只读复制为 `Rb25_snapshot.bin`，未修改原始日志。
正文为 4734976 字节，00 占 37.638818%，FF 占 61.697990%。
INFO、WARN、ERROR、tick、seq 标签均未检出，尚未恢复正常日志文本。
文件头及 RTC 时间戳由接收板本地产生，不能证明主板正文正确。

当前主板 HAL 在每次发送结束的 `SPI_CloseTransfer()` 中关闭 SPI；原配置
`MasterKeepIOState=DISABLE`，两端 SCK 均无上下拉。软件 NSS 使从机始终选中，
因此空闲 SCK 悬空或切换毛刺可能产生伪数据及位错位。这是待实机验证的原因，
不能仅凭日志字节分布认定已经找到唯一根因。

参考：ST AN5543 的 4.2.2、4.3.2、4.5 节，说明 SPI 禁用时的引脚控制、
AFCNTR 以及 SCK 毛刺导致的失步。
https://www.st.com/resource/en/application_note/an5543-enhanced-methods-to-handle-spi-communication-on-stm32-devices-stmicroelectronics.pdf

本次修改：

- H723：`SPI_MASTER_KEEP_IO_STATE_ENABLE`，PA5 SCK 下拉，保留先前的 VERY_HIGH 输出速度。
- F405：PB3 SCK 下拉；PB5 MOSI 保持原有无上下拉。
- 两端 `.ioc` 同步修改，时钟仍为 12 Mbit/s，CPOL/CPHA 和位序未更改。

两端编译链接均通过，0 错误、0 警告，见 `Rb25_sender_idle_build.log`、
`Rb25_receiver_idle_build.log`。未自动烧录。
须更新两端固件，并同时重新上电，以清除可能已经存在的位错位。
若仍无正常标签，下一步检查实际 MOSI/SCK 波形与主板 DMA 暂存区内容，
再通过降低 SCK 做对照。不能通过删除零字节或更换文本编码修复原始信息丢失。
