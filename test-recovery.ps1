param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$binary = Join-Path $PSScriptRoot "Run64\$Configuration\MultiThreadServer.exe"
$testDirectory = Join-Path $PSScriptRoot ('MultiThreadServer\MultiThreadServer\build\recovery-' + [Guid]::NewGuid().ToString('N'))
$realm = 'test_' + [Guid]::NewGuid().ToString('N')
$processes = @(); $clients = @(); $listeners = @()
New-Item -ItemType Directory -Path (Join-Path $testDirectory 'Data') -Force | Out-Null

function Check([bool]$ok, [string]$message) { if (-not $ok) { throw $message } }
function Read-Exact($stream, [int]$count) {
    $bytes = [byte[]]::new($count); $offset = 0
    while ($offset -lt $count) {
        $read = $stream.Read($bytes, $offset, $count - $offset)
        if ($read -eq 0) { throw 'Connection closed while reading' }
        $offset += $read
    }
    return ,$bytes
}
function Read-Packet($client, [int]$expected) {
    do {
        $header = Read-Exact $client.GetStream() 4
        $size = ([int]$header[0] -shl 8) -bor $header[1]
        $type = ([int]$header[2] -shl 8) -bor $header[3]
        Check ($size -ge 4 -and $size -le 4096) 'Bad frame size'
        $body = Read-Exact $client.GetStream() ($size - 4)
        # World의 비동기 상태/알림은 응답 대기 중에도 올 수 있다.
    } while ($type -in @(22,23,26) -and $type -ne $expected)
    Check ($type -eq $expected) "Unexpected packet $type, expected $expected"
    return ,$body
}
function Write-Packet($client, [int]$type, [byte[]]$body) {
    $size = $body.Length + 4
    $bytes = [byte[]]@((($size -shr 8) -band 255),($size -band 255),(($type -shr 8) -band 255),($type -band 255)) + $body
    $client.GetStream().Write($bytes,0,$bytes.Length)
}
function Varint([byte[]]$bytes, [ref]$offset) {
    [long]$value = 0; $shift = 0
    do { $next = $bytes[$offset.Value]; $offset.Value++; $value = $value -bor ([long]($next -band 127) -shl $shift); $shift += 7 } while ($next -ge 128 -and $shift -lt 64)
    return $value
}
function Fields([byte[]]$bytes) {
    $result = @{}; $offset = 0
    while ($offset -lt $bytes.Length) {
        $tag = Varint $bytes ([ref]$offset); $number = [int]($tag -shr 3); $wire = $tag -band 7
        if ($wire -eq 0) { $result[$number] = Varint $bytes ([ref]$offset) }
        else {
            $count = if ($wire -eq 2) { Varint $bytes ([ref]$offset) } elseif ($wire -eq 1) { 8 } elseif ($wire -eq 5) { 4 } else { throw 'Unexpected wire type' }
            $value = [byte[]]::new($count); [Array]::Copy($bytes,$offset,$value,0,$count); $offset += $count
            $result[$number] = $value
        }
    }
    return $result
}
function Bytes-Field([int]$number, [byte[]]$bytes) {
    $output = [Collections.Generic.List[byte]]::new(); $output.Add([byte]($number * 8 + 2)); $length = $bytes.Length
    while ($length -ge 128) { $output.Add([byte](($length -band 127) -bor 128)); $length = $length -shr 7 }
    $output.Add([byte]$length); $output.AddRange($bytes); return ,$output.ToArray()
}
function Text-Field([int]$number, [string]$text) { return ,(Bytes-Field $number ([Text.Encoding]::UTF8.GetBytes($text))) }
function Connect-Test([int]$port) {
    $client = [Net.Sockets.TcpClient]::new(); $script:clients += $client
    $client.ReceiveTimeout = 10000; $client.SendTimeout = 3000; $client.Connect('127.0.0.1',$port)
    Write-Packet $client 1 ((Text-Field 1 'RecoveryProbe') + [byte[]]@(16,5)); $null = Read-Packet $client 2
    return $client
}
function Send-Login($client, [string]$account) { Write-Packet $client 10 ((Text-Field 1 $account) + (Text-Field 2 'PortfolioTest!2026')) }
function Login-Test([int]$port, [string]$account) {
    $client = Connect-Test $port; Send-Login $client $account
    $reply = Fields (Read-Packet $client 11); $unit = Fields $reply[1]
    return [pscustomobject]@{ Client=$client; Id=[Text.Encoding]::UTF8.GetString($unit[1]); Unit=$unit[5]; Cooldown=$reply[2] }
}
function Logout-Test($client) { Write-Packet $client 15 @(); $reply = Fields (Read-Packet $client 21); Check ($reply[1] -eq 1) 'Logout failed' }
function Start-TestServer([string]$id, [int]$port, [string]$log) {
    $process = Start-Process -FilePath $binary -ArgumentList @('--server',$id,'--config',('"'+$configPath+'"'),'--run-seconds','180') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $testDirectory "$log.out.txt") -RedirectStandardError (Join-Path $testDirectory "$log.err.txt")
    $null=$process.Handle; $script:processes += $process
    for ($i=0; $i -lt 100; $i++) {
        Check (-not $process.HasExited) "Server $id exited"
        $probe=[Net.Sockets.TcpClient]::new()
        try { $probe.Connect('127.0.0.1',$port); return $process } catch { Start-Sleep -Milliseconds 50 } finally { $probe.Dispose() }
    }
    throw 'Server readiness timeout'
}
function Stop-Owned($process) { if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() } }
try {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Run64\$Configuration\Cluster.local.ini") -Destination $testDirectory
    foreach ($file in @('Unit.csv','Skill.csv','Map.ini','StartPoint.csv')) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Data\$file") -Destination (Join-Path $testDirectory 'Data') }
    $ports=@()
    for ($i=0;$i -lt 2;$i++) { $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0); $listener.Start(); $listeners+=$listener; $ports+=$listener.LocalEndpoint.Port }
    [IO.File]::WriteAllLines((Join-Path $testDirectory 'Data\GameServer.csv'),@('Index,Name,Race,Address,Port',"A,Game A,Zerg,127.0.0.1,$($ports[0])"))
    $configPath=Join-Path $testDirectory 'Server.ini'
    $config=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Server.ini')).Replace('Port=7100',"Port=$($ports[1])").Replace('Realm=local',"Realm=$realm").Replace('SessionLeaseSeconds=15','SessionLeaseSeconds=10').Replace('DeathReentrySeconds=100','DeathReentrySeconds=5')
    [IO.File]::WriteAllText($configPath,$config)
    foreach ($listener in $listeners) { $listener.Stop() }; $listeners=@()
    $gameServer=Start-TestServer 'A' $ports[0] 'A'
    $worldServer=Start-TestServer 'World' $ports[1] 'World'
    $saved=Login-Test $ports[0] 'CrashRecovery'
    Stop-Owned $gameServer
    $gameServer=Start-TestServer 'A' $ports[0] 'A-restarted'
    $duplicate=Connect-Test $ports[0]; Send-Login $duplicate 'CrashRecovery'
    $denied=Fields (Read-Packet $duplicate 21); Check ($denied[1] -ne 1) 'Restart bypassed active lease'
    Start-Sleep -Seconds 11
    $restored=Login-Test $ports[0] 'CrashRecovery'
    Check ($restored.Id -eq $saved.Id -and $restored.Unit -eq $saved.Unit) 'Restart lost account/unit'
    Write-Output '[OK] Game crash/restart: active lease rejected, same account/unit restored after expiry'

    Write-Packet $restored.Client 12 @(); $ticket=Fields (Read-Packet $restored.Client 13)
    $world=Connect-Test $ports[1]
    Write-Packet $world 16 ((Bytes-Field 1 $ticket[1]) + (Bytes-Field 2 $ticket[2]))
    $entered=Fields (Read-Packet $world 17); Check ($entered[1] -eq 1) 'World entry failed'
    Stop-Owned $worldServer
    $worldServer=Start-TestServer 'World' $ports[1] 'World-restarted'
    Start-Sleep -Seconds 11
    $restored=Login-Test $ports[0] 'CrashRecovery'
    Check ($restored.Id -eq $saved.Id -and $restored.Cooldown -gt 0) 'World crash lost identity/cooldown'
    Logout-Test $restored.Client
    Write-Output '[OK] World crash/restart: account survives and persisted reentry cooldown remains'

    $lost=Login-Test $ports[0] 'LostTransfer'
    Write-Packet $lost.Client 12 @(); $ticket=Fields (Read-Packet $lost.Client 13)
    $duplicate=Connect-Test $ports[0]; Send-Login $duplicate 'LostTransfer'
    $denied=Fields (Read-Packet $duplicate 21); Check ($denied[1] -ne 1) 'Pending transfer admitted duplicate login'
    Start-Sleep -Seconds 31
    $restored=Login-Test $ports[0] 'LostTransfer'
    Check ($restored.Id -eq $lost.Id -and $restored.Unit -eq $lost.Unit) 'Lost ticket recovery failed'
    $replay=Connect-Test $ports[1]
    Write-Packet $replay 16 ((Bytes-Field 1 $ticket[1]) + (Bytes-Field 2 $ticket[2]))
    $denied=Fields (Read-Packet $replay 17); Check ($denied[1] -ne 1) 'Expired transfer accepted'
    Logout-Test $restored.Client
    Write-Output '[OK] Lost transfer: pending duplicate rejected, recovery after expiry, old ticket rejected'

    # SQL 서비스는 중지하지 않는다. 이번 테스트 계정 하나의 저장 트랜잭션만 잠시 막는다.
    $healthy=Login-Test $ports[0] 'Healthy'
    $database=[Data.SqlClient.SqlConnection]::new('Server=tcp:127.0.0.1,1433;Database=project_db;Integrated Security=true;Encrypt=true;TrustServerCertificate=true;Connection Timeout=5')
    try {
        $database.Open(); $transaction=$database.BeginTransaction(); $command=$database.CreateCommand(); $command.Transaction=$transaction
        $command.CommandText="DECLARE @result int; EXEC @result=sys.sp_getapplock @Resource=@resource,@LockMode='Exclusive',@LockOwner='Transaction',@LockTimeout=1000; SELECT @result;"
        $null=$command.Parameters.AddWithValue('@resource',"portfolio:${realm}:Blocked")
        Check ($command.ExecuteScalar() -ge 0) 'Test account lock failed'
        $blocked=Connect-Test $ports[0]; Send-Login $blocked 'Blocked'
        Start-Sleep -Milliseconds 100
        $watch=[Diagnostics.Stopwatch]::StartNew()
        Write-Packet $healthy.Client 25 (Text-Field 1 'chat-during-db-lock')
        $chat=Fields (Read-Packet $healthy.Client 21)
        Check ($chat[1] -eq 1 -and $watch.ElapsedMilliseconds -lt 800) 'DB wait stalled game processing'
        $null=Read-Packet $healthy.Client 26
        $denied=Fields (Read-Packet $blocked 21)
        Check ($denied[1] -ne 1) 'Failed DB save was acknowledged as success'
        $transaction.Rollback(); $command.Dispose(); $transaction.Dispose()
    }
    finally { $database.Dispose() }
    $retry=Login-Test $ports[0] 'Blocked'; Logout-Test $retry.Client; Logout-Test $healthy.Client
    Write-Output '[OK] DB lock/write failure: game chat responsive, no false login success, fresh retry succeeds'
    Write-Output "[OK] $Configuration recovery checks passed. Logs: $testDirectory"
}
finally {
    foreach ($client in $clients) { $client.Dispose() }
    foreach ($listener in $listeners) { $listener.Stop() }
    foreach ($process in $processes) { Stop-Owned $process; $process.Dispose() }
}
