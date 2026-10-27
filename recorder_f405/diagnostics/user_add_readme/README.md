# 主板日志支持包

这两套源码用于 STM32H723 主板，F405 log 板使用其独立接收固件。选择一种通信方式，复制对应目录中的子文件夹到主板工程 User/ 下；不能同时编译两套同名模块。

| 通信方式 | 总使用说明 | 接收端选择 |
|---|---|---|
| USB CDC | [USB/README.md](USB/README.md) | COMM_INTERFACE_USB，使用 READY/ACK |
| SPI DMA | [SPI/README.md](SPI/README.md) | COMM_INTERFACE_SPI，当前随包启动延时 3 秒后允许发送 |

每套 README 均包含文件复制、CubeMX/Keil、DMA/SCT、任务与异常入口、日志与可选告警、故障补报和排错步骤。同步脚本和自定义 SCT 应一并复制；移植后不依赖 recorder233 的原目录。

旧分散文档已归档到整理工作区，不再作为接入入口。固件包不是独立 Keil 工程，业务采样仍须按目标主板适配。
