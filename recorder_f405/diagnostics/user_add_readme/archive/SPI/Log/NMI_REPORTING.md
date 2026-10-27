> SPI 版本说明：本文保留原有故障或告警接入细节；涉及 USB 握手、CDC 队列及返回值的部分，统一以本目录 README.md 中的 SPI DMA 接口和显式接收就绪控制为准。

# NMI 时钟现场与复位后回报

适用 STM32H723 / ArmClang 6。本增量沿用已有 FaultLog_BootInit、FaultLog_Service 和 IWDG 复位回报流程，不增加任务。

## 接入

将 nmi_log.c 与 fault_log.c、fault_log_port.c 一起编译，同步更新两个模块的 c/h。在 stm32h7xx_it.c 的 USER CODE BEGIN Includes 中，在现有四个 fault 宏旁加入：

```c
#define NMI_Handler FaultLog_GeneratedNMI_Handler
```

真实 NMI_Handler 在自定义 nmi_log.c 中，以 naked 汇编保存原 MSP/PSP、EXC_RETURN、R4–R11 和屏蔽寄存器后切换专用 1024 字节栈。不要再从普通 C 函数调用该入口，不需要修改启动文件。CubeMX 勾选 Keep User Code；生成后核对中断重命名与 Keil 源文件列表。

默认 DTCM 保留区仍为 0x2001F000..0x2001FFFF，沿用 fault_noinit.sct 的 `.bss.noinit.fault_*` 通配选择。HardFault 记录保持 180 字节/version 1；NMI 记录为 272 字节，独立 magic 0x4E4D494C/version 1，校验覆盖自身整个记录。两份记录、两份 1024 字节栈及测试标记均须落入这 4KB。具体地址以新 MAP 为准，旧版应急栈固定偏移不再适用。本增量不跨旧固件版本恢复 RAM 记录。

默认 DTCM 无缓存；可选 AXI 保留区仍须单独配置缓存策略，USB 的 MPU 区不覆盖它。CIFR/CSS 寄存器名称和地址仅适用此 H723 基线。

## 捕获内容与顺序

进入 NMI 后先置记录无效，再读取 RCC 原始时钟寄存器，随后保存 CPU 现场，最后提交校验与 magic。捕获期间不调用 HAL_RCC_NMI_IRQHandler，不清 CSS 标志，不调用 USB、RTOS、printf 或喂狗函数，停留等待 IWDG 复位。没有运行中的 IWDG 就只保留 RAM 现场，不会自动重启补报。

复位并握手后先发送四段 `[nmi] id=... part=1/4..4/4`，再发送仍有效的三段 `[hardfault]`，最后恢复业务日志：

| 段 | 内容 |
|---|---|
| 1/4 | 心跳 tick/tick_valid、复位前后 RSR、HSE/LSE CSS 标志、CR/CFGR/CIFR/CIER/BDCR/CSR/HSICFGR/CRRCR/CSICFGR |
| 2/4 | D1/D2/D3 分频、PLL 时钟源与使能配置、三组 PLL DIVR/FRACR、D2CCIP2R（含 USB 时钟选择）、SCB CCR |
| 3/4 | exception、fault_capture、stack_valid、MSP/PSP/EXC_RETURN、PC/LR/xPSR、R0–R12 |
| 4/4 | CONTROL/PRIMASK/BASEPRI/FAULTMASK、SCB 故障寄存器、FPCCR 与整个 NMI 记录校验 |

除 id/part 外本版本 NMI 字段统一按十六进制输出。fault_tick 是最近一次日志心跳，并非精确硬件触发时刻；tick_valid=0 时不使用。hse_css/lse_css 从捕获的 CIFR 中提取。硬件 CSS 可能已在进中断前切换时钟，因此 CFGR/CR 表示进入 NMI 后的状态，无法还原故障发生前全部瞬时状态；两标志为零也不能据此排除全部硬件 NMI 来源。

NMI 有独立计数、保留记录与应急栈，可以打断 HardFault 捕获而不覆盖后者。fault_capture=1 表示打断其捕获路径；xPSR 低位可辅助识别被打断的异常。允许读取应急栈所在的完整 H723 DTCM/AXI 物理范围；非法 EXC_RETURN、地址越界、不对齐或压栈错误时跳过栈读取，stack_valid=0 时 PC/LR 无效。若 HardFault 记录尚未完整提交，复位后会判无效而不是补报半写数据。

四段全部入队不立即清 pending；仍须队列排空、最后一包完成且会话未变。BUSY 保留当前段，重连从第一段重发。传输层发送完成不等于 SD 已落盘。

Watch 可查看 g_nmi_record、g_nmi_log_diag（pending/valid/invalid/cold_boot/reports/retries/replay_restarts/part）；原有 g_fault_log_diag.pending 不包含 NMI，统一使用 FaultLog_Service 调度。

## 测试

仅在 FAULT_LOG_ENABLE_TEST=1 时提供 g_nmi_test_request：Watch 写 1，由正常日志任务消费并写 ICSR.NMIPENDSET 触发 NMI，不改动晶振和 PLL。预期 IWDG 复位后收到四段，exception=2、hse_css=lse_css=0（未发生真实 CSS 时），再恢复普通日志。该开关同时保留原有 10 秒周期 UDF 测试，故障回报完成后会继续注入 HardFault。发行配置恢复 0，真实 NMI 捕获仍启用。

软件触发只能验证入口、保留与传输流程，不能代替真实 HSE/LSE 时钟故障试验。保留与本次烧录一致的 AXF/MAP 定位 PC。
