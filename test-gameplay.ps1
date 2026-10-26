param([ValidateSet('Debug','Release')][string]$Configuration='Debug', [ValidateSet('IOCP','WSAPoll')][string]$NetworkMode='IOCP', [ValidateRange(1,16)][int]$BattleWorkers=4, [switch]$VerifyParallel)
$ErrorActionPreference = 'Stop'
$binaryDirectory = Join-Path $PSScriptRoot "Run64\$Configuration"
$testDirectory = Join-Path $PSScriptRoot ('MultiThreadServer\MultiThreadServer\build\gameplay-check-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $testDirectory 'Data') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $binaryDirectory 'Cluster.local.ini') -Destination $testDirectory
foreach ($file in @('Unit.csv', 'Skill.csv', 'StartPoint.csv', 'Map.ini')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Data\$file") -Destination (Join-Path $testDirectory 'Data')
}
# 랜덤 유닛 종류와 종족은 유지하고 모든 추첨 결과에서 전투를 검증하도록 테스트 수치만 통일한다.
$unitPath = Join-Path $testDirectory 'Data\Unit.csv'
$units = Import-Csv -LiteralPath $unitPath -Encoding UTF8
foreach ($unit in $units) {
    $unit.HP = '30'; $unit.Shield = '5'; $unit.Armor = '0'; $unit.MP = '100'; $unit.InitialMP = '1'
    $unit.AttackRange = '5'; $unit.AttackDamage = '20'; $unit.AttackCount = '1'
    $unit.AttackIntervalMs = '1000'; $unit.MoveSpeed = '100'; $unit.AttackType = 'Ranged'; $unit.DamageType = 'Normal'
}
$units | Export-Csv -LiteralPath $unitPath -Encoding UTF8 -NoTypeInformation
$mapPath = Join-Path $testDirectory 'Data\Map.ini'
$map = [IO.File]::ReadAllText($mapPath) -replace 'BuildingHP=1500', 'BuildingHP=15' -replace 'BuildingShield=1000', 'BuildingShield=0'
[IO.File]::WriteAllText($mapPath, $map)
$processes = @()
$listeners = @()
try {
    $ports = @()
    for ($i = 0; $i -lt 5; ++$i) {
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
        $listener.Start(); $listeners += $listener; $ports += $listener.LocalEndpoint.Port
    }
    $config = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Server.ini')).Replace('MaxWorldPlayers=1500','MaxWorldPlayers=64').Replace('SnapshotRadius=25','SnapshotRadius=0')
    $config = $config.Replace('Realm=local', ('Realm=test_' + [Guid]::NewGuid().ToString('N')))
    # 요청한 1=저그, 2=저그, 3=프로토스, 4=테란 배치를 실제 5개 프로세스로 검증한다.
    $serverRows = @('Index,Name,Race,Address,Port')
    $races = @('Zerg', 'Zerg', 'Protoss', 'Terran')
    for ($i = 0; $i -lt 4; ++$i) { $serverRows += "$($i + 1),Game $($i + 1),$($races[$i]),127.0.0.1,$($ports[$i])" }
    [IO.File]::WriteAllLines((Join-Path $testDirectory 'Data\GameServer.csv'), $serverRows, [Text.UTF8Encoding]::new($false))
    $config = $config.Replace('Port=7100', "Port=$($ports[4])")
    $config = $config.Replace('DeathReentrySeconds=100', 'DeathReentrySeconds=2').Replace('RoundRestartSeconds=300', 'RoundRestartSeconds=4')
    $configPath = Join-Path $testDirectory 'Server.ini'
    $config = $config.Replace('Mode=IOCP', "Mode=$NetworkMode")
    $config = [regex]::Replace($config, '(?ms)(\[Battle\].*?WorkerCount=)\d+', ('${1}'+$BattleWorkers))
    if ($VerifyParallel) { $config = $config.Replace('VerifyParallel=0','VerifyParallel=1').Replace('VerifySpatialSearch=0','VerifySpatialSearch=1') }
    [IO.File]::WriteAllText($configPath, $config)
    foreach ($listener in $listeners) { $listener.Stop() }
    $listeners = @()
    $ids = @('1', '2', '3', '4', 'World')
    for ($i = 0; $i -lt $ids.Count; ++$i) {
        $process = Start-Process -FilePath (Join-Path $binaryDirectory 'MultiThreadServer.exe') -ArgumentList @('--server', $ids[$i], '--config', ('"' + $configPath + '"'), '--run-seconds', '35') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $testDirectory "$($ids[$i]).out.txt") -RedirectStandardError (Join-Path $testDirectory "$($ids[$i]).err.txt")
        $null = $process.Handle
        $processes += $process
        $ready = $false
        for ($attempt = 0; $attempt -lt 100; ++$attempt) {
            if ($process.HasExited) { throw "Server $($ids[$i]) failed to start" }
            $probe = [Net.Sockets.TcpClient]::new()
            try { $probe.Connect('127.0.0.1', $ports[$i]); $ready = $true; break }
            catch { Start-Sleep -Milliseconds 50 }
            finally { $probe.Dispose() }
        }
        if (-not $ready) { throw 'Server startup timed out' }
    }
    # 게임 서버를 먼저 띄운 경우에도 World 시작 후 중계 연결이 자동으로 붙어야 한다.
    foreach ($id in @('1', '2', '3', '4')) {
        $relayReady = $false
        for ($attempt = 0; $attempt -lt 100; ++$attempt) {
            $stream = [IO.File]::Open((Join-Path $testDirectory "$id.out.txt"), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
            $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8)
            try { $relayLog = $reader.ReadToEnd() } finally { $reader.Dispose() }
            if ($relayLog.Contains('connected to World')) { $relayReady = $true; break }
            Start-Sleep -Milliseconds 50
        }
        if (-not $relayReady) { throw "Chat relay did not connect: $id" }
    }
    & (Join-Path $binaryDirectory 'DummyClient.exe') --flow-check --config $configPath
    if ($LASTEXITCODE -ne 0) { throw "Gameplay checks failed: exit code $LASTEXITCODE" }
    # 실제 메뉴에서도 입력을 기다리는 동안 복귀 로그인하는지 확인한다.
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $binaryDirectory 'DummyClient.exe'
    $start.Arguments = '--play --config "' + $configPath + '"'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.Encoding]::UTF8
    $menu = [Diagnostics.Process]::new()
    $menu.StartInfo = $start
    $previousInputEncoding = [Console]::InputEncoding
    try {
        # Windows PowerShell 5.1은 기본 입력 스트림을 만들 때 BOM을 먼저 쓸 수 있다.
        # 첫 서버 번호 앞에 BOM이 붙지 않도록 프로세스 시작 전부터 인코딩을 지정한다.
        [Console]::InputEncoding = [Text.UTF8Encoding]::new($false)
        $null = $menu.Start()
        $menuOutput = [Text.StringBuilder]::new()
        $menuLine = $menu.StandardOutput.ReadLineAsync()
        $menuError = $menu.StandardError.ReadToEndAsync()
        # PowerShell 호스트의 입력 인코딩에 영향받지 않도록 C++ 콘솔과 같은 UTF-8로 보낸다.
        $menuInput = [IO.StreamWriter]::new($menu.StandardInput.BaseStream, [Text.UTF8Encoding]::new($false))
        $menuInput.AutoFlush = $true
        $menuInput.WriteLine('2')
        $menuInput.WriteLine('InteractiveSmoke')
        $menuInput.WriteLine('PortfolioTest!2026')
        $menuInput.WriteLine('Status')
        $menuInput.WriteLine('move 60 60')
        $menuInput.WriteLine('attack building:Terran')
        $menuInput.WriteLine('auto 1')
        $menuInput.WriteLine('back')
        $menuInput.WriteLine('Chat 안녕하세요')
        # 비밀번호 해시 계산 시간이 달라도 첫 채팅의 실제 수신 시점부터 간격을 잰다.
        $chatDeadline = [DateTime]::UtcNow.AddSeconds(10)
        while (-not $menuOutput.ToString().Contains('[일반 2] InteractiveSmoke: 안녕하세요')) {
            if ([DateTime]::UtcNow -ge $chatDeadline) { throw 'First interactive chat timed out' }
            if ($menuLine.IsCompleted) {
                $line = $menuLine.GetAwaiter().GetResult()
                if ($null -eq $line) { throw 'Interactive client closed before first chat' }
                $null = $menuOutput.AppendLine($line)
                $menuLine = $menu.StandardOutput.ReadLineAsync()
            } else { Start-Sleep -Milliseconds 20 }
        }
        Start-Sleep -Milliseconds 700
        $menuInput.WriteLine('Chat 월드 월드 채팅 확인')
        $menuInput.WriteLine('World')
        $menuInput.WriteLine('auto off')
        Start-Sleep -Milliseconds 1000
        $menuInput.WriteLine('status')
        $menuInput.WriteLine('Chat 일반 전장 채팅 확인')
        $menuInput.WriteLine('Chat 월드 전장에서는 거부')
        $menuInput.WriteLine('back')
        Start-Sleep -Milliseconds 1200
        $menuInput.WriteLine('status')
        $menuInput.WriteLine('chat 복귀 후 채팅')
        $menuInput.WriteLine('Server')
        $menuInput.WriteLine('3')
        $menuInput.WriteLine('InteractiveSwitch')
        $menuInput.WriteLine('PortfolioTest!2026')
        $menuInput.WriteLine('Status')
        $menuInput.WriteLine('Chat 서버 변경 후 채팅')
        Start-Sleep -Milliseconds 500
        $menuInput.WriteLine('Quit')
        $menuInput.Dispose()
        if (-not $menu.WaitForExit(8000)) { throw 'Interactive dummy did not stop' }
        $null = $menuOutput.AppendLine($menuLine.GetAwaiter().GetResult())
        $null = $menuOutput.Append($menu.StandardOutput.ReadToEnd())
        $text = $menuOutput.ToString()
        [IO.File]::WriteAllText((Join-Path $testDirectory 'interactive.txt'), $text)
        if ($menu.ExitCode -ne 0 -or -not $text.Contains('[GAME LOGOUT] 2') -or
            -not $text.Contains('[OK] Entered World') -or -not $text.Contains('[GAME LOGIN] 2') -or $text.Contains('[FAIL]') -or
            -not $text.Contains('[GAME 명령] world / status / server / quit / chat 일반|월드 내용') -or
            ([regex]::Matches($text, '\[DENIED\] GameServer')).Count -ne 4 -or
            -not $text.Contains('[일반 2] InteractiveSmoke: 안녕하세요') -or
            -not $text.Contains('[월드 2] InteractiveSmoke: 월드 채팅 확인') -or
            -not $text.Contains('[일반 World] InteractiveSmoke: 전장 채팅 확인') -or
            -not $text.Contains('[DENIED] World battle only allows local chat') -or
            -not $text.Contains('[일반 2] InteractiveSmoke: 복귀 후 채팅') -or
            -not $text.Contains('[일반 3] InteractiveSwitch: 서버 변경 후 채팅') -or -not $text.Contains('[GAME] 3')) {
            throw ('Interactive flow failed. ' + $text + $menuError.Result)
        }
        Write-Output '[OK] Interactive menu: game-only commands, Korean chat, World/back, return chat, server switch, case-insensitive commands'
    }
    finally {
        if (-not $menu.HasExited) { $menu.Kill() }
        $menu.Dispose()
        [Console]::InputEncoding = $previousInputEncoding
    }
    foreach ($process in $processes) {
        if (-not $process.WaitForExit(30000)) { throw 'Server shutdown timed out' }
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw 'Server exit failed' }
    }
    Write-Output "[OK] $Configuration gameplay checks passed. Logs: $testDirectory"
}
finally {
    foreach ($listener in $listeners) { $listener.Stop() }
    foreach ($process in $processes) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        $process.Dispose()
    }
}
