$ErrorActionPreference = 'Stop'
$taskPackage = Join-Path $PSScriptRoot 'package'
$taskDestination = [System.IO.Path]::GetFullPath('D:\RM\recorder233\User_add')
$taskUsbDestination = [System.IO.Path]::GetFullPath((Join-Path $taskDestination 'USB'))
$taskSpiDestination = [System.IO.Path]::GetFullPath((Join-Path $taskDestination 'SPI'))

# 移动前验证绝对目标，所有移动均限制在用户指定的 User_add 中。
foreach ($taskPath in @($taskUsbDestination, $taskSpiDestination)) {
    if (-not $taskPath.StartsWith($taskDestination + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "目标超出 User_add：$taskPath"
    }
    if (Test-Path -LiteralPath $taskPath) { throw "目标目录已存在，停止以免覆盖：$taskPath" }
}
$taskOriginal = @(Get-ChildItem -LiteralPath $taskDestination -Force)
$taskBaseline = Join-Path $taskPackage 'USB'
$taskHashes = @{}
foreach ($taskFile in Get-ChildItem -LiteralPath $taskDestination -File -Recurse -Force) {
    $taskRelative = $taskFile.FullName.Substring($taskDestination.Length + 1)
    $taskCopy = Join-Path $taskBaseline $taskRelative
    if (-not (Test-Path -LiteralPath $taskCopy)) { throw "暂存中缺少原文件：$taskRelative" }
    $taskHash = (Get-FileHash -LiteralPath $taskFile.FullName -Algorithm SHA256).Hash
    if ($taskHash -ne (Get-FileHash -LiteralPath $taskCopy -Algorithm SHA256).Hash) {
        throw "原文件已变化，停止发布：$taskRelative"
    }
    $taskHashes[$taskRelative] = $taskHash
}
if ($taskHashes.Count -ne @(Get-ChildItem -LiteralPath $taskBaseline -File -Recurse -Force).Count) {
    throw '原目录与 USB 基线文件数量不同，停止发布。'
}

New-Item -ItemType Directory -Path $taskUsbDestination, $taskSpiDestination | Out-Null
Get-ChildItem -LiteralPath (Join-Path $taskPackage 'SPI') -Force |
    Copy-Item -Destination $taskSpiDestination -Recurse
foreach ($taskEntry in $taskOriginal) {
    Move-Item -LiteralPath $taskEntry.FullName -Destination (Join-Path $taskUsbDestination $taskEntry.Name)
}
Copy-Item -LiteralPath (Join-Path $taskPackage 'README.md') -Destination (Join-Path $taskDestination 'README.md')

# 迁移后再次验证 USB 内容及 SPI 发布内容，原文件不做任何内容修改。
foreach ($taskEntry in $taskHashes.GetEnumerator()) {
    if ((Get-FileHash -LiteralPath (Join-Path $taskUsbDestination $taskEntry.Key)).Hash -ne $taskEntry.Value) {
        throw "USB 迁移校验失败：$($taskEntry.Key)"
    }
}
foreach ($taskFile in Get-ChildItem -LiteralPath (Join-Path $taskPackage 'SPI') -File -Recurse) {
    $taskRelative = $taskFile.FullName.Substring((Join-Path $taskPackage 'SPI').Length + 1)
    if ((Get-FileHash -LiteralPath $taskFile.FullName).Hash -ne
        (Get-FileHash -LiteralPath (Join-Path $taskSpiDestination $taskRelative)).Hash) {
        throw "SPI 发布校验失败：$taskRelative"
    }
}
Write-Output "已将 $($taskHashes.Count) 个原文件原样迁入 USB，并发布 SPI 支持包。"
