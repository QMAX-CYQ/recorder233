# Log 开发文档

> 更新日期：2026-10-24。代码基线：`main` / `26d4f61a756ac4028c1a5a5be79ceb7f32b7c777`（v2.0）。
> 仓库：[QMAX-CYQ/recorder233](https://github.com/QMAX-CYQ/recorder233)。本次通过 `git ls-remote origin refs/heads/main` 在线确认 GitHub main 与本地 HEAD 一致；版本内容依据提交记录、现行源码及已有诊断报告核对。GitHub 连接器返回 404，远端核对使用 Git 完成。

## 1. 范围与证据说明

- `oldest version/`：早期 H7 板上直接记录 SD 卡的工程归档，主要对应 v0.1～v0.4；历史提交中的路径为 `rm_main/`。
- `recorder_f405/`：v0.5 起的独立记录板工程，当前接收端实现位于此处。
- `D:/RM/rm_main_gimbal/`：当前配套发送端工程。发送队列、10 通道压力日志和主板握手说明来自该工程源码及诊断归档，不能视为本仓库 `rm_main/` 的现行实现，也不直接归入本仓库某个提交。
- 下文“提交记录现象”来自提交说明；“报告实测现象”来自已有日志审核；“代码风险”是源码推断。没有详细故障记录的版本不补造复现过程或唯一根因。
- `User_add/`：仓库当前提供的 H723 发送端支持包，含日志、USB CDC、fault/NMI、D-Cache 和业务告警；不是完整主板工程，也不加入 F405 编译。
- 版本号来自提交标题，并非已确认存在的 Git tag。软件版本 v2.0 与日志文件头 `VERSION=1` 是不同概念。

## 2. 当前日志链路与参数

```mermaid
flowchart LR
    A[主板生成日志] --> B[主板 CDC 发送 FIFO]
    B --> C[F405 USB Host 接收与控制帧过滤]
    C --> D[按行插入 RTC 时间戳]
    D --> E[64 KiB 环形缓存]
    E --> F[16 KiB 分块写入 FatFs]
    F --> G[f_sync / SD 卡]
```

正常启动：F405 环形缓存初始化、SD 挂载、日志文件创建、文件头写入并同步成功，且 USB 接收请求已提交后，发送 READY；主板把同会话 ACK 放进发送 FIFO 后才生成日志；F405 收到 ACK 后接受正文。

| 参数 / 位置 | 当前值及作用 |
|---|---|
| `User/Recorder/recorder_inter.h`：`USE_RECORDER` / `USE_RTC` | 均为 1 |
| `TSKD_FRE` | 200 ms；记录任务循环中的延时，实际间隔还含执行耗时 |
| `PCFILE_BUFFER_SIZE` | 64 KiB；lwrb 留一个空槽，可用容量为 65535 字节 |
| `PCFILE_WRITE_BUFFER_SIZE` | 16 KiB（16384 字节）；不足一块时留在 RAM |
| `PCFILE_SIZE` / `FILE_MAX_NUM` | 8 MiB 切换阈值 / 3000 个索引槽；按块检查，文件可超过阈值 |
| `FREE_SIZE_LIMIT` | `23552 * 1024` 字节，约 23 MiB；低于阈值执行旧文件清理 |
| `usb_buffer` / 时间戳缓冲 | 512 / 5120 字节；接收请求长度来自 CDC IN 端点大小 |
| `usb_task` 周期 | 1 ms 的 `osDelayUntil`；不代表每毫秒必定收到一个 USB 包 |
| `SyncToFile` 写入预算 | 循环约 1 s；这是循环预算，不能中断一次阻塞的 `f_write` |
| 文件名 / 元数据 | `RbN.txt`；`oldest.txt`、`lastest.txt`、`create_seq.txt`。保留历史拼写 `lastest` |
| 文件头 | 352 字节 ASCII 表格；magic `0x52454344`、版本 1、索引、创建序号、作者 |
| 正文 RTC 前缀 | `MMDD-HH:MM:SS `；在每行首个非 CR/LF 字节前插入，跨 USB 包保留行状态 |

## 3. 版本记录

### 开发起点（尚未命名版本）

| 日期 | 提交 | 内容 |
|---|---|---|
| 2026-06-02 | [026a7e9](https://github.com/QMAX-CYQ/recorder233/commit/026a7e9)、[ca4a028](https://github.com/QMAX-CYQ/recorder233/commit/ca4a028) | 初始化仓库、提交 `rm_main` 工程 |
| 2026-09-07 | [b2aa8d5](https://github.com/QMAX-CYQ/recorder233/commit/b2aa8d5) | 增加 `sdmmc test.7z`；提交说明记录在正点原子板测试正常 |
| 2026-09-21 | [ab351ed](https://github.com/QMAX-CYQ/recorder233/commit/ab351ed)、[afd5e10](https://github.com/QMAX-CYQ/recorder233/commit/afd5e10) | 新板测试及忽略规则；未提供独立 Bug 复现报告 |

### v0.1：初步运行与文件索引（2026-09-21）

提交：[13bd370](https://github.com/QMAX-CYQ/recorder233/commit/13bd370)。关键文件：`rm_main/User/Recorder/recorder.c`。

- 沿用环形缓存、SD 分块写入、文件轮转及文件头扫描恢复索引的结构；本次把文件头读写改为 `recorder_file_header_t` 二进制结构，保存 magic、version、index、create_seq。
- 记录任务使用 `osDelay(TSKD_FRE)`；该提交删除了此前的 `check_buffer_overflow_count`、`check_buffer_overflow_bytes`、`check_buffer_max_used`。
- **现象与限制：** 提交仅标为“小成 / 测试版”；二进制头中存在非文本字节，是后续文本可读性问题的背景。此时 `RecorderData` 未用实际写入长度建立有效的溢出统计，不能用写卡错误为 0 证明无丢失。
- **Watch：** `isSdCardInit`、`pcfile_status`、`check_write_ok_count`、`check_write_fail_count`、`pcfile_buffer_lwrb`。本版本不能再添加已删除的三个 `check_buffer_*` 变量。

同日后续提交：

- [5036b2f](https://github.com/QMAX-CYQ/recorder233/commit/5036b2f)：修改 `.ioc`，避免看门狗初始化函数自动生成在 main 中。提交没有提供看门狗复位的实测次数，不据此认定发生过复位 Bug。
- [3c964e3](https://github.com/QMAX-CYQ/recorder233/commit/3c964e3)：`BSP_SD_Init` 在 HAL 初始化成功后显式配置 SDMMC 4 bit 总线，配置失败返回错误；同时清理 main 中看门狗相关代码。

### v0.2：偶现数据异常与写入参数调整（2026-09-28）

提交：[833f0fc](https://github.com/QMAX-CYQ/recorder233/commit/833f0fc)。关键文件：`rm_main/User/Recorder/recorder.c`、`recorder_inter.h`，以及底盘、云台任务。

- **提交记录现象：** “偶现数据异常”；没有提供异常字节、触发负载或唯一根因。
- **修改：** 任务延时 500 → 250 ms，写块 4 → 16 KiB，环形缓存保持 128 KiB；增加写入耗时、累计数据量、平均写入速度统计。
- 增加 `log_flag`，底盘与云台的测试日志受开关控制，便于限定产生日志的时段；清理旧文件头实现的注释备份。
- **解决方案口径：** 该提交通过参数与测试控制调整处理异常，但没有新增端到端序号校验或溢出恢复，不能仅凭差异断言异常一定由 SD 速度不足引起。
- **Watch：** `log_flag`、`cost_time`、`max_time`、`total_time`、`total_Mb`、`averange_time`、`averange_speed`、`averange_cnt`，以及 v0.1 的写卡计数。变量拼写为 `averange`，不要改成 `average`。

### v0.3：可读文本文件头（2026-09-28）

提交：[b8e59d4](https://github.com/QMAX-CYQ/recorder233/commit/b8e59d4)。关键文件：`rm_main/User/Recorder/recorder.c`。

- **提交记录现象：** 文件头 ASCII 格式有问题，文本编辑器打开会卡顿。
- **修改与方案：** 新文件改写固定 352 字节 ASCII 表格头；检查格式化长度、写入返回值与短写；读头兼容二进制头及历史文本格式，保留按 create_seq 恢复新旧文件索引的能力。
- **注意：** 兼容读取不等于重写已有文件，旧日志不会自动变成新文本头；文件头内部版本号仍为 1。
- **Watch / 验证：** `pcfile_status.current_index`、`pcfile_status.oldest_index`、`current_filepath`、`g_create_seq`（静态变量）；直接用文本编辑器检查新文件头，重启确认索引恢复。

### v0.4：压力数据与寄存器诊断（2026-09-29）

提交：[6a7306e](https://github.com/QMAX-CYQ/recorder233/commit/6a7306e)。关键文件：`rm_main/User/Task/debug_task.c`。

- 增加 `[stress] seq=... tick=... data=...`，用递增 sequence 和可计算的十六进制 pattern 检查记录连续性及内容异常。
- 增加 FDCAN 寄存器、CPU fault 寄存器、NVIC 状态诊断，文本日志统一通过 `DEBUG_INFO`；轮转槽数扩为 26。
- `log_flag` 控制测试输出；该历史实现按 1 ms 任务循环运行，与后来的“10 通道 / 每 10 ms 一条”测试配置不同。
- **现象与方案：** 本次是验证能力扩充，提交没有描述新增 Bug。通过 seq 缺口、tick 间隔和 pattern 一致性辅助定位异常。
- **Watch：** `log_flag`、`debug_text_log_enable`、`debug_wave`、写卡耗时与失败计数；`sequence` 是函数内静态符号，可能需要对应作用域定位。

### v0.5：独立 F405 记录板（2026-10-09）

提交：[7adb463](https://github.com/QMAX-CYQ/recorder233/commit/7adb463)。关键目录：`recorder_f405/`。

- 引入 F405RGT6 工程，使用 SDIO、FatFs 和 FreeRTOS，将记录板从早期主控工程独立出来。
- RTC 使用 LSE，硬件以纽扣电池接 VBAT 保持日历；启用 RTC 日志前缀。
- F405 参数调整为 64 KiB 环形缓存、16 KiB 写块、200 ms 任务延时；`RecorderData` 使用临界区与 `LWRB_FLAG_WRITE_ALL`，避免只写部分记录。
- **现象与限制：** 提交记录的是新板测试，没有具体 Bug 复现。此时写缓存接口仍不向调用者返回成功状态；“全写或不写”只能避免半包，不能保证被拒绝的数据得到重试。
- **Watch：** `hrtc`、`isSdCardInit`、`pcfile_status`、`max_lwrb_size`、`check_write_fail_count`、`cost_time`、`max_time`。

### v0.6：F405 USB CDC 接收跑通（2026-10-12）

提交：[cfd5e7c](https://github.com/QMAX-CYQ/recorder233/commit/cfd5e7c)。关键文件：`USB_HOST/App/usb_host.c`、`User/Task/usb_task.c`、`User/Recorder/recorder.c`。

- 配置 USB Host CDC；接收回调取得实际长度，仅发布数据就绪标志，任务负责入缓存。
- 增加返回状态的 `RecorderDataWrite`，入缓存成功后才清除 `usb_rx_ready`；失败保留原包，后续循环重试，避免下一次接收覆盖未处理数据。
- `usb_rx_pending` 标记正在等待回调；仅在 USB 应用 READY 且没有待处理包时提交接收，断开时清状态。
- **现象与方案：** “USB 通信跑通”是提交结论，没有单独故障报告。此版已通过 ring buffer 写 SD，v0.7 的重点是进一步加入背压、时间戳及统计，不能把 v0.6 写成回调直接写卡。
- **Watch：** `Appli_state`、`usb_rx_pending`、`usb_rx_ready`、`usb_rx_len`、`hUsbHostHS`、记录板基本写卡计数。前三个接收符号为静态变量。

### v0.7：USB 背压、按行时间戳与基础统计（2026-10-13）

提交：[0eb0652](https://github.com/QMAX-CYQ/recorder233/commit/0eb0652)。关键文件：`User/Task/usb_task.c`、`User/Recorder/recorder.c/.h`。

- 增加 `RecorderDataGetBufferStatus`，接收前检查 ring 是否初始化及剩余空间；端点请求长度超过接收数组时标记异常。
- 正文按行加 RTC 前缀，支持拆包、合包；时间戳格式化成功后缓存结果，入 ring 成功后才提交新的行首状态，避免重试重复加前缀。
- ring 空间不足时保留当前包并等待，停止提交下一接收请求；增加 USB 回调、空数据、入缓存失败、阻塞、已入缓存包数与字节数统计。
- **代码风险与方案：** 防止接收过快覆盖待处理包或部分写入；背压可能传导到主板，引起发送 BUSY。接收端保留包不代表发送端不会丢日志。
- **Watch：** `usb_rx_ring_blocked_count`、`usb_rx_write_fail_count`、`usb_rx_receive_fail_count`、`usb_rx_ring_free`、`usb_rx_written_byte_count`；完整释义见第 4 节。

### v0.8：RTC 初始时间与链路诊断（2026-10-14）

提交：[ea3d365](https://github.com/QMAX-CYQ/recorder233/commit/ea3d365)。关键文件：`Core/Src/rtc.c`、`main.c`、`User/Recorder/control_def.h`、`recorder.c`、`User/Task/usb_task.c`。

- 增加 `RTC_SetInitialTime` 和 `RTC_INITIAL_*` 宏，校验 2000～2099 年日期与时分秒；可选保存到 Flash Sector 11（`0x080E0000`），最后写入有效标记。
- **代码确认问题：** 旧 RTC 初始化中，在备份域检查之后仍无条件设置时间/日期，重启会覆盖正在运行的日历。改为只有备份域无有效标记时初始化；旧标记升级不改时间。
- 当前 `RTC_FLASH_SAVE_ENABLED=0`，main 仍调用设置函数，但函数的实际设置与 Flash 保存实现受条件编译控制，禁用时返回 `HAL_ERROR`，main 未处理该返回值；只修改时间宏而不启用相应配置，不会设置 RTC。启用后，同一份已保存初始配置不会在每次重启时重新设置 RTC。
- 增加 `g_usb_rx_diag`、`g_recorder_diag`，细分 tick 缺口、等待、写卡错误和耗时。
- 修复 v0.7 的接收请求状态竞态：提交前置 `usb_rx_pending=1`，提交失败再清零，避免快速回调清零后又被提交路径写回 1、导致停止继续接收。这是代码风险修复，未发现单独实机复现报告。
- **Watch：** `g_usb_rx_diag`、`g_recorder_diag`、`hrtc`；该版本尚无 `g_rtc_read_diag`。
- **配置注意：** 当前 Keil IROM 覆盖整个 1 MiB Flash，文档未确认 Sector 11 已由链接配置独立预留。启用 Flash 保存前须核对固件占用与擦除区域。

### v0.8.1：归档整理（2026-10-14）

提交：[c778cb7](https://github.com/QMAX-CYQ/recorder233/commit/c778cb7)。将部分发送端暂存代码、备份和脚本移出本目录；本仓库主要体现为归档文件删除和说明更新。

- 提交明确“不影响功能”，没有新增 Bug 修复或 Watch 变量。
- 接收端源码是当前行为依据；`diagnostics` 中的 originals、暂存 sender/receiver 代码仅作历史参考。

### v0.9：启动 READY / ACK 握手（2026-10-14）

提交：[bc50767](https://github.com/QMAX-CYQ/recorder233/commit/bc50767)。关键文件：`User/Task/log_link_protocol.h`、`usb_task.c/.h`、`User/Recorder/recorder.c/.h`。

- **报告实测现象：** Rb91～Rb160 压力测试启动阶段缺少 seq=32～175，共 144 条；主板队列峰值 32/32，溢出计数 144，与缺失数量吻合。后续稳态记录连续。见 [长期测试报告](recorder_f405/diagnostics/Rb91_Rb160_review.md)。
- **解决方案：** 文件头写入并 `f_sync` 成功后才发布 `recorder_log_ready`；文件创建失败保持 `RS_INIT`。USB 接收请求成功提交后，每 200 ms 发 READY，等待相同 session 的 ACK。
- 控制帧为 16 字节：`RMLG`、类型 READY=1/ACK=2、协议版本 1、长度 16、session、小端 FNV-1a 校验。支持控制帧跨包及与正文合包；握手前业务数据丢弃，控制帧不进入日志文件。
- 配套主板 ACK 与日志共用 FIFO，ACK 入队成功才生成日志；等待期间不推进 seq、不补发等待期间日志。两端固件需要配套。
- **验证边界：** Rb162 未缺 seq，但首 tick 为 230→231→240，有启动调度追赶；配套主板重新对齐 wake_time 后，Rb163 记录周期恢复。该主板改动不是本仓库 F405 提交差异。
- **Watch：** `recorder_log_ready`、`g_log_host_ack_received`、`g_log_host_link_diag`；配套主板 `g_log_link_ready`、`g_log_link_diag`。
- 握手证明启动条件满足，未实现逐条落盘 ACK、运行中 SD 故障恢复或断开期间数据可靠重传。

### v0.9.1：FAT 修改时间、RTC 快照与索引文本修复（2026-10-15）

提交：[5f8950a](https://github.com/QMAX-CYQ/recorder233/commit/5f8950a)。关键文件：`FATFS/App/fatfs.c`、`User/Recorder/calendar.c/.h`、`recorder.c`、`log.c`、`User/Task/usb_task.c`。

1. **文件修改时间缺失 / 不正确：** 旧 `get_fattime` 返回 0。现在共用 RTC 快照，将年份基准转换为 FAT 的 1980 年基准，秒按 2 s 精度打包；RTC 读取失败使用 2000-01-01 00:00:00，并计数。`_FS_NORTC=0`。目录修改时间随 FatFs 同步/关闭更新，不是每条日志到达时立即刷新。
2. **RTC 并发读取风险：** 旧代码通过共享日历字段取时，多个调用者可能交错读取影子寄存器或读到混合字段。统一 `RTC_ReadSnapshot`，短 PRIMASK 临界区按 GetTime→GetDate 顺序读取，恢复原中断状态；即使取时失败也读取日期以释放影子锁。校验完整日期、闰年和时分秒，调用者使用局部快照。这是源码风险修复，不代表已经实测复现跨午夜异常。
3. **索引文件旧尾巴：** 保留索引、删除日志后，实测 `create_seq.txt` 为 `32 00 37 00`，即新值 `2` 后仍有旧值 `7`；`lastest.txt` 同样有旧尾部。根因是 `FA_OPEN_ALWAYS` 覆盖不截短，且写入字符串 NUL。改用 `FA_CREATE_ALWAYS`、只写有效文本加 CRLF，并检查短写和关闭结果。
4. **索引读取越界风险：** 旧实现最多读 32 字节，再写 `file_name[br]='\0'`，满长时越界。新读取函数预留结束符空间，兼容旧 NUL 文本；序号解析拒绝负数、垃圾尾巴及 uint32 溢出。

新索引示例：`create_seq.txt` 内容为 `2\r\n`、`lastest.txt` 为 `Rb1.txt\r\n`、`oldest.txt` 为 `Rb0.txt\r\n`。既有日志不必删除；下次保存索引时清除尾部残留。重建索引文本并非断电原子替换，仍依赖日志文件头恢复。

**Watch：** `g_rtc_read_diag`、写卡错误结构与文件状态。索引保存目前没有专用全局失败计数，`g_recorder_diag` 也不统计文件头和索引写入；索引异常仍需检查函数返回路径或 SD 文件实际内容。

### v0.9.2 / v0.9.3：文档与归档整理（2026-10-15）

提交：[716d2a5](https://github.com/QMAX-CYQ/recorder233/commit/716d2a5)、[fc2475e](https://github.com/QMAX-CYQ/recorder233/commit/fc2475e)。新增开发及使用文档，整理仓库目录；旧 H7 工程移动到 `oldest version/`。这两个版本主要完善文档入口及使用前准备，不据此推断新增运行时功能。

### v1.0：超时与重新握手（2026-10-16）

提交：[be8429a](https://github.com/QMAX-CYQ/recorder233/commit/be8429a)。提交说明针对短暂 offline、复位或卡死后的重新握手。当前实现还包含 USB 接收超时快照与 Host 受控恢复，现行行为应结合 `usb_task.c/.h` 判断，不能把“无数据”直接等同于故障。

- `USB_RX_TIMEOUT_MS=1000`：保存首次及最近四次接收等待超时现场；诊断超时不等于已丢数据。
- `USB_HOST_RECOVERY_TIMEOUT_MS=2000`：接收待完成、没有待处理包且不处于 ring 背压时，结合通道 CHENA/CHDIS 异常或 URB_ERROR/STALL 判断是否恢复。
- 恢复请求交给原 Host 线程串行服务，重新建立 Host/HCD 状态、清理当前应用会话并重新枚举。失败冷却 5000 ms，已连接情况下重新枚举/握手等待 10000 ms 后可重试。
- `core_restarted` 仅表示内核重建接口成功；`restored` 还要求 APPLICATION_READY、有效 ACK 及新的接收完成回调。恢复不清 SD 环形缓存，但暂存和 USB 在途数据可能丢失。
- ST Host 核心增加 `USBH_ResetStateForRecovery` / `USBH_UserRecoveryService` 钩子，CubeMX 生成后需要恢复补丁。现行代码已移除 HAL 跟踪环及首次停止快照，诊断归档里的旧变量不能直接用于当前固件。

**Watch：** `g_usb_rx_timeout_diag`、`g_usb_host_recovery_diag`、`g_usb_rx_diag.raw_bytes`。完整历史方案见 [受控恢复记录](recorder_f405/diagnostics/usb_host_recovery/README.md)，当前补丁流程见 [工具说明](recorder_f405/tools/usb_host_recovery/README.md)。历史报告有实测边界，不能以其旧源码清单替代当前代码。

### v1.1：主板支持包接入（2026-10-18）

提交：[4adac64](https://github.com/QMAX-CYQ/recorder233/commit/4adac64)。提交记录说明 `User_add` 已在目标底盘工程作为支持库运行并更新 README。当前包统一使用 `log_task` / `LogTaskHandle` / `g_log_task_diag`，提供 CDC FIFO、握手、故障补报及业务事件支持。

接入需要替换业务采样对象、创建任务、接入队列和回调，并配置故障保留区及最早启动初始化。提交里的目标工程运行结果不意味着其他 MCU 或所有默认采样对象可以直接使用。具体以 [User_add/README.md](User_add/README.md) 为准。

### v1.1.1：H723 D-Cache 与 USB DMA（2026-10-18）

提交：[41c456f](https://github.com/QMAX-CYQ/recorder233/commit/41c456f)。增加 `usb_dma_memory.h`、`usb_dma_port.h`、链接布局及 DMA 辅助接口。USB DMA 专用 16 KiB 区域涵盖枚举、控制传输与 CDC 数据对象；D-Cache 开启时配置 MPU Region 7 为非缓存区。仅在 CDC TX 上 Clean 不能覆盖枚举与接收。

必须同时匹配 scatter、内存常量和 `usbd_conf.c` USER CODE 钩子；不能在运行中复用 Region 7 或任意切换 D-Cache。该方案适用于 H723 发送端，F405 不编译它。内存地址、验证方式及硬件边界见 [DCACHE_PORTING.md](User_add/DCACHE_PORTING.md) 和 [VALIDATION.md](User_add/VALIDATION.md)。

### v1.1.2：NMI 补报与业务状态告警（2026-10-20）

提交：[3b4d181](https://github.com/QMAX-CYQ/recorder233/commit/3b4d181)。增加独立 NMI 保留记录和 1024 字节应急栈，捕获 RCC 时钟与 CPU 现场；NMI 捕获路径不调用 USB/RTOS/printf，等待已运行的 IWDG 复位后补报。

握手完成后先发四段 `[nmi]`，再发仍有效的三段 `[hardfault]`，最后恢复普通日志。补报入队后还需等待队列与在途发送排空，并确认会话未变化；断链则从第一段重发。记录、应急栈和链接保留区必须整体匹配；没有 IWDG 时不能保证自动重启补报。

当前包同时含 `status_log` 离线/恢复告警，默认启动宽限 5 秒、每模块限频 1 秒，需要显式接入业务状态。它不修改原控制输出或看门狗策略。详见 [NMI_REPORTING.md](User_add/NMI_REPORTING.md)、[STATUS_ALERTS.md](User_add/STATUS_ALERTS.md)。

### v2.0：新硬件板、LED 与电池测量（2026-10-24）

提交：[26d4f61](https://github.com/QMAX-CYQ/recorder233/commit/26d4f61a756ac4028c1a5a5be79ceb7f32b7c777)。CubeMX 增加 SPI1、ADC1、TIM2，调整 USART1 并移除 USART6；外设初始化不等于已实现对应业务协议。

- **SD 灯 PA0/TIM2_CH1：** `sd_state != FR_OK` 熄灭；FR_OK 且文件非 RS_READY 时满亮；RS_READY 时约 2 秒一次呼吸。运行中检测卡不可用也发布灭灯状态。
- **通信灯 PA1/TIM2_CH2：** LED 启动后未握手就绪时呼吸；APPLICATION_READY 且收到会话 ACK 后常亮；收到日志正文时以 2 Hz 闪烁，停止接收约 1 秒后回到常亮，断链恢复呼吸。引脚用途按当前源码核对，提交正文的顺序描述不作为引脚映射。
- **实现：** TIM2 时钟 84 MHz，PSC=83、ARR=999，PWM 1 kHz，CCR 范围 0～1000；默认任务每 10 ms 刷新，两灯独立计时，接收回调不负责灯效。`SD_led_control` 只发布模式，`led_control_update` 执行 SD PWM，`COMM_led_control` 使用累计日志数据包数识别新数据。
- **VBAT：** ADC1 内部 VBAT，12 位、21 MHz、480 Cycles，内部二分压；`Battery_ReadVoltage(vref_v, &voltage)` 测量后关闭 VBATE。`main` 当前仅上电读一次到 `battery_voltage`，参考值暂传 3.3 V。尚未自动校准 VREFINT、实现剩余寿命模型或自动更换阈值。主电源关闭时不能靠 VBAT 运行 ADC。
- **补丁工具：** 修复普通 PowerShell 找不到 Git 的使用问题，支持 PATH、常见安装目录、Codex 内置 Git，以及显式 `-GitPath`。Apply 前先完整预检，Check 不修改文件。

**验证：** 已有 `MDK-ARM/led_build.log` 记录 AC6.24 编译/链接 0 错误、0 警告。用户反馈上电 VBAT 约 3.241 V，但没有同步万用表参考值，不能视为校准结果或电池寿命结论。该构建记录不证明灯效、实际接线、SPI 业务或 USB 故障恢复已经完成硬件回归。

## 4. Keil5 Watch 变量表（当前 F405）

以下表达式按 v2.0 源码核对，保留 v0.9.1 的基础诊断并补充现行恢复和电池变量。`a / b` 表示两个独立变量或字段，添加 Watch 时分别输入；也可直接添加整个诊断结构后展开。

### 4.1 建议先添加的表达式

```text
Appli_state
isSdCardInit
pcfile_status
usb_rx_ring_free
usb_rx_ring_blocked_count
usb_rx_receive_fail_count
usb_rx_write_fail_count
max_lwrb_size
check_write_fail_count
g_usb_rx_diag
g_recorder_diag
g_log_host_ack_received
g_log_host_link_diag
g_rtc_read_diag
```

### 4.2 USB 接收与缓存状态

定义：`User/Task/usb_task.c`；结构声明：`usb_task.h`。

| 表达式 | 含义 / 判断 |
|---|---|
| `Appli_state` | USB Host 应用状态；稳定接收时为 `APPLICATION_READY`，不是文件就绪状态 |
| `usb_rx_callback_count` | 接收回调次数，含控制帧 / 无正文回调，不等于日志行数 |
| `usb_rx_zero_length_count` | 过滤后没有正文的回调数；ACK-only 回调也可计入，增加不必然异常 |
| `usb_rx_last_packet_len` | 最近控制帧过滤后的正文长度 |
| `usb_rx_receive_fail_count` / `usb_rx_last_receive_status` | 接收请求提交失败次数 / 最近 USBH 返回值；`USBH_OK=0`、`USBH_BUSY=1`、`USBH_FAIL=2` |
| `usb_rx_request_size_invalid` | 端点请求长度超过 512 的标志，非次数；正常为 0 |
| `usb_rx_ring_ready` / `usb_rx_ring_free` | 最近采样的 ring 初始化状态 / 剩余空间，单位字节 |
| `usb_rx_ring_blocked_count` | 进入等待缓存空间状态的次数，按等待段计，不是溢出字节或丢包数 |
| `usb_rx_write_fail_count` | 格式化完成后实际尝试入 ring 失败的次数；包保留重试，一包可计多次 |
| `usb_rx_written_packet_count` / `usb_rx_written_byte_count` | 已入 ring 的正文接收块数 / 含 RTC 前缀字节数；不是 SD 落盘量 |
| `g_usb_rx_diag.raw_bytes` | 控制帧过滤后、RTC 前缀插入前的正文总字节数 |
| `g_usb_rx_diag.complete_lines` / `tick_lines` / `non_tick_lines` | 换行完成的行数 / 成功解析 tick 的 INFO 行 / 其余行 |
| `g_usb_rx_diag.tick_gap_count` / `estimated_missing` | 相邻解析 tick >10 ms 的次数 / 间隔为 10 ms 整数倍时推算缺失；调度延迟也会造成跳变 |
| `g_usb_rx_diag.duplicate_tick` / `backward_tick` / `irregular_tick_gap` | 重复 / 倒退 / 非 10 ms 整数倍间隔；uint32 正常回绕按差值处理 |
| `g_usb_rx_diag.gap_history` / `gap_history_next` | 最近 8 处缺口的前后主板 tick、F405 tick、推算缺失数；下一槽为序号 `% 8` |
| `g_usb_rx_diag.line_bytes_pending` / `non_crlf_lines` | 尚未换行的行长 / 非 CRLF 结束行数；跨包半行可以是正常状态 |
| `g_usb_rx_diag.max_callback_gap_ms` / `max_task_gap_ms` | 非空正文回调最大间隔 / USB 任务最大运行间隔 |
| `g_usb_rx_diag.max_pending_ms` / `max_ready_hold_ms` / `max_ring_wait_ms` | 接收请求等待 / 收到正文待处理 / 等待 ring 的最长观察时间，单位 ms |
| `g_usb_rx_diag.min_ring_free` / `ring_samples` | 观察到的最低剩余空间 / 采样数；采样数为 0 时最低值无解释意义 |
| `g_usb_rx_diag.format_fail` | 时间戳格式化失败尝试次数；失败后可能继续尝试，并非独立坏包数 |
| `g_usb_rx_diag.oversize_callback` | 当前是在过滤并限制长度后检查，不能作为原始超长回调的可靠计数；为 0 不证明底层长度正常 |
| `g_usb_rx_diag.disconnect_transitions` / `discarded_ready_packets` | 任务观察到的断开转换 / 当时未处理的正文块数 |
| `g_usb_rx_diag.discarded_partial_line_bytes` | 断开时诊断解析器累计的未完整行长度，不是 ring 实际删除字节数 |

tick 解析仅适配 `INFO: [...] tick=数字 ` 或 `INFO: [...] t=数字 `，且标识需出现在保存的短行前缀内。普通日志不满足格式时，`tick_lines=0` 不表示接收失败。

### 4.3 SD 写入、错误与吞吐

定义：`User/Recorder/recorder.c`；声明：`recorder.h`。

| 表达式 | 含义 / 判断 |
|---|---|
| `isSdCardInit` / `g_recorder_diag.mount_result` | 最近挂载结果；`FR_OK=0` |
| `pcfile_status.state` | `RS_INIT=0`、`RS_READY=1`、`RS_CLOSE=2`、`RS_ERROR=3`；写错误计数增加不一定切到 RS_ERROR |
| `pcfile_status.current_index` / `oldest_index` / `current_filepath` | 当前 / 最老文件索引、当前文件名 |
| `max_lwrb_size` | 写入任务观察到的 ring 最大占用字节，不是全时段精确峰值 |
| `check_write_ok_count` | 一轮有完整数据写入后 `f_sync` 成功的次数，不是成功写块数 |
| `check_write_fail_count` | 数据写错误、短写或同步错误的合计次数，不含所有元数据操作 |
| `g_recorder_diag.ring_attempts` / `ring_accepted_bytes` | 有效参数入 ring 尝试次数 / 成功字节数，包含 RTC 前缀 |
| `g_recorder_diag.ring_rejected` / `ring_rejected_bytes` | 全量入 ring 被拒的尝试次数 / 每次拒绝长度累计；重试重复计数，不能当真实丢失量 |
| `g_recorder_diag.dequeued_bytes` | 已从 ring 取出的正文总字节数；不代表已成功写卡 |
| `g_recorder_diag.write_attempts` / `write_reported_bytes` | 数据 `f_write` 调用次数 / FatFs 报告写入字节数；不含文件头和索引 |
| `g_recorder_diag.write_error` / `write_short` | 返回非 FR_OK / 写入长度非 16384 的次数；同一次调用可同时增加两者 |
| `g_recorder_diag.unconfirmed_bytes` | 短写时请求长度减报告长度的累计值，不是独立的物理丢失检测 |
| `g_recorder_diag.last_write_result` / `last_write_bytes` | 最近数据写结果 / 报告长度；正常为 0 / 16384 |
| `g_recorder_diag.sync_attempts` / `sync_error` / `last_sync_result` | 正文批次同步尝试 / 同步错误 / 最近结果；正常最近结果 0 |
| `g_recorder_diag.write_active` / `last_write_tick` | 是否正在 `f_write` / 最近开始 tick |
| `g_recorder_diag.last_write_ms` / `max_write_ms` / `max_sync_ms` | 最近写耗时 / 最大写耗时 / 最大同步耗时，单位 ms |
| `g_recorder_diag.last_write_error_tick` / `last_sync_tick` | 最近数据写异常 tick / 同步开始 tick |
| `g_recorder_diag.card_not_ready` | SD 状态检查不为可传输状态的次数；忙状态也可能计入，不直接等于拔卡次数 |
| `cost_time` / `max_time` / `averange_time` | 有写入并同步的批次总耗时 / 最大值 / 平均值，单位 ms |
| `total_Mb` / `averange_speed` / `averange_cnt` | 完整块累计量（实际为 MiB）/ MiB/s / 统计批次数；零耗时与初期样本需谨慎解读 |

代码先出队再写盘，短写或错误没有把未确认数据重新放回 ring。因此缓存拒绝通常可以重试，写盘失败则已有数据丢失风险。即便 `f_write` 返回成功，也应结合 `f_sync` 与实际文件验证。

### 4.4 握手状态

| 表达式 | 含义 / 判断 |
|---|---|
| `g_log_host_ack_received` | 已收到当前会话有效 ACK，正常进入记录后为 1 |
| `g_log_host_link_diag.session` | 当前会话号，应与配套主板一致 |
| `g_log_host_link_diag.ready_attempts` / `ready_started` / `ready_completed` | READY 提交尝试 / USBH 接受 / 完成回调次数 |
| `g_log_host_link_diag.ack_received` | 每会话首次匹配 ACK 的累计次数，重复 ACK 不重复增加 |
| `g_log_host_link_diag.tx_failed` | READY 提交失败次数 |
| `g_log_host_link_diag.discarded_before_ready` | 握手前丢弃的非控制数据字节数；重连旧数据可能使其增长 |
| `recorder_log_ready` | `recorder.c` 内静态标志；当前文件头同步完成后为 1 |

`RecorderLogIsReady()` 是函数接口，建议观察内部 RAM 标志，不在运行中依赖 Watch 调用函数。READY 已提交而 completed 长期不增，检查 USB OUT 完成链路；文件未就绪时不会发送 READY。

### 4.5 RTC 与文件时间

定义 / 声明：`User/Recorder/calendar.c/.h`。

| 表达式 | 含义 / 判断 |
|---|---|
| `g_rtc_read_diag.snapshot_calls` / `snapshot_ok` | 快照请求 / 完整校验通过次数 |
| `g_rtc_read_diag.rtc_uninitialized` / `read_error` | RTC 未初始化 / HAL 读取失败次数 |
| `g_rtc_read_diag.invalid_date` / `invalid_time` | 日期或时分秒不合法次数 |
| `g_rtc_read_diag.invalid_argument` / `rtc_disabled` | 参数错误 / 编译禁用 RTC 的调用次数 |
| `g_rtc_read_diag.fattime_fallback_count` | FAT 时间使用备用日期次数，正常不增加 |
| `g_rtc_read_diag.calendar_fallback_count` | 日志或文件名日历使用备用日期次数，正常不增加 |
| `g_rtc_read_diag.last_result` | 0 成功、1 未初始化、2 HAL 错误、3 日期无效、4 时间无效、5 参数无效、6 RTC 禁用 |

统计合法不证明 RTC 正在走时；时钟停走但字段合法不会触发这些错误。需要对照实际时间变化与 LSE/VBAT 状态。

### 4.6 历史变量与静态变量

- `check_buffer_overflow_count`、`check_buffer_overflow_bytes`、`check_buffer_max_used` 曾在 v0.1 之前存在，v0.1 删除，当前 F405 无此变量。接收端当前也没有独立、精确的“ring 实际丢失条数”计数器。
- `usb_rx_pending`、`usb_rx_ready`、`usb_rx_len`、`usb_rx_formatted_ready`、`recorder_log_ready`、`g_create_seq` 是静态符号。若 Keil Watch 无法解析裸名，从 Symbols 定位并使用其生成的作用域表达式；编译优化可能使部分符号不可见。
- `prv_us_time.overflow_cnt` 属于辅助定时器溢出计时，不是日志缓存溢出统计，不应用它判断日志丢失。

### 4.7 当前恢复与电池变量

| 表达式 | 含义 / 使用边界 |
|---|---|
| `battery_voltage` | 上电读取的电池电压，单位 V；非持续更新，不直接表示剩余容量 |
| `g_usb_rx_timeout_diag.timeout_count` / `active` | 超时累计次数 / 当前请求是否处于已记录的超时状态 |
| `g_usb_rx_timeout_diag.first` / `history` | 首次现场 / 最近四次现场；valid 有效时读取 |
| `g_usb_rx_timeout_diag.current_pending_ms` | 当前待完成接收等待时间；正常静默也会等待 |
| `g_usb_host_recovery_diag.state` | IDLE、REQUESTED、RUNNING、WAIT_LINK 或 FAILED |
| `g_usb_host_recovery_diag.requests` / `attempts` | 已提出请求 / Host 线程执行恢复次数 |
| `g_usb_host_recovery_diag.core_restarted` / `restored` | 内核接口成功 / 重新就绪、ACK 与新回调满足恢复条件 |
| `g_usb_host_recovery_diag.failed` / `last_status` | 接口失败次数 / 阶段与返回值编码 |
| `g_usb_host_recovery_diag.discarded_ready_bytes` | 恢复时丢弃的应用暂存字节，不含全部 USB 在途数据 |
| `g_usb_rx_diag.callbacks_with_data` | 过滤后含日志正文的回调累计数，也是通信灯活动输入 |

`led_control.c` 的模式与计时变量为静态内部实现，通常观察 CCR1、CCR2 及上述输入即可。历史诊断中的 `g_usb_stop_first_failure` 和 HAL 跟踪环已移除，不应作为现行固件必需 Watch 项。

## 5. 配套主板 Watch（另一个工程）

下表保留来自 `D:/RM/rm_main_gimbal/` 的发送端诊断；本仓库 `User_add` 使用 `g_log_task_diag`，不能直接套用表中的 `g_debug_task_diag`。支持包还可观察 `g_fault_record`、`g_nmi_record`、`g_nmi_log_diag`、`g_fault_log_diag`、`g_log_event_diag`、`g_status_log_recovery_coalesced` 及 `g_status_log_invalid_module`，接入与字段含义以包内 README 为准。F405 不具有这些主板符号。

| 表达式 | 含义 / 判断 |
|---|---|
| `g_log_link_ready` / `g_log_link_diag.session` | 主板已收到 READY 并排队 ACK / 会话号 |
| `g_log_link_diag.ready_rx` / `ack_queued` / `ack_failed` / `waiting_loops` | READY 次数、ACK 入队成功 / 失败、等待循环次数 |
| `cdc_tx_pending_count` / `cdc_tx_inflight` | 尚未启动的队列项数 / 在途状态；容量 32，在途项已从 pending 移出 |
| `cdc_tx_queue_overflow_count` | 队列满导致拒绝的调用次数；日志缺失的重要证据，包含所有 CDC 调用方 |
| `cdc_tx_start_error_count` | 队首启动失败次数；失败保留队首重试，不等于已经丢失 |
| `cdc_tx_queued_count` / `started_count` / `complete_count` | 在 `cdc_tx_` 前缀下分别为入队 / 启动 / 完成累计次数 |
| `g_cdc_tx_diag.queue_high_water` / `buffered` | 待发队列峰值 / 需要缓冲的调用次数 |
| `g_cdc_tx_diag.attempts` / `accepted` / `busy` / `invalid` | 所有 CDC 调用的尝试 / 入队接受 / 队列满 / 参数错误 |
| `g_cdc_tx_diag.started` / `completed` / `start_failed` | USB 启动 / 完成 / 启动失败次数；accepted 只证明入队 |
| `g_cdc_tx_diag.accepted_bytes` / `completed_bytes` | 原始发送字节；包括 ACK 和其他 CDC 业务，不含 F405 RTC 前缀 |
| `g_cdc_tx_diag.disconnect_lost` / `disconnect_lost_bytes` | 断开时在途但未完成的记录 / 长度；可能部分已送达，不是精确物理丢失量 |
| `g_log_tx_diag.attempts` / `accepted` / `busy` / `failed` | 仅 `sys_printf` 日志尝试 / 入队成功 / 满队列 / 其他失败 |
| `g_log_tx_diag.format_error` / `format_truncated` / `max_line_bytes` | 格式化错误 / 截断 / 最大行长；格式缓冲当前 768 字节 |
| `g_log_tx_diag.failure_streak` / `max_failure_streak` / `failure_tick` / `failure_status` | 当前 / 最大连续失败及最近 8 次失败历史 |
| `g_debug_task_diag.max_gap_ms` / `late_count` / `catchup_count` | 任务最大间隔 / 超过 10 ms / 小于 10 ms 的次数 |
| `g_debug_task_diag.max_run_ms` / `overrun_count` / `group_count` | 最大执行耗时 / 超过周期次数 / 10 个通道累计次数 |

Watch 中必须写完整字段，例如 `cdc_tx_started_count`，不能直接输入表中为节省宽度省略前缀的 `started_count`。

## 6. 常见现象与定位顺序

| 现象 | 优先证据 | 处理方向 / 当前方案 |
|---|---|---|
| 启动日志缺一段 | 主板 overflow 增长、峰值 32，seq 缺口 | 两端烧录配套握手固件，确认 READY→ACK→生成顺序 |
| 文件已创建但无正文 | 文件就绪标志、READY started/completed、ACK 标志 | 按 SD 就绪、接收提交、READY 完成、主板 ACK 入队逐步定位 |
| ring 等待变长 | free 接近 0、blocked 增长、max_write_ms/max_sync_ms 大 | 检查卡写延迟与产生日志速率；同时检查主板满队列拒绝 |
| 接收似乎停住 | pending 等待时间持续升高、回调停止 | 排查 USB 完成路径；确认包含 v0.8 提交前置 pending 修复 |
| 文件中间缺记录 | seq 缺口、发送 BUSY、接收 tick 缺口、写卡错误 | 对照两端同窗口计数，区分未生成、发送拒绝、接收遗漏与写失败 |
| tick 间隔异常但 seq 连续 | catchup / late 与间隔一致 | 检查调度；握手后重新对齐主板周期，勿直接当作丢包 |
| 文件尾半行 / 少一小段 | ring 余量不足 16 KiB、末尾残行 | 当前只写整块且无完整停机尾刷；不能用正文中段连续性证明尾部全部保存 |
| 索引显示旧字符或 NUL | 直接查看三个元数据文件字节 | v0.9.1 截短重建、只写文本、读取兼容；不需删除所有日志 |
| 文件修改日期异常 | RTC fallback 计数、get_fattime、同步状态 | v0.9.1 接入 FAT 时间；校准 RTC，核查同步和 RTC 走时 |

## 7. 已有验证与后续验证边界

### 7.1 仓库现有报告

| 资料 | 结论与范围 |
|---|---|
| [Rb91～Rb160](recorder_f405/diagnostics/Rb91_Rb160_review.md) | 约 3 h 15 min，1,172,983 条完整记录，缺 144 条，全部集中于启动；队列溢出 144，后续 1,172,951 条连续 |
| [Rb162](recorder_f405/diagnostics/Rb162_review.md) | 握手后 3496 条 seq 连续；启动 tick 230→231→240，短暂追赶 |
| [Rb163](recorder_f405/diagnostics/Rb163_review.md) | 主板 tick 对齐后 9315 条连续；10 ms 记录周期正常；尾部 180 字节残行单列 |
| [Rb164～Rb167](recorder_f405/diagnostics/Rb164_Rb167_review.md) | 55,850 条完整记录，覆盖 558.49 s；seq / 周期无异常，队列峰值 8/32、溢出 0；尾部 189 字节残行单列 |
| [握手实现说明](recorder_f405/diagnostics/ready_link/README.md) | 记载两端独立 V6.22 工程 0 错误 / 0 警告，以及拆包、校验、旧会话、队列满等原生 C 测试 |
| [RTC 快照说明](recorder_f405/diagnostics/rtc_snapshot/README.md) | 记载独立编译及日期边界、IRQ 恢复、HAL 失败、备用计数测试；说明当时尚未烧录或实测跨午夜 |
| [索引文本修复说明](recorder_f405/diagnostics/metadata_fix/README.md) | 记载独立编译、旧尾部复现、截短、旧格式、uint32 边界、短写与关闭失败测试；当时尚未实卡复测 |

以上是历史资料结论，不是本次文档整理重新执行的编译或实机测试。部分实现说明记录“尚未烧录”，后续压力报告属于另外的测试时点；每项硬件行为须按对应报告范围解读。

### 7.2 使用 Watch 的观察方法

1. 使用对应固件的调试符号，复位后等连接和握手稳定，记录两端初始快照及当前文件名。
2. 连续运行 30～60 s 后记录结束快照，比较计数增量；长测追加跨文件边界、SD 高延迟、两端不同上电顺序与重连条件。
3. 尽量运行中观察，断点停机会扰动 USB 和任务时序。统计结构不是整体原子快照，短暂不平衡不等于丢失。
4. uint32 计数自然回绕，复位后重新累计；不要运行中手工清零。两块板 tick 起点不同，按缺口与失败历史对应事件。
5. 不能直接比较 USB 包数与日志条数；主板 CDC 含 ACK / 其他业务，F405 raw_bytes 已过滤控制帧，ring 与落盘字节又含 RTC 前缀。文件头与索引不计入正文写入诊断。
6. 验证当前仍存在的边界：不足 16 KiB 的尾刷、短写数据恢复、SD 移除后恢复、断电时元数据一致性、RTC 跨午夜与实际走时。这些尚不能由“错误计数为 0”排除。

### 7.3 v1.x / v2.0 验证范围

后续主板移植与测试资料见 [User_add/VALIDATION.md](User_add/VALIDATION.md)；USB Host 恢复历史资料见 [受控恢复记录](recorder_f405/diagnostics/usb_host_recovery/README.md)。这些资料保留各自时点的源码和符号，部分已移除的跟踪接口不应重新加入当前 Watch 清单。

本次仅更新文档并检查本地链接、参数及版本基线，未重新执行固件编译、烧录、异常注入或电池测量。已有构建与模拟测试结论应按原报告范围解读；重点仍需验证新板灯效、万用表对照、重连恢复、长时间记录和掉电边界。

## 8. 后续版本记录模板

```markdown
### vX.Y：修改主题（YYYY-MM-DD）

- 提交：SHA / GitHub 链接。
- 适用工程与关键文件：发送端 / 接收端，以及改动入口。
- Bug 现象：触发条件、实际表现、期望行为，附日志 / Watch 快照。
- 根因与证据：已证实原因；尚未确认部分单独注明。
- 解决方案：具体状态、缓存或接口行为如何改变。
- Watch 变化：新增 / 删除变量、单位、计数时机、是否重试累计。
- 验证：编译环境、用例、实机时长、缺失数、错误计数、文件边界。
- 剩余限制：未覆盖条件与尚未修复的问题。
```
