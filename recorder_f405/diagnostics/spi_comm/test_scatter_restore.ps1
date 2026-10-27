$ErrorActionPreference = 'Stop'
$taskRoot = 'D:\RM\wheel_leg_2026\wheel_leg_2025--4\rm_main'
$taskScript = Join-Path $taskRoot 'User\Support\restore_keil_scatter.ps1'
$taskFixture = Join-Path $PSScriptRoot 'scatter_restore_fixture'
$taskMdk = Join-Path $taskFixture 'MDK-ARM'
$taskLog = Join-Path $taskFixture 'User\Log'
New-Item -ItemType Directory -Force -Path $taskMdk, $taskLog | Out-Null
$taskProject = Join-Path $taskMdk 'rm_main.uvprojx'
$taskOriginal = [IO.File]::ReadAllText((Join-Path $taskRoot 'MDK-ARM\rm_main.uvprojx'))
$taskBroken = $taskOriginal.Replace('<umfTarg>0</umfTarg>', '<umfTarg>1</umfTarg>').Replace('<ScatterFile>..\User\Log\fault_noinit.sct</ScatterFile>', '<ScatterFile>rm_main.sct</ScatterFile>')
[IO.File]::WriteAllText($taskProject, $taskBroken)
Copy-Item -LiteralPath (Join-Path $taskRoot 'User\Log\fault_noinit.sct') -Destination $taskLog -Force
& powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $taskScript -ProjectPath $taskProject
if ($LASTEXITCODE -ne 0) { throw '修复脚本失败。' }
if ([IO.File]::ReadAllText($taskProject) -cne $taskOriginal) { throw '修改超出预期的两个链接字段。' }
if ([IO.File]::ReadAllText($taskProject + '.before_scatter_restore.bak') -cne $taskBroken) { throw '备份不完整。' }
$taskStamp = (Get-Item -LiteralPath $taskProject).LastWriteTimeUtc
& powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $taskScript -ProjectPath $taskProject
if ($LASTEXITCODE -ne 0 -or (Get-Item -LiteralPath $taskProject).LastWriteTimeUtc -ne $taskStamp) { throw '重复运行改写了工程。' }

# 缺少自定义文件时必须失败，且不能改写待修复工程。
$taskMissing = Join-Path $taskFixture 'missing\MDK-ARM'
New-Item -ItemType Directory -Force -Path $taskMissing | Out-Null
$taskMissingProject = Join-Path $taskMissing 'rm_main.uvprojx'
[IO.File]::WriteAllText($taskMissingProject, $taskBroken)
$taskProcess = Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $taskScript + '"'), '-ProjectPath', ('"' + $taskMissingProject + '"')) -WindowStyle Hidden -PassThru -Wait -RedirectStandardError (Join-Path $taskFixture 'missing_sct_error.log')
if ($taskProcess.ExitCode -eq 0 -or [IO.File]::ReadAllText($taskMissingProject) -cne $taskBroken) { throw '缺少 SCT 时没有安全停止。' }
Write-Output 'PASS: field-only restoration, backup, idempotence, missing-SCT failure without mutation'
