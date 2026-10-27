# SPI 主板日志支持包：使用与接入

本包用于 **STM32H723 主板发送日志**，不是 F405 log 板固件，也不是独立可烧录工程。基线为 Keil ARM Compiler 6、CMSIS-RTOS v1 / FreeRTOS、毫秒 tick。HAL、RTOS、外设初始化和机器人业务对象由目标工程提供。

只选择 USB 或 SPI 中的一套，不同时编译两套同名文件。本文包含日常接入、使用和排错所需步骤，无需再到各子目录寻找说明。

## 1. 复制文件并加入工程

将本目录的子目录复制到目标工程 `User/` 下，保持结构：

```text
目标工程/
├─ 工程名.ioc
├─ MDK-ARM/工程名.uvprojx
└─ User/
   ├─ Log/          日志接口、故障/NMI、告警、SCT 源文件
   ├─ Task/         日志任务和业务采样示例
   ├─ Support/      缓存辅助、DMA 配置、编译前同步脚本
   ├─ Usb_device/   USB 适配头文件及 USB 版 CDC 接口
   └─ examples/     可选 status 业务映射，不作为独立 C 文件编译
```

复制后使用目标工程内的文件，不引用原电脑的 `recorder233/User_add` 路径。Keil Include Path 加入 `../User/Log`、`../User/Task`、`../User/Support`、`../User/Usb_device`。

两种版本都编译 `Log/log.c`、`Log/fault_log.c`、`Log/nmi_log.c`、`Log/fault_log_port.c`、`Task/log_task.c`。需要离线/恢复告警时再加入 `Log/status_log.c`。替换旧日志任务和同名模块，避免重复符号或重复发送。

## 2. 配置 SPI DMA 和接收就绪

另外编译 `Log/spi_log.c`、`Support/dcache_handle.c`。SPI 日志不编译 USB 版 CDC 接口；`Usb_device/usb_dma_port.h` 只在主板仍有其他 USB Device DMA 业务时按本文后面的说明接入。

| 配置项 | 主板发送端要求 |
|---|---|
| SPI | 独占 SPI1，主机仅发送，Motorola |
| 帧 | 8 bit、MSB First、关闭硬件 CRC |
| 时序 | CPOL Low、CPHA 2 Edge，即模式 1 |
| NSS | Software，不使用硬件片选 |
| 速率 | 可先低速验证，12 Mbit/s 的分频按实际内核时钟计算 |
| TX DMA | NORMAL、内存递增、外设不递增、BYTE/BYTE，建议 HIGH 优先级 |
| 中断 | 对应 DMA IRQ 和 SPI1 IRQ 均启用，分别调用 HAL DMA/SPI IRQ 处理函数 |

H7 的发送完成依赖 SPI EOT 中断，只有 DMA IRQ 不够。本包提供 `HAL_SPI_TxCpltCallback()`、`HAL_SPI_ErrorCallback()` 强定义；已有回调需按句柄合并，避免重复。默认使用弱回调机制，`USE_HAL_SPI_REGISTER_CALLBACKS=0`；采用注册机制时自行注册分派。SPI1 不与灯带等业务共用。

已验证主板使用 PA5=SCK、PA7=MOSI，接到 log 板 PB3=SCK、PB5=MOSI，并可靠共地；使用其他引脚须按实际原理图/AF 映射核对，不能照抄旧 PD7。SCK 空闲低电平，下拉用于复位/空闲稳定。12 Mbit/s、约 10 cm 连线的实际工程将 PA5/PA7 的 GPIO Speed 设为 Low 后，Rb38 正常日志样本恢复；Low 控制边沿速度，不改变 SPI 分频，也不保证所有负载均可用。新硬件先核对波形与日志完整性。

F405 配套固件选择 `COMM_INTERFACE_SPI`，同样模式 1、8 bit、MSB，RX 使用循环 DMA，当前接收缓冲 8192 字节。四线 5V/GND/MOSI/CLK 中 5V 是供电，信号为各板 MCU 对应的逻辑电平，不能把 5V 直接接到 SPI 信号脚。

**当前随包 `log_task()` 已调用 `SPI_LogInit()`，等待 3000 ms，再调用 `SPI_LogSetReceiverReady(1U)`。** 这是四线、两板同时上电的固定延时方案，不是真正接收就绪确认；F405 必须先完成 SPI DMA 初始化。不要再叠加另一套初始化任务。若改用就绪 GPIO，应移除任务内自动置 1，改由实际握手状态控制。

`SPI_LogSetReceiverReady(0U)` 阻止后续数据块启动，不截断在途发送。独立复位 log 板时，固定延时不能自动重同步；单向链路也没有 SD 背压或接收 ACK。`SPI_LogService()` 已在日志任务周期调用。

`spi_log_config.h` 默认队列为 32 槽、每槽 768 字节；实际 DMA 暂存区独立、32 字节对齐，放入 AXI SRAM `0x2404E000..0x2404EFFF` 的 `RW_LOG_SPI_DMA`，不能放入 DTCM。发送前由缓存辅助函数 Clean，D-Cache 关闭时跳过维护。

`SPI_LogTransmit()` 完整复制调用方数据，返回 `LOG_SPI_OK/BUSY/FAIL`（0/1/2）；`sys_printf()` 沿用这些返回值。队满拒绝整块，启动失败保留队首重试；传输错误时在途数据进度未知，不自动重发。故障补报等待队列和在途包都排空。

## 3. 配置构建前同步和内存布局

在 Keil **Options for Target → User → Before Build/Rebuild** 启用命令，按目标工程的实际路径填写，例如：

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "D:\你的主板工程\User\Support\sync_dma_memory.ps1" -IocPath "D:\你的主板工程\工程名.ioc"
```

将 **Stop on Exit Code** 设为 **>=1**，脚本失败必须停止构建。目标工程根目录保留唯一的 `.ioc`，使用默认 `MDK-ARM` 工程目录。

脚本先根据 `.ioc` 更新 `Support/dma_memory_config.h`，再调用同目录的 `sync_keil_scatter.ps1`，把 `User/Log/fault_noinit.sct` 同步成 `MDK-ARM/工程名.sct`。两个脚本必须一起复制。生成 SCT 的头文件包含路径会按当前工程位置计算；搬迁后更新上述构建命令即可，不需要原电脑目录。

Keil **Linker** 页取消 **Use Memory Layout from Target Dialog**，Scatter File 填 `工程名.sct`（例如 `rm_main.sct`）。唯一维护的源文件是 `User/Log/fault_noinit.sct`，不要手改生成文件。**CubeMX 把引用改回默认名称是正常的，编译前会刷新其内容**；构建输出应有 `Scatter synchronized`。本包不依赖 CubeMX 生成后钩子，也不在编译时修改 `.uvprojx`。

| D-Cache | USB Device DMA | USB 内存安排 |
|---|---|---|
| 关 | 关 | 不创建专用 USB 区 |
| 关 | 开 | 不创建专用 USB 区，USB DMA 对象仍明确放入 AXI SRAM |
| 开 | 关 | 不创建专用 USB 区 |
| 开 | 开 | 创建 16 KiB `RW_USB_DMA`，由 USB 适配头配置非缓存 MPU Region 7 |

同步依据为 `CORTEX_M7.CPU_DCache` 及当前 USB Device 模式的 DMA 开关，不是运行时自动检测。修改 CubeMX 配置后先生成代码，再 Rebuild；不要靠 C 编译器的单独 `-D` 宏改变 SCT 布局。随包初始配置头不是目标工程检测结果，必须运行同步脚本。

模板按 H723 的 1 MiB Flash、128 KiB DTCM、320 KiB AXI SRAM配置，默认在 DTCM `0x2001F000` 保留 4 KiB 故障/NMI 区。USB 专用区为 `0x24048000` 起的 16 KiB。改用其他 MCU、Bootloader 地址、不同 USB 类时，必须调整 SCT 和对象选择器并检查 MAP。AXI 故障保留区不在 USB 非缓存区内，选择它时需单独解决缓存策略。

## 4. 接入启动、异常入口和日志任务

在 `main.c` 的 USER CODE 包含 `fault_log.h`，在 `main()` 的 `USER CODE BEGIN 1` 最早调用，早于 `HAL_Init()` 和清复位标志：

```c
FaultLog_BootInit();
```

在 `stm32h7xx_it.c` 的 `USER CODE BEGIN Includes` 中加入重命名，让本包提供真实异常入口：

```c
#define NMI_Handler FaultLog_GeneratedNMI_Handler
#define HardFault_Handler FaultLog_GeneratedHardFault_Handler
#define MemManage_Handler FaultLog_GeneratedMemManage_Handler
#define BusFault_Handler FaultLog_GeneratedBusFault_Handler
#define UsageFault_Handler FaultLog_GeneratedUsageFault_Handler
```

保留默认启动汇编，移除旧的同名强定义；不要用普通 C 包装函数调用本包异常入口。在 RTOS 初始化文件的 USER CODE 中包含 `log_task.h`、`log.h`，定义句柄并在创建业务任务前执行初始化：

```c
/* 全局变量区定义一次，log_task.c 仅引用此句柄。 */
osThreadId LogTaskHandle;

/* MX_FREERTOS_Init() 内、创建业务任务前。 */
LogEvent_Init();
osThreadDef(LogTask, log_task, osPriorityLow, 0, 1024);
LogTaskHandle = osThreadCreate(osThread(LogTask), NULL);
/* 创建失败时按目标工程策略处理，不能继续假定任务已运行。 */
```

本基线的 1024 为栈字数，约 4 KiB；其他 RTOS 接口先确认单位。开启 `configSUPPORT_STATIC_ALLOCATION` 和 `INCLUDE_uxTaskGetStackHighWaterMark`，提供 FreeRTOS 所需静态内存回调。格式化函数有 768 字节局部缓冲，需观察栈余量。

**必须适配 `Task/log_task.c` 的业务采样。** 当前示例依赖 FDCAN/UART、电机驱动、IMU、云台及业务任务句柄，并非复制后即可用于任意主板。删除或替换不存在的头文件、extern 和 `log_snapshot()` 字段；保留的外设须先初始化。不必移植整套机器人业务代码。

修改采样内容时，先清零 48 个数据字，再填业务字段；同时维护标签表。改变通道数量时同步修改 `LOG_CHANNEL_COUNT`。当前发送器的 `words[48]`、`n=48` 和格式参数须一致。浮点使用 IEEE-754 原始位模式，解析时按位还原，不能把十六进制整数直接转换成浮点值。

## 5. 日志和可选告警怎么用

任务中包含 `log.h`，例如：

```c
DEBUG_INFO("motor_ready=%lu\r\n", (unsigned long)motor_ready);
/* 需要检查是否入队时，读取 sys_printf() 的返回值。 */
```

日志应使用 CRLF 结束一行，发送块边界不等于行边界。默认 10 通道、每 10 ms 一条 INFO，单通道约 100 ms 一条；文件示例：

```text
1026-01:42:01 INFO: [sys] tick=3002 seq=0 ch=0 n=48 w=0F0BF025/...
```

行首日历时间由配套 F405 接收固件逐行添加，发送端只发送原始文本和自身 `tick`。配套 USB/SPI 接收版本共用时间戳逻辑，并跨数据块保留行首状态。RTC 时间正确不等于正文传输正确。

离线/恢复告警可选：编译 `status_log.c`，在唯一状态任务入口调用一次 `StatusLog_Init(osKernelSysTick())`；每轮完成原离线判定后调用：

```c
StatusLog_Observe(LOG_MODULE_VISION, 1U, vision_online,
                  vision_last_rx_hal, 0U, 0U,
                  osKernelSysTick(), HAL_GetTick());
```

示例变量由业务提供。`active=0` 表示未使用，`online!=0` 表示在线，不要直接套用名称含 offline 的函数返回值。时间单位均为毫秒，HAL/RTOS 时间轴须对应；非 1 kHz tick 先换算。状态机不实现原始超时检测，也不改变控制输出和喂狗条件。

默认启动宽限 5 秒，持续离线每模块约 1 秒提醒，恢复通知一次。事件由日志任务统一消费；拥塞时恢复快照可合并，不保证保存全部抖动历史。`examples/status_log_gimbal.inc` 是可选业务映射，修改后合入状态任务，不单独编译。不从 ISR 调用上述日志/告警接口。

## 6. 故障和 NMI 回报

默认 `FAULT_LOG_TEST_IRAM1=1`，故障保留区在 DTCM；`FAULT_LOG_ENABLE_TEST=0`，不会自动注入异常。保持 IWDG 正常初始化和运行，异常捕获后停止喂狗，等待暖复位；未启用 IWDG 就不会自动重启补报。

日志任务已调用心跳、测试服务及 `FaultLog_Service()`，无需新建故障服务任务。链路允许发送后，优先回报 NMI 四段 `[nmi] part=1/4..4/4`、HardFault 三段 `[hardfault] part=1/3..3/3`，再恢复普通日志。队列满会重试，链路会话变化会从首段重发。异常捕获本身不调用 RTOS、传输发送或 printf。

两份记录、两份 1024 字节应急栈及标记共用 4 KiB UNINIT 区，必须核对 MAP。UNINIT 仅用于跨暖复位保留，不是掉电保存；下载和调试器清 RAM 也可能覆盖。`stack_valid=0` 时 PC/LR 不可信；解析故障地址需保存与烧录版本一致的 AXF/MAP。NMI 时钟寄存器表示进入异常后的现场，不保证还原故障前瞬间状态。

仅诊断时将 `FAULT_LOG_ENABLE_TEST=1`：会周期注入 UDF；`g_fault_test_request` 写 1 开始手动倒计时、写 2 取消手动倒计时，自动模式仍可能继续。`g_nmi_test_request` 写 1 触发软件 NMI。测试会引起复位，软件 NMI 不代替真实时钟故障试验。交付前改回 0，真实异常捕获仍然启用。

## 7. 仍保留其他 USB 业务时

纯 SPI 主板无需编译 USB 初始化或 CDC。若主板另有 USB Device DMA，必须保留其枚举和数据缓存保护：在 `usbd_conf.c` 的 `USER CODE BEGIN PV` 加入 PCD 32 字节对齐 extern，在 `USER CODE BEGIN 0` 包含 `usb_dma_port.h`，在 `USB_OTG_HS_MspInit 0` 调用 `USB_DMA_ConfigMemory()`；CDC RX/TX 数组也需 32 字节对齐声明。仅声明生成器已定义的对象，不重复定义。

PCD 示例声明为 `extern PCD_HandleTypeDef hpcd_USB_OTG_HS __attribute__((aligned(32)));`。USB 保护覆盖 SETUP、EP0、描述符、类缓冲及 RX/TX；仅 Clean 日志 TX 不够。删除旧的同名 MPU 实现和强制开启 USB DMA 赋值；Region 7 不可被其他模块占用。修改 USB 类或缓冲段名时同步检查 SCT 选择器。

## 8. 第一次上板与排错

1. Rebuild 后核对 MAP：`.bss.spi_log_dma` 在 `0x2404E000`、32 字节对齐，故障区及可选 USB 区互不重叠。构建输出应有 `Scatter synchronized`。
2. 确认实际烧录的是 SPI 版 log 板固件及正常 DMA 发送主板固件，排除临时 CPU 固定文本诊断版本。
3. 两板同时上电运行，至少等待当前任务的 3 秒。主板看 `g_spi_log_link_ready`、`g_spi_tx_diag.started/completed/errors/start_failed`；排空要求 `spi_tx_pending_count==0` 且 `spi_tx_inflight==0`。
4. log 板看 `g_spi_comm_diag`：接收、错误、覆盖丢弃计数；主板本地完成不能代替接收端检查。文件应有一次时间戳，`seq` 连续、`tick` 按周期变化。
5. 主板其他观察项为 `g_log_task_diag`、`g_log_tx_diag`、`g_log_event_diag`、`g_fault_log_diag`、`g_nmi_log_diag`。不要照搬其他工程才有的快照变量名。

| 现象 | 优先检查 |
|---|---|
| 一直等待、不发数据 | SPI/DMA 初始化、日志任务是否运行、接收允许门控 |
| 只发一块或完成数不增长 | SPI EOT IRQ、DMA IRQ、HAL 回调冲突、句柄是否独占 |
| 主板完成、log 无数据 | 实际 SCK/MOSI 引脚、AF、共地、接收板固件和 DMA 是否启动 |
| 成段乱码/位错位 | 两端模式和 FirstBit、时钟毛刺、边沿速度、线长及地回路；不能靠改文本编码或增大缓冲区修复 |
| CubeMX 后内存布局恢复默认 | 确认 Before Build 命令启用、两个脚本均已复制、Stop on Exit Code 为 >=1 |
| `Image$$RW_USB_DMA$$Base/Limit` 未定义 | 可选 USB 保护配置与 SCT 是否一致，是否残留旧的无条件链接符号引用 |

单向 SPI 无逐包接收或 SD 落盘确认，持续 12 Mbit/s 上限约 1.5 MB/s，缓存和 SD 吞吐需按业务验证。文件末尾半行不能直接当成中途丢包。本次整理不修改 C/H/SCT 或重新烧录；Rb38 是当前实际主板的样本结果，不代表包迁移到任意主板后均已通过验收。
