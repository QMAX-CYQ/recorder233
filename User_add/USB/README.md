# USB 主板日志支持包：使用与接入

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

## 2. 配置 USB CDC 和 D-Cache 适配

本包接口为 `USB_OTG_HS` / CDC HS 命名，适用基线为 HS 外设配内置 FS PHY，名称 HS 不代表链路一定工作在 480 Mbit/s。CubeMX 生成 USB Device CDC，USB DMA 和 D-Cache 按实际需要配置。

将 `Usb_device/usbd_cdc_if.c/h` 替换或合并到目标 `USB_DEVICE/App` 中对应文件，只编译一份。保留本包队列、READY/ACK 解析、发送完成回调和对齐声明，不能只拷贝 `CDC_Transmit_HS()`。USB 初始化、描述符和底层文件仍由目标工程提供。

`Usb_device/usb_dma_port.h` 仅在 `USB_DEVICE/Target/usbd_conf.c` 的既有 USER CODE 中接入，不作为 C 文件编译，也不放在公共头文件中：

```c
/* USER CODE BEGIN PV：约束生成器定义的 PCD 句柄对齐。 */
extern PCD_HandleTypeDef hpcd_USB_OTG_HS __attribute__((aligned(32)));

/* USER CODE BEGIN 0：在 HAL 头文件之后包含。 */
#include "usb_dma_port.h"

/* HAL_PCD_MspInit 的 USB_OTG_HS_MspInit 0：启动 USB 前执行。 */
USB_DMA_ConfigMemory();
```

上述三段分别放入对应钩子，不能粘到同一作用域。CDC 源文件已含 RX/TX 的 32 字节对齐 extern，保留生成器原数组定义，不要再定义第二份。删除旧补丁内联的同名 MPU 函数、链接符号声明和 HAL 包装器，避免重复定义；不要保留强制 `dma_enable=ENABLE` 的历史赋值。

受保护区覆盖 PCD/SETUP、EP0 控制、描述符/字符串、类缓冲及 CDC RX/TX，不是只覆盖日志 TX。Region 7 留给此适配；不要在 USB 运行中动态开启缓存或复用该 Region。其他 USB IP、复合类或对象名改变时须重新适配。

接收端 F405 选择 `COMM_INTERFACE_USB`，使用支持 READY/ACK 的配套固件。主板收到有效 READY 且 ACK 入队后才置 `g_log_link_ready=1`，枚举成功本身不会开始普通日志。任务已周期调用 `LogLinkService_HS()` 和 `CDC_TxService_HS()`，不需要人为强制设置 ready。

需要其他 USB 业务接收时，在用户自己的 C 文件提供 `LogLinkPayloadReceived(const uint8_t *data, uint16_t length)` 强实现；默认弱实现为空。数据仅回调期间有效，延后处理必须复制。`sys_printf()` / CDC 发送返回 `USBD_OK/BUSY/FAIL`，OK 表示已复制入队；普通 INFO 不保证自动重发。

`dcache_handle.c/h` 仅用于其他 UART/SPI DMA 的可选缓存辅助，已有同名函数时合并，不能重复编译。USB 非缓存区方案不依赖每包 Clean；其他可缓存 DMA 的 RX 区必须独占缓存行，不能在 RX 完成后用 Clean 覆盖新数据。

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

## 7. 第一次上板与排错

1. Rebuild 后核对 MAP：故障 UNINIT 区不重叠；启用 USB DMA 时，全部 USB DMA 对象都在 AXI SRAM；缓存与 USB DMA 同时开启时专用区和 MPU Region 7 一致。
2. 两板运行，不暂停/单步。查看 `g_log_link_ready`、`g_log_link_diag`：未握手先检查接收板固件和枚举，再检查 READY/ACK。
3. 查看 `g_cdc_tx_diag`、`cdc_tx_pending_count`、`cdc_tx_inflight`、`g_log_tx_diag`、`g_log_task_diag`。队满、启动失败、格式截断和栈余量要分别处理。
4. 日志应有一次行首时间戳，`seq` 连续、`tick` 按配置周期变化。`WARN: [offline]` 是业务状态，不等于 USB 故障。文件尾半行需与中途缺号区分。
5. 真实故障看 `g_fault_log_diag`、`g_fault_record`、`g_nmi_log_diag`、`g_nmi_record`；选用测试注入前先确认 IWDG 和保留区正确。

| 现象 | 优先检查 |
|---|---|
| `Image$$RW_USB_DMA$$Base/Limit` 未定义 | 编译前脚本是否运行、生成 SCT 是否更新、是否混用了旧 MPU 代码 |
| 枚举失败或开启缓存后异常 | PCD/EP0/描述符是否全在 DMA 可达区、USB 初始化钩子和对齐声明 |
| 枚举成功但没有日志 | READY/ACK、日志任务是否创建、接收板是否选择 USB |
| 重复符号/重复日志 | 是否编译了两份 CDC、日志任务或异常处理实现 |
| 更换电脑后脚本失败 | Before Build 的路径、两个同步脚本、Windows PowerShell、唯一 ioc 文件 |

USB 接口发送完成和 READY/ACK 都不等于 SD 已落盘，普通日志没有逐条持久化确认。本次整理不修改 C/H/SCT 或重新烧录；历史构建/模拟测试和来源工程日志不能代替目标主板的上板验收。
