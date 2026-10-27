# 通信接口

`comm_task` 只负责每 1 ms 调用 `comm_poll()`，不直接引用 USB 类型。`comm.c/h` 提供统一的 `comm_t comm`，包括通信类型、init/process/get_status 函数指针、初始化标志及状态。

## 配置与调用顺序

在 `User/Recorder/control_def.h` 中选择 `COMM_INTERFACE`：

| 值 | 当前支持情况 |
|---|---|
| `COMM_INTERFACE_USB` | USB CDC 接收，逐行添加时间戳 |
| `COMM_INTERFACE_NONE` | 不创建通信任务，不启动 USB Host 接收链路 |
| `COMM_INTERFACE_SPI` | SPI1 从机循环 DMA 接收，逐行添加相同格式的时间戳，当前默认选择 |
| `COMM_INTERFACE_UART` | 只预留编号，选择时编译报错 |

保留 `INFO_FROM_DEVICE` 时当前默认使用 SPI；注释后进入原本地调试模式，通信默认 NONE。可在 `#ifndef COMM_INTERFACE` 内修改选择或通过构建宏覆盖。`USB_COMM` 由选择结果派生，供 CubeMX USB Host 初始化条件使用，不单独定义。原 `usart_comm.c/h` 仍用于既有串口日志，不是新通信抽象下的 UART 接收实现。

当前默认任务先完成 CubeMX 生成的 `MX_USB_HOST_Init()`，然后调用一次 `comm_init()`。后者绑定函数指针并初始化接收后端，全部完成后才置 initialized。通信任务在 initialized 之前不调用后端，可安全等待 USB Host 初始化。USB Host 外设初始化仍归 CubeMX 管理；后端初始化不会重复创建 Host 线程或队列。

`comm_poll()` 由唯一通信任务调用：执行一次 process，再通过 get_status 发布 `comm.status.ready` 和 `comm.status.rx_count`。LED 只依赖这两个字段。USB 的 ready 表示握手完成，rx_count 表示日志数据包数；SPI 的 ready 表示接收 DMA 已启动且没有待恢复错误，rx_count 表示复制到待写缓冲区的数据块数（不是发送端包数）。均不表示落盘条数；两字段不是整体原子快照。

## USB 后端

`usb_comm.c` 的全部实现仅在 `COMM_INTERFACE == COMM_INTERFACE_USB` 时编译；选择 SPI 或 NONE 时，USB 接收缓冲区、诊断变量、CDC 回调及恢复钩子均不生成，不占用该模块的 SRAM。SPI 后端同样只在选择 SPI 时编译。

`usb_comm.c/h` 从原 `usb_task.c/h` 迁移，提供 `usb_comm_init()`、`usb_comm_process()`、`usb_comm_get_status()`。process 执行一轮接收、时间戳、背压及握手服务，不包含任务循环或延时。USB 超时诊断、受控恢复、Host 线程弱钩子和 CDC 回调仍由 USB 后端维护，原 `g_usb_*`、`g_log_host_*` Watch 变量保持命名。

受控恢复仍在原 Host 线程串行执行；`usb_comm_process()` 仅检测并提交恢复请求。迁移没有更改协议、缓存尺寸、发送 READY 周期或恢复阈值。历史诊断归档可能仍引用旧路径，新源码位于本目录。

USB Host 已开启 DMA，直接交给 DMA 的 `usb_buffer` 和 `log_host_ready_frame` 均显式按 4 字节对齐。不能依赖原文件的链接顺序保证字节数组地址对齐；代码重排后地址可能改变。构建后可运行 `diagnostics/comm_refactor/check_usb_dma_layout.ps1` 检查当前 MAP 中这两个缓冲区的实际地址。

## 共用逐行时间戳

USB 与 SPI 均调用 `LogRxFormatTimestamp()`，RTC 启用时前缀为 `MMDD-HH:MM:SS `，关闭 RTC 时为 HAL 毫秒计数加空格。仅在非空行开头插入一次，LF 重新进入行首，CR 和空行保持原样。行首状态跨 USB 包或 DMA 数据块保留。

格式化结果在写入成功前缓存；总环满或写入失败时重试同一份数据，既不重复添加前缀，也不刷新原时间戳。只有整块写入成功后才提交下一行首状态。SPI 每个 512 字节原始块使用独立 4096 字节格式化缓冲区，覆盖最密集的单字符加 LF 输入。`written_bytes` 包含新增前缀，`rx_chunks` 仍统计原始接收块。

测试入口 `diagnostics/spi_comm/run_test.cmd` 覆盖 RTC/毫秒模式、所有分包位置、跨块 CRLF、空行、密集短行、背压、写入失败重试和 DMA 恢复。USB 独立验证工程为 `MDK-ARM/recorder_f405_usb_validation.uvprojx`（`COMM_INTERFACE=1`）。

## SPI 后端

`spi_comm.c/h` 提供 `spi_comm_init()`、`spi_comm_process()`、`spi_comm_get_status()`，由 `comm_init()` 根据 `COMM_INTERFACE_SPI` 绑定。当前默认选择 SPI；也可在 `control_def.h` 的默认选择处改为 `#define COMM_INTERFACE COMM_INTERFACE_SPI`，或使用构建宏 `COMM_INTERFACE=2`。CubeMX 仍负责 SPI/DMA 外设初始化。

SPI1 为从机只接收，8 bit、MSB First、CPOL Low、CPHA 2 Edge、Software NSS。连接主板 SCK → PB3、MOSI → PB5，并共地。DMA2 Stream0 使用 BYTE 对齐、CIRCULAR、HIGH 优先级；`.ioc` 已同步。DMA 缓冲区为普通 SRAM 中按 4 字节对齐的 8192 字节，不能放入 CCM。

循环 DMA 持续接收，不在完成中断中停机重挂。完成回调累计回绕次数；通信任务每 1 ms 结合 NDTR 和待处理 TC 标志读取生产位置，每轮最多复制十六块，每块不超过 512 字节，能处理未达到半满/全满的尾部数据。先复制到独立待写缓冲区并检查 DMA 是否在复制期间覆盖源数据，再使用公共时间戳格式化函数处理，通过 `RecorderDataGetBufferStatus()` / `RecorderDataWrite()` 全量写入 lwrb。总环满或写入失败时保留待写块重试，不在 IRQ 中调用 Recorder/RTOS 接口。

SPI 使用 `log_rx_timestamp.c/h` 与 USB 共用逐行时间戳处理，不解析 USB 的 READY/ACK 控制帧。发送端应提供换行和自身采样时间。Software NSS 单向链路无法确认主板是否连接，也不提供背压；主板须在从机 DMA 就绪后开始发送。总环长时间阻塞会导致 DMA 覆盖，后端保留已复制块并跳过已覆盖的旧字节，累计 `g_spi_comm_diag.overwritten_bytes`；恢复时额外丢弃一块旧数据以留出复制余量，单独累计 `resync_discarded_bytes`，避免持续发送时反复追逐即将被覆盖的数据。复制期间发生覆盖时不提交可能混杂的数据。

`g_spi_comm_diag` 还提供接收块、写入块/字节、总环阻塞、写入失败、启动失败和 SPI/DMA 错误及恢复次数。错误回调仅标记故障，由任务执行 HAL abort 后重新启动；已复制块保留，尚未复制的 DMA 数据失效，错误期间丢失字节无法精确统计。12 Mbit/s 时 DMA 每 5.46 ms 回绕一次，TC 中断须在一个回绕周期内得到处理，否则单个 TC 标志无法记录多次回绕，覆盖统计不再可靠。软件 NSS 下发生位错位时，需要发送端停钟并协调重新同步；自动重启无法保证恢复协议帧边界。

模拟测试：`diagnostics/spi_comm/run_test.cmd`。SPI 构建验证工程：`MDK-ARM/recorder_f405_spi_validation.uvprojx`，使用 `COMM_INTERFACE=2` 和独立输出目录。实际接线、12 Mbit/s 连续接收及 SD 卡阻塞场景仍需实机验证。

## 扩展与生成代码

将来的 UART 后端需要实现同一组接口，在 `comm_init()` 增加绑定，并解除配置头对应的未实现检查；任务和 LED 不需要访问新的底层句柄。

CubeMX 生成后检查 `comm_init()` 位于 Host 初始化之后、`CommTask` 创建和默认任务 LED 刷新仍在 USER CODE 区，并核对 Keil 中 `comm_task.c`、`comm.c`、`usb_comm.c`、`spi_comm.c`、`log_rx_timestamp.c` 的源文件条目，并确认 SPI1 RX DMA 仍为 CIRCULAR/HIGH。USB Host 核心补丁继续按 [工具说明](../../tools/usb_host_recovery/README.md) 恢复。

原生选择/分派测试入口：`diagnostics/comm_refactor/run_test.cmd`（需要匹配本机 MSVC 环境）。恢复回归入口：`diagnostics/usb_recovery_reuse/test_recovery.py`。固件使用主 Keil 工程 Rebuild；原生测试和编译通过不能代替 USB 实机重连、背压与长期记录验证。
