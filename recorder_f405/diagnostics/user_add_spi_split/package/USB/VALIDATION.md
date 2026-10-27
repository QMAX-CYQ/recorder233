# 2026-10-19 NMI 时钟现场增量验证

- 主板工程已加入独立 nmi_log.c/h、USER CODE 中的 NMI 入口重命名与 Keil 源文件项。自定义文件承载记录、对齐和汇编入口；未改动生成区默认 NMI 函数体。
- NMI 272 字节独立记录、1024 字节独立栈；HardFault 原 180 字节/version 1 记录及三段回报保留。编译期检查两份记录、两份栈和标记可容纳于原 4KB UNINIT 区。
- 使用更新后的实际 fault_log.c 与 nmi_log.c 本机模拟测试，测试开/关两种配置通过。原有 20 轮故障回归及 NMI CSS/时钟保存、打断 fault 时不覆盖记录、应急栈帧、四段优先补报、BUSY/重连、队列/在途排空后清除、校验拒绝和软件触发均覆盖。
- 轮腿主板 Keil ArmClang 6.24 构建通过：0 错误、0 警告。直接解析新 AXF 确认 0x08000008 的 NMI 向量为 0x0800FCA1，指向 nmi_log.o 的 naked NMI_Handler，入口首先读取原 MSP/PSP。
- 主板 MAP：NMI 记录 0x2001F000/272 字节，NMI 栈 0x2001F110/1024 字节，fault 记录 0x2001F510/180 字节，fault 栈 0x2001F5C8/1024 字节。UNINIT 共 2504/4096 字节。
- User_add 隔离 H723 构建仍待审批执行；此前审批服务曾容量不足/超时，不能当作该构建通过。
- 未烧录或进行软件 NMI/真实 CSS 硬件触发试验。软件测试操作、回报格式及局限见 NMI_REPORTING.md。

证据与待执行构建：F405 工作区 diagnostics/nmi_log_refresh。以下为历史验证记录，不能代替此次新增 NMI 的目标构建及硬件验证。

---

# 2026-10-18 status 告警提取验证

- 从 rm_main_gimbal 的 status_task 提取模块独立离线/恢复状态机，提供
  status_log.c/h 和可选 examples/status_log_gimbal.inc。16 个模块名、事件
  格式和 LogEvent_Post/Service 沿用现有接口，没有改动 log.c/h 消费者。
- 核心不包含 HAL/RTOS/电机/协议头文件，时钟和状态由调用者传入；保留
  5 秒启动宽限、1 秒模块限频、恢复优先重试、拥塞快照合并行为，并补充
  未初始化/模块索引保护。状态机由单个任务串行调用。
- 原生测试直接编译实际 status_log.c，覆盖宽限边界、从未上线、正常恢复、
  模块独立限频、HAL/RTOS 换算、空闲模块过滤、非法模块、队满重试、恢复
  先于新离线、长期拥塞合并、未启用模块排出旧恢复、32 位 tick 回绕；通过。
- 隔离 H723 完整工程使用实际 User_add 包和云台适配示例，替换验证副本中
  的旧状态机；ArmClang 6.24 编译链接 0 错误、0 警告。未修改实际
  rm_main_gimbal、轮腿主板或 F405 固件，未烧录硬件。
- 本次新增逻辑尚无新硬件日志验收。业务 online 极性、注册条件和时间轴仍需
  按 STATUS_ALERTS.md 核对；不以模拟投递成功证明 USB/SD 最终持久保存。

测试与构建位于 `diagnostics/user_add_status_refresh`。下面保留缓存适配及
此前整包验证记录。

---

# 2026-10-18 D-Cache / USB DMA 增量验证

- 本次只更新缓存适配、CDC 受保护对齐声明、内存模板和文档；未替换业务
  log_snapshot、事件队列、协议、fault 核心或 F405 接收固件。
- 新增 usb_dma_memory.h / usb_dma_port.h，后者提取自主板已有 USER CODE
  实现，通过现有 MSP 初始化钩子接入；USB DMA 始终启用，缓存关闭时跳过 MPU。
- 隔离 H723 完整验证工程使用本次 staged 包和接入示例，ArmClang 6.24 编译
  链接通过，0 错误、0 警告。实际主板工程文件未被这次库构建修改或烧录。
- MAP 验证 PCD/Setup、device/control、描述符、字符串、CDC 类静态分配和
  RX/TX 均位于 0x24048000..0x2404BFFF；PCD/RX/TX 为 32 字节对齐。
  大日志队列不在非缓存区；存在两个 USB 区链接符号依赖。
- 本机模拟使用实际 dcache_handle.c，覆盖关闭缓存、空地址、零长度、整数
  范围溢出和 TX/RX 行范围对齐。USB port 模拟只替换 CPU/HAL 和链接符号，
  覆盖缓存开/关、重复初始化、保留 MPU/中断状态、已占用 Region 7 拒绝、
  收发屏障及原 HAL 返回值传递。四种场景均通过。MSVC 测试桩有常量条件及
  有符号比较警告；目标 ArmClang 构建无警告。
- 迁移来源的轮腿主板 Rb11 上电日志：15246 条完整记录，seq 0..15245 连续，
  152.45 秒内相邻 tick 均为 10ms，溢出/启动失败/格式错误为 0，
  CFSR/HFSR/DFSR 均为 0。文件末尾一条不完整。此为来源工程的上板证据，
  不是本次打包后的新硬件测试，也不证明其他 DMA 或传感器接收已正常。
- 新包仍需用户按 DCACHE_PORTING.md 接入。未自动运行 CubeMX 重新生成；
  关闭缓存并开启 DMA 的完整硬件回归仍需补测。AXI fault 保留区缓存策略
  不由 USB MPU 区自动覆盖，默认 DTCM 配置保持不变。

本次构建、MAP 和测试文件位于 F405 工作区
`diagnostics/user_add_dcache_refresh`。下面保留上一版日志/fault 回归记录。

---

# 整合包验证记录

日期：2026-10-17。

- 独立 H723 完整工程使用 ARM Compiler 6.22，替换为本包 log_task、log、CDC、fault 核心及适配器完成编译和链接，0 错误、0 警告。
- 验证工程通过编译宏将原 DebugTask 入口/句柄映射到 LogTask，未改动主板或 F405 实际工程文件。
- 默认配置 IRAM1=1、故障测试=0；MAP 中记录位于 0x2001F000，1024 字节应急栈位于 0x2001F0B8，UNINIT 区独立。测试注入函数未进入默认固件。
- 模拟测试覆盖实际 fault 捕获、校验拒绝、冷/暖启动、不可读栈、三段补报、队列排空后清 pending、BUSY 重试和断链重发；测试关闭和开启两种配置均通过。开启模式完成 20 轮模拟捕获/暖复位/补报/重新计时。
- 实际发送端 READY/ACK 服务测试覆盖所有控制帧拆分位置、校验失败、旧会话、新会话、队列 BUSY、挂起和断链。
- 实际 log.c 测试覆盖 sys_printf 状态返回、事件初始化/参数校验、32 项容量、未握手等待、BUSY 保留队头、offline/recovered 格式及消费。

以上为编译/链接及模拟测试，不等于新整合包已上板验证。硬件移植仍须检查 RAM 保留、IWDG、任务栈、USB 配置、业务采样对象和实际发送。motor/power 示例须由目标业务提供数据。

完整构建日志和测试代码保存在 F405 工作区 diagnostics/user_add_refresh；源包未包含主板业务库、HAL 或 RTOS。
