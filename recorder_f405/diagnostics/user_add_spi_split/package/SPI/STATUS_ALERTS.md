> SPI 版本说明：本文保留原有故障或告警接入细节；涉及 USB 握手、CDC 队列及返回值的部分，统一以本目录 README.md 中的 SPI DMA 接口和显式接收就绪控制为准。

# status 离线 / 恢复告警移植

提取来源：`D:/RM/rm_main_gimbal/User/Task/status_task.c`。本包已有 log.c/h 的
事件队列和日志格式，本次补上独立的触发状态机，不复制整套 status 任务、
看门狗、控制逻辑或离线判定驱动。

## 行为

- 状态任务启动后默认 5000 ms 宽限期，期间不产生事件。结束后，从未上线
  但 active=1 的模块也会首次告警。
- 每个模块独立记录状态，持续离线默认每 1000 ms 投递一次。实际误差取决于
  状态任务调用周期，源工程每 33 ms 检查一次。首次告警 offline_ms 为 0。
- offline_ms 从宽限期后的首次离线观察开始，不是从最后收包时刻开始。
- 离线恢复后投递一次 RECOVERED，包含离线时长和最后收包时间快照；
  不对始终在线的模块重复发送恢复记录。
- 队列满时，离线告警下轮重试，不推进已成功告警的计时；不补发所有历史
  周期告警。恢复事件每模块保留一份，下轮优先重试，再投递新的离线告警。
- 长期拥塞期间同一模块多次恢复，仅保留最近待提交恢复快照；旧快照被替换
  时递增 g_status_log_recovery_coalesced，不保证保留全部抖动历史。
- active=0 用于未注册/未使用的模块，清除当前离线状态，不产生新事件；
  之前已经暂存的恢复事件仍优先重试。invalid module 会被拒绝并计数。

## 接入

1. 编译 `status_log.c`，Include Path 包含本包目录。已有原版 status_log 状态机
   时，用本模块替换原 typedef/state/observe/offline 函数和同名诊断变量；
   不要同时运行两套告警生产者，否则会重复告警或出现重复符号。
2. 调度器启动前调用已有 `LogEvent_Init()`，保留单个 log_task 消费队列。
3. 在唯一的 status 任务启动处调用 `StatusLog_Init(osKernelSysTick())`，只调用
   一次，不要在每轮循环初始化。该 API 不清空已入队事件，不用于在线重置历史。
4. 每轮先执行原有各模块 check_offline()，再采样两个时间轴并观察状态：

```c
#include "status_log.h"

/* 状态任务进入时 */
StatusLog_Init(osKernelSysTick());

/* 每轮：先保持原来的离线判定和其他业务逻辑 */
status.vision = vision_check_offline();
uint32_t now_rtos = osKernelSysTick();
uint32_t now_hal = HAL_GetTick();
StatusLog_Observe(LOG_MODULE_VISION, 1U, status.vision,
                  vision.last_rx_tick, 0U, 0U, now_rtos, now_hal);
```

online 参数非零表示在线。不要把名称叫 check_offline 的函数都当成 1=离线；
源工程 vision/vtm/imu/board 函数返回的是 online。对含位掩码/离线数量的
聚合状态，必须先解码，不能直接传给 online。

本模块不依赖 HAL/RTOS 头文件。时间参数均为毫秒：last_rx_hal 和 now_hal
来自 HAL，now_rtos 来自 RTOS；保留 last_rx_hal=0 为“从未收包”。按无符号
差值换算为 RTOS 时间轴，支持正常的 32 位 tick 回绕。非 1kHz RTOS 应先
换算为毫秒；若协议时间本来就是 RTOS 毫秒，传相同时间基准，不要再次混算。

模块仅供同一个任务串行调用，不从 ISR 调用；LogEvent_Post 自身的临界区
不能替代状态机的单写者约束。它只提交值快照，不在 status 任务中格式化或
等待 USB，因此不会增加一套发送任务。

## 云台工程的完整模块映射

可选示例 `examples/status_log_gimbal.inc` 复现来源工程的 16 个映射：
vision、vtm、gimbal_imu、board、fric_right/left、driver_0/1、yaw、dji_trigger、
joint_0..3、dm_trigger、pit。源工程没有在这份告警映射里覆盖其他外设。

在该工程 status_task.c 中，删除旧告警状态机后，包含示例：

```c
#include "examples/status_log_gimbal.inc"
/* 任务启动时 */
StatusLog_Init(osKernelSysTick());
/* 每轮完成状态判定后 */
StatusLog_ObserveGimbal(osKernelSysTick(), HAL_GetTick());
```

该 .inc 不是单独编译的 C 文件。其他工程按实际结构改写映射，尤其 status.board
与 status.board_comm、IMU 对象、扳机电机名称，以及电机是否注册的判断。
示例用 list.next/prev 非空判断电机已注册；CAN 编号保留工程枚举原值，不自动
转换成物理 CAN1/2/3。不用的模块传 active=0 或不接入。

参数 defaults 可在工程全局编译宏中覆盖；仅在 status_task.c 包含头文件前
#define 不会改变独立编译的 status_log.c。也可直接调整 status_log.h。

## 输出与观察

离线由已有 log.c 输出 WARN `[offline]`，恢复输出 INFO `[recovered]`：
module、send_tick、last_rx_tick、offline_ms、can_index、can_id。
send_tick 是消费队列时刻，offline_ms 是观察事件时刻的快照，拥塞时二者
不会同步增长。无自动 SD 落盘确认，不能把队列投递成功当作文件已保存。

观察 g_log_event_diag、g_status_log_recovery_coalesced 和
 g_status_log_invalid_module。队列满与 USB BUSY 已由 log.c/h 统计。
本包不改变 status.all、IWDG 喂狗条件和协议驱动的超时判定；使用者仍应保证
看门狗先初始化后喂狗，并保持现有业务安全逻辑。
