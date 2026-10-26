param(
    [ValidateRange(1,1000)][int]$PerServer = 500,
    [ValidateRange(10,1800)][int]$Seconds = 240,
    [ValidateRange(1,100)][int]$TestCooldownSeconds = 100,
    [ValidateSet('', 'Linear', 'Grid')][string]$SpatialSearch = '',
    [switch]$VerifySpatialSearch,
    [switch]$NoReplay
)
$ErrorActionPreference = 'Stop'
$binaryDirectory = $PSScriptRoot
$serverExe = Join-Path $binaryDirectory 'MultiThreadServer.exe'
$dummyExe = Join-Path $binaryDirectory 'DummyClient.exe'
$runId = [Guid]::NewGuid().ToString('N')
$directory = Join-Path $binaryDirectory ('BattleRuns\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $runId)
$servers = @(); $listeners = @(); $dummyProcess = $null
$config = [IO.File]::ReadAllText((Join-Path $binaryDirectory 'Server.ini'))
function Set-Config([string]$Section, [string]$Key, [string]$Value) {
    $pattern = '(?ms)(^\[' + [regex]::Escape($Section) + '\]\r?\n)(.*?)(?=^\[|\z)'
    $match = [regex]::Match($script:config, $pattern)
    if (-not $match.Success) { throw "Missing INI section: $Section" }
    $body = $match.Groups[2].Value
    $keyPattern = '(?m)^' + [regex]::Escape($Key) + '=[^\r\n]*'
    if ([regex]::IsMatch($body,$keyPattern)) { $body = [regex]::Replace($body,$keyPattern,($Key+'='+$Value)) }
    else { $body = $body.TrimEnd() + "`r`n$Key=$Value`r`n`r`n" }
    $script:config = $script:config.Substring(0,$match.Index) + $match.Groups[1].Value + $body + $script:config.Substring($match.Index+$match.Length)
}
function Source-Path([string]$Key) {
    $match = [regex]::Match($script:config,'(?m)^'+$Key+'=([^\r\n]+)')
    if (-not $match.Success) { throw "Missing source path: $Key" }
    $path = $match.Groups[1].Value.Trim()
    if ([IO.Path]::IsPathRooted($path)) { return $path }
    return Join-Path $binaryDirectory $path
}
try {
    $gameServers = @(Import-Csv -LiteralPath (Source-Path 'ServerFile') -Encoding UTF8)
    $total = $PerServer * $gameServers.Count
    if ($total -gt 2048) { throw 'Battle supports up to 2048 total players' }
    New-Item -ItemType Directory -Path (Join-Path $directory 'Data') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $binaryDirectory 'Cluster.local.ini') -Destination $directory
    foreach ($entry in @{UnitFile='Unit.csv'; SkillFile='Skill.csv'; MapFile='Map.ini'; StartPointFile='StartPoint.csv'}.GetEnumerator()) {
        Copy-Item -LiteralPath (Source-Path $entry.Key) -Destination (Join-Path $directory ('Data\'+$entry.Value))
        Set-Config 'Data' $entry.Key ('Data/'+$entry.Value)
    }
    # 시연에서는 재입장을 관찰할 시간을 확보한다. 유닛 밸런스/원본 맵은 수정하지 않는다.
    $mapPath = Join-Path $directory 'Data\Map.ini'
    $map = [IO.File]::ReadAllText($mapPath)
    $map = [regex]::Replace($map,'(?m)^BuildingHP=\d+',('BuildingHP='+[Math]::Min(1000000,[Math]::Max(100000,$PerServer*1500))))
    [IO.File]::WriteAllText($mapPath,$map,[Text.UTF8Encoding]::new($false))
    $ports = @()
    for ($i=0; $i -le $gameServers.Count; ++$i) {
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
        $listener.Start(); $listeners += $listener; $ports += $listener.LocalEndpoint.Port
    }
    for ($i=0; $i -lt $gameServers.Count; ++$i) { $gameServers[$i].Address='127.0.0.1'; $gameServers[$i].Port=$ports[$i] }
    $gameServers | Export-Csv -LiteralPath (Join-Path $directory 'Data\GameServer.csv') -NoTypeInformation -Encoding UTF8
    Set-Config 'Data' 'ServerFile' 'Data/GameServer.csv'
    Set-Config 'Database' 'Realm' ('battle_'+$runId)
    Set-Config 'Database' 'WorkerCount' '6'
    Set-Config 'Database' 'QueueCapacity' '4096'
    Set-Config 'Database' 'SessionLeaseSeconds' '60'
    Set-Config 'Network' 'StopFile' 'stop.request'
    Set-Config 'Network' 'WorkerCount' '4'
    Set-Config 'Network' 'MaxClientsPerWorker' '1024'
    Set-Config 'Network' 'MaxPendingSendBytes' '262144'
    Set-Config 'World' 'Address' '127.0.0.1'
    Set-Config 'World' 'Port' $ports[-1]
    Set-Config 'Replay' 'Enabled' '1'
    Set-Config 'Replay' 'Directory' 'Replays'
    Set-Config 'Replay' 'QueueMB' '32'
    Set-Config 'Replay' 'MaxFileMB' '1024'
    Set-Config 'Replay' 'FrameTicks' '4'
    Set-Config 'Battle' 'DeathReentrySeconds' $TestCooldownSeconds
    # 비어 있으면 원본 INI 설정을 유지한다. 비교 실행은 복사본만 변경한다.
    if ($SpatialSearch) { Set-Config 'Battle' 'SpatialSearch' $SpatialSearch }
    if ($VerifySpatialSearch) { Set-Config 'Battle' 'VerifySpatialSearch' '1' }
    # 최대 인원은 사용자가 편집한 Server.ini 값을 그대로 사용한다. 부족하면 더미가 시작 전에 거부한다.
    $configPath = Join-Path $directory 'Server.ini'
    [IO.File]::WriteAllText($configPath,$config,[Text.UTF8Encoding]::new($false))
    & $serverExe --config-check World --config $configPath
    if ($LASTEXITCODE -ne 0) { throw 'Battle settings failed validation' }
    foreach ($listener in $listeners) { $listener.Stop() }; $listeners=@()
    $ids = @($gameServers | ForEach-Object {$_.Index}) + @('World')
    for ($i=0; $i -lt $ids.Count; ++$i) {
        $process = Start-Process -FilePath $serverExe -ArgumentList @('--server',$ids[$i],'--config',('"'+$configPath+'"'),'--run-seconds',($Seconds+1500)) -WorkingDirectory $directory -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $directory "server-$i.out.txt") -RedirectStandardError (Join-Path $directory "server-$i.err.txt")
        $null=$process.Handle; $servers+=$process; $ready=$false
        for ($attempt=0;$attempt -lt 100;++$attempt) {
            if ($process.HasExited) { throw "Server startup failed: $($ids[$i])" }
            $probe=[Net.Sockets.TcpClient]::new()
            try { $probe.Connect('127.0.0.1',$ports[$i]); $ready=$true; break }
            catch { Start-Sleep -Milliseconds 50 }
            finally { $probe.Dispose() }
        }
        if (-not $ready) { throw "Server startup timed out: $($ids[$i])" }
    }
    $report=Join-Path $directory 'result.txt'
    Write-Host "[BATTLE] $PerServer per game server / $total total / death cooldown=$TestCooldownSeconds / $Seconds battle seconds"
    Write-Host "[DIRECTORY] $directory"
    $dummyProcess=Start-Process -FilePath $dummyExe -ArgumentList @('--battle','--config',('"'+$configPath+'"'),'--per-server',$PerServer,'--seconds',$Seconds,'--report',('"'+$report+'"')) -WorkingDirectory $directory -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $directory 'dummy.out.txt') -RedirectStandardError (Join-Path $directory 'dummy.err.txt')
    $null=$dummyProcess.Handle; $shown=0
    while (-not $dummyProcess.HasExited) {
        Start-Sleep -Seconds 2
        $lines=@(Get-Content -LiteralPath (Join-Path $directory 'dummy.out.txt'))
        if ($lines.Count -gt $shown) { $lines | Select-Object -Skip $shown | Write-Host; $shown=$lines.Count }
        foreach ($process in $servers) { if ($process.HasExited) { throw 'A battle server exited unexpectedly' } }
        if (@(Get-ChildItem -LiteralPath $directory -Filter 'server-*.err.txt' | Select-String -Pattern '\[REPLAY FAIL\]|\[WORKER FAIL\]').Count) { throw 'Server recording/worker failure' }
    }
    if ($dummyProcess.ExitCode -ne 0) { throw "Battle dummy failed. Logs: $directory" }
    Get-Content -LiteralPath $report | Write-Host
    # 이 실행 전용 파일을 감지한 서버가 워커와 리플레이를 정상 마감한다.
    [IO.File]::WriteAllText((Join-Path $directory 'stop.request'), 'stop')
    foreach ($process in $servers) {
        if (-not $process.WaitForExit(30000)) { throw 'Owned server did not stop gracefully' }
        if ($process.ExitCode -ne 0) { throw 'Owned server shutdown failed' }
    }
    $files=@(Get-ChildItem -LiteralPath (Join-Path $directory 'Replays') -Filter '*.replay' | Sort-Object Length -Descending)
    if (-not $files.Count) { throw 'No completed replay file' }
    $replay=$files[0].FullName
    & $dummyExe --replay $replay --verify --require Attack
    if ($LASTEXITCODE -ne 0) { throw 'Battle replay validation failed' }
    $replayDirectory=Join-Path $binaryDirectory 'Replays'; New-Item -ItemType Directory -Path $replayDirectory -Force | Out-Null
    $saved=Join-Path $replayDirectory ("Battle-${total}-"+$files[0].Name)
    Copy-Item -LiteralPath $replay -Destination $saved
    Write-Host "[REPLAY] $saved"
    Write-Host "[RESULT] $report"
}
finally {
    if ($dummyProcess) { if (-not $dummyProcess.HasExited) { $dummyProcess.Kill(); $dummyProcess.WaitForExit() }; $dummyProcess.Dispose() }
    foreach ($process in $servers) { if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }; $process.Dispose() }
    foreach ($listener in $listeners) { $listener.Stop() }
}
if (-not $NoReplay) { & $dummyExe --replay $saved }
