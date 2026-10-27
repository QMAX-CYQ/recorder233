# Rb34 固定文本测试结果

正文 131072 字节；完整 `SPI_TEST: 0123456789 ABCDEF\r\n` 为 0 条，
`SPI_TEST` 为 0 次，CR/LF 均为 0。正文开头为接收端 RTC 前缀与 `SPI_I`。
因此 CPU 固定文本发送同样出现损坏，发送 DMA 或发送缓存不是唯一原因。
本次分析未修改两板生产固件或 SPI 参数。

移除唯一的 RTC 前缀后按 MSB 位流查找 `SPI_` 的 32 位序列，得到 3687 个位置。
相邻位置的间隔：284 位 2846 次、285 位 835 次、283 位 3 次、560 位 2 次。
正常固定包只有 29 字节，即 232 位。

进一步构造纯数据模型：前 32 位保持原样，后续位序列将每个 `10` 替换成 `100`，
模型长度变为 284 位。2845 段接收位流与此模型逐位完全一致。
这说明损坏规律与数据跳变高度相关，不像单纯固定字节错位。
模型不等同于实际线上测量，不能凭它证明每个 MOSI 下降沿都产生了 SCK 毛刺，
也不能排除主板实际输出时序、接收端采样及其他共因。

下一步保持 CPU 诊断固件和 3 Mbit/s，先对照更短的 SCK/MOSI 与可靠的地回路。
若有示波器，应在 log 板 PB3/PB5 对本板 GND 同时观察时钟和数据，
重点看 MOSI 下降附近 SCK 是否多次跨过输入阈值，并核对单包是否真正为 232 个时钟。
源端 PA5/PA7 波形正常而接收端异常时，优先查连接和信号完整性。
如果没有示波器，可先缩短至约 5 cm、SCK 邻近地线后重复同一测试。
不根据本份日志直接修改 CPHA、字节序或增大 DMA 缓冲区。

参考：ST AN5543 第 4.5 节将 SCK 干扰毛刺和重配置时的时钟处理列为失步原因。
https://www.st.com/resource/en/application_note/an5543-enhanced-methods-to-handle-spi-communication-on-stm32-devices-stmicroelectronics.pdf

详细计数见 Rb34_analysis.json、Rb34_fixed_packet_analysis.json。
