# 按 CubeMX 配置选择 USB DMA 内存布局

两套 `Log/fault_noinit.sct` 和 `Usb_device/usb_dma_port.h` 共用
`Support/dma_memory_config.h`，只在以下条件成立时启用 `RW_USB_DMA`：

```c
DMA_MEMORY_DCACHE_ENABLED && DMA_MEMORY_USB_DMA_ENABLED
```

| D-Cache | USB Device DMA | 链接布局 |
| --- | --- | --- |
| 关闭 | 关闭 | 不创建 `RW_USB_DMA` |
| 关闭 | 开启 | 不创建 `RW_USB_DMA`，USB DMA 对象明确放入普通 AXI SRAM |
| 开启 | 关闭 | 不创建 `RW_USB_DMA` |
| 开启 | 开启 | 创建 16 KB `RW_USB_DMA`，启动 USB 前配置 MPU Region 7 为非缓存 |

故障 `RW_FAULT_NOINIT` 始终保留；SPI 版本的 `RW_LOG_SPI_DMA` 也始终保留，
SPI 暂存区继续使用发送前 Clean 的缓存维护方式。

## 必须配置构建前同步

链接脚本在编译链接时执行，不能读取运行时 SCB 寄存器，也不能直接读取
C 源码中的 `SCB_EnableDCache()` 或 `hpcd.Init.dma_enable` 赋值。
`sync_dma_memory.ps1` 从指定 `.ioc` 读取：

- `CORTEX_M7.CPU_DCache`；
- 当前 `USB_OTG_HS.VirtualMode-*` 对应的 `dma_enable-*`。

未显式记录的开关按 CubeMX 默认关闭处理；未知值、重复项或不支持的 USB
模式会终止同步。修改 CubeMX 配置后先生成代码，再 Rebuild，避免 `.ioc`
与实际 C 初始化不一致。默认适配 USB_OTG_HS Device，其他模式需重新核对。

在 Keil 的 **Options for Target → User → Before Build/Rebuild** 启用一条
用户命令，填写实际路径，例如：

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "D:\RM\recorder233\User_add\SPI\Support\sync_dma_memory.ps1" -IocPath "D:\你的主板工程\rm_main.ioc"
```

使用 USB 日志包时将路径中的 `SPI` 换为 `USB`。脚本默认在自身所在目录
更新 `dma_memory_config.h`，不修改 `.ioc`，配置未变时不重复写文件。
将这条命令的 **Stop on Exit Code** 设为 **>=1**，不要保留默认 Not Specified；脚本执行失败必须停止构建。支持包随附头文件保守设置两项为 1；
它不是目标工程的自动检测结果，只有接入此构建前命令后才会随 `.ioc` 自动更新。
不要仅配置 C 的 `-D` 宏，链接脚本预处理不会自动继承这些参数。

## 同步代码与链接脚本

保留 `Log/Support/Task/Usb_device` 目录结构并加入对应 Include Path。
Keil Linker 关闭自动内存布局，选用本套 `Log/fault_noinit.sct`。
仅替换 `.sct`、却继续使用旧版无条件引用链接符号的 USB MPU 代码，仍会
出现 `Image$$RW_USB_DMA$$Base/Limit` 未定义。

在 `usbd_conf.c` 的 `USER CODE BEGIN 0` 引入本套：

```c
#include "usb_dma_port.h"
```

删除旧的内联 `usb_dma_region_start/limit`、`USB_DMA_ConfigMemory()` 和 HAL
发送包装实现，防止旧代码继续无条件引用专用区或产生重复定义。
在 `HAL_PCD_MspInit()` 的 `USB_OTG_HS_MspInit 0` 中保留：

```c
USB_DMA_ConfigMemory();
```

删除历史补丁中强制 `pcdHandle->Init.dma_enable = ENABLE;` 的语句，
让 CubeMX 根据 `.ioc` 生成实际开关。不要在 USB 启动后动态开启 D-Cache。
当生成配置认为缓存关闭、但运行时缓存已开启且 USB DMA 开启时，辅助函数
会调用 `Error_Handler()`，防止在缺少非缓存区时继续启动 DMA。

SPI 日志包的 `Usb_device/usb_dma_port.h` 仅供主板仍使用其他 USB Device
业务时接入，纯 SPI 工程无需调用或编译 USB 初始化。
若没有 USB Device，请在 `.ioc` 中关闭相应外设后同步配置。

仍保留 PCD、RX、TX 的 32 字节对齐声明。PCD SETUP、控制缓冲区、描述符及
CDC RX/TX 都属于 USB DMA 数据，不能只对日志 TX 做缓存处理。
修改类库、对象名或缓冲区段名时，须同步核对 scatter 选择器。

构建后检查 MAP 中区域条件及地址。即使关闭 D-Cache，只要启用 USB DMA，
这些对象仍必须位于 DMA 可访问的 AXI SRAM，不能放入 DTCM。
实机枚举、收发、故障复位和补报还需按目标工程验证。
