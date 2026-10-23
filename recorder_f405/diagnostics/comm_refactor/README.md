# 通信分层回归

日期：2026-10-24。

- `run_test.cmd` 使用实际 `User/Comm/comm.c`，USB 后端以测试替身验证函数指针绑定、初始化仅执行一次、初始化前不轮询、链路状态变化、接收计数回绕及 NONE 不调用后端。SPI、UART 和非法配置必须编译失败。
- 本次原生测试通过。`diagnostics/usb_recovery_reuse/test_recovery.py` 已适配新后端路径，恢复回归通过，包括正常静默、回调竞争、背压不误触发、初始化失败、枚举超时、计时回绕及 100 次恢复的资源平衡。
- 对比迁移前源码，CDC 回调、超时诊断及 Host 恢复函数保持原逻辑；接收处理主体仅移除外层任务循环，将恢复分支的延时/continue 改为返回，由通信任务统一调度。
- 主 Keil 工程 AC6.24 全量 Rebuild：0 错误、0 警告，已生成 HEX。链接后 Code=110820、RO=2616、RW=1176、ZI=118560 字节。构建日志见 `MDK-ARM/comm_build.log`（相对工程根目录）。

USB Host 外设仍由默认任务的 CubeMX 初始化代码启动，之后调用 `comm_init()`；通信任务等待 initialized 再执行后端。没有新增 SPI/UART 接收、改变缓存或握手参数。本次未烧录或执行真实 USB 重连、拔插和长期压力测试。

## READY DMA 对齐修复

重构后用户反馈 Host 已进入 HOST_CLASS、ring_samples 持续增长、接收提交成功但没有数据回调。进一步 Watch 显示 READY 的 attempts/started/completed 同步增长、tx_failed=0，而 ack_received=0；这证明任务运行与发送完成，但不能证明控制帧内容正确。

MAP 检查发现 `log_host_ready_frame` 位于 `0x200008E1`，未满足 USB Host DMA 的 4 字节对齐要求。底层 `stm32f4xx_ll_usb.c` 在写 HCDMA 时明确要求 xfer_buff 按 32 位对齐；记录板 usbh_conf.c 开启了 DMA。给 READY 帧增加显式 aligned(4)，避免链接布局变化后依赖偶然对齐。

`check_usb_dma_layout.ps1` 在修复前对旧 MAP 报错，修复后通过：READY 帧 `0x200008E4`，接收缓冲 `0x200186B8`。AC6.24 重新编译链接 0 错误、0 警告，HEX 已更新。尚需烧录后确认 ACK 返回和日志接收恢复；未将当前证据表述为已完成硬件修复验证。
