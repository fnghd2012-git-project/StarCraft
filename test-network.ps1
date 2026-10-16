param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$binaryDirectory = Join-Path $PSScriptRoot "MultiThreadServer\MultiThreadServer\build\x64\$Configuration\bin"
$server = Join-Path $binaryDirectory 'MultiThreadServer.exe'
$dummy = Join-Path $binaryDirectory 'DummyClient.exe'
$testDirectory = Join-Path $PSScriptRoot ('MultiThreadServer\MultiThreadServer\build\network-check-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $testDirectory 'Data') -Force | Out-Null
foreach ($name in @('Unit.csv', 'Skill.csv', 'StartPoint.csv', 'Map.ini')) {
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
        $result = & $server --config-check A --config $configPath 2>&1
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

# client_name="test"를 담은 유효한 Protobuf HelloRequest. 생성 코드와 독립적인 입력도 검증한다.
$helloPacket = [byte[]]@(0,0,0,14,0,1,0,2,10,4,116,101,115,116)

function Read-Hello {
    param([IO.Stream]$Stream)
    $header = Read-Exact $Stream 8
    $length = ($header[0] -shl 24) -bor ($header[1] -shl 16) -bor ($header[2] -shl 8) -bor $header[3]
    if ($length -le 8 -or $length -gt 4096 -or $header[4] -ne 0 -or $header[5] -ne 2 -or $header[6] -ne 0 -or $header[7] -ne 2) {
        throw 'Invalid HelloReply header'
    }
    # 실제 필드/좌표 비교는 같은 테스트에서 실행하는 C++ 더미가 수행한다.
    $null = Read-Exact $Stream ($length - 8)
}

function Test-BadPacket {
    param([int]$Port, [byte[]]$Packet, [string]$Description, [switch]$AfterHello)
    $client = New-TestClient $Port
    try {
        $stream = $client.GetStream()
        if ($AfterHello) {
            $stream.Write($helloPacket, 0, $helloPacket.Length)
            Read-Hello $stream
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
    # 고정 개발 포트를 사용하지 않고 이번 테스트에 쓸 로컬 포트를 고른다.
    $ports = @()
    for ($i = 0; $i -lt 4; ++$i) {
        $listener = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback, 0)
        $listener.Start()
        $reservations += $listener
        $ports += $listener.LocalEndpoint.Port
    }
    $config = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Server.ini'))
    $oldPorts = @(7001, 7002, 7003, 7100)
    for ($i = 0; $i -lt 4; ++$i) { $config = $config.Replace("Port=$($oldPorts[$i])", "Port=$($ports[$i])") }
    $configPath = Join-Path $testDirectory 'Server.ini'
    [IO.File]::WriteAllText($configPath, $config)
    Invoke-Checked $server @('--config-check', 'A', '--config', $configPath)
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
    $startPath = Join-Path $testDirectory 'Data\StartPoint.csv'
    $originalStart = [IO.File]::ReadAllText($startPath)
    try {
        [IO.File]::WriteAllText($startPath, $originalStart.Replace('1,1,Terran,60,104', '1,1,Terran,0,0'))
        $null = & $server --config-check A --config $configPath
        if ($LASTEXITCODE -ne 0) { throw 'Zero start coordinate was rejected' }
        Write-Output '[OK] Zero start coordinates accepted'
    }
    finally { [IO.File]::WriteAllText($startPath, $originalStart) }
    foreach ($listener in $reservations) { $listener.Stop() }
    $reservations = @()

    $ids = @('A', 'B', 'C', 'World')
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

    Test-BadPacket $ports[0] ([byte[]]@(0,0,0,7,0,1,0,2)) 'length below header'
    Test-BadPacket $ports[0] ([byte[]]@(0,0,16,1,0,1,0,2)) 'packet above 4096 bytes'
    Test-BadPacket $ports[0] ([byte[]]@(0,0,0,8,0,1,0,1)) 'unsupported version'
    Test-BadPacket $ports[0] ([byte[]]@(0,0,0,8,0,3,0,2)) 'ping before hello'
    Test-BadPacket $ports[0] ([byte[]]@(0,0,0,8,0,99,0,2)) 'unknown type'
    Test-BadPacket $ports[0] $helloPacket 'duplicate hello' -AfterHello
    Test-BadPacket $ports[0] ([byte[]]@(0,0,0,8,0,1,0,2)) 'missing client name'
    Test-BadPacket $ports[0] ([byte[]]@(0,0,0,11,0,1,0,2,10,5,65)) 'truncated protobuf string'
    Test-BadPacket $ports[0] ([byte[]]@(0,0,0,11,0,1,0,2,10,1,255)) 'invalid UTF-8 client name'
    Test-BadPacket $ports[0] ([byte[]]@(0,0,0,9,0,3,0,2,15)) 'invalid protobuf wire type' -AfterHello
    Test-BadPacket $ports[0] ([byte[]]@(0,0,0,11,0,3,0,2,18,1,65)) 'missing ping sequence' -AfterHello

    # 헤더 3바이트만 보낸 상태에서는 응답이 없어야 하고, 나머지가 오면 복원해야 한다.
    $fragmentClient = New-TestClient $ports[0]
    try {
        $stream = $fragmentClient.GetStream()
        $stream.Write($helloPacket, 0, 3)
        Start-Sleep -Milliseconds 100
        if ($fragmentClient.Available -ne 0) { throw 'Partial header produced a reply' }
        $stream.Write($helloPacket, 3, $helloPacket.Length - 3)
        Read-Hello $stream
        Write-Output '[OK] Partial header accumulation and Protobuf response'
    }
    finally { $fragmentClient.Dispose() }
    Invoke-Checked $dummy @('--server', 'A', '--config', $configPath, '--clients', '16', '--rounds', '10')
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
