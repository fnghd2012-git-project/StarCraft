param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$binary = Join-Path $PSScriptRoot "Run64\$Configuration\MultiThreadServer.exe"
$testDirectory = Join-Path $PSScriptRoot ('MultiThreadServer\MultiThreadServer\build\chat-relay-check-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $testDirectory 'Data') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Run64\$Configuration\Cluster.local.ini") -Destination $testDirectory
foreach ($file in @('Unit.csv', 'Skill.csv', 'StartPoint.csv', 'Map.ini')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Data\$file") -Destination (Join-Path $testDirectory 'Data')
}
$ownedProcesses = [Collections.Generic.List[Diagnostics.Process]]::new()
$clients = [Collections.Generic.List[Net.Sockets.TcpClient]]::new()
$listeners = @()

function Read-Exact([IO.Stream]$Stream, [int]$Count) {
    $bytes = [byte[]]::new($Count)
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($bytes, $offset, $Count - $offset)
        if ($read -eq 0) { throw 'Connection closed during packet read' }
        $offset += $read
    }
    return ,$bytes
}
function Read-Packet([Net.Sockets.TcpClient]$Client, [int]$ExpectedType) {
    $stream = $Client.GetStream()
    $header = Read-Exact $stream 4
    $size = ([int]$header[0] -shl 8) -bor $header[1]
    $type = ([int]$header[2] -shl 8) -bor $header[3]
    if ($size -lt 4 -or $size -gt 4096 -or $type -ne $ExpectedType) { throw "Unexpected packet: $type / $size" }
    return ,(Read-Exact $stream ($size - 4))
}
function Write-Packet([Net.Sockets.TcpClient]$Client, [int]$Type, [byte[]]$Body) {
    $size = 4 + $Body.Length
    $header = [byte[]]@((($size -shr 8) -band 255), ($size -band 255), (($Type -shr 8) -band 255), ($Type -band 255))
    $Client.GetStream().Write($header, 0, $header.Length)
    $Client.GetStream().Write($Body, 0, $Body.Length)
}
function Text-Field([string]$Text) {
    # 여기서는 짧은 ASCII 문자열을 사용한다. UTF-8/최대 길이는 C++ 통합 테스트에서 검사한다.
    $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
    if ($bytes.Length -gt 127) { throw 'Probe text too long' }
    return ,([byte[]]@(10, $bytes.Length) + $bytes)
}
function Send-Chat([Net.Sockets.TcpClient]$Client, [string]$Text, [bool]$Global, [bool]$Expected) {
    $body = Text-Field $Text
    if ($Global) { $body += [byte[]]@(16, 1) }
    Write-Packet $Client 25 $body
    $reply = Read-Packet $Client 21
    $ok = $reply.Length -ge 2 -and $reply[0] -eq 8 -and $reply[1] -eq 1
    if ($ok -ne $Expected) { throw "Chat result mismatch: $Text" }
    if (-not $Expected -and -not [Text.Encoding]::UTF8.GetString($reply).Contains('unavailable')) {
        throw 'Expected an unavailable relay error'
    }
}
function Read-Chat([Net.Sockets.TcpClient]$Client, [string]$Text) {
    $body = Read-Packet $Client 26
    if (-not [Text.Encoding]::UTF8.GetString($body).Contains($Text)) { throw 'Chat probe text mismatch' }
}
function Start-TestServer([string]$Id, [int]$Port, [string]$LogName) {
    $process = Start-Process -FilePath $binary -ArgumentList @('--server', $Id, '--config', ('"' + $configPath + '"'), '--run-seconds', '60') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $testDirectory "$LogName.out.txt") -RedirectStandardError (Join-Path $testDirectory "$LogName.err.txt")
    $null = $process.Handle
    $ownedProcesses.Add($process)
    for ($attempt = 0; $attempt -lt 100; ++$attempt) {
        if ($process.HasExited) { throw "Server $Id stopped" }
        $probe = [Net.Sockets.TcpClient]::new()
        try { $probe.Connect('127.0.0.1', $Port); return $process }
        catch { Start-Sleep -Milliseconds 50 }
        finally { $probe.Dispose() }
    }
    throw "Server $Id startup timed out"
}
function Wait-Relays([int]$Count) {
    foreach ($id in @('1', '2')) {
        $ready = $false
        for ($attempt = 0; $attempt -lt 100; ++$attempt) {
            $stream = [IO.File]::Open((Join-Path $testDirectory "$id.out.txt"), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
            $reader = [IO.StreamReader]::new($stream)
            try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
            if ([regex]::Matches($text, 'connected to World').Count -ge $Count) { $ready = $true; break }
            Start-Sleep -Milliseconds 50
        }
        if (-not $ready) { throw "Relay $id did not reconnect" }
    }
}
try {
    $ports = @()
    for ($i = 0; $i -lt 3; ++$i) {
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
        $listener.Start(); $listeners += $listener; $ports += $listener.LocalEndpoint.Port
    }
    $configPath = Join-Path $testDirectory 'Server.ini'
    $config = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Server.ini')).Replace('Port=7100', "Port=$($ports[2])")
    $config = $config.Replace('Realm=local', ('Realm=test_' + [Guid]::NewGuid().ToString('N')))
    [IO.File]::WriteAllText($configPath, $config)
    $rows = @('Index,Name,Race,Address,Port', "1,Game1,Zerg,127.0.0.1,$($ports[0])", "2,Game2,Terran,127.0.0.1,$($ports[1])")
    [IO.File]::WriteAllLines((Join-Path $testDirectory 'Data\GameServer.csv'), $rows)
    foreach ($listener in $listeners) { $listener.Stop() }
    $listeners = @()
    for ($i = 0; $i -lt 2; ++$i) {
        $null = Start-TestServer ([string]($i + 1)) $ports[$i] ([string]($i + 1))
        $client = [Net.Sockets.TcpClient]::new()
        $clients.Add($client)
        $client.ReceiveTimeout = 5000; $client.SendTimeout = 3000
        $client.Connect('127.0.0.1', $ports[$i])
        Write-Packet $client 1 ((Text-Field 'RelayProbe') + [byte[]]@(16,5))
        $null = Read-Packet $client 2
        $password = Text-Field 'PortfolioTest!2026'; $password[0] = 18
        Write-Packet $client 10 ((Text-Field "RelayProbe$i") + $password)
        $null = Read-Packet $client 11
    }
    Send-Chat $clients[0] 'local-before-world' $false $true
    Read-Chat $clients[0] 'local-before-world'
    Start-Sleep -Milliseconds 600
    Send-Chat $clients[0] 'offline-world-chat' $true $false
    $world = Start-TestServer 'World' $ports[2] 'World'
    Wait-Relays 1
    Send-Chat $clients[0] 'global-before-restart' $true $true
    foreach ($client in $clients) { Read-Chat $client 'global-before-restart' }
    # 이 테스트가 만든 World만 종료한다. 게임 서버와 클라이언트 연결은 계속 유지한다.
    Stop-Process -Id $world.Id -Force
    $world.WaitForExit()
    Start-Sleep -Milliseconds 1000
    Send-Chat $clients[0] 'local-during-outage' $false $true
    Read-Chat $clients[0] 'local-during-outage'
    Start-Sleep -Milliseconds 600
    Send-Chat $clients[0] 'offline-after-stop' $true $false
    $null = Start-TestServer 'World' $ports[2] 'World-restarted'
    Wait-Relays 2
    Send-Chat $clients[1] 'global-after-reconnect' $true $true
    foreach ($client in $clients) { Read-Chat $client 'global-after-reconnect' }
    Write-Output "[OK] $Configuration relay outage/restart: local chat stays available, global fails offline, reconnect restores delivery. Logs: $testDirectory"
}
finally {
    foreach ($client in $clients) { $client.Dispose() }
    foreach ($listener in $listeners) { $listener.Stop() }
    foreach ($process in $ownedProcesses) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        $process.Dispose()
    }
}
