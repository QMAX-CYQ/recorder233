$ErrorActionPreference = 'Stop'
$taskIoc = Join-Path $PSScriptRoot 'usb_config.ioc'
$taskOriginal = [System.IO.File]::ReadAllText($taskIoc)
try {
    [System.IO.File]::WriteAllText($taskIoc, 'CORTEX_M7.CPU_DCache=INVALID')
    $taskLog = Join-Path $PSScriptRoot 'invalid_config.log'
    $taskBuild = Start-Process -FilePath 'D:\KEIL5\UV4\UV4.exe' -ArgumentList @('-b',(Join-Path $PSScriptRoot 'usb_validation.uvprojx'),'-j0','-o',$taskLog) -WindowStyle Hidden -PassThru
    $taskBuild.WaitForExit()
    $taskText = [System.IO.File]::ReadAllText($taskLog)
    if ($taskBuild.ExitCode -eq 0 -or $taskText -match 'compiling |assembling |linking\.\.\.') {
        throw '配置生成失败后构建没有正确停止。'
    }
    Write-Output '通过：非法 CubeMX 开关使构建在编译前停止。'
} finally {
    [System.IO.File]::WriteAllText($taskIoc, $taskOriginal)
}
