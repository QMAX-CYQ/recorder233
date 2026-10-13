$senderRoot = 'D:\RM\rm_main_gimbal'
$stageRoot = Join-Path $PSScriptRoot 'tx_queue'
$originalHashes = Get-Content -LiteralPath (Join-Path $stageRoot 'original_hashes.json') -Raw | ConvertFrom-Json
$backupRoot = Join-Path $stageRoot 'originals'
foreach ($entry in $originalHashes.PSObject.Properties) {
    $targetPath = Join-Path $senderRoot $entry.Name
    if ((Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash.ToLower() -ne $entry.Value) {
        throw "Source changed since staging: $targetPath"
    }
}
foreach ($entry in $originalHashes.PSObject.Properties) {
    $targetPath = Join-Path $senderRoot $entry.Name
    $backupPath = Join-Path $backupRoot $entry.Name
    New-Item -ItemType Directory -Path (Split-Path $backupPath) -Force | Out-Null
    Copy-Item -LiteralPath $targetPath -Destination $backupPath
    Copy-Item -LiteralPath (Join-Path $stageRoot $entry.Name) -Destination $targetPath
}
# Validate using an installed compiler in a separate project copy. Leave the
# user's original V6.24 project settings unchanged.
$projectPath = Join-Path $senderRoot 'MDK-ARM\rm_main.uvprojx'
$validationPath = Join-Path $senderRoot 'MDK-ARM\rm_main_tx_queue_validation.uvprojx'
$projectText = [System.IO.File]::ReadAllText($projectPath)
$projectText = $projectText.Replace('6240000::V6.24::ARMCLANG', '6220000::V6.22::ARMCLANG')
[System.IO.File]::WriteAllText($validationPath, $projectText)
$buildLogPath = Join-Path $stageRoot 'sender_queue_build.log'
$buildProcess = Start-Process -FilePath 'D:\KEIL5\UV4\UV4.exe' -ArgumentList @('-b', ('"'+$validationPath+'"'), '-o', ('"'+$buildLogPath+'"')) -WindowStyle Hidden -PassThru
$buildProcess.WaitForExit()
Get-Content -LiteralPath $buildLogPath | Select-Object -Last 15
