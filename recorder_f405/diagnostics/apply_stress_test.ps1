$ErrorActionPreference = 'Stop'
$senderRoot = 'D:\RM\rm_main_gimbal'
$stageRoot = Join-Path $PSScriptRoot 'stress_test'
$hashes = Get-Content -LiteralPath (Join-Path $stageRoot 'original_hashes.json') -Raw | ConvertFrom-Json
# 先验证原文件，避免覆盖用户在准备期间产生的改动。
foreach ($entry in $hashes.PSObject.Properties) {
    $path = Join-Path $senderRoot $entry.Name
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower() -ne $entry.Value) {
        throw "Source changed since staging: $path"
    }
}
$archiveRoot = Join-Path $senderRoot 'diagnostics\stress_test'
if (Test-Path -LiteralPath $archiveRoot) { throw 'Stress test archive already exists' }
Copy-Item -LiteralPath $stageRoot -Destination $archiveRoot -Recurse
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'prepare_stress_test.py') -Destination (Join-Path $senderRoot 'diagnostics\prepare_stress_test.py')
foreach ($entry in $hashes.PSObject.Properties) {
    $path = Join-Path $senderRoot $entry.Name
    $backup = Join-Path $archiveRoot ('originals\'+$entry.Name)
    New-Item -ItemType Directory -Path (Split-Path $backup) -Force | Out-Null
    Copy-Item -LiteralPath $path -Destination $backup
    Copy-Item -LiteralPath (Join-Path $stageRoot $entry.Name) -Destination $path
}
# 原工程编译器设置不变，使用 V6.22 副本验证。
$logPath = Join-Path $archiveRoot 'build.log'
$projectPath = Join-Path $senderRoot 'MDK-ARM\rm_main_tx_queue_validation.uvprojx'
$process = Start-Process -FilePath 'D:\KEIL5\UV4\UV4.exe' -ArgumentList @('-b', ('"'+$projectPath+'"'), '-o', ('"'+$logPath+'"')) -WindowStyle Hidden -PassThru
$process.WaitForExit()
Get-Content -LiteralPath $logPath | Select-Object -Last 14
