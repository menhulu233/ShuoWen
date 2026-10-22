[CmdletBinding()]
param(
    [string]$ModelsRoot = "D:\workspace\X-ASR\X-ASR-zh-en\deployment\models",
    [string]$SherpaDir = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root "dist"
$sherpaDlls = @("sherpa-onnx-c-api.dll", "onnxruntime.dll")

New-Item -ItemType Directory -Force -Path $dist | Out-Null

if (-not $SkipBuild) {
    Write-Host "[1/4] Building engine..."
    & cmd /c "`"$root\scripts\_build_engine.bat`""
    if ($LASTEXITCODE -ne 0) { throw "engine build failed ($LASTEXITCODE)" }

    Write-Host "[2/4] Building Flutter UI..."
    Push-Location (Join-Path $root "ui")
    try {
        & flutter build windows --release
        if ($LASTEXITCODE -ne 0) { throw "flutter build failed ($LASTEXITCODE)" }
    } finally {
        Pop-Location
    }
}

Write-Host "[3/4] Staging binaries..."
Copy-Item (Join-Path $root "engine\build\bin\shuowen_engine.exe") $dist -Force
Copy-Item (Join-Path $root "engine\build\bin\shuowen_selftest.exe") $dist -Force
Copy-Item -Recurse -Force (Join-Path $root "ui\build\windows\x64\runner\Release\*") $dist

if (-not $SherpaDir) {
    $candidates = @(
        (Join-Path $root "engine\build\bin"),
        "D:\workspace\X-ASR\projects\Vibe_XASR\windows_build\src\VibeXASR.Windows\bin\Debug\net8.0-windows\runtimes\win-x64\native",
        "D:\workspace\X-ASR\projects\Vibe_XASR\windows_build\src\VibeXASR.Tests\bin\Debug\net10.0-windows\runtimes\win-x64\native"
    )
    $SherpaDir = $candidates | Where-Object {
        $ok = $true
        foreach ($dll in $sherpaDlls) { $ok = $ok -and (Test-Path (Join-Path $_ $dll)) }
        $ok
    } | Select-Object -First 1
}
if (-not $SherpaDir) {
    throw "sherpa-onnx runtime not found. Pass -SherpaDir with sherpa-onnx-c-api.dll and onnxruntime.dll."
}
foreach ($dll in $sherpaDlls) {
    Copy-Item (Join-Path $SherpaDir $dll) $dist -Force
}

Write-Host "[4/4] Staging models..."
$tierSource = Join-Path $ModelsRoot "chunk-960ms-model"
if (-not (Test-Path $tierSource)) {
    throw "default model tier not found: $tierSource"
}
$modelsDir = Join-Path $dist "models"
New-Item -ItemType Directory -Force -Path $modelsDir | Out-Null
$tierTarget = Join-Path $modelsDir "chunk-960ms-model"
if (Test-Path $tierTarget) {
    $resolvedTarget = [IO.Path]::GetFullPath($tierTarget).TrimEnd("\")
    $resolvedModels = [IO.Path]::GetFullPath($modelsDir).TrimEnd("\") + "\"
    if (-not $resolvedTarget.StartsWith($resolvedModels,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace an unexpected model path: $resolvedTarget"
    }
    $item = Get-Item -LiteralPath $resolvedTarget -Force
    if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
        # Remove the junction only; never recurse into the X-ASR checkout.
        [System.IO.Directory]::Delete($resolvedTarget, $false)
    } else {
        [System.IO.Directory]::Delete($resolvedTarget, $true)
    }
}
Copy-Item -Recurse -Force $tierSource $tierTarget

$vad = Join-Path $modelsDir "silero_vad.onnx"
$vadOk = (Test-Path $vad) -and ((Get-Item $vad).Length -gt 1000000)
if (-not $vadOk) {
    Write-Host "Downloading Silero VAD V4..."
    try {
        Invoke-WebRequest `
            -Uri "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad_v4.onnx" `
            -OutFile $vad
    } catch {
        Write-Warning "VAD download failed; engine will fall back to energy VAD. $($_.Exception.Message)"
    }
}

$installerTarget = Join-Path $dist "installer"
New-Item -ItemType Directory -Force -Path $installerTarget | Out-Null
Copy-Item (Join-Path $root "installer\install.ps1") $installerTarget -Force
Copy-Item (Join-Path $root "installer\uninstall.ps1") $installerTarget -Force
Copy-Item (Join-Path $root "installer\README.md") $installerTarget -Force

@"
ShuoWen 说文 v0.1.0

Run:  shuowen_engine.exe
Hotkey: hold Right Ctrl, speak, release to insert.
Tray: open panel / settings / history / quit.
Data: %APPDATA%\ShuoWen
Install: run installer\install.ps1 (per-user, no admin required)
"@ | Set-Content -Encoding UTF8 (Join-Path $dist "QUICKSTART.txt")

Write-Host "Package ready: $dist"
