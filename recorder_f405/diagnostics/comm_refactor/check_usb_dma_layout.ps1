param(
    [string]$MapPath = (Join-Path $PSScriptRoot '../../MDK-ARM/recorder_f405/recorder_f405.map')
)

$ErrorActionPreference = 'Stop'
$mapText = Get-Content -LiteralPath $MapPath -Raw
foreach ($symbol in @('log_host_ready_frame', 'usb_buffer')) {
    $entry = [regex]::Match($mapText, '(?m)^\s*' + [regex]::Escape($symbol) + '\s+(0x[0-9a-fA-F]+)\s+Data\s+')
    if (-not $entry.Success) { throw "DMA buffer missing from MAP: $symbol" }
    $address = [Convert]::ToUInt64($entry.Groups[1].Value.Substring(2), 16)
    if (($address % 4) -ne 0) {
        throw "USB DMA buffer $symbol is not 4-byte aligned: $($entry.Groups[1].Value)"
    }
    Write-Output "PASS: $symbol at $($entry.Groups[1].Value) is 4-byte aligned"
}
