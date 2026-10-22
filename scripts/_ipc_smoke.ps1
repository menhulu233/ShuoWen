[CmdletBinding()]
param(
    [string]$EngineExe = "",
    [switch]$KeepEngine
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $EngineExe) { $EngineExe = Join-Path $root "dist\shuowen_engine.exe" }

function Connect-Pipe {
    param([int]$TimeoutMs = 5000)
    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(".", "shuowen.engine", [System.IO.Pipes.PipeDirection]::InOut)
    try {
        $pipe.Connect($TimeoutMs)
    } catch {
        $pipe.Dispose()
        throw
    }
    return $pipe
}

# Requests and broadcasts share one pipe. Keep reading until a line that
# carries the matching response id, otherwise a broadcast event (shutdown,
# state, ...) would be mistaken for the response we are waiting for.
function Read-Response {
    param($Reader, [int]$Id)
    while ($true) {
        $line = $Reader.ReadLine()
        if ($null -eq $line) { throw "pipe closed while waiting for response id=$Id" }
        $obj = $line | ConvertFrom-Json
        if ($obj.PSObject.Properties.Name -contains "id" -and $obj.id -eq $Id) { return $obj }
    }
}

$pipe = $null
$engine = $null
$started = $false
try {
    try { $pipe = Connect-Pipe -TimeoutMs 1500 } catch { $pipe = $null }
    if ($null -eq $pipe) {
        if (-not (Test-Path $EngineExe)) {
            throw "engine not found: $EngineExe (start shuowen_engine.exe manually)"
        }
        $engine = Start-Process -FilePath $EngineExe -WorkingDirectory (Split-Path -Parent $EngineExe) -PassThru -WindowStyle Hidden
        $started = $true
        $deadline = (Get-Date).AddSeconds(20)
        while ((Get-Date) -lt $deadline) {
            try { $pipe = Connect-Pipe -TimeoutMs 1000; break } catch { Start-Sleep -Milliseconds 300 }
        }
        if ($null -eq $pipe) { throw "engine did not open \\.\pipe\shuowen.engine" }
    }

    $writer = [System.IO.StreamWriter]::new($pipe)
    $writer.AutoFlush = $true
    $reader = [System.IO.StreamReader]::new($pipe)

    $writer.WriteLine('{"id":1,"cmd":"hello","args":{"window":"settings"}}')
    $hello = Read-Response -Reader $reader -Id 1
    if (-not $hello.ok) { throw "hello failed: $($hello.error.message)" }
    $data = $hello.data

    Write-Output ("version=" + $data.version)
    Write-Output ("status=" + $data.engine.status)
    Write-Output ("tier=" + $data.settings.Tier)
    Write-Output ("mode=" + $data.settings.Mode)
    Write-Output ("vad=" + $data.engine.vad)
    Write-Output ("tiers=" + $data.models.tiers.Count)
    Write-Output ("hudVisible=" + $data.hud.visible)
    Write-Output ("lifetimeChars=" + $data.history.lifetimeChars)

    $writer.WriteLine('{"id":2,"cmd":"engine.status"}')
    $status = Read-Response -Reader $reader -Id 2
    Write-Output ("engine.status.ok=" + $status.ok)

    $writer.WriteLine('{"id":3,"cmd":"settings.get"}')
    $settings = Read-Response -Reader $reader -Id 3
    Write-Output ("settings.get.ok=" + $settings.ok)

    if ($started -and -not $KeepEngine) {
        $writer.WriteLine('{"id":4,"cmd":"app.quit"}')
        $quit = Read-Response -Reader $reader -Id 4
        Write-Output ("quit=" + $quit.ok)
    } else {
        Write-Output "quit=skipped (engine was already running)"
    }
} finally {
    if ($pipe) { $pipe.Dispose() }
    if ($engine -and -not $engine.HasExited) { Stop-Process -Id $engine.Id -Force }
}
