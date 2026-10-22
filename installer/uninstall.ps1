[CmdletBinding()]
param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\ShuoWen")
)

$ErrorActionPreference = "Stop"
$resolved = [IO.Path]::GetFullPath($InstallDir).TrimEnd("\")
if ($resolved.Length -lt 4 -or $resolved -eq [IO.Path]::GetPathRoot($resolved)) {
    throw "Refusing to remove a filesystem root: $resolved"
}
$marker = Join-Path $resolved ".shuowen-install"
$engine = Join-Path $resolved "shuowen_engine.exe"
if (-not (Test-Path -LiteralPath $marker) -or -not (Test-Path -LiteralPath $engine)) {
    throw "Refusing to remove an unmarked directory: $resolved"
}

Get-Process shuowen_engine, shuowen_ui -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" `
    -Name "ShuoWen" -ErrorAction SilentlyContinue
Remove-Item -LiteralPath "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ShuoWen" `
    -Recurse -Force -ErrorAction SilentlyContinue

$programs = [Environment]::GetFolderPath("Programs")
Remove-Item -LiteralPath (Join-Path $programs "说文 ShuoWen.lnk") -Force -ErrorAction SilentlyContinue
$desktop = [Environment]::GetFolderPath("Desktop")
Remove-Item -LiteralPath (Join-Path $desktop "说文 ShuoWen.lnk") -Force -ErrorAction SilentlyContinue

if (Test-Path -LiteralPath $resolved) {
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

Write-Host "Removed $resolved"
Write-Host "User data kept in $env:APPDATA\ShuoWen (settings, history, models)."
