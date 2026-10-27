$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$taskMain = [System.IO.File]::ReadAllText('D:\RM\rm_main_gimbal\Core\Src\main.c')
$taskMain = $taskMain.Replace('#include "main.h"', '#include "main.h"' + "`r`n" + '#include "dma_memory_config.h"')
$taskMain = $taskMain.Replace('FaultLog_BootInit();', 'FaultLog_BootInit();' + "`r`n#if DMA_MEMORY_DCACHE_ENABLED`r`n  SCB_EnableDCache();`r`n#endif")
[System.IO.File]::WriteAllText((Join-Path $taskRoot 'main.c'), $taskMain)
$taskConf = [System.IO.File]::ReadAllText((Join-Path $taskRoot '../user_add_status_refresh/usbd_conf.c'))
$taskConf = $taskConf.Replace('    /* Keep DMA enabled even if the generated default changes. */' + "`r`n" + '    pcdHandle->Init.dma_enable = ENABLE;', '')
$taskConf = $taskConf.Replace('hpcd_USB_OTG_HS.Init.dma_enable = ENABLE;', 'hpcd_USB_OTG_HS.Init.dma_enable = DMA_MEMORY_USB_DMA_ENABLED ? ENABLE : DISABLE;')
[System.IO.File]::WriteAllText((Join-Path $taskRoot 'usbd_conf.c'), $taskConf)

foreach ($taskMode in @('USB','SPI')) {
    $taskLower = $taskMode.ToLower()
    $taskText = [System.IO.File]::ReadAllText((Join-Path $taskRoot "../user_add_spi_split/${taskLower}_validation.uvprojx"))
    foreach ($taskBranch in @('USB','SPI')) {
        $taskOld = Join-Path $taskRoot "../user_add_spi_split/package/$taskBranch"
        $taskOld = [System.IO.Path]::GetFullPath($taskOld)
        $taskNew = Join-Path $taskRoot "package/$taskBranch"
        foreach ($taskFile in Get-ChildItem -LiteralPath $taskNew -File -Recurse) {
            $taskText = $taskText.Replace((Join-Path $taskOld $taskFile.Name), $taskFile.FullName)
        }
    }
    $taskIncludes = @('Log','Support','Task','Usb_device') | ForEach-Object { Join-Path $taskRoot "package/$taskMode/$_" }
    $taskIncludes += Join-Path $taskRoot 'package/USB/Usb_device'
    $taskText = $taskText.Replace('<IncludePath>', '<IncludePath>' + ($taskIncludes -join ';') + ';')
    $taskText = [regex]::Replace($taskText, '<OutputDirectory>.*?</OutputDirectory>', '<OutputDirectory>' + (Join-Path $taskRoot "${taskLower}_build\") + '</OutputDirectory>')
    $taskText = [regex]::Replace($taskText, '<ListingPath>.*?</ListingPath>', '<ListingPath>' + (Join-Path $taskRoot "${taskLower}_build\") + '</ListingPath>')
    $taskText = $taskText.Replace('D:\RM\rm_main_gimbal\Core\Src\main.c', (Join-Path $taskRoot 'main.c'))
    $taskText = [regex]::Replace($taskText, '<FilePath>[^<]*usbd_conf.c</FilePath>', '<FilePath>' + (Join-Path $taskRoot 'usbd_conf.c') + '</FilePath>')
    $taskScript = Join-Path $taskRoot "package/$taskMode/Support/sync_dma_memory.ps1"
    $taskIoc = Join-Path $taskRoot "${taskLower}_config.ioc"
    $taskCommand = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + $taskScript + '" -IocPath "' + $taskIoc + '"'
    $taskHook = '<BeforeMake><RunUserProg1>1</RunUserProg1><RunUserProg2>0</RunUserProg2><UserProg1Name>' + $taskCommand + '</UserProg1Name><UserProg2Name /><UserProg1Dos16Mode>0</UserProg1Dos16Mode><UserProg2Dos16Mode>0</UserProg2Dos16Mode><nStopB1X>2</nStopB1X><nStopB2X>0</nStopB2X></BeforeMake>'
    $taskText = [regex]::Replace($taskText, '(?s)<BeforeMake>.*?</BeforeMake>', $taskHook)
    [System.IO.File]::WriteAllText((Join-Path $taskRoot "${taskLower}_validation.uvprojx"), $taskText)
}
