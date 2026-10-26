param([ValidateSet('Debug','Release')][string]$Configuration='Debug', [string]$RedisServer='C:\Program Files\Redis\redis-server.exe')
$ErrorActionPreference='Stop'
$bin=Join-Path $PSScriptRoot "Run64\$Configuration"
$dir=Join-Path $PSScriptRoot ('.deps\ranking-' + [guid]::NewGuid().ToString('N'))
$realm='rank_' + [guid]::NewGuid().ToString('N')
$processes=@(); $listeners=@(); $redis=$null; $dummy=$null
New-Item -ItemType Directory -Path (Join-Path $dir 'Data') -Force | Out-Null
function Wait-Port($process,[int]$port) {
    for($i=0;$i -lt 100;++$i) {
        if($process.HasExited){throw "Process exited on port $port. Logs: $dir"}
        $probe=[Net.Sockets.TcpClient]::new()
        try {$probe.Connect('127.0.0.1',$port);return} catch {Start-Sleep -Milliseconds 50} finally {$probe.Dispose()}
    }
    throw "Port $port not ready"
}
function Wait-Signal([string]$name) {
    $deadline=[DateTime]::UtcNow.AddSeconds(70)
    while(-not (Test-Path -LiteralPath (Join-Path $dir $name))) {
        if($dummy.HasExited){throw "Dummy failed: $(Get-Content (Join-Path $dir 'dummy.out.txt') -Raw) $(Get-Content (Join-Path $dir 'dummy.err.txt') -Raw)"}
        if([DateTime]::UtcNow -gt $deadline){throw "Signal timed out: $name. Logs: $dir"}
        Start-Sleep -Milliseconds 50
    }
}
function Start-TestRedis([string]$name) {
    $p=Start-Process -FilePath $RedisServer -ArgumentList @('"'+(Join-Path $dir 'redis.conf')+'"') -WorkingDirectory $dir -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $dir "$name.out.txt") -RedirectStandardError (Join-Path $dir "$name.err.txt")
    $null=$p.Handle; Wait-Port $p $ports[4]; return $p
}
try {
    if(-not (Test-Path -LiteralPath $RedisServer)){throw 'Specify -RedisServer pointing to a Redis server executable for the isolated outage test'}
    Copy-Item -LiteralPath (Join-Path $bin 'Cluster.local.ini') -Destination $dir
    Copy-Item -Path (Join-Path $PSScriptRoot 'Data\*') -Destination (Join-Path $dir 'Data')
    $units=@(Import-Csv (Join-Path $dir 'Data\Unit.csv') | Where-Object Name -in @('Marine','HighTemplar','Zergling'))
    foreach($u in $units){$u.HP='100';$u.Shield='0';$u.Armor='0';$u.MoveSpeed='100';$u.MP='1000';$u.InitialMP='1000';if($u.AttackType -ne 'None'){$u.AttackDamage='1000';$u.AttackRange='200';$u.AttackIntervalMs='100'}}
    $units | Export-Csv (Join-Path $dir 'Data\Unit.csv') -Encoding UTF8 -NoTypeInformation
    $skills=Import-Csv (Join-Path $dir 'Data\Skill.csv')
    foreach($s in $skills){if($s.SkillType -eq 'Storm'){$s.CastRange='200';$s.Power='1000';$s.TickIntervalMs='1000';$s.DurationMs='1000'}}
    $skills | Export-Csv (Join-Path $dir 'Data\Skill.csv') -Encoding UTF8 -NoTypeInformation
    $mapPath=Join-Path $dir 'Data\Map.ini';$map=[IO.File]::ReadAllText($mapPath).Replace('BuildingHP=1500','BuildingHP=100').Replace('BuildingShield=1000','BuildingShield=0');[IO.File]::WriteAllText($mapPath,$map)
    $ports=@()
    for($i=0;$i -lt 5;++$i){$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$listeners+=$listener;$ports+=$listener.LocalEndpoint.Port}
    [IO.File]::WriteAllLines((Join-Path $dir 'Data\GameServer.csv'),@('Index,Name,Race,Address,Port',"A,Game A,Zerg,127.0.0.1,$($ports[0])","B,Game B,Protoss,127.0.0.1,$($ports[1])","C,Game C,Terran,127.0.0.1,$($ports[2])"))
    # 1초로 줄이면 로그인 응답과 재입장 거절 검사 사이에 대기가 끝날 수 있어 3초를 둔다.
    $config=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Server.ini')).Replace('MaxWorldPlayers=1500','MaxWorldPlayers=64').Replace('SnapshotRadius=25','SnapshotRadius=0').Replace('Realm=local',"Realm=$realm").Replace('Port=7100',"Port=$($ports[3])").Replace('Port=6379',"Port=$($ports[4])").Replace('DeathReentrySeconds=100','DeathReentrySeconds=3').Replace('RoundRestartSeconds=300','RoundRestartSeconds=3').Replace('[Network]',"[Network]`nStopFile=stop.signal")
    $configPath=Join-Path $dir 'Server.ini';[IO.File]::WriteAllText($configPath,$config)
    # 기존 6379 서비스는 건드리지 않는다. 이 시험이 시작한 별도 Redis만 중단/재시작한다.
    [IO.File]::WriteAllLines((Join-Path $dir 'redis.conf'),@('bind 127.0.0.1',"port $($ports[4])",'save ""','appendonly no'))
    foreach($l in $listeners){$l.Stop()};$listeners=@()
    $redis=Start-TestRedis 'redis-first'
    $ids=@('A','B','C','World')
    for($i=0;$i -lt 4;++$i){
        $p=Start-Process -FilePath (Join-Path $bin 'MultiThreadServer.exe') -ArgumentList @('--server',$ids[$i],'--config',('"'+$configPath+'"'),'--run-seconds','180') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $dir "$($ids[$i]).out.txt") -RedirectStandardError (Join-Path $dir "$($ids[$i]).err.txt")
        $null=$p.Handle;$processes+=$p;Wait-Port $p $ports[$i]
    }
    $dummy=Start-Process -FilePath (Join-Path $bin 'DummyClient.exe') -ArgumentList @('--ranking-check','--config',('"'+$configPath+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $dir 'dummy.out.txt') -RedirectStandardError (Join-Path $dir 'dummy.err.txt')
    $null=$dummy.Handle
    Wait-Signal 'outage.ready'
    $redis.Kill();$redis.WaitForExit();$redis.Dispose();$redis=$null
    [IO.File]::WriteAllText((Join-Path $dir 'outage.down'),'ready')
    Wait-Signal 'outage.checked'
    $redis=Start-TestRedis 'redis-second'
    [IO.File]::WriteAllText((Join-Path $dir 'outage.restored'),'ready')
    if(-not $dummy.WaitForExit(45000)){throw 'Dummy ranking test timed out'}
    Get-Content (Join-Path $dir 'dummy.out.txt')
    if($dummy.ExitCode -ne 0){throw "Ranking check failed: $(Get-Content (Join-Path $dir 'dummy.err.txt') -Raw)"}
    [IO.File]::WriteAllText((Join-Path $dir 'stop.signal'),'stop')
    foreach($p in $processes){if(-not $p.WaitForExit(10000)){throw 'Server shutdown timed out'};if($p.ExitCode -ne 0){throw 'Server exit failed'}}
    $db=[Data.SqlClient.SqlConnection]::new('Server=tcp:127.0.0.1,1433;Database=project_db;Integrated Security=true;Encrypt=true;TrustServerCertificate=true;Connection Timeout=5')
    try {
        $db.Open();$cmd=$db.CreateCommand();$cmd.CommandText='SELECT m.Complete,s.AccountId,s.Kills,s.Deaths,s.Entries FROM dbo.PortfolioMatch m JOIN dbo.PortfolioMatchStats s ON m.Realm=s.Realm AND m.MatchId=s.MatchId WHERE m.Realm=@realm ORDER BY m.Complete DESC,s.AccountId';$null=$cmd.Parameters.AddWithValue('@realm',$realm)
        $table=[Data.DataTable]::new();$reader=$cmd.ExecuteReader();$table.Load($reader);$reader.Close();$cmd.Dispose()
        $expected=@{RankMarine=@(2,0,1);RankZerg=@(0,3,4);RankCaster=@(1,0,1);RankAlly=@(0,1,1)}
        $complete=@($table.Rows | Where-Object Complete)
        if($complete.Count -ne 4){throw "Expected 4 final results, got $($complete.Count)"}
        foreach($row in $complete){$e=$expected[$row.AccountId];if($null -eq $e -or $row.Kills -ne $e[0] -or $row.Deaths -ne $e[1] -or $row.Entries -ne $e[2]){throw "Unexpected SQL result for $($row.AccountId)"}}
        $partial=@($table.Rows | Where-Object {-not $_.Complete})
        if($partial.Count -ne 1 -or $partial[0].Deaths -ne 0 -or $partial[0].Entries -ne 1){throw 'Shutdown/normal logout result mismatch'}
        $table | Format-Table -AutoSize | Out-String | Write-Output
        foreach($file in Get-ChildItem (Join-Path $dir 'MatchResults') -Filter '*.xml'){& (Join-Path $PSScriptRoot 'restore-match-result.ps1') -Path $file.FullName}
        $cmd=$db.CreateCommand();$cmd.CommandText='SELECT COUNT(*) FROM dbo.PortfolioMatchStats WHERE Realm=@realm';$null=$cmd.Parameters.AddWithValue('@realm',$realm);if($cmd.ExecuteScalar() -ne 5){throw 'Duplicate SQL result after retry'};$cmd.Dispose()
    } finally {$db.Dispose()}
    Write-Output "[OK] $Configuration ranking, Redis outage/recovery, final SQL transaction/idempotence. Realm=$realm Logs=$dir"
} finally {
    foreach($l in $listeners){$l.Stop()}
    foreach($p in @($dummy,$redis)+$processes){if($null -ne $p){if(-not $p.HasExited){$p.Kill();$p.WaitForExit()};$p.Dispose()}}
}
