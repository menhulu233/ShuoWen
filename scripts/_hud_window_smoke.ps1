[CmdletBinding()]
param(
    [string]$EngineExe = "",
    [string]$UiExe = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $EngineExe) { $EngineExe = Join-Path $root "dist\shuowen_engine.exe" }
if (-not $UiExe) { $UiExe = Join-Path $root "dist\shuowen_ui.exe" }

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class SwWin {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextLength(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hgt, uint flags);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool GetLayeredWindowAttributes(IntPtr h, out uint key, out byte alpha, out uint flags);
  [DllImport("user32.dll")] public static extern int GetWindowRgnBox(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@

function Find-HudWindow {
    $script:hud = [IntPtr]::Zero
    $cb = [SwWin+EnumProc]{
        param($h, $l)
        $len = [SwWin]::GetWindowTextLength($h)
        if ($len -gt 0) {
            $sb = New-Object System.Text.StringBuilder ($len + 1)
            [void][SwWin]::GetWindowText($h, $sb, $sb.Capacity)
            if ($sb.ToString() -eq "ShuoWen HUD") { $script:hud = $h }
        }
        return $true
    }
    [void][SwWin]::EnumWindows($cb, [IntPtr]::Zero)
    return $script:hud
}

$script:failures = 0
function Assert([bool]$condition, [string]$name) {
    if ($condition) { Write-Output "PASS  $name" }
    else { Write-Output "FAIL  $name"; $script:failures++ }
}

$engine = Get-Process shuowen_engine -ErrorAction SilentlyContinue | Select-Object -First 1
$startedEngine = $null
if (-not $engine) {
    if (-not (Test-Path $EngineExe) -or -not (Test-Path $UiExe)) {
        throw "dist binaries missing; run scripts\package.ps1 first"
    }
    $startedEngine = Start-Process -FilePath $EngineExe -WorkingDirectory (Split-Path -Parent $EngineExe) -PassThru -WindowStyle Hidden
}

$hud = [IntPtr]::Zero
$deadline = (Get-Date).AddSeconds(15)
while ((Get-Date) -lt $deadline) {
    $hud = Find-HudWindow
    if ($hud -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds 250
}
if ($hud -eq [IntPtr]::Zero) { throw "HUD window not found within 15s" }

$hudPid = 0
[void][SwWin]::GetWindowThreadProcessId($hud, [ref]$hudPid)

$ex = [SwWin]::GetWindowLong($hud, -20)
Assert (($ex -band 0x80000) -ne 0) "hud has WS_EX_LAYERED"
Assert (($ex -band 0x20) -ne 0) "hud has WS_EX_TRANSPARENT"
Assert (($ex -band 0x08000000) -ne 0) "hud has WS_EX_NOACTIVATE"

$key = 0
$alpha = 0
$flags = 0
$layered = [SwWin]::GetLayeredWindowAttributes($hud, [ref]$key, [ref]$alpha, [ref]$flags)
Assert ($layered -and $alpha -eq 255 -and $flags -eq 2) "hud layered attributes are opaque (black-window regression)"

$rect = New-Object SwWin+RECT
[void][SwWin]::GetWindowRect($hud, [ref]$rect)
$dpi = [SwWin]::GetDpiForWindow($hud)
$scale = $dpi / 96.0
$width = $rect.R - $rect.L
$height = $rect.B - $rect.T
# window_manager may apply the logical size either directly or scaled by the
# monitor DPI; accept both so the check only rejects a genuinely wrong size.
$widthOk = ([Math]::Abs($width - 500) -le 2) -or ([Math]::Abs($width - 500 * $scale) -le 2)
$heightOk = ([Math]::Abs($height - 104) -le 2) -or ([Math]::Abs($height - 104 * $scale) -le 2)
Assert $widthOk "hud width is 500 logical px (got $width @ $scale x)"
Assert $heightOk "hud height is 104 logical px (got $height @ $scale x)"

$rgnBox = New-Object SwWin+RECT
$rgnType = [SwWin]::GetWindowRgnBox($hud, [ref]$rgnBox)
Assert ($rgnType -ne 0) "hud has a rounded window region (type=$rgnType)"

[void][SwWin]::SetWindowPos($hud, [IntPtr](-1), $rect.L, $rect.T, 0, 0, 0x0001 -bor 0x0002 -bor 0x0010 -bor 0x0040)
Start-Sleep -Seconds 2
Assert ([SwWin]::IsWindowVisible($hud)) "hud becomes visible when shown"

Add-Type -AssemblyName System.Drawing
$shot = New-Object System.Drawing.Bitmap($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($shot)
$hdc = $graphics.GetHdc()
$printed = [SwWin]::PrintWindow($hud, $hdc, 2)
$graphics.ReleaseHdc($hdc)
$graphics.Dispose()
$accent = 0
for ($x = 0; $x -lt $width; $x += 2) {
    for ($y = 0; $y -lt $height; $y += 2) {
        $c = $shot.GetPixel($x, $y)
        $a1 = [Math]::Abs($c.R - 0x7C) -le 40 -and [Math]::Abs($c.G - 0x5C) -le 40 -and [Math]::Abs($c.B - 0xFF) -le 40
        $a2 = [Math]::Abs($c.R - 0x38) -le 40 -and [Math]::Abs($c.G - 0xE1) -le 40 -and [Math]::Abs($c.B - 0xD6) -le 40
        if ($a1 -or $a2) { $accent++ }
    }
}
$shotPath = Join-Path $env:TEMP "shuowen_hud_smoke.png"
$shot.Save($shotPath)
$shot.Dispose()
Assert ($printed -and $accent -gt 50) "hud renders accent pixels (found $accent, screenshot $shotPath)"

[void][SwWin]::ShowWindow($hud, 0)

if ($startedEngine) {
    Stop-Process -Id $startedEngine.Id -Force -ErrorAction SilentlyContinue
    Stop-Process -Id $hudPid -Force -ErrorAction SilentlyContinue
}

if ($script:failures -gt 0) { throw "$($script:failures) HUD window assertion(s) failed" }
Write-Output "HUD window smoke passed"
