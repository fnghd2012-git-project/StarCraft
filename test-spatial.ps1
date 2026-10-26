param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$binaryDirectory = Join-Path $PSScriptRoot "Run64\$Configuration"
$serverExe = Join-Path $binaryDirectory 'MultiThreadServer.exe'
& $serverExe --spatial-check
if ($LASTEXITCODE -ne 0) { throw 'Spatial equality check failed' }

# 실행 중인 서버 설정을 건드리지 않고 별도 복사본으로 입력 검증만 수행한다.
$directory = Join-Path $PSScriptRoot ('MultiThreadServer\MultiThreadServer\build\spatial-check-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $binaryDirectory 'Data') -Destination $directory -Recurse
Copy-Item -LiteralPath (Join-Path $binaryDirectory 'Cluster.local.ini') -Destination $directory
$source = [IO.File]::ReadAllText((Join-Path $binaryDirectory 'Server.ini'))
$path = Join-Path $directory 'Server.ini'
$cases = @(
    @{Key='SpatialSearch'; Value='Linear'; Pass=$true},
    @{Key='SpatialSearch'; Value='Grid'; Pass=$true},
    @{Key='SpatialSearch'; Value='Unknown'; Pass=$false},
    @{Key='SpatialCellSize'; Value='0'; Pass=$false},
    @{Key='SpatialCellSize'; Value='nan'; Pass=$false},
    @{Key='SpatialCellSize'; Value='1000001'; Pass=$false},
    @{Key='VerifySpatialSearch'; Value='2'; Pass=$false}
)
foreach ($case in $cases) {
    $config = [regex]::Replace($source, '(?m)^'+$case.Key+'=[^\r\n]*', $case.Key+'='+$case.Value)
    [IO.File]::WriteAllText($path, $config, [Text.UTF8Encoding]::new($false))
    $logPath = Join-Path $directory ($case.Key+'-'+$case.Value+'.txt')
    $process = Start-Process -FilePath $serverExe -ArgumentList @('--config-check','World','--config',('"'+$path+'"')) -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput $logPath -RedirectStandardError ($logPath+'.err')
    if (($process.ExitCode -eq 0) -ne $case.Pass) { throw "Unexpected config result: $($case.Key)=$($case.Value)" }
}
# 예전 INI 파일도 계속 읽을 수 있어야 한다. 생략하면 Linear/25/검증 끔으로 동작한다.
$config = [regex]::Replace($source, '(?m)^(SpatialSearch|SpatialCellSize|VerifySpatialSearch)=[^\r\n]*\r?\n', '')
[IO.File]::WriteAllText($path, $config, [Text.UTF8Encoding]::new($false))
$legacy = @(& $serverExe --config-check World --config $path)
if ($LASTEXITCODE -ne 0) { throw 'Legacy INI rejected' }
if (($legacy -join "`n") -notmatch '(?m)^SpatialSearch=Linear$') { throw 'Legacy INI default is not Linear' }
Write-Output "[OK] Spatial INI validation and legacy defaults. Logs: $directory"
