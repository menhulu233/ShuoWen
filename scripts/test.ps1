[CmdletBinding()]
param(
    [string[]]$Only = @("engine", "ui", "ipc", "window"),
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$results = [ordered]@{}
$failed = $false

function Want([string]$name) { return $Only -contains $name }

if ($Build) {
    Write-Host "== build (package.ps1) =="
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "package.ps1")
    if ($LASTEXITCODE -ne 0) { throw "package.ps1 failed ($LASTEXITCODE)" }
}

if (Want "engine") {
    Write-Host "== engine: selftest =="
    $selftest = Join-Path $root "dist\shuowen_selftest.exe"
    if (-not (Test-Path $selftest)) {
        $selftest = Join-Path $root "engine\build\bin\shuowen_selftest.exe"
    }
    if (-not (Test-Path $selftest)) {
        $results.engine = "SKIP (selftest missing; run with -Build)"
    } else {
        $env:SHUOWEN_MODELS_DIR = Join-Path $root "dist\models"
        $env:SHUOWEN_TEST_WAV_DIR = Join-Path $root "engine\tests\fixtures"
        & $selftest
        $code = $LASTEXITCODE
        if ($code -eq 0) { $results.engine = "PASS" } else { $results.engine = "FAIL"; $failed = $true }
    }
}

if (Want "ui") {
    Write-Host "== ui: analyze + test =="
    Push-Location (Join-Path $root "ui")
    try {
        & flutter analyze
        $analyze = $LASTEXITCODE
        & flutter test
        $test = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($analyze -eq 0 -and $test -eq 0) { $results.ui = "PASS" }
    else { $results.ui = "FAIL"; $failed = $true }
}

if (Want "ipc") {
    Write-Host "== ipc: smoke =="
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "_ipc_smoke.ps1")
    $code = $LASTEXITCODE
    if ($code -eq 0) { $results.ipc = "PASS" } else { $results.ipc = "FAIL"; $failed = $true }
}

if (Want "window") {
    Write-Host "== window: hud smoke =="
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "_hud_window_smoke.ps1")
    $code = $LASTEXITCODE
    if ($code -eq 0) { $results.window = "PASS" } else { $results.window = "FAIL"; $failed = $true }
}

Write-Host ""
Write-Host "== summary =="
foreach ($key in $results.Keys) { Write-Host ("{0,-8} {1}" -f $key, $results[$key]) }
if ($failed) { exit 1 }
exit 0
