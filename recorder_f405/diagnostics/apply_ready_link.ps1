$ErrorActionPreference = 'Stop'
$stageRoot = Join-Path $PSScriptRoot 'ready_link'
$roots = @{sender='D:\RM\rm_main_gimbal';receiver='D:\RM\recorder233\recorder_f405'}
$manifest = Get-Content -LiteralPath (Join-Path $stageRoot 'manifest.json') -Raw | ConvertFrom-Json
# 两端全部验证通过后再应用，避免覆盖并行修改。
foreach ($entry in $manifest) {
    $path = Join-Path $roots[$entry.side] $entry.name
    if ($entry.exists) {
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower() -ne $entry.sha256) { throw "Source changed: $path" }
    } elseif (Test-Path -LiteralPath $path) { throw "New file already exists: $path" }
}
foreach ($entry in $manifest) {
    $path = Join-Path $roots[$entry.side] $entry.name
    if ($entry.exists) {
        $backup = Join-Path $stageRoot ('originals\'+$entry.side+'\'+$entry.name)
        New-Item -ItemType Directory -Path (Split-Path $backup) -Force | Out-Null
        Copy-Item -LiteralPath $path -Destination $backup
    }
    Copy-Item -LiteralPath (Join-Path $stageRoot ($entry.side+'\'+$entry.name)) -Destination $path
}
# 保留原始 V6.24 设置，独立副本选择已安装的 V6.22。
$utf8 = [Text.UTF8Encoding]::new($false)
$receiverOriginal = Join-Path $roots.receiver 'MDK-ARM\recorder_f405.uvprojx'
$receiverValidation = Join-Path $roots.receiver 'MDK-ARM\recorder_f405_ready_validation.uvprojx'
$projectText = [IO.File]::ReadAllText($receiverOriginal).Replace('6240000::V6.24::ARMCLANG','6220000::V6.22::ARMCLANG')
# 此 F405 工程在编译器副本下未自动搜索 C 标准头文件，显式加入其包含目录。
$projectText = $projectText.Replace('<IncludePath>', '<IncludePath>D:\KEIL5\ARM\ArmCompiler6.22\include;')
[IO.File]::WriteAllText($receiverValidation, $projectText, $utf8)
$senderValidation = Join-Path $roots.sender 'MDK-ARM\rm_main_tx_queue_validation.uvprojx'
foreach ($build in @(@{name='sender';path=$senderValidation},@{name='receiver';path=$receiverValidation})) {
    $buildLog = Join-Path $stageRoot ($build.name+'_build.log')
    $process = Start-Process -FilePath 'D:\KEIL5\UV4\UV4.exe' -ArgumentList @('-b',('"'+$build.path+'"'),'-o',('"'+$buildLog+'"')) -WindowStyle Hidden -PassThru
    $process.WaitForExit()
    Write-Output $build.name
    Get-Content -LiteralPath $buildLog | Select-Object -Last 12
}
