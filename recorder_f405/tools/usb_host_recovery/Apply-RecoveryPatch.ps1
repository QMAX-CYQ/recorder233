param(
    [ValidateSet('Apply', 'Check')]
    [string]$Mode = 'Apply',
    [string]$ProjectRoot = (Join-Path $PSScriptRoot '../..'),
    [string]$GitPath
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$patchPath = Join-Path $PSScriptRoot 'usbh_core_recovery.patch'

if (-not $GitPath) {
    $gitCommand = Get-Command git.exe -CommandType Application -ErrorAction SilentlyContinue
    if ($gitCommand) { $GitPath = $gitCommand.Source }
    else {
        $candidates = @(
            "$env:ProgramFiles/Git/cmd/git.exe",
            "${env:ProgramFiles(x86)}/Git/cmd/git.exe",
            "$env:LOCALAPPDATA/Programs/Git/cmd/git.exe",
            "$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/git/cmd/git.exe"
        )
        $GitPath = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
            Select-Object -First 1
    }
}
if (-not $GitPath -or -not (Test-Path -LiteralPath $GitPath -PathType Leaf)) {
    throw 'Git executable not found. Install Git for Windows and reopen PowerShell, or pass -GitPath "C:\Program Files\Git\cmd\git.exe".'
}

foreach ($relativePath in @(
    'Middlewares/ST/STM32_USB_Host_Library/Core/Inc/usbh_core.h',
    'Middlewares/ST/STM32_USB_Host_Library/Core/Src/usbh_core.c'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $ProjectRoot $relativePath))) {
        throw "Missing USB Host source: $relativePath"
    }
}

$repoOutput = & $GitPath -C $ProjectRoot rev-parse --show-toplevel 2>&1
if ($LASTEXITCODE -ne 0) { throw "Project must be in a Git repository: $repoOutput" }
$repoRoot = [IO.Path]::GetFullPath(($repoOutput | Select-Object -Last 1).ToString().Trim())
$projectFull = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\', '/')
$repoFull = $repoRoot.TrimEnd('\', '/')
$applyArgs = @('-C', $repoRoot, 'apply', '--whitespace=nowarn')
if (-not $projectFull.Equals($repoFull, [StringComparison]::OrdinalIgnoreCase)) {
    if (-not $projectFull.StartsWith($repoFull + [IO.Path]::DirectorySeparatorChar,
                                    [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Project directory is outside the Git repository.'
    }
    $relativeDirectory = $projectFull.Substring($repoFull.Length + 1).Replace('\', '/')
    $applyArgs += "--directory=$relativeDirectory"
}

# Preflight both files together. Never use --reject or partially apply a patch.
$ErrorActionPreference = 'Continue' # Windows PowerShell 5 treats native stderr as errors.
$forward = & $GitPath @applyArgs --check $patchPath 2>&1
$forwardExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($forwardExit -eq 0) {
    if ($Mode -eq 'Check') {
        throw 'Recovery patch is missing. Run this script with -Mode Apply, then rebuild.'
    }
    & $GitPath @applyArgs $patchPath
    if ($LASTEXITCODE -ne 0) { throw 'Failed to apply recovery patch.' }
    Write-Output 'Applied USB Host recovery patch. Rebuild before flashing.'
    exit 0
}

$ErrorActionPreference = 'Continue'
$reverse = & $GitPath @applyArgs --reverse --check $patchPath 2>&1
$reverseExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($reverseExit -eq 0) {
    Write-Output 'USB Host recovery patch is already present; no files changed.'
    exit 0
}

throw "USB Host sources do not match this patch (library changed or patch incomplete). No files changed. Review manually.`n$forward`n$reverse"
