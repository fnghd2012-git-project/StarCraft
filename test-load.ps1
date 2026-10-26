param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$Quick,
    [ValidateSet('None','Directory','FileLimit')][string]$ReplayFailure = 'None'
)
$ErrorActionPreference = 'Stop'
$binaryDirectory = Join-Path $PSScriptRoot "Run64\$Configuration"
$testDirectory = Join-Path $PSScriptRoot ('MultiThreadServer\MultiThreadServer\build\load-' + [Guid]::NewGuid().ToString('N'))
$processes = @(); $listeners = @(); $client = $null
New-Item -ItemType Directory -Path (Join-Path $testDirectory 'Data') -Force | Out-Null
$reportPath = Join-Path $testDirectory 'latency.csv'; $resources = @()
try {
    Copy-Item -LiteralPath (Join-Path $binaryDirectory 'Cluster.local.ini') -Destination $testDirectory
    foreach ($file in @('Unit.csv','Skill.csv','StartPoint.csv','Map.ini')) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Data\$file") -Destination (Join-Path $testDirectory 'Data') }
    # 측정 도중 사망/복귀로 접속 수가 줄지 않도록 복사본 HP/MP/속도만 조정한다.
    $unitPath = Join-Path $testDirectory 'Data\Unit.csv'; $units = Import-Csv -LiteralPath $unitPath -Encoding UTF8
    foreach ($unit in $units) { $unit.HP = '100000'; $unit.MP = '1000'; $unit.InitialMP = '1000'; $unit.MoveSpeed = '100' }
    $units | Export-Csv -LiteralPath $unitPath -Encoding UTF8 -NoTypeInformation
    $ports = @()
    for ($i = 0; $i -lt 4; ++$i) {
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
        $listener.Start(); $listeners += $listener; $ports += $listener.LocalEndpoint.Port
    }
    [IO.File]::WriteAllLines((Join-Path $testDirectory 'Data\GameServer.csv'), @('Index,Name,Race,Address,Port', "A,Game A,Zerg,127.0.0.1,$($ports[0])", "B,Game B,Protoss,127.0.0.1,$($ports[1])", "C,Game C,Terran,127.0.0.1,$($ports[2])"))
    $config = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Server.ini')).Replace('MaxWorldPlayers=1500','MaxWorldPlayers=64').Replace('SnapshotRadius=25','SnapshotRadius=0').Replace('Realm=local', ('Realm=test_' + [Guid]::NewGuid().ToString('N'))).Replace('Port=7100', "Port=$($ports[3])")
    if ($ReplayFailure -eq 'Directory') { [IO.File]::WriteAllText((Join-Path $testDirectory 'Replays'), 'Intentional file/directory collision') }
    if ($ReplayFailure -eq 'FileLimit') { $config = $config.Replace('MaxFileMB=128','MaxFileMB=1') }
    $configPath = Join-Path $testDirectory 'Server.ini'; [IO.File]::WriteAllText($configPath, $config)
    foreach ($listener in $listeners) { $listener.Stop() }; $listeners = @()
    $ids = @('A','B','C','World')
    for ($i = 0; $i -lt 4; ++$i) {
        $process = Start-Process -FilePath (Join-Path $binaryDirectory 'MultiThreadServer.exe') -ArgumentList @('--server',$ids[$i],'--config',('"' + $configPath + '"'),'--run-seconds','600') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $testDirectory "$($ids[$i]).out.txt") -RedirectStandardError (Join-Path $testDirectory "$($ids[$i]).err.txt")
        $null = $process.Handle; $processes += $process; $ready = $false
        for ($attempt = 0; $attempt -lt 100; ++$attempt) {
            if ($process.HasExited) { throw "Server $($ids[$i]) failed. Logs: $testDirectory" }
            $probe = [Net.Sockets.TcpClient]::new()
            try { $probe.Connect('127.0.0.1',$ports[$i]); $ready = $true; break }
            catch { Start-Sleep -Milliseconds 50 }
            finally { $probe.Dispose() }
        }
        if (-not $ready) { throw 'Server startup timed out' }
    }
    $phases = @(@('ping',64,8), @('world',16,8), @('world',32,8), @('world',64,10), @('combat',64,60))
    if ($Quick) { $phases = @(@('ping',8,3), @('world',8,4), @('combat',8,6)) }
    if ($ReplayFailure -ne 'None') { $phases = ,@('world',64,15) }
    $index = 0
    foreach ($phase in $phases) {
        ++$index; $mode = $phase[0]; $count = $phase[1]; $seconds = $phase[2]
        $world = $processes[3]; $world.Refresh(); $cpuBefore = $world.TotalProcessorTime.TotalMilliseconds
        $started = [DateTime]::UtcNow; $peakBytes = 0L
        $phaseOut = Join-Path $testDirectory "phase-$index.out.txt"; $phaseErr = Join-Path $testDirectory "phase-$index.err.txt"
        $client = Start-Process -FilePath (Join-Path $binaryDirectory 'DummyClient.exe') -ArgumentList @('--load','--config',('"' + $configPath + '"'),'--mode',$mode,'--clients',$count,'--seconds',$seconds,'--rate','10','--csv',('"' + $reportPath + '"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput $phaseOut -RedirectStandardError $phaseErr
        $null = $client.Handle
        while (-not $client.WaitForExit(1000)) {
            $world.Refresh(); $peakBytes = [Math]::Max($peakBytes, $world.WorkingSet64)
            if ($world.HasExited) { throw 'World stopped during load' }
            if (([DateTime]::UtcNow - $started).TotalSeconds -gt ($seconds + 150)) { throw 'Load preparation/run timed out' }
        }
        if ($client.ExitCode -ne 0) { throw "Load failed: $(Get-Content -LiteralPath $phaseOut -Raw) $(Get-Content -LiteralPath $phaseErr -Raw)" }
        $world.Refresh(); $peakBytes = [Math]::Max($peakBytes, $world.WorkingSet64)
        $elapsedMs = ([DateTime]::UtcNow - $started).TotalMilliseconds
        $resources += [pscustomobject]@{ Phase=$index; Mode=$mode; Clients=$count; WholePhaseSeconds=[Math]::Round($elapsedMs / 1000,3); WorldCpuOneCorePercent=[Math]::Round(($world.TotalProcessorTime.TotalMilliseconds - $cpuBefore) * 100 / $elapsedMs,3); WorldPeakWorkingSetMB=[Math]::Round($peakBytes / 1MB,3) }
        $client.Dispose(); $client = $null
        Get-Content -LiteralPath $phaseOut | Write-Output
        $row = Import-Csv -LiteralPath $reportPath | Select-Object -Last 1
        if ([int]$row.Errors -ne 0 -or [int]$row.Requests -eq 0) { throw 'Load had errors/no requests' }
        if ($mode -eq 'world' -and [int]$row.Rejected -ne 0) { throw 'Movement unexpectedly rejected' }
        if ($mode -ne 'ping' -and [int]$row.SnapshotUpdates -eq 0) { throw 'No World snapshots received' }
        Start-Sleep -Seconds 1
    }
    $resources | Export-Csv -LiteralPath (Join-Path $testDirectory 'resources.csv') -NoTypeInformation -Encoding UTF8
    # 성공/실패 로그는 쓰는 중인 파일과 공유해서 읽는다.
    $stream = [IO.File]::Open((Join-Path $testDirectory 'World.err.txt'), 'Open', 'Read', 'ReadWrite')
    $reader = [IO.StreamReader]::new($stream)
    try { $errors = $reader.ReadToEnd() } finally { $reader.Dispose() }
    if ($ReplayFailure -eq 'None' -and $errors.Contains('[REPLAY FAIL]')) { throw $errors }
    if ($ReplayFailure -ne 'None' -and -not $errors.Contains('[REPLAY FAIL]')) { throw 'Expected recorder failure not observed' }
    if ($ReplayFailure -eq 'FileLimit') {
        foreach ($file in Get-ChildItem -LiteralPath (Join-Path $testDirectory 'Replays')) { if ($file.Length -gt 1MB) { throw 'Replay exceeded configured file cap' } }
    }
}
finally {
    foreach ($listener in $listeners) { $listener.Stop() }
    if ($client) { if (-not $client.HasExited) { $client.Kill(); $client.WaitForExit() }; $client.Dispose() }
    foreach ($process in $processes) { if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }; $process.Dispose() }
}
# 강제 종료된 테스트 서버의 기록은 완성 경기로 취급하지 않는다. 저장된 정상 프레임만 검증한다.
if ($ReplayFailure -ne 'Directory') {
    $partial = Get-ChildItem -LiteralPath (Join-Path $testDirectory 'Replays') -Filter '*.part' | Select-Object -First 1
    if (-not $partial) { throw 'Missing partial recording' }
    $arguments = @('--replay', $partial.FullName, '--verify', '--allow-partial')
    if ($ReplayFailure -eq 'None') { $arguments += @('--require','Skill') }
    & (Join-Path $binaryDirectory 'DummyClient.exe') @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Load replay validation failed' }
}
Write-Output "[OK] $Configuration load checks passed. ReplayFailure=$ReplayFailure. Logs: $testDirectory"
