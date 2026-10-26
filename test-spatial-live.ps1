param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot ('.deps\spatial-live-'+[Guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$battle = Join-Path $PSScriptRoot "Run64\$Configuration\StartBattle.ps1"
$worldIndex = @(Import-Csv -LiteralPath (Join-Path $PSScriptRoot 'Data\GameServer.csv')).Count
foreach ($mode in @('Linear','Grid')) {
    # 자신의 포트/Realm/PID만 쓰며 설정 원본과 기존 실행 서버를 변경하지 않는다.
    $log = Join-Path $OutputDirectory ($mode+'-live.txt')
    & $battle -PerServer 40 -Seconds 35 -TestCooldownSeconds 3 -SpatialSearch $mode -VerifySpatialSearch -NoReplay *> $log
    $line = Get-Content -LiteralPath $log | Where-Object { $_ -match '^\[DIRECTORY\] ' } | Select-Object -Last 1
    if (-not $line) { throw "Missing battle directory: $mode" }
    $run = $line.Substring('[DIRECTORY] '.Length).Trim()
    $worldLog = Join-Path $run "server-$worldIndex.out.txt"
    $metrics = @(Get-Content -LiteralPath $worldLog | Where-Object { $_ -match '^\[METRICS\]' })
    [long]$queries = 0; [long]$verified = 0
    foreach ($entry in $metrics) {
        if ($entry -notmatch "spatial=$mode ") { throw 'Unexpected spatial mode' }
        if ($entry -match 'spatial_queries=(\d+)') { $queries += [long]$Matches[1] }
        if ($entry -match 'spatial_verified=(\d+)') { $verified += [long]$Matches[1] }
    }
    if ($queries -eq 0 -or $queries -ne $verified) { throw "Live spatial comparison incomplete: $mode $queries/$verified" }
    Copy-Item -LiteralPath $worldLog -Destination (Join-Path $OutputDirectory ($mode+'-world.txt'))
    Copy-Item -LiteralPath (Join-Path $run 'result.txt') -Destination (Join-Path $OutputDirectory ($mode+'-battle-result.txt'))
    Write-Output "[OK] Live spatial $mode verified=$verified; 120 bots, cooldown=3, battle=35s. Source: $run"
}
