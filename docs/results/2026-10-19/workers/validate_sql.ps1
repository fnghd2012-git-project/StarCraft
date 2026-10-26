param(
    [string]$SeriesDirectory = (Join-Path $PSScriptRoot 'matrix'),
    [string]$ReportDirectory = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'
$runs = @(Get-Content -LiteralPath (Join-Path $SeriesDirectory 'completed.json') -Raw | ConvertFrom-Json)
if ($runs.Count -ne 18) { throw 'Expected 18 completed worker comparison runs' }
$connection = [Data.SqlClient.SqlConnection]::new('Server=tcp:127.0.0.1,1433;Database=project_db;Integrated Security=true;Encrypt=true;TrustServerCertificate=true;Connection Timeout=5')
$summary = @()
try {
    $connection.Open()
    foreach ($run in $runs) {
        if (-not $run.realm.StartsWith('workers_')) { throw 'Expected isolated test Realm' }
        $command = $connection.CreateCommand()
        $command.CommandText = 'SELECT m.MatchId,m.Winner,m.Complete,m.EndReason,s.PlayerId,s.AccountId,s.GameServer,s.Kills,s.Deaths,s.Entries FROM dbo.PortfolioMatch m JOIN dbo.PortfolioMatchStats s ON m.Realm=s.Realm AND m.MatchId=s.MatchId WHERE m.Realm=@realm ORDER BY s.PlayerId'
        $null = $command.Parameters.AddWithValue('@realm', $run.realm)
        $table = [Data.DataTable]::new()
        $reader = $command.ExecuteReader(); $table.Load($reader); $reader.Close(); $command.Dispose()
        $directory = Join-Path $SeriesDirectory "r$($run.repeat)-n$($run.network)-d$($run.db)"
        $files = @(Get-ChildItem -LiteralPath $directory -Filter '*.xml')
        if ($files.Count -ne 1) { throw 'Expected one normally closed match per run' }
        [xml]$xml = [IO.File]::ReadAllText($files[0].FullName)
        $players = @($xml.match.player)
        if ($table.Rows.Count -ne 204 -or $players.Count -ne 204) { throw 'Expected 12 warmup + 96 session + 96 combat participants' }
        $byId = @{}; foreach ($row in $table.Rows) { $byId[$row.PlayerId] = $row }
        foreach ($player in $players) {
            $row = $byId[$player.id]
            if ($null -eq $row -or $row.MatchId -ne $xml.match.id -or $row.AccountId -ne $player.name -or $row.GameServer -ne $player.server -or
                $row.Kills -ne [long]$player.kills -or $row.Deaths -ne [long]$player.deaths -or $row.Entries -ne [long]$player.entries -or
                $row.Winner -ne 0 -or $row.Complete -or $row.EndReason -ne 'server_shutdown') { throw 'SQL/XML comparison failed' }
            if ($row.Kills -ne 0 -or $row.Deaths -ne 0 -or $row.Entries -ne 1) { throw 'Fixed-population fixture changed: death or reentry occurred' }
        }
        $summary += [pscustomobject]@{ Repeat=$run.repeat; Network=$run.network; DB=$run.db; Realm=$run.realm; MatchId=$xml.match.id; Rows=$players.Count; Equal=$true; Kills=0; Deaths=0; Entries=204 }
    }
} finally { $connection.Dispose() }
$summary | Export-Csv -LiteralPath (Join-Path $ReportDirectory 'sql-validation.csv') -Encoding UTF8 -NoTypeInformation
Write-Output "[OK] 18 runs, 3672 participant rows: XML/SQL exact match, no deaths, each entry counted once."
