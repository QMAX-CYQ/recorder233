param(
    [Parameter(Mandatory=$true)][string]$IocPath,
    [string]$OutputPath = '',
    [ValidateSet('USB_OTG_HS','USB_OTG_FS')][string]$UsbInstance = 'USB_OTG_HS'
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrEmpty($OutputPath)) {
    $OutputPath = Join-Path ([System.IO.Path]::GetDirectoryName($MyInvocation.MyCommand.Path)) 'dma_memory_config.h'
}
$taskIoc = (Resolve-Path -LiteralPath $IocPath).Path
$taskValues = @{}
foreach ($taskLine in [System.IO.File]::ReadAllLines($taskIoc)) {
    if ($taskLine.StartsWith('#') -or -not $taskLine.Contains('=')) { continue }
    $taskPair = $taskLine -split '=', 2
    if ($taskValues.ContainsKey($taskPair[0])) { throw "重复的 CubeMX 配置项：$($taskPair[0])" }
    $taskValues[$taskPair[0]] = $taskPair[1].Trim()
}
function Convert-ConfigFlag([string]$Value, [string]$Key) {
    switch -Regex ($Value) {
        '^(Enabled|ENABLE|true|1)$' { return 1 }
        '^(Disabled|DISABLE|false|0)$' { return 0 }
        default { throw "不能识别配置 $Key=$Value，停止构建以免使用错误布局。" }
    }
}
# CubeMX 未显式记录的开关按默认关闭处理；只读取当前启用模式对应的 DMA 项。
$taskCache = 0
if ($taskValues.ContainsKey('CORTEX_M7.CPU_DCache')) {
    $taskCache = Convert-ConfigFlag $taskValues['CORTEX_M7.CPU_DCache'] 'CORTEX_M7.CPU_DCache'
}
$taskDma = 0
$taskModes = @($taskValues.Keys | Where-Object { $_ -like "$UsbInstance.VirtualMode*" })
if ($taskModes.Count -gt 1) { throw '存在多个 USB 模式，不能确定生效的 DMA 配置。' }
$taskDmaKey = "$UsbInstance.dma_enable"
if ($taskModes.Count -eq 1) {
    $taskMode = $taskValues[$taskModes[0]]
    if ($taskMode -notmatch 'Device') { throw "当前 USB 模式不是 Device：$taskMode" }
    $taskModeDmaKey = "$UsbInstance.dma_enable-$taskMode"
    if ($taskValues.ContainsKey($taskModeDmaKey)) { $taskDmaKey = $taskModeDmaKey }
}
if ($taskValues.ContainsKey($taskDmaKey)) {
    $taskDma = Convert-ConfigFlag $taskValues[$taskDmaKey] $taskDmaKey
}
$taskSha = [System.Security.Cryptography.SHA256]::Create()
try {
    $taskHash = [BitConverter]::ToString($taskSha.ComputeHash([System.IO.File]::ReadAllBytes($taskIoc))).Replace('-', '')
} finally { $taskSha.Dispose() }
$taskHeader = @"
#ifndef DMA_MEMORY_CONFIG_H
#define DMA_MEMORY_CONFIG_H
/* 按 CubeMX 配置自动生成，请勿手改；源文件：$([System.IO.Path]::GetFileName($taskIoc))。
 * SHA256：$taskHash */
#define DMA_MEMORY_DCACHE_ENABLED $taskCache
#define DMA_MEMORY_USB_DMA_ENABLED $taskDma
#endif
"@
$taskOutput = [System.IO.Path]::GetFullPath($OutputPath)
if (-not (Test-Path -LiteralPath $taskOutput) -or
    [System.IO.File]::ReadAllText($taskOutput) -ne $taskHeader) {
    [System.IO.File]::WriteAllText($taskOutput, $taskHeader, (New-Object System.Text.UTF8Encoding($false)))
}
Write-Output "D-Cache=$taskCache USB-DMA=$taskDma RW_USB_DMA=$($taskCache -band $taskDma)"

# 在编译前刷新 CubeMX 默认 SCT，避免生成后钩子被跳过或路径再次被覆盖。
& (Join-Path $PSScriptRoot 'sync_keil_scatter.ps1') -ProjectRoot (Split-Path $taskIoc -Parent)
exit 0
