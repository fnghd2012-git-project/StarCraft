param(
    [switch]$Pilot,
    [string]$PythonPath,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
if (-not $PythonPath) {
    $PythonPath = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
    if (-not (Test-Path -LiteralPath $PythonPath)) {
        $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
        if (-not $pythonCommand) { throw 'Python 경로를 -PythonPath로 지정하세요. 그래프 생성에는 ReportLab과 Poppler도 필요합니다.' }
        $PythonPath = $pythonCommand.Source
    }
}
if (@(Get-Process -Name MultiThreadServer,DummyClient -ErrorAction SilentlyContinue).Count -gt 0) {
    throw '다른 게임 서버/더미가 실행 중입니다. 비교 조건을 위해 종료한 뒤 실행하세요. 이 스크립트가 다른 실행을 종료하지 않습니다.'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $PSScriptRoot ('docs\results\' + (Get-Date -Format 'yyyy-MM-dd') + '\workers-' + (Get-Date -Format 'HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
}
if (-not [IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory = Join-Path $PSScriptRoot $OutputDirectory }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$sourceDirectory = Join-Path $PSScriptRoot 'docs\results\2026-10-19\workers'
$seriesDirectory = Join-Path $OutputDirectory $(if ($Pilot) { 'pilot' } else { 'matrix' })
$arguments = @((Join-Path $sourceDirectory 'run_benchmark.py'), '--output', $seriesDirectory)
if ($Pilot) { $arguments += '--pilot' }
$env:PYTHONIOENCODING = 'utf-8'
& $PythonPath @arguments
if ($LASTEXITCODE -ne 0) { throw "워커 비교 실패. 결과 폴더: $OutputDirectory" }
if (-not $Pilot) {
    & (Join-Path $sourceDirectory 'validate_sql.ps1') -SeriesDirectory $seriesDirectory -ReportDirectory $OutputDirectory
    & $PythonPath (Join-Path $sourceDirectory 'validate_artifacts.py') --matrix $seriesDirectory --report $OutputDirectory
    if ($LASTEXITCODE -ne 0) { throw 'Redis/리플레이 정합성 검증 실패' }
    & $PythonPath (Join-Path $sourceDirectory 'analyze.py') --matrix $seriesDirectory --report $OutputDirectory
    if ($LASTEXITCODE -ne 0) { throw '표본 분석/그래프 생성 실패' }
}
Write-Output "[OK] 워커 비교 결과: $OutputDirectory"
