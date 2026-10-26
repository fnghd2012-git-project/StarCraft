param([ValidateSet('Debug','Release')][string]$Configuration='Debug', [ValidateSet('IOCP','WSAPoll')][string]$NetworkMode='IOCP', [ValidateRange(1,16)][int]$BattleWorkers=4, [switch]$VerifyParallel)
$ErrorActionPreference = 'Stop'
$binaryDirectory = Join-Path $PSScriptRoot "Run64\$Configuration"
$testDirectory = Join-Path $PSScriptRoot ('MultiThreadServer\MultiThreadServer\build\account-skills-' + [Guid]::NewGuid().ToString('N'))
$realm = 'test_' + [Guid]::NewGuid().ToString('N')
$processes = @(); $listeners = @()
New-Item -ItemType Directory -Path (Join-Path $testDirectory 'Data') -Force | Out-Null
try {
    Copy-Item -LiteralPath (Join-Path $binaryDirectory 'Cluster.local.ini') -Destination $testDirectory
    foreach ($file in @('Unit.csv', 'Skill.csv', 'StartPoint.csv', 'Map.ini')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Data\$file") -Destination (Join-Path $testDirectory 'Data')
    }
    # 원본 밸런스는 유지한다. 테스트 데이터에서만 체력/속도/지속 시간을 조정한다.
    $unitPath = Join-Path $testDirectory 'Data\Unit.csv'
    $units = Import-Csv -LiteralPath $unitPath -Encoding UTF8
    foreach ($unit in $units) {
        $unit.HP = '2000'; $unit.MP = '1000'; $unit.InitialMP = '1000'; $unit.Armor = '0'; $unit.MoveSpeed = '100'
        if ($unit.AttackType -ne 'None') { $unit.AttackRange = '10' }
        if ($unit.Name -eq 'Zealot') { $unit.IsMechanical = '1'; $unit.Shield = '80' }
        if ($unit.Name -eq 'Goliath') { $unit.Armor = '3' }
    }
    # MP 부족 거부를 오래 기다리지 않고 검증하는 테스트 전용 유닛이다.
    $lowMP = ($units | Where-Object Name -eq 'HighTemplar').PSObject.Copy()
    $lowMP.Name = 'LowMPTemplar'; $lowMP.UnitIndex = '203'; $lowMP.MP = '20'; $lowMP.InitialMP = '0'
    $units = @($units) + @($lowMP)
    $units | Export-Csv -LiteralPath $unitPath -Encoding UTF8 -NoTypeInformation
    $skillPath = Join-Path $testDirectory 'Data\Skill.csv'
    $skills = Import-Csv -LiteralPath $skillPath -Encoding UTF8
    foreach ($skill in $skills) {
        if ($skill.SkillType -in @('Stim','Lockdown','Storm','DarkSwarm','Plague')) { $skill.DurationMs = '1000' }
        if ($skill.SkillType -in @('Storm','Plague')) { $skill.TickIntervalMs = '200' }
        if ($skill.SkillType -eq 'Plague') { $skill.Power = '1000000' }
    }
    $skills | Export-Csv -LiteralPath $skillPath -Encoding UTF8 -NoTypeInformation
    $ports = @()
    for ($i = 0; $i -lt 4; ++$i) {
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
        $listener.Start(); $listeners += $listener; $ports += $listener.LocalEndpoint.Port
    }
    [IO.File]::WriteAllLines((Join-Path $testDirectory 'Data\GameServer.csv'), @('Index,Name,Race,Address,Port', "A,Game A,Zerg,127.0.0.1,$($ports[0])", "B,Game B,Protoss,127.0.0.1,$($ports[1])", "C,Game C,Terran,127.0.0.1,$($ports[2])"))
    $configPath = Join-Path $testDirectory 'Server.ini'
    $config = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Server.ini')).Replace('MaxWorldPlayers=1500','MaxWorldPlayers=64').Replace('SnapshotRadius=25','SnapshotRadius=0').Replace('Port=7100', "Port=$($ports[3])").Replace('Realm=local', "Realm=$realm").Replace('DeathReentrySeconds=100', 'DeathReentrySeconds=5')
    $config = $config.Replace('Mode=IOCP', "Mode=$NetworkMode")
    $config = [regex]::Replace($config, '(?ms)(\[Battle\].*?WorkerCount=)\d+', ('${1}'+$BattleWorkers))
    if ($VerifyParallel) { $config = $config.Replace('VerifyParallel=0','VerifyParallel=1').Replace('VerifySpatialSearch=0','VerifySpatialSearch=1') }
    [IO.File]::WriteAllText($configPath, $config)
    # 테스트 전용 Realm에 유닛을 준비한다. 평문 비밀번호는 DB에 넣지 않는다.
    $database = [Data.SqlClient.SqlConnection]::new('Server=tcp:127.0.0.1,1433;Database=project_db;Integrated Security=true;Encrypt=true;TrustServerCertificate=true;Connection Timeout=5')
    try {
        $database.Open()
        $seedUnits = @($units)
        $secondTemplar = ($units | Where-Object Name -eq 'HighTemplar').PSObject.Copy()
        $secondTemplar.Name = 'HighTemplar2'; $seedUnits += $secondTemplar
        foreach ($unit in $seedUnits) {
            $salt = [Guid]::NewGuid().ToString('N')
            $derive = [Security.Cryptography.Rfc2898DeriveBytes]::new('PortfolioTest!2026', [Text.Encoding]::UTF8.GetBytes($salt), 600000, [Security.Cryptography.HashAlgorithmName]::SHA256)
            try { $hash = [BitConverter]::ToString($derive.GetBytes(32)).Replace('-', '').ToLowerInvariant() }
            finally { $derive.Dispose() }
            $server = @{ Terran='C'; Protoss='B'; Zerg='A' }[$unit.Race]
            $command = $database.CreateCommand()
            try {
                $command.CommandText = 'INSERT dbo.PortfolioAccount(Realm,AccountId,PlayerId,PasswordSalt,PasswordHash) VALUES(@realm,@name,@player,@salt,@hash); INSERT dbo.PortfolioUnit(Realm,AccountId,ServerId,UnitIndex,Race) VALUES(@realm,@name,@server,@unit,@race);'
                foreach ($entry in @{ realm=$realm; name=$unit.Name; player=[Guid]::NewGuid().ToString('N'); salt=$salt; hash=$hash; server=$server; unit=[int]$unit.UnitIndex; race=$unit.Race }.GetEnumerator()) { $null = $command.Parameters.AddWithValue('@' + $entry.Key, $entry.Value) }
                $null = $command.ExecuteNonQuery()
            }
            finally { $command.Dispose() }
        }
    }
    finally { $database.Dispose() }
    foreach ($listener in $listeners) { $listener.Stop() }; $listeners = @()
    $ids = @('A','B','C','World')
    for ($i = 0; $i -lt 4; ++$i) {
        $process = Start-Process -FilePath (Join-Path $binaryDirectory 'MultiThreadServer.exe') -ArgumentList @('--server', $ids[$i], '--config', ('"' + $configPath + '"'), '--run-seconds', '180') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $testDirectory "$($ids[$i]).out.txt") -RedirectStandardError (Join-Path $testDirectory "$($ids[$i]).err.txt")
        $null = $process.Handle; $processes += $process
        $ready = $false
        for ($attempt = 0; $attempt -lt 100; ++$attempt) {
            if ($process.HasExited) { throw "Server $($ids[$i]) failed: $(Get-Content -LiteralPath (Join-Path $testDirectory "$($ids[$i]).err.txt") -Raw)" }
            $probe = [Net.Sockets.TcpClient]::new()
            try { $probe.Connect('127.0.0.1', $ports[$i]); $ready = $true; break }
            catch { Start-Sleep -Milliseconds 50 }
            finally { $probe.Dispose() }
        }
        if (-not $ready) { throw 'Server startup timed out' }
    }
    foreach ($mode in @('--account-check','--skill-check')) {
        & (Join-Path $binaryDirectory 'DummyClient.exe') $mode --config $configPath
        if ($LASTEXITCODE -ne 0) { throw "$mode failed. Logs: $testDirectory" }
    }
    Write-Output "[OK] $Configuration account/skill checks passed. Logs: $testDirectory"
}
finally {
    foreach ($listener in $listeners) { $listener.Stop() }
    foreach ($process in $processes) {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}
