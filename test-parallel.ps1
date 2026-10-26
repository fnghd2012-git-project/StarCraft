param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot "Run64\$Configuration\MultiThreadServer.exe") --battle-check
if ($LASTEXITCODE -ne 0) { throw 'Parallel calculation fixture failed' }
foreach($count in @(1,2,4)) {
    & (Join-Path $PSScriptRoot 'test-gameplay.ps1') -Configuration $Configuration -BattleWorkers $count -VerifyParallel
}
& (Join-Path $PSScriptRoot 'test-account-skills.ps1') -Configuration $Configuration -BattleWorkers 4 -VerifyParallel
Write-Output '[OK] Worker 1/2/4 gameplay; serial comparisons of movement/MP/Storm/snapshot bytes; skills regression'
