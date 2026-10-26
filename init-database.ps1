param([string]$Database = 'project_db')
$ErrorActionPreference = 'Stop'
# 명시한 개인 로컬 DB에만 스키마를 추가한다. 기존 데이터와 다른 DB는 수정하지 않는다.
if ($Database -notmatch '^project_db(?:_test_[a-zA-Z0-9]+)?$') { throw 'Only the local portfolio database is supported' }
$connection = [System.Data.SqlClient.SqlConnection]::new("Server=tcp:127.0.0.1,1433;Database=$Database;Integrated Security=true;Encrypt=true;TrustServerCertificate=true;Connection Timeout=5")
try {
    $connection.Open()
    $sql = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Database\001_accounts.sql')) + "`nGO`n" + [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Database\002_rankings.sql'))
    foreach ($batch in [regex]::Split($sql, '(?m)^GO\s*$')) {
        if ([string]::IsNullOrWhiteSpace($batch)) { continue }
        $command = $connection.CreateCommand()
        try { $command.CommandText=$batch; $command.CommandTimeout=10; $null=$command.ExecuteNonQuery() }
        finally { $command.Dispose() }
    }
    Write-Output "[OK] Local $Database portfolio account/session/match-ranking schema installed"
}
finally { $connection.Dispose() }
