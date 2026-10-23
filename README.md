# STM32 日志记录模块

STM32F405 独立记录板通过 USB Host 接收主板 CDC 日志，按行添加 RTC 时间戳，经环形缓存写入 SD 卡。配套 H723 主板支持包提供发送队列、READY/ACK 握手、业务告警和复位后故障补报。

更新日期：2026-10-24。已在线查询 GitHub `main`，最新提交为 [26d4f61：v2.0 硬件新板](https://github.com/QMAX-CYQ/recorder233/commit/26d4f61a756ac4028c1a5a5be79ceb7f32b7c777)，与本地 HEAD 一致。版本号来自提交说明，不代表已发布同名 tag。

## 文档入口

| 文档 | 内容 |
|---|---|
| [使用说明](使用说明.md) | 供电和接入准备、编译烧录、RTC 校准、灯态、电池测量、取卡及排障 |
| [开发文档](log开发文档.md) | 版本演进、关键参数、Watch 变量、验证依据和已知边界 |
| [主板支持包](User_add/README.md) | H723 发送端、任务、链接布局及异常入口接入 |
| [D-Cache 移植](User_add/DCACHE_PORTING.md) | H723 USB DMA 内存和 MPU 配置 |
| [NMI 补报](User_add/NMI_REPORTING.md) | 时钟与 CPU 现场、IWDG 复位后的四段补报 |
| [业务告警](User_add/STATUS_ALERTS.md) | 模块离线、恢复和限频接入 |
| [USB Host 补丁](recorder_f405/tools/usb_host_recovery/README.md) | CubeMX 生成后恢复中间件补丁、Git 路径及检查命令 |

## 快速使用

1. 打开 `recorder_f405/MDK-ARM/recorder_f405.uvprojx`，当前使用 ARM Compiler 6.24。
2. 在 `recorder_f405/User/Recorder/control_def.h` 保留 `INFO_FROM_DEVICE`，按使用说明完成首次 RTC 校准，日常固件使用 `RTC_FLASH_SAVE_ENABLED=0U`。
3. 在记录板断电时插好 FAT32 SD 卡，按实际硬件连接主板 USB CDC Device 与 F405 USB Host。
4. 主板按 `User_add/README.md` 接入支持包，修改业务采样对象并烧录配套固件；该目录不是 F405 代码，也不是可以单独烧录的主板工程。
5. Rebuild 并烧录记录板。正常记录时，PA0 的 SD 灯呼吸，PA1 的通信灯随日志接收以 2 Hz 闪烁；链路就绪但无日志时通信灯常亮。
6. 停止发送、确认写入同步结束后断电取卡，读取 `RbN.txt`。不足 16 KiB 的尾部缓存仍可能未落盘，详见使用说明。

## v2.0 硬件功能

- TIM2：PA0/CH1 为 SD 灯，PA1/CH2 为通信灯；84 MHz 定时器时钟，PSC=83、ARR=999，PWM 1 kHz。引脚用途按源码核对，提交标题下的顺序描述不作为接线依据。
- ADC1：内部 VBAT 通道，480 Cycles；无需将纽扣电池额外接到 GPIO ADC。`battery_voltage` 当前仅上电读取一次，参考电压暂按 3.3 V 换算。
- 新板增加 SPI1、调整 USART1 并移除 USART6。外设初始化不等于已经实现所有对应业务，接线以实际原理图和 `.ioc` 为准。

## CubeMX 重新生成后

在 `recorder_f405` 目录运行，然后 Rebuild：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\usb_host_recovery\Apply-RecoveryPatch.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\usb_host_recovery\Apply-RecoveryPatch.ps1 -Mode Check
```

脚本自动查找 Git，支持 `-GitPath`。已完整应用时不会重复修改；库版本不匹配时停止，按工具 README 核对。生成后同时检查 LED 周期任务、ADC 用户代码、源文件列表和 Include Path。

## 验证与边界

已有 v2.0 构建记录为 Keil AC6.24，0 错误、0 警告。硬件灯效、VBAT 校准、USB 异常恢复及长期记录需要按对应测试范围验证；编译通过不等于完成这些硬件测试。受控 USB 恢复不提供逐条持久化 ACK，当前也没有完整的 SD 热插拔恢复或停机尾刷功能。
