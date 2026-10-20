# Rb17 与 AXF 核对结果

分析对象：`E:/Rb17.txt`；用户指定的轮腿主板 `rm_main.axf`，文件时间
2026-10-18 22:37:50。未修改任何主板或库源文件，未编译、未烧录。

## 结论

日志运输与采样在 142.86 秒完整区间内稳定；业务层有三种持续离线。
已采样到的两个有效 DJI 电机 state 均无异常，但日志未覆盖全部电机状态，
不能下“所有电机 state 正常”的结论。源码与 AXF 还共同确认了 LK 命令
校验及 UART RX 缓存顺序问题。

## 日志完整性和运行状态

- 文件 7,094,624 字节，共 14,704 行，其中 14,287 条完整快照、408 条离线
  告警、文件头，以及最后一条不完整快照。
- 完整快照 seq=0..14286，tick=442..143302，无跳号、重复或复位回退；
  所有相邻快照间隔 10 ms，10 通道轮转正确，全部 48 个合法十六进制字。
- seq=14287 在文件末尾截断。不能由此证明 USB 中途丢包，也不能仅凭文件
  区分拔卡/断电/未刷新/关闭文件问题。
- CFSR/HFSR/SHCSR 均为 0，无 hardfault 回报。DFSR 全程为 0xB，是调试
  粘滞状态，不能单凭它判定采集中发生 HardFault 或本次暂停；本次无时间缺口。
- CCR 全程 0x70200，D-Cache 开启。AXF 包含 USB MPU/提交屏障路径，相关
  缓冲区位于 0x24048xxx 非缓存专用区。日志未直接采样 USB DMA 使能寄存器。
- CDC 队列溢出、启动失败、格式错误/格式化截断均为 0，队列峰值 4/32。
  最后 task 快照 accepted/started/completed 均为 14690，累计字节数一致。
- FreeRTOS 剩余堆及历史最小剩余堆均为 12992 字节，无累计诊断计数回退。
  status/shoot 栈历史最小余量分别 58/59 words（232/236 bytes）；仅说明本次
  观测下的余量，不是全业务工况的栈安全证明。
- 各 FDCAN IR 快照均为 0x800，映射为 TX FIFO Empty，不是电机错误码。
  未采样 PSR/ECR，不能据此完整评估所有总线错误历史。

## 电机 state：按当前驱动和实际编译结果解码

`drv_dji_motor.h:64` 的 dji_state_e：

| 值 | 定义 |
|---|---|
| 0 | DJI_MOTOR_OK，无异常 |
| 1 | 存储芯片访问失败 |
| 2 | 电调供电电压过高 |
| 3 | 三相线未接入 |
| 4 | 位置传感器数据丢失 |
| 5 | 电机温度异常或过高 |
| 7 | 校准失败 |
| 8 | 过热 |

驱动在 `drv_dji_motor.c:66` 将反馈 data[7] 写入 state；AXF 中
0x0802033C..0x08020340 的 LDRB/STRB 对应同一解析。不同电调/电机协议是否
实际实现该反馈字节，应按硬件协议确认，这里严格按用户现有驱动定义判断。

| 对象 | 日志覆盖 | 本次结果 |
|---|---|---|
| driver_motor[0]，左轮，CAN3 / 0x204 | dji w[0..7]，w[1] 为 state | 1428 次采样 state=0、online=1，receive_cnt 持续增长 |
| yaw_motor，CAN3 / 0x205 | dji w[24..31]，w[25] 为 state | 1428 次采样 state=0、online=1，receive_cnt 持续增长 |
| driver_motor[1]，右轮，CAN3 / 0x203 | 未采样 state | 未见该模块离线告警，不能据此推断错误码为 0 |
| joint_motor[0..3]，四个 DM 关节 | 未采样 state/err_state | 未见四关节离线告警，但无法判断内部故障状态 |
| trigger_motor，LK，CAN3 / 0x142 | err_code 未采样 | 持续离线，见下文；不是已经证实的硬件 state 故障码 |
| fric_motor[0..1]、pit_motor | 部分字段为全 0 | 当前 can_comm_init 未注册这些对象；零值不能作为在线正常证据 |

当前 dm 通道实际引用 pit_motor，并且若干字段已注释；当前 pit_motor 是
DJI 类型且未初始化，并非四个关节的集合。dm 全零不能证明四个 DM 关节
正常或离线。AXF 验证了 dji/dm 采样映射；AXF 是程序映像，不是运行时 RAM
快照，不能恢复未写入文件的 state 历史。

`drv_dm_motor.h` 只有 uint8_t state / err_state，没有枚举数值含义；其 .c
从反馈 data[0] 高四位取 state，state>1 时保存到 err_state，并未正常后清零。
这意味着 err_state 为历史粘滞值，不能当成当前状态。本次没有这些字段，
不能臆造具体过压/过温码解释。

有符号反馈已按源码还原，而不是把 FFFFFFFF 误判成巨大正数：左轮转速
-13..20 rpm，反馈电流原始值 -213..732；yaw 转速 -35..23 rpm，反馈电流
-579..844。两者命令电流快照均为 0。是否符合机械动作要求需结合测试工况，
这些数值本身没有越出 int16_t，也没有构成驱动定义的 state 故障。

## 持续业务离线

| 模块 | 次数 | 首次/末次 send_tick | 最后收包时间 | CAN |
|---|---:|---|---|---|
| remote | 136 | 5032 / 143132 | 始终 0 | 非 CAN，字段 0 为占位 |
| supercap | 136 | 5042 / 143142 | 始终 0 | can_index=1 即 CAN_CHANNEL_2，0x20 |
| trigger | 136 | 5052 / 143152 | 始终 0 | can_index=2 即 CAN_CHANNEL_3，0x142 |

无 recovered 记录。告警间隔为 1020/1030 ms，源任务 33 ms 轮询且由日志任务
排队输出，符合约 1 秒限频。模块若未连接，这些告警符合事实；若预期已连接，
说明主板没有更新该模块接收状态，但日志本身不能区分断电、接线、协议或接收链路。
底盘 IMU 始终 online=1，last_rx_tick 更新；浮点数据无 NaN/Inf。
云台 IMU 业务值、在线标志和 last_rx_tick 为 0；它没有纳入本主板当前 status
映射，须结合是否实际使用判断，不据此新增“必须在线”的假设。

## 已确认的源码问题

### LK 扳机命令过滤条件恒真

`User/Drivers/drv_lk_motor.c:36`：

```c
if(cmd != READ_ERR || cmd != CLEAR_ERR || cmd != CLOSE_MOTOR || cmd != START_MOTOR)
    return;
```

四个命令值不同，任意 cmd 都满足至少一个不等式，因此所有命令均被拒绝。
若意图拒绝不属于白名单的命令，这里应使用 &&。AXF 0x08024BEC 起的比较
分支与源代码一致，合法 START_MOTOR 也无法到达 can_std_transmit。
另 `User/Task/comm_task.c:36` 的 lk_set_current 调用被注释。
因此当前软件没有正常执行这两条扳机控制/请求路径，这与持续离线吻合；
不能进一步仅凭日志认定电机损坏。当前分析未修改此逻辑。

### UART DMA 缓存处理顺序错误

`User/Communication/usart_comm.c:61..76` 及 AXF 的 usart_user_handler 显示：
AbortReceive -> 解析缓冲区 -> Receive_DMA -> InvalidateDCacheForDmaRx。
可缓存接收区应在 DMA 停止后、CPU 解析前完成失效处理；重新启动 DMA 后
再失效还会与 DMA 写入并行。遥控、裁判和 TOF 都有此顺序。
本次不能证明 remote 离线就是这个原因，但源代码/二进制中的顺序问题明确存在。

### UART3/UART9 的 FE 标志

uart123 的 huart3 ISR、uart6910 的 huart9 ISR 在各 1429 次快照中 FE 均置位；
PE/NE/ORE 均未见置位。这是 Framing Error 状态，不等于 1429 次独立错误，
也可能是启动时置位后未清除。当前 usart_comm 未为两者启动接收，所以先核对
是否有实际用途、输入电平/串口参数，再决定配置与清标志策略。

## 后续补充采样建议

将 driver_motor[1].online/state/receive_cnt、joint_motor[0..3].online/state/
err_state/receive_cnt、trigger_motor.online/err_code/send_cnt/receive_cnt 加入
明确字段，才能核对全部电机内部状态。LK 修复和 UART 顺序修复可另行进行，
本次未对用户源码实施修改。
