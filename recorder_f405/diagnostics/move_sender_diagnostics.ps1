$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath('D:\RM\recorder233\recorder_f405\diagnostics')
$destinationRoot = [IO.Path]::GetFullPath('D:\RM\rm_main_gimbal\diagnostics')
$items = @('original_sender', 'sender', 'tx_queue', 'cubemx_queue_fix',
    'apply_sender.ps1', 'apply_tx_queue.ps1', 'apply_cubemx_queue_fix.ps1',
    'prepare_tx_queue.py', 'prepare_cubemx_queue_fix.py', 'finish_sender_stage.py',
    'sender_build.log', 'sender_manifest.json', 'sender.patch')
# 在移动前检查所有绝对路径和目标冲突，避免覆盖已有资料。
$inventory = @()
foreach ($name in $items) {
    $sourcePath = [IO.Path]::GetFullPath((Join-Path $sourceRoot $name))
    $targetPath = [IO.Path]::GetFullPath((Join-Path $destinationRoot $name))
    if (-not $sourcePath.StartsWith($sourceRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
        -not $targetPath.StartsWith($destinationRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Path outside the intended diagnostics directories'
    }
    if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Missing source: $sourcePath" }
    if (Test-Path -LiteralPath $targetPath) { throw "Destination already exists: $targetPath" }
    $sourceItem = Get-Item -LiteralPath $sourcePath
    $files = if ($sourceItem.PSIsContainer) { Get-ChildItem -LiteralPath $sourcePath -File -Recurse } else { @($sourceItem) }
    foreach ($file in $files) {
        $inventory += [pscustomobject]@{
            relative = [IO.Path]::GetRelativePath($sourceRoot, $file.FullName)
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        }
    }
}
New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null
foreach ($name in $items) {
    Move-Item -LiteralPath (Join-Path $sourceRoot $name) -Destination (Join-Path $destinationRoot $name)
}
# 校验移动前后的文件内容，再更新清单中的旧暂存目录。
foreach ($entry in $inventory) {
    $path = Join-Path $destinationRoot $entry.relative
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Hash mismatch after move: $path"
    }
}
$manifestPath = Join-Path $destinationRoot 'sender_manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach ($entry in $manifest) {
    $relative = [IO.Path]::GetRelativePath($sourceRoot, $entry.staged)
    if ($relative.StartsWith('..')) { throw 'Unexpected manifest staging path' }
    $entry.staged = Join-Path $destinationRoot $relative
}
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 5), $utf8)
$readme = @'
# 主板发送端诊断资料

本目录从 F405 工程迁入，包含主板的历史修改脚本、源码备份、暂存代码、测试和编译记录。不会自动参与 Keil 编译或烧录。

- original_sender：最初加入发送诊断之前的源码备份。
- sender、sender_manifest.json、sender.patch：早期诊断修改的暂存代码、原文件校验和差异。
- tx_queue：加入发送 FIFO 的历史版本、测试和修改前备份。
- cubemx_queue_fix：整理 CubeMX 保留区后的版本、校验结果和修改前备份。
- apply_*.ps1、prepare_*.py、finish_sender_stage.py：历史代码修改工具，不是固件组成部分。

这些资料是不同阶段的快照，并非当前源码镜像。脚本不保证可重复运行，不要批量执行或用旧暂存代码直接覆盖现有源码。源码修改以 USB_DEVICE/App 和 User 中的当前文件为准。

日志分析报告、接收端备份仍在 D:/RM/recorder233/recorder_f405/diagnostics。早期 prepare_diagnostics.py 同时涉及两端，因此保留在 F405 目录，其发送资料输出路径已更新到本目录。

迁移只调整辅助资料及清单路径，没有修改固件源码和工程配置。
'@
[IO.File]::WriteAllText((Join-Path $destinationRoot 'README.md'), $readme, $utf8)
[IO.File]::WriteAllText((Join-Path $destinationRoot 'migration_inventory.json'), ($inventory | ConvertTo-Json -Depth 4), $utf8)
Write-Output ("Moved {0} entries; verified {1} files." -f $items.Count, $inventory.Count)
