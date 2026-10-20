# NMI 时钟现场增量

已修改轮腿主板 User/Log/fault_log.c/h，新增 nmi_log.c/h 与 NMI_REPORTING.md；在 USER CODE 增加 NMI 重命名宏，在 Keil 项目添加 nmi_log.c。

NMI 使用独立 272 字节记录及 1024 字节栈，先 RCC 时钟后 CPU，复位后优先四段回报。原 HardFault 的 180 字节记录、version 1、三段格式及现有配置不变。保留区仍为 4KB，编译期检查容量。

本机 MSVC /W4 模拟测试测试开关 0/1 均通过，使用实际更新后的两个模块：原 20 轮 HardFault 回归，NMI 时钟/CSS 保存、异常打断独立性、应急栈帧、四段优先级、BUSY/断线重连、发送排空后清除、暖复位、校验拒绝、不可读栈、软件触发。

静态核对：所有 RCC 寄存器存在于当前 STM32H723 CMSIS 头；NMI 项目源项仅一份；生成中断文件除 USER CODE 中的一行宏外字节相同；package/staged/实际主板 c/h 完全一致。

轮腿主板 Keil ArmClang 6.24 编译通过（0 错误、0 警告）。新 AXF NMI 向量为 0x0800FCA1，入口首先读取 MSP/PSP；MAP 保留区使用 2504/4096 字节，两份记录、两份栈均位于 DTCM 保留区。细节及 AXF SHA256 在 wheel_elf_verification.json；保存 wheel_nmi.axf/map。User_add 隔离构建两次审批均超时，命令未执行；主板构建不受影响。未烧录或进行硬件触发试验。

User_add 七文件增量已发布，发布后哈希与 package_manifest.json 完全一致，原文件备份在 before_user_add；完整七文件增量在 package，manifest 在 package_manifest.json，publish_pending.ps1 带原文件备份。
