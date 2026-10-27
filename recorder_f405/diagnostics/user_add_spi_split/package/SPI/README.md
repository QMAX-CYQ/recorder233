# H723 主板 SPI DMA 日志支持包

本目录由原 USB 支持包派生。保留 `sys_printf()`、DEBUG 宏、事件队列、
status 告警、FaultLog 和 NMI 补报接口，底层替换为 `HAL_SPI_Transmit_DMA()`。
SPI 源文件不引用 USB Device、CDC、USB 初始化或 USB 握手协议。

## 文件及接入

1. 仅将本目录加入日志头文件路径，编译 `log.c`、`log_task.c`、`spi_log.c`、
   `dcache_handle.c`、`fault_log.c`、`nmi_log.c`、`fault_log_port.c`、`status_log.c`。
   排除原日志任务和同名实现；不添加 USB 版本的 `usbd_cdc_if.c`。
2. CubeMX 配置独占的 SPI1：主机只发送、Motorola、8 bit、MSB First、
   CPOL Low、CPHA 2 Edge、Software NSS、关闭硬件 CRC，SCK 为 12 Mbit/s。
   分频值由实际 SPI 内核时钟决定，不固定为某个数值。
3. 启用 SPI1 TX DMA：NORMAL、内存递增、外设不递增、BYTE/BYTE 对齐，
   建议 HIGH 优先级。启用对应 DMA IRQ 及 SPI1 IRQ，并分别调用
   `HAL_DMA_IRQHandler()` 和 `HAL_SPI_IRQHandler()`。H7 的完成回调依赖
   SPI EOT 中断，只有 DMA IRQ 不足以完成发送收尾。
4. 使用本目录 `fault_noinit.sct`，关闭 Keil 自动内存布局，重新构建并核对 MAP。
   DMA 暂存区 `.bss.spi_log_dma` 必须处于 AXI SRAM `0x2404E000..0x2404EFFF`，
   且按 32 字节对齐。每次发送前使用 `CleanDCacheForDmaTx()`，缓存关闭时
   自动跳过维护；不需要 USB 专用 MPU 区或 `usb_dma_port.h`。
5. SPI1 不得同时用于 WS2812 灯带或其他驱动。本包提供两个 HAL 强回调：
   `HAL_SPI_TxCpltCallback()`、`HAL_SPI_ErrorCallback()`；若工程已有回调，
   须合并句柄分派，不能重复定义。默认使用 HAL 弱回调机制，
   `USE_HAL_SPI_REGISTER_CALLBACKS` 须为 0；启用注册机制的工程须自行注册分派。
6. 主板 SCK/MOSI 与 F405 的 PB3/PB5 对接并共地。F405 选择 SPI 接收后端，
   参数与主机匹配；建议接收 DMA 缓冲区 8192 字节以适应 12 Mbit/s。

初始化顺序示例：

```c
#include "spi_log.h"

/* 先完成 HAL、DMA、SPI 的 CubeMX 初始化。 */
MX_DMA_Init();
MX_SPI1_Init();
SPI_LogInit();

/* 由应用在确认记录板接收 DMA 已启动后执行，例如使用额外就绪 GPIO。 */
SPI_LogSetReceiverReady(1U);
```

`log_task()` 入口也会调用一次可重复调用的 `SPI_LogInit()`，周期调用
`SPI_LogService()`。如果应用提前调用初始化，须在 SPI/DMA 初始化后执行。
记录板复位、断开或需要重新同步时，调用 `SPI_LogSetReceiverReady(0U)`；
再次确认接收就绪后置 1。置 0 不截断正在发送的数据，只阻止后续数据块启动。

SPI 单向链路没有 USB 的 READY/ACK，不会自动探测接收板。默认就绪为 0，
未允许发送时日志任务等待；业务接口可以在本地初始化后排队。
如果应用只能采用固定开机延时，须自行确保接收板先启动且延时足够；
固定延时不能检测接收板重启或 SD 卡背压。

## 队列、状态与错误

默认 32 个队列槽，每槽最多 768 字节，独立 DMA 暂存区为 768 字节。
配置位于 `spi_log_config.h`。`SPI_LogTransmit()` 完整复制调用方缓冲区，
返回 `LOG_SPI_OK/BUSY/FAIL`（0/1/2）；OK 仅表示入队，满队列时不部分入队。
`sys_printf()` 返回值也使用这三个常量，`log.h` 已包含对应定义。

启动成功后释放队列槽，暂存区保持不变直到 H7 HAL 的发送完成回调；
回调会接续下一块，不受日志任务 10 ms 周期限制。`spi_tx_pending_count`
表示尚未启动的块数，`spi_tx_inflight` 表示存在在途发送。只有两者均为 0
才视为本地排空，故障补报据此等待最后一块完成。

`g_spi_tx_diag` 记录入队、完成、启动失败、队满、错误及恢复统计。
启动失败保留队首重试；传输错误记录在途块长度，由任务中止并重启。
未知已发送进度的数据不自动重发，尚未启动的队列保留。
`error_lost_bytes` 是错误时在途块的长度，不代表精确丢失字节数。
错误及暂停使本地会话变化，故障补报会重新从首段安排发送。
需要位同步恢复时，必须协调记录板停钟及重启接收；本地自动恢复不能
保证接收端的字节边界。DMA 完成不证明 F405 已写入 SD 卡。

SPI 主板按原始字节流发送，不含 USB 控制帧；F405 接收端与 USB 共用逐行时间戳处理，文件中每个非空日志行开头都会添加 MMDD-HH:MM:SS 时间戳。
保留日志自身 `tick` 字段及 CRLF 换行。当前接口无接收侧背压，主机持续
12 Mbit/s 可达 1.5 MB/s，接收缓存和 SD 写入速度仍须实机验证。

## 日志、故障与告警接入

在 `main()` 最早、`HAL_Init()` 前调用 `FaultLog_BootInit()`。
将 CubeMX 生成的 NMI/HardFault/MemManage/BusFault/UsageFault 入口通过宏
改名为 `FaultLog_GeneratedNMI_Handler` 等，让本包提供真实异常入口。
禁止用普通 C 包装函数调用异常入口。

创建业务任务前调用 `LogEvent_Init()`，创建一个 `log_task`，保留
`LogTaskHandle`。CMSIS-RTOS v1 示例栈深度 1024 字，需实机核对栈余量。
保留 `configSUPPORT_STATIC_ALLOCATION`、任务栈余量查询支持与 IWDG 配置。
`log_snapshot()` 使用当前主板业务对象，迁移到其他主板时须自行适配。
status 告警和 NMI 记录格式分别见 `STATUS_ALERTS.md`、`NMI_REPORTING.md`。
故障捕获核心及 UNINIT 保留区策略沿用原包，默认关闭故障注入。

本包未自动修改实际主板工程、回调、CubeMX 配置或烧录硬件。
验证说明见 `VALIDATION.md`。
