param(
    [ValidateRange(1,64)][int]$PerServer = 10,
    [ValidateRange(30,1800)][int]$TimeoutSeconds = 600,
    [switch]$NoReplay
)
$ErrorActionPreference = 'Stop'
$binaryDirectory = $PSScriptRoot
$dummy = Join-Path $binaryDirectory 'DummyClient.exe'
$serverExe = Join-Path $binaryDirectory 'MultiThreadServer.exe'
$sourceConfig = Join-Path $binaryDirectory 'Server.ini'
$runId = [Guid]::NewGuid().ToString('N')
$directory = Join-Path $binaryDirectory ('MatchRuns\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $runId)
$servers = @(); $listeners = @(); $menus = @(); $replayPath = ''
$oldEncoding = [Console]::InputEncoding
$configText = [IO.File]::ReadAllText($sourceConfig)

function Config-Value([string]$Key) {
    $found = [regex]::Match($configText, '(?m)^' + [regex]::Escape($Key) + '=([^\r\n]+)')
    if (-not $found.Success) { throw "Missing setting: $Key" }
    return $found.Groups[1].Value.Trim()
}
function Source-Path([string]$Key) {
    $value = Config-Value $Key
    if ([IO.Path]::IsPathRooted($value)) { return $value }
    return Join-Path $binaryDirectory $value
}

# 더미의 기존 대화형 모드를 그대로 사용한다. 출력을 계속 비워 파이프가 차서 전투가 멈추지 않게 한다.
function Read-Menus {
    foreach ($menu in $script:menus) {
        while ($null -ne $menu.Pending -and $menu.Pending.IsCompleted) {
            $line = $menu.Pending.GetAwaiter().GetResult()
            if ($null -eq $line) { $menu.Pending = $null; break }
            $menu.Log.WriteLine($line)
            if ($menu.Tail.Count -ge 8) { $null = $menu.Tail.Dequeue() }; $menu.Tail.Enqueue($line)
            if ($line.StartsWith('[GAME] ')) { $menu.LoggedIn = $true }
            if ($line.Contains('[OK] Entered World')) { $menu.Entered = $true }
            if ($line.Contains('auto=off')) { $menu.Paused = $true }
            if ($line.StartsWith('[GAME LOGIN] ')) { $menu.Returned = $true }
            if ($line.StartsWith($menu.Name + ' / ')) { $menu.Unit = [regex]::Match($line, ' / ([^/]+ \(#\d+\))').Groups[1].Value }
            if ($line -match '^\[WORLD\] tick=\d+ players=(\d+)') { $menu.Players = [int]$Matches[1] }
            if ($line.StartsWith('building:')) { $menu.Buildings[($line.Split(' ')[0])] = $line }
            if ($line.StartsWith('[FAIL]') -or $line.StartsWith('[DISCONNECTED]')) { $menu.Failure = $line }
            $menu.Pending = $menu.Process.StandardOutput.ReadLineAsync()
        }
    }
}
function Wait-Menus([string]$Property, [int]$Seconds) {
    $until = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        Read-Menus
        $waiting = @($script:menus | Where-Object { -not $_.$Property })
        if ($waiting.Count -eq 0) { return }
        foreach ($menu in $waiting) {
            if ($menu.Failure -or $menu.Process.HasExited) { throw "$($menu.Name): $($menu.Failure) / $($menu.Tail -join ' | ')" }
        }
        Start-Sleep -Milliseconds 20
    } while ([DateTime]::UtcNow -lt $until)
    throw "Timed out waiting for $Property : $($waiting.Name -join ', '). Logs: $directory"
}

try {
    if (-not (Test-Path -LiteralPath $dummy) -or -not (Test-Path -LiteralPath $serverExe)) { throw 'Build the server and dummy first.' }
    $gameServers = @(Import-Csv -LiteralPath (Source-Path 'ServerFile') -Encoding UTF8)
    $total = $gameServers.Count * $PerServer
    if ($total -lt 1 -or $total -gt 64) { throw "World supports 1..64 players. Requested: $total" }
    New-Item -ItemType Directory -Path (Join-Path $directory 'Data') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $binaryDirectory 'Cluster.local.ini') -Destination $directory
    # 유닛/스킬/맵/시작점 수치를 바꾸지 않는다. 접속 포트와 DB Realm만 시연용으로 분리한다.
    foreach ($entry in @{ UnitFile='Unit.csv'; SkillFile='Skill.csv'; MapFile='Map.ini'; StartPointFile='StartPoint.csv' }.GetEnumerator()) {
        Copy-Item -LiteralPath (Source-Path $entry.Key) -Destination (Join-Path $directory ('Data\' + $entry.Value))
        $configText = [regex]::Replace($configText, '(?m)^' + $entry.Key + '=[^\r\n]*', ($entry.Key + '=Data/' + $entry.Value))
    }
    $ports = @()
    for ($i = 0; $i -le $gameServers.Count; ++$i) {
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
        $listener.Start(); $listeners += $listener; $ports += $listener.LocalEndpoint.Port
    }
    for ($i = 0; $i -lt $gameServers.Count; ++$i) { $gameServers[$i].Address = '127.0.0.1'; $gameServers[$i].Port = $ports[$i] }
    $gameServers | Export-Csv -LiteralPath (Join-Path $directory 'Data\GameServer.csv') -NoTypeInformation -Encoding UTF8
    $configText = [regex]::Replace($configText, '(?m)^ServerFile=[^\r\n]*', 'ServerFile=Data/GameServer.csv')
    $configText = [regex]::Replace($configText, '(?m)^Realm=[^\r\n]*', ('Realm=demo_' + $runId))
    $configText = [regex]::Replace($configText, '(?m)^Address=[^\r\n]*', 'Address=127.0.0.1')
    $configText = [regex]::Replace($configText, '(?m)^Port=[^\r\n]*', ('Port=' + $ports[-1]))
    $configText = [regex]::Replace($configText, '(?m)^MaxWorldPlayers=[^\r\n]*', 'MaxWorldPlayers=64')
    $configText = [regex]::Replace($configText, '(?m)^Enabled=[^\r\n]*', 'Enabled=1')
    $configText = [regex]::Replace($configText, '(?m)^Directory=[^\r\n]*', 'Directory=Replays')
    # 경기 종료 후 복귀/정리를 마칠 여유를 둔다. 두 번째 경기는 진행하지 않는다.
    $configText = [regex]::Replace($configText, '(?m)^RoundRestartSeconds=[^\r\n]*', 'RoundRestartSeconds=300')
    $configText = [regex]::Replace($configText, '(?m)^SnapshotRadius=[^\r\n]*', 'SnapshotRadius=0')
    $configPath = Join-Path $directory 'Server.ini'; [IO.File]::WriteAllText($configPath, $configText)
    & $serverExe --config-check World --config $configPath
    if ($LASTEXITCODE -ne 0) { throw 'Match configuration failed validation' }
    foreach ($listener in $listeners) { $listener.Stop() }; $listeners = @()
    $ids = @($gameServers | ForEach-Object { $_.Index }) + @('World')
    for ($i = 0; $i -lt $ids.Count; ++$i) {
        $process = Start-Process -FilePath $serverExe -ArgumentList @('--server',$ids[$i],'--config',('"' + $configPath + '"'),'--run-seconds',($TimeoutSeconds + 150)) -WorkingDirectory $binaryDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $directory "server-$i.out.txt") -RedirectStandardError (Join-Path $directory "server-$i.err.txt")
        $null = $process.Handle; $servers += $process; $ready = $false
        for ($attempt = 0; $attempt -lt 100; ++$attempt) {
            if ($process.HasExited) { throw "Server $($ids[$i]) failed. Logs: $directory" }
            $probe = [Net.Sockets.TcpClient]::new()
            try { $probe.Connect('127.0.0.1',$ports[$i]); $ready = $true; break }
            catch { Start-Sleep -Milliseconds 50 }
            finally { $probe.Dispose() }
        }
        if (-not $ready) { throw "Server $($ids[$i]) startup timed out" }
    }
    [Console]::InputEncoding = [Text.UTF8Encoding]::new($false)
    $group = 0
    foreach ($gameServer in $gameServers) {
        ++$group
        for ($number = 1; $number -le $PerServer; ++$number) {
            $name = 'Demo_' + $group + '_' + $number.ToString('00')
            $start = [Diagnostics.ProcessStartInfo]::new(); $start.FileName = $dummy
            $start.Arguments = '--play --config "' + $configPath + '"'; $start.WorkingDirectory = $binaryDirectory
            $start.UseShellExecute = $false; $start.CreateNoWindow = $true
            $start.RedirectStandardInput = $true; $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
            $start.StandardOutputEncoding = [Text.Encoding]::UTF8
            $process = [Diagnostics.Process]::new(); $process.StartInfo = $start; $null = $process.Start()
            $writer = [IO.StreamWriter]::new($process.StandardInput.BaseStream, [Text.UTF8Encoding]::new($false)); $writer.AutoFlush = $true
            $menu = [pscustomobject]@{ Name=$name; Server=$gameServer.Index; Unit=''; Process=$process; Input=$writer; Pending=$process.StandardOutput.ReadLineAsync(); Error=$process.StandardError.ReadToEndAsync(); Log=[IO.StreamWriter]::new((Join-Path $directory "$name.txt"), $false, [Text.UTF8Encoding]::new($false)); Tail=[Collections.Generic.Queue[string]]::new(); LoggedIn=$false; Entered=$false; Paused=$false; Returned=$false; Players=0; Buildings=@{}; Failure='' }
            $menus += $menu
            $writer.WriteLine($gameServer.Index); $writer.WriteLine($name); $writer.WriteLine('PortfolioDemo!2026')
        }
        Wait-Menus 'LoggedIn' 60
        Write-Host "[LOGIN] $($gameServer.Index): $PerServer players / random units assigned"
    }
    foreach ($menu in $menus) { $menu.Input.WriteLine('world'); $menu.Input.WriteLine('auto off'); $menu.Input.WriteLine('status') }
    Wait-Menus 'Entered' 60; Wait-Menus 'Paused' 20
    $menus | Select-Object Server,Name,Unit | Export-Csv -LiteralPath (Join-Path $directory 'participants.csv') -Encoding UTF8 -NoTypeInformation
    $until = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $menus[0].Input.WriteLine('status'); Start-Sleep -Milliseconds 200; Read-Menus
        if ($menus[0].Players -eq $total) { break }
    } while ([DateTime]::UtcNow -lt $until)
    if ($menus[0].Players -ne $total) { throw "World population mismatch: $($menus[0].Players)/$total" }
    Write-Host "[START] $total players in World. All groups start automatic building attacks."
    Write-Host '[INFO] Building-only mode preserves the original one-match demo. For unit combat and reentry use StartBattle1500.cmd.'
    foreach ($menu in $menus) { $menu.Input.WriteLine('auto buildings') }
    $started = [DateTime]::UtcNow; $nextStatus = $started
    while (([DateTime]::UtcNow - $started).TotalSeconds -lt $TimeoutSeconds) {
        Read-Menus
        foreach ($menu in $menus) { if ($menu.Failure -or $menu.Process.HasExited) { throw "$($menu.Name): $($menu.Failure) / $($menu.Tail -join ' | ')" } }
        foreach ($process in $servers) { if ($process.HasExited) { throw "Match server stopped. Logs: $directory" } }
        # 확정 Footer까지 쓴 .replay만 경기 종료 후보로 사용한다. .part는 성공으로 처리하지 않는다.
        $completed = Get-ChildItem -LiteralPath (Join-Path $directory 'Replays') -Filter '*.replay' | Select-Object -First 1
        if ($completed) { $replayPath = $completed.FullName; break }
        if ([DateTime]::UtcNow -ge $nextStatus) {
            $nextStatus = [DateTime]::UtcNow.AddSeconds(5)
            $stream = [IO.File]::Open((Join-Path $directory ("server-$($gameServers.Count).err.txt")), 'Open', 'Read', 'ReadWrite')
            $reader = [IO.StreamReader]::new($stream)
            try { $serverError = $reader.ReadToEnd() } finally { $reader.Dispose() }
            if ($serverError.Contains('[REPLAY FAIL]')) { throw "Recording failed. Logs: $directory" }
            $active = @($menus | Where-Object { -not $_.Returned })
            $observer = $active | Select-Object -First 1
            Write-Host ("[MATCH] {0}s / returned={1}/{2}" -f [int]([DateTime]::UtcNow - $started).TotalSeconds, ($total - $active.Count), $total)
            if ($observer) {
                foreach ($key in @($observer.Buildings.Keys | Sort-Object)) { Write-Host ('  ' + $observer.Buildings[$key]) }
                $observer.Input.WriteLine('status')
            }
            foreach ($menu in $menus) { $menu.Log.Flush() }
        }
        Start-Sleep -Milliseconds 20
    }
    if (-not $replayPath) { throw "Match did not finish within $TimeoutSeconds seconds. See logs: $directory" }
    Wait-Menus 'Returned' 20
    foreach ($menu in $menus) { $menu.Input.WriteLine('quit') }
    $until = [DateTime]::UtcNow.AddSeconds(15)
    while (@($menus | Where-Object { -not $_.Process.HasExited }).Count -gt 0 -and [DateTime]::UtcNow -lt $until) { Read-Menus; Start-Sleep -Milliseconds 20 }
    Read-Menus
    foreach ($menu in $menus) { if (-not $menu.Process.HasExited -or $menu.Process.ExitCode -ne 0 -or $menu.Failure -or $menu.Error.Result) { throw "Dummy exit failed: $($menu.Name) / $($menu.Tail -join ' | ')" } }
    $verification = (& $dummy --replay $replayPath --verify --require Attack) -join "`r`n"
    if ($LASTEXITCODE -ne 0 -or $verification -notmatch 'reason=round_finished' -or $verification -notmatch ('(?m)^\[ACTION\] Enter=' + $total + '\r?$')) { throw "Match recording verification failed: $verification" }
    [IO.File]::WriteAllText((Join-Path $directory 'result.txt'), $verification)
    Write-Host $verification
    if ($verification -match 'winner=(\d+)') {
        $winner = @{ '0'='Draw'; '1'='Terran'; '2'='Protoss'; '3'='Zerg' }[$Matches[1]]
        Write-Host "[RESULT] Winner: $winner"
    }
    $outputDirectory = Join-Path $binaryDirectory 'Replays'; New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $saved = Join-Path $outputDirectory ("Match-$($PerServer)x$($gameServers.Count)-" + $completed.Name)
    Copy-Item -LiteralPath $replayPath -Destination $saved
    $replayPath = $saved
    Write-Host "[OK] One match completed; all $total players returned and logged out."
    Write-Host "[REPLAY FILE] $replayPath"
    Write-Host "[LOGS] $directory"
}
finally {
    # 이 실행이 만든 프로세스만 정리한다. 개인 테스트 서버와 기존 리플레이는 유지한다.
    foreach ($menu in $menus) { if (-not $menu.Process.HasExited) { $menu.Process.Kill(); $menu.Process.WaitForExit() } }
    Read-Menus
    foreach ($menu in $menus) { $menu.Input.Dispose(); $menu.Log.Dispose(); $menu.Process.Dispose() }
    foreach ($process in $servers) { if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }; $process.Dispose() }
    foreach ($listener in $listeners) { $listener.Stop() }
    [Console]::InputEncoding = $oldEncoding
}
if (-not $NoReplay) {
    Write-Host '[REPLAY] Opening 2D viewer. Click Play (default 1x); change speed only when needed.'
    & $dummy --replay $replayPath
    if ($LASTEXITCODE -ne 0) { throw 'Replay player failed' }
}
