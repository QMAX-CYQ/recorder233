# 10 通道链路与 SD 卡压力测试

主板每 10ms 生成一条日志，10 通道轮转，每通道周期为 100ms。每条由 32 个通道数据字和 16 个公共内核状态字组成；数据字以 8 位十六进制输出，不添加无意义填充。

格式：`INFO: [通道] tick=源tick seq=生成序号 ch=通道索引 n=48 w=XXXXXXXX/…\r\n`。seq 从 0 开始，每次生成日志递增，包括入队失败的记录。seq 缺口用于区分记录缺失，tick 用于检查实际任务周期。ch 应按 0～9 循环。签名与字段布局已变更，旧 8 通道的格式匹配、组轮转规则不能继续用于新日志；接收端按行解析 tick 的功能仍兼容。

| 通道索引 | 标签 | 内容 |
|---:|---|---|
| 0 | sys | RCC 时钟、复位原因、外设使能与复位配置 |
| 1 | task | 9 个任务栈水位、任务数、堆空间、USB 队列和日志统计 |
| 2 | fdcan1 | FDCAN1 的 32 个配置、FIFO、发送与中断寄存器 |
| 3 | fdcan23 | FDCAN2、FDCAN3 各 16 个寄存器 |
| 4 | uart123 | UART1/2/3 寄存器、HAL 收发状态、日志计数 |
| 5 | uart6910 | UART6/9/10 寄存器、HAL 收发状态、日志计数 |
| 6 | nvic | NVIC 使能/挂起/活动状态、外设与系统异常优先级 |
| 7 | dji | 驱动、摩擦轮、偏航电机状态、编码值、电流和计数 |
| 8 | dm | 达妙俯仰/拨盘电机控制反馈、云台参考反馈与输出 |
| 9 | imu | 两个 IMU 的姿态、角速度、加速度、在线状态和云台数据 |

公共字 32～47 是 SCB、CoreDebug、DWT 状态。DWT 仅观察现有配置，不启用计数器；未启用时部分计数可能为 0。浮点字段通过 memcpy 保留 IEEE-754 位模式，没有浮点 printf；负整数以 uint32 位模式发送。

寄存器读取避开 UART RDR、FDCAN PSR/ECR、SysTick CTRL 等会消费数据或清除状态的读操作。不写寄存器，不改变电机或传感器控制。快照是观测值，不保证多字段来自严格同一时刻。

## 数据量与缓冲

每条通常约 470～493 字节，100 条/秒，原始日志约 47～49.3KB/s。F405 每行增加约 14 字节时间戳，落盘约 48.4～50.7KB/s；实际长度随 tick、seq 位数变化，视觉等其他数据另计。按当前负载，16KB 块约每 0.32～0.34 秒写一次，8MiB 文件约 2.8～2.9 分钟轮转一次。

log 格式化缓冲扩为 768 字节，最坏长度 493 字节（不含结尾 NUL），USB 单槽上限仍为 2048 字节。队列仍为用户设置的 32 槽，发送功能与接收功能未改。DebugTask 栈从 512 扩为 1024 个字（约 4KB），优先级仍为 Low；修改位于 freertos.c 的 USER CODE 区。

## Watch

- g_debug_task_diag.group_count[10]：各通道生成次数。
- last_run_ms、max_run_ms、overrun_count：任务执行耗时；耗时含抢占，超过或等于 10ms 计一次超期。
- max_gap_ms、late_count、catchup_count：生成周期与调度情况。
- g_log_tx_diag.last_line_bytes、max_line_bytes：发送前原始行长度。
- format_error、format_truncated：应保持为 0。
- cdc_tx_pending_count、queue_high_water、queue_overflow_count：队列占用和拒收。
- task 通道 words[7]：DebugTask 栈水位，以字为单位。
- F405 的 ring_rejected、write_error、write_short、sync_error、max_write_ms、max_sync_ms、min_ring_free：写卡和缓冲压力。

任务耗时仅以毫秒统计，更细粒度测量需另行设计，不自动写 DWT 配置。大负载测试可能使队列满，是否可持续需结合实机统计与日志连续性验证。

## 验证

独立 V6.22 工程副本编译 0 错误、0 警告；原工程 V6.24 配置保留。原生 C 测试提取实际 sys_printf 和新发送格式，用 48 个 FFFFFFFF、最大 tick/seq 验证全部 10 通道均保留 48 个数据字与 CRLF，没有格式错误或截断。尚未烧录或完成实机压力测试。

originals 保存修改前的五个文件，schema.json 保存完整字段索引，line_lengths.json 保存各通道的最坏长度。

## 字段索引

### sys：通道字 0～31

| 字索引 | 数据来源 |
|---:|---|
| 0 | `RCC->CR` |
| 1 | `RCC->CFGR` |
| 2 | `RCC->D1CFGR` |
| 3 | `RCC->D2CFGR` |
| 4 | `RCC->D3CFGR` |
| 5 | `RCC->PLLCKSELR` |
| 6 | `RCC->PLLCFGR` |
| 7 | `RCC->PLL1DIVR` |
| 8 | `RCC->PLL1FRACR` |
| 9 | `RCC->PLL2DIVR` |
| 10 | `RCC->PLL2FRACR` |
| 11 | `RCC->PLL3DIVR` |
| 12 | `RCC->PLL3FRACR` |
| 13 | `RCC->D1CCIPR` |
| 14 | `RCC->D2CCIP1R` |
| 15 | `RCC->D2CCIP2R` |
| 16 | `RCC->D3CCIPR` |
| 17 | `RCC->AHB3ENR` |
| 18 | `RCC->AHB1ENR` |
| 19 | `RCC->AHB2ENR` |
| 20 | `RCC->AHB4ENR` |
| 21 | `RCC->APB3ENR` |
| 22 | `RCC->APB1LENR` |
| 23 | `RCC->APB1HENR` |
| 24 | `RCC->APB2ENR` |
| 25 | `RCC->APB4ENR` |
| 26 | `RCC->RSR` |
| 27 | `RCC->AHB3RSTR` |
| 28 | `RCC->AHB1RSTR` |
| 29 | `RCC->AHB2RSTR` |
| 30 | `RCC->AHB4RSTR` |
| 31 | `RCC->APB3RSTR` |

### task：通道字 0～31

| 字索引 | 数据来源 |
|---:|---|
| 0 | `debug_stack_words(CommTaskHandle)` |
| 1 | `debug_stack_words(ModeSwitchTaskHandle)` |
| 2 | `debug_stack_words(StatusTaskHandle)` |
| 3 | `debug_stack_words(ChassisTaskHandle)` |
| 4 | `debug_stack_words(GimbalTaskHandle)` |
| 5 | `debug_stack_words(ShootTaskHandle)` |
| 6 | `debug_stack_words(UITaskHandle)` |
| 7 | `debug_stack_words(DebugTaskHandle)` |
| 8 | `debug_stack_words(BoardCommTaskHandle)` |
| 9 | `uxTaskGetNumberOfTasks()` |
| 10 | `xPortGetFreeHeapSize()` |
| 11 | `xPortGetMinimumEverFreeHeapSize()` |
| 12 | `g_debug_task_diag.loops` |
| 13 | `g_debug_task_diag.max_gap_ms` |
| 14 | `g_debug_task_diag.late_count` |
| 15 | `g_debug_task_diag.catchup_count` |
| 16 | `g_debug_task_diag.max_run_ms` |
| 17 | `cdc_tx_pending_count` |
| 18 | `cdc_tx_inflight` |
| 19 | `cdc_tx_queue_overflow_count` |
| 20 | `g_cdc_tx_diag.accepted` |
| 21 | `g_cdc_tx_diag.completed` |
| 22 | `g_cdc_tx_diag.started` |
| 23 | `g_cdc_tx_diag.accepted_bytes` |
| 24 | `g_cdc_tx_diag.completed_bytes` |
| 25 | `g_cdc_tx_diag.buffered` |
| 26 | `g_cdc_tx_diag.queue_high_water` |
| 27 | `g_cdc_tx_diag.start_failed` |
| 28 | `g_log_tx_diag.format_truncated` |
| 29 | `g_log_tx_diag.format_error` |
| 30 | `g_log_tx_diag.last_line_bytes` |
| 31 | `g_log_tx_diag.max_line_bytes` |

### fdcan1：通道字 0～31

| 字索引 | 数据来源 |
|---:|---|
| 0 | `hfdcan1.Instance->CCCR` |
| 1 | `hfdcan1.Instance->NBTP` |
| 2 | `hfdcan1.Instance->DBTP` |
| 3 | `hfdcan1.Instance->IR` |
| 4 | `hfdcan1.Instance->IE` |
| 5 | `hfdcan1.Instance->ILS` |
| 6 | `hfdcan1.Instance->ILE` |
| 7 | `hfdcan1.Instance->GFC` |
| 8 | `hfdcan1.Instance->RXF0S` |
| 9 | `hfdcan1.Instance->RXF1S` |
| 10 | `hfdcan1.Instance->TXFQS` |
| 11 | `hfdcan1.Instance->TXBRP` |
| 12 | `hfdcan1.Instance->TXBTO` |
| 13 | `hfdcan1.Instance->TXBCF` |
| 14 | `hfdcan1.Instance->TXBTIE` |
| 15 | `hfdcan1.Instance->TXBCIE` |
| 16 | `hfdcan1.Instance->CREL` |
| 17 | `hfdcan1.Instance->ENDN` |
| 18 | `hfdcan1.Instance->TEST` |
| 19 | `hfdcan1.Instance->RWD` |
| 20 | `hfdcan1.Instance->TSCC` |
| 21 | `hfdcan1.Instance->TSCV` |
| 22 | `hfdcan1.Instance->TOCC` |
| 23 | `hfdcan1.Instance->TOCV` |
| 24 | `hfdcan1.Instance->TDCR` |
| 25 | `hfdcan1.Instance->SIDFC` |
| 26 | `hfdcan1.Instance->XIDFC` |
| 27 | `hfdcan1.Instance->XIDAM` |
| 28 | `hfdcan1.Instance->HPMS` |
| 29 | `hfdcan1.Instance->NDAT1` |
| 30 | `hfdcan1.Instance->NDAT2` |
| 31 | `hfdcan1.Instance->TXEFS` |

### fdcan23：通道字 0～31

| 字索引 | 数据来源 |
|---:|---|
| 0 | `hfdcan2.Instance->CCCR` |
| 1 | `hfdcan2.Instance->NBTP` |
| 2 | `hfdcan2.Instance->DBTP` |
| 3 | `hfdcan2.Instance->IR` |
| 4 | `hfdcan2.Instance->IE` |
| 5 | `hfdcan2.Instance->ILS` |
| 6 | `hfdcan2.Instance->ILE` |
| 7 | `hfdcan2.Instance->GFC` |
| 8 | `hfdcan2.Instance->RXF0S` |
| 9 | `hfdcan2.Instance->RXF1S` |
| 10 | `hfdcan2.Instance->TXFQS` |
| 11 | `hfdcan2.Instance->TXBRP` |
| 12 | `hfdcan2.Instance->TXBTO` |
| 13 | `hfdcan2.Instance->TXBCF` |
| 14 | `hfdcan2.Instance->TXBTIE` |
| 15 | `hfdcan2.Instance->TXBCIE` |
| 16 | `hfdcan3.Instance->CCCR` |
| 17 | `hfdcan3.Instance->NBTP` |
| 18 | `hfdcan3.Instance->DBTP` |
| 19 | `hfdcan3.Instance->IR` |
| 20 | `hfdcan3.Instance->IE` |
| 21 | `hfdcan3.Instance->ILS` |
| 22 | `hfdcan3.Instance->ILE` |
| 23 | `hfdcan3.Instance->GFC` |
| 24 | `hfdcan3.Instance->RXF0S` |
| 25 | `hfdcan3.Instance->RXF1S` |
| 26 | `hfdcan3.Instance->TXFQS` |
| 27 | `hfdcan3.Instance->TXBRP` |
| 28 | `hfdcan3.Instance->TXBTO` |
| 29 | `hfdcan3.Instance->TXBCF` |
| 30 | `hfdcan3.Instance->TXBTIE` |
| 31 | `hfdcan3.Instance->TXBCIE` |

### uart123：通道字 0～31

| 字索引 | 数据来源 |
|---:|---|
| 0 | `huart1.Instance->ISR` |
| 1 | `huart1.Instance->CR1` |
| 2 | `huart1.Instance->CR2` |
| 3 | `huart1.Instance->CR3` |
| 4 | `huart1.Instance->BRR` |
| 5 | `huart1.Instance->PRESC` |
| 6 | `huart1.Instance->RTOR` |
| 7 | `huart1.Instance->GTPR` |
| 8 | `huart2.Instance->ISR` |
| 9 | `huart2.Instance->CR1` |
| 10 | `huart2.Instance->CR2` |
| 11 | `huart2.Instance->CR3` |
| 12 | `huart2.Instance->BRR` |
| 13 | `huart2.Instance->PRESC` |
| 14 | `huart2.Instance->RTOR` |
| 15 | `huart2.Instance->GTPR` |
| 16 | `huart3.Instance->ISR` |
| 17 | `huart3.Instance->CR1` |
| 18 | `huart3.Instance->CR2` |
| 19 | `huart3.Instance->CR3` |
| 20 | `huart3.Instance->BRR` |
| 21 | `huart3.Instance->PRESC` |
| 22 | `huart3.Instance->RTOR` |
| 23 | `huart3.Instance->GTPR` |
| 24 | `huart1.gState` |
| 25 | `huart1.RxState` |
| 26 | `huart2.gState` |
| 27 | `huart2.RxState` |
| 28 | `huart3.gState` |
| 29 | `huart3.RxState` |
| 30 | `g_log_tx_diag.accepted` |
| 31 | `g_log_tx_diag.busy` |

### uart6910：通道字 0～31

| 字索引 | 数据来源 |
|---:|---|
| 0 | `huart6.Instance->ISR` |
| 1 | `huart6.Instance->CR1` |
| 2 | `huart6.Instance->CR2` |
| 3 | `huart6.Instance->CR3` |
| 4 | `huart6.Instance->BRR` |
| 5 | `huart6.Instance->PRESC` |
| 6 | `huart6.Instance->RTOR` |
| 7 | `huart6.Instance->GTPR` |
| 8 | `huart9.Instance->ISR` |
| 9 | `huart9.Instance->CR1` |
| 10 | `huart9.Instance->CR2` |
| 11 | `huart9.Instance->CR3` |
| 12 | `huart9.Instance->BRR` |
| 13 | `huart9.Instance->PRESC` |
| 14 | `huart9.Instance->RTOR` |
| 15 | `huart9.Instance->GTPR` |
| 16 | `huart10.Instance->ISR` |
| 17 | `huart10.Instance->CR1` |
| 18 | `huart10.Instance->CR2` |
| 19 | `huart10.Instance->CR3` |
| 20 | `huart10.Instance->BRR` |
| 21 | `huart10.Instance->PRESC` |
| 22 | `huart10.Instance->RTOR` |
| 23 | `huart10.Instance->GTPR` |
| 24 | `huart6.gState` |
| 25 | `huart6.RxState` |
| 26 | `huart9.gState` |
| 27 | `huart9.RxState` |
| 28 | `huart10.gState` |
| 29 | `huart10.RxState` |
| 30 | `g_log_tx_diag.accepted` |
| 31 | `g_log_tx_diag.busy` |

### nvic：通道字 0～31

| 字索引 | 数据来源 |
|---:|---|
| 0 | `NVIC->ISER[0]` |
| 1 | `NVIC->ISPR[0]` |
| 2 | `NVIC->IABR[0]` |
| 3 | `NVIC->ISER[1]` |
| 4 | `NVIC->ISPR[1]` |
| 5 | `NVIC->IABR[1]` |
| 6 | `NVIC->ISER[2]` |
| 7 | `NVIC->ISPR[2]` |
| 8 | `NVIC->IABR[2]` |
| 9 | `NVIC->ISER[3]` |
| 10 | `NVIC->ISPR[3]` |
| 11 | `NVIC->IABR[3]` |
| 12 | `NVIC->ISER[4]` |
| 13 | `NVIC->ISPR[4]` |
| 14 | `NVIC->IABR[4]` |
| 15 | `NVIC_GetPriority(FDCAN1_IT0_IRQn)` |
| 16 | `NVIC_GetPriority(FDCAN1_IT1_IRQn)` |
| 17 | `NVIC_GetPriority(FDCAN2_IT0_IRQn)` |
| 18 | `NVIC_GetPriority(FDCAN3_IT0_IRQn)` |
| 19 | `NVIC_GetPriority(USART1_IRQn)` |
| 20 | `NVIC_GetPriority(USART2_IRQn)` |
| 21 | `NVIC_GetPriority(USART6_IRQn)` |
| 22 | `NVIC_GetPriority(OTG_HS_IRQn)` |
| 23 | `SCB->SHPR[0]` |
| 24 | `SCB->SHPR[1]` |
| 25 | `SCB->SHPR[2]` |
| 26 | `SCB->SHPR[3]` |
| 27 | `SCB->SHPR[4]` |
| 28 | `SCB->SHPR[5]` |
| 29 | `SCB->SHPR[6]` |
| 30 | `SCB->SHPR[7]` |
| 31 | `SCB->SHPR[8]` |

### dji：通道字 0～31

| 字索引 | 数据来源 |
|---:|---|
| 0 | `driver_motor[0].online` |
| 1 | `driver_motor[0].state` |
| 2 | `driver_motor[0].ecd` |
| 3 | `driver_motor[0].speed_rpm` |
| 4 | `driver_motor[0].rx_current` |
| 5 | `driver_motor[0].tx_current` |
| 6 | `driver_motor[0].send_cnt` |
| 7 | `driver_motor[0].receive_cnt` |
| 8 | `fric_motor[0].online` |
| 9 | `fric_motor[0].state` |
| 10 | `fric_motor[0].ecd` |
| 11 | `fric_motor[0].speed_rpm` |
| 12 | `fric_motor[0].rx_current` |
| 13 | `fric_motor[0].tx_current` |
| 14 | `fric_motor[0].send_cnt` |
| 15 | `fric_motor[0].receive_cnt` |
| 16 | `fric_motor[1].online` |
| 17 | `fric_motor[1].state` |
| 18 | `fric_motor[1].ecd` |
| 19 | `fric_motor[1].speed_rpm` |
| 20 | `fric_motor[1].rx_current` |
| 21 | `fric_motor[1].tx_current` |
| 22 | `fric_motor[1].send_cnt` |
| 23 | `fric_motor[1].receive_cnt` |
| 24 | `yaw_motor.online` |
| 25 | `yaw_motor.state` |
| 26 | `yaw_motor.ecd` |
| 27 | `yaw_motor.speed_rpm` |
| 28 | `yaw_motor.rx_current` |
| 29 | `yaw_motor.tx_current` |
| 30 | `yaw_motor.send_cnt` |
| 31 | `yaw_motor.receive_cnt` |

### dm：通道字 0～31

| 字索引 | 数据来源 |
|---:|---|
| 0 | `pit_motor.online` |
| 1 | `pit_motor.err_state` |
| 2 | `pit_motor.send_cnt` |
| 3 | `pit_motor.receive_cnt` |
| 4 | `pit_motor.last_rx_tick` |
| 5 | `debug_float_bits(pit_motor.p)` |
| 6 | `debug_float_bits(pit_motor.v)` |
| 7 | `debug_float_bits(pit_motor.kp)` |
| 8 | `debug_float_bits(pit_motor.kd)` |
| 9 | `debug_float_bits(pit_motor.t)` |
| 10 | `debug_float_bits(pit_motor.position)` |
| 11 | `debug_float_bits(pit_motor.velocity)` |
| 12 | `debug_float_bits(pit_motor.torque)` |
| 13 | `trigger_motor_2325.online` |
| 14 | `trigger_motor_2325.err_state` |
| 15 | `trigger_motor_2325.send_cnt` |
| 16 | `trigger_motor_2325.receive_cnt` |
| 17 | `trigger_motor_2325.last_rx_tick` |
| 18 | `debug_float_bits(trigger_motor_2325.p)` |
| 19 | `debug_float_bits(trigger_motor_2325.v)` |
| 20 | `debug_float_bits(trigger_motor_2325.kp)` |
| 21 | `debug_float_bits(trigger_motor_2325.kd)` |
| 22 | `debug_float_bits(trigger_motor_2325.t)` |
| 23 | `debug_float_bits(trigger_motor_2325.position)` |
| 24 | `debug_float_bits(trigger_motor_2325.velocity)` |
| 25 | `debug_float_bits(trigger_motor_2325.torque)` |
| 26 | `debug_float_bits(gimbal.yaw_angle.ref)` |
| 27 | `debug_float_bits(gimbal.yaw_angle.fdb)` |
| 28 | `debug_float_bits(gimbal.pit_angle.ref)` |
| 29 | `debug_float_bits(gimbal.pit_angle.fdb)` |
| 30 | `debug_float_bits(gimbal.yaw_output)` |
| 31 | `debug_float_bits(gimbal.pit_output)` |

### imu：通道字 0～31

| 字索引 | 数据来源 |
|---:|---|
| 0 | `debug_float_bits(gimbal_imu.pit)` |
| 1 | `debug_float_bits(gimbal_imu.yaw)` |
| 2 | `debug_float_bits(gimbal_imu.rol)` |
| 3 | `debug_float_bits(gimbal_imu.wx)` |
| 4 | `debug_float_bits(gimbal_imu.wy)` |
| 5 | `debug_float_bits(gimbal_imu.wz)` |
| 6 | `debug_float_bits(gimbal_imu.ax)` |
| 7 | `debug_float_bits(gimbal_imu.ay)` |
| 8 | `debug_float_bits(gimbal_imu.az)` |
| 9 | `debug_float_bits(chassis_imu.pit)` |
| 10 | `debug_float_bits(chassis_imu.yaw)` |
| 11 | `debug_float_bits(chassis_imu.rol)` |
| 12 | `debug_float_bits(chassis_imu.wx)` |
| 13 | `debug_float_bits(chassis_imu.wy)` |
| 14 | `debug_float_bits(chassis_imu.wz)` |
| 15 | `debug_float_bits(chassis_imu.ax)` |
| 16 | `debug_float_bits(chassis_imu.ay)` |
| 17 | `debug_float_bits(chassis_imu.az)` |
| 18 | `gimbal_imu.online` |
| 19 | `gimbal_imu.last_rx_tick` |
| 20 | `chassis_imu.online` |
| 21 | `chassis_imu.last_rx_tick` |
| 22 | `debug_float_bits(gimbal.yaw_angle.ref)` |
| 23 | `debug_float_bits(gimbal.yaw_angle.fdb)` |
| 24 | `debug_float_bits(gimbal.yaw_spd.ref)` |
| 25 | `debug_float_bits(gimbal.yaw_spd.fdb)` |
| 26 | `debug_float_bits(gimbal.pit_angle.ref)` |
| 27 | `debug_float_bits(gimbal.pit_angle.fdb)` |
| 28 | `debug_float_bits(gimbal.pit_spd.ref)` |
| 29 | `debug_float_bits(gimbal.pit_spd.fdb)` |
| 30 | `debug_float_bits(gimbal.yaw_output)` |
| 31 | `debug_float_bits(gimbal.pit_output)` |

### 每通道公共字 32～47

| 字索引 | 数据来源 |
|---:|---|
| 32 | `SCB->ICSR` |
| 33 | `SCB->SHCSR` |
| 34 | `SCB->CFSR` |
| 35 | `SCB->HFSR` |
| 36 | `SCB->DFSR` |
| 37 | `SCB->VTOR` |
| 38 | `SCB->AIRCR` |
| 39 | `SCB->CCR` |
| 40 | `SCB->CPACR` |
| 41 | `CoreDebug->DEMCR` |
| 42 | `DWT->CTRL` |
| 43 | `DWT->CYCCNT` |
| 44 | `DWT->CPICNT` |
| 45 | `DWT->EXCCNT` |
| 46 | `DWT->LSUCNT` |
| 47 | `DWT->FOLDCNT` |
