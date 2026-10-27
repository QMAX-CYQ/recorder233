$ErrorActionPreference = 'Stop'
# 同步发送端支持包说明，避免继续声称记录板 SPI 不增加时间戳。
$taskPaths = @(
    'D:\RM\recorder233\User_add\SPI\README.md',
    (Join-Path $PSScriptRoot '../user_add_spi_split/package/SPI/README.md')
)
$taskOld = 'SPI 日志按原始字节流传输，不含 USB 控制帧，F405 SPI 后端也不增加时间戳。'
$taskNew = 'SPI 主板按原始字节流发送，不含 USB 控制帧；F405 接收端与 USB 共用逐行时间戳处理，文件中每个非空日志行开头都会添加 MMDD-HH:MM:SS 时间戳。'
foreach ($taskPath in $taskPaths) {
    $taskText = [System.IO.File]::ReadAllText($taskPath)
    if ($taskText.Contains($taskNew)) { continue }
    if (-not $taskText.Contains($taskOld)) { throw "说明内容已变化，停止覆盖：$taskPath" }
    [System.IO.File]::WriteAllText($taskPath, $taskText.Replace($taskOld, $taskNew))
}
