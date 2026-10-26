param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$python=Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
& $python (Join-Path $PSScriptRoot 'docs\results\2026-10-19\iocp\fault_check.py') --configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw 'IOCP lifetime / pool / disconnect checks failed' }
