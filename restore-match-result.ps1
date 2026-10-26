param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference = 'Stop'
# DB 장애 때 남은 MatchResults/*.xml 한 경기를 개인 DB에 멱등하게 재적재한다.
$xml = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $Path))
$connection = [Data.SqlClient.SqlConnection]::new('Server=tcp:127.0.0.1,1433;Database=project_db;Integrated Security=true;Encrypt=true;TrustServerCertificate=true;Connection Timeout=5')
try {
    $connection.Open()
    $command = $connection.CreateCommand()
    try {
        $command.CommandText = 'EXEC dbo.PortfolioSaveMatch @payload'
        $null = $command.Parameters.Add('@payload', [Data.SqlDbType]::Xml)
        $command.Parameters['@payload'].Value = $xml
        $null = $command.ExecuteNonQuery()
        Write-Output '[OK] Match result restored (existing committed match is unchanged)'
    } finally { $command.Dispose() }
} finally { $connection.Dispose() }
