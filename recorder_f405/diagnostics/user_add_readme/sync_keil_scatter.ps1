param([string]$ProjectRoot = '')
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
}
$taskRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$taskIocFiles = @(Get-ChildItem -LiteralPath $taskRoot -Filter '*.ioc' -File)
if ($taskIocFiles.Count -ne 1) { throw '工程根目录应有唯一的 ioc 文件，不能确定生成 SCT 的名称。' }
$taskProjectName = [IO.Path]::GetFileNameWithoutExtension($taskIocFiles[0].Name)
foreach ($taskLine in [IO.File]::ReadAllLines($taskIocFiles[0].FullName)) {
    if ($taskLine.StartsWith('ProjectManager.ProjectName=')) {
        $taskProjectName = $taskLine.Substring('ProjectManager.ProjectName='.Length).Trim()
    }
}
if ([string]::IsNullOrWhiteSpace($taskProjectName) -or $taskProjectName -match '[\\/:*?"<>|]' -or
    $taskProjectName -eq '.' -or $taskProjectName -eq '..') { throw '工程名称为空或包含非法路径字符。' }
$taskSourcePath = Join-Path $taskRoot 'User\Log\fault_noinit.sct'
$taskDestinationDir = Join-Path $taskRoot 'MDK-ARM'
$taskDestinationPath = Join-Path $taskDestinationDir ($taskProjectName + '.sct')
if (-not (Test-Path -LiteralPath $taskSourcePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $taskDestinationDir -PathType Container)) {
    throw '缺少 User/Log/fault_noinit.sct 或 MDK-ARM 目录。'
}
$taskSource = [IO.File]::ReadAllText($taskSourcePath)
$taskRequired = @('RW_FAULT_NOINIT')
if (Test-Path -LiteralPath (Join-Path (Split-Path $taskSourcePath -Parent) 'spi_log_config.h')) {
    $taskRequired += 'RW_LOG_SPI_DMA'
}
foreach ($taskRegion in $taskRequired) {
    if ($taskSource -notmatch ('\b' + $taskRegion + '\b')) { throw "自定义 SCT 缺少必要区域：$taskRegion" }
}

# 复制到生成目录后包含路径会改变，按源文件目录解析并检查每个直接包含的头文件。
$taskSourceDir = Split-Path $taskSourcePath -Parent
$taskOutput = [regex]::Replace($taskSource, '(?m)^(\s*#include\s+)"([^"\r\n]+)"', [System.Text.RegularExpressions.MatchEvaluator]{
    param($taskMatch)
    $taskInclude = [IO.Path]::GetFullPath((Join-Path $taskSourceDir $taskMatch.Groups[2].Value))
    if (-not (Test-Path -LiteralPath $taskInclude -PathType Leaf)) { throw "SCT 包含的头文件不存在：$taskInclude" }
    return $taskMatch.Groups[1].Value + '"' + $taskInclude.Replace('\', '/') + '"'
})
if (-not (Test-Path -LiteralPath $taskDestinationPath) -or
    [IO.File]::ReadAllText($taskDestinationPath) -cne $taskOutput) {
    [IO.File]::WriteAllText($taskDestinationPath, $taskOutput, (New-Object Text.UTF8Encoding($false)))
}
Write-Output "Scatter synchronized: $taskDestinationPath"
