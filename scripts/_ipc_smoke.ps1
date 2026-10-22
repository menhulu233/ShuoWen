$ErrorActionPreference = "Stop"
$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(".", "shuowen.engine", [System.IO.Pipes.PipeDirection]::InOut)
$pipe.Connect(5000)
$writer = [System.IO.StreamWriter]::new($pipe)
$writer.AutoFlush = $true
$reader = [System.IO.StreamReader]::new($pipe)

$writer.WriteLine('{"id":1,"cmd":"hello","args":{"window":"settings"}}')
$hello = $reader.ReadLine() | ConvertFrom-Json
Write-Output ("status=" + $hello.engine.status)
Write-Output ("tier=" + $hello.engine.tier)
Write-Output ("mode=" + $hello.settings.Mode)
Write-Output ("tiers=" + $hello.models.tiers.Count)
Write-Output ("hudVisible=" + $hello.hud.visible)
Write-Output ("lifetimeChars=" + $hello.history.lifetimeChars)

$writer.WriteLine('{"id":2,"cmd":"app.quit"}')
$quit = $reader.ReadLine() | ConvertFrom-Json
Write-Output ("quit=" + $quit.ok)
$pipe.Dispose()
