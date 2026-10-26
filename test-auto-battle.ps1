param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$binaryDirectory = Join-Path $PSScriptRoot "Run64\$Configuration"
$testDirectory = Join-Path $PSScriptRoot ('MultiThreadServer\MultiThreadServer\build\auto-battle-check-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
$processes = @()
$listeners = @()
$menus = @()
$previousInputEncoding = [Console]::InputEncoding

function Read-Menu($menu) {
    while ($null -ne $menu.Pending -and $menu.Pending.IsCompleted) {
        $line = $menu.Pending.GetAwaiter().GetResult()
        if ($null -eq $line) { $menu.Pending = $null; break }
        $null = $menu.Text.AppendLine($line)
        $menu.Pending = $menu.Process.StandardOutput.ReadLineAsync()
    }
}

function Wait-Menu($menu, [string]$expected, [int]$seconds = 15) {
    $deadline = [DateTime]::UtcNow.AddSeconds($seconds)
    do {
        Read-Menu $menu
        if ($menu.Text.ToString().Contains($expected)) { return }
        if ($menu.Process.HasExited) { break }
        Start-Sleep -Milliseconds 25
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Missing '$expected' in $($menu.Name): $($menu.Text)"
}

function Start-Menu([string]$configPath, [string]$server, [string]$name) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $binaryDirectory 'DummyClient.exe'
    $start.Arguments = '--play --config "' + $configPath + '"'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.Encoding]::UTF8
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $null = $process.Start()
    $writer = [IO.StreamWriter]::new($process.StandardInput.BaseStream, [Text.UTF8Encoding]::new($false))
    $writer.AutoFlush = $true
    $menu = [pscustomobject]@{ Process = $process; Input = $writer; Name = $name; Text = [Text.StringBuilder]::new(); Pending = $process.StandardOutput.ReadLineAsync(); Error = $process.StandardError.ReadToEndAsync() }
    $script:menus += $menu
    $writer.WriteLine($server)
    $writer.WriteLine($name)
    $writer.WriteLine('PortfolioTest!2026')
    # auto 명령 없이 입장만으로 시작해야 한다.
    $writer.WriteLine('World')
    Wait-Menu $menu '[AUTO] 목표:'
    return $menu
}

function Stop-Menu($menu) {
    $menu.Input.WriteLine('Quit')
    $menu.Input.Dispose()
    if (-not $menu.Process.WaitForExit(8000)) { throw 'Dummy did not stop' }
    Read-Menu $menu
    [IO.File]::WriteAllText((Join-Path $testDirectory "$($menu.Name).txt"), $menu.Text.ToString())
    if ($menu.Process.ExitCode -ne 0 -or $menu.Text.ToString().Contains('[FAIL]') -or $menu.Error.Result) {
        throw "Dummy failed: $($menu.Text) $($menu.Error.Result)"
    }
}

function Start-Servers([bool]$noAttack) {
    $scenario = if ($noAttack) { 'support' } else { 'attack' }
    $directory = Join-Path $testDirectory $scenario
    New-Item -ItemType Directory -Path (Join-Path $directory 'Data') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $binaryDirectory 'Cluster.local.ini') -Destination $directory
    foreach ($file in @('Unit.csv', 'Skill.csv', 'StartPoint.csv', 'Map.ini')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Data\$file") -Destination (Join-Path $directory 'Data')
    }
    # 운영 CSV는 그대로 두고, 랜덤 결과와 무관하게 근접 공격/비공격 동작을 각각 검증한다.
    $unitPath = Join-Path $directory 'Data\Unit.csv'
    $units = Import-Csv -LiteralPath $unitPath -Encoding UTF8
    foreach ($unit in $units) {
        $unit.MoveSpeed = '80'
        $unit.AttackRange = '1'; $unit.AttackDamage = '10'; $unit.AttackCount = '1'
        $unit.AttackIntervalMs = '250'; $unit.AttackType = 'Melee'; $unit.DamageType = 'Normal'
        if ($noAttack) {
            $unit.AttackRange = '0'; $unit.AttackDamage = '0'; $unit.AttackCount = '0'
            $unit.AttackIntervalMs = '0'; $unit.AttackType = 'None'; $unit.DamageType = 'None'
        }
    }
    $units | Export-Csv -LiteralPath $unitPath -Encoding UTF8 -NoTypeInformation
    $mapPath = Join-Path $directory 'Data\Map.ini'
    $map = [IO.File]::ReadAllText($mapPath).Replace('BuildingHP=1500', 'BuildingHP=40').Replace('BuildingShield=1000', 'BuildingShield=10')
    # 저그 시작점과 넥서스 사이에 아군 건물을 놓아 직선 이동 차단과 중앙 경유를 확인한다.
    $map = $map.Replace("[Zerg]`r`nBuilding=Hive`r`nBuildingX=60`r`nBuildingY=8", "[Zerg]`r`nBuilding=Hive`r`nBuildingX=86`r`nBuildingY=38")
    $map = $map.Replace("[Zerg]`nBuilding=Hive`nBuildingX=60`nBuildingY=8", "[Zerg]`nBuilding=Hive`nBuildingX=86`nBuildingY=38")
    if (-not $map.Contains('BuildingX=86')) { throw 'Obstacle fixture was not applied' }
    [IO.File]::WriteAllText($mapPath, $map)
    $ports = @()
    for ($i = 0; $i -lt 4; ++$i) {
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
        $listener.Start(); $script:listeners += $listener; $ports += $listener.LocalEndpoint.Port
    }
    $rows = @('Index,Name,Race,Address,Port', "A,Game A,Zerg,127.0.0.1,$($ports[0])", "B,Game B,Protoss,127.0.0.1,$($ports[1])", "C,Game C,Terran,127.0.0.1,$($ports[2])")
    [IO.File]::WriteAllLines((Join-Path $directory 'Data\GameServer.csv'), $rows)
    $config = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Server.ini')).Replace('MaxWorldPlayers=1500','MaxWorldPlayers=64').Replace('SnapshotRadius=25','SnapshotRadius=0').Replace('Port=7100', "Port=$($ports[3])").Replace('RoundRestartSeconds=300', 'RoundRestartSeconds=1')
    $configPath = Join-Path $directory 'Server.ini'
    $config = $config.Replace('Realm=local', ('Realm=test_' + [Guid]::NewGuid().ToString('N')))
    [IO.File]::WriteAllText($configPath, $config)
    foreach ($listener in $script:listeners) { $listener.Stop() }
    $script:listeners = @()
    $ids = @('A', 'B', 'C', 'World')
    for ($i = 0; $i -lt $ids.Count; ++$i) {
        $process = Start-Process -FilePath (Join-Path $binaryDirectory 'MultiThreadServer.exe') -ArgumentList @('--server', $ids[$i], '--config', ('"' + $configPath + '"'), '--run-seconds', '90') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $directory "$($ids[$i]).out.txt") -RedirectStandardError (Join-Path $directory "$($ids[$i]).err.txt")
        $null = $process.Handle
        $script:processes += $process
        $ready = $false
        for ($attempt = 0; $attempt -lt 100; ++$attempt) {
            if ($process.HasExited) { throw "Server $($ids[$i]) failed" }
            $probe = [Net.Sockets.TcpClient]::new()
            try { $probe.Connect('127.0.0.1', $ports[$i]); $ready = $true; break }
            catch { Start-Sleep -Milliseconds 50 }
            finally { $probe.Dispose() }
        }
        if (-not $ready) { throw 'Server startup timed out' }
    }
    return $configPath
}

function Stop-Servers {
    # 이 테스트가 시작한 PID만 정리한다. 사용자가 실행한 서버는 건드리지 않는다.
    foreach ($process in $script:processes) {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
    $script:processes = @()
}

try {
    [Console]::InputEncoding = [Text.UTF8Encoding]::new($false)
    $configPath = Start-Servers $false
    foreach ($server in @('A', 'B', 'C')) {
        $name = "Auto_$server"
        $menu = Start-Menu $configPath $server $name
        if ($server -eq 'A') {
            $menu.Input.WriteLine('auto off')
            Wait-Menu $menu '[AUTO] 자동 공략 중지.'
            Start-Sleep -Milliseconds 400
            $menu.Input.WriteLine('status')
            Wait-Menu $menu 'auto=off'
            $menu.Input.WriteLine('Chat 일반 자동 중지 확인')
            Wait-Menu $menu '[일반 World] Auto_A: 자동 중지 확인'
            $menu.Input.WriteLine('auto on')
            $menu.Input.WriteLine('status')
            Wait-Menu $menu 'auto=on'
        }
        # 콘솔 입력 없이 두 적 건물을 차례로 파괴하고 원래 게임 서버에 돌아와야 한다.
        Wait-Menu $menu "[GAME LOGIN] $server"
        $text = $menu.Text.ToString()
        $race = @{ A = 'Zerg'; B = 'Protoss'; C = 'Terran' }[$server]
        $winner = @{ A = 3; B = 2; C = 1 }[$server]
        $targets = @([regex]::Matches($text, '\[AUTO\] 목표: (building:\w+)') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
        if ($targets.Count -ne 2 -or $targets -contains "building:$race" -or
            -not $text.Contains("winner=$winner") -or ([regex]::Matches($text, 'attacked \w+ HP=0')).Count -ne 2 -or
            $text.Contains('경로가 없습니다') -or $text.Contains('서버가 자동 공략 요청을 거부')) {
            throw "Automatic building battle failed: $text"
        }
        Stop-Menu $menu
        Write-Output "[OK] ${server}: automatic entry, two enemy buildings destroyed, own faction wins, original game login"
        Start-Sleep -Milliseconds 1400
    }
    Stop-Servers
    $configPath = Start-Servers $true
    $menu = Start-Menu $configPath 'A' 'Support'
    Start-Sleep -Seconds 2
    $menu.Input.WriteLine('status')
    Wait-Menu $menu 'auto=on'
    $menu.Input.WriteLine('back')
    Wait-Menu $menu '[GAME LOGIN] A'
    if ($menu.Text.ToString().Contains('attacked ')) { throw 'Non-attacking unit caused damage' }
    Stop-Menu $menu
    Write-Output '[OK] Non-attacking unit approaches safely and waits; back remains available'
    Write-Output "[OK] $Configuration automatic building battle checks passed. Logs: $testDirectory"
}
finally {
    foreach ($menu in $menus) {
        if (-not $menu.Process.HasExited) { $menu.Process.Kill(); $menu.Process.WaitForExit() }
        Read-Menu $menu
        [IO.File]::WriteAllText((Join-Path $testDirectory "$($menu.Name).txt"), $menu.Text.ToString())
        $menu.Input.Dispose(); $menu.Process.Dispose()
    }
    foreach ($listener in $listeners) { $listener.Stop() }
    Stop-Servers
    [Console]::InputEncoding = $previousInputEncoding
}
