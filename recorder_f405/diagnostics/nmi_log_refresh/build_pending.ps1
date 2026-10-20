$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$taskKeil = 'D:\KEIL5\UV4\UV4.exe'
$taskProjects = @(
    @{Path='D:\RM\wheel_leg_2026\wheel_leg_2025--4\rm_main\MDK-ARM\rm_main.uvprojx'; Log='wheel_build.log'},
    @{Path=(Join-Path $taskRoot 'support_validation.uvprojx'); Log='package_build.log'}
)
foreach ($taskProject in $taskProjects) {
    $taskLog = Join-Path $taskRoot $taskProject.Log
    $taskArgs = @('-b', ('"' + $taskProject.Path + '"'), '-j0', '-o', ('"' + $taskLog + '"'))
    $taskProcess = Start-Process -FilePath $taskKeil -ArgumentList $taskArgs -WindowStyle Hidden -PassThru
    $taskProcess.WaitForExit()
    Get-Content -LiteralPath $taskLog -Tail 20
    if ($taskProcess.ExitCode -ne 0) { throw "Build failed or reported warnings: $($taskProject.Path)" }
}
Write-Output 'Both Keil builds passed. Check MAP/vector entries and preserve the matching AXF before flashing.'
