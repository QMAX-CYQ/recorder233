$ErrorActionPreference = 'Stop'
$diagRoot = $PSScriptRoot
$diagItems = Get-Content -LiteralPath (Join-Path $diagRoot 'sender_manifest.json') -Raw | ConvertFrom-Json
# Verify every source before changing any file; do not overwrite intervening edits.
foreach ($diagItem in $diagItems) {
    $diagActual = (Get-FileHash -LiteralPath $diagItem.source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($diagActual -ne $diagItem.sha256) { throw "Source changed: $($diagItem.source)" }
}
foreach ($diagItem in $diagItems) {
    $diagRelative = [IO.Path]::GetRelativePath('D:\RM\rm_main_gimbal', $diagItem.source)
    $diagBackup = Join-Path (Join-Path $diagRoot 'original_sender') $diagRelative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($diagBackup)) | Out-Null
    [IO.File]::Copy($diagItem.source, $diagBackup, $false)
    [IO.File]::Copy($diagItem.staged, $diagItem.source, $true)
}
Write-Output 'Applied diagnostic counters to six sender source/header files.'
