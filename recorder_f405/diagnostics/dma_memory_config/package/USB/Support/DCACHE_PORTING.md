> 新版条件化内存布局以 [DMA_MEMORY.md](DMA_MEMORY.md) 为准；不要保留旧的强制开启 USB DMA 赋值。

# H723 D-Cache / USB DMA 移植说明

本补丁提取自 2026-10-18 轮腿主板的修复，不包含该工程的业务逻辑。
适用 STM32H723 + Cortex-M7 + ArmClang + USB Device CDC HS IP（内置 FS PHY）。
F405 是记录器接收端，不应编译本主板支持包。USB FS IP、其他 MCU、其他
链接工具链或不同 USB 复合类必须重新核对符号、RAM 可达性与容量。

## 为什么不能只给 CDC TX 加 Clean

USB DMA 不与 CPU D-Cache 自动保持一致。枚举就要 DMA 访问 SETUP、EP0
描述符和控制数据；仅在日志发送处 Clean 无法修复枚举和接收。
本包采用专用 MPU Normal 非缓存、非缓冲、禁止执行区，保留其他 RAM 的缓存。
构建前按 [DMA_MEMORY.md](DMA_MEMORY.md) 从 .ioc 同步配置。
只有 D-Cache 和 USB DMA 同时开启才保留非缓存区并配置 MPU Region 7。
只有 USB DMA 开启时仍将 DMA 对象放在 AXI SRAM，但不配置 MPU 区。
不要在 USB 运行中动态切换 D-Cache 或复用 Region 7。

## 文件和内存布局

- `usb_dma_memory.h`：C 和 scatter 共用区域常量。
- `usb_dma_port.h`：包含在已有 USER CODE 0 内的 MPU 配置和本文件局部 HAL
  收发包装器。包装器在提交 DMA 前执行 DSB；宏别名必须在包装器定义之后。
  该头文件不是独立编译单元，不加入 Keil C 文件列表，不放入全局公共头文件。
- `fault_noinit.sct`：USB 区 `0x24048000..0x2404BFFF`，16 KB；普通 AXI RAM
  截止 `0x24048000`。故障区仍按 `FAULT_LOG_TEST_IRAM1` 选择 DTCM 或 AXI 顶端。
- USB 区收集 `usbd_conf.o`、`usb_device.o`、`usbd_desc.o`、`usbd_cdc.o` 的 RW/ZI，
  以及 `usbd_cdc_if.o` 中的 UserRxBufferHS/UserTxBufferHS。因此包含 PCD Setup、
  EP0 句柄字段、动态生成字符串、CDC 控制缓冲和各描述符。日志队列是 CPU
  数据，留在普通 RAM，发送前复制到专用 TX 缓冲。没有把所有业务 RAM 禁用缓存。
- 本方案无需在 USB 每包收发时 Clean/Invalidate。地址、尺寸、MPU 编号和
  scatter 必须一致；修改本头文件后 Rebuild，并检查最终 MAP。

## 现有 USER CODE 钩子：必须完成以下接入

将本套 Log/Support/Task/Usb_device 加入 Include Path；CubeMX 开启 Keep User Code。USB DMA 开关由 .ioc 决定，须先接入 DMA_MEMORY.md 中的构建前同步命令。
下列片段均合入已有同名钩子，不创建自定义钩子名，不整体替换用户 usbd_conf.c。

`USB_DEVICE/Target/usbd_conf.c`，`USER CODE BEGIN PV`：

```c
extern PCD_HandleTypeDef hpcd_USB_OTG_HS __attribute__((aligned(32)));
```

`USER CODE BEGIN 0`：

```c
#include "usb_dma_port.h"
```

`HAL_PCD_MspInit()` 的 `USER CODE BEGIN USB_OTG_HS_MspInit 0`：

```c
USB_DMA_ConfigMemory();
```

该钩子位于 HAL 初始化 USB 内核和启用 DMA 之前；若生成代码的形参名不同，
相应修改 `pcdHandle`。不要放到 FIFO 配置或初始化后的钩子。
`usb_dma_port.h` 的 MPU 函数会同步属性转换前已有的缓存写入，保留原 MPU
控制状态和中断屏蔽状态。Region 7 已占用时调用用户 `Error_Handler()`。

随包 `usbd_cdc_if.c` 已在 `USER CODE BEGIN PV` 中加入：

```c
extern uint8_t UserRxBufferHS[APP_RX_DATA_SIZE] __attribute__((aligned(32)));
extern uint8_t UserTxBufferHS[APP_TX_DATA_SIZE] __attribute__((aligned(32)));
```

实际数组定义保持 CubeMX 生成形式，ArmClang 合并同一对象声明的对齐要求。
不要另外定义第二套数组。PCD 句柄同理。CDC 类静态分配保留生成器默认的
uint32_t 对齐；位于非缓存区，不需要依赖逐缓存行失效。

Keil 关闭自动 scatter 布局，选用本包 `fault_noinit.sct`。不要只复制 USB
头文件而沿用旧 scatter。只有受保护区开启时，头文件才引用 Image$$RW_USB_DMA$$Base/Limit；
重新生成若丢失自定义 scatter，应当链接失败，而不是生成错误的 DMA 固件。
如果生成器改变对象名、数组段名或 USB 类，需同步修改 scatter 选择器。

CubeMX 再生成后检查：USB_DMA_ConfigMemory 的钩子、两个对齐声明、头文件
引用、自定义 scatter 和 Include Path。本包没有自动运行 CubeMX 或覆盖工程。

## 其他 UART/SPI DMA 的可选辅助函数

`dcache_handle.c/h` 提供 CleanDCacheForDmaTx 和 InvalidateDCacheForDmaRx，
只在 D-Cache 开启、地址非空、长度非零且范围可表示时执行缓存维护。
关闭 D-Cache 后不会再写 DCCMVAC/DCIMVAC。原工程已有同名函数时替换或合并，
不要同时编译两套定义。这些是可选辅助函数，USB 非缓存区方案不依赖它们。

- 可缓存 TX：CPU 写完 -> Clean -> 启动 DMA -> DMA 完成后允许重写。
- 可缓存 RX：接收区应独占完整的 32 字节缓存行。交给 DMA 前按所有权处理
  原缓存，完成后 Invalidate，再交给 CPU。DMA 运行时 CPU 不应修改接收区。
- 接收完成时不能 Clean，否则可能把旧缓存写回覆盖 DMA 新数据。
- 辅助函数向外对齐不等于数组自动隔离。未对齐、与其他变量共享缓存行、
  循环 DMA 半区或正在传输的区间必须按业务重新设计，不能只复制函数调用。
- 本函数不是任意地址合法性检查器，不要把外设地址、无效指针作为缓存范围。

## 故障与验收

本次主板曾在 D-Cache 关闭后、裁判串口缓存清理路径进入 HardFault；现场
PC 落于写 DCCMVAC，CFSR=0x400。不能据此断言所有关闭缓存时的维护调用
必然产生故障；修复采取关闭时跳过无必要维护，避免依赖该路径。

IWDG 必须初始化后才喂狗。调试时禁用初始化，也要禁用喂狗或检查
`hiwdg1.Instance == IWDG1`。故障捕获函数是记录点，不代表原始故障点；
非精确总线异常的 PC 可能晚于真正出错访问。

默认故障保留区为 DTCM，不受 D-Cache 影响。若改为 AXI 保留区，USB 的
Region 7 不覆盖 `0x2404F000`；必须另外为保留区配置非缓存属性或完整缓存
维护。USB 修复不等于所有 UART、SPI 和 fault 保留内存均已完成缓存适配。

验收分别测试 D-Cache 开/关 + USB DMA 开：检查 HOST_CLASS、READY/ACK、
持续计数、队列溢出/格式错误、故障寄存器。开启缓存时检查 Region 7 属性和
MAP 中全部 USB DMA 对象；不能只确认 UserTxBufferHS。测试期间不暂停或单步。
日志文件末尾半行与中途缺号要分开判断，末尾问题需检查停止/关闭/落盘流程。
