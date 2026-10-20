$ErrorActionPreference = 'Stop'
$taskSource = Join-Path $PSScriptRoot 'package'
$taskTarget = 'D:\RM\recorder233\User_add'
$taskBackup = Join-Path $PSScriptRoot ('before_user_add_' + (Get-Date -Format yyyyMMdd_HHmmss))
New-Item -ItemType Directory -Path $taskBackup | Out-Null
foreach ($taskName in (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'publish_files.txt'))) {
    $taskOld = Join-Path $taskTarget $taskName
    if (Test-Path -LiteralPath $taskOld) { Copy-Item -LiteralPath $taskOld -Destination (Join-Path $taskBackup $taskName) }
    Copy-Item -LiteralPath (Join-Path $taskSource $taskName) -Destination $taskOld
}
Write-Output 'NMI increment copied. Existing USB, business code and configuration preserved.'
