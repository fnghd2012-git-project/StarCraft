param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$bin=Join-Path $PSScriptRoot "Run64\$Configuration"
$directory=Join-Path $PSScriptRoot ('.deps\capacity-'+[Guid]::NewGuid().ToString('N'))
$servers=@(); $listeners=@()
try {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Data') -Destination $directory -Recurse
    Copy-Item -LiteralPath (Join-Path $bin 'Cluster.local.ini') -Destination $directory
    $ports=@()
    for ($i=0;$i -lt 4;++$i) {
        $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
        $listener.Start(); $listeners+=$listener; $ports+=$listener.LocalEndpoint.Port
    }
    $rows=Import-Csv -LiteralPath (Join-Path $directory 'Data\GameServer.csv') -Encoding UTF8
    for ($i=0;$i -lt 3;++$i) { $rows[$i].Port=$ports[$i] }
    $rows | Export-Csv -LiteralPath (Join-Path $directory 'Data\GameServer.csv') -NoTypeInformation -Encoding UTF8
    $config=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Server.ini'))
    $config=$config.Replace('Realm=local',('Realm=capacity_'+[Guid]::NewGuid().ToString('N'))).Replace('Port=7100',"Port=$($ports[3])")
    $config=[regex]::Replace($config,'(?ms)^\[Server\.[^\]]+\]\r?\n.*?(?=^\[|\z)','')
    $config=$config.Replace('MaxGamePlayers=500','MaxGamePlayers=2').Replace('MaxWorldPlayers=1500','MaxWorldPlayers=1').Replace('SnapshotRadius=25','SnapshotRadius=0')
    $config=$config.Replace('QueueCapacity=256','QueueCapacity=16').Replace('WorkerCount=2','WorkerCount=1')
    $config+="`r`n[Server.A]`r`nMaxPlayers=1`r`n[Server.C]`r`nMaxPlayers=32`r`n"
    $configPath=Join-Path $directory 'Server.ini'
    [IO.File]::WriteAllText($configPath,$config)
    $invalid=Join-Path $directory 'Invalid.ini'
    [IO.File]::WriteAllText($invalid,$config.Replace('MaxPlayers=1','MaxPlayers=0'))
    # 실패가 정답인 입력이다. PowerShell 5.1의 stderr 예외 대신 종료 코드로 판정한다.
    $invalidProcess=Start-Process -FilePath (Join-Path $bin 'MultiThreadServer.exe') -ArgumentList @('--config-check','A','--config',('"'+$invalid+'"')) -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $directory 'invalid.txt') -RedirectStandardError (Join-Path $directory 'invalid.err.txt')
    if ($invalidProcess.ExitCode -eq 0) { throw 'Zero capacity accepted' }
    foreach ($listener in $listeners) { $listener.Stop() }; $listeners=@()
    $ids=@('A','B','C','World')
    for ($i=0;$i -lt 4;++$i) {
        $process=Start-Process -FilePath (Join-Path $bin 'MultiThreadServer.exe') -ArgumentList @('--server',$ids[$i],'--config',('"'+$configPath+'"'),'--run-seconds','30') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $directory "$i.out.txt") -RedirectStandardError (Join-Path $directory "$i.err.txt")
        $null=$process.Handle; $servers+=$process; $ready=$false
        for ($n=0;$n -lt 100;++$n) {
            if ($process.HasExited) { throw 'Capacity server startup failed' }
            $probe=[Net.Sockets.TcpClient]::new()
            try { $probe.Connect('127.0.0.1',$ports[$i]); $ready=$true; break }
            catch { Start-Sleep -Milliseconds 50 }
            finally { $probe.Dispose() }
        }
        if (-not $ready) { throw 'Capacity server startup timed out' }
    }
    & (Join-Path $bin 'DummyClient.exe') --capacity-check --config $configPath
    if ($LASTEXITCODE -ne 0) { throw 'Capacity checks failed' }
    Write-Output "[OK] $Configuration capacity checks: override A=1, fallback B=2, World=1, zero rejected. Logs: $directory"
}
finally {
    foreach ($process in $servers) { if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }; $process.Dispose() }
    foreach ($listener in $listeners) { $listener.Stop() }
}
