$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'prepare_validation.ps1')
$taskResults = @()
foreach ($taskMode in @('USB','SPI')) {
    $taskLower = $taskMode.ToLower()
    foreach ($taskCache in @(0,1)) {
        foreach ($taskDma in @(0,1)) {
            $taskCacheText = @('Disabled','Enabled')[$taskCache]
            $taskDmaText = @('DISABLE','ENABLE')[$taskDma]
            $taskIoc = "CORTEX_M7.CPU_DCache=$taskCacheText`r`nUSB_OTG_HS.VirtualMode-Device_Only_FS=Device_Only_FS`r`nUSB_OTG_HS.dma_enable-Device_Only_FS=$taskDmaText`r`n"
            [System.IO.File]::WriteAllText((Join-Path $PSScriptRoot "${taskLower}_config.ioc"), $taskIoc)
            $taskLog = Join-Path $PSScriptRoot "${taskLower}_${taskCache}_${taskDma}.log"
            $taskProject = Join-Path $PSScriptRoot "${taskLower}_validation.uvprojx"
            $taskBuild = Start-Process -FilePath 'D:\KEIL5\UV4\UV4.exe' -ArgumentList @('-b',$taskProject,'-j0','-o',$taskLog) -WindowStyle Hidden -PassThru
            $taskBuild.WaitForExit()
            if ($taskBuild.ExitCode -ne 0) { Get-Content -LiteralPath $taskLog -Tail 18; throw "构建失败：$taskMode $taskCache/$taskDma" }
            $taskMap = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot "${taskLower}_build/rm_main.map"))
            $taskProtected = [regex]::IsMatch($taskMap, 'Execution Region RW_USB_DMA ')
            if ($taskProtected -ne [bool]($taskCache -band $taskDma)) { throw 'USB 专用区条件错误。' }
            if (-not $taskMap.Contains('Execution Region RW_FAULT_NOINIT ')) { throw '故障保留区缺失。' }
            if ($taskMode -eq 'SPI' -and -not $taskMap.Contains('Execution Region RW_LOG_SPI_DMA ')) { throw 'SPI DMA 区缺失。' }
            if ($taskDma) {
                foreach ($taskSymbol in @('hpcd_USB_OTG_HS','UserRxBufferHS','UserTxBufferHS')) {
                    $taskMatch = [regex]::Match($taskMap, '(?m)^\s*' + $taskSymbol + '\s+(0x[0-9a-fA-F]+)\s+Data\s+')
                    if (-not $taskMatch.Success) { throw "DMA 符号缺失：$taskSymbol" }
                    $taskAddress = [Convert]::ToUInt64($taskMatch.Groups[1].Value.Substring(2),16)
                    if ($taskAddress -lt 0x24000000 -or $taskAddress -ge 0x24050000) { throw "USB DMA 数据落在 AXI 之外：$taskSymbol" }
                }
            }
            $taskSavedMap = Join-Path $PSScriptRoot "${taskLower}_${taskCache}_${taskDma}.map"
            Copy-Item -LiteralPath (Join-Path $PSScriptRoot "${taskLower}_build/rm_main.map") -Destination $taskSavedMap
            $taskResults += [PSCustomObject]@{Mode=$taskMode;DCache=$taskCache;UsbDma=$taskDma;Protected=$taskProtected;Build='0 errors, 0 warnings'}
            Write-Output "通过：$taskMode D-Cache=$taskCache USB-DMA=$taskDma 专用区=$taskProtected"
        }
    }
}
$taskResults | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'matrix_results.json') -Encoding utf8
