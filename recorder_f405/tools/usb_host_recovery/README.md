# USB Host 受控恢复补丁

恢复主体在自建的 `User/Comm/usb_comm.c/h`，由通用 `User/Task/comm_task.c` 经通信函数指针调度。这里保存必须修改 ST USB Host 核心库的最小补丁，不依赖 HAL 诊断钩子。

USB 后端当前保留超时快照、历史及接收诊断统计；HAL 跟踪环和首次停止快照已移除。接收、时间戳、缓冲区背压、READY/ACK及受控恢复仍保留。通信选择和调用顺序见 [通信接口说明](../../User/Comm/README.md)。

补丁包含：

- `USBH_ResetStateForRecovery`：释放旧类并重建 Host 状态，保留原线程、队列和注册类。
- `USBH_UserRecoveryService` 弱入口与接口声明；应用提供实际实现。
- 两种 CMSIS-OS 分支的 Host 循环先调用恢复服务，确保恢复和枚举/类处理在同一线程串行执行。

CubeMX 重新生成代码或更新固件库后，在工程目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\usb_host_recovery\Apply-RecoveryPatch.ps1
```

检查补丁是否仍存在（不修改文件）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\usb_host_recovery\Apply-RecoveryPatch.ps1 -Mode Check
```

默认操作是 Apply；已完整应用时重复执行不会修改文件。Check 在缺失、不完整或不匹配时返回非零，适合用于构建前检查。脚本必须能访问 Git，工程可位于仓库根目录或仓库子目录。

如果出现“无法将 git 项识别为 cmdlet”等错误，表示当前 PowerShell 找不到 Git，与 CubeMX 的 PWM 配置无关。脚本会依次查找 PATH、Git for Windows 常见安装目录以及当前用户的 Codex 内置 Git。也可以显式指定实际的 git.exe 路径：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\usb_host_recovery\Apply-RecoveryPatch.ps1 -GitPath "C:\Program Files\Git\cmd\git.exe"
```

上面的路径仅为示例，必须对应已存在的文件。若没有安装 Git，可安装 Git for Windows 并启用命令行 PATH，随后关闭并重新打开 PowerShell，再执行默认命令。

脚本先检查两份文件是否均可应用，再执行；不使用部分应用或强制覆盖。如果更换库版本后上下文不匹配，会停止且不修改文件，需要核对新版本的状态机、类释放和线程接口并更新补丁。上下文匹配也不能代替升级后的编译和硬件恢复测试。

移植到其他工程时，将此目录、应用恢复实现及依赖一并迁移；用 `-ProjectRoot <目标工程目录>` 指定工程。仅复制补丁不包含应用状态机、CDC握手和记录器缓冲处理。

不能通过手写 USER CODE 标记使 ST 中间件补丁自动受到 CubeMX 保护。这里采用“生成后补丁恢复、构建前检查”的方式；当前未修改 Keil 构建事件，重新生成后需执行上述命令并 Rebuild。
