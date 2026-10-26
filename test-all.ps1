param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
# 빌드 후 기능/장애 회귀를 순서대로 실행한다. 성능 수치는 별도 test-load 실행으로 측정한다.
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration -RunChecks -RunNetworkChecks
& (Join-Path $PSScriptRoot 'test-replay.ps1') -Configuration $Configuration
& (Join-Path $PSScriptRoot 'test-capacity.ps1') -Configuration $Configuration
& (Join-Path $PSScriptRoot 'test-account-skills.ps1') -Configuration $Configuration
& (Join-Path $PSScriptRoot 'test-auto-battle.ps1') -Configuration $Configuration
& (Join-Path $PSScriptRoot 'test-chat-relay.ps1') -Configuration $Configuration
& (Join-Path $PSScriptRoot 'test-recovery.ps1') -Configuration $Configuration
Write-Output "[OK] $Configuration full regression passed"
