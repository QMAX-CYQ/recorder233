$ErrorActionPreference = 'Stop'
$taskPackage = [IO.Path]::GetFullPath('D:\RM\recorder233\User_add')
$taskArchive = Join-Path $PSScriptRoot 'archive'
$taskStageFiles = @('README.md','USB_README.md','SPI_README.md','sync_keil_scatter.ps1')
foreach ($taskFile in $taskStageFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $taskFile))) { throw "缺少待发布文件：$taskFile" }
}

# 先备份原说明并验证副本，再移除原位置文件；只处理包内的 Markdown 文件。
$taskOldDocs = @(Get-ChildItem -LiteralPath $taskPackage -Recurse -File -Filter '*.md')
$taskCodeBefore = @{}
Get-ChildItem -LiteralPath $taskPackage -Recurse -File | Where-Object { $_.Extension -in @('.c','.h','.sct') } | ForEach-Object {
    $taskCodeBefore[$_.FullName] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
}
foreach ($taskDoc in $taskOldDocs) {
    $taskFull = [IO.Path]::GetFullPath($taskDoc.FullName)
    if (-not $taskFull.StartsWith($taskPackage + '\', [StringComparison]::OrdinalIgnoreCase)) { throw '说明文件越出固件包目录。' }
    $taskRelative = $taskFull.Substring($taskPackage.Length + 1)
    $taskBackup = Join-Path $taskArchive $taskRelative
    if (Test-Path -LiteralPath $taskBackup) { throw "归档已存在，停止以免覆盖历史备份：$taskBackup" }
    New-Item -ItemType Directory -Force -Path (Split-Path $taskBackup -Parent) | Out-Null
    Copy-Item -LiteralPath $taskFull -Destination $taskBackup
    if ((Get-FileHash -LiteralPath $taskFull).Hash -ne (Get-FileHash -LiteralPath $taskBackup).Hash) { throw '归档校验失败。' }
}
foreach ($taskDoc in $taskOldDocs) { Remove-Item -LiteralPath $taskDoc.FullName }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $taskPackage 'README.md')
foreach ($taskVariant in @('USB','SPI')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot ($taskVariant + '_README.md')) -Destination (Join-Path $taskPackage ($taskVariant + '\README.md'))
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'sync_keil_scatter.ps1') -Destination (Join-Path $taskPackage ($taskVariant + '\Support\sync_keil_scatter.ps1'))
}
foreach ($taskCode in $taskCodeBefore.Keys) {
    if ((Get-FileHash -LiteralPath $taskCode -Algorithm SHA256).Hash -ne $taskCodeBefore[$taskCode]) { throw "固件源码意外变化：$taskCode" }
}
$taskCodeBefore | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'firmware_hashes_before.json') -Encoding utf8
Write-Output "Archived $($taskOldDocs.Count) old documents; published three README files and two missing helpers. C/H/SCT hashes unchanged."
