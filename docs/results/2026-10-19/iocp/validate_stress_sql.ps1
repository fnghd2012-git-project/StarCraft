param([string]$report)
$ErrorActionPreference = 'Stop'
$directory = [IO.File]::ReadAllText((Join-Path $report 'run-directory.txt')).Trim()
$config = [IO.File]::ReadAllText((Join-Path $directory 'Server.ini'))
$realm = [regex]::Match($config, '(?m)^Realm=([^\r\n]+)').Groups[1].Value
if (-not $realm.StartsWith('battle_')) { throw 'Expected isolated battle Realm' }
$connection = [Data.SqlClient.SqlConnection]::new('Server=tcp:127.0.0.1,1433;Database=project_db;Integrated Security=true;Encrypt=true;TrustServerCertificate=true;Connection Timeout=5')
try {
    $connection.Open()
    $command = $connection.CreateCommand()
    $command.CommandText = 'SELECT m.MatchId,m.Winner,m.Complete,m.EndReason,s.PlayerId,s.AccountId,s.GameServer,s.Kills,s.Deaths,s.Entries FROM dbo.PortfolioMatch m JOIN dbo.PortfolioMatchStats s ON m.Realm=s.Realm AND m.MatchId=s.MatchId WHERE m.Realm=@realm ORDER BY m.MatchId,s.PlayerId'
    $null = $command.Parameters.AddWithValue('@realm', $realm)
    $table = [Data.DataTable]::new()
    $reader = $command.ExecuteReader(); $table.Load($reader); $reader.Close(); $command.Dispose()
    $rows = @($table.Rows | Select-Object MatchId,Winner,Complete,EndReason,PlayerId,AccountId,GameServer,Kills,Deaths,Entries)
    $rows | Export-Csv -LiteralPath (Join-Path $report 'sql-match-stats.csv') -NoTypeInformation -Encoding UTF8
    $checked = 0
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $directory 'MatchResults') -Filter '*.xml') {
        [xml]$xml = [IO.File]::ReadAllText($file.FullName)
        $matchRows = @($rows | Where-Object MatchId -EQ $xml.match.id)
        if ($matchRows.Count -ne @($xml.match.player).Count) { throw 'SQL/XML participant count mismatch' }
        $byId = @{}; foreach ($row in $matchRows) { $byId[$row.PlayerId] = $row }
        foreach ($player in $xml.match.player) {
            $row = $byId[$player.id]
            if ($null -eq $row -or $row.AccountId -ne $player.name -or $row.GameServer -ne $player.server -or
                $row.Kills -ne [long]$player.kills -or $row.Deaths -ne [long]$player.deaths -or $row.Entries -ne [long]$player.entries -or
                $row.Winner -ne [int]$xml.match.winner -or [int]$row.Complete -ne [int]$xml.match.complete -or $row.EndReason -ne $xml.match.reason) {
                throw "SQL/XML result mismatch: $($player.id)"
            }
            ++$checked
        }
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $report $file.Name)
    }
    if ($checked -ne $rows.Count -or $checked -lt 1500) { throw 'Expected all 1500 player results' }
    "[OK] SQL/XML exact match: $checked participant rows. Realm=$realm"
    $rows | Group-Object GameServer | Select-Object Name,Count | Format-Table -AutoSize
} finally { $connection.Dispose() }

# 재현에 필요한 공개 설정과 로그만 보관한다. Cluster.local.ini 공유 키는 복사하지 않는다.
foreach ($name in @('Server.ini','result.txt','dummy.out.txt','dummy.err.txt','server-0.out.txt','server-1.out.txt','server-2.out.txt','server-3.out.txt','server-0.err.txt','server-1.err.txt','server-2.err.txt','server-3.err.txt')) {
    Copy-Item -LiteralPath (Join-Path $directory $name) -Destination (Join-Path $report $name)
}
New-Item -ItemType Directory -Path (Join-Path $report 'Data') -Force | Out-Null
Copy-Item -Path (Join-Path $directory 'Data/*') -Destination (Join-Path $report 'Data')
