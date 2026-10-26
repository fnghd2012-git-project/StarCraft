param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$GameplayDirectory = ''
)
$ErrorActionPreference = 'Stop'
$dummy = Join-Path $PSScriptRoot "Run64\$Configuration\DummyClient.exe"
if (-not $GameplayDirectory) {
    $lines = @(& (Join-Path $PSScriptRoot 'test-gameplay.ps1') -Configuration $Configuration)
    $lines | Write-Output
    $result = $lines | Where-Object { $_ -match 'gameplay checks passed. Logs: (.+)$' } | Select-Object -Last 1
    if (-not $result) { throw 'Gameplay fixture failed' }
    $null = $result -match 'Logs: (.+)$'; $GameplayDirectory = $Matches[1]
}
$GameplayDirectory = (Resolve-Path -LiteralPath $GameplayDirectory).Path
$files = @(Get-ChildItem -LiteralPath (Join-Path $GameplayDirectory 'Replays') -Filter '*.replay')
if ($files.Count -lt 2) { throw 'Expected finished match and next match recording' }
$matched = 0; $finished = 0
foreach ($file in $files) {
    $text = (& $dummy --replay $file.FullName --verify) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw $text }
    Write-Output $text
    if ($text -match 'reason=round_finished winner=[123]') { ++$finished }
    $compare = (& $dummy --replay $file.FullName --verify --compare (Join-Path $GameplayDirectory 'live-state.pb')) -join "`n"
    if ($LASTEXITCODE -eq 0) { ++$matched; Write-Output $compare }
    elseif ($compare -notmatch 'Live tick not in this match') { throw $compare }
}
if ($finished -lt 1 -or $matched -ne 1) { throw 'Winner/live-state comparison failed' }
$directory = Join-Path $GameplayDirectory 'replay-validation'
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$source = $files[0].FullName
$bytes = [IO.File]::ReadAllBytes($source)
# 로그인 패킷을 통째로 기록하지 않았는지 알려진 테스트 비밀번호로도 확인한다.
if ([Text.Encoding]::UTF8.GetString($bytes).Contains('PortfolioTest!2026')) { throw 'Password leaked to replay' }
function Check-Rejected([string]$Name, [byte[]]$Data, [bool]$AllowPartial = $false) {
    $path = Join-Path $directory $Name; [IO.File]::WriteAllBytes($path, $Data)
    $arguments = @('--replay', $path, '--verify'); if ($AllowPartial) { $arguments += '--allow-partial' }
    $text = (& $dummy @arguments) -join "`n"
    if ($LASTEXITCODE -eq 0) { throw "Malformed replay accepted: $Name" }
    Write-Output "[OK] Rejected $Name : $text"
}
$corrupt = [byte[]]$bytes.Clone(); $corrupt[30] = $corrupt[30] -bxor 1
Check-Rejected 'corrupt.replay' $corrupt
Check-Rejected 'corrupt-partial.replay.part' $corrupt $true
$magic = [byte[]]$bytes.Clone(); $magic[0] = 0
Check-Rejected 'magic.replay' $magic
$oversize = [byte[]]$bytes.Clone(); for ($i = 8; $i -lt 12; ++$i) { $oversize[$i] = 255 }
Check-Rejected 'oversize.replay' $oversize
$truncated = [byte[]]$bytes[0..($bytes.Length - 6)]
Check-Rejected 'truncated.replay.part' $truncated
$partial = (& $dummy --replay (Join-Path $directory 'truncated.replay.part') --verify --allow-partial) -join "`n"
if ($LASTEXITCODE -ne 0 -or $partial -notmatch 'complete=0' -or $partial -notmatch '\[PARTIAL\]') { throw "Partial recovery failed: $partial" }
Write-Output '[OK] Truncated tail: normal load rejected; explicit partial mode recovered validated frames'
Check-Rejected 'trailing.replay' ([byte[]]($bytes + @(0))) $true
# 완성 파일만 있으면 현재 CSV와 DB/서버 없이 재생할 수 있어야 한다.
$standalone = Join-Path $directory 'standalone.replay'; Copy-Item -LiteralPath $source -Destination $standalone
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $dummy; $start.Arguments = '--replay "' + $standalone + '" --console'; $start.WorkingDirectory = $directory
$start.UseShellExecute = $false; $start.CreateNoWindow = $true
$start.RedirectStandardInput = $true; $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
$process = [Diagnostics.Process]::new(); $process.StartInfo = $start
$oldEncoding = [Console]::InputEncoding
try {
    [Console]::InputEncoding = [Text.UTF8Encoding]::new($false)
    $null = $process.Start(); $output = $process.StandardOutput.ReadToEndAsync(); $errorOutput = $process.StandardError.ReadToEndAsync()
    $writer = [IO.StreamWriter]::new($process.StandardInput.BaseStream, [Text.UTF8Encoding]::new($false)); $writer.AutoFlush = $true
    foreach ($line in @('speed 2','play','pause','seek 0','step 20','status','events','speed 0','seek -1','quit')) { $writer.WriteLine($line) }
    $writer.Dispose()
    if (-not $process.WaitForExit(10000)) { throw 'Replay menu timed out' }
    $text = $output.Result; [IO.File]::WriteAllText((Join-Path $directory 'menu.txt'), $text)
    foreach ($marker in @('[REPLAY] Speed=2','[REPLAY] Playing','[REPLAY] Paused','[STATE]','[DENIED] Speed','[DENIED] Seek')) {
        if (-not $text.Contains($marker)) { throw "Replay menu missing: $marker / $text" }
    }
    if ($process.ExitCode -ne 0) { throw $errorOutput.Result }
}
finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose(); [Console]::InputEncoding = $oldEncoding
}
Write-Output "[OK] $Configuration replay checks passed: winner, exact live state, corruption, truncation, offline controls. Logs: $directory"
