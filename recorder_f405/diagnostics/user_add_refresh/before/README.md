# STM32H723 故障捕获与复位后补报组件

从当前主板固件提取。适用基线：STM32H723、Cortex-M7、Keil ARM Compiler 6（armclang），IWDG 暖复位后 RAM 保留。当前应用未启用 D-cache。不是向任意 MCU 加入两个文件即可使用的通用库。

## 文件

| 文件 | 用途 |
| --- | --- |
| `fault_log.c/h` | 异常入口、应急栈、现场记录、校验、三段 ERROR 补报 |
| `fault_log_config.h` | 保留区选择与测试开关，C 和链接脚本共用 |
| `fault_log_port.c/h` | 日志/链路适配层，附当前主板实现 |
| `fault_noinit.sct` | 当前 H723 Flash/RAM 布局的 Keil scatter 模板 |

发行默认 `FAULT_LOG_ENABLE_TEST=0`，不会自动制造故障。`FAULT_LOG_TEST_IRAM1=1` 使用已验证的 IRAM1/DTCM 写后读回版本。主工程原有文件未迁移，仍使用原实现；不要把原实现与本组件同时编译。

## 接入现有主板工程

1. 添加本目录的 `fault_log.c`、`fault_log_port.c`，移除原 `User/Support/fault_log.c` 的编译项。将本目录加入头文件搜索路径，并确保使用本目录的同名头文件；避免旧目录中的同名文件遮蔽配置。
2. 在 Keil Linker 关闭自动内存布局，选择本目录 `fault_noinit.sct`，随后 Rebuild。模板按 1MB Flash、128KB DTCM、320KB AXI SRAM 配置，其他型号/Bootloader 布局须调整。
3. `main.c` 的 `USER CODE BEGIN Includes` 中包含 `fault_log.h`；在 `main()` 的 `USER CODE BEGIN 1` 中调用 `FaultLog_BootInit()`，必须早于 HAL_Init、复位标志清除和其他保留区访问。
4. 在 `stm32h7xx_it.c` 的 `USER CODE BEGIN Includes` 内加入以下宏，让 CubeMX 生成的四个空处理函数改名，真实异常入口由本组件提供：

```c
#define HardFault_Handler FaultLog_GeneratedHardFault_Handler
#define MemManage_Handler FaultLog_GeneratedMemManage_Handler
#define BusFault_Handler FaultLog_GeneratedBusFault_Handler
#define UsageFault_Handler FaultLog_GeneratedUsageFault_Handler
```

原主板工程已完成步骤 3、4，不要重复添加。启动汇编保持默认，不需要临时采样代码。检查最终向量表引用本组件的真实异常入口，避免其他文件重复定义处理函数。不要用普通 C 包装函数调用 naked 异常入口，否则会改变原始 LR/SP。

## 任务中的调用

由单一日志任务周期服务。先维护发送链路，再处理故障补报；补报期间暂缓其他日志，避免队列持续不为空：

```c
for (;;) {
    FaultLog_Heartbeat(osKernelSysTick());
    LogLinkService_HS();
    CDC_TxService_HS();
    FaultLog_TestService(osKernelSysTick()); /* 默认关闭，为空操作 */
    if (!FaultLog_Service()) {
        /* 此处生成普通日志；补报等待时先不生成。 */
    }
    osDelay(10U);
}
```

`FaultLog_Service()` 返回 1 表示仍在等待/补报，0 表示没有待补报现场。当前主板任务已经按此顺序调用。心跳只保存上次任务时间，异常中不调用 RTOS。

## 移植日志和 USB 接口

`fault_log.c` 只依赖 `main.h` 提供芯片/CMSIS 寄存器，以及 `fault_log_port.h` 的接口；不直接引用原工程的日志、USB 或 RTOS 对象。附带适配文件仍依赖当前工程的 `log.h`、`usbd_cdc_if.h`、`cmsis_os.h`，其他固件需要实现同名接口：

- LinkReady：真正能够补报的链路状态，当前基于 READY/ACK 握手，而非仅 USB 已枚举。
- Session：接收端会话变化时改变；DisconnectCount：每次断链/DeInit 时递增。用来在连接变化后从第一段重新发送。
- TxIdle：发送队列为空且最后一个在途包已完成。不能仅以入队成功、USB 非 BUSY 作为补报完成。
- Tick：毫秒时间。
- Error：复制完整日志行并返回 OK/BUSY/FAIL；队列满时返回 BUSY，调用方下轮重试。附带适配器使用任务栈上的格式化缓冲，底层必须复制数据。

附带适配器和当前 `sys_printf` 各有一个 768 字节局部缓冲。当前主板日志任务栈为 1024 个字（4KB）；移植时要按实际调用链检查任务栈余量，不能沿用只有几百字节栈的小任务。

链路状态读取接口也会在短关中断区调用，只允许读状态，不能延时、申请锁或调用阻塞 USB/RTOS 服务。不要让不可用链路返回 ready，也不要在数据未发送完成时返回 idle，否则会提前清除 pending。

## 保留内存和复位

- 两个普通 RAM 区分别限制到 `0x2001F000`、`0x2404F000`，顶端 4KB 中选一个作为 `.bss.noinit.fault_*` 的 UNINIT 区。记录和应急栈一起迁移。
- IRAM1：记录 `0x2001F000`，应急栈 `0x2001F0B8`。提交 checksum/magic 后，以 volatile 逐字读回全部 180 字节记录，再执行 DSB，满足 DTCM/ITCM 编程后读回要求。
- IRAM2：记录 `0x2404F000`，应急栈 `0x2404F0B8`。头部两个对齐 64 位单元完整读回并原值写回，再执行 DSB。
- 宏同时决定链接布局和读回策略，修改后 Rebuild，不能只在 C 编译选项中另行定义内存选择。检查 MAP 中的 UNINIT 区、记录和应急栈地址，以及普通数据/栈无重叠。
- 冷启动跳过未初始化 ECC RAM 的有效性读取，先初始化记录；暖复位校验旧记录。UNINIT 不表示掉电保存，程序下载、调试器 RAM 初始化或启动代码清 RAM 也可能清除数据。
- 必须启用 IWDG。捕获后只做整数/寄存器访问，最后停在异常中不喂狗，等待 IWDG；不依赖 USB、RTOS 或软件复位。必须确保其他高优先级异常也不会继续喂狗。
- 若启用 D-cache，应先为保留区设置适当内存属性或实现缓存维护；当前 AXI 读回写回不能代替 CPU cache clean。其他 MCU 的寄存器、复位标志、ECC 粒度及 `fault_frame_address()` 允许栈范围都须重新核对。
- 应急栈固定 1024 字节，异常入口汇编与 `FAULT_LOG_STACK_BYTES` 必须保持一致。异常调用链避免浮点；栈帧不可读时仍补报状态寄存器，`stack_valid=0` 时 PC/LR 不可信。

## 验证与定位

先用测试开关 1 验证：链路就绪且补报完成后 10 秒注入 UDF，等待 IWDG 复位，检查三段 ERROR、校验和及连续轮次；发行时改回 0。测试宏关闭不会关闭真实异常捕获。

现场包含 PC/LR、R0–R12、MSP/PSP、EXC_RETURN、CFSR/HFSR 等。分析时使用与烧录固件完全对应的 AXF/MAP；重新编译后地址可能变化，旧测试地址不可直接套用。正常情况下不输出 fault_boot、阶段标记等临时诊断。
