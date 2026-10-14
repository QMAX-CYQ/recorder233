# 主板日志发送使用例子

本目录提供日志任务示例及发送支持代码，来源为 `D:/RM/rm_main_gimbal`，整理日期 2026-10-15。原例子面向 STM32H723、CMSIS-RTOS v1 和 FreeRTOS；F405 是接收与写卡端。

## 保留文件

| 文件 | 用途 |
|---|---|
| `log_task.c/.h` | 日志任务、10 通道轮转采样与任务诊断 |
| `log.c/.h` | `DEBUG_INFO`、`sys_printf`、格式化与发送诊断 |
| `usbd_cdc_if.c/.h` | CDC 发送队列、发送完成回调、READY/ACK 握手；作为合入用户 CDC 接口的参考 |
| `log_link_protocol.h` | 16 字节控制帧的构建、校验和跨包解析 |
| `README.md` | 使用与移植说明 |

HAL、CMSIS、FreeRTOS、USB Device 中间件、外设初始化和机器人业务代码由用户工程提供，本目录不重复附带。编译器提供 C 标准库。本目录不是完整可链接的固件工程。

## 接入步骤

1. 在目标主板工程中配置 USB Device CDC 和 FreeRTOS，初始化实际使用的外设。将本目录加入头文件搜索路径，将 `log_task.c`、`log.c` 加入工程。
2. 把 `usbd_cdc_if.c/.h` 中的发送队列、完成回调、控制帧解析和握手服务合入用户现有 CDC 接口，加入 `log_link_protocol.h`。避免同时编译两份 CDC 接口或重复定义诊断变量。
3. 当前 `CDC_Receive_HS` 只调用 `LogLinkFilter` 解析控制帧，再设置接收缓冲并提交下一次接收。有效 READY 仍由 `LogLinkOnFrame` 处理，非控制数据当前没有业务处理入口。USB 初始化、描述符及底层配置仍使用目标工程的实现。`log.c` 所需的 `usb_device.h`、`cmsis_os.h`、`FreeRTOS.h` 同样由目标工程提供。
4. 按实际业务修改 `log_snapshot()`。当前例子读取 H723 寄存器、电机、IMU、云台数据，并引用 `fdcan.h`、`usart.h`、`drv_dji_motor.h`、`drv_dm_motor.h`、`gimbal_task.h`、`prot_imu.h`。用户工程没有这些对象时，应替换或删除相应采样、头文件和 extern，不必移植整套机器人业务库。UART/FDCAN 的 Instance 必须先初始化。
5. 在用户 RTOS 初始化文件中包含 `log_task.h`，定义一次任务句柄，并创建日志任务：

```c
#include "log_task.h"

/* RTOS 全局变量区；log_task.c 中只有 extern 声明。 */
osThreadId LogTaskHandle;

/* MX_FREERTOS_Init() 的任务创建区。 */
osThreadDef(LogTask, log_task, osPriorityLow, 0, 1024);
LogTaskHandle = osThreadCreate(osThread(LogTask), NULL);
```

源工程 CMSIS-RTOS v1 将 1024 作为 FreeRTOS 栈字数，约 4096 字节；其他接口的栈单位需单独确认。任务创建返回 NULL 时按用户工程策略处理。示例 tick 为 1000 Hz，需要开启 `INCLUDE_uxTaskGetStackHighWaterMark`。

6. task 通道还引用其他业务任务句柄，不存在的任务可删除对应采样，或提供值为 NULL 的句柄；`log_stack_words()` 对 NULL 返回 0。已有 `DebugTaskHandle` 时应同步调整注册和采样，避免同时创建原 `debug_task` 和新 `log_task` 重复发送。
7. F405 烧录配套 READY/ACK 固件。主板收到 READY 并成功排队 ACK 后，`g_log_link_ready=1` 才开始生成日志；没有记录板回应时保持等待。

## CDC 接收与启动握手

`CDC_Receive_HS` 通过 `LogLinkFilter` 解析控制帧，并重新提交接收请求。有效 READY 由 `LogLinkOnFrame` 记录；`LogLinkService_HS` 将同会话 ACK 加入发送队列后，`g_log_link_ready` 置为 1，日志任务开始生成记录。

非控制数据目前没有业务消费者。如果需要处理其他 USB 接收数据，可使用 `LogLinkFilter` 返回的有效长度，处理 `log_link_filtered` 中的业务数据。

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

## 任务名称与数据格式

- 入口：`log_task`；诊断类型：`log_task_diag_t`；诊断变量：`g_log_task_diag`。
- 任务句柄：`LogTaskHandle`；宏：`LOG_CHANNEL_COUNT=10`、`LOG_PERIOD_MS=10`。
- 每 10 ms 一条，10 通道轮转，单通道周期约 100 ms；每条携带 tick、seq、channel 和 48 个十六进制数据字。
- 浮点数据发送 IEEE-754 原始位模式；USB 包边界不等于日志行边界。
- `DEBUG_INFO` 是公共日志输出宏，采样数据经格式化后进入 CDC 发送队列。

## Keil Watch

```text
g_log_task_diag
g_log_tx_diag
g_cdc_tx_diag
g_log_link_diag
g_log_link_ready
cdc_tx_pending_count
cdc_tx_queue_overflow_count
LogTaskHandle
```

`g_log_tx_diag.accepted` 仅证明日志入队，`g_cdc_tx_diag.completed` 表示 USB 发送完成，均不能代替 SD 落盘确认。队列满返回 BUSY，当前 `sys_printf` 不重试该记录，seq 可能产生缺口。启动握手不提供逐条可靠重传。

## 验证范围

当前示例曾使用 ARM Compiler 6.22、STM32H723 配置完成日志任务、格式化发送、CDC 接口及任务创建参考的语法检查。本文中的 motor/power 业务代码为移植示例，需要接入用户的数据模块后在目标工程验证；完整链接、烧录和实机验证由目标工程完成。
