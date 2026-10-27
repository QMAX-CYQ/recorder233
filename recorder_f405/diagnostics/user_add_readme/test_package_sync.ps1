$ErrorActionPreference = 'Stop'
$taskPackage = 'D:\RM\recorder233\User_add'
$taskUtf8 = New-Object Text.UTF8Encoding($true)
$taskHelper = Join-Path $PSScriptRoot 'sync_keil_scatter.ps1'
[IO.File]::WriteAllText($taskHelper, [IO.File]::ReadAllText($taskHelper), $taskUtf8)
foreach ($taskVariant in @('USB', 'SPI')) {
    # 使用带空格的移植目录和非 rm_main 工程名，验证不依赖来源电脑路径。
    $taskRoot = Join-Path $PSScriptRoot ('fixtures\' + $taskVariant + ' portable project')
    $taskUser = Join-Path $taskRoot 'User'
    $taskMdk = Join-Path $taskRoot 'MDK-ARM'
    New-Item -ItemType Directory -Force -Path $taskUser, $taskMdk | Out-Null
    foreach ($taskFolder in @('Log','Support')) {
        Copy-Item -LiteralPath (Join-Path $taskPackage ($taskVariant + '\' + $taskFolder)) -Destination $taskUser -Recurse -Force
    }
    Copy-Item -LiteralPath $taskHelper -Destination (Join-Path $taskUser 'Support\sync_keil_scatter.ps1') -Force
    $taskScript = Join-Path $taskUser 'Support\sync_dma_memory.ps1'
    $taskIoc = Join-Path $taskRoot 'portable_test.ioc'
    $taskSct = Join-Path $taskMdk 'portable_test.sct'
    foreach ($taskCache in @(0,1)) {
        foreach ($taskDma in @(0,1)) {
            $taskLines = @('ProjectManager.ProjectName=portable_test', ('CORTEX_M7.CPU_DCache=' + $taskCache), 'USB_OTG_HS.VirtualMode-Device_Only_FS=Device_Only_FS', ('USB_OTG_HS.dma_enable-Device_Only_FS=' + $taskDma))
            [IO.File]::WriteAllLines($taskIoc, $taskLines)
            [IO.File]::WriteAllText($taskSct, 'CubeMX overwritten default layout')
            & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $taskScript -IocPath $taskIoc
            if ($LASTEXITCODE -ne 0) { throw "同步失败：$taskVariant $taskCache $taskDma" }
            $taskHeader = [IO.File]::ReadAllText((Join-Path $taskUser 'Support\dma_memory_config.h'))
            if (-not $taskHeader.Contains("#define DMA_MEMORY_DCACHE_ENABLED $taskCache") -or
                -not $taskHeader.Contains("#define DMA_MEMORY_USB_DMA_ENABLED $taskDma")) { throw '配置头不同步。' }
            $taskText = [IO.File]::ReadAllText($taskSct)
            if (-not $taskText.Contains('RW_FAULT_NOINIT') -or $taskText.Contains('CubeMX overwritten')) { throw '没有恢复自定义布局。' }
            if ($taskVariant -eq 'SPI' -and -not $taskText.Contains('RW_LOG_SPI_DMA')) { throw 'SPI 区丢失。' }
            foreach ($taskMatch in [regex]::Matches($taskText, '(?m)^\s*#include\s+"([^"]+)"')) {
                if (-not (Test-Path -LiteralPath $taskMatch.Groups[1].Value)) { throw '生成 SCT 包含路径不可解析。' }
            }
        }
    }
    $taskStamp = (Get-Item -LiteralPath $taskSct).LastWriteTimeUtc
    & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $taskScript -IocPath $taskIoc
    if ($LASTEXITCODE -ne 0 -or (Get-Item -LiteralPath $taskSct).LastWriteTimeUtc -ne $taskStamp) { throw '重复执行不幂等。' }
}
Write-Output 'PASS: USB/SPI, four DMA/cache combinations, regeneration recovery, relocated path with spaces, renamed project, include resolution, idempotence'
