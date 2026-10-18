# 主板日志发送与故障补报支持包

更新日期：2026-10-17。基于当前 `D:/RM/rm_main_gimbal` 主板实现整合，包含轮转日志、USB CDC 发送队列、READY/ACK、业务告警队列和 fault 捕获/复位后补报。

本目录位于 F405 记录器仓库中，但这些文件供 **H723 发送主板** 使用，不加入 F405 接收工程编译。适用基线为 STM32H723、Cortex-M7、Keil ARM Compiler 6、CMSIS-RTOS v1/FreeRTOS，毫秒 tick。用户工程提供 HAL、CMSIS、FreeRTOS、USB Device、中断与外设初始化；本目录不是独立可烧录固件。

## 文件与默认配置

| 文件 | 用途 |
| --- | --- |
| `log_task.c/h` | 日志任务、10 通道轮转采样、故障心跳与补报优先调度 |
| `log.c/h` | 返回发送状态的 sys_printf、DEBUG 宏、业务事件队列 |
| `usbd_cdc_if.c/h` | CDC 发送队列、发送完成回调、会话握手及可选业务接收钩子 |
| `log_link_protocol.h` | 16 字节控制帧构建、校验、跨包解析 |
| `fault_log.c/h` | 异常入口、应急栈、现场校验及三段 ERROR 补报 |
| `fault_log_port.c/h` | 将 fault 服务连接到本包日志/USB/RTOS 接口 |
| `fault_log_config.h` | 内存选择与故障测试开关，C 和链接脚本共用 |
| `fault_noinit.sct` | H723 Flash/RAM 布局及 UNINIT 保留区模板 |

默认 `FAULT_LOG_TEST_IRAM1=1`，使用 DTCM 写后读回；`FAULT_LOG_ENABLE_TEST=0`，只捕获真实异常，不自动注入。临时 fault_boot、阶段标记和调试器 PC 采样不在此包中。

本包沿用 `log_task` / `LogTaskHandle` / `g_log_task_diag` 命名。不要同时编译原 `debug_task`、原 fault 实现、原 log 实现和本包同名实现；替换旧版本时同步更新 c/h，旧版 void sys_printf 不能与新 fault 适配器混用。

## 接入步骤

1. 配置 USB Device CDC HS、CMSIS-RTOS v1、FreeRTOS 和 IWDG。添加 `log_task.c`、`log.c`、`fault_log.c`、`fault_log_port.c`，并将本目录加入 Include Path。不要同时编译两套同名文件。
2. 把附带 `usbd_cdc_if.c/h` 的发送队列、完成回调和协议处理合入目标 CDC 接口，或在 USB 配置一致时替换现有接口。USB 初始化、描述符及底层配置仍由用户工程提供。随包 CDC 的修改位于原 USER CODE 区；重新生成后仍应检查发送完成回调是否接入。
3. 修改 `log_snapshot()` 以匹配业务。当前示例依赖 `fdcan.h`、`usart.h`、`drv_dji_motor.h`、`drv_dm_motor.h`、`gimbal_task.h`、`prot_imu.h` 等当前主板对象。不存在的采样/extern/头文件需替换，不能仅靠复制这些文件编译；无需移植整套机器人业务代码。UART/FDCAN 的 Instance 应先初始化。
4. Keil Linker 关闭自动内存布局，选用本目录 `fault_noinit.sct`。模板按 1MB Flash、128KB DTCM、320KB AXI SRAM 配置；其他型号或 Bootloader 工程须调整。使用本目录的配置头文件，修改内存宏后 Rebuild。
5. 在 `main.c` 的 `USER CODE BEGIN Includes` 包含 `fault_log.h`，在 `main()` 的 `USER CODE BEGIN 1` 最早调用：

```c
FaultLog_BootInit(); /* 早于 HAL_Init、清复位标志和其他保留区访问 */
```

6. 在 `stm32h7xx_it.c` 的 `USER CODE BEGIN Includes` 加入以下宏，给 CubeMX 的空处理函数改名，真实入口由 fault_log.c 提供：

```c
#define HardFault_Handler FaultLog_GeneratedHardFault_Handler
#define MemManage_Handler FaultLog_GeneratedMemManage_Handler
#define BusFault_Handler FaultLog_GeneratedBusFault_Handler
#define UsageFault_Handler FaultLog_GeneratedUsageFault_Handler
```

保持默认启动汇编即可，不需要新增早期采样。核对最终向量表指向本包真实入口，且不存在其他同名强定义。不要使用普通 C 包装函数调用 naked 异常入口，否则会改变原始 LR/SP。

7. RTOS 初始化文件包含 `log_task.h`、`log.h`，在创建业务任务前调用 `LogEvent_Init()`，再创建一次日志任务：

```c
/* RTOS 全局变量区，log_task.c 仅 extern 此句柄。 */
osThreadId LogTaskHandle;

/* MX_FREERTOS_Init() 内、创建各任务前。 */
LogEvent_Init();
osThreadDef(LogTask, log_task, osPriorityLow, 0, 1024);
LogTaskHandle = osThreadCreate(osThread(LogTask), NULL);
/* 返回 NULL 时按用户工程策略处理。 */
```

源工程 CMSIS-RTOS v1 的 1024 为栈字数，约 4096 字节；其他接口须确认单位。fault 适配器与 sys_printf 各有 768 字节局部格式化缓冲，需在实机检查栈余量。开启 `INCLUDE_uxTaskGetStackHighWaterMark` 和 `configSUPPORT_STATIC_ALLOCATION`，并满足 FreeRTOS 静态分配配置要求。

task 通道还读取其他业务任务句柄；无对应任务时删除采样，或提供 NULL 句柄，`log_stack_words(NULL)` 返回 0。保留一个日志任务，避免旧 DebugTask 和新 LogTask 同时发送。

8. F405 使用配套 READY/ACK 接收固件。IWDG 正常运行，故障发生后停止喂狗等待暖复位；不要让其他异常处理继续喂狗。随后检查 MAP、故障补报及普通日志连续性。

## 任务执行顺序与补报优先级

`log_task()` 已集成以下流程，无需用户再次增加一套故障服务任务：

```text
更新故障心跳
→ LogLinkService_HS / CDC_TxService_HS
→ FaultLog_TestService（发行默认关闭）
→ 未握手：等待
→ FaultLog_Service：有待补报则先补报，暂缓普通日志
→ LogEvent_Service：最多发送一个业务事件
→ 轮转发送一条 INFO 快照
```

`FaultLog_Service()` 返回 1 表示仍待补报。三段 ERROR 全部入队后，还要等待 FIFO 排空且无在途包，并确认会话和断链计数未变化，才清除 pending。断链或会话变化会从第一段重发。等待结束后普通日志重新对齐周期，不突发补发等待期间的快照。

只有日志任务调用 `FaultLog_Service()` 和 `LogEvent_Service()`；其他任务通过事件队列或现有 DEBUG 宏提交。补报期间应避免其他任务持续灌满发送队列。

## CDC 握手与业务接收

主板收到有效 READY 后保存会话，ACK 成功入队才设置 `g_log_link_ready=1`。USB 枚举完成不等于记录板已准备好。F405 未握手时任务保持等待。

`CDC_Receive_HS` 先用 `LogLinkFilter` 解析控制帧，剩余业务数据交给弱函数 `LogLinkPayloadReceived()`，最后重新提交 USB 接收。默认钩子为空，保留旧包忽略业务数据的行为；需要视觉或其他 USB 接收业务时，在用户自己的 c 文件提供同名强实现：

```c
void LogLinkPayloadReceived(const uint8_t *data, uint16_t length)
{
    /* 在这里复制或解析业务数据，例如转交现有视觉协议模块。 */
}
```

data 只在回调期间有效，需要延后处理时必须复制；不能保存临时指针。当前主板的视觉库依赖没有直接带入支持包。

## 日志接口和业务告警

`sys_printf()` 现在返回 USBD_OK/BUSY/FAIL，与 CDC 入队结果一致。DEBUG_ERROR/WARN/INFO/PRINTF 可忽略返回值；fault 和事件服务会依据它决定是否重试。普通 INFO 仍不自动重试，seq 可显示丢弃位置。

同步带入当前主板的业务告警接口：

```c
log_event_t event = {
    .module = LOG_MODULE_BOARD,
    .kind = LOG_EVENT_OFFLINE,
    .offline_ms = 1000U,
    .last_rx_tick = 12345U,
    .can_index = 1U,
    .can_id = 0x101U
};
(void)LogEvent_Post(&event); /* 只允许任务上下文，返回 1 表示入队 */
```

上电在调度器启动前调用 `LogEvent_Init()`；消费者已接入日志任务。静态队列容量为 32，复制事件值而不保存调用者指针，满时返回 0。业务模块名及离线判定由用户维护；本包没有自动读取机器人 status_task 的离线判定。恢复事件使用 LOG_EVENT_RECOVERED。事件 BUSY 时保留队头下次重试，不推进 INFO 的 seq 或通道。

## 按实际业务修改 log_snapshot()

例如，用户只需要记录电机状态和电源状态，可以将通道 0 定义为 `motor`，通道 1 定义为 `power`。保留现有发送函数的 48 字格式，每次先把数组清零，再填入需要记录的字段；未使用的字发送为 0。

假设用户业务模块的 `app_log_data.h` 提供以下定义，并在一个业务 `.c` 文件中定义、更新 `g_app_log_data`：

```c
/* app_log_data.h：字段可按实际业务调整。 */
typedef struct {
    uint32_t motor_online;
    uint32_t motor_error_count;
    float motor_speed_rpm;
    float motor_target_rpm;
    float bus_voltage_v;
    float bus_current_a;
    float temperature_c;
} app_log_data_t;

extern app_log_data_t g_app_log_data;
```

在 `log_task.c` 中包含 `app_log_data.h`，用下面的实现替换整个原 `log_snapshot()`（包括原函数末尾的通用寄存器采样）。保留现有 `log_float_bits()` 辅助函数：

```c
static void log_snapshot(uint8_t channel, uint32_t *w)
{
    app_log_data_t sample;

    /* 本例假设业务数据由任务更新，在短临界区中复制。 */
    taskENTER_CRITICAL();
    sample = g_app_log_data;
    taskEXIT_CRITICAL();

    memset(w, 0, 48U * sizeof(*w));
    switch (channel) {
    case 0U: /* motor */
        w[0] = sample.motor_online;
        w[1] = sample.motor_error_count;
        w[2] = log_float_bits(sample.motor_speed_rpm);
        w[3] = log_float_bits(sample.motor_target_rpm);
        break;
    case 1U: /* power */
        w[0] = log_float_bits(sample.bus_voltage_v);
        w[1] = log_float_bits(sample.bus_current_a);
        w[2] = log_float_bits(sample.temperature_c);
        break;
    default:
        break;
    }
}
```

再修改 `log_task.h` 中的通道数：

```c
#define LOG_CHANNEL_COUNT 2U
#define LOG_PERIOD_MS     10U
```

并将 `log_send_snapshot()` 中的标签表改为：

```c
static const char *const tags[LOG_CHANNEL_COUNT] = {
    "motor", "power"
};
```

任务会按 `motor → power → motor → power` 轮转，仍每 10 ms 一条，每个通道约 20 ms 一条。以下表格就是这份业务日志的字段解释，应随采样代码一起维护：

| 通道 | 数据字 | 含义 | 解码 |
|---|---|---|---|
| motor | w[0] | 电机在线状态 | uint32 |
| motor | w[1] | 电机错误次数 | uint32 |
| motor | w[2] / w[3] | 实际 / 目标转速，rpm | float32 原始位模式 |
| power | w[0] / w[1] | 母线电压 V / 电流 A | float32 原始位模式 |
| power | w[2] | 温度，℃ | float32 原始位模式 |

浮点字段解码时，将十六进制字按 IEEE-754 float32 位模式还原，不能直接把十六进制整数转换成浮点数。也可以改用有符号定点整数，例如以 mV 为单位记录电压，但要同步注明单位和解码方式。

调整时还需注意：

- 原采样不再使用的外设、机器人业务头文件和辅助函数可移除。task 通道也不保留时，可一并移除业务任务句柄 extern 和 `log_stack_words()`；任务创建所需的 `LogTaskHandle` 仍在 RTOS 初始化文件定义。
- `log_send_snapshot()` 当前发送 48 个数据字，`words[48]`、格式中的 `n=48` 和参数列表应保持一致。仅改采样内容及通道数量时，不需要修改发送格式。
- 采样函数只读取或复制业务数据，避免在其中等待外设、执行控制动作或写 SD 卡。数据若由中断或 DMA 更新，应使用业务模块适用的快照方式。
- 上述业务数据结构和字段名是接入示例，需要由用户实现；修改通道后，配套日志分析脚本的通道映射及周期判断也应同步调整。

## 保留内存和复位

- 两个普通 RAM 区分别限制到 `0x2001F000`、`0x2404F000`，顶端 4KB 中选一个作为 `.bss.noinit.fault_*` 的 UNINIT 区。记录和应急栈一起迁移。
- IRAM1：记录 `0x2001F000`，应急栈 `0x2001F0B8`。提交 checksum/magic 后，以 volatile 逐字读回全部 180 字节记录，再执行 DSB，满足 DTCM/ITCM 编程后读回要求。
- IRAM2：记录 `0x2404F000`，应急栈 `0x2404F0B8`。头部两个对齐 64 位单元完整读回并原值写回，再执行 DSB。
- 宏同时决定链接布局和读回策略，修改后 Rebuild，不能只在 C 编译选项中另行定义内存选择。检查 MAP 中的 UNINIT 区、记录和应急栈地址，以及普通数据/栈无重叠。
- 冷启动跳过未初始化 ECC RAM 的有效性读取，先初始化记录；暖复位校验旧记录。UNINIT 不表示掉电保存，程序下载、调试器 RAM 初始化或启动代码清 RAM 也可能清除数据。
- 必须启用 IWDG。捕获后只做整数/寄存器访问，最后停在异常中不喂狗，等待 IWDG；不依赖 USB、RTOS 或软件复位。必须确保其他高优先级异常也不会继续喂狗。
- 若启用 D-cache，应先为保留区设置适当内存属性或实现缓存维护；当前 AXI 读回写回不能代替 CPU cache clean。其他 MCU 的寄存器、复位标志、ECC 粒度及 `fault_frame_address()` 允许栈范围都须重新核对。
- 应急栈固定 1024 字节，异常入口汇编与 `FAULT_LOG_STACK_BYTES` 必须保持一致。异常调用链避免浮点；栈帧不可读时仍补报状态寄存器，`stack_valid=0` 时 PC/LR 不可信。

## fault 适配层与测试

本包的 fault_log_port.c 已连接本包 log/CDC/RTOS 接口，整包接入时不需要额外实现。更换传输方式时，只修改适配层：LinkReady 为真正可发送状态；Session 和 DisconnectCount 用于检测重连；TxIdle 必须同时表示队列为空、最后一个在途包完成；Tick 返回毫秒；Error 必须复制完整行并返回状态。

状态读取接口也在短关中断区调用，只允许读内存，不能延时、加锁或调用阻塞服务。Error 在普通任务上下文格式化；异常捕获不调用 USB、RTOS 或日志输出。

需要测试时将 fault_log_config.h 的 FAULT_LOG_ENABLE_TEST 设为 1：每次链路就绪且补报完成后 10 秒注入 UDF，等待 IWDG 复位再补报，循环重复。实际墙钟间隔还包含复位、枚举和补报时间。Watch 中 g_fault_test_request 写 1 可重新开始手动倒计时，写 2 取消手动倒计时；下一轮仍可恢复自动模式。发行必须改回 0，关闭测试不会关闭真实异常捕获。

## 日志格式、观察与定位

默认 LOG_CHANNEL_COUNT=10、LOG_PERIOD_MS=10，每 10 ms 一条 INFO，各通道约 100 ms 一条。每条有 tick、seq、ch 和 48 个十六进制字；浮点按 IEEE-754 原始位模式发送。USB 包边界不等于日志行边界。

```text
g_log_task_diag
g_log_tx_diag
g_cdc_tx_diag
g_log_event_diag
g_log_link_diag
g_log_link_ready
g_fault_record
g_fault_log_diag
cdc_tx_pending_count
cdc_tx_queue_overflow_count
LogTaskHandle
```

`g_log_tx_diag.accepted` 表示入队、`g_cdc_tx_diag.completed` 表示 USB 发送完成，都不代表 SD 已落盘。READY/ACK 不提供普通日志逐条可靠重传。fault 补报也没有 SD 落盘 ACK，不能将清 pending 当作持久存储证明。

故障输出为三段 `[hardfault] id=... part=1/3..3/3`，记录 PC/LR、R0–R12、MSP/PSP、EXC_RETURN、CFSR/HFSR 等。保留与实际烧录固件一致的 AXF/MAP，用 PC 定位指令、LR 辅助定位调用方，再结合状态寄存器分析；stack_valid=0 时不要相信栈帧 PC/LR。测试模式下 UDF 对应的 HardFault 为预期注入，地址会随编译变化。

## 版本迁移与验证范围

旧 log_task 已增加 fault 心跳、测试服务及三段补报优先级；log.c/h 的 sys_printf 返回值及事件队列同步到当前主板；CDC 保留发送队列/握手逻辑，并提供独立业务接收钩子。业务采样、任务及诊断命名继续采用旧包的 log_* 名称。

旧 readme1 的业务移植示例与 fault README 的链接/异常接入说明已合并到本文。此版本的构建和回归结果见随包 VALIDATION.md。目标固件仍需按实际外设、业务数据、内存布局和任务栈进行上板验证；本次未执行烧录。
