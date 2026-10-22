[CmdletBinding()]
param(
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
if (-not $OutDir) {
    $OutDir = Join-Path (Split-Path -Parent $PSScriptRoot) "engine\tests\fixtures"
}
Add-Type -AssemblyName System.Speech
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function New-Fixture {
    param([string]$Text, [string]$Voice, [string]$FileName)
    $path = Join-Path $OutDir $FileName
    $synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
    try {
        $synth.SelectVoice($Voice)
        $format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(
            16000,
            [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
            [System.Speech.AudioFormat.AudioChannel]::Mono)
        $synth.SetOutputToWaveFile($path, $format)
        $synth.Speak($Text)
    } finally {
        $synth.Dispose()
    }
    $size = (Get-Item $path).Length
    if ($size -lt 10000) { throw "fixture too small: $path ($size bytes)" }
    Write-Output "wrote $FileName ($size bytes)"
}

New-Fixture -Text "今天天气不错" -Voice "Microsoft Huihui Desktop" -FileName "zh_greeting.wav"
New-Fixture -Text "hello world" -Voice "Microsoft Zira Desktop" -FileName "en_hello.wav"
