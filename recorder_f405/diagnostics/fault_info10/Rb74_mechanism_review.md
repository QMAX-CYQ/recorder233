# Rb74 全链路复核

2026-10-16。只审查，未修改固件源码。复核当前主板源码、scatter、MAP、调用栈报告、
本目录固件 AXF 的反汇编以及用户 Rb74。

## 本次事实

Rb74 第1028-1029行：IWDG复位标记04420000、pending=1、count=1、exception=3、
fault_tick=10006、PC0800D41C、CFSR00010000、HFSR40000000。
capture_valid=1、capture_stage=4、mask=0F。
magic=0，stored_hash=calc_hash=60B78B57，失败位图只有bit0。
boot_invalid=1、replay_pending=0；文件无ERROR。

ch=9状态第一次变化：

| 状态 | 首个INFO tick | 当前magic/hash | 当前pending/补报pending | 旧magic/hash/重算 | 失败位图 | reports/retries/overflow |
|---|---:|---|---|---|---|---|
| 冷启动 | 326 | 48464C47/DCBC3CF8 | 0/0 | 0/0/0，raw_valid=0 | 0 | 0/0/0 |
| 故障复位后 | 3258 | 48464C47/DCBC3CF8 | 0/0 | 0/60B78B57/60B78B57，raw_valid=1 | 1 | 0/0/0 |

两段INFO属于不同启动，tick不能跨复位相减。fault_boot快照和INFO字段相互吻合。
后续有效当前magic不是保留故障被补报成功，而是fault_empty生成的有效空记录。
原始记录仍在普通RAM启动快照中，下一次主板复位会被新快照代替。

## 完整机制

1. debug_task心跳后调用FaultLog_TestService。自动注入要求tick>=10000、链路ready、
   无待补报、一次性noinit标记未触发。手动request=1启动10秒倒计时，可重复触发。
   注入先保存双字一次性标记，清SHCSR.USGFAULTENA，执行UDF。
2. UDF升级HardFault。AXF向量0800000C保存0800CB89，指向fault_log的真实处理入口。
   stm32h7xx_it.c里的兜底死循环已通过宏重命名；不是本次实际入口。
3. naked入口先读取原MSP、PSP和EXC_RETURN，然后MSP切到2001F4B8，减48字节，
   保存R4-R11及PRIMASK/CONTROL/BASEPRI/FAULTMASK，CPSID i后跳FaultLog_Capture。
   不是异常中调用RTOS或printf。裸入口之前的硬件压栈仍依赖原异常栈；不能救回所有栈故障。
4. Capture阶段1后先将记录置零（因此magic/checksum起初为零），写版本/长度/pending、
   异常状态、原栈指针和通过边界检查的8个核心帧字。若栈错误标记存在则不读帧。
   当前EXC_RETURN=FFFFFFED时选择PSP，检查104字节扩展帧范围，核心字仍自SP读取。
5. 阶段2后进入capture_commit，阶段3，计算32位FNV风格hash（不包含magic及checksum），
   写checksum，DSB，阶段4，写magic=48464C47，DSB。没有阶段5。
   当前AXF：magic STR在0800CB78，后续DSB在0800CB7A，退出位置0800CB7E。
   上轮四阶段固件旧地址0800CAC8不能沿用于本次INFO10固件。
6. Capture最后停留NOP循环，不喂狗。IWDG由status_task初始化并在正常任务健康时刷新，
   参数Prescaler32/Reload1000。故障后剩余超时取决于上次实际刷新，不能只凭复位结果
   断定校验计算耗尽了时间。PRIMASK不能阻止NMI或硬件复位。
7. Reset_Handler先SystemInit再__main，之后main最早执行FaultLog_BootInit。
   boot快照在这一时点保存旧记录及判定；它不是Reset_Handler第一条指令处的快照。
   冷启动先写空记录避免探测未初始化ECC；热启动验证magic/version/size/pending/hash。
   校验失败会fault_empty清除原记录，形成有效空记录。只有校验通过且pending=1才复制
   到replay_record、设置补报pending。
8. debug_task等待新会话握手，先发两段fault_boot，再FaultLog_Service逐段发3条ERROR。
   BUSY不推进分段；会话或DeInit变化则从第1段重发；INFO在等待期间暂停。
   三段入队后等队列空、无inflight且会话未变，才清原pending并重新计算hash、reports++。
   此条件不包含F405的SD落盘确认，不是端到端持久化保证。

## 链接、栈及指令核对

- RW_FAULT_NOINIT：2001F000起、4KB UNINIT，实际占4D0。
- g_fault_record：2001F000，180字节。
- 应急栈：2001F0B8起1024字节，向下生长；阶段区在栈顶以上2001F4B8，不重叠。
- 阶段区12字节；一次性测试标记2001F4C8。
- 调用栈报告Capture最大88字节，加手写48字节保存共136字节；未显示1KB应急栈不足。
  静态报告不涵盖所有嵌套异常，也不证明运行中不存在越界写。
- 编译器没有省略checksum/magic写入；实际使用32位STR，顺序与源码一致。
- 对Core/User源码检索，发现的显式记录写入者为捕获、空记录初始化、补报清除；
  未发现第二个普通业务直接写入者。无法据此排除越界写、调试器操作或总线问题。
- 本次没有证据证明noinit被普通ZI整体清零；否则其余字段和阶段保留需要另作解释。
  但失败这一轮尚未拿到SystemInit前数据，不能排除启动阶段局部改写。

## 机制中明确的边界

1. 有效记录只在magic最后提交后成立。checksum正确但magic=0仍应判无效；
   不建议直接放宽magic检查当成已完成记录，否则半提交记录也被当成正式故障。
2. 无效现场被初始化清除是当前策略。快照虽保留原始字，但只在普通RAM跨当前启动有效。
   如需后续多次复位仍保留，应另设带状态的隔离记录；此审查未改动该策略。
3. 新故障捕获会覆盖单槽旧现场；next_fault_count在无效记录清空后可能重回1，
   id不是跨所有失败/冷启动全局唯一编号。
4. USB ACK入队与实际收到、主板发送完成与SD持久化是不同事件。
   队列满返回BUSY不覆盖旧FIFO项，但握手前旧数据、在途断开及接收端截断仍有独立风险。
5. 100ms INFO不能观察HardFault内提交的指令级变化，因为调度器此时已经停止。
6. 原生测试验证C状态机、边界判断及格式，不能模拟M7、DTCM、IWDG、调试器或总线真实时序。

## 结论与下一证据

Rb74失败点是“提交magic状态在启动校验时无效”，不是ERROR已经发送后文件丢失。
数据证明checksum写入内容被保留、阶段4写入被保留；不证明阶段4函数返回，也不证明
magic从未执行。此前断点单步成功只能证明那一次路径成功，不证明全速失败的原因。
五阶段能出ERROR尚非稳定修复；诊断代码改变函数调用、栈、指令布局和执行时序，
不能仅凭最后阶段号判定DSB死锁或看门狗在两条指令间到期。

优先在一次真实失败中取得：
- 故障处理期间不设执行断点、不单步；Reset_Handler入口读magic/hash/阶段。
- 同一轮继续到BootInit校验前对照。
- 如reset入口已坏，下一步可用对2001F000的条件数据写断点区分HardFault期间
  是否执行过提交magic的store；它应在写入之后触发，避免事先暂停改变待观察路径。
  仍需考虑硬件调试本身影响，且区分捕获清零、正常空记录初始化和真正提交。

架构复核参考：ST PM0253异常栈及屏障说明；ST ES0491列出的cache相关条件不能仅凭
“加了DSB表现改变”就认定匹配。本次未确认具体芯片勘误为根因。
https://www.st.com/resource/en/programming_manual/pm0253-stm32f7-series-and-stm32h7-series-cortexm7-processor-programming-manual-stmicroelectronics.pdf
https://www.st.com/resource/en/errata_sheet/es0491-stm32h72xx73xx-device-errata-stmicroelectronics.pdf
