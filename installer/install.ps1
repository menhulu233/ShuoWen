[CmdletBinding()]
param(
    [string]$Source = "",
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\ShuoWen"),
    [switch]$NoAutostart,
    [switch]$DesktopShortcut,
    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"
if (-not $Source) {
    $packageRoot = Split-Path -Parent $PSScriptRoot
    $candidate = Join-Path $packageRoot "dist"
    if (Test-Path (Join-Path $candidate "shuowen_engine.exe")) {
        $Source = $candidate
    } elseif (Test-Path (Join-Path $packageRoot "shuowen_engine.exe")) {
        $Source = $packageRoot
    } else {
        $Source = $candidate
    }
}
$engine = Join-Path $Source "shuowen_engine.exe"
if (-not (Test-Path $engine)) {
    throw "Package not found: $engine. Run scripts\package.ps1 first."
}

if (Get-Process shuowen_engine -ErrorAction SilentlyContinue) {
    Write-Warning "A ShuoWen engine is already running. Quit it from the tray before using the installed copy."
}

Write-Host "Installing to $InstallDir"
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item -Recurse -Force (Join-Path $Source "*") $InstallDir
Copy-Item (Join-Path $PSScriptRoot "uninstall.ps1") $InstallDir -Force
Set-Content -LiteralPath (Join-Path $InstallDir ".shuowen-install") `
    -Value "ShuoWen user install" -Encoding ASCII

$target = Join-Path $InstallDir "shuowen_engine.exe"
$icon = Join-Path $InstallDir "assets\shuowen.ico"
$shell = New-Object -ComObject WScript.Shell

function New-Shortcut([string]$Path) {
    $shortcut = $shell.CreateShortcut($Path)
    $shortcut.TargetPath = $target
    $shortcut.WorkingDirectory = $InstallDir
    if (Test-Path $icon) { $shortcut.IconLocation = $icon }
    $shortcut.Description = "说文本地语音输入法"
    $shortcut.Save()
}

function Set-LaunchAtLogin([bool]$Enabled) {
    $dataDir = Join-Path $env:APPDATA "ShuoWen"
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
    $settingsPath = Join-Path $dataDir "settings.json"
    $settings = $null
    if (Test-Path -LiteralPath $settingsPath) {
        try {
            $settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
        } catch {
            Write-Warning "Could not parse $settingsPath; LaunchAtLogin was not changed."
            return
        }
    }
    if ($null -eq $settings -or $settings -isnot [pscustomobject]) {
        $settings = [pscustomobject]@{}
    }
    if ($settings.PSObject.Properties.Name -contains "LaunchAtLogin") {
        $settings.LaunchAtLogin = $Enabled
    } else {
        $settings | Add-Member -NotePropertyName "LaunchAtLogin" `
            -NotePropertyValue $Enabled
    }
    $json = $settings | ConvertTo-Json -Depth 100
    [IO.File]::WriteAllText($settingsPath, $json + "`r`n",
        (New-Object Text.UTF8Encoding($false)))
}

$programs = [Environment]::GetFolderPath("Programs")
New-Shortcut (Join-Path $programs "说文 ShuoWen.lnk")
if ($DesktopShortcut) {
    New-Shortcut (Join-Path ([Environment]::GetFolderPath("Desktop")) "说文 ShuoWen.lnk")
}

if ($NoAutostart) {
    Set-LaunchAtLogin $false
} else {
    $run = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
    New-Item -Path $run -Force | Out-Null
    Set-ItemProperty -Path $run -Name "ShuoWen" `
        -Value ('"' + $target + '" --autostart')
}

$uninstallScript = Join-Path $InstallDir "uninstall.ps1"
$uninstallCommand = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' +
    $uninstallScript + '" -InstallDir "' + $InstallDir + '"'
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ShuoWen"
New-Item -Path $uninstallKey -Force | Out-Null
$estimatedKb = [Math]::Round(((Get-ChildItem -LiteralPath $InstallDir -Recurse -File |
    Measure-Object -Property Length -Sum).Sum) / 1KB)
$iconPath = if (Test-Path $icon) { $icon } else { $target }
$values = @{
    DisplayName = "说文 ShuoWen"
    DisplayVersion = "0.1.0"
    Publisher = "ShuoWen"
    InstallLocation = $InstallDir
    DisplayIcon = $iconPath
    UninstallString = $uninstallCommand
    QuietUninstallString = $uninstallCommand
    NoModify = 1
    NoRepair = 1
    EstimatedSize = [int]$estimatedKb
}
foreach ($name in $values.Keys) {
    New-ItemProperty -Path $uninstallKey -Name $name -Value $values[$name] -Force | Out-Null
}

if (-not $NoLaunch) {
    Start-Process -FilePath $target -WorkingDirectory $InstallDir
}
Write-Host "Installed. Hotkey: hold Right Ctrl. Tray: settings / history / quit."
Write-Host "Uninstall from Windows Settings > Apps, or run $uninstallScript"
