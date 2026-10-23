# 通信接口

`comm_task` 只负责每 1 ms 调用 `comm_poll()`，不直接引用 USB 类型。`comm.c/h` 提供统一的 `comm_t comm`，包括通信类型、init/process/get_status 函数指针、初始化标志及状态。

## 配置与调用顺序

在 `User/Recorder/control_def.h` 中选择 `COMM_INTERFACE`：

| 值 | 当前支持情况 |
|---|---|
| `COMM_INTERFACE_USB` | 已实现，外部日志模式默认选择 |
| `COMM_INTERFACE_NONE` | 不创建通信任务，不启动 USB Host 接收链路 |
| `COMM_INTERFACE_SPI` / `COMM_INTERFACE_UART` | 只预留编号，选择时编译报错，尚未实现接收后端 |

保留 `INFO_FROM_DEVICE` 时默认使用 USB；注释后进入原本地调试模式，通信默认 NONE。可在 `#ifndef COMM_INTERFACE` 内修改选择或通过构建宏覆盖。`USB_COMM` 由选择结果派生，供 CubeMX USB Host 初始化条件使用，不单独定义。原 `usart_comm.c/h` 仍用于既有串口日志，不是新通信抽象下的 UART 接收实现。

当前默认任务先完成 CubeMX 生成的 `MX_USB_HOST_Init()`，然后调用一次 `comm_init()`。后者绑定函数指针并初始化接收后端，全部完成后才置 initialized。通信任务在 initialized 之前不调用后端，可安全等待 USB Host 初始化。USB Host 外设初始化仍归 CubeMX 管理；后端初始化不会重复创建 Host 线程或队列。

`comm_poll()` 由唯一通信任务调用：执行一次 process，再通过 get_status 发布 `comm.status.ready` 和 `comm.status.rx_count`。LED 只依赖这两个字段。ready 表示链路握手完成，rx_count 表示收到日志正文的数据包累计数，不表示落盘条数；两字段不是整体原子快照。

## USB 后端

`usb_comm.c/h` 从原 `usb_task.c/h` 迁移，提供 `usb_comm_init()`、`usb_comm_process()`、`usb_comm_get_status()`。process 执行一轮接收、时间戳、背压及握手服务，不包含任务循环或延时。USB 超时诊断、受控恢复、Host 线程弱钩子和 CDC 回调仍由 USB 后端维护，原 `g_usb_*`、`g_log_host_*` Watch 变量保持命名。

受控恢复仍在原 Host 线程串行执行；`usb_comm_process()` 仅检测并提交恢复请求。迁移没有更改协议、缓存尺寸、发送 READY 周期或恢复阈值。历史诊断归档可能仍引用旧路径，新源码位于本目录。

USB Host 已开启 DMA，直接交给 DMA 的 `usb_buffer` 和 `log_host_ready_frame` 均显式按 4 字节对齐。不能依赖原文件的链接顺序保证字节数组地址对齐；代码重排后地址可能改变。构建后可运行 `diagnostics/comm_refactor/check_usb_dma_layout.ps1` 检查当前 MAP 中这两个缓冲区的实际地址。

## 扩展与生成代码

将来的 SPI/UART 后端需要实现同一组接口，在 `comm_init()` 增加绑定，并解除配置头对应的未实现检查；任务和 LED 不需要访问新的底层句柄。本次没有添加 SPI/UART 接收功能。

CubeMX 生成后检查 `comm_init()` 位于 Host 初始化之后、`CommTask` 创建和默认任务 LED 刷新仍在 USER CODE 区，并核对 Keil 中 `comm_task.c`、`comm.c`、`usb_comm.c` 的源文件条目。USB Host 核心补丁继续按 [工具说明](../../tools/usb_host_recovery/README.md) 恢复。

原生选择/分派测试入口：`diagnostics/comm_refactor/run_test.cmd`（需要匹配本机 MSVC 环境）。恢复回归入口：`diagnostics/usb_recovery_reuse/test_recovery.py`。固件使用主 Keil 工程 Rebuild；原生测试和编译通过不能代替 USB 实机重连、背压与长期记录验证。
