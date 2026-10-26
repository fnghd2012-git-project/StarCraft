param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug', [ValidateSet('IOCP','WSAPoll')][string]$NetworkMode='IOCP'
)

$ErrorActionPreference = 'Stop'
$binaryDirectory = Join-Path $PSScriptRoot "Run64\$Configuration"
$server = Join-Path $binaryDirectory 'MultiThreadServer.exe'
$dummy = Join-Path $binaryDirectory 'DummyClient.exe'
$testDirectory = Join-Path $PSScriptRoot ('MultiThreadServer\MultiThreadServer\build\network-check-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $testDirectory 'Data') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $binaryDirectory 'Cluster.local.ini') -Destination $testDirectory
foreach ($name in @('GameServer.csv', 'Unit.csv', 'Skill.csv', 'StartPoint.csv', 'Map.ini')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Data\$name") -Destination (Join-Path $testDirectory "Data\$name")
}

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed: $Program ($LASTEXITCODE)" }
}

function Test-RejectedData {
    param([string]$Name, [string]$Before, [string]$After)
    $path = Join-Path $testDirectory $Name
    $original = [IO.File]::ReadAllText($path)
    if (-not $original.Contains($Before)) { throw "Test input missing: $Name" }
    try {
        [IO.File]::WriteAllText($path, $original.Replace($Before, $After))
        $savedPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $result = & $server --config-check $gameServers[0].Index --config $configPath 2>&1
        $exitCode = $LASTEXITCODE
        $ErrorActionPreference = $savedPreference
        if ($exitCode -eq 0) { throw "Invalid data accepted: $Name" }
        Write-Output "[OK] Invalid data rejected: $Name"
    }
    finally { [IO.File]::WriteAllText($path, $original) }
}

function New-TestClient {
    param([int]$Port)
    $client = New-Object Net.Sockets.TcpClient
    $client.ReceiveTimeout = 3000
    $client.SendTimeout = 3000
    $client.Connect('127.0.0.1', $Port)
    return $client
}

function Read-Exact {
    param([IO.Stream]$Stream, [int]$Count)
    $buffer = New-Object byte[] $Count
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($buffer, $offset, $Count - $offset)
        if ($read -eq 0) { throw 'Unexpected EOF' }
        $offset += $read
    }
    return ,$buffer
}

# client_name="test", protocol_version=5. 생성 코드와 독립적인 원시 입력도 검증한다.
$versionCheckPacket = [byte[]]@(0,12,0,1,10,4,116,101,115,116,16,5)

function Read-ServerVersionCheck {
    param([IO.Stream]$Stream)
    $header = Read-Exact $Stream 4
    $length = ([int]$header[0] -shl 8) -bor $header[1]
    if ($length -le 4 -or $length -gt 4096 -or $header[2] -ne 0 -or $header[3] -ne 2) {
        throw 'Invalid ServerVersionCheckReply header'
    }
    # 실제 필드/좌표 비교는 같은 테스트에서 실행하는 C++ 더미가 수행한다.
    $null = Read-Exact $Stream ($length - 4)
}

function Test-BadPacket {
    param([int]$Port, [byte[]]$Packet, [string]$Description, [switch]$AfterServerVersionCheck)
    $client = New-TestClient $Port
    try {
        $stream = $client.GetStream()
        if ($AfterServerVersionCheck) {
            $stream.Write($versionCheckPacket, 0, $versionCheckPacket.Length)
            Read-ServerVersionCheck $stream
        }
        $stream.Write($Packet, 0, $Packet.Length)
        $closed = $false
        try { $closed = $stream.ReadByte() -eq -1 }
        catch [IO.IOException] {
            $socketError = $_.Exception.InnerException
            if ($socketError -is [Net.Sockets.SocketException] -and
                $socketError.SocketErrorCode -in @([Net.Sockets.SocketError]::ConnectionReset, [Net.Sockets.SocketError]::ConnectionAborted)) {
                $closed = $true
            }
            else { throw }
        }
        if (-not $closed) { throw "Malformed packet was not rejected: $Description" }
        Write-Output "[OK] Malformed packet rejected: $Description"
    }
    finally { $client.Dispose() }
}

$processes = @()
$reservations = @()
try {
    $serverCsv = Join-Path $testDirectory 'Data\GameServer.csv'
    $gameServers = @(Import-Csv -LiteralPath $serverCsv -Encoding UTF8)
    # 고정 개발 포트를 사용하지 않고 이번 테스트에 쓸 로컬 포트를 고른다.
    $ports = @()
    for ($i = 0; $i -lt $gameServers.Count + 1; ++$i) {
        $listener = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback, 0)
        $listener.Start()
        $reservations += $listener
        $ports += $listener.LocalEndpoint.Port
    }
    $config = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Server.ini'))
    for ($i = 0; $i -lt $gameServers.Count; ++$i) { $gameServers[$i].Port = [string]$ports[$i] }
    $gameServers | Export-Csv -LiteralPath $serverCsv -Encoding UTF8 -NoTypeInformation
    $config = $config.Replace('Port=7100', "Port=$($ports[-1])")
    $configPath = Join-Path $testDirectory 'Server.ini'
    [IO.File]::WriteAllText($configPath, $config.Replace('Mode=IOCP', "Mode=$NetworkMode"))
    Invoke-Checked $server @('--config-check', $gameServers[0].Index, '--config', $configPath)
    Test-RejectedData 'Data\Unit.csv' 'Ghost,7,45,200,100' 'Ghost,7,45,200,999'
    Test-RejectedData 'Data\Unit.csv' '7,0,1,0,0,0,0' '7,0,999,0,0,0,0'
    Test-RejectedData 'Data\Skill.csv' '2,2,Cloaking' '2,1,Cloaking'
    Test-RejectedData 'Data\StartPoint.csv' '2,1,Protoss,104,60' '2,1,Terran,104,60'
    Test-RejectedData 'Data\StartPoint.csv' '1,1,Terran,60,104' '1,1,Terran,60,500'
    Test-RejectedData 'Data\StartPoint.csv' '1,1,Terran,60,104' '1,1,Terran,-1,104'
    Test-RejectedData 'Data\StartPoint.csv' '1,1,Terran,60,104' '1,1,Terran,60,-1'
    Test-RejectedData 'Data\Map.ini' 'MinX=0' 'MinX=-1'
    Test-RejectedData 'Data\Map.ini' 'MinY=0' 'MinY=-1'
    Test-RejectedData 'Server.ini' 'WorkerCount=2' 'WorkerCount=0'
    Test-RejectedData 'Server.ini' 'PollTimeoutMs=5' 'PollTimeoutMs=0'
    Test-RejectedData 'Server.ini' 'QueueMB=8' 'QueueMB=0'
    Test-RejectedData 'Server.ini' 'MaxFileMB=128' 'MaxFileMB=1025'
    Test-RejectedData 'Server.ini' 'SnapshotSeconds=1' 'SnapshotSeconds=0'
    Test-RejectedData 'Server.ini' 'IntervalSeconds=5' 'IntervalSeconds=0'
    Test-RejectedData 'Data\GameServer.csv' '"B"' '"a"'
    Test-RejectedData 'Data\GameServer.csv' '"A"' '"world"'
    $startPath = Join-Path $testDirectory 'Data\StartPoint.csv'
    $originalStart = [IO.File]::ReadAllText($startPath)
    try {
        [IO.File]::WriteAllText($startPath, $originalStart.Replace('1,1,Terran,60,104', '1,1,Terran,0,0'))
        $null = & $server --config-check $gameServers[0].Index --config $configPath
        if ($LASTEXITCODE -ne 0) { throw 'Zero start coordinate was rejected' }
        Write-Output '[OK] Zero start coordinates accepted'
    }
    finally { [IO.File]::WriteAllText($startPath, $originalStart) }
    foreach ($listener in $reservations) { $listener.Stop() }
    $reservations = @()

    $ids = @($gameServers | ForEach-Object { $_.Index }) + @('World')
    for ($i = 0; $i -lt $ids.Count; ++$i) {
        $id = $ids[$i]
        # 테스트가 끝나면 서버가 자체 종료하여 워커 join과 소켓 정리를 함께 검증한다.
        $process = Start-Process -FilePath $server -ArgumentList @('--server', $id, '--config', ('"' + $configPath + '"'), '--run-seconds', '15') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $testDirectory "$id.out.txt") -RedirectStandardError (Join-Path $testDirectory "$id.err.txt")
        # Windows PowerShell에서 종료 코드 조회에 쓸 프로세스 핸들을 미리 유지한다.
        $null = $process.Handle
        $processes += $process
        $ready = $false
        for ($attempt = 0; $attempt -lt 100; ++$attempt) {
            if ($process.HasExited) { throw "Server $id exited before listen" }
            try {
                $probe = New-TestClient $ports[$i]
                $probe.Dispose()
                $ready = $true
                break
            }
            catch { Start-Sleep -Milliseconds 50 }
        }
        if (-not $ready) { throw "Server $id did not start" }
        Invoke-Checked $dummy @('--server', $id, '--config', $configPath, '--clients', '8', '--rounds', '10')
    }

    Test-BadPacket $ports[0] ([byte[]]@(0,0,0,1)) 'zero length'
    Test-BadPacket $ports[0] ([byte[]]@(0,3,0,1)) 'length below header'
    Test-BadPacket $ports[0] ([byte[]]@(16,1,0,1)) 'packet above 4096 bytes'
    Test-BadPacket $ports[0] ([byte[]]@(255,255,0,1)) 'unsigned 16-bit length above limit'
    Test-BadPacket $ports[0] ([byte[]]@(0,0,0,14,0,1,0,3,10,4,116,101,115,116)) 'legacy 8-byte frame'
    Test-BadPacket $ports[0] ([byte[]]@(0,4,0,3)) 'ping before version check'
    Test-BadPacket $ports[0] ([byte[]]@(0,4,0,99)) 'unknown type'
    Test-BadPacket $ports[0] ([byte[]]@(0,4,1,1)) 'unknown 16-bit type'
    foreach ($type in 5..9) {
        Test-BadPacket $ports[0] ([byte[]]@(0,4,0,$type)) "reserved system type $type" -AfterServerVersionCheck
    }
    Test-BadPacket $ports[0] $versionCheckPacket 'duplicate version check' -AfterServerVersionCheck
    Test-BadPacket $ports[0] ([byte[]]@(0,10,0,1,10,4,116,101,115,116)) 'missing protocol version'
    Test-BadPacket $ports[0] ([byte[]]@(0,12,0,1,10,4,116,101,115,116,16,4)) 'older protocol version'
    Test-BadPacket $ports[0] ([byte[]]@(0,12,0,1,10,4,116,101,115,116,16,6)) 'newer protocol version'
    Test-BadPacket $ports[0] ([byte[]]@(0,6,0,1,16,5)) 'missing client name'
    Test-BadPacket $ports[0] ([byte[]]@(0,7,0,1,10,5,65)) 'truncated protobuf string'
    Test-BadPacket $ports[0] ([byte[]]@(0,7,0,1,10,1,255)) 'invalid UTF-8 client name'
    Test-BadPacket $ports[0] ([byte[]]@(0,5,0,3,15)) 'invalid protobuf wire type' -AfterServerVersionCheck
    Test-BadPacket $ports[0] ([byte[]]@(0,7,0,3,18,1,65)) 'missing ping sequence' -AfterServerVersionCheck

    # 헤더 3바이트만 보낸 상태에서는 응답이 없어야 하고, 나머지가 오면 복원해야 한다.
    $fragmentClient = New-TestClient $ports[0]
    try {
        $stream = $fragmentClient.GetStream()
        $stream.Write($versionCheckPacket, 0, 3)
        Start-Sleep -Milliseconds 100
        if ($fragmentClient.Available -ne 0) { throw 'Partial header produced a reply' }
        $stream.Write($versionCheckPacket, 3, $versionCheckPacket.Length - 3)
        Read-ServerVersionCheck $stream
        Write-Output '[OK] Partial header accumulation and Protobuf response'
    }
    finally { $fragmentClient.Dispose() }
    # 헤더가 완성돼도 본문이 덜 왔으면 처리하지 않는다.
    $fragmentClient = New-TestClient $ports[0]
    try {
        $stream = $fragmentClient.GetStream()
        $stream.Write($versionCheckPacket, 0, 6)
        Start-Sleep -Milliseconds 100
        if ($fragmentClient.Available -ne 0) { throw 'Partial body produced a reply' }
        $stream.Write($versionCheckPacket, 6, $versionCheckPacket.Length - 6)
        Read-ServerVersionCheck $stream
        Write-Output '[OK] Partial body accumulation and Protobuf response'
    }
    finally { $fragmentClient.Dispose() }
    Invoke-Checked $dummy @('--server', $gameServers[0].Index, '--config', $configPath, '--clients', '16', '--rounds', '10')
    foreach ($process in $processes) {
        if (-not $process.WaitForExit(20000)) { throw 'Server did not stop cleanly' }
        $process.WaitForExit()
        if ($null -eq $process.ExitCode -or $process.ExitCode -ne 0) { throw "Server failed with exit code $($process.ExitCode)" }
    }
    Write-Output "[OK] $Configuration network checks passed. Logs: $testDirectory"
}
finally {
    foreach ($listener in $reservations) { $listener.Stop() }
    foreach ($process in $processes) {
        # 이 스크립트에서 시작한 프로세스만 실패 시 정리한다.
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        $process.Dispose()
    }
}
