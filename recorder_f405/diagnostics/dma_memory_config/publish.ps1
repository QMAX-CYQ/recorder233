$ErrorActionPreference = 'Stop'
$taskDestination = [System.IO.Path]::GetFullPath('D:\RM\recorder233\User_add')
$taskFiles = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'publish_manifest.json') -Raw | ConvertFrom-Json
foreach ($taskFile in $taskFiles) {
    $taskTarget = [System.IO.Path]::GetFullPath((Join-Path $taskDestination $taskFile.Path))
    if (-not $taskTarget.StartsWith($taskDestination + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw '目标超出指定 User_add 目录。'
    }
    if ($taskFile.BeforeHash) {
        if (-not (Test-Path -LiteralPath $taskTarget) -or
            (Get-FileHash -LiteralPath $taskTarget).Hash -ne $taskFile.BeforeHash) {
            throw "目标文件已变化，停止覆盖：$taskTarget"
        }
    } elseif (Test-Path -LiteralPath $taskTarget) { throw "新文件位置已被占用：$taskTarget" }
    $taskSource = Join-Path $PSScriptRoot ('package/' + $taskFile.Path)
    if ((Get-FileHash -LiteralPath $taskSource).Hash -ne $taskFile.AfterHash) {
        throw "暂存文件已变化：$taskSource"
    }
}
foreach ($taskFile in $taskFiles) {
    $taskTarget = Join-Path $taskDestination $taskFile.Path
    New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($taskTarget)) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('package/' + $taskFile.Path)) -Destination $taskTarget -Force
    if ((Get-FileHash -LiteralPath $taskTarget).Hash -ne $taskFile.AfterHash) { throw '发布校验失败。' }
}
Write-Output "已发布并校验 $($taskFiles.Count) 个文件，未修改实际主板工程及 CubeMX 配置。"
